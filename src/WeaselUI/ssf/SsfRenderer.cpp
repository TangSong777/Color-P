// Sogou SSF skin renderer.
//
// The compositor works purely on `SsfImage` (straight-alpha RGBA) and emits text
// runs as data. That keeps it free of DirectWrite/GDI and lets the offscreen
// preview tool reuse it verbatim.

#include "SsfRenderer.h"

#include <algorithm>

namespace weasel {
namespace ssf {
namespace {

inline int S(int value, double scale) {
  const double v = value * scale;
  return static_cast<int>(v >= 0 ? v + 0.5 : v - 0.5);
}

}  // namespace

// ---------------------------------------------------------------------------
// Font selection
// ---------------------------------------------------------------------------

bool NeedsChineseFont(const std::wstring& text) {
  for (wchar_t wc : text) {
    const uint32_t cp = static_cast<uint32_t>(wc);
    // CJK Unified Ideographs and the common CJK blocks
    if (cp >= 0x2E80 && cp <= 0x9FFF) return true;    // radicals .. CJK UI
    if (cp >= 0xF900 && cp <= 0xFAFF) return true;    // compatibility ideographs
    if (cp >= 0xFF00 && cp <= 0xFFEF) return true;    // fullwidth / halfwidth
    if (cp >= 0x3000 && cp <= 0x303F) return true;    // CJK punctuation
    if (cp >= 0x20000 && cp <= 0x2FA1F) return true;  // extensions B..F
  }
  return false;
}

std::wstring ChooseFontFace(const Skin& skin,
                            const std::wstring& text,
                            ITextMeasurer::FontKind kind) {
  const std::wstring ch = Utf8ToWide(skin.font_ch);
  const std::wstring en = Utf8ToWide(skin.font_en);

  // Numeric selector labels ("1.", "2." …) are ASCII, but Color-P/Sogou
  // styles them with the CJK face. Keep this rule here as well as in the
  // DirectWrite format setup so the fallback and preview renderers agree.
  if (kind == ITextMeasurer::LABEL && !ch.empty()) return ch;

  // Comments and auxiliary strings are Latin-ish by nature, but they can carry
  // Chinese, so the content still decides.
  if (NeedsChineseFont(text)) {
    if (!ch.empty()) return ch;
    if (!en.empty()) return en;
  } else {
    if (!en.empty()) return en;
    if (!ch.empty()) return ch;
  }
  return std::wstring();
}

// ---------------------------------------------------------------------------
// Background composition
// ---------------------------------------------------------------------------

SsfImagePtr SsfRenderer::ComposeBackground(const Skin& skin,
                                           const LayoutOptions& layout_options,
                                           const LayoutInput& input,
                                           const SsfLayoutResult& layout) {
  const double scale = layout_options.scale > 0.0 ? layout_options.scale : 1.0;

  auto surface = std::make_shared<SsfImage>();
  surface->width = (std::max)(1, layout.window.cx);
  surface->height = (std::max)(1, layout.window.cy);
  // Start fully transparent. Never pre-fill with an opaque rectangle: the whole
  // point of the skin is its alpha, and a white rectangle behind it would show
  // through the rounded corners.
  surface->pixels.assign(
      static_cast<size_t>(surface->width) * surface->height * 4, 0);

  const bool horizontal = layout_options.orientation == Orientation::Horizontal;
  const Scheme& scheme =
      horizontal ? skin.Horizontal(layout_options.prefer_split_background)
                 : skin.Vertical(layout_options.prefer_split_background);

  const bool split = layout_options.prefer_split_background &&
                     scheme.IsSplitBackground();

  if (split) {
    DrawPinyinStrip(*surface, skin, scheme, layout, scale);
    DrawCandidateStrip(*surface, skin, scheme, layout, -1, false, scale);
  } else {
    // Single-background form: one bitmap covers the whole window.
    SsfImagePtr bg = images_.Get(scheme.pic);
    if (bg) {
      surface->BlitNineSlice(*bg, scheme.ns_pinyin, 0, 0, layout.window.cx,
                             layout.window.cy);
    } else {
      // No artwork at all: a flat, legible surface is better than nothing, and
      // it is what makes a half-broken skin diagnosable rather than invisible.
      Color flat;
      flat.r = 248;
      flat.g = 248;
      flat.b = 246;
      flat.a = 255;
      surface->FillRect(0, 0, layout.window.cx, layout.window.cy, flat);
    }
  }

  // Highlight: draw the highlighted candidate's backing bitmap if the scheme
  // provides one, otherwise leave the skin's own background showing.
  if (layout_options.orientation == Orientation::Horizontal ||
      layout_options.orientation == Orientation::Vertical) {
    if (!layout.highlight.Empty() && !scheme.zhongwen_pic_hover.empty()) {
      SsfImagePtr hl = images_.Get(scheme.zhongwen_pic_hover.empty()
                                       ? scheme.zhongwen_pic
                                       : scheme.zhongwen_pic_hover);
      if (hl) {
        surface->BlitNineSlice(*hl, scheme.ns_zhongwen, layout.highlight.left,
                               layout.highlight.top, layout.highlight.Width(),
                               layout.highlight.Height());
      }
    }
  }

  // Separator line between preedit and candidates.
  if (scheme.has_separator && layout.has_pinyin_area && !split) {
    const int y = layout.candidate_bg.Empty() ? layout.pinyin_bg.bottom
                                              : layout.candidate_bg.top;
    const int x0 = S(scheme.zhongwen_insets.left, scale);
    const int x1 = layout.window.cx - S(scheme.zhongwen_insets.right, scale);
    surface->FillRect(x0, y, (std::max)(0, x1 - x0), 1, scheme.separator);
  }

  (void)input;
  return surface;
}

void SsfRenderer::DrawPinyinStrip(SsfImage& surface,
                                  const Skin& skin,
                                  const Scheme& scheme,
                                  const SsfLayoutResult& layout,
                                  double scale) {
  (void)skin;
  if (layout.pinyin_bg.Empty()) return;

  SsfImagePtr img = images_.Get(scheme.pinyin_pic);
  if (img) {
    surface.BlitNineSlice(*img, scheme.ns_pinyin, layout.pinyin_bg.left,
                          layout.pinyin_bg.top, layout.pinyin_bg.Width(),
                          layout.pinyin_bg.Height());
    return;
  }
  // Fall back to the single background bitmap if the split one is missing, so a
  // skin with an incomplete asset set still produces a recognisable window.
  SsfImagePtr fallback = images_.Get(scheme.pic);
  if (fallback) {
    surface.BlitNineSlice(*fallback, scheme.ns_pinyin, layout.pinyin_bg.left,
                          layout.pinyin_bg.top, layout.pinyin_bg.Width(),
                          layout.pinyin_bg.Height());
  }
  (void)scale;
}

void SsfRenderer::DrawCandidateStrip(SsfImage& surface,
                                     const Skin& skin,
                                     const Scheme& scheme,
                                     const SsfLayoutResult& layout,
                                     int index,
                                     bool highlighted,
                                     double scale) {
  (void)skin;
  (void)index;
  (void)highlighted;
  if (layout.candidate_bg.Empty()) return;

  SsfImagePtr img = images_.Get(scheme.zhongwen_pic);
  if (img) {
    surface.BlitNineSlice(*img, scheme.ns_zhongwen, layout.candidate_bg.left,
                          layout.candidate_bg.top, layout.candidate_bg.Width(),
                          layout.candidate_bg.Height());
    return;
  }
  SsfImagePtr fallback = images_.Get(scheme.pic);
  if (fallback) {
    surface.BlitNineSlice(*fallback, scheme.ns_zhongwen,
                          layout.candidate_bg.left, layout.candidate_bg.top,
                          layout.candidate_bg.Width(),
                          layout.candidate_bg.Height());
  }
  (void)scale;
}

void SsfRenderer::DrawStatusBar(SsfImage& surface,
                                const Skin& skin,
                                const SsfLayoutResult& layout,
                                const LayoutInput& input,
                                double scale) {
  (void)input;
  if (layout.status_bar.Empty()) return;

  SsfImagePtr bar = images_.Get(skin.status.pic);
  if (bar) {
    // The status bar background is a plain rectangle in every skin observed so
    // far (100x29 for Color-P), so it is drawn 1:1 and clipped rather than
    // 9-sliced: stretching it would move the button artwork relative to the
    // declared *_pos coordinates, and the whole point of *_pos is that the
    // button sits at a fixed place on that artwork.
    surface.Blend(*bar, layout.status_bar.left, layout.status_bar.top);
  }

  // Buttons. Each was already resolved to the file name matching the current
  // normal / hover / pressed state by the layout engine, so the renderer only
  // has to blit it. If a state-specific bitmap is missing the button simply
  // does not draw, which is the graceful outcome for an incomplete skin.
  const SsfLayoutResult::StatusIconBox* boxes[5] = {
      &layout.icon_cn_en, &layout.icon_biaodian, &layout.icon_quan_ban,
      &layout.icon_fan_jian, &layout.icon_menu};
  for (const SsfLayoutResult::StatusIconBox* b : boxes) {
    if (!b->visible || b->normal.empty() || b->box.Empty()) continue;
    SsfImagePtr icon = images_.Get(b->normal);
    if (!icon) continue;
    // Icons are pixel art with fixed size; draw 1:1 at the declared position so
    // the glyph inside the button lines up with the background art.
    surface.Blend(*icon, b->box.left, b->box.top);
  }
  (void)scale;
}

// ---------------------------------------------------------------------------
// Full composition
// ---------------------------------------------------------------------------

RenderResult SsfRenderer::Compose(const Skin& skin,
                                  const LayoutOptions& layout_options,
                                  const LayoutInput& input,
                                  const RenderText& text,
                                  const SsfLayoutResult& layout,
                                  const RenderOptions& options) {
  RenderResult result;
  result.surface = ComposeBackground(skin, layout_options, input, layout);
  if (!result.surface) return result;

  const double scale = options.scale > 0.0 ? options.scale : 1.0;
  const bool horizontal = layout_options.orientation == Orientation::Horizontal;

  if (options.draw_status_bar)
    DrawStatusBar(*result.surface, skin, layout, input, scale);

  // -------------------------------------------------------------------------
  // Pinyin
  // -------------------------------------------------------------------------
  if (!text.pinyin.empty() && !layout.pinyin_text.Empty()) {
    TextRun run;
    run.box = layout.pinyin_text;
    run.text = text.pinyin;
    run.color = skin.pinyin_color;
    run.font = ITextMeasurer::PINYIN;
    run.baseline = layout.pinyin_baseline;
    result.runs.push_back(run);
  }

  if (!text.aux.empty() && !layout.aux_text.Empty()) {
    TextRun run;
    run.box = layout.aux_text;
    run.text = text.aux;
    run.color = skin.comphint_color;
    run.font = ITextMeasurer::PINYIN;
    run.baseline = layout.pinyin_baseline;
    result.runs.push_back(run);
  }

  // -------------------------------------------------------------------------
  // Candidates
  // -------------------------------------------------------------------------
  for (size_t i = 0; i < layout.candidates.size(); ++i) {
    const CandidateBox& box = layout.candidates[i];
    const bool hl = (static_cast<int>(i) == text.highlighted);

    if (i < text.labels.size() && !text.labels[i].empty() &&
        !box.label.Empty()) {
      TextRun run;
      run.box = box.label;
      run.text = text.labels[i];
      // The first candidate is emphasised with its own colour, so a skin that
      // distinguishes it is reproduced faithfully.
      run.color = hl ? skin.zhongwen_first_color : skin.zhongwen_color;
      run.font = ITextMeasurer::LABEL;
      run.baseline = layout.candidate_baseline;
      result.runs.push_back(run);
    }

    if (i < text.texts.size() && !box.text.Empty()) {
      TextRun run;
      run.box = box.text;
      run.text = text.texts[i];
      run.color = hl ? skin.zhongwen_first_color : skin.zhongwen_color;
      run.font = ITextMeasurer::CANDIDATE;
      run.style = hl ? FontStyle::Bold : FontStyle::Regular;
      run.baseline = layout.candidate_baseline;
      result.runs.push_back(run);
    }

    if (i < text.comments.size() && !text.comments[i].empty() &&
        !box.comment.Empty()) {
      TextRun run;
      run.box = box.comment;
      run.text = text.comments[i];
      run.color = skin.comphint_color;
      run.font = ITextMeasurer::COMMENT;
      run.baseline = layout.candidate_baseline;
      result.runs.push_back(run);
    }
  }

  // -------------------------------------------------------------------------
  // Page indicator
  // -------------------------------------------------------------------------
  if (layout.has_page_indicator && input.page_size > 0) {
    result.page_text = L"< " + std::to_wstring(input.page_number + 1) + L" >";
    result.page_rect = layout.next_page;
    TextRun run;
    run.box = layout.next_page;
    run.text = result.page_text;
    run.color = skin.comphint_color;
    run.font = ITextMeasurer::COMMENT;
    run.baseline = layout.candidate_baseline;
    result.runs.push_back(run);
  }

  // -------------------------------------------------------------------------
  // Text rectangle bookkeeping
  //
  // A small margin is included because GDI's anti-aliasing and DirectWrite's
  // overhang can both paint a pixel or two outside the reported advance box.
  // The repair pass is idempotent and only ever raises alpha from 0, so
  // over-reporting is harmless while under-reporting produces holes.
  // -------------------------------------------------------------------------
  for (const TextRun& run : result.runs) {
    Rect r = run.box;
    r.left -= 2;
    r.top -= 2;
    r.right += 4;
    r.bottom += 4;
    result.text_rects.push_back(r);
  }

  (void)horizontal;
  return result;
}

void SsfRenderer::WarmUp(const Skin& skin,
                         bool horizontal,
                         bool prefer_split_background,
                         bool include_status_bar) {
  const Scheme& scheme = horizontal
                             ? skin.Horizontal(prefer_split_background)
                             : skin.Vertical(prefer_split_background);
  auto load = [this](const std::string& name) {
    if (!name.empty()) (void)images_.Get(name);
  };

  // ComposeBackground can use the primary artwork as a fallback for either
  // split strip, and the hover artwork only when the active candidate changes.
  load(scheme.pic);
  load(scheme.pinyin_pic);
  load(scheme.zhongwen_pic);
  load(scheme.zhongwen_pic_hover);

  if (!include_status_bar) return;
  load(skin.status.pic);
  for (const StatusButton* b : {&skin.status.cn_en, &skin.status.biaodian,
                                &skin.status.quan_ban, &skin.status.fan_jian,
                                &skin.status.menu}) {
    for (const std::string& name : b->names) load(name);
    for (const std::string& name : b->names_hover) load(name);
    for (const std::string& name : b->names_down) load(name);
  }
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

std::vector<SsfRenderer::ImageReport> SsfRenderer::Inspect(const Skin& skin,
                                                           bool horizontal) const {
  std::vector<ImageReport> report;
  // Inspect() is const but populating the cache is a benign memoisation; use a
  // local store so callers do not observe a mutation through a const method.
  SsfImageStore scratch;
  scratch.SetDirectory(images_.Directory());

  const Scheme& scheme = horizontal ? skin.Horizontal() : skin.Vertical();
  auto add = [&](const std::string& name) {
    if (name.empty()) return;
    ImageReport r;
    r.name = name;
    SsfImagePtr img = scratch.Get(name);
    if (img && img->Valid()) {
      r.loaded = true;
      r.width = img->Width();
      r.height = img->Height();
    }
    report.push_back(r);
  };

  add(scheme.pic);
  add(scheme.pinyin_pic);
  add(scheme.zhongwen_pic);
  add(skin.status.pic);
  for (const StatusButton* b : {&skin.status.cn_en, &skin.status.biaodian,
                                &skin.status.quan_ban, &skin.status.fan_jian,
                                &skin.status.menu}) {
    for (const std::string& n : b->names) add(n);
    for (const std::string& n : b->names_hover) add(n);
    for (const std::string& n : b->names_down) add(n);
  }
  return report;
}

}  // namespace ssf
}  // namespace weasel
