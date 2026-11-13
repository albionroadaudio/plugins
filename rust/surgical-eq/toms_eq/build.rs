// Fedora 44 ships glibc 2.43, which gave acosf/atan2f new symbol versions.
// Bitwig's Flatpak runtime (freedesktop 25.08) only has glibc 2.42, so a
// plugin linked against the new versions fails to load there. Redirect those
// calls to wrappers in src/compat.rs that use the long-standing f64 versions.
fn main() {
    if std::env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("linux") {
        for sym in ["acosf", "atan2f"] {
            println!("cargo:rustc-cdylib-link-arg=-Wl,--wrap={sym}");
        }
    }
}
