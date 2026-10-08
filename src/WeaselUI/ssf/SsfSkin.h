// Sogou SSF skin model.
//
// This file defines the parsed, structured representation of a Sogou
// (搜狗拼音输入法) `.ssf` skin after `skin.ini` has been interpreted. It carries
// no parsing and no drawing logic, so both the parser and the renderer can be
// unit-tested against it independently.
//
// Parameter semantics recorded here were established from evidence, not from
// guesswork. See docs/ssf-skin.md for the full derivation and for the list of
// fields that remain *unverified*. Short version:
//
//   layout_horizontal = mode, left, right      -> 9-slice borders
//   layout_vertical   = mode, top,  bottom     -> 9-slice borders
//   *_marge           = top, bottom, left, right      (NOT left,top,right,bottom)
//   colours           = 0xRRGGBB written in BGR byte order
//
// Verified against: fkxxyz/ssfconv, Flygeon/9IME, and pixel-level analysis of
// the bundled Color-P skin.

#pragma once

// WeaselUI's stdafx.h deliberately does NOT define WIN32_LEAN_AND_MEAN, so
// <windows.h> is pulled in with its `min`/`max` *macros* still active. Those
// collide head-on with std::min / std::max in this module. Defining NOMINMAX
// here, before any Windows header is reached through this include chain,
// suppresses them. This must stay above the first #include.
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace weasel {
namespace ssf {

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

struct Rect {
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;

  int Width() const { return right - left; }
  int Height() const { return bottom - top; }
  bool Empty() const { return right <= left || bottom <= top; }
};

struct Point {
  int x = 0;
  int y = 0;
};

struct Size {
  int cx = 0;
  int cy = 0;
  bool Empty() const { return cx <= 0 || cy <= 0; }
};

// Margin quad, stored the way skin.ini writes it: [top, bottom, left, right].
struct Margin4 {
  int top = 0;
  int bottom = 0;
  int left = 0;
  int right = 0;
};

struct Insets {
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;
};

// RGBA, straight (NOT premultiplied). `a == 0` means fully transparent.
struct Color {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t a = 255;
};

// ---------------------------------------------------------------------------
// Scheme (one of [Scheme_H1] / [Scheme_H2] / [Scheme_V1] / [Scheme_V2])
// ---------------------------------------------------------------------------

// A 9-slice rule: how much of the source bitmap must be preserved verbatim at
// each edge, and what is stretched in between.
struct NineSlice {
  int left = 0;
  int right = 0;
  int top = 0;
  int bottom = 0;
  // skin.ini's leading "mode" field of layout_horizontal / layout_vertical. Its
  // meaning is unverified; it is carried through untouched and never used to
  // switch behaviour.
  int mode_x = 0;
  int mode_y = 0;
};

struct Scheme {
  // Background for the whole window. Present in the "single background" form
  // ([Scheme_H1] / [Scheme_V1]).
  std::string pic;
  // Split-background form ([Scheme_H2] / [Scheme_V2]): a separate background
  // for the pinyin (preedit) strip and for the candidate strip.
  //
  // NOTE: 9IME treats pinyin_pic/zhongwen_pic as *highlight* images. For
  // Color-P that is demonstrably wrong -- see docs/ssf-skin.md §2.5.
  std::string pinyin_pic;
  std::string zhongwen_pic;
  std::string pinyin_pic_hover;
  std::string zhongwen_pic_hover;

  NineSlice ns_pinyin;
  NineSlice ns_zhongwen;

  Margin4 pinyin_marge;
  Margin4 zhongwen_marge;

  // Text insets derived from the marge values, in the convention the renderer
  // uses. Filled in by NormalizeScheme().
  Insets pinyin_insets;    // text inset inside the pinyin strip
  Insets zhongwen_insets;  // text inset inside the candidate strip
  int gap = 0;             // vertical gap between the two strips

  Point anchor;
  bool has_anchor = false;

  // Optional separator colour between preedit and candidates.
  Color separator;
  bool has_separator = false;

  // True when this scheme draws pinyin and candidates on one bitmap.
  bool IsSingleBackground() const { return !pic.empty(); }
  // True when this scheme has the split pinyin/candidate backgrounds.
  bool IsSplitBackground() const { return !pinyin_pic.empty() || !zhongwen_pic.empty(); }
};

// ---------------------------------------------------------------------------
// Status bar
// ---------------------------------------------------------------------------

struct SsfImage;  // fwd (defined in SsfImage.h)

// One status-bar button (cn/en, punctuation, full/half shape, trad/simp, menu).
struct StatusButton {
  bool display = false;
  Point pos;

  // Normal / hover / pressed image file names, one per visual state the button
  // can take. `names.size()` > 1 only for the cn/en button, whose third entry
  // is the "other" state (e.g. Chinese with a temporary English run).
  std::vector<std::string> names;
  std::vector<std::string> names_hover;
  std::vector<std::string> names_down;

  bool Empty() const { return names.empty(); }
};

struct StatusBar {
  std::string pic;

  StatusButton cn_en;
  StatusButton biaodian;
  StatusButton quan_ban;
  StatusButton fan_jian;
  StatusButton menu;

  bool AnyVisible() const {
    return cn_en.display || biaodian.display || quan_ban.display ||
           fan_jian.display || menu.display;
  }
};

// ---------------------------------------------------------------------------
// Whole skin
// ---------------------------------------------------------------------------

struct Skin {
  // [General]
  std::string skin_name;
  std::string skin_version;
  std::string skin_author;

  // [Display]
  int font_size = 14;
  std::string font_ch;
  std::string font_en;
  Color pinyin_color;
  Color zhongwen_first_color;
  Color zhongwen_color;
  Color comphint_color;

  // Raw, uninterpreted [Display] flags. Carried through for diagnostics only;
  // nothing in the renderer depends on them because their semantics are
  // unverified.
  bool use_gdip = false;
  bool aero = false;
  bool glow = false;
  bool large_font_support = false;

  Scheme h1;  // horizontal, single background
  Scheme h2;  // horizontal, split background
  Scheme v1;  // vertical, single background
  Scheme v2;  // vertical, split background

  StatusBar status;

  // Pick the scheme for the requested orientation. Prefers the split-background
  // form when it is present, because that is what Sogou actually renders for a
  // skin that defines pinyin_pic/zhongwen_pic, and it is the only way to
  // reproduce the two-tone preedit/candidate look.
  const Scheme& Horizontal(bool prefer_split = true) const {
    if (prefer_split && h2.IsSplitBackground()) return h2;
    if (h1.IsSingleBackground()) return h1;
    return h2;
  }
  const Scheme& Vertical(bool prefer_split = true) const {
    if (prefer_split && v2.IsSplitBackground()) return v2;
    if (v1.IsSingleBackground()) return v1;
    return v2;
  }

  bool Empty() const { return skin_name.empty() && font_ch.empty(); }
};

// ---------------------------------------------------------------------------
// Derived helpers (implemented in SsfSkin.cpp)
// ---------------------------------------------------------------------------

// Fill in pinyin_insets / zhongwen_insets / gap from the raw marge values,
// applying the established [top, bottom, left, right] convention.
void NormalizeScheme(Scheme& scheme);

// Same, for every scheme in a skin.
void NormalizeSkin(Skin& skin);

// Apply sane defaults to anything the skin file omitted, so the renderer never
// has to special-case a half-specified skin.
void ApplySkinDefaults(Skin& skin);

}  // namespace ssf
}  // namespace weasel
