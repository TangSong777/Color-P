// skin.ini parsing for the Sogou SSF compatibility layer.
//
// Deliberately dependency-free: it does not use Windows APIs, so it can be
// compiled and exercised by a console test harness as well as by WeaselUI.

#pragma once

#include <map>
#include <string>
#include <vector>

#include "SsfSkin.h"

namespace weasel {
namespace ssf {

// ---------------------------------------------------------------------------
// Text decoding
// ---------------------------------------------------------------------------

// Decode raw skin.ini bytes in the encodings real skins use, in this order:
//   1. UTF-16LE with BOM      (FF FE)   <- what the bundled Color-P skin uses
//   2. UTF-16BE with BOM      (FE FF)
//   3. UTF-8 with BOM         (EF BB BF)
//   4. strict UTF-8
//   5. GBK / CP936            (ANSI fallback)
//
// The fallback chain matters: skin_name / font_ch / font_en contain Chinese
// text that must not become mojibake, and a wrong guess silently produces a
// garbage font name that then fails to resolve at draw time.
std::wstring DecodeIniText(const char* data, size_t size);

// ---------------------------------------------------------------------------
// INI structure
// ---------------------------------------------------------------------------

// Section name -> key -> value. Section and key names are stored lower-cased,
// because skin.ini authors are inconsistent about case. Values keep their
// original case (file names and font names are case-sensitive to Windows in
// practice, and font names definitely are).
using IniSection = std::map<std::string, std::string>;
using IniFile = std::map<std::string, IniSection>;

// Parse already-decoded text. Tolerates:
//   * CRLF and LF line endings
//   * leading UTF-8 BOM remnants
//   * ';' and '#' comments, whole-line and trailing
//   * whitespace around section names, keys and values
//   * keys with no '=' (stored with value "1")
//   * a stray key appearing before any section (ignored)
IniFile ParseIni(const std::wstring& text);

// Typed accessors. All are case-insensitive on the names and return `def` when
// the section, the key, or a valid value is missing.
std::string IniGetString(const IniFile& ini,
                         const std::string& section,
                         const std::string& key,
                         const std::string& def = std::string());
int IniGetInt(const IniFile& ini,
              const std::string& section,
              const std::string& key,
              int def = 0);
bool IniGetBool(const IniFile& ini,
                const std::string& section,
                const std::string& key,
                bool def = false);
// Comma-separated integer list. Also accepts the "0x" prefix on each element.
// Returns an empty vector when the key is absent.
std::vector<int> IniGetIntList(const IniFile& ini,
                               const std::string& section,
                               const std::string& key);

// ---------------------------------------------------------------------------
// Value parsers (exposed separately so they can be unit-tested directly)
// ---------------------------------------------------------------------------

// "0,13,27" -> {0, 13, 27}; also accepts whitespace and a trailing comma.
// Returns false and leaves `out` untouched when any element is not an integer.
bool ParseIntList(const std::string& value, std::vector<int>& out);

// "4,3" -> Point{4, 3}
bool ParsePoint(const std::string& value, Point& out);

// Four comma-separated integers in skin.ini's own order:
//     [top, bottom, left, right]
// Returns false unless exactly four valid integers are present.
bool ParseMargin4(const std::string& value, Margin4& out);

// A Sogou colour. skin.ini writes them as `0xRRGGBB` but stores the bytes in
// BGR order, so `0x6e6cff` is actually RGB(255,108,110) -- verified against the
// pixels of the bundled Color-P skin (see docs/ssf-skin.md §2.3).
//
// `with_alpha` selects whether a leading alpha byte is expected:
//   false -> value is 0xRRGGBB, result is fully opaque
//   true  -> value is 0xAARRGGBB
bool ParseSogouColor(const std::string& value, Color& out, bool with_alpha = false);

// Parse "0,13,27" into a NineSlice using the leading field as the unverified
// mode. Requires at least three integers; extra ones are ignored.
bool ParseNineSliceH(const std::string& value, NineSlice& out);
bool ParseNineSliceV(const std::string& value, NineSlice& out);

// ---------------------------------------------------------------------------
// Skin assembly
// ---------------------------------------------------------------------------

// Build a Skin from a parsed IniFile. Never fails: a missing or malformed
// section yields defaults, and ApplySkinDefaults() is applied at the end.
Skin BuildSkinFromIni(const IniFile& ini);

// Convenience: decode bytes and build the skin in one call.
Skin ParseSkinIni(const char* data, size_t size);

}  // namespace ssf
}  // namespace weasel
