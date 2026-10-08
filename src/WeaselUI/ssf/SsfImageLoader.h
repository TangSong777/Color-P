// Platform image decoding for the Sogou SSF compatibility layer.
//
// Sogou skins ship PNGs (and occasionally BMPs). WeaselUI already links GDI+,
// so that is used here rather than pulling in a new dependency or hand-rolling a
// PNG decoder. This file is the only place in ssf/ that touches Windows.

#pragma once

#include <string>
#include <unordered_map>

#include "SsfImage.h"

namespace weasel {
namespace ssf {

// Load one file into a straight-alpha RGBA buffer. Returns a null pointer and
// leaves the log to the caller when the file is missing or undecodable; a bad
// skin must never take the IME down.
SsfImagePtr LoadImageFile(const std::wstring& path);

// Case-insensitive file-name -> decoded image cache for one skin directory.
//
// File names in skin.ini are frequently written in a different case from the
// files on disk (and sometimes with backslashes), so lookup is case-folded.
class SsfImageStore {
 public:
  // Directory that relative image names in skin.ini are resolved against.
  void SetDirectory(const std::wstring& dir) { directory_ = dir; }
  const std::wstring& Directory() const { return directory_; }

  // Decode and cache. `name` is the raw value from skin.ini. Returns null when
  // the image is absent or broken. Repeated lookups are cheap.
  SsfImagePtr Get(const std::string& name);

  // Drop every decoded bitmap. Call when the skin changes so a redeploy does not
  // leak the previous skin's images.
  void Clear() { cache_.clear(); }

  // Number of distinct images successfully decoded so far (diagnostics only).
  size_t LoadedCount() const { return cache_.size(); }

 private:
  std::wstring directory_;
  std::unordered_map<std::string, SsfImagePtr> cache_;
};

// Convert a UTF-8 std::string (as stored in Skin) to a wide string.
std::wstring Utf8ToWide(const std::string& s);

// Case-fold a file name for use as a cache key: lower-cased, separators
// normalised to '/', surrounding whitespace removed.
std::string NormalizeImageKey(const std::string& name);

}  // namespace ssf
}  // namespace weasel
