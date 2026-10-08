// Unit tests for the Sogou SSF compatibility layer.
//
// A self-contained console harness rather than a framework: the layer has no
// dependencies worth pulling a test runner in for, and this keeps the build to
// plain cl.exe + link.exe (see build-ssf-tests.ps1).
//
// Covers the parser, the colour byte order, the encoding chain, the layout
// engine and the 9-slice compositor -- i.e. everything that can be checked
// without a window station.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ssf/SsfImage.h"
#include "ssf/SsfIniParser.h"
#include "ssf/SsfLayout.h"
#include "ssf/SsfSkin.h"
#include "ssf/SsfVisibility.h"

using namespace weasel::ssf;

namespace {

int g_checks = 0;
int g_failures = 0;
const char* g_case = "";

void Check(bool ok, const char* what) {
  ++g_checks;
  if (!ok) {
    ++g_failures;
    std::printf("  FAIL [%s] %s\n", g_case, what);
  }
}

void CheckEq(long long got, long long want, const char* what) {
  ++g_checks;
  if (got != want) {
    ++g_failures;
    std::printf("  FAIL [%s] %s: got %lld, want %lld\n", g_case, what, got,
                want);
  }
}

void CheckStr(const std::string& got, const std::string& want,
              const char* what) {
  ++g_checks;
  if (got != want) {
    ++g_failures;
    std::printf("  FAIL [%s] %s: got '%s', want '%s'\n", g_case, what,
                got.c_str(), want.c_str());
  }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string Utf16leBytes(const std::wstring& text) {
  std::string out;
  out.push_back(static_cast<char>(0xFF));
  out.push_back(static_cast<char>(0xFE));
  for (wchar_t wc : text) {
    out.push_back(static_cast<char>(wc & 0xFF));
    out.push_back(static_cast<char>((wc >> 8) & 0xFF));
  }
  return out;
}

std::string Utf8Bytes(const std::wstring& text) {
  std::string out;
  for (wchar_t wc : text) {
    const unsigned cp = static_cast<unsigned>(wc);
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }
  return out;
}

// The Color-P skin.ini, reduced but structurally identical to the real file,
// including the awkward bits: a UTF-16LE-style key ordering, the misspelled
// "marge", a leading mode field in every layout triple, and lower-case/upper
// case section-name inconsistencies.
const char* kColorPIni =
    "[General]\n"
    "skin_id=487582\n"
    "skin_name=Color-P test\n"
    "skin_version=0.9\n"
    "skin_author=test\n"
    "\n"
    "[Display]\n"
    "font_size=14\n"
    "font_ch=SimSun\n"
    "font_en=Arial\n"
    "pinyin_color=0x3c3c3c\n"
    "zhongwen_first_color=0x3c3c3c\n"
    "zhongwen_color=0x6e6cff\n"
    "comphint_color=0x2f2f2f\n"
    "use_gdip=1\n"
    "aero=0\n"
    "glow=0\n"
    "LargeFontSupport=1\n"
    "\n"
    "[Scheme_H1]\n"
    "pic=skin2.png\n"
    "layout_horizontal=0,8,70\n"
    "layout_vertical=0,32,10\n"
    "pinyin_marge=12,4,6,48\n"
    "zhongwen_marge=4,5,6,5\n"
    "anchor=4,7\n"
    "\n"
    "[Scheme_H2]\n"
    "pinyin_pic=skin2_1.png\n"
    "pinyin_layout_horizontal=0,13,27\n"
    "pinyin_layout_vertical=0,8,7\n"
    "pinyin_marge=7,6,8,10\n"
    "zhongwen_pic=skin1_2.png\n"
    "zhongwen_layout_horizontal=0,12,27\n"
    "zhongwen_layout_vertical=0,8,10\n"
    "zhongwen_marge=10,9,8,8\n"
    "anchor=3,4\n"
    "\n"
    "[Scheme_V1]\n"
    "pic=skin2.png\n"
    "layout_horizontal=0,5,54\n"
    "layout_vertical=0,31,9\n"
    "pinyin_marge=11,3,6,52\n"
    "zhongwen_marge=6,7,7,4\n"
    "anchor=3,6\n"
    "\n"
    "[Scheme_V2]\n"
    "pinyin_pic=skin2_1.png\n"
    "pinyin_layout_horizontal=0,12,19\n"
    "pinyin_layout_vertical=0,9,10\n"
    "pinyin_marge=7,6,8,10\n"
    "zhongwen_pic=skin2_2.png\n"
    "zhongwen_layout_horizontal=0,12,19\n"
    "zhongwen_layout_vertical=0,9,8\n"
    "zhongwen_marge=9,8,8,32\n"
    "anchor=3,4\n"
    "\n"
    "[StatusBar]\n"
    "pic=skin1_2.png\n"
    "\n"
    "cn_en_display=1\n"
    "cn_en_pos=4,3\n"
    "cn_en=cn3.png,en3.png,a3.png\n"
    "cn_en_down=cn3.png,en3.png,a3.png\n"
    "cn_en_hover=cn2.png,en2.png,a2.png\n"
    "\n"
    "biaodian_display=1\n"
    "biaodian_pos=23,3\n"
    "biaodian=cn_biaodian3.png,en_biaodian3.png\n"
    "biaodian_down=cn_biaodian3.png,en_biaodian3.png\n"
    "biaodian_hover=cn_biaodian2.png,en_biaodian2.png\n"
    "\n"
    "quan_ban_display=1\n"
    "quan_ban_pos=37,3\n"
    "quan_ban=quan3.png,ban3.png\n"
    "quan_ban_down=quan3.png,ban3.png\n"
    "quan_ban_hover=quan2.png,ban2.png\n"
    "\n"
    "fan_jian_display=0\n"
    "fan_jian_pos=56,3\n"
    "fan_jian=jian3.png,fan3.png\n"
    "fan_jian_down=jian3.png,fan3.png\n"
    "fan_jian_hover=jian2.png,fan2.png\n"
    "\n"
    "menu_display=1\n"
    "menu_pos=73,3\n"
    "menu=menu3.png\n"
    "menu_down=menu3.png\n"
    "menu_hover=menu2.png\n";

// Minimal UTF-8 -> wide conversion for the test fixtures. The SSF layer has its
// own (in SsfImageLoader), but that one drags in GDI+, which the tests
// deliberately avoid.
std::wstring Utf8ToWideLoose(const std::string& s) {
  std::wstring out;
  size_t i = 0;
  while (i < s.size()) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
      out.push_back(static_cast<wchar_t>(c));
      ++i;
    } else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
      out.push_back(
          static_cast<wchar_t>(((c & 0x1F) << 6) | (s[i + 1] & 0x3F)));
      i += 2;
    } else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
      out.push_back(static_cast<wchar_t>(((c & 0x0F) << 12) |
                                         ((s[i + 1] & 0x3F) << 6) |
                                         (s[i + 2] & 0x3F)));
      i += 3;
    } else {
      out.push_back(static_cast<wchar_t>(c));
      ++i;
    }
  }
  return out;
}

// A deterministic measurer: every character is 8px wide, line height 20, ascent
// 15, except 'i' which is 3px. Predictable numbers make layout assertions exact.
class FakeMeasurer : public ITextMeasurer {
 public:
  Size Measure(const std::wstring& text, FontKind kind) const override {
    Size s;
    int width = 0;
    for (wchar_t c : text) width += (c == L'i' ? 3 : 8);
    s.cx = width;
    s.cy = LineHeight(kind);
    return s;
  }
  int LineHeight(FontKind) const override { return 20; }
  int Ascent(FontKind) const override { return 15; }
};

// A tiny image filled with one colour, for 9-slice tests.
SsfImage MakeImage(int w, int h, uint8_t r, uint8_t g, uint8_t b,
                   uint8_t a = 255) {
  SsfImage img;
  img.width = w;
  img.height = h;
  img.pixels.assign(static_cast<size_t>(w) * h * 4, 0);
  for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
    img.pixels[i * 4 + 0] = r;
    img.pixels[i * 4 + 1] = g;
    img.pixels[i * 4 + 2] = b;
    img.pixels[i * 4 + 3] = a;
  }
  return img;
}

// ===========================================================================
// Parser tests
// ===========================================================================

void TestIntListParsing() {
  g_case = "ParseIntList";
  std::vector<int> v;

  Check(ParseIntList("0,13,27", v), "parses a triple");
  CheckEq(v.size(), 3, "triple size");
  CheckEq(v[0], 0, "triple[0]");
  CheckEq(v[1], 13, "triple[1]");
  CheckEq(v[2], 27, "triple[2]");

  Check(ParseIntList(" 7 , 6 , 8 , 10 ", v), "tolerates whitespace");
  CheckEq(v.size(), 4, "quad size");
  CheckEq(v[3], 10, "quad[3]");

  Check(ParseIntList("1,2,", v), "tolerates a trailing comma");
  CheckEq(v.size(), 2, "trailing comma size");

  v.clear();
  Check(!ParseIntList("1,abc,3", v), "rejects a non-numeric element");
  CheckEq(v.size(), 0, "output untouched on failure");

  // Hex is accepted because `separator` mixes a colour with geometry.
  Check(ParseIntList("0xd8d8d8,66", v), "accepts hex elements");
  CheckEq(v[0], 0xd8d8d8, "hex value");
}

void TestPointAndMargin() {
  g_case = "ParsePoint/ParseMargin4";
  Point p;
  Check(ParsePoint("4,3", p), "point parses");
  CheckEq(p.x, 4, "point.x");
  CheckEq(p.y, 3, "point.y");
  Check(!ParsePoint("4", p), "rejects a 1-element point");

  Margin4 m;
  Check(ParseMargin4("7,6,8,10", m), "margin parses");
  // Established order is [top, bottom, left, right].
  CheckEq(m.top, 7, "margin.top");
  CheckEq(m.bottom, 6, "margin.bottom");
  CheckEq(m.left, 8, "margin.left");
  CheckEq(m.right, 10, "margin.right");
  Check(!ParseMargin4("1,2,3", m), "rejects a 3-element margin");
}

void TestSogouColor() {
  g_case = "ParseSogouColor";
  Color c;

  // The decisive case: 0x6e6cff is stored BGR, so it is RGB(255,108,110).
  Check(ParseSogouColor("0x6e6cff", c), "parses zhongwen_color");
  CheckEq(c.r, 255, "red channel");
  CheckEq(c.g, 108, "green channel");
  CheckEq(c.b, 110, "blue channel");
  CheckEq(c.a, 255, "opaque by default");

  Check(ParseSogouColor("0x3c3c3c", c), "parses pinyin_color");
  CheckEq(c.r, 0x3c, "grey red");
  CheckEq(c.g, 0x3c, "grey green");
  CheckEq(c.b, 0x3c, "grey blue");

  // Decimal form, as some skins write it.
  Check(ParseSogouColor("16777215", c), "parses decimal");
  CheckEq(c.r, 255, "decimal red");
  CheckEq(c.b, 255, "decimal blue");

  Check(ParseSogouColor("0xff0000", c, true), "parses with alpha");
  CheckEq(c.a, 255, "alpha channel");

  Check(!ParseSogouColor("", c), "rejects empty");
  Check(!ParseSogouColor("zzz", c), "rejects garbage");
}

void TestNineSliceParsing() {
  g_case = "ParseNineSlice";
  NineSlice ns;
  Check(ParseNineSliceH("0,13,27", ns), "horizontal triple");
  CheckEq(ns.left, 13, "left border");
  CheckEq(ns.right, 27, "right border");
  ns = NineSlice();
  Check(ParseNineSliceV("0,8,7", ns), "vertical triple");
  CheckEq(ns.top, 8, "top border");
  CheckEq(ns.bottom, 7, "bottom border");
}

void TestSkinParsing() {
  g_case = "BuildSkinFromIni";
  const std::string ini(kColorPIni);
  Skin skin = ParseSkinIni(ini.data(), ini.size());

  CheckStr(skin.skin_name, "Color-P test", "skin_name");
  CheckEq(skin.font_size, 14, "font_size");
  CheckStr(skin.font_ch, "SimSun", "font_ch");
  CheckStr(skin.font_en, "Arial", "font_en");

  CheckEq(skin.zhongwen_color.r, 255, "skin colour red");
  CheckEq(skin.zhongwen_color.g, 108, "skin colour green");
  CheckEq(skin.zhongwen_color.b, 110, "skin colour blue");

  Check(skin.use_gdip, "use_gdip parsed");
  Check(!skin.aero, "aero parsed");
  Check(skin.large_font_support, "LargeFontSupport parsed");

  // H1: single background.
  CheckStr(skin.h1.pic, "skin2.png", "H1 pic");
  Check(skin.h1.IsSingleBackground(), "H1 is single-background");
  CheckEq(skin.h1.ns_pinyin.left, 8, "H1 left border");
  CheckEq(skin.h1.ns_pinyin.right, 70, "H1 right border");
  CheckEq(skin.h1.ns_pinyin.top, 32, "H1 top border");
  CheckEq(skin.h1.ns_pinyin.bottom, 10, "H1 bottom border");
  CheckEq(skin.h1.pinyin_marge.top, 12, "H1 pinyin top");
  CheckEq(skin.h1.pinyin_marge.right, 48, "H1 pinyin right");
  Check(skin.h1.has_anchor, "H1 anchor present");
  CheckEq(skin.h1.anchor.x, 4, "H1 anchor x");
  CheckEq(skin.h1.anchor.y, 7, "H1 anchor y");

  // H2: split background.
  CheckStr(skin.h2.pinyin_pic, "skin2_1.png", "H2 pinyin_pic");
  CheckStr(skin.h2.zhongwen_pic, "skin1_2.png", "H2 zhongwen_pic");
  Check(skin.h2.IsSplitBackground(), "H2 is split-background");
  Check(!skin.h2.IsSingleBackground(), "H2 has no single pic");
  CheckEq(skin.h2.ns_pinyin.left, 13, "H2 pinyin left border");
  CheckEq(skin.h2.ns_pinyin.right, 27, "H2 pinyin right border");
  CheckEq(skin.h2.ns_zhongwen.left, 12, "H2 candidate left border");
  CheckEq(skin.h2.ns_zhongwen.right, 27, "H2 candidate right border");
  CheckEq(skin.h2.pinyin_marge.top, 7, "H2 pinyin top");
  CheckEq(skin.h2.pinyin_marge.bottom, 6, "H2 pinyin bottom");
  CheckEq(skin.h2.zhongwen_marge.top, 10, "H2 candidate top");
  CheckEq(skin.h2.zhongwen_marge.bottom, 9, "H2 candidate bottom");
  // gap = pinyin.bottom + candidate.top = 6 + 10
  CheckEq(skin.h2.gap, 0, "H2 margins must not add an external gap");

  // Insets must follow the [top, bottom, left, right] convention.
  CheckEq(skin.h2.pinyin_insets.left, 8, "H2 pinyin inset left");
  CheckEq(skin.h2.pinyin_insets.top, 7, "H2 pinyin inset top");
  CheckEq(skin.h2.pinyin_insets.right, 10, "H2 pinyin inset right");
  CheckEq(skin.h2.zhongwen_insets.left, 8, "H2 candidate inset left");
  CheckEq(skin.h2.zhongwen_insets.top, 10, "H2 candidate inset top");

  // V1 and V2.
  CheckStr(skin.v1.pic, "skin2.png", "V1 pic");
  CheckEq(skin.v1.ns_pinyin.left, 5, "V1 left border");
  CheckStr(skin.v2.zhongwen_pic, "skin2_2.png", "V2 zhongwen_pic");
  CheckEq(skin.v2.zhongwen_marge.right, 32, "V2 candidate right margin");

  // Status bar.
  CheckStr(skin.status.pic, "skin1_2.png", "status bar pic");
  Check(skin.status.cn_en.display, "cn_en shown");
  CheckEq(skin.status.cn_en.pos.x, 4, "cn_en pos.x");
  CheckEq(skin.status.cn_en.pos.y, 3, "cn_en pos.y");
  CheckEq(skin.status.cn_en.names.size(), 3, "cn_en has three states");
  CheckStr(skin.status.cn_en.names[2], "a3.png", "cn_en third state");
  CheckEq(skin.status.cn_en.names_hover.size(), 3, "cn_en hover has three");
  Check(skin.status.biaodian.display, "biaodian shown");
  CheckEq(skin.status.biaodian.names.size(), 2, "biaodian states");
  Check(skin.status.quan_ban.display, "quan_ban shown");
  Check(!skin.status.fan_jian.display, "fan_jian disabled in this fixture");
  Check(skin.status.menu.display, "menu shown");
  CheckEq(skin.status.menu.pos.x, 73, "menu pos.x");
  CheckStr(skin.status.menu.names_hover[0], "menu2.png", "menu hover art");
}

void TestSchemeSelection() {
  g_case = "Scheme selection";
  const std::string ini(kColorPIni);
  Skin skin = ParseSkinIni(ini.data(), ini.size());

  // Split-background skins must prefer H2/V2, because that is the only way to
  // reproduce the two-tone preedit/candidate look.
  CheckStr(skin.Horizontal(true).zhongwen_pic, "skin1_2.png",
           "horizontal prefers split");
  CheckStr(skin.Vertical(true).zhongwen_pic, "skin2_2.png",
           "vertical prefers split");
  // With splitting disabled the single-background scheme is used.
  CheckStr(skin.Horizontal(false).pic, "skin2.png", "horizontal single");
  CheckStr(skin.Vertical(false).pic, "skin2.png", "vertical single");
}

void TestEncoding() {
  g_case = "Encoding";

  // UTF-16LE with BOM -- what the real Color-P skin.ini uses.
  {
    const std::wstring text = L"[General]\nskin_name=ColorP\n";
    const std::string bytes = Utf16leBytes(text);
    const std::wstring decoded = DecodeIniText(bytes.data(), bytes.size());
    Skin skin = BuildSkinFromIni(ParseIni(decoded));
    CheckStr(skin.skin_name, "ColorP", "utf16 skin_name");
  }

  // UTF-16LE carrying real Chinese text: the point of the whole encoding chain
  // is that a font name survives intact rather than becoming mojibake.
  {
    const std::wstring text = L"[Display]\nfont_ch=\u6c49\u4eea\u7ec6\u4e2d\u5706\u7b80\n";
    const std::string bytes = Utf16leBytes(text);
    const std::wstring decoded = DecodeIniText(bytes.data(), bytes.size());
    Skin skin = BuildSkinFromIni(ParseIni(decoded));
    const std::wstring face = Utf8ToWideLoose(skin.font_ch);
    CheckEq(face.size(), static_cast<long long>(6), "utf16 CJK name length");
    CheckEq(static_cast<long long>(face[0]), 0x6C49, "utf16 CJK first char");
  }

  // UTF-8.
  {
    const std::wstring text = L"[General]\nskin_name=Color-P\n";
    const std::string bytes = Utf8Bytes(text);
    const std::wstring decoded = DecodeIniText(bytes.data(), bytes.size());
    Skin skin = BuildSkinFromIni(ParseIni(decoded));
    CheckStr(skin.skin_name, "Color-P", "utf8 skin_name");
  }

  // UTF-8 BOM must be stripped, otherwise the first key is corrupt.
  {
    std::string bytes = "\xEF\xBB\xBF[General]\nskin_name=WithBom\n";
    const std::wstring decoded = DecodeIniText(bytes.data(), bytes.size());
    Skin skin = BuildSkinFromIni(ParseIni(decoded));
    CheckStr(skin.skin_name, "WithBom", "utf8 BOM stripped");
  }

  // GBK: 0xD6 0xD0 0xCE 0xC4 is the GBK encoding of two Chinese characters.
  {
    std::string bytes = "[General]\nskin_name=";
    bytes.push_back(static_cast<char>(0xD6));
    bytes.push_back(static_cast<char>(0xD0));
    bytes.push_back(static_cast<char>(0xCE));
    bytes.push_back(static_cast<char>(0xC4));
    bytes.push_back('\n');
    const std::wstring decoded = DecodeIniText(bytes.data(), bytes.size());
    // The decoded name must not be empty and must not contain the raw bytes.
    Skin skin = BuildSkinFromIni(ParseIni(decoded));
    Check(!skin.skin_name.empty(), "gbk decoded non-empty");
    Check(skin.skin_name.size() >= 2, "gbk produced multi-byte characters");
  }
}

void TestMalformedIni() {
  g_case = "Malformed INI";

  // Empty input.
  {
    Skin skin = ParseSkinIni("", 0);
    Check(skin.Empty(), "empty input yields an empty skin");
    CheckEq(skin.font_size, 14, "default font size applied");
  }

  // Keys before any section are ignored; junk lines and comments are skipped.
  {
    const char* ini =
        "orphan=1\n"
        "; a comment\n"
        "# another\n"
        "[Display]\n"
        "font_size=notanumber\n"
        "\n"
        "[Scheme_H2]\n"
        "pinyin_pic=x.png\n"
        "layout_horizontal=garbage\n"
        "pinyin_marge=1,2\n";
    Skin skin = ParseSkinIni(ini, std::strlen(ini));
    CheckEq(skin.font_size, 14, "malformed font_size falls back");
    CheckStr(skin.h2.pinyin_pic, "x.png", "good key still parsed");
    CheckEq(skin.h2.ns_pinyin.left, 0, "malformed layout left at default");
    CheckEq(skin.h2.pinyin_marge.top, 0, "short marge ignored");
    // Defaults must still produce a usable inset.
    Check(skin.h2.pinyin_insets.left > 0, "default inset applied");
  }

  // A missing [Scheme_H2] must not break the skin.
  {
    const char* ini = "[Display]\nfont_size=16\n";
    Skin skin = ParseSkinIni(ini, std::strlen(ini));
    CheckEq(skin.font_size, 16, "font size from a minimal skin");
    Check(!skin.h2.IsSplitBackground(), "absent H2 stays empty");
    Check(skin.Horizontal().pic.empty(), "no pic anywhere");
  }
}

// ===========================================================================
// Layout tests
// ===========================================================================

LayoutInput MakeInput(const std::vector<std::string>& texts) {
  LayoutInput in;
  in.preedit = L"nihao";
  for (const std::string& t : texts) in.texts.push_back(Utf8ToWideLoose(t));
  in.labels.push_back(L"1.");
  for (size_t i = 1; i < texts.size(); ++i)
    in.labels.push_back(L"2.");
  in.highlighted = 0;
  return in;
}

void TestLayoutHorizontal() {
  g_case = "Layout horizontal";
  const std::string ini(kColorPIni);
  Skin skin = ParseSkinIni(ini.data(), ini.size());
  FakeMeasurer m;

  LayoutOptions opts;
  opts.scale = 1.0;
  opts.orientation = Orientation::Horizontal;
  opts.prefer_split_background = true;

  LayoutInput in;
  in.preedit = L"nihao";
  in.texts = {L"aa", L"bb"};
  in.labels = {L"1.", L"2."};
  in.highlighted = 0;

  SsfLayoutResult L = SsfLayoutEngine().Compute(skin, opts, in, m);

  // Pinyin strip: inset top (7) + line height (20) + inset bottom (6) = 33.
  CheckEq(L.pinyin_bg.Height(), 33, "pinyin strip height");
  CheckEq(L.pinyin_text.top, 7, "preedit text top");
  CheckEq(L.pinyin_text.left, 8, "preedit text left");
  CheckEq(L.pinyin_baseline, 22, "preedit baseline");
  Check(L.has_pinyin_area, "pinyin area present");

  // Two strips must not overlap and must be separated by the gap.
  CheckEq(L.candidate_bg.top, L.pinyin_bg.bottom + skin.h2.gap,
          "gap between strips");
  Check(L.candidate_bg.bottom <= L.window.cy, "candidate bg inside window");

  // Candidate boxes must be ordered, non-overlapping and inside the window.
  CheckEq(L.candidates.size(), static_cast<size_t>(2), "two candidates");
  for (size_t i = 0; i < L.candidates.size(); ++i) {
    const CandidateBox& b = L.candidates[i];
    Check(b.box.left >= 0 && b.box.right <= L.window.cx, "candidate inside width");
    Check(b.box.top >= 0 && b.box.bottom <= L.window.cy, "candidate inside height");
    Check(b.box.Width() > 0 && b.box.Height() > 0, "candidate non-degenerate");
    Check(b.text.left >= b.box.left, "text inside cell");
    Check(b.label.right <= b.text.left, "label before text");
    if (i > 0)
      Check(L.candidates[i - 1].box.right <= b.box.left, "cells do not overlap");
  }

  // The first candidate has no label of width 0 issues.
  Check(L.candidates[0].label.Width() > 0, "label has width");

  // Highlight covers the highlighted candidate's row.
  CheckEq(L.highlight.left, L.candidates[0].box.left, "highlight left");
  CheckEq(L.highlight.right, L.candidates[0].box.right, "highlight right");
  Check(L.highlight.Height() > 0, "highlight non-degenerate");

  // Hit testing must agree with the drawn boxes.
  CheckEq(SsfLayoutEngine::HitTest(
              L, L.candidates[0].box.left + 1, L.candidates[0].box.top + 1),
          0, "hit test candidate 0");
  CheckEq(SsfLayoutEngine::HitTest(
              L, L.candidates[1].box.left + 1, L.candidates[1].box.top + 1),
          1, "hit test candidate 1");
  CheckEq(SsfLayoutEngine::HitTest(L, L.window.cx + 10, L.window.cy + 10), -1,
          "hit test outside");
  // A point in the gap between strips belongs to no candidate.
  CheckEq(SsfLayoutEngine::HitTest(L, 5, L.pinyin_bg.bottom + 1), -1,
          "hit test in the gap");
}

void TestLayoutWidthGrowsWithContent() {
  g_case = "Dynamic width";
  const std::string ini(kColorPIni);
  Skin skin = ParseSkinIni(ini.data(), ini.size());
  FakeMeasurer m;

  LayoutOptions opts;
  opts.scale = 1.0;
  opts.prefer_split_background = true;
  // Pin the artwork floor so the comparison is about the text, not the bitmaps.
  opts.native_pinyin_bg = Size{40, 20};
  opts.native_candidate_bg = Size{40, 20};

  LayoutInput short_in;
  short_in.preedit = L"ni";
  short_in.texts = {L"a"};

  LayoutInput long_in;
  long_in.preedit = L"ni";
  long_in.texts = {L"a", L"bbbbbbbbbbbbbbbb", L"cccccccccccccccc"};

  SsfLayoutResult a = SsfLayoutEngine().Compute(skin, opts, short_in, m);
  SsfLayoutResult b = SsfLayoutEngine().Compute(skin, opts, long_in, m);

  Check(b.window.cx > a.window.cx, "window grows with more candidates");
  CheckEq(a.window.cy, b.window.cy, "height unchanged in horizontal mode");

  // A single very long candidate must also widen the window.
  LayoutInput huge_in;
  huge_in.preedit = L"ni";
  huge_in.texts = {L"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};
  SsfLayoutResult c = SsfLayoutEngine().Compute(skin, opts, huge_in, m);
  Check(c.window.cx > a.window.cx, "window grows with a long candidate");

  // A very short candidate list must not collapse below the artwork minimum.
  LayoutOptions floored = opts;
  floored.native_candidate_bg = Size{300, 20};
  floored.native_pinyin_bg = Size{300, 20};
  SsfLayoutResult d = SsfLayoutEngine().Compute(skin, floored, short_in, m);
  Check(d.window.cx >= 300, "window floored at the skin's native width");
}

void TestLayoutVertical() {
  g_case = "Layout vertical";
  const std::string ini(kColorPIni);
  Skin skin = ParseSkinIni(ini.data(), ini.size());
  FakeMeasurer m;

  LayoutOptions opts;
  opts.scale = 1.0;
  opts.orientation = Orientation::Vertical;
  opts.prefer_split_background = true;

  LayoutInput in;
  in.preedit = L"ni";
  in.texts = {L"aa", L"bb", L"cc"};

  SsfLayoutResult L = SsfLayoutEngine().Compute(skin, opts, in, m);

  CheckEq(L.candidates.size(), static_cast<size_t>(3), "three rows");
  for (size_t i = 1; i < L.candidates.size(); ++i) {
    Check(L.candidates[i].box.top >= L.candidates[i - 1].box.bottom,
          "rows stack downwards");
    // In vertical mode each row spans the full content width.
    CheckEq(L.candidates[i].box.Width(), L.window.cx, "row spans the window");
  }
  Check(L.window.cy > L.candidate_bg.Height(),
        "window grows taller than one strip for three rows");
}

void TestLayoutScale() {
  g_case = "DPI scale";
  const std::string ini(kColorPIni);
  Skin skin = ParseSkinIni(ini.data(), ini.size());
  FakeMeasurer m;

  LayoutInput in;
  in.preedit = L"ni";
  in.texts = {L"aa"};

  LayoutOptions one;
  one.scale = 1.0;
  LayoutOptions two;
  two.scale = 2.0;

  SsfLayoutResult a = SsfLayoutEngine().Compute(skin, one, in, m);
  SsfLayoutResult b = SsfLayoutEngine().Compute(skin, two, in, m);

  // Insets and the gap must scale, not just the text.
  CheckEq(b.pinyin_text.left, a.pinyin_text.left * 2, "inset scales");
  Check(b.window.cy > a.window.cy, "window grows with scale");
}

void TestStatusBarLayout() {
  g_case = "Status bar";
  const std::string ini(kColorPIni);
  Skin skin = ParseSkinIni(ini.data(), ini.size());
  FakeMeasurer m;

  LayoutOptions opts;
  opts.scale = 1.0;
  opts.status_icon_slot = Size{25, 25};

  LayoutInput in;
  in.preedit = L"ni";
  in.texts = {L"aa"};
  in.show_status_bar = true;
  in.chinese_mode = true;

  SsfLayoutResult L = SsfLayoutEngine().Compute(skin, opts, in, m);

  Check(!L.status_bar.Empty(), "status bar has an extent");
  Check(L.status_bar.top >= 0, "status bar positioned");
  Check(L.window.cy >= L.status_bar.bottom, "status bar inside the window");

  Check(L.icon_cn_en.visible, "cn_en icon visible");
  CheckStr(L.icon_cn_en.normal, "cn3.png", "cn_en normal art (chinese)");
  Check(L.icon_biaodian.visible, "biaodian icon visible");
  Check(L.icon_quan_ban.visible, "quan_ban icon visible");
  Check(!L.icon_fan_jian.visible, "fan_jian hidden when display=0");
  Check(L.icon_menu.visible, "menu icon visible");

  // Position comes from *_pos offset by the bar origin.
  CheckEq(L.icon_menu.box.left, L.status_bar.left + 73, "menu pos.x honoured");
  CheckEq(L.icon_menu.box.top, L.status_bar.top + 3, "menu pos.y honoured");

  // Hit testing must find the button that was drawn.
  const int hit = SsfLayoutEngine::HitTestStatusBar(
      L, L.icon_menu.box.left + 2, L.icon_menu.box.top + 2);
  CheckEq(hit, 4, "hit test finds the menu button");
  CheckEq(SsfLayoutEngine::HitTestStatusBar(L, -5, -5), -1, "hit test outside");

  // Switching state must switch the artwork.
  in.chinese_mode = false;
  SsfLayoutResult L2 = SsfLayoutEngine().Compute(skin, opts, in, m);
  CheckStr(L2.icon_cn_en.normal, "en3.png", "cn_en art switches to english");

  in.chinese_mode = true;
  in.hovered_button = 4;
  SsfLayoutResult L3 = SsfLayoutEngine().Compute(skin, opts, in, m);
  CheckStr(L3.icon_menu.normal, "menu2.png", "hover art used when hovered");
  // Hovering the menu must not disturb the other buttons' artwork.
  CheckStr(L3.icon_cn_en.normal, "cn3.png", "cn_en unaffected by menu hover");
  CheckStr(L3.icon_biaodian.normal, "cn_biaodian3.png",
           "biaodian unaffected by menu hover");

  // Hovering a status button must not move it.
  in.hovered_button = 1;
  SsfLayoutResult L3b = SsfLayoutEngine().Compute(skin, opts, in, m);
  CheckStr(L3b.icon_biaodian.normal, "cn_biaodian2.png", "biaodian hover art");
  CheckEq(L3b.icon_biaodian.box.left, L.icon_biaodian.box.left,
          "hovering does not move the button");

  in.hovered_button = -1;
  in.pressed_button = 4;
  SsfLayoutResult L4 = SsfLayoutEngine().Compute(skin, opts, in, m);
  // menu has no separate _down art in this fixture, so normal art is reused.
  CheckStr(L4.icon_menu.normal, "menu3.png", "down falls back to normal art");
}

void TestNoImagesLayoutStillSane() {
  g_case = "Layout without artwork";
  // A skin that parses but has no usable images must still lay out sanely:
  // this is what happens when a skin directory is incomplete.
  const char* ini =
      "[Display]\nfont_size=14\nfont_ch=SimSun\n"
      "[Scheme_H2]\npinyin_pic=missing.png\nzhongwen_pic=missing.png\n"
      "pinyin_layout_horizontal=0,4,4\npinyin_layout_vertical=0,3,3\n"
      "zhongwen_layout_horizontal=0,4,4\nzhongwen_layout_vertical=0,3,3\n"
      "pinyin_marge=4,4,4,4\nzhongwen_marge=4,4,4,4\n";
  Skin skin = ParseSkinIni(ini, std::strlen(ini));
  FakeMeasurer m;

  LayoutOptions opts;
  opts.scale = 1.0;
  LayoutInput in;
  in.preedit = L"ni";
  in.texts = {L"aa", L"bb"};

  SsfLayoutResult L = SsfLayoutEngine().Compute(skin, opts, in, m);
  Check(L.window.cx > 0, "window has a width");
  Check(L.window.cy > 0, "window has a height");
  CheckEq(L.candidates.size(), static_cast<size_t>(2), "candidates laid out");
  Check(L.highlight.Width() >= 0, "highlight not negative");
}

// ===========================================================================
// Image / 9-slice tests
// ===========================================================================

void TestNineSliceIdentity() {
  g_case = "9-slice identity";
  // Destination equal to source must reproduce the source exactly.
  SsfImage src = MakeImage(10, 10, 10, 20, 30);
  // Put a marker in each corner so a mis-mapping is visible.
  auto set = [&src](int x, int y, uint8_t r) {
    src.pixels[(static_cast<size_t>(y) * src.width + x) * 4 + 0] = r;
  };
  set(0, 0, 200);
  set(9, 0, 201);
  set(0, 9, 202);
  set(9, 9, 203);
  set(5, 5, 204);

  SsfImage dst;
  dst.width = 10;
  dst.height = 10;
  dst.pixels.assign(10 * 10 * 4, 0);

  NineSlice ns;
  ns.left = ns.right = ns.top = ns.bottom = 3;
  dst.BlitNineSlice(src, ns, 0, 0, 10, 10);

  auto get = [&dst](int x, int y) {
    return dst.pixels[(static_cast<size_t>(y) * dst.width + x) * 4 + 0];
  };
  CheckEq(get(0, 0), 200, "top-left marker preserved");
  CheckEq(get(9, 0), 201, "top-right marker preserved");
  CheckEq(get(0, 9), 202, "bottom-left marker preserved");
  CheckEq(get(9, 9), 203, "bottom-right marker preserved");
  CheckEq(get(5, 5), 204, "centre marker preserved");
}

void TestNineSliceStretch() {
  g_case = "9-slice stretch";
  // Corners must be copied 1:1 while the middle stretches.
  SsfImage src = MakeImage(12, 12, 0, 0, 0);
  // Left 4 columns red, right 4 columns blue, middle green.
  for (int y = 0; y < 12; ++y) {
    for (int x = 0; x < 12; ++x) {
      uint8_t* p = &src.pixels[(static_cast<size_t>(y) * 12 + x) * 4];
      if (x < 4) {
        p[0] = 255;
        p[1] = 0;
        p[2] = 0;
      } else if (x >= 8) {
        p[0] = 0;
        p[1] = 0;
        p[2] = 255;
      } else {
        p[0] = 0;
        p[1] = 255;
        p[2] = 0;
      }
    }
  }

  SsfImage dst;
  dst.width = 40;
  dst.height = 12;
  dst.pixels.assign(static_cast<size_t>(40) * 12 * 4, 0);

  NineSlice ns;
  ns.left = 4;
  ns.right = 4;
  ns.top = 0;
  ns.bottom = 0;
  dst.BlitNineSlice(src, ns, 0, 0, 40, 12);

  auto px = [&dst](int x, int y) {
    return &dst.pixels[(static_cast<size_t>(y) * dst.width + x) * 4];
  };
  // Left 4 columns still red, right 4 still blue.
  CheckEq(px(0, 5)[0], 255, "left corner region red");
  CheckEq(px(3, 5)[0], 255, "left border ends red");
  CheckEq(px(39, 5)[2], 255, "right border blue");
  CheckEq(px(36, 5)[2], 255, "right border starts blue");
  // Middle is green, i.e. stretched rather than tiled with a corner colour.
  CheckEq(px(20, 5)[1], 255, "middle stretched green");
  CheckEq(px(20, 5)[0], 0, "middle has no red bleed");
}

void TestNineSliceDegenerate() {
  g_case = "9-slice degenerate inputs";
  SsfImage src = MakeImage(8, 8, 100, 100, 100);
  SsfImage dst;
  dst.width = 20;
  dst.height = 20;
  dst.pixels.assign(20 * 20 * 4, 0);

  // Borders larger than the destination: must not produce negative rectangles
  // or read outside the source.
  NineSlice huge;
  huge.left = huge.right = huge.top = huge.bottom = 1000;
  dst.BlitNineSlice(src, huge, 0, 0, 20, 20);
  Check(true, "survived borders larger than the destination");

  // Borders larger than the source.
  NineSlice over;
  over.left = over.right = over.top = over.bottom = 50;
  dst.BlitNineSlice(src, over, 0, 0, 20, 20);
  Check(true, "survived borders larger than the source");

  // Zero-size destination.
  dst.BlitNineSlice(src, over, 0, 0, 0, 0);
  Check(true, "survived a zero-size destination");

  // Negative borders.
  NineSlice neg;
  neg.left = neg.right = neg.top = neg.bottom = -5;
  dst.BlitNineSlice(src, neg, 0, 0, 20, 20);
  Check(true, "survived negative borders");

  // Destination much larger than the source (heavy stretch).
  dst.BlitNineSlice(src, NineSlice(), 0, 0, 2000, 3);
  Check(true, "survived a 100x horizontal stretch");

  // Destination offset so it overhangs both edges.
  dst.BlitNineSlice(src, huge, -5, -5, 30, 30);
  Check(true, "survived a negative destination offset");
}

void TestBlendAndAlpha() {
  g_case = "Compositing";
  SsfImage dst = MakeImage(4, 4, 0, 0, 0, 0);  // fully transparent
  SsfImage src = MakeImage(2, 2, 255, 0, 0, 255);
  dst.Blend(src, 1, 1);

  auto px = [&dst](int x, int y) {
    return &dst.pixels[(static_cast<size_t>(y) * dst.width + x) * 4];
  };
  CheckEq(px(0, 0)[3], 0, "untouched pixel stays transparent");
  CheckEq(px(1, 1)[3], 255, "blended pixel opaque");
  CheckEq(px(1, 1)[0], 255, "blended pixel red");
  CheckEq(px(2, 2)[3], 255, "blend covers the full source rect");

  // Blending off-canvas must be clipped, not crash.
  dst.Blend(src, -10, -10);
  dst.Blend(src, 100, 100);
  Check(true, "clipped blends survived");

  // 50% source over opaque black should give a mid grey.
  SsfImage dst2 = MakeImage(1, 1, 0, 0, 0, 255);
  SsfImage half = MakeImage(1, 1, 255, 255, 255, 128);
  dst2.Blend(half, 0, 0);
  CheckEq(dst2.pixels[0], 128, "50% white over black is mid grey");
  CheckEq(dst2.pixels[3], 255, "alpha stays opaque");
}

void TestTextAlphaRepair() {
  g_case = "Alpha repair";
  // Simulate what GDI does: zero the alpha of pixels it touched.
  SsfImage img = MakeImage(10, 10, 200, 100, 50, 255);
  for (int y = 3; y < 6; ++y) {
    for (int x = 3; x < 6; ++x) {
      img.pixels[(static_cast<size_t>(y) * 10 + x) * 4 + 3] = 0;
    }
  }
  img.RepairTextAlpha(2, 2, 5, 5);

  auto alpha = [&img](int x, int y) {
    return img.pixels[(static_cast<size_t>(y) * 10 + x) * 4 + 3];
  };
  CheckEq(alpha(4, 4), 255, "clobbered alpha repaired");
  CheckEq(alpha(8, 8), 255, "pixels outside the rect untouched (already 255)");

  // A genuinely transparent pixel outside the repair rect must stay transparent,
  // otherwise the rounded corners would fill in.
  SsfImage img2 = MakeImage(10, 10, 0, 0, 0, 255);
  img2.pixels[0 * 4 + 3] = 0;  // (0,0) transparent
  img2.RepairTextAlpha(5, 5, 3, 3);
  CheckEq(img2.pixels[0 * 4 + 3], 0, "transparent pixel outside rect preserved");
}

void TestPremultiply() {
  g_case = "Premultiplied BGRA export";
  SsfImage img = MakeImage(2, 1, 255, 128, 64, 128);
  std::vector<uint8_t> out;
  img.ToPremultipliedBGRA(out);
  CheckEq(out.size(), static_cast<size_t>(8), "output size");
  // BGRA order, premultiplied by 128/255.
  CheckEq(out[0], 32, "blue premultiplied");
  CheckEq(out[1], 64, "green premultiplied");
  CheckEq(out[2], 128, "red premultiplied");
  CheckEq(out[3], 128, "alpha preserved");

  // Fully transparent pixels must be all zero, otherwise a layered window shows
  // a dark fringe.
  SsfImage clear = MakeImage(1, 1, 255, 255, 255, 0);
  std::vector<uint8_t> out2;
  clear.ToPremultipliedBGRA(out2);
  CheckEq(out2[0], 0, "transparent pixel has zero blue");
  CheckEq(out2[3], 0, "transparent pixel has zero alpha");
}

void TestNormalizeDefaults() {
  g_case = "Normalisation and defaults";

  // A scheme with no marge at all must still receive usable insets.
  Scheme s;
  NormalizeScheme(s);
  Skin skin;
  skin.h2 = s;
  ApplySkinDefaults(skin);
  Check(skin.h2.pinyin_insets.left > 0, "default pinyin inset");
  Check(skin.h2.zhongwen_insets.left > 0, "default candidate inset");
  CheckEq(skin.h2.gap, 0, "default gap remains zero after defaults");

  // Outrageous values must be clamped.
  Skin s2;
  s2.font_size = 10000;
  ApplySkinDefaults(s2);
  Check(s2.font_size <= 96, "font size clamped");
  s2.font_size = -5;
  ApplySkinDefaults(s2);
  Check(s2.font_size >= 4, "negative font size clamped");

  Scheme neg;
  neg.pinyin_marge.top = -10;
  neg.pinyin_marge.left = -20;
  neg.ns_pinyin.left = -3;
  NormalizeScheme(neg);
  Skin s3;
  s3.h2 = neg;
  ApplySkinDefaults(s3);
  Check(s3.h2.pinyin_insets.left >= 0, "negative inset clamped");
  Check(s3.h2.pinyin_insets.top >= 0, "negative inset top clamped");
  Check(s3.h2.ns_pinyin.left >= 0, "negative border clamped");

  // A skin with transparent colours must get legible fallbacks.
  Skin s4;
  ApplySkinDefaults(s4);
  CheckEq(s4.pinyin_color.a, 255, "pinyin colour made opaque");
  CheckEq(s4.zhongwen_color.a, 255, "candidate colour made opaque");
  CheckEq(s4.zhongwen_first_color.a, 255, "first-candidate colour made opaque");
}

}  // namespace

int wmain() {
  g_case = "empty candidate visibility";
  Check(SuppressEmptyWindow(true, true, false), "literal keypad commit hides empty skin");
  Check(!SuppressEmptyWindow(true, true, true), "explicit CN/EN tip remains visible");
  Check(!SuppressEmptyWindow(true, false, false), "pinyin or candidate content remains visible");
  Check(!SuppressEmptyWindow(true, false, true), "content survives a mode event");
  Check(!SuppressEmptyWindow(false, true, false), "stock renderer behavior unchanged");
  std::printf("SSF compatibility layer tests\n");
  std::printf("=============================\n");

  TestIntListParsing();
  TestPointAndMargin();
  TestSogouColor();
  TestNineSliceParsing();
  TestSkinParsing();
  TestSchemeSelection();
  TestEncoding();
  TestMalformedIni();

  TestLayoutHorizontal();
  TestLayoutWidthGrowsWithContent();
  TestLayoutVertical();
  TestLayoutScale();
  TestStatusBarLayout();
  TestNoImagesLayoutStillSane();

  TestNineSliceIdentity();
  TestNineSliceStretch();
  TestNineSliceDegenerate();
  TestBlendAndAlpha();
  TestTextAlphaRepair();
  TestPremultiply();
  TestNormalizeDefaults();

  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  if (g_failures) {
    std::printf("FAILED\n");
    return 1;
  }
  std::printf("PASSED\n");
  return 0;
}
