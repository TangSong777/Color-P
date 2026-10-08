// Adapter: drives the Sogou SSF layout engine from Weasel's Layout interface.
//
// Weasel organises a candidate window by asking a `weasel::Layout` for a set of
// rectangles (preedit, per-candidate, highlight, page indicator, status icon)
// and then hit-tests against those same rectangles, which is what keeps the
// clickable area identical to the painted area. Rather than bypassing that
// contract, this adapter implements it on top of `ssf::SsfLayoutEngine`, so the
// SSF skin and the stock renderer share one geometry model.
//
// Fonts and colours come from the skin (font_ch / font_en / font_size and the
// four colour keys), not from Weasel's YAML style. Text is still measured and
// drawn through DirectWrite so metrics match the glyphs actually rendered.

#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

// weasel::Layout -- the interface this class implements -- lives in the
// WeaselUI project rather than in the shared include/ directory. It is written
// to be consumed from a translation unit that already has the ATL/WTL/GDI+
// stack in scope (normally supplied by stdafx.h), so those headers are pulled
// in here explicitly. This module deliberately does not use the precompiled
// header, so it must be self-sufficient.
#include <atlbase.h>
#include <atlwin.h>
#include <wtl/atlapp.h>
#include <wtl/atlgdi.h>
#include <wtl/atlmisc.h>
#include <gdiplus.h>

#include <WeaselIPCData.h>
#include <WeaselUI.h>

#include "../Layout.h"

#include "SsfImageLoader.h"
#include "SsfLayout.h"
#include "SsfRenderer.h"
#include "SsfSkin.h"

namespace weasel {
namespace ssf {

// Drives one SSF scheme. Lives as long as the panel's current layout.
class SsfLayoutAdapter : public weasel::Layout {
 public:
  // Measure a string with the font the skin selected for `kind`, returning the
  // advance box in device pixels. Supplied by the panel so that measurement and
  // drawing cannot drift apart.
  using MeasureFn = std::function<CSize(const std::wstring& text,
                                        ITextMeasurer::FontKind kind)>;
  // Baseline offset from the top of the line box, for the same font.
  using AscentFn = std::function<int(ITextMeasurer::FontKind kind)>;

  SsfLayoutAdapter(const UIStyle& style,
                   const Context& context,
                   const Status& status,
                   PDWR pDWR,
                   const Skin& skin,
                   MeasureFn measure,
                   AscentFn ascent);

  // --- weasel::Layout -----------------------------------------------------

  void DoLayout(CDCHandle dc, PDWR pDWR = NULL) override;
  CSize GetContentSize() const override { return _contentSize; }
  CRect GetPreeditRect() const override;
  CRect GetAuxiliaryRect() const override;
  CRect GetHighlightRect() const override;
  CRect GetCandidateLabelRect(int id) const override;
  CRect GetCandidateTextRect(int id) const override;
  CRect GetCandidateCommentRect(int id) const override;
  CRect GetCandidateRect(int id) const override;
  CRect GetStatusIconRect() const override;
  IsToRoundStruct GetRoundInfo(int id) override;
  IsToRoundStruct GetTextRoundInfo() override;
  CRect GetContentRect() override;
  CRect GetPrepageRect() override;
  CRect GetNextpageRect() override;
  TextRange GetPreeditRange() override;
  CSize GetBeforeSize() override;
  CSize GetHilitedSize() override;
  CSize GetAfterSize() override;
  std::wstring GetLabelText(const std::vector<Text>& labels,
                            int id,
                            const wchar_t* format) const override;
  bool IsInlinePreedit() const override;
  bool ShouldDisplayStatusIcon() const override;
  void GetTextSizeDW(const std::wstring& text,
                     size_t nCount,
                     ComPtr<IDWriteTextFormat1>& pTextFormat,
                     PDWR pDWR,
                     LPSIZE lpSize) const override;

  // --- SSF-specific accessors --------------------------------------------

  const SsfLayoutResult& layout() const { return layout_; }
  const Skin& skin() const { return *skin_; }
  const Scheme& scheme() const { return *scheme_; }
  Orientation orientation() const { return options_.orientation; }
  double scale() const { return options_.scale; }

  // Which status-bar button is under the point, or -1. Mirrors the ordering used
  // by `ssf::SsfLayoutEngine::HitTestStatusBar`.
  int HitTestStatusButton(int x, int y) const {
    return SsfLayoutEngine::HitTestStatusBar(layout_, x, y);
  }

  // Recompute after the scheme, scale or orientation changed.
  void SetScale(double scale) { options_.scale = scale; }
  void SetOrientation(Orientation o) { options_.orientation = o; }

  // Native sizes of the artwork, needed so the window is never smaller than the
  // skin's own bitmaps. Filled by the caller from the decoded images.
  void SetNativeSizes(Size main_bg,
                      Size pinyin_bg,
                      Size candidate_bg,
                      Size icon_slot) {
    options_.native_main_bg = main_bg;
    options_.native_pinyin_bg = pinyin_bg;
    options_.native_candidate_bg = candidate_bg;
    options_.status_icon_slot = icon_slot;
  }

  // The assembled input, exposed so the renderer draws exactly what was laid
  // out rather than rebuilding it (and possibly diverging).
  const LayoutInput& input() const { return input_; }

  // Text runs for the current layout. Recomputed by DoLayout.
  const RenderText& render_text() const { return render_text_; }

  // Status-bar interaction state, set by the panel before a layout pass.
  void SetStatusBarEnabled(bool enabled) { status_bar_enabled_ = enabled; }
  // The cursor-near mode tip uses the source PNG's physical dimensions. This
  // is deliberately independent of the candidate-window DPI scale.
  void SetModeTipSize(Size size) { mode_tip_size_ = size; }
  void SetStatusBarHover(int button) { hovered_button_ = button; }
  void SetStatusBarPress(int button) { pressed_button_ = button; }

 private:
  void BuildInput();
  void BuildRenderText();

  // Measurement shim handed to the engine.
  class Measurer : public ITextMeasurer {
   public:
    Measurer(const SsfLayoutAdapter& owner, const MeasureFn& measure,
             const AscentFn& ascent)
        : owner_(owner), measure_(measure), ascent_(ascent) {}
    Size Measure(const std::wstring& text, FontKind kind) const override;
    int LineHeight(FontKind kind) const override;
    int Ascent(FontKind kind) const override;

   private:
    const SsfLayoutAdapter& owner_;
    const MeasureFn& measure_;
    const AscentFn& ascent_;
  };

  const Skin* skin_ = nullptr;
  const Scheme* scheme_ = nullptr;
  MeasureFn measure_;
  AscentFn ascent_;
  LayoutOptions options_;
  LayoutInput input_;
  RenderText render_text_;
  ::weasel::ssf::SsfLayoutResult layout_;
  Measurer measurer_;
  CSize _contentSize{0, 0};
  TextRange _range{0, 0, 0};
  CSize _beforesz{0, 0}, _hilitedsz{0, 0}, _aftersz{0, 0};
  CRect _empty_;
  IsToRoundStruct _round_;
  std::vector<std::wstring> label_cache_;
  bool status_bar_enabled_ = false;
  Size mode_tip_size_{25, 25};
  int hovered_button_ = -1;
  int pressed_button_ = -1;
};

}  // namespace ssf
}  // namespace weasel
