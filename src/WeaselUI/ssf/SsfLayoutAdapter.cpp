// Adapter: drives the Sogou SSF layout engine from Weasel's Layout interface.

#include "SsfLayoutAdapter.h"

// MAX_CANDIDATES_COUNT lives in StandardLayout.h, together with the rest of the
// Weasel layout constants.
#include "../StandardLayout.h"

#include <algorithm>

namespace weasel {
namespace ssf {
namespace {

inline CRect ToCRect(const Rect& r) {
  return CRect(r.left, r.top, r.right, r.bottom);
}

inline CRect ClampTo(const CRect& rc, const CSize& size) {
  CRect out(rc);
  if (out.left < 0) out.left = 0;
  if (out.top < 0) out.top = 0;
  if (out.right > size.cx) out.right = size.cx;
  if (out.bottom > size.cy) out.bottom = size.cy;
  if (out.right < out.left) out.right = out.left;
  if (out.bottom < out.top) out.bottom = out.top;
  return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Measurer
// ---------------------------------------------------------------------------

Size SsfLayoutAdapter::Measurer::Measure(const std::wstring& text,
                                        FontKind kind) const {
  const CSize sz = measure_(text, kind);
  Size out;
  out.cx = sz.cx;
  out.cy = sz.cy;
  return out;
}

int SsfLayoutAdapter::Measurer::LineHeight(FontKind kind) const {
  // Line height is the ascent plus the descent. The panel's ascent callback
  // reports only the ascent, so the line box is derived from a measurement of a
  // representative string instead -- the one value guaranteed to come from the
  // same layout that will draw the glyphs.
  //
  // The probe is deliberately Latin ("Ag"): a CJK glyph makes DirectWrite
  // report the font's full CJK line box, which for Sogou's 14pt skins is
  // several pixels taller than the row the skin was authored around, and that
  // extra height pushed the text visibly below centre in the candidate strip.
  // Real candidate text is mixed, and DirectWrite already sizes a line to its
  // tallest run, so the Latin box is the better baseline here.
  const CSize sz = measure_(L"Ag", kind);
  (void)owner_;
  return sz.cy > 0 ? sz.cy : 1;
}

int SsfLayoutAdapter::Measurer::Ascent(FontKind kind) const {
  return ascent_(kind);
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SsfLayoutAdapter::SsfLayoutAdapter(const UIStyle& style,
                                   const Context& context,
                                   const Status& status,
                                   PDWR pDWR,
                                   const Skin& skin,
                                   MeasureFn measure,
                                   AscentFn ascent)
    : weasel::Layout(style, context, status, pDWR),
      skin_(&skin),
      measure_(std::move(measure)),
      ascent_(std::move(ascent)),
      measurer_(*this, measure_, ascent_) {
  // SSF geometry is authoritative; Weasel's own layout knobs must not perturb
  // it. The only ones carried over are the two that decide whether this is a
  // horizontal or vertical window, and they are re-read in DoLayout.
  options_.prefer_split_background = skin.h1.pic.empty();
  options_.scale = pDWR ? static_cast<double>(pDWR->dpiScaleLayout) : 1.0;
  if (options_.scale <= 0.0) options_.scale = 1.0;
  options_.orientation = (style.layout_type == UIStyle::LAYOUT_VERTICAL ||
                          style.layout_type == UIStyle::LAYOUT_VERTICAL_FULLSCREEN)
                             ? Orientation::Vertical
                             : Orientation::Horizontal;

  _round_.IsTopLeftNeedToRound = false;
  _round_.IsTopRightNeedToRound = false;
  _round_.IsBottomLeftNeedToRound = false;
  _round_.IsBottomRightNeedToRound = false;
  _round_.Hemispherical = false;
}

// ---------------------------------------------------------------------------
// Input assembly
// ---------------------------------------------------------------------------

void SsfLayoutAdapter::BuildInput() {
  input_ = LayoutInput();

  input_.preedit = _context.preedit.str;
  input_.aux = _context.aux.str;

  const size_t n = std::min<size_t>(_context.cinfo.candies.size(),
                                    MAX_CANDIDATES_COUNT);
  input_.texts.reserve(n);
  input_.labels.reserve(n);
  input_.comments.reserve(n);
  label_cache_.clear();
  label_cache_.reserve(n);

  const wchar_t* fmt = _style.label_text_format.empty()
                           ? L"%s"
                           : _style.label_text_format.c_str();
  for (size_t i = 0; i < n; ++i) {
    input_.texts.push_back(_context.cinfo.candies[i].str);
    std::wstring label;
    if (i < _context.cinfo.labels.size()) {
      wchar_t buffer[128];
      swprintf_s<128>(buffer, fmt, _context.cinfo.labels[i].str.c_str());
      label = buffer;
    }
    label_cache_.push_back(label);
    input_.labels.push_back(label);
    input_.comments.push_back(i < _context.cinfo.comments.size()
                                  ? _context.cinfo.comments[i].str
                                  : std::wstring());
  }

  input_.highlighted = _context.cinfo.highlighted;

  // Page indicator: show it when there is more than one page, matching what
  // Sogou does (it draws the pager inside the candidate strip).
  input_.page_number = _context.cinfo.currentPage;
  input_.page_size = _context.cinfo.totalPages;
  input_.is_last_page = _context.cinfo.is_last_page;
  input_.show_page_indicator =
      _context.cinfo.totalPages > 1 && !_context.cinfo.candies.empty();

  // Status bar: only meaningful when the user has enabled it and the current
  // state actually asks for it (the panel decides the latter).
  input_.show_status_bar = status_bar_enabled_;
  input_.chinese_mode = !_status.ascii_mode;
  input_.chinese_punctuation = !_status.ascii_mode;
  input_.full_shape = _status.full_shape;
  input_.traditional = false;  // no Weasel equivalent; see docs/ssf-skin.md
  input_.hovered_button = hovered_button_;
  input_.pressed_button = pressed_button_;
}

void SsfLayoutAdapter::BuildRenderText() {
  render_text_ = RenderText();
  render_text_.pinyin = input_.preedit;
  render_text_.aux = input_.aux;
  render_text_.labels = input_.labels;
  render_text_.texts = input_.texts;
  render_text_.comments = input_.comments;
  render_text_.highlighted = input_.highlighted;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void SsfLayoutAdapter::DoLayout(CDCHandle dc, PDWR pDWR) {
  (void)dc;
  if (!skin_) return;
  if (pDWR) {
    const double s = static_cast<double>(pDWR->dpiScaleLayout);
    if (s > 0.0) options_.scale = s;
  }

  scheme_ = &(options_.orientation == Orientation::Vertical
                  ? skin_->Vertical(options_.prefer_split_background)
                  : skin_->Horizontal(options_.prefer_split_background));

  BuildInput();
  BuildRenderText();

  layout_ = SsfLayoutEngine().Compute(*skin_, options_, input_, measurer_);

  _contentSize.cx = layout_.window.cx;
  _contentSize.cy = layout_.window.cy;
  // In an idle CN/EN switch there is no candidate layout to size the window.
  // Use the original PNG extent rather than a hard-coded status-icon size.
  if (ShouldDisplayStatusIcon()) {
    _contentSize = CSize((std::max)(1, mode_tip_size_.cx),
                         (std::max)(1, mode_tip_size_.cy));
  }

  // Preedit split, for the highlighted-preedit colouring Weasel supports. Only
  // applied when the pieces genuinely add up to the whole string's advance
  // width, so the text never shifts.
  _range = TextRange(0, 0, -1);
  _beforesz = CSize(0, 0);
  _hilitedsz = CSize(0, 0);
  _aftersz = CSize(0, 0);
  for (const TextAttribute& attr : _context.preedit.attributes) {
    // Rime always supplies the cursor, while a selected conversion range is
    // optional. Preserve both pieces of information in one TextRange.
    if (attr.range.cursor >= 0)
      _range.cursor = attr.range.cursor;
    if (attr.type == HIGHLIGHTED) {
      _range.start = attr.range.start;
      _range.end = attr.range.end;
    }
  }
  if (_range.start < _range.end && !input_.preedit.empty()) {
    const size_t n = input_.preedit.size();
    const size_t s = std::min<size_t>(static_cast<size_t>(_range.start), n);
    const size_t e = std::min<size_t>(static_cast<size_t>(_range.end), n);
    const std::wstring before = input_.preedit.substr(0, s);
    const std::wstring mid = input_.preedit.substr(s, e - s);
    const std::wstring after = input_.preedit.substr(e);
    const CSize whole = measure_(input_.preedit, ITextMeasurer::PINYIN);
    const CSize b = measure_(before, ITextMeasurer::PINYIN);
    const CSize m = measure_(mid, ITextMeasurer::PINYIN);
    const CSize a = measure_(after, ITextMeasurer::PINYIN);
    if (abs((b.cx + m.cx + a.cx) - whole.cx) <= 1) {
      _beforesz = b;
      _hilitedsz = m;
      _aftersz = a;
    } else {
      _range = TextRange(0, 0, -1);
    }
  }
}

// ---------------------------------------------------------------------------
// Rectangles
// ---------------------------------------------------------------------------

CRect SsfLayoutAdapter::GetPreeditRect() const {
  return ClampTo(ToCRect(layout_.pinyin_text), _contentSize);
}

CRect SsfLayoutAdapter::GetAuxiliaryRect() const {
  return ClampTo(ToCRect(layout_.aux_text), _contentSize);
}

CRect SsfLayoutAdapter::GetHighlightRect() const {
  return ClampTo(ToCRect(layout_.highlight), _contentSize);
}

CRect SsfLayoutAdapter::GetCandidateLabelRect(int id) const {
  if (id < 0 || static_cast<size_t>(id) >= layout_.candidates.size())
    return CRect(0, 0, 0, 0);
  return ClampTo(ToCRect(layout_.candidates[id].label), _contentSize);
}

CRect SsfLayoutAdapter::GetCandidateTextRect(int id) const {
  if (id < 0 || static_cast<size_t>(id) >= layout_.candidates.size())
    return CRect(0, 0, 0, 0);
  return ClampTo(ToCRect(layout_.candidates[id].text), _contentSize);
}

CRect SsfLayoutAdapter::GetCandidateCommentRect(int id) const {
  if (id < 0 || static_cast<size_t>(id) >= layout_.candidates.size())
    return CRect(0, 0, 0, 0);
  return ClampTo(ToCRect(layout_.candidates[id].comment), _contentSize);
}

CRect SsfLayoutAdapter::GetCandidateRect(int id) const {
  if (id < 0 || static_cast<size_t>(id) >= layout_.candidates.size())
    return CRect(0, 0, 0, 0);
  // The hit target is exactly the painted cell, which is the whole point of
  // routing mouse input through this class.
  return ClampTo(ToCRect(layout_.candidates[id].box), _contentSize);
}

CRect SsfLayoutAdapter::GetStatusIconRect() const {
  if (!ShouldDisplayStatusIcon()) return CRect(0, 0, 0, 0);
  return CRect(0, 0, _contentSize.cx, _contentSize.cy);
}

IsToRoundStruct SsfLayoutAdapter::GetRoundInfo(int id) {
  (void)id;
  return _round_;
}

IsToRoundStruct SsfLayoutAdapter::GetTextRoundInfo() {
  return _round_;
}

CRect SsfLayoutAdapter::GetContentRect() {
  return CRect(0, 0, _contentSize.cx, _contentSize.cy);
}

CRect SsfLayoutAdapter::GetPrepageRect() {
  return ClampTo(ToCRect(layout_.prev_page), _contentSize);
}

CRect SsfLayoutAdapter::GetNextpageRect() {
  return ClampTo(ToCRect(layout_.next_page), _contentSize);
}

TextRange SsfLayoutAdapter::GetPreeditRange() {
  return _range;
}

CSize SsfLayoutAdapter::GetBeforeSize() {
  return _beforesz;
}

CSize SsfLayoutAdapter::GetHilitedSize() {
  return _hilitedsz;
}

CSize SsfLayoutAdapter::GetAfterSize() {
  return _aftersz;
}

std::wstring SsfLayoutAdapter::GetLabelText(const std::vector<Text>& labels,
                                            int id,
                                            const wchar_t* format) const {
  if (id < 0 || static_cast<size_t>(id) >= labels.size()) return std::wstring();
  wchar_t buffer[128];
  swprintf_s<128>(buffer, format ? format : L"%s", labels[id].str.c_str());
  return std::wstring(buffer);
}

bool SsfLayoutAdapter::IsInlinePreedit() const {
  // An SSF skin always draws the pinyin inside its own window; there is no
  // in-host-composition variant.
  return false;
}

bool SsfLayoutAdapter::ShouldDisplayStatusIcon() const {
  // Keep Color-P candidate-only by default, but retain its Sogou CN/EN art for
  // an actual ascii_mode notification. Ordinary empty refreshes (including
  // literal numpad digits) intentionally never create a standalone tip.
  return _context.empty() && _status.show_mode_tip;
}

void SsfLayoutAdapter::GetTextSizeDW(const std::wstring& text,
                                     size_t nCount,
                                     ComPtr<IDWriteTextFormat1>& pTextFormat,
                                     PDWR pDWR,
                                     LPSIZE lpSize) const {
  // Delegates to the caller-supplied measurer so that SSF geometry and Weasel's
  // own drawing agree on every advance width.
  (void)pTextFormat;
  (void)pDWR;
  if (!lpSize) return;
  const CSize sz = measure_(text.substr(0, nCount), ITextMeasurer::CANDIDATE);
  lpSize->cx = sz.cx;
  lpSize->cy = sz.cy;
}

}  // namespace ssf
}  // namespace weasel
