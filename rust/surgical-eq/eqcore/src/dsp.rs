//! Biquad filters (RBJ Audio EQ Cookbook), computed and run in f64.

use std::f64::consts::PI;

/// Normalised biquad coefficients (a0 = 1).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Coefs {
    pub b0: f64,
    pub b1: f64,
    pub b2: f64,
    pub a1: f64,
    pub a2: f64,
}

impl Default for Coefs {
    fn default() -> Self {
        Self::IDENTITY
    }
}

/// Q values for Butterworth sections.
pub const BUTTER_Q2: f64 = std::f64::consts::FRAC_1_SQRT_2;
pub const BUTTER_Q4: [f64; 2] = [0.541_196_100_146_197, 1.306_562_964_876_376_5];

impl Coefs {
    pub const IDENTITY: Coefs = Coefs { b0: 1.0, b1: 0.0, b2: 0.0, a1: 0.0, a2: 0.0 };

    fn norm(b0: f64, b1: f64, b2: f64, a0: f64, a1: f64, a2: f64) -> Self {
        Coefs { b0: b0 / a0, b1: b1 / a0, b2: b2 / a0, a1: a1 / a0, a2: a2 / a0 }
    }

    fn w0(fs: f64, f: f64) -> f64 {
        2.0 * PI * f.clamp(1.0, fs * 0.49) / fs
    }

    pub fn peak(fs: f64, f: f64, q: f64, db: f64) -> Self {
        if db.abs() < 1e-6 {
            return Self::IDENTITY;
        }
        let a = 10f64.powf(db / 40.0);
        let w = Self::w0(fs, f);
        let (s, c) = w.sin_cos();
        let al = s / (2.0 * q);
        Self::norm(1.0 + al * a, -2.0 * c, 1.0 - al * a, 1.0 + al / a, -2.0 * c, 1.0 - al / a)
    }

    pub fn low_shelf(fs: f64, f: f64, q: f64, db: f64) -> Self {
        if db.abs() < 1e-6 {
            return Self::IDENTITY;
        }
        let a = 10f64.powf(db / 40.0);
        let w = Self::w0(fs, f);
        let (s, c) = w.sin_cos();
        let al = s / (2.0 * q);
        let k = 2.0 * a.sqrt() * al;
        Self::norm(
            a * ((a + 1.0) - (a - 1.0) * c + k),
            2.0 * a * ((a - 1.0) - (a + 1.0) * c),
            a * ((a + 1.0) - (a - 1.0) * c - k),
            (a + 1.0) + (a - 1.0) * c + k,
            -2.0 * ((a - 1.0) + (a + 1.0) * c),
            (a + 1.0) + (a - 1.0) * c - k,
        )
    }

    pub fn high_shelf(fs: f64, f: f64, q: f64, db: f64) -> Self {
        if db.abs() < 1e-6 {
            return Self::IDENTITY;
        }
        let a = 10f64.powf(db / 40.0);
        let w = Self::w0(fs, f);
        let (s, c) = w.sin_cos();
        let al = s / (2.0 * q);
        let k = 2.0 * a.sqrt() * al;
        Self::norm(
            a * ((a + 1.0) + (a - 1.0) * c + k),
            -2.0 * a * ((a - 1.0) + (a + 1.0) * c),
            a * ((a + 1.0) + (a - 1.0) * c - k),
            (a + 1.0) - (a - 1.0) * c + k,
            2.0 * ((a - 1.0) - (a + 1.0) * c),
            (a + 1.0) - (a - 1.0) * c - k,
        )
    }

    pub fn highpass(fs: f64, f: f64, q: f64) -> Self {
        let w = Self::w0(fs, f);
        let (s, c) = w.sin_cos();
        let al = s / (2.0 * q);
        Self::norm((1.0 + c) / 2.0, -(1.0 + c), (1.0 + c) / 2.0, 1.0 + al, -2.0 * c, 1.0 - al)
    }

    pub fn lowpass(fs: f64, f: f64, q: f64) -> Self {
        let w = Self::w0(fs, f);
        let (s, c) = w.sin_cos();
        let al = s / (2.0 * q);
        Self::norm((1.0 - c) / 2.0, 1.0 - c, (1.0 - c) / 2.0, 1.0 + al, -2.0 * c, 1.0 - al)
    }

    /// Squared magnitude of the response at `f` Hz.
    pub fn mag2(&self, fs: f64, f: f64) -> f64 {
        let w = 2.0 * PI * f / fs;
        let (s1, c1) = w.sin_cos();
        let (s2, c2) = (2.0 * w).sin_cos();
        let nr = self.b0 + self.b1 * c1 + self.b2 * c2;
        let ni = -(self.b1 * s1 + self.b2 * s2);
        let dr = 1.0 + self.a1 * c1 + self.a2 * c2;
        let di = -(self.a1 * s1 + self.a2 * s2);
        (nr * nr + ni * ni) / (dr * dr + di * di).max(1e-30)
    }

    pub fn db(&self, fs: f64, f: f64) -> f64 {
        10.0 * self.mag2(fs, f).max(1e-24).log10()
    }
}

/// Transposed direct form II state for one channel.
#[derive(Clone, Copy, Default, Debug)]
pub struct State {
    z1: f64,
    z2: f64,
}

impl State {
    #[inline]
    pub fn tick(&mut self, c: &Coefs, x: f64) -> f64 {
        let y = c.b0 * x + self.z1;
        self.z1 = c.b1 * x - c.a1 * y + self.z2;
        self.z2 = c.b2 * x - c.a2 * y;
        y
    }
}

/// One-pole smoother run once per processing chunk.
#[derive(Clone, Copy, Default, Debug)]
pub struct Smooth {
    pub value: f64,
}

impl Smooth {
    /// `k` is the per-chunk coefficient from [`chunk_coef`].
    #[inline]
    pub fn step(&mut self, target: f64, k: f64) -> f64 {
        self.value += (target - self.value) * k;
        if (target - self.value).abs() < 1e-7 {
            self.value = target;
        }
        self.value
    }
}

/// Smoothing coefficient for a chunk of `len` samples with time constant `tau` seconds.
pub fn chunk_coef(len: usize, fs: f64, tau: f64) -> f64 {
    1.0 - (-(len as f64) / (tau * fs)).exp()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn peak_hits_its_gain_at_centre() {
        let c = Coefs::peak(48000.0, 400.0, 1.8, -6.0);
        assert!((c.db(48000.0, 400.0) + 6.0).abs() < 0.01);
        assert!(c.db(48000.0, 20.0).abs() < 0.1);
        assert!(c.db(48000.0, 15000.0).abs() < 0.1);
    }

    #[test]
    fn butterworth_is_minus_3db_at_cutoff() {
        let lp = Coefs::lowpass(48000.0, 15000.0, BUTTER_Q2);
        assert!((lp.db(48000.0, 15000.0) + 3.01).abs() < 0.05);
        let hp: f64 = BUTTER_Q4.iter().map(|&q| Coefs::highpass(48000.0, 20.0, q).db(48000.0, 20.0)).sum();
        assert!((hp + 3.01).abs() < 0.05, "{hp}");
    }

    #[test]
    fn state_matches_magnitude() {
        // Drive a 400 Hz sine through a -6 dB peak and measure the steady-state level.
        let fs = 48000.0;
        let c = Coefs::peak(fs, 400.0, 1.8, -6.0);
        let mut s = State::default();
        let mut peak: f64 = 0.0;
        for i in 0..48000 {
            let y = s.tick(&c, (2.0 * PI * 400.0 * i as f64 / fs).sin());
            if i > 24000 {
                peak = peak.max(y.abs());
            }
        }
        assert!((20.0 * peak.log10() + 6.0).abs() < 0.05);
    }
}
