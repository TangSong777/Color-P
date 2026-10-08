// Derived values for the Sogou SSF skin model.
//
// Kept separate from SsfIniParser.cpp so the "raw parse" and the "interpretation"
// steps stay independently testable: if a convention below turns out to be
// wrong, only this file changes.

#include "SsfIniParser.h"

#include <algorithm>

namespace weasel {
namespace ssf {
namespace {

// Sogou's own defaults, used only when the skin file says nothing at all, so
// the renderer never has to deal with a zero-height text area.
const int kFallbackFontSize = 14;
const int kMinInset = 3;
const int kMinGap = 1;

}  // namespace

void NormalizeScheme(Scheme& s) {
  // -------------------------------------------------------------------------
  // marge -> text insets.
  //
  // skin.ini writes four values per strip. The order established from
  // 9IME (crates/9ime-core/src/skin.rs) and corroborated by the comments in
  // fkxxyz/ssfconv is:
  //
  //     *_marge = top, bottom, left, right
  //
  // i.e. NOT the more familiar left, top, right, bottom.
  //
  // Concretely, with Color-P's [Scheme_H2]:
  //     pinyin_marge   = 7,6,8,10  -> top 7, bottom 6, left  8, right 10
  //     zhongwen_marge = 10,9,8,8  -> top 10, bottom 9, left 8, right  8
  // -------------------------------------------------------------------------
  s.pinyin_insets.left = s.pinyin_marge.left;
  s.pinyin_insets.top = s.pinyin_marge.top;
  s.pinyin_insets.right = s.pinyin_marge.right;
  s.pinyin_insets.bottom = s.pinyin_marge.bottom;

  s.zhongwen_insets.left = s.zhongwen_marge.left;
  s.zhongwen_insets.top = s.zhongwen_marge.top;
  s.zhongwen_insets.right = s.zhongwen_marge.right;
  s.zhongwen_insets.bottom = s.zhongwen_marge.bottom;

  // Gap between the pinyin strip and the candidate strip.
  //
  // This is ZERO by default, and that is the correct reading. 9IME computes
  // pinyin_marge[1] + zhongwen_marge[0] here, but those two values are already
  // the *internal* bottom and top padding of their own strips -- the pinyin
  // strip is sized as pinyin_top + text + pinyin_bottom, and the candidate strip
  // as candidate_top + text + candidate_bottom. Adding them again as an
  // inter-strip gap inserts a second margin that Sogou never draws, which showed
  // up as a fully transparent band between the pink pinyin bar and the candidate
  // strip. That was a visible bug.
  //
  // The two bitmaps are designed to abut: skin2_1.png is a pink bar and
  // skin1_2.png starts with the candidate strip's pink top border, so stacking
  // them with no gap reproduces Sogou's seam exactly.
  //
  // A skin that genuinely wants breathing room between the strips can still say
  // so with an explicit `separator` entry, which reserves a line between them.
  s.gap = s.has_separator ? 1 : 0;
}

void NormalizeSkin(Skin& skin) {
  NormalizeScheme(skin.h1);
  NormalizeScheme(skin.h2);
  NormalizeScheme(skin.v1);
  NormalizeScheme(skin.v2);
}

void ApplySkinDefaults(Skin& skin) {
  if (skin.font_size <= 0) skin.font_size = kFallbackFontSize;
  // Guard against absurd values in hand-edited skins; Sogou's own editor clamps
  // to roughly this range.
  skin.font_size = (std::max)(4, (std::min)(96, skin.font_size));

  // Colours: a skin that omits them still has to be legible. The fallbacks
  // deliberately match Sogou's own defaults rather than Weasel's.
  auto ensure_opaque = [](Color& c, uint8_t r, uint8_t g, uint8_t b) {
    if (c.a == 0) {
      c.r = r;
      c.g = g;
      c.b = b;
      c.a = 255;
    }
  };
  ensure_opaque(skin.pinyin_color, 0x00, 0x44, 0x88);
  ensure_opaque(skin.zhongwen_color, 0x11, 0x11, 0x11);
  ensure_opaque(skin.zhongwen_first_color, 0x00, 0x44, 0xCC);
  ensure_opaque(skin.comphint_color, 0x2F, 0x2F, 0x2F);

  // Clamp the derived insets so a malformed or hostile skin cannot produce a
  // negative-size drawing area.
  auto fix = [](Insets& i) {
    i.left = (std::max)(0, i.left);
    i.top = (std::max)(0, i.top);
    i.right = (std::max)(0, i.right);
    i.bottom = (std::max)(0, i.bottom);
  };

  for (Scheme* s : {&skin.h1, &skin.h2, &skin.v1, &skin.v2}) {
    fix(s->pinyin_insets);
    fix(s->zhongwen_insets);
    // A strip with no margins at all would draw text flush against the bitmap
    // edge and look broken; give it the minimum breathing room Sogou uses.
    if (s->pinyin_insets.left == 0 && s->pinyin_insets.right == 0 &&
        s->pinyin_insets.top == 0) {
      s->pinyin_insets.left = kMinInset;
      s->pinyin_insets.right = kMinInset;
      s->pinyin_insets.top = kMinInset;
    }
    if (s->zhongwen_insets.left == 0 && s->zhongwen_insets.right == 0 &&
        s->zhongwen_insets.top == 0) {
      s->zhongwen_insets.left = kMinInset;
      s->zhongwen_insets.right = kMinInset;
      s->zhongwen_insets.top = kMinInset;
    }
    s->gap = (std::max)(0, s->gap);

    // 9-slice borders must be non-negative; zero is legal and means "this edge
    // stretches all the way to the border".
    s->ns_pinyin.left = (std::max)(0, s->ns_pinyin.left);
    s->ns_pinyin.right = (std::max)(0, s->ns_pinyin.right);
    s->ns_pinyin.top = (std::max)(0, s->ns_pinyin.top);
    s->ns_pinyin.bottom = (std::max)(0, s->ns_pinyin.bottom);
    s->ns_zhongwen.left = (std::max)(0, s->ns_zhongwen.left);
    s->ns_zhongwen.right = (std::max)(0, s->ns_zhongwen.right);
    s->ns_zhongwen.top = (std::max)(0, s->ns_zhongwen.top);
    s->ns_zhongwen.bottom = (std::max)(0, s->ns_zhongwen.bottom);
  }
}

}  // namespace ssf
}  // namespace weasel
