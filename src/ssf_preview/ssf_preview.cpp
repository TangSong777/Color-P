// ssf_preview -- offscreen renderer for the Sogou SSF compatibility layer.
//
// WHY THIS EXISTS
// ---------------
// Tuning a bitmap skin to pixel accuracy through the IME itself is painful:
// every adjustment needs a redeploy, a running WeaselServer, an editable host
// application and a screenshot. This tool drives exactly the same
// SsfIniParser + SsfLayoutEngine + SsfRenderer code paths that WeaselPanel uses
// and writes the result straight to a PNG, so geometry and colour can be
// compared against a Sogou reference screenshot in a tight loop.
//
// It deliberately links ONLY the platform-free SSF sources plus GDI+/GDI, so it
// builds without ATL, WTL, Boost, WinSparkle or librime.
//
// Usage:
//   ssf_preview <skin-dir> [options]
//     --scheme H1|H2|V1|V2      default: H2 when the skin provides it, else H1
//     --preedit "ni'hao"        pinyin / composition string
//     --candidates "a|b|c"      '|' separated candidate list
//     --labels "1.|2.|3."       '|' separated labels (default 1. 2. 3.)
//     --comments "x|y|z"        '|' separated comments
//     --highlight N             highlighted candidate index (default 0)
//     --scale F                 whole-pipeline scale (default 1.0)
//     --out preview.png         output file
//     --status                  draw the status bar (default on)
//     --no-status               hide the status bar
//     --inspect                 print parsed skin parameters and image info only
//     --debug                   print the computed layout rectangles

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>

#include <windows.h>
// gdiplus.h needs COM's IStream/IStorage declared first; without ATL in the
// include path that ordering has to be explicit.
#include <objidl.h>
#include <gdiplus.h>

#include "ssf/SsfImageLoader.h"
#include "ssf/SsfIniParser.h"
#include "ssf/SsfLayout.h"
#include "ssf/SsfRenderer.h"
#include "ssf/SsfSkin.h"

using namespace weasel::ssf;

namespace {

// ---------------------------------------------------------------------------
// Text measurement through GDI.
//
// WeaselPanel uses DirectWrite. GDI is used here so the tool has no extra
// dependency, which means advance widths can differ by a pixel from the live
// IME. For skin geometry work (which is what this tool is for) that is
// acceptable, and it is called out in docs/ssf-skin.md. The ascent is read from
// TEXTMETRIC so the baseline lands in the same place as a DirectWrite run.
// ---------------------------------------------------------------------------
class GdiMeasurer : public ITextMeasurer {
 public:
  GdiMeasurer(const Skin& skin, double scale) : skin_(skin), scale_(scale) {
    hdc_ = ::CreateCompatibleDC(nullptr);
    ::SetBkMode(hdc_, TRANSPARENT);

    const double dpi_scale = scale_ > 0 ? scale_ : 1.0;
    const int pixel_height = (std::max)(
        1, static_cast<int>(skin_.font_size * dpi_scale + 0.5));
    // SSF font_size is expressed in skin pixels, so it maps directly to the
    // GDI logical height at 96 DPI instead of being converted from points.
    const int height = -pixel_height;

    for (int i = 0; i < 4; ++i) {
      fonts_[i] = ::CreateFontW(
          height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
          OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
          DEFAULT_PITCH | FF_DONTCARE, FaceFor(i).c_str());
      if (fonts_[i] == nullptr) continue;
      HGDIOBJ old = ::SelectObject(hdc_, fonts_[i]);
      TEXTMETRICW tm = {};
      if (::GetTextMetricsW(hdc_, &tm)) {
        ascent_[i] = tm.tmAscent;
        line_height_[i] = tm.tmHeight + tm.tmExternalLeading;
      }
      ::SelectObject(hdc_, old);
      if (line_height_[i] <= 0) line_height_[i] = height;
    }
  }

  ~GdiMeasurer() {
    for (HFONT f : fonts_) {
      if (f) ::DeleteObject(f);
    }
    if (hdc_) ::DeleteDC(hdc_);
  }

  Size Measure(const std::wstring& text, FontKind kind) const override {
    Size out;
    if (text.empty() || hdc_ == nullptr) return out;
    HGDIOBJ old = ::SelectObject(hdc_, fonts_[static_cast<int>(kind) & 3]);
    SIZE sz = {};
    if (::GetTextExtentPoint32W(hdc_, text.c_str(),
                                static_cast<int>(text.size()), &sz)) {
      out.cx = sz.cx;
      // Report the full line box, not the glyph ink height: the layout engine
      // reserves vertical space per line, and matching WeaselPanel here is what
      // keeps the pinyin strip the same height in both.
      out.cy = line_height_[static_cast<int>(kind) & 3];
    }
    ::SelectObject(hdc_, old);
    return out;
  }

  int LineHeight(FontKind kind) const override {
    return line_height_[static_cast<int>(kind) & 3];
  }

  int Ascent(FontKind kind) const override {
    return ascent_[static_cast<int>(kind) & 3];
  }

 private:
  std::wstring FaceFor(int kind) const {
    // Same precedence as WeaselPanel: the skin's Chinese face, then its English
    // face, then a CJK-capable fallback so the preview still renders on a
    // machine that lacks the skin's font.
    std::wstring face = Utf8ToWide((kind == PINYIN || kind == LABEL) ? skin_.font_en : skin_.font_ch);
    if (face.empty()) face = Utf8ToWide(skin_.font_en);
    if (face.empty()) face = L"Microsoft YaHei";
    (void)kind;
    return face;
  }

  const Skin& skin_;
  double scale_;
  HDC hdc_ = nullptr;
  HFONT fonts_[4] = {nullptr, nullptr, nullptr, nullptr};
  int ascent_[4] = {0, 0, 0, 0};
  int line_height_[4] = {0, 0, 0, 0};
};

// ---------------------------------------------------------------------------
// PNG encode through GDI+
// ---------------------------------------------------------------------------
bool SavePng(const SsfImage& image, const std::wstring& path) {
  if (!image.Valid()) return false;

  // GDI+ wants non-premultiplied 32bpp ARGB with byte order B,G,R,A.
  std::vector<uint8_t> bgra(static_cast<size_t>(image.width) * image.height * 4);
  for (size_t i = 0; i < static_cast<size_t>(image.width) * image.height; ++i) {
    bgra[i * 4 + 0] = image.pixels[i * 4 + 2];  // B
    bgra[i * 4 + 1] = image.pixels[i * 4 + 1];  // G
    bgra[i * 4 + 2] = image.pixels[i * 4 + 0];  // R
    bgra[i * 4 + 3] = image.pixels[i * 4 + 3];  // A
  }

  Gdiplus::Bitmap bmp(image.width, image.height, image.width * 4,
                      PixelFormat32bppARGB, bgra.data());
  if (bmp.GetLastStatus() != Gdiplus::Ok) return false;

  CLSID clsid;
  if (::CLSIDFromString(L"{557cf406-1a04-11d3-9a73-0000f81ef32e}", &clsid) !=
      S_OK)
    return false;  // PNG encoder
  return bmp.Save(path.c_str(), &clsid, nullptr) == Gdiplus::Ok;
}

std::vector<std::wstring> SplitBar(const std::string& s) {
  std::vector<std::wstring> out;
  std::string cur;
  for (char c : s) {
    if (c == '|') {
      out.push_back(Utf8ToWide(cur));
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) out.push_back(Utf8ToWide(cur));
  return out;
}

void PrintUsage() {
  std::printf(
      "ssf_preview <skin-dir> [--scheme H1|H2|V1|V2] [--preedit TEXT]\n"
      "            [--candidates \"a|b|c\"] [--labels \"1.|2.\"]\n"
      "            [--comments \"x|y\"] [--highlight N] [--scale F]\n"
      "            [--out FILE] [--status|--no-status]\n"
      "            [--inspect] [--debug]\n");
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  // Non-ASCII skin names and candidate text must survive to the console.
  ::SetConsoleOutputCP(CP_UTF8);
  ::SetConsoleCP(CP_UTF8);

  if (argc < 2) {
    PrintUsage();
    return 2;
  }

  std::wstring skin_dir = argv[1];
  std::string scheme_id = "auto";
  std::string preedit = "ni'hao'ya'wo'shi'xiao'ke'ai'ya";
  std::string candidates =
      "\xe4\xbd\xa0\xe5\xa5\xbd\xe5\x91\x80\xe6\x88\x91\xe6\x98\xaf\xe5\xb0\x8f"
      "\xe5\x8f\xaf\xe7\x88\xb1\xe5\x91\x80|"
      "\xe4\xbd\xa0\xe5\xa5\xbd\xe5\x91\x80\xe6\x88\x91\xe6\x98\xaf\xe5\xb0\x8f"
      "\xe5\x8f\xaf\xe7\x88\xb1\xe5\x90\x96|"
      "\xe4\xbd\xa0\xe5\xa5\xbd\xe5\x91\x80\xe6\x88\x91\xe6\x98\xaf\xe5\xb0\x8f"
      "\xe5\x8f\xaf\xe7\x88\xb1\xe9\xb8\xad|"
      "\xe4\xbd\xa0\xe5\xa5\xbd\xe5\x91\x80|"
      "\xe4\xbd\xa0\xe5\xa5\xbd";
  std::string labels = "1.|2.|3.|4.|5.";
  std::string comments;
  int highlight = 0;
  double scale = 1.0;
  std::wstring out_path = L"preview.png";
  bool show_status = true;
  bool inspect_only = false;
  bool debug = false;

  for (int i = 2; i < argc; ++i) {
    std::wstring a = argv[i];
    auto next = [&](const wchar_t* name) -> std::wstring {
      if (i + 1 >= argc) {
        std::fwprintf(stderr, L"missing value for %ls\n", name);
        return std::wstring();
      }
      return argv[++i];
    };
    auto narrow = [](const std::wstring& w) {
      // UTF-8, not a byte truncation: argument values can carry non-ASCII text
      // (Chinese sample candidates, a skin name) and a naive cast would corrupt
      // them.
      if (w.empty()) return std::string();
      const int need = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(),
                                             static_cast<int>(w.size()),
                                             nullptr, 0, nullptr, nullptr);
      if (need <= 0) return std::string();
      std::string s(static_cast<size_t>(need), '\0');
      ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(),
                            static_cast<int>(w.size()), &s[0], need, nullptr,
                            nullptr);
      return s;
    };
    if (a == L"--scheme") {
      scheme_id = narrow(next(L"--scheme"));
    } else if (a == L"--preedit") {
      preedit = narrow(next(L"--preedit"));
    } else if (a == L"--candidates") {
      candidates = narrow(next(L"--candidates"));
    } else if (a == L"--labels") {
      labels = narrow(next(L"--labels"));
    } else if (a == L"--comments") {
      comments = narrow(next(L"--comments"));
    } else if (a == L"--highlight") {
      highlight = _wtoi(next(L"--highlight").c_str());
    } else if (a == L"--scale") {
      scale = _wtof(next(L"--scale").c_str());
    } else if (a == L"--out") {
      out_path = next(L"--out");
    } else if (a == L"--status") {
      show_status = true;
    } else if (a == L"--no-status") {
      show_status = false;
    } else if (a == L"--inspect") {
      inspect_only = true;
    } else if (a == L"--debug") {
      debug = true;
    } else {
      std::fwprintf(stderr, L"unknown option: %ls\n", a.c_str());
      PrintUsage();
      return 2;
    }
  }

  Gdiplus::GdiplusStartupInput gdiplus_input;
  ULONG_PTR gdiplus_token = 0;
  if (Gdiplus::GdiplusStartup(&gdiplus_token, &gdiplus_input, nullptr) !=
      Gdiplus::Ok) {
    std::fprintf(stderr, "GDI+ init failed\n");
    return 1;
  }

  int exit_code = 0;
  {
    // 1. Load skin.ini.
    const std::wstring ini_path = skin_dir + L"\\skin.ini";
    HANDLE h = ::CreateFileW(ini_path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                             nullptr);
    if (h == INVALID_HANDLE_VALUE) {
      std::fwprintf(stderr, L"cannot open %ls\n", ini_path.c_str());
      Gdiplus::GdiplusShutdown(gdiplus_token);
      return 1;
    }
    LARGE_INTEGER size;
    ::GetFileSizeEx(h, &size);
    std::vector<char> raw(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    ::ReadFile(h, raw.data(), static_cast<DWORD>(raw.size()), &read, nullptr);
    ::CloseHandle(h);

    Skin skin = ParseSkinIni(raw.data(), read);

    std::printf("skin_name      : %s\n", skin.skin_name.c_str());
    std::printf("font_size      : %d\n", skin.font_size);
    std::printf("font_ch        : %s\n", skin.font_ch.c_str());
    std::printf("font_en        : %s\n", skin.font_en.c_str());
    std::printf("pinyin_color   : #%02X%02X%02X (a=%u)\n", skin.pinyin_color.r,
                skin.pinyin_color.g, skin.pinyin_color.b, skin.pinyin_color.a);
    std::printf("first_cand_col : #%02X%02X%02X\n",
                skin.zhongwen_first_color.r, skin.zhongwen_first_color.g,
                skin.zhongwen_first_color.b);
    std::printf("other_cand_col : #%02X%02X%02X\n", skin.zhongwen_color.r,
                skin.zhongwen_color.g, skin.zhongwen_color.b);

    auto dump_scheme = [](const char* name, const Scheme& s) {
      if (s.pic.empty() && s.pinyin_pic.empty() && s.zhongwen_pic.empty())
        return;
      std::printf("[%s] pic=%s pinyin_pic=%s zhongwen_pic=%s\n", name,
                  s.pic.c_str(), s.pinyin_pic.c_str(), s.zhongwen_pic.c_str());
      std::printf("  ns_pinyin  l=%d r=%d t=%d b=%d (mode %d/%d)\n",
                  s.ns_pinyin.left, s.ns_pinyin.right, s.ns_pinyin.top,
                  s.ns_pinyin.bottom, s.ns_pinyin.mode_x, s.ns_pinyin.mode_y);
      std::printf("  ns_zhongwen l=%d r=%d t=%d b=%d\n", s.ns_zhongwen.left,
                  s.ns_zhongwen.right, s.ns_zhongwen.top, s.ns_zhongwen.bottom);
      std::printf("  pinyin_marge=%d,%d,%d,%d  zhongwen_marge=%d,%d,%d,%d\n",
                  s.pinyin_marge.top, s.pinyin_marge.bottom, s.pinyin_marge.left,
                  s.pinyin_marge.right, s.zhongwen_marge.top,
                  s.zhongwen_marge.bottom, s.zhongwen_marge.left,
                  s.zhongwen_marge.right);
      std::printf("  insets pinyin=%d,%d,%d,%d zhongwen=%d,%d,%d,%d gap=%d\n",
                  s.pinyin_insets.left, s.pinyin_insets.top,
                  s.pinyin_insets.right, s.pinyin_insets.bottom,
                  s.zhongwen_insets.left, s.zhongwen_insets.top,
                  s.zhongwen_insets.right, s.zhongwen_insets.bottom, s.gap);
      if (s.has_anchor)
        std::printf("  anchor=%d,%d  <-- semantics UNVERIFIED, unused\n",
                    s.anchor.x, s.anchor.y);
    };
    dump_scheme("Scheme_H1", skin.h1);
    dump_scheme("Scheme_H2", skin.h2);
    dump_scheme("Scheme_V1", skin.v1);
    dump_scheme("Scheme_V2", skin.v2);
    if (skin.status.AnyVisible()) {
      std::printf("[StatusBar] pic=%s cn_en=%d biaodian=%d quan_ban=%d "
                  "fan_jian=%d menu=%d\n",
                  skin.status.pic.c_str(), skin.status.cn_en.display ? 1 : 0,
                  skin.status.biaodian.display ? 1 : 0,
                  skin.status.quan_ban.display ? 1 : 0,
                  skin.status.fan_jian.display ? 1 : 0,
                  skin.status.menu.display ? 1 : 0);
    }

    // 2. Choose orientation / scheme.
    const bool vertical = (scheme_id == "V1" || scheme_id == "V2");
    LayoutOptions opts;
    opts.orientation = vertical ? Orientation::Vertical : Orientation::Horizontal;
    opts.scale = scale > 0 ? scale : 1.0;
    opts.prefer_split_background = (scheme_id == "H2" || scheme_id == "V2" || (scheme_id == "auto" && skin.h1.pic.empty()));

    const Scheme& scheme =
        vertical ? skin.Vertical(opts.prefer_split_background)
                 : skin.Horizontal(opts.prefer_split_background);
    std::printf("using orientation=%s split_background=%d\n",
                vertical ? "vertical" : "horizontal",
                scheme.IsSplitBackground() ? 1 : 0);

    // 3. Resolve images and publish their native sizes, which the layout uses to
    //    avoid squashing the artwork.
    SsfRenderer renderer;
    renderer.images().SetDirectory(skin_dir);
    int trailing = 0;
    auto native = [&](const std::string& n) -> Size {
      SsfImagePtr img = renderer.images().Get(n);
      Size s;
      if (img && img->Valid()) {
        s.cx = img->Width();
        s.cy = img->Height();
      }
      return s;
    };
    Size main_bg = native(scheme.pic);
    Size py_bg = native(scheme.pinyin_pic);
    Size ca_bg = native(scheme.zhongwen_pic);
    Size icon_slot{0, 0};
    if (!skin.status.menu.names.empty())
      icon_slot = native(skin.status.menu.names[0]);
    if (icon_slot.cx <= 0) {
      // Fall back to any declared button so the status bar still gets a slot
      // size on a skin that hides the menu button.
      for (const StatusButton* b : {&skin.status.cn_en, &skin.status.biaodian,
                                    &skin.status.quan_ban,
                                    &skin.status.fan_jian}) {
        if (!b->names.empty()) {
          icon_slot = native(b->names[0]);
          if (icon_slot.cx > 0) break;
        }
      }
    }
    if (icon_slot.cx <= 0) icon_slot = Size{26, 25};

    std::printf("images: main=%dx%d pinyin=%dx%d candidate=%dx%d\n", main_bg.cx,
                main_bg.cy, py_bg.cx, py_bg.cy, ca_bg.cx, ca_bg.cy);

    for (const auto& r : renderer.Inspect(skin, !vertical)) {
      std::printf("  %-28s %s", r.name.c_str(), r.loaded ? "OK" : "MISSING");
      if (r.loaded) std::printf("  %dx%d", r.width, r.height);
      std::printf("\n");
    }

    opts.native_main_bg = main_bg;
    opts.native_pinyin_bg = py_bg;
    opts.native_candidate_bg = ca_bg;
    opts.status_icon_slot = icon_slot;
    opts.trailing_controls_width = trailing;

    if (inspect_only) {
      Gdiplus::GdiplusShutdown(gdiplus_token);
      return 0;
    }

    // 4. Lay out.
    LayoutInput input;
    input.preedit = Utf8ToWide(preedit);
    input.texts = SplitBar(candidates);
    input.labels = SplitBar(labels);
    input.comments = SplitBar(comments);
    input.highlighted = highlight;
    input.show_status_bar = show_status;
    input.page_size = 1;
    input.show_page_indicator = false;

    GdiMeasurer measurer(skin, opts.scale);
    SsfLayoutResult layout =
        SsfLayoutEngine().Compute(skin, opts, input, measurer);

    std::printf("window         : %dx%d\n", layout.window.cx, layout.window.cy);
    std::printf("pinyin_text    : (%d,%d)-(%d,%d) baseline=%d\n",
                layout.pinyin_text.left, layout.pinyin_text.top,
                layout.pinyin_text.right, layout.pinyin_text.bottom,
                layout.pinyin_baseline);
    std::printf("candidate_bg   : (%d,%d)-(%d,%d)\n", layout.candidate_bg.left,
                layout.candidate_bg.top, layout.candidate_bg.right,
                layout.candidate_bg.bottom);
    if (debug) {
      for (size_t i = 0; i < layout.candidates.size(); ++i) {
        const CandidateBox& b = layout.candidates[i];
        std::printf("  cand[%zu] box=(%d,%d)-(%d,%d) label=(%d,%d)-(%d,%d) "
                    "text=(%d,%d)-(%d,%d)\n",
                    i, b.box.left, b.box.top, b.box.right, b.box.bottom,
                    b.label.left, b.label.top, b.label.right, b.label.bottom,
                    b.text.left, b.text.top, b.text.right, b.text.bottom);
      }
      std::printf("  highlight=(%d,%d)-(%d,%d)\n", layout.highlight.left,
                  layout.highlight.top, layout.highlight.right,
                  layout.highlight.bottom);
    }

    // 5. Compose.
    RenderText rtext;
    rtext.pinyin = input.preedit;
    rtext.aux = input.aux;
    rtext.labels = input.labels;
    rtext.texts = input.texts;
    rtext.comments = input.comments;
    rtext.highlighted = input.highlighted;

    RenderOptions ropts;
    ropts.scale = opts.scale;
    ropts.draw_status_bar = show_status;

    RenderResult result = renderer.Compose(skin, opts, input, rtext, layout,
                                           ropts);
    if (!result.surface) {
      std::fprintf(stderr, "composition failed\n");
      Gdiplus::GdiplusShutdown(gdiplus_token);
      return 1;
    }

    // 6. Rasterise the text runs with GDI on top of the composed surface, then
    //    repair the alpha that GDI clobbers inside those rectangles.
    {
      const int w = result.surface->width;
      const int h = result.surface->height;

      BITMAPINFO bmi = {};
      bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
      bmi.bmiHeader.biWidth = w;
      bmi.bmiHeader.biHeight = -h;
      bmi.bmiHeader.biPlanes = 1;
      bmi.bmiHeader.biBitCount = 32;
      bmi.bmiHeader.biCompression = BI_RGB;

      HDC screen = ::GetDC(nullptr);
      void* bits = nullptr;
      HBITMAP dib = ::CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits,
                                       nullptr, 0);
      ::ReleaseDC(nullptr, screen);
      if (dib && bits) {
        // Same conversion the live renderer performs.
        std::vector<uint8_t> bgra;
        result.surface->ToPremultipliedBGRA(bgra);
        std::memcpy(bits, bgra.data(), bgra.size());

        HDC mem = ::CreateCompatibleDC(nullptr);
        HGDIOBJ old_bmp = ::SelectObject(mem, dib);
        ::SetBkMode(mem, TRANSPARENT);

        for (const TextRun& run : result.runs) {
          if (run.text.empty()) continue;
          const int pixel_height = (std::max)(
              1, static_cast<int>(skin.font_size * opts.scale + 0.5));
          HFONT font = ::CreateFontW(
              -pixel_height, 0, 0, 0,
              run.style == FontStyle::Bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE,
              FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
              ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
              ChooseFontFace(skin, run.text, ITextMeasurer::CANDIDATE).c_str());
          HGDIOBJ old_font = ::SelectObject(mem, font);
          ::SetTextColor(mem, RGB(run.color.r, run.color.g, run.color.b));
          // TextOut uses the font's ascent as the top of the ink box, which is
          // exactly how the measurer derived the baseline.
          ::TextOutW(mem, run.box.left, run.box.top, run.text.c_str(),
                     static_cast<int>(run.text.size()));
          ::SelectObject(mem, old_font);
          ::DeleteObject(font);
        }

        // Alpha repair: GDI zeroes alpha wherever it drew.
        auto* px = static_cast<uint8_t*>(bits);
        for (const Rect& r : result.text_rects) {
          const int x0 = (std::max)(0, r.left);
          const int y0 = (std::max)(0, r.top);
          const int x1 = (std::min)(w, r.right);
          const int y1 = (std::min)(h, r.bottom);
          for (int y = y0; y < y1; ++y) {
            uint8_t* row = px + (static_cast<size_t>(y) * w + x0) * 4;
            for (int x = x0; x < x1; ++x, row += 4) {
              if (row[3] == 0) row[3] = 255;
            }
          }
        }

        // Read back into an SsfImage (un-premultiply) so SavePng can encode it.
        SsfImage out;
        out.width = w;
        out.height = h;
        out.pixels.assign(static_cast<size_t>(w) * h * 4, 0);
        for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
          const uint8_t b = px[i * 4 + 0];
          const uint8_t g = px[i * 4 + 1];
          const uint8_t r = px[i * 4 + 2];
          const uint8_t a = px[i * 4 + 3];
          auto un = [a](uint8_t c) -> uint8_t {
            if (a == 0) return 0;
            if (a == 255) return c;
            const unsigned v = (static_cast<unsigned>(c) * 255u + a / 2u) / a;
            return static_cast<uint8_t>(v > 255u ? 255u : v);
          };
          out.pixels[i * 4 + 0] = un(r);
          out.pixels[i * 4 + 1] = un(g);
          out.pixels[i * 4 + 2] = un(b);
          out.pixels[i * 4 + 3] = a;
        }

        ::SelectObject(mem, old_bmp);
        ::DeleteDC(mem);

        if (SavePng(out, out_path)) {
          std::fwprintf(stdout, L"wrote %ls (%dx%d)\n", out_path.c_str(), w, h);
        } else {
          std::fprintf(stderr, "PNG save failed\n");
          exit_code = 1;
        }
      }
      if (dib) ::DeleteObject(dib);
    }
  }

  Gdiplus::GdiplusShutdown(gdiplus_token);
  return exit_code;
}
