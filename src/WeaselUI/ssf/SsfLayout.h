// Layout engine for the Sogou SSF compatibility layer.
//
// Pure geometry: consumes a decoded Skin plus text measurements and produces
// every rectangle the renderer and the hit-testing code need. It has no
// dependency on Windows, DirectWrite or GDI+, so the whole of it is unit
// testable. Text measurement is injected through `ITextMeasurer`.

#pragma once

// See the note in SsfSkin.h: windows.h's min/max macros are live in this
// translation unit, so suppress them before any Windows header is reached.
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <string>
#include <vector>

#include "SsfImage.h"
#include "SsfSkin.h"

namespace weasel {
namespace ssf {

enum class Orientation {
  Horizontal,
  Vertical,
};

// How the candidate window's content is arranged. Mirrors Sogou's own notion:
//   - "single"  : [Scheme_H1] / [Scheme_V1] -- one background bitmap
//   - "split"   : [Scheme_H2] / [Scheme_V2] -- separate pinyin and candidate
//                 background bitmaps
enum class BackgroundMode {
  Single,
  Split,
};

// Text measurement, injected so the layout engine stays platform-free.
class ITextMeasurer {
 public:
  virtual ~ITextMeasurer() = default;

  // Advance width and rendered height of `text` when drawn with the font
  // selected by `kind`.
  enum FontKind { PINYIN = 0, CANDIDATE = 1, LABEL = 2, COMMENT = 3 };
  virtual Size Measure(const std::wstring& text, FontKind kind) const = 0;

  // Height of one line of text in the given font, including internal leading.
  virtual int LineHeight(FontKind kind) const = 0;

  // Distance from the top of that line box down to the baseline. Used to align
  // the pinyin strip and the candidate row on a common baseline, which is what
  // makes a skin look "right" rather than merely close.
  virtual int Ascent(FontKind kind) const = 0;
};

// ---------------------------------------------------------------------------
// Layout output
// ---------------------------------------------------------------------------

// One candidate, split into its parts so the renderer can colour them
// independently (label, candidate text, comment/hint).
struct CandidateBox {
  Rect box;      // union of the parts; also the mouse hit target
  Rect label;    // empty when the skin has no labels
  Rect text;
  Rect comment;  // empty when the candidate has no comment
  int index = 0; // candidate index within the page
};

struct SsfLayoutResult {
  Size window;  // whole candidate window, in device pixels

  // Background strips. In Single mode only `main_bg` is meaningful; in Split
  // mode both are.
  Rect main_bg;
  Rect pinyin_bg;
  Rect candidate_bg;

  bool has_pinyin_area = false;
  bool has_aux_area = false;

  Rect pinyin_text;   // preedit text run
  Rect aux_text;      // auxiliary / status text run
  Size pinyin_text_size;
  Size aux_text_size;

  std::vector<CandidateBox> candidates;
  Rect highlight;  // highlighted candidate's backing rectangle
  int highlighted = 0;

  // Page indicator (< n/m >), empty when it should not be drawn.
  Rect prev_page;
  Rect next_page;
  bool has_page_indicator = false;

  // Status bar. `status_bar.Height() == 0` when it is not shown.
  Rect status_bar;
  struct StatusIconBox {
    bool visible = false;
    Rect box;
    std::string normal;  // image file name
    std::string hover;
    std::string down;
  };
  StatusIconBox icon_cn_en;
  StatusIconBox icon_biaodian;
  StatusIconBox icon_quan_ban;
  StatusIconBox icon_fan_jian;
  StatusIconBox icon_menu;

  // Baseline used for pinyin and candidates, in window coordinates. Exposed so
  // the renderer does not have to recompute it.
  int pinyin_baseline = 0;
  int candidate_baseline = 0;
};

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

// Everything the layout needs to know about what is currently being typed.
struct LayoutInput {
  std::wstring preedit;             // e.g. "ni'hao"
  std::wstring aux;                 // auxiliary string, e.g. a tip
  std::vector<std::wstring> texts;  // candidate surfaces
  std::vector<std::wstring> labels; // candidate labels (may be empty)
  std::vector<std::wstring> comments;
  int highlighted = 0;

  bool show_page_indicator = false;
  int page_number = 0;   // 0-based
  int page_size = 0;     // total pages
  bool is_last_page = true;

  // Status bar state.
  bool show_status_bar = false;
  bool chinese_mode = true;
  bool use_third_cn_state = false;  // cn_en button's third icon
  bool chinese_punctuation = true;
  bool full_shape = false;
  bool traditional = false;
  // -1 = not hovered, 0 = hover only, 1 = pressed; index into the status bar's
  // button list for hover/press, -1 for none.
  int hovered_button = -1;
  int pressed_button = -1;
};

// ---------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------

struct LayoutOptions {
  Orientation orientation = Orientation::Horizontal;
  // When true, prefer the split-background scheme (H2/V2) if the skin has one.
  bool prefer_split_background = true;
  // Whole-pipeline scale factor. 1.0 at 100% DPI; the caller scales by
  // dpi/96 so that bitmaps, insets and fonts move together and the skin keeps
  // its proportions.
  double scale = 1.0;
  // Never shrink the window below the natural size of the skin's own artwork,
  // so a short candidate list does not squash the background.
  bool enforce_native_minimum = true;
  // Native (unscaled) pixel sizes of the background bitmaps the scheme refers
  // to. The layout engine has no access to decoded images, so the caller passes
  // them in. Zero means "unknown", which disables the corresponding constraint.
  Size native_main_bg{0, 0};
  Size native_pinyin_bg{0, 0};
  Size native_candidate_bg{0, 0};
  // Icon slot sizes for the status bar, so its surface is sized from the real
  // artwork rather than a guess.
  Size status_icon_slot{0, 0};
  // Extra pixels appended after the candidate row, for the next-page/expand
  // affordances Sogou draws inside the window. 0 disables.
  int trailing_controls_width = 0;
};

class SsfLayoutEngine {
 public:
  SsfLayoutEngine() = default;

  // Compute the layout. Never throws and never returns negative sizes; a skin
  // with no usable images still produces a sane flat layout.
  SsfLayoutResult Compute(const Skin& skin,
                          const LayoutOptions& options,
                          const LayoutInput& input,
                          const ITextMeasurer& measurer) const;

  // Index of the candidate whose `box` contains the point, or -1. This is what
  // keeps the mouse hit targets aligned with the drawn visuals.
  static int HitTest(const SsfLayoutResult& layout, int x, int y);

  // Index of the status-bar button containing the point, or -1. Order matches
  // Layout's icon_* fields: 0 cn_en, 1 biaodian, 2 quan_ban, 3 fan_jian,
  // 4 menu.
  static int HitTestStatusBar(const SsfLayoutResult& layout, int x, int y);
};

}  // namespace ssf
}  // namespace weasel
