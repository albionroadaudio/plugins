//! Spectrum analyser. The audio thread writes a mono sum of the input (pre-EQ) and the
//! output (post-EQ) into lock-free rings; the GUI windows and FFTs the latest block.

use std::sync::atomic::{AtomicU32, AtomicU64, AtomicUsize, Ordering};

pub const FFT_N: usize = 8192;
const MASK: usize = FFT_N - 1;
/// Display floor, dB.
pub const FLOOR_DB: f32 = -96.0;
/// Spectrum tilt so pink-ish material reads flat, dB per octave around 1 kHz.
pub const TILT_DB_OCT: f32 = 4.5;

/// Written by the audio thread, read by the GUI.
pub struct Shared {
    /// f64 bits of the current sample rate (0 until initialised).
    pub sample_rate: AtomicU64,
    pre: Box<[AtomicU32]>,
    post: Box<[AtomicU32]>,
    /// Total samples written.
    pos: AtomicUsize,
}

impl Default for Shared {
    fn default() -> Self {
        let ring = || (0..FFT_N).map(|_| AtomicU32::new(0)).collect::<Vec<_>>().into_boxed_slice();
        Self { sample_rate: AtomicU64::new(0), pre: ring(), post: ring(), pos: AtomicUsize::new(0) }
    }
}

impl Shared {
    pub fn sample_rate(&self) -> f64 {
        let v = f64::from_bits(self.sample_rate.load(Ordering::Relaxed));
        if v > 0.0 { v } else { 48000.0 }
    }

    /// Audio thread: current write position, to pass to [`write`](Self::write) and [`commit`](Self::commit).
    #[inline]
    pub fn cursor(&self) -> usize {
        self.pos.load(Ordering::Relaxed)
    }

    #[inline]
    pub fn write(&self, at: usize, pre: f32, post: f32) {
        self.pre[at & MASK].store(pre.to_bits(), Ordering::Relaxed);
        self.post[at & MASK].store(post.to_bits(), Ordering::Relaxed);
    }

    #[inline]
    pub fn commit(&self, pos: usize) {
        self.pos.store(pos, Ordering::Release);
    }
}

/// GUI-side state: FFT scratch plus smoothed per-bin levels in dB.
pub struct Analyzer {
    window: Vec<f32>,
    twiddle: Vec<(f32, f32)>,
    re: Vec<f32>,
    im: Vec<f32>,
    pub pre: Vec<f32>,
    pub post: Vec<f32>,
    last_pos: usize,
}

impl Default for Analyzer {
    fn default() -> Self {
        use std::f64::consts::PI;
        let window = (0..FFT_N).map(|i| (0.5 - 0.5 * (2.0 * PI * i as f64 / FFT_N as f64).cos()) as f32).collect();
        let twiddle = (0..FFT_N / 2)
            .map(|k| {
                let a = -2.0 * PI * k as f64 / FFT_N as f64;
                (a.cos() as f32, a.sin() as f32)
            })
            .collect();
        Self {
            window,
            twiddle,
            re: vec![0.0; FFT_N],
            im: vec![0.0; FFT_N],
            pre: vec![FLOOR_DB; FFT_N / 2],
            post: vec![FLOOR_DB; FFT_N / 2],
            last_pos: 0,
        }
    }
}

impl Analyzer {
    /// Refresh from the audio thread's rings. `dt` is seconds since the last call.
    pub fn update(&mut self, shared: &Shared, dt: f32) {
        let pos = shared.pos.load(Ordering::Acquire);
        let fresh = pos != self.last_pos;
        self.last_pos = pos;
        // Rise quickly, fall at ~40 dB/s; with no new audio, everything falls away.
        let fall = 40.0 * dt.clamp(0.0, 0.1);
        for (ring, which) in [(&shared.pre, 0), (&shared.post, 1)] {
            if fresh {
                for i in 0..FFT_N {
                    let s = f32::from_bits(ring[pos.wrapping_sub(FFT_N).wrapping_add(i) & MASK].load(Ordering::Relaxed));
                    self.re[i] = s * self.window[i];
                    self.im[i] = 0.0;
                }
                fft(&mut self.re, &mut self.im, &self.twiddle);
            }
            let out = if which == 0 { &mut self.pre } else { &mut self.post };
            let norm = 4.0 / FFT_N as f32;
            for (k, o) in out.iter_mut().enumerate() {
                let new = if fresh {
                    let m = (self.re[k] * self.re[k] + self.im[k] * self.im[k]).sqrt() * norm;
                    ((20.0 * (m as f64).max(1e-9).log10()) as f32).max(FLOOR_DB)
                } else {
                    FLOOR_DB
                };
                *o = if new > *o { *o + (new - *o) * 0.6 } else { (*o - fall).max(new) };
            }
        }
    }

    /// Level in dB (tilted) of the loudest bin between `f0` and `f1` Hz.
    pub fn level(bins: &[f32], fs: f64, f0: f64, f1: f64) -> f32 {
        let hz_per_bin = fs / FFT_N as f64;
        let k0 = f0 / hz_per_bin;
        let k1 = f1 / hz_per_bin;
        let last = bins.len() - 1;
        let raw = if k1.floor() > k0.ceil() {
            bins[(k0.ceil() as usize).min(last)..=(k1.floor() as usize).min(last)].iter().copied().fold(FLOOR_DB, f32::max)
        } else {
            // Less than one bin per pixel: interpolate.
            let k = (k0 + k1) / 2.0;
            let i = (k.floor() as usize).min(last - 1);
            let t = (k - i as f64).clamp(0.0, 1.0) as f32;
            bins[i] * (1.0 - t) + bins[i + 1] * t
        };
        if raw <= FLOOR_DB + 0.5 {
            return FLOOR_DB;
        }
        let fc = ((f0 * f1).sqrt() / 1000.0) as f32;
        raw + TILT_DB_OCT * (fc as f64).max(1e-3).log2() as f32
    }
}

/// In-place iterative radix-2 complex FFT.
fn fft(re: &mut [f32], im: &mut [f32], tw: &[(f32, f32)]) {
    let n = re.len();
    let mut j = 0;
    for i in 1..n {
        let mut bit = n >> 1;
        while j & bit != 0 {
            j ^= bit;
            bit >>= 1;
        }
        j |= bit;
        if i < j {
            re.swap(i, j);
            im.swap(i, j);
        }
    }
    let mut len = 2;
    while len <= n {
        let step = n / len;
        let half = len / 2;
        for s in (0..n).step_by(len) {
            for k in 0..half {
                let (wr, wi) = tw[k * step];
                let (a, b) = (s + k, s + k + half);
                let xr = re[b] * wr - im[b] * wi;
                let xi = re[b] * wi + im[b] * wr;
                re[b] = re[a] - xr;
                im[b] = im[a] - xi;
                re[a] += xr;
                im[a] += xi;
            }
        }
        len <<= 1;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn full_scale_sine_reads_0db_at_its_bin() {
        let shared = Shared::default();
        let fs = 48000.0;
        let f = 1000.0;
        for i in 0..FFT_N {
            let s = (2.0 * std::f64::consts::PI * f * i as f64 / fs).sin() as f32;
            shared.write(i, s, s * 0.5);
        }
        shared.commit(FFT_N);
        let mut a = Analyzer::default();
        for _ in 0..20 {
            a.last_pos = 0;
            a.update(&shared, 0.016);
        }
        let k = (f / (fs / FFT_N as f64)).round() as usize;
        let peak = a.pre.iter().copied().fold(FLOOR_DB, f32::max);
        assert!((a.pre[k] - peak).abs() < 1.0 && peak > -1.5 && peak < 0.5, "pre {peak}");
        let post = a.post.iter().copied().fold(FLOOR_DB, f32::max);
        assert!((post + 6.0).abs() < 1.0, "post {post}");
        assert!(a.pre[k * 3] < -60.0);
    }
}
