//! Surgical EQ window: Albion Road styling (paper/ink palette, terracotta accent, mono eyebrows,
//! square corners), a response graph with draggable band handles, and one row of knobs.

use crate::analyzer::{Analyzer, FLOOR_DB};
use crate::dsp::{Coefs, BUTTER_Q2, BUTTER_Q4};
use crate::{band_coefs, fmt_hz, lp_limit, EqParams, InstrumentDef, Move, Shared, GAIN_RANGE, HP_SLOPE, HP_STEPS, LP_SLOPE, LP_STEPS_KHZ};
use nih_plug::prelude::*;
use nih_plug_egui::create_egui_editor;
use nih_plug_egui::egui::{
    self, text::LayoutJob, Align, Align2, Color32, CornerRadius, FontData, FontDefinitions, FontFamily, FontId, Id,
    Layout, Pos2, Rect, Sense, Shape, Stroke, StrokeKind, TextFormat, Vec2,
};
use std::sync::Arc;

const MARGIN: f32 = 24.0;
const COL_W: f32 = 118.0;
const GRAPH_H: f32 = 210.0;
const KNOB_H: f32 = 104.0;
const KNOB_R: f32 = 22.0;
const DB_VIEW: f64 = 12.0;
const F_LO: f64 = 20.0;
const F_HI: f64 = 20000.0;

/// Window size for an instrument: one knob column per band plus low and high cut.
pub fn size(def: &InstrumentDef) -> (u32, u32) {
    let cols = def.bands.len() as f32 + 2.0;
    ((MARGIN * 2.0 + cols * COL_W).ceil() as u32, 590)
}

#[derive(Clone, Copy)]
struct Palette {
    bg: Color32,
    surface: Color32,
    text: Color32,
    muted: Color32,
    line: Color32,
    accent: Color32,
    eyebrow: Color32,
    olive: Color32,
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
    v.override_text_color = Some(pal.text);
    v.selection.bg_fill = pal.accent;
    v.window_corner_radius = CornerRadius::ZERO;
    ctx.set_visuals(v);
    ctx.style_mut(|s| {
        s.spacing.item_spacing = Vec2::new(8.0, 6.0);
        s.text_styles.insert(egui::TextStyle::Body, FontId::new(13.0, sans()));
    });
}

fn text(t: &str, family: FontFamily, size: f32, color: Color32, spacing: f32) -> LayoutJob {
    let mut j = LayoutJob::default();
    run(&mut j, t, family, size, color, spacing);
    j
}

fn run(job: &mut LayoutJob, text: &str, family: FontFamily, size: f32, color: Color32, spacing: f32) {
    job.append(text, 0.0, TextFormat { font_id: FontId::new(size, family), color, extra_letter_spacing: spacing, ..Default::default() });
}

/// Paint a (possibly letter-spaced) job centred at `pos`.
fn paint_centered(p: &egui::Painter, pos: Pos2, job: LayoutJob) {
    let g = p.layout_job(job);
    p.galley(pos - g.size() / 2.0, g, Color32::PLACEHOLDER);
}

/// Square button. `filled` uses the site's inverted button colours.
fn chip(ui: &mut egui::Ui, pal: &Palette, label: &str, filled: bool, min_w: f32) -> egui::Response {
    let font = FontId::new(11.0, mono());
    let galley = ui.painter().layout_job(text(&label.to_uppercase(), font.family.clone(), font.size, pal.text, 1.2));
    let size = Vec2::new((galley.size().x + 22.0).max(min_w), galley.size().y + 12.0);
    let (rect, resp) = ui.allocate_exact_size(size, Sense::click());
    let hovered = resp.hovered();
    let (bg, fg, stroke) = if filled {
        (if hovered { pal.btn_hover } else { pal.btn_bg }, pal.btn_text, pal.btn_bg)
    } else {
        (if hovered { pal.surface } else { Color32::TRANSPARENT }, if hovered { pal.accent } else { pal.muted }, if hovered { pal.accent } else { pal.line })
    };
    let p = ui.painter();
    p.rect_filled(rect, CornerRadius::ZERO, bg);
    p.rect_stroke(rect, CornerRadius::ZERO, Stroke::new(1.0_f32, stroke), StrokeKind::Inside);
    paint_centered(p, rect.center(), text(&label.to_uppercase(), font.family, font.size, fg, 1.2));
    resp.on_hover_cursor(egui::CursorIcon::PointingHand)
}

/// The Albion Road mark: double ring around seven rounded bars, centre bar in the accent.
fn logo(ui: &mut egui::Ui, pal: &Palette, size: f32) {
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
        let x = c.x + (i as f32 - 3.0) * gap;
        let bar = Rect::from_center_size(Pos2::new(x, c.y), Vec2::new(w, r * h));
        p.rect_filled(bar, CornerRadius::same((w / 2.0) as u8), if i == 3 { pal.accent } else { pal.text });
    }
}

fn hline(ui: &mut egui::Ui, pal: &Palette) {
    let w = ui.available_width();
    let (rect, _) = ui.allocate_exact_size(Vec2::new(w, 1.0), Sense::hover());
    ui.painter().hline(rect.x_range(), rect.center().y, Stroke::new(1.0_f32, pal.line));
}

// ── parameter gestures ────────────────────────────────────────────────

/// Relative drag on a parameter's normalised value, with shift for fine control,
/// double-click to reset and scroll-wheel stepping.
fn drag_param<P: Param>(ui: &egui::Ui, resp: &egui::Response, setter: &ParamSetter, param: &P, delta_px: f32, range_px: f32) {
    let id = resp.id.with("start");
    let fine = ui.input(|i| i.modifiers.shift);
    let scale = if fine { range_px * 4.0 } else { range_px };
    if resp.double_clicked() {
        setter.begin_set_parameter(param);
        setter.set_parameter_normalized(param, param.default_normalized_value());
        setter.end_set_parameter(param);
        return;
    }
    if resp.drag_started() {
        setter.begin_set_parameter(param);
        ui.data_mut(|d| d.insert_temp(id, param.unmodulated_normalized_value()));
    }
    if resp.dragged() {
        let v: f32 = ui.data(|d| d.get_temp(id)).unwrap_or(param.unmodulated_normalized_value());
        let v = (v + delta_px / scale).clamp(0.0, 1.0);
        ui.data_mut(|d| d.insert_temp(id, v));
        setter.set_parameter_normalized(param, v);
    }
    if resp.drag_stopped() {
        setter.end_set_parameter(param);
    }
    if resp.hovered() && !resp.dragged() {
        let scroll = ui.input(|i| i.raw_scroll_delta.y);
        if scroll != 0.0 {
            let step = match param.step_count() {
                Some(n) => 1.0 / n as f32,
                None => if fine { 0.1 / (2.0 * GAIN_RANGE) } else { 0.5 / (2.0 * GAIN_RANGE) },
            };
            let v = (param.unmodulated_normalized_value() + step * scroll.signum()).clamp(0.0, 1.0);
            setter.begin_set_parameter(param);
            setter.set_parameter_normalized(param, v);
            setter.end_set_parameter(param);
        }
    }
}

fn toggle(setter: &ParamSetter, p: &BoolParam) {
    setter.begin_set_parameter(p);
    setter.set_parameter(p, !p.value());
    setter.end_set_parameter(p);
}

// ── knob ──────────────────────────────────────────────────────────────

struct KnobStyle {
    /// Arc fills from the centre (gain) rather than from the left (cut knobs).
    bipolar: bool,
    ticks: usize,
    labels: &'static [(usize, &'static str)],
}

const GAIN_KNOB: KnobStyle = KnobStyle { bipolar: true, ticks: 13, labels: &[(0, "-6"), (3, "-3"), (6, "0"), (9, "+3"), (12, "+6")] };
const HP_KNOB: KnobStyle =
    KnobStyle { bipolar: false, ticks: 16, labels: &[(0, "OFF"), (3, "30"), (6, "60"), (9, "90"), (12, "120"), (15, "150")] };
const LP_KNOB: KnobStyle = KnobStyle {
    bipolar: false,
    ticks: 12,
    labels: &[(0, "OFF"), (1, "12"), (3, "14"), (5, "16"), (7, "18"), (9, "20"), (11, "22")],
};

const SWEEP: f32 = 135.0_f32 * std::f32::consts::PI / 180.0;

fn polar(c: Pos2, r: f32, norm: f32) -> Pos2 {
    let a = -SWEEP + norm * 2.0 * SWEEP;
    Pos2::new(c.x + r * a.sin(), c.y - r * a.cos())
}

fn arc(c: Pos2, r: f32, from: f32, to: f32) -> Vec<Pos2> {
    let (a, b) = if from <= to { (from, to) } else { (to, from) };
    let n = (((b - a) * 48.0).ceil() as usize).max(2);
    (0..=n).map(|i| polar(c, r, a + (b - a) * i as f32 / n as f32)).collect()
}

fn knob<P: Param>(ui: &mut egui::Ui, pal: &Palette, setter: &ParamSetter, param: &P, style: &KnobStyle, enabled: bool) {
    let (rect, resp) = ui.allocate_exact_size(Vec2::new(COL_W, KNOB_H), Sense::click_and_drag());
    drag_param(ui, &resp, setter, param, -resp.drag_delta().y, 180.0);
    let resp = resp.on_hover_cursor(egui::CursorIcon::ResizeVertical);
    let p = ui.painter();
    let c = Pos2::new(rect.center().x, rect.top() + 50.0);
    let v = param.modulated_normalized_value();
    let active = if enabled { pal.accent } else { pal.muted };
    let hot = resp.hovered() || resp.dragged();

    // ticks + labels
    for i in 0..style.ticks {
        let n = i as f32 / (style.ticks - 1) as f32;
        let labelled = style.labels.iter().any(|(j, _)| *j == i);
        let (r0, r1) = if labelled { (KNOB_R + 8.0, KNOB_R + 13.0) } else { (KNOB_R + 9.0, KNOB_R + 11.0) };
        p.line_segment([polar(c, r0, n), polar(c, r1, n)], Stroke::new(1.0_f32, if labelled { pal.muted } else { pal.line }));
    }
    for (i, l) in style.labels {
        let n = *i as f32 / (style.ticks - 1) as f32;
        let pos = polar(c, KNOB_R + 22.0, n);
        paint_centered(p, pos, text(l, mono(), 8.5, pal.muted, 0.0));
    }
    // track and value arcs
    p.add(Shape::line(arc(c, KNOB_R + 4.0, 0.0, 1.0), Stroke::new(3.0_f32, pal.line)));
    let origin = if style.bipolar { 0.5 } else { 0.0 };
    if (v - origin).abs() > 0.002 {
        p.add(Shape::line(arc(c, KNOB_R + 4.0, origin, v), Stroke::new(3.0_f32, active)));
    }
    // body
    p.circle_filled(c, KNOB_R, pal.surface);
    p.circle_stroke(c, KNOB_R, Stroke::new(1.0_f32, if hot { pal.accent } else { pal.line }));
    p.line_segment([polar(c, KNOB_R * 0.3, v), polar(c, KNOB_R - 3.0, v)], Stroke::new(2.5_f32, if enabled { pal.text } else { pal.muted }));
}

const EXTRA_H: f32 = 38.0;

/// Frequency slider confined to the band's zone. Click or drag to place, shift-drag for fine,
/// double-click to reset, scroll to nudge.
fn freq_slider(ui: &mut egui::Ui, pal: &Palette, setter: &ParamSetter, param: &FloatParam, min: f32, max: f32, enabled: bool) {
    let (rect, resp) = ui.allocate_exact_size(Vec2::new(98.0, EXTRA_H), Sense::click_and_drag());
    let track = Rect::from_min_size(rect.min + Vec2::new(5.0, 7.0), Vec2::new(rect.width() - 10.0, 4.0));
    if resp.double_clicked() {
        setter.begin_set_parameter(param);
        setter.set_parameter_normalized(param, param.default_normalized_value());
        setter.end_set_parameter(param);
    } else {
        if resp.drag_started() || resp.clicked() {
            setter.begin_set_parameter(param);
        }
        if resp.dragged() || resp.clicked() {
            let fine = ui.input(|i| i.modifiers.shift);
            let v = if fine && !resp.clicked() {
                param.unmodulated_normalized_value() + resp.drag_delta().x / (track.width() * 6.0)
            } else {
                resp.interact_pointer_pos().map_or(param.unmodulated_normalized_value(), |p| (p.x - track.left()) / track.width())
            };
            setter.set_parameter_normalized(param, v.clamp(0.0, 1.0));
        }
        if resp.drag_stopped() || resp.clicked() {
            setter.end_set_parameter(param);
        }
        if resp.hovered() && !resp.dragged() {
            let scroll = ui.input(|i| i.raw_scroll_delta.y);
            if scroll != 0.0 {
                setter.begin_set_parameter(param);
                setter.set_parameter_normalized(param, (param.unmodulated_normalized_value() + 0.02 * scroll.signum()).clamp(0.0, 1.0));
                setter.end_set_parameter(param);
            }
        }
    }
    let hot = resp.hovered() || resp.dragged();
    let _ = resp.on_hover_cursor(egui::CursorIcon::ResizeHorizontal);
    let p = ui.painter();
    let v = param.modulated_normalized_value();
    let x = track.left() + v * track.width();
    let col = if enabled { pal.accent } else { pal.muted };
    p.rect_filled(track, CornerRadius::ZERO, pal.line);
    p.rect_filled(Rect::from_min_max(track.min, Pos2::new(x, track.bottom())), CornerRadius::ZERO, col);
    let thumb = Rect::from_center_size(Pos2::new(x, track.center().y), Vec2::new(8.0, 14.0));
    p.rect_filled(thumb, CornerRadius::ZERO, if hot { pal.accent } else if enabled { pal.text } else { pal.muted });
    let y = track.bottom() + 9.0;
    let short = |f: f32| if f >= 1000.0 { format!("{}k", f / 1000.0) } else { format!("{f:.0}") };
    p.text(Pos2::new(rect.left(), y), Align2::LEFT_CENTER, short(min), FontId::new(8.5, mono()), pal.muted);
    p.text(Pos2::new(rect.right(), y), Align2::RIGHT_CENTER, short(max), FontId::new(8.5, mono()), pal.muted);
    let label_col = if !enabled { pal.muted } else if hot { pal.accent } else { pal.text };
    p.text(Pos2::new(rect.center().x, y + 12.0), Align2::CENTER_CENTER, fmt_hz(param.value()), FontId::new(10.5, mono()), label_col);
}

fn fixed_box(ui: &mut egui::Ui, pal: &Palette, label: &str) {
    let (rect, _) = ui.allocate_exact_size(Vec2::new(98.0, EXTRA_H), Sense::hover());
    paint_centered(ui.painter(), rect.center(), text(&label.to_uppercase(), mono(), 9.0, pal.muted, 0.8));
}

/// One knob column: name, hint, knob, readout, slider (or slope), on/off.
#[allow(clippy::too_many_arguments)]
fn column(
    ui: &mut egui::Ui,
    pal: &Palette,
    setter: &ParamSetter,
    name: &str,
    hint: &str,
    on: &BoolParam,
    readout: String,
    knob_fn: impl FnOnce(&mut egui::Ui, bool),
    extra: impl FnOnce(&mut egui::Ui, bool),
) {
    let enabled = on.value();
    ui.allocate_ui_with_layout(Vec2::new(COL_W, 250.0), Layout::top_down(Align::Center), |ui| {
        ui.spacing_mut().item_spacing.y = 2.0;
        let spacing = if name.chars().count() > 12 { 0.3 } else { 1.4 };
        ui.label(text(&name.to_uppercase(), mono(), 10.0, if enabled { pal.eyebrow } else { pal.muted }, spacing));
        ui.label(text(&hint.to_uppercase(), mono(), 8.5, pal.muted, 0.8));
        knob_fn(ui, enabled);
        ui.label(text(&readout, bold(), 15.0, if enabled { pal.text } else { pal.muted }, 0.0));
        ui.add_space(4.0);
        extra(ui, enabled);
        ui.add_space(4.0);
        if chip(ui, pal, if enabled { "On" } else { "Off" }, enabled, 54.0).clicked() {
            toggle(setter, on);
        }
    });
}

// ── graph ─────────────────────────────────────────────────────────────

fn x_of(r: &Rect, f: f64) -> f32 {
    r.left() + ((f / F_LO).log10() / (F_HI / F_LO).log10()) as f32 * r.width()
}
fn f_of(r: &Rect, x: f32) -> f64 {
    F_LO * (F_HI / F_LO).powf(((x - r.left()) / r.width()) as f64)
}
fn y_of(r: &Rect, db: f64) -> f32 {
    r.center().y - (db / DB_VIEW) as f32 * (r.height() / 2.0)
}

fn freq_label(f: f64) -> String {
    if f >= 1000.0 { format!("{}k", f / 1000.0) } else { format!("{f}") }
}

fn draw_graph(ui: &mut egui::Ui, pal: &Palette, setter: &ParamSetter, params: &EqParams, fs: f64, spec: Option<&Analyzer>) {
    let def = params.def;
    let w = ui.available_width();
    let (outer, resp) = ui.allocate_exact_size(Vec2::new(w, GRAPH_H), Sense::hover());
    let plot = Rect::from_min_max(outer.min + Vec2::new(30.0, 6.0), outer.max - Vec2::new(6.0, 18.0));
    let painter = ui.painter_at(outer);
    painter.rect_filled(outer, CornerRadius::ZERO, pal.surface);
    let clip = ui.painter_at(plot);

    // band zones
    for (i, b) in def.bands.iter().enumerate() {
        let on = params.bands[i].on.value();
        let zr = Rect::from_x_y_ranges(x_of(&plot, b.min as f64)..=x_of(&plot, b.max as f64), plot.y_range());
        clip.rect_filled(zr, CornerRadius::ZERO, if on { pal.accent.gamma_multiply(0.07) } else { pal.line.gamma_multiply(0.15) });
    }

    // spectrum: input as a solid fill, output as a faint line
    if let Some(a) = spec {
        let sy = |db: f32| plot.bottom() - ((db - FLOOR_DB) / -FLOOR_DB).clamp(0.0, 1.0) * plot.height();
        let step = 2.0;
        let mut x = plot.left();
        let mut seg: Vec<Pos2> = Vec::new();
        let fill = pal.olive.gamma_multiply(0.42);
        let line = Stroke::new(1.0_f32, pal.text.gamma_multiply(0.45));
        while x < plot.right() {
            let (f0, f1) = (f_of(&plot, x), f_of(&plot, x + step));
            if f1 >= fs / 2.0 {
                break;
            }
            let pre = Analyzer::level(&a.pre, fs, f0, f1);
            if pre > FLOOR_DB {
                clip.rect_filled(Rect::from_min_max(Pos2::new(x, sy(pre)), Pos2::new(x + step, plot.bottom())), CornerRadius::ZERO, fill);
            }
            let post = Analyzer::level(&a.post, fs, f0, f1);
            if post > FLOOR_DB {
                seg.push(Pos2::new(x + step / 2.0, sy(post)));
            } else if seg.len() > 1 {
                clip.add(Shape::line(std::mem::take(&mut seg), line));
            } else {
                seg.clear();
            }
            x += step;
        }
        if seg.len() > 1 {
            clip.add(Shape::line(seg, line));
        }
    }

    // grid
    let decades = [10.0, 100.0, 1000.0, 10000.0];
    for d in decades {
        for m in 1..10 {
            let f = d * m as f64;
            if !(F_LO..=F_HI).contains(&f) {
                continue;
            }
            let x = x_of(&plot, f);
            let major = matches!(m, 1 | 2 | 5);
            clip.vline(x, plot.y_range(), Stroke::new(1.0_f32, if major { pal.line } else { pal.line.gamma_multiply(0.45) }));
            if major {
                let align = if f >= F_HI { Align2::RIGHT_TOP } else { Align2::CENTER_TOP };
                painter.text(Pos2::new(x, plot.bottom() + 3.0), align, freq_label(f), FontId::new(9.0, mono()), pal.muted);
            }
        }
    }
    for db in [-12.0, -9.0, -6.0, -3.0, 0.0, 3.0, 6.0, 9.0, 12.0] {
        let y = y_of(&plot, db);
        let major = db as i32 % 6 == 0;
        let col = if db == 0.0 { pal.muted } else if major { pal.line } else { pal.line.gamma_multiply(0.45) };
        clip.hline(plot.x_range(), y, Stroke::new(1.0_f32, col));
        if major {
            let l = if db > 0.0 { format!("+{db:.0}") } else { format!("{db:.0}") };
            painter.text(Pos2::new(plot.left() - 5.0, y), Align2::RIGHT_CENTER, l, FontId::new(9.0, mono()), pal.muted);
        }
    }

    // response
    let hp = params.hp_hz();
    let lp = params.lp_hz().map(|f| f.min(lp_limit(fs)));
    let mut cut_coefs: Vec<Coefs> = Vec::with_capacity(3);
    if let Some(f) = hp {
        cut_coefs.extend(BUTTER_Q4.iter().map(|&q| Coefs::highpass(fs, f, q)));
    }
    if let Some(f) = lp {
        cut_coefs.push(Coefs::lowpass(fs, f, BUTTER_Q2));
    }
    let band_coefs: Vec<Coefs> = def
        .bands
        .iter()
        .enumerate()
        .map(|(i, b)| {
            let (db, f) = params.band(i);
            band_coefs(b, fs, f, db)
        })
        .collect();
    let total = |f: f64| -> f64 {
        if f >= fs / 2.0 {
            return f64::NAN;
        }
        cut_coefs.iter().chain(band_coefs.iter()).map(|c| c.db(fs, f)).sum()
    };

    let n = (plot.width() / 2.0) as usize;
    let xs: Vec<f32> = (0..=n).map(|i| plot.left() + plot.width() * i as f32 / n as f32).collect();
    let clampy = |db: f64| y_of(&plot, db.clamp(-DB_VIEW * 1.5, DB_VIEW * 1.5));

    // individual bands, faint
    for (i, c) in band_coefs.iter().enumerate() {
        if params.band(i).0.abs() < 0.05 {
            continue;
        }
        let pts: Vec<Pos2> = xs.iter().map(|&x| Pos2::new(x, clampy(c.db(fs, f_of(&plot, x))))).collect();
        clip.add(Shape::line(pts, Stroke::new(1.0_f32, pal.olive.gamma_multiply(0.8))));
    }

    let y0 = y_of(&plot, 0.0);
    let mut pts = Vec::with_capacity(xs.len());
    for &x in &xs {
        let db = total(f_of(&plot, x));
        if db.is_nan() {
            break;
        }
        let y = clampy(db);
        clip.vline(x, if y < y0 { y..=y0 } else { y0..=y }, Stroke::new(2.0_f32, pal.accent.gamma_multiply(0.16)));
        pts.push(Pos2::new(x, y));
    }
    clip.add(Shape::line(pts, Stroke::new(2.0_f32, pal.accent)));

    // cut markers
    let cut_marker = |f: f64, label: String| {
        let x = x_of(&plot, f);
        let seg = [Pos2::new(x, plot.top()), Pos2::new(x, plot.bottom())];
        clip.extend(Shape::dashed_line(&seg, Stroke::new(1.0_f32, pal.muted), 3.0, 3.0));
        let align = if x > plot.center().x { Align2::RIGHT_TOP } else { Align2::LEFT_TOP };
        let dx = if x > plot.center().x { -4.0 } else { 4.0 };
        clip.text(Pos2::new(x + dx, plot.top() + 4.0), align, label, FontId::new(9.0, mono()), pal.muted);
    };
    if let Some(f) = hp {
        cut_marker(f, format!("LOW CUT {f:.0} HZ"));
    }
    if let Some(f) = lp {
        cut_marker(f, format!("HIGH CUT {:.1} KHZ", f / 1000.0).replace(".0 ", " "));
    }

    // band handles: drag left/right for frequency, up/down for gain
    for (i, b) in def.bands.iter().enumerate() {
        let bp = &params.bands[i];
        let (_, f) = params.band(i);
        let g = bp.gain.value() as f64;
        let c = Pos2::new(x_of(&plot, f), y_of(&plot, g));
        let id = Id::new(("eq-handle", i));
        let hr = ui.interact(Rect::from_center_size(c, Vec2::splat(20.0)), id, Sense::click_and_drag());
        if hr.double_clicked() {
            for p in [&bp.gain, &bp.freq] {
                setter.begin_set_parameter(p);
                setter.set_parameter_normalized(p, p.default_normalized_value());
                setter.end_set_parameter(p);
            }
        } else {
            if hr.drag_started() {
                setter.begin_set_parameter(&bp.freq);
                setter.begin_set_parameter(&bp.gain);
            }
            if hr.dragged() {
                if let Some(pos) = hr.interact_pointer_pos() {
                    let nf = f_of(&plot, pos.x).clamp(b.min as f64, b.max as f64);
                    let ng = (-(pos.y - plot.center().y) / (plot.height() / 2.0)) as f64 * DB_VIEW;
                    setter.set_parameter(&bp.freq, nf as f32);
                    setter.set_parameter(&bp.gain, ((ng.clamp(-GAIN_RANGE as f64, GAIN_RANGE as f64) * 10.0).round() / 10.0) as f32);
                }
            }
            if hr.drag_stopped() {
                setter.end_set_parameter(&bp.gain);
                setter.end_set_parameter(&bp.freq);
            }
        }
        let hot = hr.hovered() || hr.dragged();
        let hr = hr.on_hover_cursor(egui::CursorIcon::Grab);
        let on = bp.on.value();
        let fill = if !on { pal.line } else if hot { pal.text } else { pal.accent };
        painter.circle_filled(c, if hot { 8.5 } else { 7.5 }, fill);
        painter.circle_stroke(c, if hot { 8.5 } else { 7.5 }, Stroke::new(1.5_f32, pal.surface));
        paint_centered(&painter, c, text(&format!("{}", i + 1), bold(), 9.5, if on { pal.btn_text.max_contrast(fill) } else { pal.muted }, 0.0));
        if hot {
            let tip = format!("{} · {} · {}", b.name.to_uppercase(), fmt_hz(bp.freq.value()), crate::fmt_db(bp.gain.value()));
            painter.text(c + Vec2::new(0.0, -14.0), Align2::CENTER_BOTTOM, tip, FontId::new(9.5, mono()), pal.text);
        }
        drop(hr);
    }

    // crosshair readout
    if let Some(pos) = resp.hover_pos().filter(|p| plot.contains(*p)) {
        let f = f_of(&plot, pos.x);
        clip.vline(pos.x, plot.y_range(), Stroke::new(1.0_f32, pal.muted.gamma_multiply(0.5)));
        let db = total(f);
        let label = if db.is_nan() { fmt_hz(f as f32) } else { format!("{} · {}", fmt_hz(f as f32), crate::fmt_db(db as f32)) };
        painter.text(Pos2::new(plot.right() - 6.0, plot.bottom() - 6.0), Align2::RIGHT_BOTTOM, label, FontId::new(10.0, mono()), pal.text);
    }
    painter.rect_stroke(outer, CornerRadius::ZERO, Stroke::new(1.0_f32, pal.line), StrokeKind::Inside);
}

trait MaxContrast {
    fn max_contrast(self, bg: Color32) -> Color32;
}
impl MaxContrast for Color32 {
    /// Pick ink or paper for text on `bg`.
    fn max_contrast(self, bg: Color32) -> Color32 {
        let l = 0.299 * bg.r() as f32 + 0.587 * bg.g() as f32 + 0.114 * bg.b() as f32;
        if l > 150.0 { INK } else { PAPER }
    }
}

fn hint(m: Move, lo: f32, hi: f32) -> String {
    let _ = (lo, hi);
    match m {
        Move::Boost => "boost".into(),
        Move::Cut => "cut".into(),
        Move::Either => "boost / cut".into(),
    }
}

pub fn create(params: Arc<EqParams>, shared: Arc<Shared>) -> Option<Box<dyn Editor>> {
    create_egui_editor(
        params.editor_state.clone(),
        Analyzer::default(),
        |ctx, _| install_fonts(ctx),
        move |ctx, setter, analyzer| {
            let dark = crate::theme::is_dark();
            let pal = if dark { DARK } else { LIGHT };
            apply_style(ctx, &pal, dark);
            let def = params.def;
            let spectrum = *params.spectrum.read().unwrap();
            if spectrum {
                analyzer.update(&shared, ctx.input(|i| i.stable_dt));
                ctx.request_repaint();
            }

            egui::CentralPanel::default()
                .frame(egui::Frame::new().fill(pal.bg).inner_margin(egui::Margin { left: MARGIN as i8, right: MARGIN as i8, top: 14, bottom: 10 }))
                .show(ctx, |ui| {
                    // header
                    ui.horizontal(|ui| {
                        logo(ui, &pal, 38.0);
                        ui.add_space(2.0);
                        ui.vertical(|ui| {
                            ui.add_space(3.0);
                            ui.label(text("ALBION ROAD AUDIO", bold(), 16.0, pal.text, 1.4));
                            ui.label(text("SURGICAL EQ SERIES", mono(), 9.0, pal.muted, 1.5));
                        });
                        ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                            if chip(ui, &pal, if dark { "Light mode" } else { "Dark mode" }, false, 0.0).clicked() {
                                crate::theme::set_dark(!dark);
                            }
                            if chip(ui, &pal, "Spectrum", spectrum, 0.0).clicked() {
                                *params.spectrum.write().unwrap() = !spectrum;
                            }
                            ui.add_space(10.0);
                            let mut j = LayoutJob::default();
                            run(&mut j, &format!("{} ", def.name), sans(), 28.0, pal.text, -1.0);
                            run(&mut j, "EQ", serif_italic(), 28.0, pal.accent, 0.0);
                            ui.label(j);
                        });
                    });
                    ui.add_space(10.0);
                    hline(ui, &pal);
                    ui.add_space(12.0);

                    draw_graph(ui, &pal, setter, &params, shared.sample_rate(), spectrum.then_some(&*analyzer));
                    ui.add_space(14.0);

                    // knob row
                    ui.horizontal(|ui| {
                        ui.spacing_mut().item_spacing.x = 0.0;
                        let sep = |ui: &mut egui::Ui| {
                            let (r, _) = ui.allocate_exact_size(Vec2::new(0.0, 226.0), Sense::hover());
                            ui.painter().vline(r.left(), r.y_range(), Stroke::new(1.0_f32, pal.line));
                        };
                        let hp_idx = params.hp.value() as usize;
                        column(
                            ui, &pal, setter, "Low cut", "high-pass", &params.hp_on,
                            if hp_idx == 0 { "OFF".into() } else { format!("{:.0} Hz", HP_STEPS[hp_idx.min(15)]) },
                            |ui, en| knob(ui, &pal, setter, &params.hp, &HP_KNOB, en),
                            |ui, _| fixed_box(ui, &pal, HP_SLOPE),
                        );
                        for (i, b) in def.bands.iter().enumerate() {
                            sep(ui);
                            let bp = &params.bands[i];
                            column(
                                ui, &pal, setter, &format!("{} · {}", i + 1, b.name), &hint(b.hint, b.min, b.max), &bp.on,
                                crate::fmt_db(bp.gain.value()),
                                |ui, en| knob(ui, &pal, setter, &bp.gain, &GAIN_KNOB, en),
                                |ui, en| freq_slider(ui, &pal, setter, &bp.freq, b.min, b.max, en),
                            );
                        }
                        sep(ui);
                        let lp_idx = params.lp.value() as usize;
                        column(
                            ui, &pal, setter, "High cut", "low-pass", &params.lp_on,
                            if lp_idx == 0 { "OFF".into() } else { format!("{:.0} kHz", LP_STEPS_KHZ[lp_idx.min(11)]) },
                            |ui, en| knob(ui, &pal, setter, &params.lp, &LP_KNOB, en),
                            |ui, _| fixed_box(ui, &pal, LP_SLOPE),
                        );
                    });

                    // footer
                    ui.with_layout(Layout::bottom_up(Align::Min), |ui| {
                        ui.horizontal(|ui| {
                            ui.label(text("DRAG KNOBS, SLIDERS OR GRAPH HANDLES · SHIFT FINE · DOUBLE-CLICK RESETS", mono(), 8.5, pal.muted, 1.0));
                            // The tagline only fits beside the help text in windows with 4+ bands.
                            if ui.available_width() > 250.0 {
                                ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                                    ui.label(text("INDEPENDENT · ORIGINAL · MADE WITH CARE", mono(), 8.5, pal.muted, 1.0));
                                });
                            }
                        });
                        ui.add_space(4.0);
                        hline(ui, &pal);
                    });
                });
        },
    )
}
