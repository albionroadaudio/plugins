//! Standalone build, for previewing the GUI without a host:
//! cargo run -p drs_grooves --features standalone --bin drs_grooves_standalone -- --backend dummy
use drs_grooves::DrsGrooves;
use nih_plug::prelude::*;

fn main() {
    nih_export_standalone::<DrsGrooves>();
}
