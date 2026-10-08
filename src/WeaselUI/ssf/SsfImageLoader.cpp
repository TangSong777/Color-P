// Platform image decoding for the Sogou SSF compatibility layer.

#include "SsfImageLoader.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <memory>

// gdiplus.h declares methods that take IStream/IStorage, so the COM interface
// headers must be included before it. WeaselUI's stdafx.h pulls those in via
// ATL; this module deliberately does not use the precompiled header, so the
// dependency is stated explicitly rather than relied upon through include order
// elsewhere.
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

namespace weasel {
namespace ssf {
namespace {

// GDI+ must be initialised before any Gdiplus::Bitmap is constructed. WeaselUI
// already calls GdiplusStartup for its own drawing, but this module must not
// depend on that ordering, so it keeps its own reference-counted token.
class GdiPlusScope {
 public:
  GdiPlusScope() {
    Gdiplus::GdiplusStartupInput input;
    ok_ = (Gdiplus::GdiplusStartup(&token_, &input, nullptr) == Gdiplus::Ok);
  }
  ~GdiPlusScope() {
    if (ok_) Gdiplus::GdiplusShutdown(token_);
  }
  bool ok() const { return ok_; }

 private:
  ULONG_PTR token_ = 0;
  bool ok_ = false;
};

GdiPlusScope& GdiPlus() {
  // Function-local static: initialised once, thread-safe since C++11, and torn
  // down at process exit.
  static GdiPlusScope scope;
  return scope;
}

}  // namespace

std::wstring Utf8ToWide(const std::string& s) {
  if (s.empty()) return std::wstring();
  const int need = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                         static_cast<int>(s.size()), nullptr, 0);
  if (need <= 0) return std::wstring();
  std::wstring out(static_cast<size_t>(need), L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                        &out[0], need);
  return out;
}

std::string NormalizeImageKey(const std::string& name) {
  std::string out;
  out.reserve(name.size());
  size_t b = 0, e = name.size();
  auto is_space = [](unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
  };
  while (b < e && is_space(static_cast<unsigned char>(name[b]))) ++b;
  while (e > b && is_space(static_cast<unsigned char>(name[e - 1]))) --e;
  for (size_t i = b; i < e; ++i) {
    char c = name[i];
    if (c == '\\') c = '/';
    out.push_back(static_cast<char>(
        std::tolower(static_cast<unsigned char>(c))));
  }
  return out;
}

SsfImagePtr LoadImageFile(const std::wstring& path) {
  if (path.empty() || !GdiPlus().ok()) return nullptr;

  // GDI+ refuses to load from a path unless the file exists; failing cheaply
  // here also keeps the Windows error dialog away for missing skin assets.
  const DWORD attrs = ::GetFileAttributesW(path.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY))
    return nullptr;

  auto image = std::make_shared<SsfImage>();

  {
    std::unique_ptr<Gdiplus::Bitmap> bmp(
        Gdiplus::Bitmap::FromFile(path.c_str(), FALSE));
    if (!bmp || bmp->GetLastStatus() != Gdiplus::Ok) return nullptr;

    const int w = static_cast<int>(bmp->GetWidth());
    const int h = static_cast<int>(bmp->GetHeight());
    if (w <= 0 || h <= 0) return nullptr;

    // Lock in 32bpp ARGB. GDI+ converts the source format (including indexed
    // and 24bpp PNGs) on the fly; the stride may still exceed w*4, so rows are
    // copied individually.
    Gdiplus::Rect rect(0, 0, w, h);
    Gdiplus::BitmapData data;
    std::memset(&data, 0, sizeof(data));
    if (bmp->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB,
                      &data) != Gdiplus::Ok) {
      return nullptr;
    }

    image->width = w;
    image->height = h;
    image->pixels.assign(static_cast<size_t>(w) * h * 4, 0);

    const auto* src = static_cast<const uint8_t*>(data.Scan0);
    for (int y = 0; y < h; ++y) {
      const uint8_t* srow = src + static_cast<ptrdiff_t>(y) * data.Stride;
      uint8_t* drow = image->pixels.data() + static_cast<size_t>(y) * w * 4;
      for (int x = 0; x < w; ++x) {
        // PixelFormat32bppARGB on little-endian is byte order B, G, R, A.
        drow[x * 4 + 0] = srow[x * 4 + 2];  // R
        drow[x * 4 + 1] = srow[x * 4 + 1];  // G
        drow[x * 4 + 2] = srow[x * 4 + 0];  // B
        drow[x * 4 + 3] = srow[x * 4 + 3];  // A
      }
    }

    bmp->UnlockBits(&data);
  }

  if (!image->Valid()) return nullptr;
  return image;
}

SsfImagePtr SsfImageStore::Get(const std::string& name) {
  const std::string key = NormalizeImageKey(name);
  if (key.empty()) return nullptr;

  auto it = cache_.find(key);
  if (it != cache_.end()) return it->second;

  SsfImagePtr image;
  if (!directory_.empty()) {
    std::wstring path = directory_;
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/')
      path.push_back(L'\\');
    path += Utf8ToWide(key);
    image = LoadImageFile(path);
  }

  // Cache misses too, so a broken skin does not retry the filesystem on every
  // repaint. A null entry is therefore a legitimate value in this map.
  cache_[key] = image;
  return image;
}

}  // namespace ssf
}  // namespace weasel
