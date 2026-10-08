// Layout engine for the Sogou SSF compatibility layer.
//
// See SsfLayout.h for the contract, and docs/ssf-skin.md for the derivation of
// the skin parameters consumed here.

#include "SsfLayout.h"

#include <algorithm>

namespace weasel {
namespace ssf {
namespace {

inline int S(int value, double scale) {
  const double v = value * scale;
  // round-half-away-from-zero, so a 0.5px edge never drifts consistently one
  // way across a whole window
  return static_cast<int>(v >= 0 ? v + 0.5 : v - 0.5);
}

inline int ClampMin(int v, int lo) {
  return v < lo ? lo : v;
}

// Build one status-bar icon box. `index` picks which of the button's image
// names to show; most buttons have exactly one per state, the cn/en button has
// up to three (chinese / english / "other").
SsfLayoutResult::StatusIconBox MakeIconBox(const StatusButton& button,
                                  int index,
                                  const std::string& state) {
  SsfLayoutResult::StatusIconBox box;
  if (!button.display || button.Empty()) return box;

  const std::vector<std::string>* list = &button.names;
  if (state == "hover" && !button.names_hover.empty())
    list = &button.names_hover;
  else if (state == "down" && !button.names_down.empty())
    list = &button.names_down;

  if (list->empty()) return box;
  const size_t i = static_cast<size_t>((std::max)(0, index));
  box.normal = (*list)[i < list->size() ? i : list->size() - 1];
  box.visible = !box.normal.empty();
  return box;
}

// Choose the hover/press state string for a button index.
std::string StateFor(const LayoutInput& input, int button_index) {
  if (input.pressed_button == button_index) return "down";
  if (input.hovered_button == button_index) return "hover";
  return "normal";
}

}  // namespace

SsfLayoutResult SsfLayoutEngine::Compute(const Skin& skin,
                                const LayoutOptions& options,
                                const LayoutInput& input,
                                const ITextMeasurer& measurer) const {
  SsfLayoutResult out;
  const double scale = options.scale > 0.0 ? options.scale : 1.0;

  const bool horizontal = options.orientation == Orientation::Horizontal;
  const Scheme& scheme =
      horizontal ? skin.Horizontal(options.prefer_split_background)
                 : skin.Vertical(options.prefer_split_background);

  const BackgroundMode mode =
      (options.prefer_split_background && scheme.IsSplitBackground())
          ? BackgroundMode::Split
          : BackgroundMode::Single;

  // -------------------------------------------------------------------------
  // Text metrics
  // -------------------------------------------------------------------------
  const int pinyin_line_h = ClampMin(measurer.LineHeight(ITextMeasurer::PINYIN), 1);
  const int cand_line_h = ClampMin(measurer.LineHeight(ITextMeasurer::CANDIDATE), 1);

  // -------------------------------------------------------------------------
  // Candidate row extents
  // -------------------------------------------------------------------------
  const size_t cand_count = input.texts.size();
  const int inner_left = S(scheme.zhongwen_insets.left, scale);
  const int inner_right = S(scheme.zhongwen_insets.right, scale);
  const int inner_top = S(scheme.zhongwen_insets.top, scale);
  const int inner_bottom = S(scheme.zhongwen_insets.bottom, scale);

  // A small gap between adjacent candidates. Sogou's own editor does not expose
  // this, so it is derived rather than read; kept in one place so it is easy to
  // retune against a reference screenshot.
  const bool color_p = horizontal && mode == BackgroundMode::Single &&
      skin.skin_name.find("Color-P") != std::string::npos;
  const int cand_gap = S(color_p ? 22 : 6, scale);
  const int label_gap = S(2, scale);

  struct CandMetrics {
    Size label;
    Size text;
    Size comment;
    int cell_w = 0;  // label + gap + text + (gap + comment)
  };
  std::vector<CandMetrics> metrics(cand_count);
  for (size_t i = 0; i < cand_count; ++i) {
    CandMetrics& m = metrics[i];
    if (i < input.labels.size() && !input.labels[i].empty())
      m.label = measurer.Measure(input.labels[i], ITextMeasurer::LABEL);
    m.text = measurer.Measure(input.texts[i], ITextMeasurer::CANDIDATE);
    if (i < input.comments.size() && !input.comments[i].empty())
      m.comment = measurer.Measure(input.comments[i], ITextMeasurer::COMMENT);

    m.cell_w = m.label.cx;
    if (m.label.cx > 0) m.cell_w += label_gap;
    m.cell_w += m.text.cx;
    if (m.comment.cx > 0) m.cell_w += label_gap + m.comment.cx;
  }

  int cand_row_w = inner_left + inner_right;
  if (cand_count) {
    // Any trailing controls are an explicit caller option. Color-P's SSF
    // artwork does not define controls in the candidate strip.
    cand_row_w += S(options.trailing_controls_width, scale);
    for (size_t i = 0; i < cand_count; ++i) {
      cand_row_w += metrics[i].cell_w;
      if (i + 1 < cand_count) cand_row_w += cand_gap;
    }
  }

  const int cand_row_h = cand_line_h + inner_top + inner_bottom;

  // -------------------------------------------------------------------------
  // Pinyin strip extents
  // -------------------------------------------------------------------------
  Size preedit_size;
  if (!input.preedit.empty())
    preedit_size = measurer.Measure(input.preedit, ITextMeasurer::PINYIN);

  Size aux_size;
  if (!input.aux.empty())
    aux_size = measurer.Measure(input.aux, ITextMeasurer::PINYIN);

  const int py_left = S(scheme.pinyin_insets.left, scale);
  const int py_right = S(scheme.pinyin_insets.right, scale);
  const int py_top = S(scheme.pinyin_insets.top, scale);
  const int py_bottom = S(scheme.pinyin_insets.bottom, scale);

  // Height of the pinyin strip: whichever text is present, plus its margins.
  // When neither is present the strip collapses to zero and is not drawn.
  const int preedit_text_h = input.preedit.empty() ? 0 : pinyin_line_h;
  const int aux_text_h = input.aux.empty() ? 0 : pinyin_line_h;

  int pinyin_strip_h = 0;
  if (preedit_text_h > 0 || aux_text_h > 0) {
    pinyin_strip_h = py_top + preedit_text_h;
    if (aux_text_h > 0) pinyin_strip_h += aux_text_h;
    pinyin_strip_h += py_bottom;
  }

  // -------------------------------------------------------------------------
  // The candidate strip's top edge is where the pinyin strip's bottom edge is.
  //
  // There is deliberately no extra gap here. pinyin_marge.bottom and
  // zhongwen_marge.top are the *internal* vertical padding of their own strips,
  // not space between them, and the two bitmaps abut by design
  // (skin2_1.png is a pink bar; skin1_2.png opens with the candidate strip's
  // pink top border). Inserting a gap produced a fully transparent band between
  // them -- a visible seam that Sogou never draws.
  //
  // `separator` is the one thing that legitimately reserves room between the
  // strips, and scheme.gap is 1 in exactly that case.
  const int strip_gap = (pinyin_strip_h > 0) ? S(scheme.gap, scale) : 0;
  const int cand_strip_h = cand_row_h;
  int cand_strip_top = pinyin_strip_h + strip_gap;

  // Final strip heights after applying the artwork's own minimum size. The text
  // is centred within whatever the strip ends up being, so a skin whose bitmap
  // is taller than its margin arithmetic implies still gets correctly placed
  // text instead of text pinned to the top of a taller background.
  int final_py_h = pinyin_strip_h;
  int final_cand_h = cand_strip_h;

  int pinyin_strip_w = py_left + py_right;
  pinyin_strip_w += (std::max)(preedit_size.cx, aux_size.cx);

  // -------------------------------------------------------------------------
  // Window width and height
  // -------------------------------------------------------------------------
  int content_w = (std::max)(cand_row_w, pinyin_strip_w);
  content_w = ClampMin(content_w, S(60, scale));

  int window_h = (std::max)(1, pinyin_strip_h + strip_gap + cand_strip_h);
  int window_w = content_w;

  if (options.enforce_native_minimum) {
    // Never let the window become narrower or shorter than the artwork it has to
    // hold, so the 9-slice corners are not squashed and the bitmaps are not
    // distorted. In split mode the two strips come from different bitmaps, so
    // require the union of what each needs.
    if (mode == BackgroundMode::Single) {
      window_w = (std::max)(window_w, S(options.native_main_bg.cx, scale));
      window_h = (std::max)(window_h, S(options.native_main_bg.cy, scale));
    } else {
      const int py_min_w = S(options.native_pinyin_bg.cx, scale);
      const int ca_min_w = S(options.native_candidate_bg.cx, scale);
      const int py_min_h = S(options.native_pinyin_bg.cy, scale);
      const int ca_min_h = S(options.native_candidate_bg.cy, scale);
      window_w = (std::max)(window_w, (std::max)(py_min_w, ca_min_w));
      // Grow each strip to its own bitmap's height. Since the strips are
      // stacked, the window then needs the sum of the two.
      final_py_h = (std::max)(final_py_h, py_min_h);
      final_cand_h = (std::max)(final_cand_h, ca_min_h);
      window_h = (std::max)(window_h, final_py_h + strip_gap + final_cand_h);
    }
  }

  // -------------------------------------------------------------------------
  // Emit rectangles
  // -------------------------------------------------------------------------
  cand_strip_top = final_py_h + strip_gap;
  out.window.cx = window_w;
  out.window.cy = window_h;

  if (mode == BackgroundMode::Single) {
    out.main_bg = Rect{0, 0, window_w, window_h};
  } else {
    out.pinyin_bg = Rect{0, 0, window_w, final_py_h};
    out.candidate_bg =
        Rect{0, final_py_h + strip_gap, window_w, window_h};
    out.main_bg = Rect{0, 0, window_w, window_h};
  }

  out.has_pinyin_area = pinyin_strip_h > 0;

  // Centre the pinyin text block vertically inside the pinyin strip. Sogou
  // centres its composition string in the artwork; pinning it to
  // pinyin_marge.top made it sit visibly high whenever the bitmap was taller
  // than the margin arithmetic, which is the common case (28px of artwork
  // against 7 + line height + 6).
  const int py_content_h = preedit_text_h + aux_text_h;
  const int py_slack = (std::max)(0, final_py_h - py_content_h);
  int y = py_top + (std::max)(0, final_py_h - pinyin_strip_h) / 2;
  // Color-P's H1 artwork has six transparent rows above its visible pink
  // shoulder. Sogou positions its composition string against that visible
  // shoulder, not the bitmap's physical top, which puts the string three pixels
  // higher than the raw H1 margin arithmetic. Keep this skin-specific nudge
  // here rather than changing the meaning of pinyin_marge for every SSF skin.
  if (color_p) y = (std::max)(0, y - S(4, scale));

  if (preedit_text_h > 0) {
    out.pinyin_text = Rect{py_left, y, py_left + preedit_size.cx,
                          y + preedit_text_h};
    out.pinyin_text_size = preedit_size;
    out.pinyin_baseline = y + measurer.Ascent(ITextMeasurer::PINYIN);
    y += preedit_text_h;
    if (aux_text_h > 0) {
      out.aux_text = Rect{py_left, y, py_left + aux_size.cx, y + aux_text_h};
      out.aux_text_size = aux_size;
      out.has_aux_area = true;
      y += aux_text_h;
    }
  } else if (aux_text_h > 0) {
    out.aux_text = Rect{py_left, y, py_left + aux_size.cx, y + aux_text_h};
    out.aux_text_size = aux_size;
    out.has_aux_area = true;
    out.pinyin_baseline = y + measurer.Ascent(ITextMeasurer::PINYIN);
    y += aux_text_h;
  } else {
    // No composition at all: still report a baseline so callers have something
    // sensible to work with.
    out.pinyin_baseline = y + measurer.Ascent(ITextMeasurer::PINYIN);
  }

  // Likewise centre the candidate row inside the candidate strip. When the
  // strip is exactly as tall as the inset arithmetic asked for this is a no-op.
  const int cand_slack =
      (std::max)(0, final_cand_h - (inner_top + cand_line_h + inner_bottom));
  const int centring = cand_slack / 2;
  const int cand_row_top = cand_strip_top + centring;
  // The candidate text in the same H1 asset sits one pixel closer to the
  // separator than its raw inset. This aligns SimSun's ink box with Sogou
  // without moving the candidate hit target or the background border.
  const int cand_text_top =
      (std::max)(cand_row_top,
                 cand_row_top + inner_top - (color_p ? S(1, scale) : 0));
  out.candidate_baseline =
      cand_text_top + measurer.Ascent(ITextMeasurer::CANDIDATE);

  if (horizontal) {
    int x = inner_left;
    out.candidates.resize(cand_count);
    for (size_t i = 0; i < cand_count; ++i) {
      const CandMetrics& m = metrics[i];
      const int cell_left = x;
      CandidateBox& box = out.candidates[i];
      box.index = static_cast<int>(i);

      int px = x;
      if (m.label.cx > 0) {
        box.label = Rect{px, cand_text_top, px + m.label.cx,
                         cand_text_top + cand_line_h};
        px += m.label.cx + label_gap;
      }
      box.text = Rect{px, cand_text_top, px + m.text.cx,
                      cand_text_top + cand_line_h};
      px += m.text.cx;
      if (m.comment.cx > 0) {
        px += label_gap;
        box.comment = Rect{px, cand_text_top, px + m.comment.cx,
                           cand_text_top + cand_line_h};
        px += m.comment.cx;
      }

      box.box = Rect{cell_left, cand_row_top, px, cand_row_top + cand_row_h};
      x = px + cand_gap;
    }
  } else {
    // Vertical: one candidate per row, each spanning the full content width.
    const int row_h = cand_line_h + inner_top + inner_bottom;
    out.candidates.resize(cand_count);
    for (size_t i = 0; i < cand_count; ++i) {
      const CandMetrics& m = metrics[i];
      const int row_top = cand_row_top + static_cast<int>(i) * row_h;
      const int text_top = row_top + inner_top;
      CandidateBox& box = out.candidates[i];
      box.index = static_cast<int>(i);

      int px = inner_left;
      if (m.label.cx > 0) {
        box.label = Rect{px, text_top, px + m.label.cx, text_top + cand_line_h};
        px += m.label.cx + label_gap;
      }
      box.text = Rect{px, text_top, px + m.text.cx, text_top + cand_line_h};
      px += m.text.cx;
      if (m.comment.cx > 0) {
        px += label_gap;
        box.comment = Rect{px, text_top, px + m.comment.cx,
                           text_top + cand_line_h};
        px += m.comment.cx;
      }
      box.box = Rect{0, row_top, window_w, row_top + row_h};
    }
    // The window must be tall enough for every row.
    const int rows_h = static_cast<int>(cand_count) * row_h;
    const int wanted = cand_row_top + rows_h;
    if (wanted > out.window.cy) {
      out.window.cy = wanted;
      if (mode == BackgroundMode::Single)
        out.main_bg.bottom = wanted;
      else
        out.candidate_bg.bottom = wanted;
    }
  }

  out.highlighted = input.highlighted;
  if (input.highlighted >= 0 &&
      static_cast<size_t>(input.highlighted) < out.candidates.size()) {
    const CandidateBox& hb = out.candidates[input.highlighted];
    out.highlight = Rect{hb.box.left, cand_row_top, hb.box.right,
                         cand_row_top + (horizontal ? cand_row_h
                                                    : hb.box.Height())};
  }

  // -------------------------------------------------------------------------
  // Page indicator
  // -------------------------------------------------------------------------
  if (input.show_page_indicator) {
    out.has_page_indicator = true;
    // Right-aligned on the candidate row's baseline, inside the right inset.
    const int w = S(28, scale);
    const int right = window_w - inner_right;
    out.next_page = Rect{right - w, cand_row_top + inner_top, right,
                         cand_row_top + inner_top + cand_line_h};
    out.prev_page = Rect{right - w * 2, cand_row_top + inner_top,
                         right - w, cand_row_top + inner_top + cand_line_h};
  }

  // -------------------------------------------------------------------------
  // Status bar
  // -------------------------------------------------------------------------
  if (input.show_status_bar && skin.status.AnyVisible()) {
    const StatusBar& sb = skin.status;
    // The status bar is its own surface with its own background bitmap. Its
    // extent is derived from the declared button positions plus one icon slot,
    // never from a guess, so a skin with a wide menu icon still fits.
    Size slot = options.status_icon_slot;
    if (slot.cx <= 0) slot.cx = 26;
    if (slot.cy <= 0) slot.cy = 25;
    const int slot_w = S(slot.cx, scale);
    const int slot_h = S(slot.cy, scale);

    int bar_w = 0;
    int bar_h = 0;
    auto account = [&](const StatusButton& b) {
      if (!b.display || b.Empty()) return;
      bar_w = (std::max)(bar_w, S(b.pos.x, scale) + slot_w);
      bar_h = (std::max)(bar_h, S(b.pos.y, scale) + slot_h);
    };
    account(sb.cn_en);
    account(sb.biaodian);
    account(sb.quan_ban);
    account(sb.fan_jian);
    account(sb.menu);
    bar_w = ClampMin(bar_w, S(40, scale));
    bar_h = ClampMin(bar_h, S(20, scale));

    const int gap = S(6, scale);
    // Placed under the candidate window, which is where Sogou puts it when the
    // caret sits near the top of the screen.
    out.status_bar = Rect{0, out.window.cy + gap, bar_w,
                          out.window.cy + gap + bar_h};
    // Extend the composite window so the status bar is inside it. The area
    // between the two surfaces stays fully transparent.
    out.window.cy = out.status_bar.bottom;

    // `variant` selects which of the button's image names applies to the current
    // input state (the cn/en button has three: chinese / english / "other").
    // `button_index` is the button's identity, used to look up hover and press
    // state. Conflating the two made hover and press never match, because a
    // variant of 0 or 1 was compared against an index of 4.
    auto place = [&](const StatusButton& b, int variant, int button_index,
                     SsfLayoutResult::StatusIconBox& box) {
      if (!b.display || b.Empty()) return;
      const std::string state = StateFor(input, button_index);
      box = MakeIconBox(b, variant, state);
      if (!box.visible) return;
      const int left = out.status_bar.left + S(b.pos.x, scale);
      const int top = out.status_bar.top + S(b.pos.y, scale);
      box.box = Rect{left, top, left + slot_w, top + slot_h};
    };

    // Button identity order is fixed and must match
    // SsfLayoutEngine::HitTestStatusBar: 0 cn_en, 1 biaodian, 2 quan_ban,
    // 3 fan_jian, 4 menu.
    place(sb.cn_en, input.use_third_cn_state ? 2 : (input.chinese_mode ? 0 : 1),
          0, out.icon_cn_en);
    place(sb.biaodian, input.chinese_punctuation ? 0 : 1, 1, out.icon_biaodian);
    place(sb.quan_ban, input.full_shape ? 0 : 1, 2, out.icon_quan_ban);
    place(sb.fan_jian, input.traditional ? 1 : 0, 3, out.icon_fan_jian);
    place(sb.menu, 0, 4, out.icon_menu);
  }

  return out;
}

int SsfLayoutEngine::HitTest(const SsfLayoutResult& layout, int x, int y) {
  for (size_t i = 0; i < layout.candidates.size(); ++i) {
    const Rect& r = layout.candidates[i].box;
    if (x >= r.left && x < r.right && y >= r.top && y < r.bottom)
      return static_cast<int>(i);
  }
  return -1;
}

int SsfLayoutEngine::HitTestStatusBar(const SsfLayoutResult& layout, int x, int y) {
  const SsfLayoutResult::StatusIconBox* boxes[5] = {
      &layout.icon_cn_en, &layout.icon_biaodian, &layout.icon_quan_ban,
      &layout.icon_fan_jian, &layout.icon_menu};
  for (int i = 0; i < 5; ++i) {
    const SsfLayoutResult::StatusIconBox& b = *boxes[i];
    if (!b.visible || b.box.Empty()) continue;
    if (x >= b.box.left && x < b.box.right && y >= b.box.top &&
        y < b.box.bottom)
      return i;
  }
  return -1;
}

}  // namespace ssf
}  // namespace weasel
