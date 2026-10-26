// Définitions de ui/theme.hpp — feuilles de style du module
#include <ui/theme.hpp>

namespace ui {

String CssColor(Color c) { return ColorToHex(c); }

Palette Palette::Of(Theme theme) {
	Palette p;
	switch (theme) {
	case Theme::Dark:
		p.appBg = Rgba(18, 20, 28);
		p.panel = Rgba(30, 32, 44, 235);
		p.surface = Rgba(40, 43, 58);
		p.field = Rgba(20, 24, 36, 230);
		p.hover = Rgba(54, 58, 78);
		p.pressed = Rgba(32, 35, 48);
		p.border = Rgba(70, 76, 100);
		p.borderStrong = Rgba(96, 104, 136);
		p.text = Rgba(230, 232, 240);
		p.muted = Rgba(150, 156, 178);
		p.onAccent = Rgba(255, 255, 255);
		p.accent = Rgba(64, 140, 240);
		p.accentHover = Rgba(90, 160, 250);
		p.accentPressed = Rgba(44, 110, 210);
		p.danger = Rgba(220, 80, 70);
		p.success = Rgba(80, 200, 120);
		p.warning = Rgba(235, 160, 60);
		p.scrollThumb = Rgba(255, 255, 255, 70);
		p.scrollTrack = Rgba(255, 255, 255, 18);
		break;
	case Theme::Light:
		p.appBg = Rgba(236, 238, 242);
		p.panel = Rgba(250, 250, 252);
		p.surface = Rgba(240, 241, 245);
		p.field = Rgba(255, 255, 255);
		p.hover = Rgba(226, 230, 238);
		p.pressed = Rgba(210, 214, 224);
		p.border = Rgba(196, 200, 212);
		p.borderStrong = Rgba(160, 166, 182);
		p.text = Rgba(30, 32, 40);
		p.muted = Rgba(110, 116, 132);
		p.onAccent = Rgba(255, 255, 255);
		p.accent = Rgba(40, 110, 220);
		p.accentHover = Rgba(60, 130, 235);
		p.accentPressed = Rgba(30, 90, 190);
		p.danger = Rgba(200, 50, 50);
		p.success = Rgba(40, 150, 80);
		p.warning = Rgba(200, 130, 30);
		p.scrollThumb = Rgba(0, 0, 0, 70);
		p.scrollTrack = Rgba(0, 0, 0, 14);
		break;
	case Theme::Studio:
		p.appBg = Rgba(32, 32, 32);
		p.panel = Rgba(43, 43, 43);
		p.surface = Rgba(52, 52, 52);
		p.field = Rgba(30, 30, 30);
		p.hover = Rgba(64, 64, 64);
		p.pressed = Rgba(36, 36, 36);
		p.border = Rgba(62, 62, 62);
		p.borderStrong = Rgba(84, 84, 84);
		p.text = Rgba(214, 214, 214);
		p.muted = Rgba(140, 140, 140);
		p.onAccent = Rgba(255, 255, 255);
		p.accent = Rgba(58, 121, 216);
		p.accentHover = Rgba(78, 140, 230);
		p.accentPressed = Rgba(44, 100, 190);
		p.danger = Rgba(214, 76, 76);
		p.success = Rgba(96, 180, 96);
		p.warning = Rgba(222, 160, 64);
		p.scrollThumb = Rgba(255, 255, 255, 60);
		p.scrollTrack = Rgba(255, 255, 255, 12);
		p.radius = 3.f;
		p.fontSize = 13.f;
		break;
	case Theme::Aero:
		p.appBg = Rgba(28, 56, 92);
		p.panel = Rgba(70, 120, 190, 165);
		p.surface = Rgba(80, 140, 210, 150);
		p.field = Rgba(240, 246, 255, 235);
		p.hover = Rgba(100, 170, 240, 210);
		p.pressed = Rgba(40, 100, 180, 230);
		p.border = Rgba(200, 225, 255, 160);
		p.borderStrong = Rgba(230, 242, 255, 220);
		p.text = Rgba(255, 255, 255);
		p.muted = Rgba(210, 225, 245);
		p.onAccent = Rgba(255, 255, 255);
		p.accent = Rgba(60, 140, 220, 220);
		p.accentHover = Rgba(90, 170, 245, 235);
		p.accentPressed = Rgba(40, 110, 200, 240);
		p.danger = Rgba(230, 90, 80);
		p.success = Rgba(90, 210, 130);
		p.warning = Rgba(245, 180, 70);
		p.scrollThumb = Rgba(255, 255, 255, 110);
		p.scrollTrack = Rgba(255, 255, 255, 30);
		p.radius = 8.f;
		p.glass = true;
		break;
	}
	return p;
}

String StructureCss() {
	return R"css(
/* ── Structure des widgets : direction, alignements, débordement ── */
root { display: stack; align-items: stretch; }
column, panel, scroll-view, popup, modal, tree-node, tree-children, expander, expander-body,
tabview, tab-body, tab-page, color-picker, calendar, table, table-body, listbox, selection-list,
combo-list, splitter.vertical, splitter-pane { flex-direction: column; }
row, wrap, button, checkbox, radio, menubar, menu, menu-item, selectable, list-item, combo-item, tab-bar, combo, tree-header,
expander-header, number-input, table-header, table-header-cell, table-row, table-cell, date-picker,
calendar-header, splitter.horizontal, color-picker-row, tree-toggle, titlebar, statusbar { flex-direction: row; align-items: center; }
window-frame, window-content { flex-direction: column; }
window-content { flex-grow: 1; min-height: 0; }
titlebar { flex-shrink: 0; } statusbar { flex-shrink: 0; }
.color-picker-row { flex-direction: row; align-items: center; }
wrap { flex-wrap: wrap; }
stack, knob, switch, color-swatch, slider, progress, scrollbar { display: stack; }
grid { display: grid; }
scroll-view, listbox, table-body, textarea { overflow: auto; }
input { overflow: hidden; }
spacer { flex-grow: 1; }
separator { align-self: stretch; }
separator.horizontal { height: 1px; }
separator.vertical { width: 1px; }
button, combo, menu, menu-item, tab, selectable, list-item, combo-item, tree-header,
expander-header, checkbox, radio, switch, table-row, calendar-day, date-picker, color-swatch { cursor: pointer; }
button { justify-content: center; }
input, textarea, number-value { cursor: text; }
popup { position: fixed; z-index: 100; }
modal { position: fixed; z-index: 1000; justify-content: center; align-items: center; }
tab-body, tab-page { flex-grow: 1; }
combo-text, menu-text, number-value, tree-title, expander-title { flex-grow: 1; }
slider { align-items: center; }
slider.horizontal .slider-track { width: 100%; }
slider.vertical { justify-content: center; }
slider.vertical .slider-track { height: 100%; }
knob-part { width: 100%; height: 100%; }
table-resizer { position: absolute; anchor: right; right: 0; top: 0; width: 6px; height: 100%; cursor: col-resize; }
table-header-cell { position: relative; }
splitter.horizontal > splitter-handle { width: 6px; align-self: stretch; cursor: ew-resize; }
splitter.vertical > splitter-handle { height: 6px; align-self: stretch; cursor: ns-resize; }
splitter-pane { overflow: hidden; align-items: stretch; }
splitter.horizontal, splitter.vertical { align-items: stretch; }
splitter-pane > * { flex-grow: 1; }
calendar-day { justify-content: center; text-align: center; }
heading, text, label, badge { flex-shrink: 1; }
)css";
}

String ThemeCss(const Palette &p) {
	String css;
	css += "root {\n";
	css += String("  --app-bg: ") + CssColor(p.appBg) + ";\n";
	css += String("  --panel: ") + CssColor(p.panel) + ";\n";
	css += String("  --surface: ") + CssColor(p.surface) + ";\n";
	css += String("  --field: ") + CssColor(p.field) + ";\n";
	// Popups et menus : opaques (rien ne doit transparaître sous un menu).
	css += String("  --popup: ") + CssColor(WithAlpha(p.field, 1.f)) + ";\n";
	css += String("  --hover: ") + CssColor(p.hover) + ";\n";
	css += String("  --pressed: ") + CssColor(p.pressed) + ";\n";
	css += String("  --border: ") + CssColor(p.border) + ";\n";
	css += String("  --border-strong: ") + CssColor(p.borderStrong) + ";\n";
	css += String("  --text: ") + CssColor(p.text) + ";\n";
	css += String("  --muted: ") + CssColor(p.muted) + ";\n";
	css += String("  --on-accent: ") + CssColor(p.onAccent) + ";\n";
	css += String("  --accent: ") + CssColor(p.accent) + ";\n";
	css += String("  --accent-hover: ") + CssColor(p.accentHover) + ";\n";
	css += String("  --accent-pressed: ") + CssColor(p.accentPressed) + ";\n";
	css += String("  --danger: ") + CssColor(p.danger) + ";\n";
	css += String("  --success: ") + CssColor(p.success) + ";\n";
	css += String("  --warning: ") + CssColor(p.warning) + ";\n";
	css += String("  --radius: ") + String::From(p.radius, 1) + "px;\n";
	css += String("  color: var(--text); font-size: ") + String::From(p.fontSize, 1) + "px;\n";
	css += String("  accent-color: var(--accent); caret-color: var(--text); placeholder-color: var(--muted);\n");
	css += String("  selection-color: ") + CssColor(WithAlpha(p.accent, 0.35f)) + ";\n";
	css += String("  scrollbar-thumb-color: ") + CssColor(p.scrollThumb) + ";\n";
	css += String("  scrollbar-track-color: ") + CssColor(p.scrollTrack) + ";\n";
	css += "}\n";
	css += R"css(
/* ── Conteneurs ── */
row, column, wrap, grid { gap: 6px; }
panel { background-color: var(--panel); border: 1px solid var(--border); border-radius: calc(var(--radius) + 2px);
        padding: 10px; gap: 8px; }
scroll-view { gap: 6px; }
separator { background-color: var(--border); }
heading { font-weight: bold; }
heading.h1 { font-size: 24px; } heading.h2 { font-size: 20px; } heading.h3 { font-size: 17px; }
heading.h4 { font-size: 15px; } heading.h5, heading.h6 { font-size: 14px; }
badge { background-color: var(--accent); color: var(--on-accent); padding: 1px 7px; border-radius: 9px; font-size: 12px; }
:disabled { opacity: 0.5; }

/* ── Boutons ── */
button { background-color: var(--surface); border: 1px solid var(--border); border-radius: var(--radius);
         padding: 5px 12px; gap: 6px; }
button:hover { background-color: var(--hover); border-color: var(--border-strong); }
button:active { background-color: var(--pressed); }
button:focus-visible { outline: 2px var(--accent); }
button.primary { background-color: var(--accent); border-color: var(--accent); color: var(--on-accent); }
button.primary:hover { background-color: var(--accent-hover); }
button.primary:active { background-color: var(--accent-pressed); }
button.danger { background-color: var(--danger); border-color: var(--danger); color: var(--on-accent); }
button.icon-button { padding: 4px; }
button.flat { background-color: transparent; border-color: transparent; }
button.flat:hover { background-color: var(--hover); }

/* ── Cases, radios, interrupteurs ── */
checkbox, radio { gap: 8px; }
.checkbox-box { width: 16px; height: 16px; border: 1.5px solid var(--border-strong); border-radius: 3px;
                background-color: var(--field); padding: 1px; }
checkbox:hover .checkbox-box { border-color: var(--accent); }
checkbox:checked .checkbox-box { background-color: var(--accent); border-color: var(--accent); }
.checkbox-mark { color: var(--on-accent); width: 100%; height: 100%; }
.radio-box { width: 16px; height: 16px; border: 1.5px solid var(--border-strong); border-radius: 8px;
             background-color: var(--field); padding: 2px; }
radio:hover .radio-box { border-color: var(--accent); }
radio:checked .radio-box { border-color: var(--accent); }
.radio-mark { color: var(--accent); width: 100%; height: 100%; }
switch { width: 38px; height: 20px; border-radius: 10px; background-color: var(--surface);
         border: 1px solid var(--border); padding: 2px; }
switch:checked { background-color: var(--accent); border-color: var(--accent); }
.switch-thumb { width: 14px; height: 14px; border-radius: 7px; background-color: var(--text); }
switch:checked .switch-thumb { background-color: var(--on-accent); }

/* ── Curseurs, progression, potentiomètres ── */
slider.horizontal { width: 160px; height: 20px; }
slider.vertical { width: 20px; height: 140px; }
.slider-track { background-color: var(--field); border: 1px solid var(--border); border-radius: 3px; }
slider.horizontal .slider-track { height: 6px; }
slider.vertical .slider-track { width: 6px; }
.slider-fill { background-color: var(--accent); border-radius: 3px; }
.slider-thumb { width: 14px; height: 14px; border-radius: 7px; background-color: var(--text);
                border: 1px solid var(--border-strong); }
slider:hover .slider-thumb { background-color: var(--on-accent); }
slider:focus-visible .slider-thumb { outline: 2px var(--accent); }
slider.hue .slider-track { background-image: hue-gradient(to right); height: 10px; }
slider.hue .slider-fill { background-color: transparent; }
slider.alpha .slider-track { height: 10px; }
slider.alpha .slider-fill { background-color: transparent; }
progress { width: 160px; height: 8px; background-color: var(--field); border-radius: 4px;
           border: 1px solid var(--border); }
.progress-fill { background-color: var(--accent); border-radius: 4px; }
spinner { width: 22px; height: 22px; color: var(--accent); }
knob { width: 44px; height: 44px; }
.knob-track { color: var(--border); }
.knob-value { color: var(--accent); }
.knob-needle { color: var(--text); }
scrollbar.horizontal { height: 10px; width: 160px; }
scrollbar.vertical { width: 10px; height: 140px; }
scrollbar { background-color: var(--field); border-radius: 5px; }
.scrollbar-thumb { background-color: var(--border-strong); border-radius: 5px; }
scrollbar.horizontal .scrollbar-thumb { height: 100%; }
scrollbar.vertical .scrollbar-thumb { width: 100%; }

/* ── Champs ── */
input, textarea, drag-value, number-value { background-color: var(--field); border: 1px solid var(--border);
         border-radius: var(--radius); padding: 4px 8px; }
input { min-width: 80px; }
input:hover, textarea:hover, drag-value:hover { border-color: var(--border-strong); }
input:focus, textarea:focus, drag-value:focus, number-value:focus { border-color: var(--accent); }
textarea { min-height: 60px; }
textarea.code, textarea.log { font-family: monospace; tab-size: 4; }
drag-value { text-align: center; cursor: ew-resize; min-width: 60px; }
number-input { gap: 2px; }
.number-step { padding: 2px 8px; }
number-value { text-align: center; min-width: 48px; }

/* ── Listes ── */
combo { background-color: var(--surface); border: 1px solid var(--border); border-radius: var(--radius);
        padding: 4px 8px; gap: 6px; min-width: 100px; }
combo:hover { border-color: var(--border-strong); }
combo:open { border-color: var(--accent); }
.combo-arrow { width: 12px; height: 12px; color: var(--muted); }
.combo-popup, .menu-popup { background-color: var(--popup); border: 1px solid var(--accent); border-radius: var(--radius);
              padding: 4px 0; }
.combo-list { gap: 0; }
combo-item, menu-item { padding: 4px 12px; gap: 12px; }
combo-item:hover, menu-item:hover, menu-item:open { background-color: var(--accent); color: var(--on-accent); }
combo-item:selected { color: var(--accent); }
combo-item:selected:hover { color: var(--on-accent); }
.combo-group-arrow, .menu-arrow { width: 10px; height: 10px; }
listbox { background-color: var(--field); border: 1px solid var(--border); border-radius: var(--radius); padding: 2px;
          min-height: 60px; }
list-item, selectable { padding: 3px 8px; border-radius: 3px; }
list-item:hover, selectable:hover { background-color: var(--hover); }
list-item:selected, selectable:selected { background-color: var(--accent); color: var(--on-accent); }
tree-node { gap: 0; }
tree-header { padding: 2px 4px; gap: 4px; border-radius: 3px; }
tree-header:hover { background-color: var(--hover); }
tree-header:selected { background-color: var(--accent); color: var(--on-accent); }
tree-toggle { width: 14px; height: 14px; }
.tree-arrow { width: 100%; height: 100%; color: var(--muted); }
tree-children { padding-left: 16px; }
expander { gap: 0; }
expander-header { padding: 4px 6px; gap: 6px; background-color: var(--surface); border-radius: var(--radius); }
expander-header:hover { background-color: var(--hover); }
.expander-arrow { width: 12px; height: 12px; color: var(--muted); }
expander-body { padding: 6px 4px 4px 14px; gap: 6px; }
tab-bar { gap: 2px; border-bottom: 1px solid var(--border); }
tab { padding: 5px 14px; border-radius: var(--radius) var(--radius) 0 0; color: var(--muted); }
tab:hover { background-color: var(--hover); color: var(--text); }
tab:selected { background-color: var(--surface); color: var(--text); border-bottom: 2px solid var(--accent); }
tab-page { padding: 8px 2px; gap: 6px; }

/* ── Menus ── */
menubar { background-color: var(--panel); padding: 2px 4px; gap: 0; }
menu { padding: 4px 10px; border-radius: var(--radius); }
menu:hover, menu:open { background-color: var(--hover); }
.menu-shortcut { color: var(--muted); font-size: 12px; }
menu-item:hover .menu-shortcut { color: var(--on-accent); }
menu-separator { height: 1px; background-color: var(--border); margin: 4px 6px; }
.menu-popup { min-width: 160px; }

/* ── Dialogues ── */
modal { background-color: #00000088; }

/* ── Chrome de fenêtre ── */
titlebar { height: 36px; gap: 8px; padding: 3px 4px 3px 10px; background-color: var(--panel); }
titlebar.transparent { background-color: transparent; }
.titlebar-title { font-size: 15px; }
.titlebar-icon { color: var(--accent); }
.titlebar-move { color: var(--muted); }
.titlebar-button { width: 40px; height: 28px; padding: 0; background-color: transparent; border-width: 0; }
.titlebar-button:hover { background-color: var(--hover); }
#titlebar-close:hover { background-color: var(--danger); color: #ffffff; }
statusbar { height: 24px; gap: 6px; padding: 0 2px 0 10px; }
.statusbar-text { color: var(--muted); flex-grow: 1; text-overflow: ellipsis; overflow: hidden; }
.statusbar-grip { color: var(--muted); cursor: nwse-resize; }

/* ── Tables ── */
table { background-color: var(--field); border: 1px solid var(--border); border-radius: var(--radius); }
table-header { background-color: var(--surface); border-bottom: 1px solid var(--border); }
table-header-cell { padding: 5px 8px; gap: 4px; }
table-header-cell:hover { background-color: var(--hover); }
.sort-indicator { color: var(--accent); font-size: 11px; }
.table-resizer:hover { background-color: var(--accent); }
table-row { min-height: 24px; }
table-row:nth-child(even) { background-color: #ffffff08; }
table-row:hover { background-color: var(--hover); }
table-row:selected { background-color: var(--accent); color: var(--on-accent); }
table-cell { padding: 3px 8px; overflow: hidden; }
splitter-handle { background-color: var(--border); }
splitter-handle:hover { background-color: var(--accent); }

/* ── Graphes de nœuds ── */
node-graph { overflow: hidden; }
graph-node { background-color: #26282fF0; border: 1px solid #00000080; border-radius: 4px; flex-direction: column;
             box-shadow: 0 2px 6px #00000080; }
graph-node:selected { border: 2px solid #ffc83c; }
graph-node-header { flex-direction: row; align-items: center; padding: 0 8px; border-radius: 4px 4px 0 0; }
.graph-node-title { color: #f0f2f8; font-weight: bold; }
graph-node-body { padding: 6px; gap: 4px; overflow: hidden; }
graph-node-grip { color: #ffffff5a; cursor: nwse-resize; right: 0; bottom: 0; }

/* ── Graphiques ── */
plot { background-color: var(--field); border: 1px solid var(--border); border-radius: var(--radius);
       min-width: 120px; min-height: 80px; color: var(--text); }

/* ── Couleurs, dates ── */
color-swatch { width: 28px; height: 20px; border-radius: 3px; background-image: checkerboard;
               border: 1px solid var(--border-strong); }
.color-swatch-fill { width: 100%; height: 100%; border-radius: 2px; }
color-picker { gap: 8px; padding: 8px; width: 220px; background-color: var(--panel); border: 1px solid var(--border);
               border-radius: var(--radius); }
color-picker slider.horizontal { width: 100%; }
color-sv { height: 150px; border-radius: 3px; }
.color-sv-thumb { width: 10px; height: 10px; border-radius: 5px; border: 2px solid #ffffff; }
.color-hex { min-width: 90px; }
calendar { gap: 4px; padding: 8px; background-color: var(--panel); border: 1px solid var(--border);
           border-radius: var(--radius); width: 240px; }
.calendar-title { text-align: center; font-weight: bold; }
.calendar-weekday { color: var(--muted); text-align: center; font-size: 12px; }
calendar-day { padding: 4px 0; border-radius: 3px; }
calendar-day:hover { background-color: var(--hover); }
calendar-day:checked { background-color: var(--accent); color: var(--on-accent); }
calendar-day.outside { opacity: 0; }
date-picker { background-color: var(--surface); border: 1px solid var(--border); border-radius: var(--radius);
              padding: 4px 10px; }
)css";
	if (p.glass)
		css += R"css(
/* ── Verre (Aero) ── */
panel, popup.menu-popup, .combo-popup, color-picker, calendar {
	background-image: linear-gradient(to bottom, #ffffff38, #00000000); gloss: 0.30; glow: 0.35; glow-color: #bfe0ff;
}
button { background-image: linear-gradient(to bottom, #ffffff40, #00000010); gloss: 0.45; glow: 0.25;
         border-radius: 10px; }
button:hover { glow: 0.6; }
input, textarea, drag-value, number-value, listbox, table, plot { color: #102030; }
)css";
	return css;
}

} // namespace ui
