//! Dark/light theme shared by every Albion Road Audio plugin.
//!
//! The JUCE plugins keep the choice in `~/.config/Albion Road Audio/settings.xml` as
//! `<AlbionRoadAudioSettings theme="dark|light"/>`. This reads and writes the same file, so
//! switching the theme in any plugin switches it for all of them (default: dark).
//! `ALBION_THEME=light` overrides it, which is handy for screenshots.

use std::path::PathBuf;
use std::sync::atomic::{AtomicU8, Ordering};

const UNLOADED: u8 = 0;
const DARK: u8 = 1;
const LIGHT: u8 = 2;

static THEME: AtomicU8 = AtomicU8::new(UNLOADED);

fn settings_file() -> Option<PathBuf> {
    let config = std::env::var_os("HOME").map(|h| PathBuf::from(h).join(".config"))?;
    Some(config.join("Albion Road Audio").join("settings.xml"))
}

fn load() -> bool {
    let mut dark = settings_file()
        .and_then(|f| std::fs::read_to_string(f).ok())
        .map_or(true, |xml| !xml.contains("theme=\"light\""));
    if let Some(env) = std::env::var_os("ALBION_THEME") {
        dark = env != "light";
    }
    dark
}

/// Whether the shared theme is dark.
pub fn is_dark() -> bool {
    match THEME.load(Ordering::Relaxed) {
        DARK => true,
        LIGHT => false,
        _ => {
            let dark = load();
            THEME.store(if dark { DARK } else { LIGHT }, Ordering::Relaxed);
            dark
        }
    }
}

/// Switch the shared theme and save it for every Albion Road Audio plugin.
pub fn set_dark(dark: bool) {
    THEME.store(if dark { DARK } else { LIGHT }, Ordering::Relaxed);
    let Some(file) = settings_file() else { return };
    let theme = if dark { "dark" } else { "light" };
    let xml = match std::fs::read_to_string(&file) {
        Ok(old) if old.contains("theme=\"") => {
            let start = old.find("theme=\"").unwrap() + 7;
            let end = old[start..].find('"').map_or(old.len(), |e| start + e);
            format!("{}{}{}", &old[..start], theme, &old[end..])
        }
        _ => format!("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\n<AlbionRoadAudioSettings theme=\"{theme}\"/>\n"),
    };
    if let Some(dir) = file.parent() {
        let _ = std::fs::create_dir_all(dir);
    }
    let _ = std::fs::write(file, xml);
}
