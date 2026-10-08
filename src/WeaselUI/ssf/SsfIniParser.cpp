// skin.ini parsing for the Sogou SSF compatibility layer.
//
// See SsfIniParser.h for the contract and docs/ssf-skin.md for the evidence
// behind the parameter semantics implemented here.

#include "SsfIniParser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

namespace weasel {
namespace ssf {
namespace {

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

inline bool IsSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' ||
         c == '\f';
}

std::string Trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && IsSpace(s[b])) ++b;
  while (e > b && IsSpace(s[e - 1])) --e;
  return s.substr(b, e - b);
}

std::string Lower(const std::string& s) {
  std::string out(s);
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
  }
  return out;
}

bool StartsWith(const std::string& s, const char* prefix) {
  size_t n = std::strlen(prefix);
  return s.size() >= n && std::memcmp(s.data(), prefix, n) == 0;
}

// Convert a UTF-16 code unit sequence (already host-order) to wchar_t string.
// On Windows wchar_t is 16-bit, so this is a copy; elsewhere each unit is
// widened (astral characters then occupy two wchar_t values, which is
// acceptable because the result is only ever passed back to Windows APIs).
std::wstring FromUtf16(const uint16_t* units, size_t count) {
  std::wstring out;
  out.reserve(count);
  for (size_t i = 0; i < count; ++i)
    out.push_back(static_cast<wchar_t>(units[i]));
  return out;
}

// Strict UTF-8 validation. Rejects overlong forms, surrogates and > U+10FFFF so
// that a GBK file does not accidentally pass as UTF-8 and produce mojibake.
bool IsValidUtf8(const unsigned char* p, size_t n) {
  size_t i = 0;
  while (i < n) {
    unsigned char c = p[i];
    if (c < 0x80) {
      ++i;
      continue;
    }
    size_t extra;
    uint32_t cp;
    if ((c & 0xE0) == 0xC0) {
      extra = 1;
      cp = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
      extra = 2;
      cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
      extra = 3;
      cp = c & 0x07;
    } else {
      return false;
    }
    if (i + extra >= n) return false;
    for (size_t k = 1; k <= extra; ++k) {
      unsigned char cc = p[i + k];
      if ((cc & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (cc & 0x3F);
    }
    if (extra == 1 && cp < 0x80) return false;
    if (extra == 2 && cp < 0x800) return false;
    if (extra == 3 && cp < 0x10000) return false;
    if (cp > 0x10FFFF) return false;
    if (cp >= 0xD800 && cp <= 0xDFFF) return false;
    i += extra + 1;
  }
  return true;
}

std::wstring Utf8ToWide(const unsigned char* p, size_t n) {
  std::wstring out;
  out.reserve(n);
  size_t i = 0;
  while (i < n) {
    unsigned char c = p[i];
    if (c < 0x80) {
      out.push_back(static_cast<wchar_t>(c));
      ++i;
      continue;
    }
    size_t extra;
    uint32_t cp;
    if ((c & 0xE0) == 0xC0) {
      extra = 1;
      cp = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
      extra = 2;
      cp = c & 0x0F;
    } else {
      extra = 3;
      cp = c & 0x07;
    }
    for (size_t k = 1; k <= extra && i + k < n; ++k)
      cp = (cp << 6) | (p[i + k] & 0x3F);
    i += extra + 1;
    if (cp <= 0xFFFF) {
      out.push_back(static_cast<wchar_t>(cp));
    } else {
      cp -= 0x10000;
      out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
      out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
    }
  }
  return out;
}

std::wstring GbkToWide(const char* data, size_t size) {
#ifdef _WIN32
  if (size == 0) return std::wstring();
  int need = ::MultiByteToWideChar(936, 0, data, static_cast<int>(size), nullptr, 0);
  if (need <= 0) {
    // Unmappable input: fall back to Latin-1 so we never lose the ASCII
    // structure of the file (section and key names are always ASCII).
    std::wstring out;
    out.reserve(size);
    for (size_t i = 0; i < size; ++i)
      out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(data[i])));
    return out;
  }
  std::wstring out(static_cast<size_t>(need), L'\0');
  ::MultiByteToWideChar(936, 0, data, static_cast<int>(size), &out[0], need);
  return out;
#else
  std::wstring out;
  out.reserve(size);
  for (size_t i = 0; i < size; ++i)
    out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(data[i])));
  return out;
#endif
}

// ---------------------------------------------------------------------------
// Scalar parsing
// ---------------------------------------------------------------------------

// Accepts decimal and 0x-prefixed hex, with surrounding whitespace. `ok` is set
// to false when nothing valid was consumed.
long ParseLong(const std::string& s, bool* ok) {
  std::string t = Trim(s);
  if (ok) *ok = false;
  if (t.empty()) return 0;

  bool neg = false;
  size_t i = 0;
  if (t[0] == '+' || t[0] == '-') {
    neg = (t[0] == '-');
    i = 1;
  }
  if (i >= t.size()) return 0;

  long value = 0;
  bool any = false;
  if (i + 1 < t.size() && t[i] == '0' && (t[i + 1] == 'x' || t[i + 1] == 'X')) {
    i += 2;
    for (; i < t.size(); ++i) {
      char c = t[i];
      int d;
      if (c >= '0' && c <= '9')
        d = c - '0';
      else if (c >= 'a' && c <= 'f')
        d = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F')
        d = c - 'A' + 10;
      else
        break;
      value = value * 16 + d;
      any = true;
    }
  } else {
    for (; i < t.size(); ++i) {
      char c = t[i];
      if (c < '0' || c > '9') break;
      value = value * 10 + (c - '0');
      any = true;
    }
  }
  if (!any) return 0;
  if (ok) *ok = true;
  return neg ? -value : value;
}

// ---------------------------------------------------------------------------
// Scheme assembly
// ---------------------------------------------------------------------------

void ReadStatusButton(const IniFile& ini,
                      const std::string& section,
                      const std::string& base,
                      StatusButton& out) {
  out.display = IniGetBool(ini, section, base + "_display", false);

  const std::string pos = IniGetString(ini, section, base + "_pos");
  if (!pos.empty()) {
    Point p;
    if (ParsePoint(pos, p)) out.pos = p;
  }

  auto split_list = [](const std::string& v) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : v) {
      if (c == ',') {
        parts.push_back(Trim(cur));
        cur.clear();
      } else {
        cur.push_back(c);
      }
    }
    if (!Trim(cur).empty()) parts.push_back(Trim(cur));
    return parts;
  };

  const std::string normal = IniGetString(ini, section, base);
  if (!normal.empty()) out.names = split_list(normal);
  const std::string hover = IniGetString(ini, section, base + "_hover");
  if (!hover.empty()) out.names_hover = split_list(hover);
  const std::string down = IniGetString(ini, section, base + "_down");
  if (!down.empty()) out.names_down = split_list(down);
}

Scheme ReadScheme(const IniFile& ini, const std::string& section) {
  Scheme s;

  s.pic = IniGetString(ini, section, "pic");
  s.pinyin_pic = IniGetString(ini, section, "pinyin_pic");
  s.zhongwen_pic = IniGetString(ini, section, "zhongwen_pic");
  s.pinyin_pic_hover = IniGetString(ini, section, "pinyin_pic_hover");
  s.zhongwen_pic_hover = IniGetString(ini, section, "zhongwen_pic_hover");

  // The split-background form names its layout keys after the image they
  // describe; the single-background form uses the bare names. Read the
  // qualified key first and fall back to the unqualified one, so both H1 and H2
  // end up populated.
  auto read_ns = [&](const char* qual, const char* plain, bool horizontal,
                     NineSlice& out) {
    std::string v = IniGetString(ini, section, qual);
    if (v.empty()) v = IniGetString(ini, section, plain);
    if (v.empty()) return;
    if (horizontal)
      ParseNineSliceH(v, out);
    else
      ParseNineSliceV(v, out);
  };

  read_ns("pinyin_layout_horizontal", "layout_horizontal", true, s.ns_pinyin);
  read_ns("pinyin_layout_vertical", "layout_vertical", false, s.ns_pinyin);
  read_ns("zhongwen_layout_horizontal", "layout_horizontal", true,
          s.ns_zhongwen);
  read_ns("zhongwen_layout_vertical", "layout_vertical", false, s.ns_zhongwen);

  // Margins. The split form can give one marge pair per strip; the single form
  // gives a single pair that applies to both.
  const std::string pm = IniGetString(ini, section, "pinyin_marge");
  if (!pm.empty()) ParseMargin4(pm, s.pinyin_marge);
  std::string zm = IniGetString(ini, section, "zhongwen_marge");
  if (zm.empty()) zm = pm;  // single-background skins often omit it
  if (!zm.empty()) ParseMargin4(zm, s.zhongwen_marge);

  const std::string anchor = IniGetString(ini, section, "anchor");
  if (!anchor.empty()) {
    Point p;
    if (ParsePoint(anchor, p)) {
      s.anchor = p;
      s.has_anchor = true;
    }
  }

  const std::string sep = IniGetString(ini, section, "separator");
  if (!sep.empty()) {
    // "separator=0xd8d8d8,66,196" -- first element is the colour, the rest are
    // geometry we do not currently use.
    std::string first = sep;
    size_t comma = sep.find(',');
    if (comma != std::string::npos) first = sep.substr(0, comma);
    Color c;
    if (ParseSogouColor(Trim(first), c)) {
      s.separator = c;
      s.has_separator = true;
    }
  }

  return s;
}

}  // namespace

// ---------------------------------------------------------------------------
// Text decoding
// ---------------------------------------------------------------------------

std::wstring DecodeIniText(const char* data, size_t size) {
  if (data == nullptr || size == 0) return std::wstring();
  const unsigned char* p = reinterpret_cast<const unsigned char*>(data);

  // 1 / 2: UTF-16 with BOM
  if (size >= 2 && p[0] == 0xFF && p[1] == 0xFE) {
    std::vector<uint16_t> units;
    units.reserve((size - 2) / 2);
    for (size_t i = 2; i + 1 < size; i += 2)
      units.push_back(static_cast<uint16_t>(p[i] | (p[i + 1] << 8)));
    return FromUtf16(units.data(), units.size());
  }
  if (size >= 2 && p[0] == 0xFE && p[1] == 0xFF) {
    std::vector<uint16_t> units;
    units.reserve((size - 2) / 2);
    for (size_t i = 2; i + 1 < size; i += 2)
      units.push_back(static_cast<uint16_t>((p[i] << 8) | p[i + 1]));
    return FromUtf16(units.data(), units.size());
  }

  // 3: UTF-8 with BOM
  if (size >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF)
    return Utf8ToWide(p + 3, size - 3);

  // 4: strict UTF-8
  if (IsValidUtf8(p, size)) return Utf8ToWide(p, size);

  // 5: GBK / CP936
  return GbkToWide(data, size);
}

// ---------------------------------------------------------------------------
// INI parsing
// ---------------------------------------------------------------------------

IniFile ParseIni(const std::wstring& text) {
  IniFile ini;
  std::string current;

  // Work line by line. The input is already wide; narrow it back to UTF-8 for
  // storage so that keys and values are plain std::string. Values that are not
  // ASCII (font names!) survive as UTF-8 bytes.
  std::string line;
  auto flush = [&]() {
    // strip comments: ';' or '#' outside of nothing special (no quoting support
    // in skin.ini)
    size_t cut = std::string::npos;
    for (size_t i = 0; i < line.size(); ++i) {
      if (line[i] == ';' || line[i] == '#') {
        cut = i;
        break;
      }
    }
    if (cut != std::string::npos) line = line.substr(0, cut);

    std::string t = Trim(line);
    line.clear();
    if (t.empty()) return;

    if (t[0] == '[') {
      size_t close = t.find(']');
      if (close != std::string::npos)
        current = Lower(Trim(t.substr(1, close - 1)));
      else
        current = Lower(Trim(t.substr(1)));
      return;
    }
    if (current.empty()) return;  // key before any section: ignored

    std::string key, value;
    size_t eq = t.find('=');
    if (eq != std::string::npos) {
      key = Trim(t.substr(0, eq));
      value = Trim(t.substr(eq + 1));
    } else {
      // bare keyword: keep it with value "1" so presence can be tested
      key = Trim(t);
      value = "1";
    }
    if (key.empty()) return;
    ini[current][Lower(key)] = value;
  };

  for (wchar_t wc : text) {
    if (wc == L'\n' || wc == L'\r') {
      flush();
      continue;
    }
    // narrow to UTF-8
    uint32_t cp = static_cast<uint32_t>(wc);
    if (cp < 0x80) {
      line.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      line.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      line.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      line.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      line.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      line.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      line.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      line.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      line.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      line.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }
  flush();

  return ini;
}

// ---------------------------------------------------------------------------
// Typed accessors
// ---------------------------------------------------------------------------

std::string IniGetString(const IniFile& ini,
                         const std::string& section,
                         const std::string& key,
                         const std::string& def) {
  auto s = ini.find(Lower(section));
  if (s == ini.end()) return def;
  auto k = s->second.find(Lower(key));
  if (k == s->second.end()) return def;
  return k->second;
}

int IniGetInt(const IniFile& ini,
              const std::string& section,
              const std::string& key,
              int def) {
  auto s = ini.find(Lower(section));
  if (s == ini.end()) return def;
  auto k = s->second.find(Lower(key));
  if (k == s->second.end()) return def;
  bool ok = false;
  long v = ParseLong(k->second, &ok);
  return ok ? static_cast<int>(v) : def;
}

bool IniGetBool(const IniFile& ini,
                const std::string& section,
                const std::string& key,
                bool def) {
  auto s = ini.find(Lower(section));
  if (s == ini.end()) return def;
  auto k = s->second.find(Lower(key));
  if (k == s->second.end()) return def;
  const std::string v = Lower(Trim(k->second));
  if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
  if (v == "0" || v == "false" || v == "no" || v == "off") return false;
  bool ok = false;
  long n = ParseLong(v, &ok);
  return ok ? (n != 0) : def;
}

std::vector<int> IniGetIntList(const IniFile& ini,
                               const std::string& section,
                               const std::string& key) {
  std::vector<int> out;
  const std::string v = IniGetString(ini, section, key);
  if (!v.empty()) ParseIntList(v, out);
  return out;
}

// ---------------------------------------------------------------------------
// Value parsers
// ---------------------------------------------------------------------------

bool ParseIntList(const std::string& value, std::vector<int>& out) {
  std::vector<int> parsed;
  std::string cur;
  auto flush = [&]() -> bool {
    std::string t = Trim(cur);
    cur.clear();
    if (t.empty()) return true;  // tolerate "1,2," and "1, 2"
    bool ok = false;
    long v = ParseLong(t, &ok);
    if (!ok) return false;
    parsed.push_back(static_cast<int>(v));
    return true;
  };
  for (char c : value) {
    if (c == ',') {
      if (!flush()) return false;
    } else {
      cur.push_back(c);
    }
  }
  if (!flush()) return false;
  out = parsed;
  return !parsed.empty();
}

bool ParsePoint(const std::string& value, Point& out) {
  std::vector<int> v;
  if (!ParseIntList(value, v) || v.size() < 2) return false;
  out.x = v[0];
  out.y = v[1];
  return true;
}

bool ParseMargin4(const std::string& value, Margin4& out) {
  std::vector<int> v;
  if (!ParseIntList(value, v) || v.size() < 4) return false;
  // skin.ini order is [top, bottom, left, right].
  out.top = v[0];
  out.bottom = v[1];
  out.left = v[2];
  out.right = v[3];
  return true;
}

bool ParseSogouColor(const std::string& value, Color& out, bool with_alpha) {
  std::string t = Trim(value);
  if (t.empty()) return false;
  bool ok = false;
  unsigned long n = static_cast<unsigned long>(ParseLong(t, &ok));
  if (!ok) return false;

  uint32_t v = static_cast<uint32_t>(n);
  uint8_t r, g, b, a;
  // Sogou stores 0xRRGGBB in BGR byte order, so the low byte is red and the
  // high byte is blue.
  r = static_cast<uint8_t>(v & 0xFF);
  g = static_cast<uint8_t>((v >> 8) & 0xFF);
  b = static_cast<uint8_t>((v >> 16) & 0xFF);

  if (!with_alpha) {
    a = 255;
  } else if (v > 0xFFFFFFu) {
    // Eight hex digits: the top byte is alpha (0xAARRGGBB).
    a = static_cast<uint8_t>((v >> 24) & 0xFF);
  } else {
    // Six hex digits: there is no alpha byte present, so the colour is fully
    // opaque. Treating the top byte as alpha here would silently turn every
    // six-digit colour transparent.
    a = 255;
  }
  out.r = r;
  out.g = g;
  out.b = b;
  out.a = a;
  return true;
}

bool ParseNineSliceH(const std::string& value, NineSlice& out) {
  std::vector<int> v;
  if (!ParseIntList(value, v) || v.size() < 3) return false;
  out.mode_x = v[0];
  out.left = v[1];
  out.right = v[2];
  return true;
}

bool ParseNineSliceV(const std::string& value, NineSlice& out) {
  std::vector<int> v;
  if (!ParseIntList(value, v) || v.size() < 3) return false;
  out.mode_y = v[0];
  out.top = v[1];
  out.bottom = v[2];
  return true;
}

// ---------------------------------------------------------------------------
// Skin assembly
// ---------------------------------------------------------------------------

Skin BuildSkinFromIni(const IniFile& ini) {
  Skin skin;

  skin.skin_name = IniGetString(ini, "General", "skin_name");
  skin.skin_version = IniGetString(ini, "General", "skin_version");
  skin.skin_author = IniGetString(ini, "General", "skin_author");

  skin.font_size = IniGetInt(ini, "Display", "font_size", 14);
  skin.font_ch = IniGetString(ini, "Display", "font_ch");
  skin.font_en = IniGetString(ini, "Display", "font_en");

  ParseSogouColor(IniGetString(ini, "Display", "pinyin_color"),
                  skin.pinyin_color);
  ParseSogouColor(IniGetString(ini, "Display", "zhongwen_first_color"),
                  skin.zhongwen_first_color);
  ParseSogouColor(IniGetString(ini, "Display", "zhongwen_color"),
                  skin.zhongwen_color);
  ParseSogouColor(IniGetString(ini, "Display", "comphint_color"),
                  skin.comphint_color);

  skin.use_gdip = IniGetBool(ini, "Display", "use_gdip", false);
  skin.aero = IniGetBool(ini, "Display", "aero", false);
  skin.glow = IniGetBool(ini, "Display", "glow", false);
  skin.large_font_support =
      IniGetBool(ini, "Display", "LargeFontSupport", false);

  skin.h1 = ReadScheme(ini, "Scheme_H1");
  skin.h2 = ReadScheme(ini, "Scheme_H2");
  skin.v1 = ReadScheme(ini, "Scheme_V1");
  skin.v2 = ReadScheme(ini, "Scheme_V2");

  // A scheme that only carries layout/marge but no image is still useful: the
  // renderer may be asked to lay out a skin whose images failed to load.
  skin.status.pic = IniGetString(ini, "StatusBar", "pic");
  ReadStatusButton(ini, "StatusBar", "cn_en", skin.status.cn_en);
  ReadStatusButton(ini, "StatusBar", "biaodian", skin.status.biaodian);
  ReadStatusButton(ini, "StatusBar", "quan_ban", skin.status.quan_ban);
  ReadStatusButton(ini, "StatusBar", "fan_jian", skin.status.fan_jian);
  ReadStatusButton(ini, "StatusBar", "menu", skin.status.menu);

  NormalizeSkin(skin);
  ApplySkinDefaults(skin);
  return skin;
}

Skin ParseSkinIni(const char* data, size_t size) {
  const std::wstring text = DecodeIniText(data, size);
  return BuildSkinFromIni(ParseIni(text));
}

}  // namespace ssf
}  // namespace weasel
