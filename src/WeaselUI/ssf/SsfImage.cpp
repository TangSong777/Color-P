// Bitmap compositing for the Sogou SSF compatibility layer.
//
// Pure integer maths: no Windows, no GDI+, no floating point. Everything here is
// reachable from a unit test.

#include "SsfImage.h"

#include <algorithm>
#include <cstring>

namespace weasel {
namespace ssf {
namespace {

// Source-over composite of one straight-alpha RGBA pixel onto another.
inline void BlendPixel(uint8_t* dst, const uint8_t* src) {
  const unsigned sa = src[3];
  if (sa == 0) return;
  if (sa == 255) {
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = 255;
    return;
  }
  const unsigned inv = 255u - sa;
  const unsigned da = dst[3];

  // out.a = sa + da * (1 - sa)
  const unsigned oa = sa + (da * inv + 127u) / 255u;

  // out.rgb = (src.rgb * sa + dst.rgb * da * (1 - sa)) / out.a
  auto chan = [&](unsigned s, unsigned d) -> uint8_t {
    if (oa == 0) return 0;
    const unsigned num = s * sa + (d * da * inv + 127u) / 255u;
    const unsigned v = (num + oa / 2u) / oa;
    return static_cast<uint8_t>(v > 255u ? 255u : v);
  };

  dst[0] = chan(src[0], dst[0]);
  dst[1] = chan(src[1], dst[1]);
  dst[2] = chan(src[2], dst[2]);
  dst[3] = static_cast<uint8_t>(oa > 255u ? 255u : oa);
}

inline const uint8_t* PixelAt(const SsfImage& img, int x, int y) {
  return &img.pixels[(static_cast<size_t>(y) * img.width + x) * 4];
}

inline uint8_t* PixelAt(SsfImage& img, int x, int y) {
  return &img.pixels[(static_cast<size_t>(y) * img.width + x) * 4];
}

}  // namespace

void SsfImage::Blend(const SsfImage& src, int dx, int dy) {
  if (!Valid() || !src.Valid()) return;

  const int x0 = (std::max)(0, dx);
  const int y0 = (std::max)(0, dy);
  const int x1 = (std::min)(width, dx + src.width);
  const int y1 = (std::min)(height, dy + src.height);
  if (x0 >= x1 || y0 >= y1) return;

  for (int y = y0; y < y1; ++y) {
    const uint8_t* srow = PixelAt(src, x0 - dx, y - dy);
    uint8_t* drow = PixelAt(*this, x0, y);
    for (int x = x0; x < x1; ++x, srow += 4, drow += 4)
      BlendPixel(drow, srow);
  }
}

void SsfImage::FillRect(int x, int y, int w, int h, const Color& c) {
  if (!Valid() || w <= 0 || h <= 0 || c.a == 0) return;

  const int x0 = (std::max)(0, x);
  const int y0 = (std::max)(0, y);
  const int x1 = (std::min)(width, x + w);
  const int y1 = (std::min)(height, y + h);
  if (x0 >= x1 || y0 >= y1) return;

  if (c.a == 255) {
    for (int yy = y0; yy < y1; ++yy) {
      uint8_t* p = PixelAt(*this, x0, yy);
      for (int xx = x0; xx < x1; ++xx, p += 4) {
        p[0] = c.r;
        p[1] = c.g;
        p[2] = c.b;
        p[3] = 255;
      }
    }
    return;
  }

  const uint8_t src[4] = {c.r, c.g, c.b, c.a};
  for (int yy = y0; yy < y1; ++yy) {
    uint8_t* p = PixelAt(*this, x0, yy);
    for (int xx = x0; xx < x1; ++xx, p += 4)
      BlendPixel(p, src);
  }
}

void SsfImage::BlitNineSlice(const SsfImage& src,
                             const NineSlice& ns,
                             int dx,
                             int dy,
                             int dw,
                             int dh) {
  if (!Valid() || !src.Valid() || dw <= 0 || dh <= 0) return;

  const int sw = src.width;
  const int sh = src.height;

  // Clamp the requested borders so that left+right never exceed either the
  // source or the destination, which keeps every derived rectangle inside both
  // bitmaps and free of negative widths.
  int l = (std::max)(0, ns.left);
  int r = (std::max)(0, ns.right);
  int t = (std::max)(0, ns.top);
  int b = (std::max)(0, ns.bottom);

  l = (std::min)(l, sw);
  r = (std::min)(r, sw - l);
  t = (std::min)(t, sh);
  b = (std::min)(b, sh - t);

  // Destination side: if the fixed borders do not fit, shrink them in the same
  // proportion the source uses.
  if (l + r > dw) {
    const int total = l + r;
    if (total > 0) {
      // integer proportion, rounded down for the left part
      const int nl = static_cast<int>(static_cast<long long>(l) * dw / total);
      l = (std::max)(0, (std::min)(dw, nl));
      r = dw - l;
    } else {
      l = r = 0;
    }
  }
  if (t + b > dh) {
    const int total = t + b;
    if (total > 0) {
      const int nt = static_cast<int>(static_cast<long long>(t) * dh / total);
      t = (std::max)(0, (std::min)(dh, nt));
      b = dh - t;
    } else {
      t = b = 0;
    }
  }

  const int smw = sw - l - r;  // source middle width
  const int smh = sh - t - b;  // source middle height
  const int dmw = dw - l - r;  // destination middle width
  const int dmh = dh - t - b;  // destination middle height

  // Copy one (possibly scaled) sub-rectangle, nearest-neighbour.
  // Source coordinates are absolute within `src`; the caller passes the same
  // origin it used to derive the sizes, so no relative offsets are involved.
  auto blit = [&](int sx, int sy, int swidth, int sheight, int ddx, int ddy,
                  int dwidth, int dheight) {
    if (swidth <= 0 || sheight <= 0 || dwidth <= 0 || dheight <= 0) return;
    for (int y = 0; y < dheight; ++y) {
      const int dyp = ddy + y;
      if (dyp < 0 || dyp >= height) continue;
      const int syy =
          sy + static_cast<int>(static_cast<long long>(y) * sheight / dheight);
      if (syy < 0 || syy >= sh) continue;
      uint8_t* drow = PixelAt(*this, 0, dyp);
      for (int x = 0; x < dwidth; ++x) {
        const int dxp = ddx + x;
        if (dxp < 0 || dxp >= width) continue;
        const int sxx =
            sx + static_cast<int>(static_cast<long long>(x) * swidth / dwidth);
        if (sxx < 0 || sxx >= sw) continue;
        BlendPixel(drow + static_cast<size_t>(dxp) * 4, PixelAt(src, sxx, syy));
      }
    }
  };

  // Corners: verbatim, capped by their own border size. The destination side is
  // already guaranteed to fit at least l+r and t+b, so this cannot overshoot;
  // `blit` additionally clips per pixel, which makes the whole routine safe
  // even for degenerate inputs.
  const int cl = l;
  const int cr = r;
  const int ct = t;
  const int cb = b;

  // Top-left / top-right / bottom-left / bottom-right
  blit(0, 0, cl, ct, dx, dy, cl, ct);
  blit(sw - cr, 0, cr, ct, dx + dw - cr, dy, cr, ct);
  blit(0, sh - cb, cl, cb, dx, dy + dh - cb, cl, cb);
  blit(sw - cr, sh - cb, cr, cb, dx + dw - cr, dy + dh - cb, cr, cb);

  // Edges: stretched along one axis, verbatim along the other.
  blit(l, 0, smw, ct, dx + cl, dy, dmw, ct);                            // top
  blit(l, sh - cb, smw, cb, dx + cl, dy + dh - cb, dmw, cb);            // bottom
  blit(0, t, cl, smh, dx, dy + ct, cl, dmh);                            // left
  blit(sw - cr, t, cr, smh, dx + dw - cr, dy + ct, cr, dmh);            // right

  // Centre: stretched along both axes.
  blit(l, t, smw, smh, dx + cl, dy + ct, dmw, dmh);
}

void SsfImage::RepairTextAlpha(int x, int y, int w, int h) {
  if (!Valid() || w <= 0 || h <= 0) return;

  const int x0 = (std::max)(0, x);
  const int y0 = (std::max)(0, y);
  const int x1 = (std::min)(width, x + w);
  const int y1 = (std::min)(height, y + h);
  if (x0 >= x1 || y0 >= y1) return;

  for (int yy = y0; yy < y1; ++yy) {
    uint8_t* p = PixelAt(*this, x0, yy);
    for (int xx = x0; xx < x1; ++xx, p += 4) {
      if (p[3] == 0) p[3] = 255;
    }
  }
}

Rect SsfImage::OpaqueBounds() const {
  Rect r;
  if (!Valid()) return r;
  int minx = width, miny = height, maxx = -1, maxy = -1;
  for (int y = 0; y < height; ++y) {
    const uint8_t* p = PixelAt(*this, 0, y);
    for (int x = 0; x < width; ++x, p += 4) {
      if (p[3] == 0) continue;
      if (x < minx) minx = x;
      if (x > maxx) maxx = x;
      if (y < miny) miny = y;
      if (y > maxy) maxy = y;
    }
  }
  if (maxx < 0) return Rect{};  // fully transparent
  r.left = minx;
  r.top = miny;
  r.right = maxx + 1;
  r.bottom = maxy + 1;
  return r;
}

Rect SsfImage::CropTransparentBorders() {
  const Rect b = OpaqueBounds();
  if (b.Empty()) return Rect{};  // nothing to do; caller decides what that means
  if (b.left == 0 && b.top == 0 && b.right == width && b.bottom == height)
    return Rect{};  // already tight

  const int nw = b.Width();
  const int nh = b.Height();
  std::vector<uint8_t> dst(static_cast<size_t>(nw) * nh * 4);
  for (int y = 0; y < nh; ++y) {
    const uint8_t* srow = PixelAt(*this, b.left, b.top + y);
    std::memcpy(&dst[static_cast<size_t>(y) * nw * 4], srow,
                static_cast<size_t>(nw) * 4);
  }
  pixels.swap(dst);
  width = nw;
  height = nh;

  Rect removed;
  removed.left = b.left;
  removed.top = b.top;
  removed.right = 0;
  removed.bottom = 0;
  return removed;
}

void SsfImage::ToPremultipliedBGRA(std::vector<uint8_t>& out) const {
  const size_t n = static_cast<size_t>((std::max)(0, width)) *
                   static_cast<size_t>((std::max)(0, height));
  out.assign(n * 4, 0);
  if (!Valid()) return;

  for (size_t i = 0; i < n; ++i) {
    const uint8_t r = pixels[i * 4 + 0];
    const uint8_t g = pixels[i * 4 + 1];
    const uint8_t b = pixels[i * 4 + 2];
    const uint8_t a = pixels[i * 4 + 3];
    if (a == 0) continue;  // fully transparent; premultiplied form is all zero
    if (a == 255) {
      out[i * 4 + 0] = b;
      out[i * 4 + 1] = g;
      out[i * 4 + 2] = r;
      out[i * 4 + 3] = 255;
      continue;
    }
    out[i * 4 + 0] = static_cast<uint8_t>((static_cast<unsigned>(b) * a + 127u) / 255u);
    out[i * 4 + 1] = static_cast<uint8_t>((static_cast<unsigned>(g) * a + 127u) / 255u);
    out[i * 4 + 2] = static_cast<uint8_t>((static_cast<unsigned>(r) * a + 127u) / 255u);
    out[i * 4 + 3] = a;
  }
}

}  // namespace ssf
}  // namespace weasel
