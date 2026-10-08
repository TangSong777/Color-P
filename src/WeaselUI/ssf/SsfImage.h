// Bitmap handling for the Sogou SSF compatibility layer.
//
// SsfImage itself is deliberately free of Windows and GDI+ dependencies: it is
// a plain 32-bit RGBA buffer plus the compositing operations the renderer needs.
// All platform decoding lives in SsfImageLoader.h/cpp. That split is what makes
// the nine-slice maths unit-testable without a window station.

#pragma once

// See the note in SsfSkin.h: this module is compiled into a project that does
// not define WIN32_LEAN_AND_MEAN, so windows.h's min/max macros must be
// suppressed before any Windows header is reached.
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "SsfSkin.h"

namespace weasel {
namespace ssf {

// A straight (non-premultiplied) 32-bit RGBA bitmap.
struct SsfImage {
  int width = 0;
  int height = 0;
  // Row-major RGBA, `width * height * 4` bytes. Empty when the image failed to
  // load, which every consumer must tolerate.
  std::vector<uint8_t> pixels;

  bool Valid() const {
    return width > 0 && height > 0 &&
           pixels.size() >= static_cast<size_t>(width) * height * 4;
  }

  // Accessors mirroring Rect, so call sites read the same way for both.
  int Width() const { return width; }
  int Height() const { return height; }

  void Reset() {
    width = height = 0;
    pixels.clear();
  }

  // ---------------------------------------------------------------------
  // Compositing primitives
  // ---------------------------------------------------------------------

  // Source-over composite `src` into this image at (dx, dy), clipped to bounds.
  // Alpha is composited as well, so partially transparent destinations stay
  // partially transparent.
  void Blend(const SsfImage& src, int dx, int dy);

  // Draw `src` into the destination rectangle with 9-slice scaling. The four
  // border widths come from `ns` and are clamped to fit; the centre and the
  // four edges are stretched (not tiled). Corners are copied 1:1.
  //
  // Safe for any combination of sizes: it clamps negative or oversized border
  // values, never produces a rectangle outside the source bitmap, and is a
  // no-op when either image is invalid.
  void BlitNineSlice(const SsfImage& src,
                     const NineSlice& ns,
                     int dx,
                     int dy,
                     int dw,
                     int dh);

  // Fill a rectangle with a solid, straight-alpha colour.
  void FillRect(int x, int y, int w, int h, const Color& c);

  // ---------------------------------------------------------------------
  // Text alpha repair
  // ---------------------------------------------------------------------

  // GDI/GDI+ do not maintain the alpha channel of a 32bpp DIB: every pixel a
  // glyph touches gets its alpha byte zeroed, which turns text into holes in a
  // layered window. Call this for each rectangle that had text drawn into it.
  //
  // It only ever *raises* alpha to fully opaque where it was zero, so it cannot
  // damage the surrounding anti-aliased skin pixels.
  void RepairTextAlpha(int x, int y, int w, int h);

  // ---------------------------------------------------------------------
  // Trim
  // ---------------------------------------------------------------------

  // Bounding box of the non-fully-transparent pixels; an empty rect when the
  // image is entirely transparent.
  Rect OpaqueBounds() const;

  // Drop fully transparent rows and columns at the borders so the usable artwork
  // starts at (0, 0).
  //
  // Sogou skins pad their bitmaps with transparent margins -- skin2.png carries
  // six blank rows on top and two at the bottom. 9-slicing such an image scales
  // those blank rows along with everything else, which paints a transparent band
  // into the destination; that is what produced a visible seam between the
  // pinyin strip and the candidate strip. Cropping first makes the artwork's own
  // extent the unit that gets mapped.
  //
  // Returns the offset that was removed. A caller MUST shift its 9-slice borders
  // by the same amount, or the slice boundaries no longer refer to the artwork
  // they were authored against.
  Rect CropTransparentBorders();

  // ---------------------------------------------------------------------
  // Export
  // ---------------------------------------------------------------------

  // Convert to premultiplied BGRA, the layout `UpdateLayeredWindow` expects
  // with AC_SRC_ALPHA. `out` receives width*height*4 bytes.
  void ToPremultipliedBGRA(std::vector<uint8_t>& out) const;
};

using SsfImagePtr = std::shared_ptr<SsfImage>;

}  // namespace ssf
}  // namespace weasel
