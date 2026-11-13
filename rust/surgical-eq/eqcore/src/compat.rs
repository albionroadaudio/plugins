//! Link-time replacements for libm functions whose newest glibc symbol
//! versions are missing from older runtimes (see build.rs).

#[no_mangle]
pub extern "C" fn __wrap_acosf(x: f32) -> f32 {
    (x as f64).acos() as f32
}

#[no_mangle]
pub extern "C" fn __wrap_atan2f(y: f32, x: f32) -> f32 {
    (y as f64).atan2(x as f64) as f32
}
