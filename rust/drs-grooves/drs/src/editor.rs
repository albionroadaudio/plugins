//! Plugin window, styled after albionroadaudio.co.uk: paper/ink palette with a
//! terracotta accent, heavy letter-spaced wordmark, mono eyebrow labels, serif
//! italic accents and square-cornered controls. Dark by default.

use crate::{kit, library, DrsParams, Shared};
use drumlib::{smf, Kind, Library, Pattern};
use nih_plug::prelude::*;
use nih_plug_egui::egui::{
    self, text::LayoutJob, Align, Align2, Color32, CornerRadius, FontData, FontDefinitions, FontFamily, FontId,
    Layout, Pos2, Rect, Sense, Stroke, StrokeKind, TextFormat, Vec2,
};
use nih_plug_egui::{create_egui_editor, widgets};
use std::sync::atomic::Ordering;
use std::sync::Arc;

pub const WIDTH: u32 = 1000;
pub const HEIGHT: u32 = 660;

#[derive(Clone, Copy)]
struct Palette {
    bg: Color32,
    surface: Color32,
    text: Color32,
    muted: Color32,
    line: Color32,
    accent: Color32,
    /// Accent for small text: the site's darker accent on paper, lighter on dark.
    eyebrow: Color32,
    olive: Color32,
    /// Inverted button (site's `.button-dark` / `.button-light`).
    btn_bg: Color32,
    btn_text: Color32,
    btn_hover: Color32,
}

// Values from albionroadaudio.co.uk/styles.css
const PAPER: Color32 = Color32::from_rgb(0xf3, 0xf0, 0xe7);
const PAPER_DEEP: Color32 = Color32::from_rgb(0xe8, 0xe2, 0xd5);
const INK: Color32 = Color32::from_rgb(0x24, 0x24, 0x1f);
const ACCENT: Color32 = Color32::from_rgb(0xb8, 0x5f, 0x3e);
const ACCENT_DARK: Color32 = Color32::from_rgb(0x91, 0x45, 0x2f);
const OLIVE: Color32 = Color32::from_rgb(0x73, 0x76, 0x5d);

const LIGHT: Palette = Palette {
    bg: PAPER,
    surface: PAPER_DEEP,
    text: INK,
    muted: Color32::from_rgb(0x6b, 0x69, 0x5f),
    line: Color32::from_rgb(0xd8, 0xd1, 0xc3),
    accent: ACCENT,
    eyebrow: ACCENT_DARK,
    olive: OLIVE,
    btn_bg: INK,
    btn_text: PAPER,
    btn_hover: ACCENT_DARK,
};

const DARK: Palette = Palette {
    bg: Color32::from_rgb(0x25, 0x27, 0x21),
    surface: Color32::from_rgb(0x33, 0x36, 0x2e),
    text: PAPER,
    muted: Color32::from_rgb(0xaa, 0xa9, 0x9e),
    line: Color32::from_rgb(0x48, 0x4a, 0x42),
    accent: ACCENT,
    eyebrow: Color32::from_rgb(0xd2, 0x8a, 0x69),
    olive: Color32::from_rgb(0x9a, 0x9e, 0x7c),
    btn_bg: PAPER,
    btn_text: INK,
    btn_hover: Color32::from_rgb(0xe6, 0xc4, 0xae),
};

#[derive(Default)]
struct UiState {
    genre: Option<usize>,
    status: String,
    show_map: bool,
}

fn sans() -> FontFamily {
    FontFamily::Proportional
}
fn bold() -> FontFamily {
    FontFamily::Name("bold".into())
}
fn serif_italic() -> FontFamily {
    FontFamily::Name("serif-italic".into())
}
fn mono() -> FontFamily {
    FontFamily::Monospace
}

fn install_fonts(ctx: &egui::Context) {
    let mut f = FontDefinitions::default();
    let add = |f: &mut FontDefinitions, name: &str, bytes: &'static [u8]| {
        f.font_data.insert(name.to_owned(), Arc::new(FontData::from_static(bytes)));
    };
    add(&mut f, "sans", include_bytes!("../assets/fonts/LiberationSans-Regular.ttf"));
    add(&mut f, "sans-bold", include_bytes!("../assets/fonts/LiberationSans-Bold.ttf"));
    add(&mut f, "serif-italic", include_bytes!("../assets/fonts/LiberationSerif-Italic.ttf"));
    add(&mut f, "mono", include_bytes!("../assets/fonts/LiberationMono-Regular.ttf"));
    f.families.insert(FontFamily::Proportional, vec!["sans".into()]);
    f.families.insert(FontFamily::Monospace, vec!["mono".into()]);
    f.families.insert(bold(), vec!["sans-bold".into()]);
    f.families.insert(serif_italic(), vec!["serif-italic".into()]);
    ctx.set_fonts(f);
}

fn apply_style(ctx: &egui::Context, pal: &Palette, dark: bool) {
    let mut v = if dark { egui::Visuals::dark() } else { egui::Visuals::light() };
    v.panel_fill = pal.bg;
    v.window_fill = pal.bg;
    v.extreme_bg_color = pal.surface;
    v.faint_bg_color = pal.surface;
    v.override_text_color = Some(pal.text);
    v.selection.bg_fill = pal.accent;
    v.selection.stroke = Stroke::new(1.0_f32, pal.text);
    v.hyperlink_color = pal.accent;
    for w in [&mut v.widgets.noninteractive, &mut v.widgets.inactive, &mut v.widgets.hovered, &mut v.widgets.active, &mut v.widgets.open] {
        w.corner_radius = CornerRadius::ZERO;
        w.bg_stroke = Stroke::new(1.0_f32, pal.line);
        w.fg_stroke = Stroke::new(1.0_f32, pal.text);
        w.bg_fill = pal.surface;
        w.weak_bg_fill = pal.surface;
    }
    v.widgets.hovered.bg_stroke = Stroke::new(1.0_f32, pal.accent);
    v.widgets.active.bg_stroke = Stroke::new(1.0_f32, pal.accent);
    v.window_corner_radius = CornerRadius::ZERO;
    ctx.set_visuals(v);
    ctx.style_mut(|s| {
        s.spacing.item_spacing = Vec2::new(8.0, 6.0);
        s.spacing.button_padding = Vec2::new(10.0, 6.0);
        s.text_styles.insert(egui::TextStyle::Body, FontId::new(13.0, sans()));
        s.text_styles.insert(egui::TextStyle::Button, FontId::new(12.0, sans()));
        s.text_styles.insert(egui::TextStyle::Monospace, FontId::new(11.0, mono()));
    });
}

/// One run of styled text.
fn run(job: &mut LayoutJob, text: &str, family: FontFamily, size: f32, color: Color32, spacing: f32) {
    job.append(text, 0.0, TextFormat { font_id: FontId::new(size, family), color, extra_letter_spacing: spacing, ..Default::default() });
}

fn text(t: &str, family: FontFamily, size: f32, color: Color32, spacing: f32) -> LayoutJob {
    let mut j = LayoutJob::default();
    run(&mut j, t, family, size, color, spacing);
    j
}

/// Mono, uppercase, letter-spaced label (the site's `.eyebrow`).
fn eyebrow(ui: &mut egui::Ui, t: &str, color: Color32) {
    ui.label(text(&t.to_uppercase(), mono(), 10.0, color, 1.6));
}

/// Square button. `filled` uses the site's inverted button colours.
fn chip(ui: &mut egui::Ui, pal: &Palette, label: &str, filled: bool) -> egui::Response {
    chip_pad(ui, pal, label, filled, 22.0)
}

fn chip_pad(ui: &mut egui::Ui, pal: &Palette, label: &str, filled: bool, pad: f32) -> egui::Response {
    let font = FontId::new(11.0, mono());
    let galley = ui.painter().layout_job(text(&label.to_uppercase(), font.family.clone(), font.size, pal.text, 1.2));
    let size = galley.size() + Vec2::new(pad, 14.0);
    let (rect, resp) = ui.allocate_exact_size(size, Sense::click());
    let hovered = resp.hovered();
    let (bg, fg, stroke) = if filled {
        (if hovered { pal.btn_hover } else { pal.btn_bg }, pal.btn_text, pal.btn_bg)
    } else {
        (if hovered { pal.surface } else { Color32::TRANSPARENT }, if hovered { pal.accent } else { pal.text }, if hovered { pal.accent } else { pal.line })
    };
    let p = ui.painter();
    p.rect_filled(rect, CornerRadius::ZERO, bg);
    p.rect_stroke(rect, CornerRadius::ZERO, Stroke::new(1.0_f32, stroke), StrokeKind::Inside);
    let g = p.layout_job(text(&label.to_uppercase(), font.family, font.size, fg, 1.2));
    p.galley(rect.center() - g.size() / 2.0, g, fg);
    resp.on_hover_cursor(egui::CursorIcon::PointingHand)
}

/// The Albion Road mark: double ring around seven rounded bars, centre bar in the accent.
fn logo(ui: &mut egui::Ui, pal: &Palette, size: f32, level: f32) {
    let (rect, _) = ui.allocate_exact_size(Vec2::splat(size), Sense::hover());
    let p = ui.painter();
    let c = rect.center();
    let r = size / 2.0 - 1.0;
    p.circle_stroke(c, r, Stroke::new(1.5_f32, pal.text));
    p.circle_stroke(c, r * 0.84, Stroke::new(1.0_f32, pal.accent.gamma_multiply(0.55)));
    let heights = [0.30, 0.52, 0.74, 0.95, 0.74, 0.52, 0.30];
    let w = r * 0.11;
    let gap = r * 0.19;
    for (i, h) in heights.iter().enumerate() {
        let wobble = if level > 0.0 { 0.75 + 0.25 * ((level * 6.0 + i as f32 * 1.7).sin() * 0.5 + 0.5) } else { 1.0 };
        let hh = r * h * wobble;
        let x = c.x + (i as f32 - 3.0) * gap;
        let bar = Rect::from_center_size(Pos2::new(x, c.y), Vec2::new(w, hh));
        let col = if i == 3 { pal.accent } else { pal.text };
        p.rect_filled(bar, CornerRadius::same((w / 2.0) as u8), col);
    }
}

fn hline(ui: &mut egui::Ui, pal: &Palette) {
    let w = ui.available_width();
    let (rect, _) = ui.allocate_exact_size(Vec2::new(w, 1.0), Sense::hover());
    ui.painter().hline(rect.x_range(), rect.center().y, Stroke::new(1.0_f32, pal.line));
}

fn draw_grid(ui: &mut egui::Ui, pal: &Palette, p: &Pattern, playhead: Option<f64>) {
    let mut notes: Vec<u8> = kit::GRID_ORDER.iter().copied().filter(|n| p.hits.iter().any(|h| h.note == *n)).collect();
    if notes.is_empty() {
        notes.push(36);
    }
    let row_h = 22.0;
    let label_w = 96.0;
    let width = ui.available_width();
    let height = notes.len() as f32 * row_h + 18.0;
    let (rect, _) = ui.allocate_exact_size(Vec2::new(width, height), Sense::hover());
    let painter = ui.painter_at(rect);
    let grid = Rect::from_min_max(Pos2::new(rect.left() + label_w, rect.top() + 18.0), rect.right_bottom());
    let beats = p.length_beats();
    let bpb = p.beats_per_bar();
    let x_of = |beat: f64| grid.left() + (beat / beats) as f32 * grid.width();

    painter.rect_filled(grid, CornerRadius::ZERO, pal.surface);
    // subdivision, beat and bar lines + beat numbers
    let subs = (beats * 4.0).round() as i64;
    for s in 0..=subs {
        let beat = s as f64 / 4.0;
        let x = x_of(beat);
        let (w, col) = if (beat / bpb).fract().abs() < 1e-9 {
            (1.5_f32, pal.muted)
        } else if beat.fract() == 0.0 {
            (1.0_f32, pal.line)
        } else {
            (1.0_f32, pal.line.gamma_multiply(0.45))
        };
        painter.vline(x, grid.y_range(), Stroke::new(w, col));
        if beat.fract() == 0.0 && beat < beats {
            let n = (beat % bpb) as i64 + 1;
            let bar = (beat / bpb) as i64 + 1;
            let label = if n == 1 { format!("{bar}") } else { format!("{bar}.{n}") };
            painter.text(Pos2::new(x + 3.0, rect.top() + 2.0), Align2::LEFT_TOP, label, FontId::new(9.0, mono()), pal.muted);
        }
    }
    for (ri, note) in notes.iter().enumerate() {
        let y = grid.top() + ri as f32 * row_h;
        painter.hline(grid.x_range(), y, Stroke::new(1.0_f32, pal.line.gamma_multiply(0.6)));
        painter.text(
            Pos2::new(rect.left(), y + row_h / 2.0),
            Align2::LEFT_CENTER,
            kit::short_of(*note).to_uppercase(),
            FontId::new(10.0, mono()),
            pal.muted,
        );
        let cell = (grid.width() / (beats as f32 * 4.0)).clamp(3.0, 16.0);
        for h in p.hits.iter().filter(|h| h.note == *note) {
            let x = x_of(h.beat + p.swing as f64 * h.swing_unit);
            let s = (cell - 3.0).max(3.0).min(row_h - 8.0);
            let r = Rect::from_center_size(Pos2::new(x + s / 2.0 + 1.0, y + row_h / 2.0), Vec2::new(s, row_h - 8.0));
            let col = if h.vel >= 110 {
                pal.accent
            } else {
                pal.accent.gamma_multiply(0.25 + 0.65 * (h.vel as f32 / 110.0))
            };
            painter.rect_filled(r, CornerRadius::ZERO, col);
        }
    }
    if let Some(b) = playhead {
        let x = x_of(b.rem_euclid(beats));
        painter.vline(x, grid.y_range(), Stroke::new(2.0_f32, pal.text));
    }
}

/// Export settings taken from the plugin when a save button is pressed.
struct Export {
    bars: u32,
    fill: bool,
    channel: u8,
    extra_swing: f32,
    vel_scale: f32,
}

/// `~/Music/Albion Road Audio/DRS Grooves`, where every export goes.
fn export_root() -> Result<std::path::PathBuf, String> {
    let home = std::env::var_os("HOME").ok_or("HOME is not set")?;
    Ok(std::path::PathBuf::from(home).join("Music/Albion Road Audio/DRS Grooves"))
}

fn clean(s: &str) -> String {
    s.chars().map(|c| if c.is_alphanumeric() || " -_&().,'".contains(c) { c } else { '-' }).collect()
}

/// Write one groove as `<genre>/<name> - <bpm> BPM - <n> bars[ + fill].mid`.
fn save_groove(lib: &Library, idx: usize, x: &Export) -> Result<std::path::PathBuf, String> {
    let p = &lib.patterns[idx];
    let bars = x.bars.max(p.bars);
    // a fill needs at least one groove bar before it
    let fill = x.fill && bars > 1 && !lib.fills_for[idx].is_empty();
    let dir = export_root()?.join(clean(&p.genre));
    std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
    let len = if bars == 1 { "1 bar".to_string() } else { format!("{bars} bars") };
    let path = dir.join(format!("{} - {} BPM - {}{}.mid", clean(&p.name), p.bpm, len, if fill { " + fill" } else { "" }));
    let opts = smf::ExportOptions { bars, fill, channel: x.channel, extra_swing: x.extra_swing, vel_scale: x.vel_scale, ..Default::default() };
    std::fs::write(&path, smf::render(lib, idx, &opts)).map_err(|e| e.to_string())?;
    Ok(path)
}

/// Write one fill on its own as `<genre>/Fills/<name> - <bpm> BPM.mid`.
fn save_fill(lib: &Library, idx: usize, x: &Export) -> Result<std::path::PathBuf, String> {
    let p = &lib.patterns[idx];
    let dir = export_root()?.join(clean(&p.genre)).join("Fills");
    std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
    let path = dir.join(format!("{} - {} BPM.mid", clean(&p.name), p.bpm));
    let opts = smf::ExportOptions { bars: p.bars, fill: false, channel: x.channel, extra_swing: x.extra_swing, vel_scale: x.vel_scale, ..Default::default() };
    std::fs::write(&path, smf::render(lib, idx, &opts)).map_err(|e| e.to_string())?;
    Ok(path)
}

/// Every groove (with the chosen length/fill) plus every fill as its own clip.
fn export_all(lib: &Library, x: &Export) -> Result<(usize, usize), String> {
    let mut grooves = 0;
    let mut fills = 0;
    for (idx, p) in lib.patterns.iter().enumerate() {
        match p.kind {
            Kind::Groove => {
                save_groove(lib, idx, x)?;
                grooves += 1;
            }
            Kind::Fill => {
                save_fill(lib, idx, x)?;
                fills += 1;
            }
        }
    }
    Ok((grooves, fills))
}

pub fn create(params: Arc<DrsParams>, shared: Arc<Shared>) -> Option<Box<dyn Editor>> {
    let editor_state = params.editor_state.clone();
    create_egui_editor(
        editor_state,
        UiState::default(),
        |ctx, _| install_fonts(ctx),
        move |ctx, setter, st| {
            let lib = library();
            let dark = crate::theme::is_dark();
            let pal = if dark { DARK } else { LIGHT };
            apply_style(ctx, &pal, dark);

            let selected_groove_pos = (params.pattern.value().max(0) as usize).min(lib.grooves.len() - 1);
            let selected_idx = lib.grooves[selected_groove_pos];
            let playing = shared.playing.load(Ordering::Relaxed);
            let playing_idx = shared.pattern.load(Ordering::Relaxed).min(lib.patterns.len() - 1);
            let beat = f64::from_bits(shared.beat.load(Ordering::Relaxed));
            let genre_of = |idx: usize| lib.genres.iter().position(|g| *g == lib.patterns[idx].genre).unwrap_or(0);
            let genre = *st.genre.get_or_insert_with(|| genre_of(selected_idx));
            let t = ctx.input(|i| i.time) as f32;
            if playing {
                ctx.request_repaint();
            }

            // ── header ─────────────────────────────────────────────
            egui::TopBottomPanel::top("header")
                .frame(egui::Frame::new().fill(pal.bg).inner_margin(egui::Margin { left: 28, right: 28, top: 16, bottom: 0 }))
                .show_separator_line(false)
                .show(ctx, |ui| {
                    ui.horizontal(|ui| {
                        logo(ui, &pal, 42.0, if playing { t } else { 0.0 });
                        ui.add_space(4.0);
                        ui.vertical(|ui| {
                            ui.add_space(4.0);
                            ui.label(text("ALBION ROAD AUDIO", bold(), 18.0, pal.text, 1.4));
                            ui.label(text("DRS GROOVES · FOR DRUMGIZMO", mono(), 9.0, pal.muted, 1.5));
                        });
                        ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                            let label = if dark { "Light mode" } else { "Dark mode" };
                            if chip(ui, &pal, label, false).clicked() {
                                crate::theme::set_dark(!dark);
                            }
                        });
                    });
                    ui.add_space(14.0);
                    hline(ui, &pal);
                });

            // ── footer ─────────────────────────────────────────────
            egui::TopBottomPanel::bottom("footer")
                .frame(egui::Frame::new().fill(pal.bg).inner_margin(egui::Margin { left: 28, right: 28, top: 0, bottom: 12 }))
                .show_separator_line(false)
                .show(ctx, |ui| {
                    hline(ui, &pal);
                    ui.add_space(8.0);
                    ui.horizontal(|ui| {
                        ui.label(text("INDEPENDENT · ORIGINAL · MADE WITH CARE", mono(), 9.0, pal.muted, 1.4));
                        ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                            let l = if st.show_map { "Hide note map" } else { "DRS Kit note map" };
                            if chip(ui, &pal, l, false).clicked() {
                                st.show_map = !st.show_map;
                            }
                            if !st.status.is_empty() {
                                ui.label(text(&st.status, mono(), 9.0, pal.eyebrow, 0.4));
                            }
                        });
                    });
                });

            // ── library (left) ─────────────────────────────────────
            egui::SidePanel::left("library")
                .exact_width(322.0)
                .resizable(false)
                .show_separator_line(false)
                .frame(egui::Frame::new().fill(pal.bg).inner_margin(egui::Margin { left: 28, right: 16, top: 18, bottom: 12 }))
                .show(ctx, |ui| {
                    eyebrow(ui, "The library", pal.eyebrow);
                    ui.add_space(4.0);
                    ui.horizontal_wrapped(|ui| {
                        ui.spacing_mut().item_spacing = Vec2::new(3.0, 4.0);
                        for (gi, g) in lib.genres.iter().enumerate() {
                            if chip_pad(ui, &pal, g, gi == genre, 14.0).clicked() {
                                st.genre = Some(gi);
                            }
                        }
                    });
                    ui.add_space(10.0);
                    let gname = &lib.genres[genre];
                    egui::ScrollArea::vertical().auto_shrink([false, false]).show(ui, |ui| {
                        ui.spacing_mut().item_spacing.y = 0.0;
                        for (pos, &idx) in lib.grooves.iter().enumerate() {
                            let p = &lib.patterns[idx];
                            if &p.genre != gname {
                                continue;
                            }
                            let sel = pos == selected_groove_pos;
                            let w = ui.available_width();
                            let (rect, resp) = ui.allocate_exact_size(Vec2::new(w, 30.0), Sense::click());
                            let painter = ui.painter();
                            if sel {
                                painter.rect_filled(rect, CornerRadius::ZERO, pal.surface);
                            } else if resp.hovered() {
                                painter.rect_filled(rect, CornerRadius::ZERO, pal.surface.gamma_multiply(0.5));
                            }
                            painter.hline(rect.x_range(), rect.bottom(), Stroke::new(1.0_f32, pal.line));
                            let name_col = if sel { pal.accent } else { pal.text };
                            painter.text(rect.left_center() + Vec2::new(8.0, 0.0), Align2::LEFT_CENTER, &p.name, FontId::new(13.0, if sel { bold() } else { sans() }), name_col);
                            let meta = if p.time_sig == (4, 4) { format!("{} BPM", p.bpm) } else { format!("{}/{} · {} BPM", p.time_sig.0, p.time_sig.1, p.bpm) };
                            painter.text(rect.right_center() - Vec2::new(8.0, 0.0), Align2::RIGHT_CENTER, meta, FontId::new(10.0, mono()), pal.muted);
                            if resp.on_hover_cursor(egui::CursorIcon::PointingHand).clicked() {
                                setter.begin_set_parameter(&params.pattern);
                                setter.set_parameter(&params.pattern, pos as i32);
                                setter.end_set_parameter(&params.pattern);
                            }
                        }
                        ui.add_space(12.0);
                        eyebrow(ui, "Fills · played automatically", pal.muted);
                        ui.add_space(4.0);
                        for p in lib.patterns.iter().filter(|p| &p.genre == gname && p.kind == Kind::Fill) {
                            ui.label(text(&p.name, sans(), 12.0, pal.muted, 0.0));
                            ui.add_space(3.0);
                        }
                    });
                });

            // ── main ───────────────────────────────────────────────
            egui::CentralPanel::default()
                .frame(egui::Frame::new().fill(pal.bg).inner_margin(egui::Margin { left: 16, right: 28, top: 18, bottom: 12 }))
                .show(ctx, |ui| {
                    if st.show_map {
                        eyebrow(ui, "DRS Kit 2.1 · note map", pal.eyebrow);
                        ui.add_space(6.0);
                        let mut j = LayoutJob::default();
                        run(&mut j, "Every articulation, ", sans(), 26.0, pal.text, -0.8);
                        run(&mut j, "where it lives.", serif_italic(), 26.0, pal.accent, 0.0);
                        ui.label(j);
                        ui.add_space(10.0);
                        egui::Grid::new("map").num_columns(6).spacing(Vec2::new(18.0, 6.0)).show(ui, |ui| {
                            for (i, &note) in kit::GRID_ORDER.iter().enumerate() {
                                ui.label(text(&format!("{note}"), mono(), 11.0, pal.accent, 0.0));
                                ui.label(text(kit::name_of(note), sans(), 12.0, pal.text, 0.0));
                                if i % 3 == 2 {
                                    ui.end_row();
                                }
                            }
                        });
                        ui.add_space(10.0);
                        ui.label(text(
                            "From DrumGizmo's published DRS Kit note labels. If a hit lands on the wrong articulation, edit drs/src/kit.rs and rebuild.",
                            sans(), 12.0, pal.muted, 0.0,
                        ));
                        return;
                    }

                    let show_idx = if playing { playing_idx } else { selected_idx };
                    let p = &lib.patterns[show_idx];
                    let live = playing && show_idx == playing_idx;

                    ui.horizontal(|ui| {
                        let (dot, _) = ui.allocate_exact_size(Vec2::new(8.0, 12.0), Sense::hover());
                        let pulse = if playing { 0.55 + 0.45 * (t * 4.0).sin().abs() } else { 1.0 };
                        ui.painter().circle_filled(dot.center(), 3.5, if playing { pal.olive.gamma_multiply(pulse) } else { pal.line });
                        let label = match (playing, p.kind) {
                            (true, Kind::Fill) => "Playing · fill".to_string(),
                            (true, _) => "Playing".to_string(),
                            _ => "Selected · press play in your host".to_string(),
                        };
                        eyebrow(ui, &format!("{} · {}", p.genre, label), pal.eyebrow);
                    });
                    ui.add_space(6.0);
                    let mut j = LayoutJob::default();
                    run(&mut j, &format!("{} ", p.name), sans(), 30.0, pal.text, -1.2);
                    run(&mut j, &format!("{} bpm", p.bpm), serif_italic(), 30.0, pal.accent, 0.0);
                    ui.label(j);
                    ui.add_space(2.0);
                    let meta = format!(
                        "{}/{} · {} bar{} · swing {:.0}% · {} hits",
                        p.time_sig.0,
                        p.time_sig.1,
                        p.bars,
                        if p.bars == 1 { "" } else { "s" },
                        p.swing * 100.0,
                        p.hits.len()
                    );
                    ui.label(text(&meta.to_uppercase(), mono(), 10.0, pal.muted, 1.0));
                    ui.add_space(14.0);
                    draw_grid(ui, &pal, p, if live { Some(beat) } else { None });
                    ui.add_space(18.0);

                    // controls
                    let col_w = (ui.available_width() - 3.0 * 18.0) / 4.0;
                    let slider_w = (col_w - 92.0).max(60.0);
                    egui::Grid::new("controls").num_columns(4).spacing(Vec2::new(18.0, 4.0)).show(ui, |ui| {
                        for l in ["Fill", "Extra swing", "Humanize timing", "Humanize velocity"] {
                            eyebrow(ui, l, pal.muted);
                        }
                        ui.end_row();
                        ui.add(widgets::ParamSlider::for_param(&params.fill_every, setter).with_width(slider_w));
                        ui.add(widgets::ParamSlider::for_param(&params.swing, setter).with_width(slider_w));
                        ui.add(widgets::ParamSlider::for_param(&params.humanize_time, setter).with_width(slider_w));
                        ui.add(widgets::ParamSlider::for_param(&params.humanize_vel, setter).with_width(slider_w));
                        ui.end_row();
                        ui.add_space(6.0);
                        ui.end_row();
                        for l in ["Velocity", "MIDI channel", "Crash after fill", ""] {
                            eyebrow(ui, l, pal.muted);
                        }
                        ui.end_row();
                        ui.add(widgets::ParamSlider::for_param(&params.velocity, setter).with_width(slider_w));
                        ui.add(widgets::ParamSlider::for_param(&params.channel, setter).with_width(slider_w));
                        let on = params.crash_after_fill.value();
                        if chip(ui, &pal, if on { "On · L/R crash" } else { "Off" }, on).clicked() {
                            setter.begin_set_parameter(&params.crash_after_fill);
                            setter.set_parameter(&params.crash_after_fill, !on);
                            setter.end_set_parameter(&params.crash_after_fill);
                        }
                        ui.label("");
                        ui.end_row();
                        ui.add_space(6.0);
                        ui.end_row();

                        // MIDI export: drag the files from Bitwig's browser onto a track
                        let bars = *params.export_bars.read().unwrap();
                        let fill = *params.export_fill.read().unwrap();
                        for l in ["Clip length (bars)", "Clip ending", "Save .mid", ""] {
                            eyebrow(ui, l, pal.muted);
                        }
                        ui.end_row();
                        ui.horizontal(|ui| {
                            ui.spacing_mut().item_spacing.x = 4.0;
                            for n in [1u32, 2, 4, 8] {
                                if chip_pad(ui, &pal, &n.to_string(), n == bars, 16.0).on_hover_text(format!("{n} bar{} per clip", if n == 1 { "" } else { "s" })).clicked() {
                                    *params.export_bars.write().unwrap() = n;
                                }
                            }
                            
                        });
                        let fill_label = if fill && bars > 1 { "With fill" } else { "No fill" };
                        if chip(ui, &pal, fill_label, fill && bars > 1).on_hover_text(if bars == 1 { "1-bar clips have no fill" } else { "Replace the clip's last bar with one of the genre's fills" }).clicked() && bars > 1 {
                            *params.export_fill.write().unwrap() = !fill;
                        }
                        let x = Export {
                            bars,
                            fill,
                            channel: (params.channel.value() - 1).clamp(0, 15) as u8,
                            extra_swing: params.swing.value() / 100.0,
                            vel_scale: params.velocity.value() / 100.0,
                        };
                        if chip(ui, &pal, "Save groove  →", true).on_hover_text("Write the selected groove as a .mid clip").clicked() {
                            st.status = match save_groove(lib, selected_idx, &x) {
                                Ok(path) => format!("SAVED {}", path.file_name().map(|n| n.to_string_lossy().to_string()).unwrap_or_default()),
                                Err(e) => format!("COULD NOT SAVE: {e}"),
                            };
                        }
                        if chip(ui, &pal, "Export all  →", true)
                            .on_hover_text("Write every groove (with these settings) and every fill as separate .mid clips, one folder per genre")
                            .clicked()
                        {
                            st.status = match export_all(lib, &x) {
                                Ok((g, f)) => format!("EXPORTED {g} GROOVES + {f} FILLS TO ~/MUSIC/ALBION ROAD AUDIO/DRS GROOVES"),
                                Err(e) => format!("COULD NOT EXPORT: {e}"),
                            };
                        }
                        ui.end_row();
                    });
                });
        },
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Ticks from the file's end-of-track position (format 0, single track).
    fn length_ticks(bytes: &[u8]) -> u32 {
        let mut i = 22; // MThd (14) + MTrk header (8)
        let mut t = 0u32;
        loop {
            let mut d = 0u32;
            loop {
                let b = bytes[i];
                i += 1;
                d = (d << 7) | (b & 0x7f) as u32;
                if b & 0x80 == 0 {
                    break;
                }
            }
            t += d;
            match bytes[i] {
                0xff => {
                    let kind = bytes[i + 1];
                    let len = bytes[i + 2] as usize;
                    if kind == 0x2f {
                        return t;
                    }
                    i += 3 + len;
                }
                _ => i += 3,
            }
        }
    }

    #[test]
    fn export_all_writes_every_groove_and_fill() {
        let home = std::env::temp_dir().join(format!("drs-export-test-{}", std::process::id()));
        std::env::set_var("HOME", &home);
        let lib = library();
        let x = Export { bars: 8, fill: true, channel: 9, extra_swing: 0.0, vel_scale: 1.0 };
        let (g, f) = export_all(lib, &x).unwrap();
        assert_eq!(g, lib.grooves.len());
        assert_eq!(g + f, lib.patterns.len());

        let rock = home.join("Music/Albion Road Audio/DRS Grooves/Rock");
        let clip = std::fs::read(rock.join("Straight 8ths - 110 BPM - 8 bars + fill.mid")).unwrap();
        assert_eq!(&clip[..4], b"MThd");
        assert_eq!(length_ticks(&clip), 8 * 4 * smf::PPQ as u32);
        assert!(std::fs::read_dir(rock.join("Fills")).unwrap().count() > 0);

        // 1 bar never gets a fill, even if asked for
        let one = save_groove(lib, lib.grooves[0], &Export { bars: 1, ..x }).unwrap();
        assert!(one.to_string_lossy().ends_with("1 bar.mid"));
        assert_eq!(length_ticks(&std::fs::read(one).unwrap()), 4 * smf::PPQ as u32);
        std::fs::remove_dir_all(home).unwrap();
    }
}
