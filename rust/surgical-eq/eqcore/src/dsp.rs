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

/// Most biquad sections a high or low cut can use (4 = 8th order, 48 dB/oct).
pub const MAX_CUT_SECTIONS: usize = 4;

/// Q of section `k` (0-based) of a Butterworth filter built from `sections` biquads
/// (order 2 x sections, so 12 dB/oct per section).
pub fn butter_q(sections: usize, k: usize) -> f64 {
    let order = 2.0 * sections as f64;
    1.0 / (2.0 * ((2.0 * k as f64 + 1.0) * PI / (2.0 * order)).cos())
}

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

    /// "Analog" bell: the same shape as the analog peaking filter all the way up to 20 kHz.
    ///
    /// The standard (RBJ, bilinear) bell gets squeezed narrower and lopsided as it nears
    /// Nyquist. This one places the poles by impulse invariance and solves the zeros so the
    /// magnitude matches the analog prototype at DC and peaks at exactly `db` at `f`
    /// (M. Vicanek, "Matched Second Order Digital Filters", 2016).
    pub fn analog_peak(fs: f64, f: f64, q: f64, db: f64) -> Self {
        if db.abs() < 1e-6 {
            return Self::IDENTITY;
        }
        let a = 10f64.powf(db / 40.0); // analog prototype: (s² + s·A/Q + 1) / (s² + s/(A·Q) + 1)
        let g2 = 10f64.powf(db / 10.0); // |H|² at the centre
        let w = 2.0 * PI * f.clamp(1.0, fs * 0.49) / fs;
        let zeta = 1.0 / (2.0 * a * q);
        let (a1, a2) = if zeta <= 1.0 {
            (-2.0 * (-zeta * w).exp() * ((1.0 - zeta * zeta).sqrt() * w).cos(), (-2.0 * zeta * w).exp())
        } else {
            (-2.0 * (-zeta * w).exp() * ((zeta * zeta - 1.0).sqrt() * w).cosh(), (-2.0 * zeta * w).exp())
        };
        // |H(e^jw)|² = (B0 φ0 + B1 φ1 + B2 φ2) / (A0 φ0 + A1 φ1 + A2 φ2),
        // φ0 = cos²(w/2), φ1 = sin²(w/2), φ2 = 4 φ0 φ1
        let (big_a0, big_a1, big_a2) = ((1.0 + a1 + a2).powi(2), (1.0 - a1 + a2).powi(2), -4.0 * a2);
        let phi1 = (w / 2.0).sin().powi(2);
        let phi0 = 1.0 - phi1;
        let phi2 = 4.0 * phi0 * phi1;
        let r1 = (big_a0 * phi0 + big_a1 * phi1 + big_a2 * phi2) * g2;
        let r2 = (-big_a0 + big_a1 + 4.0 * (phi0 - phi1) * big_a2) * g2;
        let big_b0 = big_a0;
        let big_b2 = (r1 - r2 * phi1 - big_b0) / (4.0 * phi1 * phi1);
        let big_b1 = r2 + big_b0 + 4.0 * (phi1 - phi0) * big_b2;
        let (s0, s1) = (big_b0.max(0.0).sqrt(), big_b1.max(0.0).sqrt());
        let wsum = 0.5 * (s0 + s1);
        let b0 = 0.5 * (wsum + (wsum * wsum + big_b2).max(0.0).sqrt());
        let b1 = 0.5 * (s0 - s1);
        let b2 = -big_b2 / (4.0 * b0);
        Coefs { b0, b1, b2, a1, a2 }
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

    /// Magnitude of the analog peaking prototype, dB.
    fn analog_peak_db(f0: f64, q: f64, db: f64, f: f64) -> f64 {
        let a = 10f64.powf(db / 40.0);
        let o = f / f0;
        let re = (1.0 - o * o).powi(2);
        10.0 * ((re + (a * o / q).powi(2)) / (re + (o / (a * q)).powi(2))).log10()
    }

    fn worst_error(c: &Coefs, fs: f64, f0: f64, q: f64, db: f64) -> f64 {
        (0..=200)
            .map(|i| 20.0 * 1000f64.powf(i as f64 / 200.0)) // 20 Hz .. 20 kHz
            .map(|f| (c.db(fs, f) - analog_peak_db(f0, q, db, f)).abs())
            .fold(0.0, f64::max)
    }

    #[test]
    fn analog_peak_matches_the_analog_shape() {
        for &fs in &[44100.0, 48000.0, 96000.0] {
            for &(f0, q, db) in &[(250.0, 1.0, 6.0), (3500.0, 1.2, -6.0), (10000.0, 0.9, 6.0), (10000.0, 0.9, -6.0), (4000.0, 8.0, -12.0)] {
                let c = Coefs::analog_peak(fs, f0, q, db);
                assert!((c.db(fs, f0) - db).abs() < 0.01, "centre gain fs {fs} f0 {f0}: {}", c.db(fs, f0));
                assert!(c.db(fs, 1.0).abs() < 0.01, "DC fs {fs} f0 {f0}");
                let err = worst_error(&c, fs, f0, q, db);
                // within 0.75 dB everywhere up to 20 kHz (the last 0.5 dB or so is in the top octave at 44.1 kHz)
                assert!(err < 0.75, "fs {fs} f0 {f0} q {q} db {db}: worst error {err:.2} dB");
            }
        }
        // ...where the standard bell is squeezed near the top at 44.1/48 kHz
        let rbj = Coefs::peak(48000.0, 10000.0, 0.9, 6.0);
        let matched = Coefs::analog_peak(48000.0, 10000.0, 0.9, 6.0);
        assert!(worst_error(&rbj, 48000.0, 10000.0, 0.9, 6.0) > 2.0 * worst_error(&matched, 48000.0, 10000.0, 0.9, 6.0));
    }

    #[test]
    fn butterworth_q_table() {
        assert!((butter_q(1, 0) - BUTTER_Q2).abs() < 1e-12);
        assert!((butter_q(2, 0) - BUTTER_Q4[0]).abs() < 1e-12 && (butter_q(2, 1) - BUTTER_Q4[1]).abs() < 1e-12);
        // 8th order: -3 dB at the cutoff and 48 dB per octave below it
        let fs = 48000.0;
        let hp = |f: f64| -> f64 { (0..4).map(|k| Coefs::highpass(fs, 70.0, butter_q(4, k)).db(fs, f)).sum() };
        assert!((hp(70.0) + 3.01).abs() < 0.05, "{}", hp(70.0));
        assert!((hp(17.5) - hp(35.0) + 48.0).abs() < 1.0, "{}", hp(17.5) - hp(35.0));
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

