// Sogou SSF skin renderer.
//
// Composes a `Layout` + `Skin` + decoded images into a single straight-alpha
// RGBA surface, and hands it to Weasel's layered window. The compositor is
// deliberately separate from WeaselPanel so it can also be driven offscreen by
// the `ssf_preview` tool, which is what makes pixel-level tuning possible
// without deploying an IME.

#pragma once

#include <string>
#include <vector>

#include "SsfImage.h"
#include "SsfImageLoader.h"
#include "SsfLayout.h"
#include "SsfSkin.h"

namespace weasel {
namespace ssf {

// Everything the renderer needs about the text to draw.
struct RenderText {
  std::wstring pinyin;
  std::wstring aux;
  std::vector<std::wstring> labels;
  std::vector<std::wstring> texts;
  std::vector<std::wstring> comments;
  int highlighted = 0;
};

enum class FontStyle {
  Regular = 0,
  Bold = 1,
};

// One text run in window coordinates. Kept as data so that a caller which
// cannot use DirectWrite (the preview tool) can still draw with the plain GDI
// path and produce the same geometry.
struct TextRun {
  Rect box;
  std::wstring text;
  Color color;
  ITextMeasurer::FontKind font = ITextMeasurer::CANDIDATE;
  FontStyle style = FontStyle::Regular;
  int baseline = 0;
};

struct RenderResult {
  SsfImagePtr surface;
  std::vector<TextRun> runs;
  // Rectangles that received text. GDI zeroes the alpha byte of every pixel a
  // glyph touches, so this must be repaired before the surface goes to a layered
  // window. Kept as data rather than applied here, because the caller knows
  // whether it drew with DirectWrite (alpha-preserving) or GDI (not).
  std::vector<Rect> text_rects;
  // Page indicator text, if any.
  std::wstring page_text;
  Rect page_rect;
};

struct RenderOptions {
  // Whole-pipeline scale, matching LayoutOptions::scale.
  double scale = 1.0;
  // Draw the preedit/candidate separator line when the skin declares one.
  bool draw_separator = true;
  // Draw the status bar background and icons.
  bool draw_status_bar = true;
};

class SsfRenderer {
 public:
  SsfRenderer() = default;

  // Compose the background layers only (no text). Useful for the preview tool's
  // geometry-diff mode and for measuring how much surface a layout needs.
  SsfImagePtr ComposeBackground(const Skin& skin,
                                const LayoutOptions& layout_options,
                                const LayoutInput& input,
                                const SsfLayoutResult& layout);

  // Compose background plus the list of text runs. Text is NOT rasterised here.
  RenderResult Compose(const Skin& skin,
                       const LayoutOptions& layout_options,
                       const LayoutInput& input,
                       const RenderText& text,
                       const SsfLayoutResult& layout,
                       const RenderOptions& options);

  // Decode the image set needed by the active layout into the renderer's real
  // cache.  This is called while the candidate window is constructed, so the
  // first visible composition does not perform synchronous disk I/O or PNG
  // decoding on the input path.
  void WarmUp(const Skin& skin,
              bool horizontal,
              bool prefer_split_background,
              bool include_status_bar);

  // Resolve every image the scheme references and describe what was found.
  // Diagnostics only; the renderer loads lazily through the store.
  struct ImageReport {
    std::string name;
    int width = 0;
    int height = 0;
    bool loaded = false;
  };
  std::vector<ImageReport> Inspect(const Skin& skin, bool horizontal) const;

  // The image store, exposed so the caller can point it at a skin directory and
  // clear it on redeploy.
  SsfImageStore& images() { return images_; }
  const SsfImageStore& images() const { return images_; }

 private:
  SsfImageStore images_;

  // Draw the pinyin strip background. Returns the strip rectangle actually
  // covered.
  void DrawPinyinStrip(SsfImage& surface,
                       const Skin& skin,
                       const Scheme& scheme,
                       const SsfLayoutResult& layout,
                       double scale);

  // Draw the candidate strip background.
  void DrawCandidateStrip(SsfImage& surface,
                          const Skin& skin,
                          const Scheme& scheme,
                          const SsfLayoutResult& layout,
                          int index,
                          bool highlighted,
                          double scale);

  // Draw the status bar background and icons.
  void DrawStatusBar(SsfImage& surface,
                     const Skin& skin,
                     const SsfLayoutResult& layout,
                     const LayoutInput& input,
                     double scale);
};

// ---------------------------------------------------------------------------
// Font helpers shared with the DirectWrite-backed measurer in WeaselPanel and
// with the preview tool's GDI measurer.
// ---------------------------------------------------------------------------

// Pick the font face for a run: the skin's Chinese face for text that contains
// CJK, the English face otherwise. Falls back gracefully when either is empty.
std::wstring ChooseFontFace(const Skin& skin,
                            const std::wstring& text,
                            ITextMeasurer::FontKind kind);

// True when the string contains at least one character that needs the Chinese
// face (CJK ideographs, CJK punctuation, fullwidth forms, or any non-Latin
// script outside Basic Latin/Latin-1).
bool NeedsChineseFont(const std::wstring& text);

}  // namespace ssf
}  // namespace weasel
