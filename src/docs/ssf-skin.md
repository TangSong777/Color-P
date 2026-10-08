# Sogou SSF skin compatibility layer

This document describes the optional renderer backend that lets Weasel (小狼毫)
draw its candidate window from a Sogou Pinyin (搜狗拼音输入法) `.ssf` skin: the
skin's own PNG backgrounds, its fonts, its colours and its layout.

It also records **how each `skin.ini` parameter was reverse-engineered, and which
parameters remain unverified**. Where a value is a heuristic rather than an
established fact, it is marked as such — please do not treat those as
specification.

---

## 1. Enabling it

The layer is **opt-in and additive**. With no skin configured, Weasel renders
exactly as before.

1. Extract the `.ssf` skin into a directory under the Rime user directory
   (`HKCU\Software\Rime\Weasel\RimeUserDir`, e.g. `D:\RimeUser`). The directory
   must contain `skin.ini` and the skin's images:

   ```
   D:\RimeUser\Color-P\
       skin.ini
       skin1_2.png   skin2.png   skin2_1.png   skin2_2.png
       cn2.png   cn3.png   en2.png ... (status-bar art)
   ```

2. Add to `weasel.custom.yaml`:

   ```yaml
   patch:
     style/ssf_skin: "Color-P"      # relative to RimeUserDir, or absolute
     style/ssf_enabled: true
     style/ssf_status_bar: true     # draw the skin's status bar
   ```

3. Redeploy from the Weasel tray menu.

**To go back:** set `style/ssf_enabled: false`, or delete the three `style/ssf_*`
lines. Nothing else needs to change and no files are moved.

---

## 2. Architecture

```
skin.ini + PNGs
      |
      v
SsfIniParser   SsfSkin      <- text decoding, INI parsing, typed model
      |
      v
SsfImage / SsfImageLoader   <- RGBA buffers, GDI+ PNG decode, 9-slice blit
      |
      v
SsfLayoutEngine             <- window size, strip rects, candidate boxes, hit tests
      |
      v
SsfLayoutAdapter            <- implements weasel::Layout on top of the engine
      |
      v
SsfRenderer                 <- composes the layered surface, emits text runs
      |
      v
WeaselPanel::DoPaint        <- 32bpp DIB + DirectWrite glyphs + alpha repair
      |
      v
UpdateLayeredWindow
```

Files (all under `WeaselUI/ssf/`):

| File | Responsibility |
|---|---|
| `SsfSkin.h/.cpp` | Structured model; `marge` → insets normalisation; defaults and clamping |
| `SsfIniParser.h/.cpp` | Encoding detection, INI parsing, value parsers, skin assembly |
| `SsfImage.h/.cpp` | Straight-alpha RGBA buffer, source-over compositing, 9-slice blit, alpha repair, premultiplied BGRA export |
| `SsfImageLoader.h/.cpp` | GDI+ decode (PNG/BMP) and a case-insensitive per-skin cache |
| `SsfLayout.h/.cpp` | Pure geometry; `ITextMeasurer` injected, so it is unit-testable |
| `SsfLayoutAdapter.h/.cpp` | `weasel::Layout` implementation; text measurement and preedit splitting |
| `SsfRenderer.h/.cpp` | Background composition, highlight, text-run list, status-bar icons |

Two design decisions are worth calling out:

* **The layout is not a bypass.** `SsfLayoutAdapter` implements Weasel's own
  `Layout` interface, so painting *and* mouse hit-testing read the same
  rectangles. Clicking a candidate always hits what is drawn.
* **The compositor emits text as data.** `SsfRenderer::Compose` returns
  `TextRun`s plus the rectangles that received text; it never rasterises glyphs.
  That is what lets `WeaselPanel` draw with DirectWrite (matching its own
  measurement) while `ssf_preview` draws with GDI, from the same composition.

---

## 3. `skin.ini` parameter semantics

### 3.1 Confirmed

| Parameter | Meaning | Evidence |
|---|---|---|
| `layout_horizontal=x,a,b` | `[unused-mode, left-border, right-border]` — 9-slice borders | 9IME `crates/9ime-core/src/skin.rs` (`stretch_left = lh[1]`, `stretch_right = lh[2]`) and `blit_nine()` in `crates/9ime-server/src/window.rs` |
| `layout_vertical=x,a,b` | `[unused-mode, top-border, bottom-border]` | same |
| `*_marge=t,b,l,r` | **`[top, bottom, left, right]`** — *not* `left,top,right,bottom` | 9IME `skin.rs`; corroborated by the comments in `fkxxyz/ssfconv` |
| `pinyin_color` | Preedit text colour | 9IME + ssfconv |
| `zhongwen_first_color` | First (highlighted) candidate colour | 9IME `candidate_hl_color` |
| `zhongwen_color` | Other candidates' colour | 9IME `candidate_color` |
| `comphint_color` | Comment / auxiliary text colour | 9IME + ssfconv |
| `font_size` / `font_ch` / `font_en` | Point size, CJK face, Latin face | 9IME; `font_en` usage added here |
| `*_display` | Whether a status-bar button is shown | 9IME has no status bar; read literally |
| `*_pos=x,y` | Button offset **within the status-bar background** | Position of `menu_pos=73,3` plus the 26px `menu3.png` lands exactly on the right edge of the 100px `skin1_2.png` |
| `<name>` / `<name>_hover` / `<name>_down` | Button artwork for normal / hover / pressed | literal reading, matches the supplied assets |
| `separator` | Optional separator colour line | ssfconv swaps the colour; first element is the colour |

#### Colours are stored BGR

`skin.ini` writes colours as `0xRRGGBB` but the bytes are in **BGR** order.
`ParseSogouColor()` therefore swaps them.

This is not a guess for the Color-P skin: `zhongwen_color=0x6e6cff` decodes to
RGB **(255, 108, 110)**, and `skin2.png`'s pink pixels measure exactly
`RGBA(255,108,110,230)`. `pinyin_color=0x3c3c3c` is grey and cannot
self-verify, but the same rule applies.

#### Encoding

`s skin.ini` files in the wild are UTF-16LE (with BOM), UTF-8, or GBK.
`DecodeIniText()` tries in this order:

1. `FF FE` → **UTF-16LE** — *this is what the bundled Color-P skin uses*
2. `FE FF` → UTF-16BE
3. `EF BB BF` → UTF-8 with BOM
4. strict UTF-8 validation (with overlong / surrogate / range checks)
5. **GBK (CP936)**

Getting this wrong silently produces a garbage font name, so the fallback chain
matters.

### 3.2 Confirmed, but by pixel analysis rather than by documentation

`Scheme_H1`/`Scheme_V1` carry a single `pic`; `Scheme_H2`/`Scheme_V2` carry
`pinyin_pic` and `zhongwen_pic`.

**`pinyin_pic` / `zhongwen_pic` are the preedit-strip and candidate-strip
backgrounds**, i.e. the split-background form. This *contradicts* 9IME, which
treats them as highlight images, and 9IME consequently cannot render a
two-tone skin at all — 9IME's `pick_scheme()` prefers `pic` and therefore falls
through to the `Scheme_H1` single background.

Evidence, from the Color-P assets:

* `pinyin_pic = skin2_1.png` is 70×28 and is a **plain pink rounded bar** with
  no white fill — exactly a preedit strip.
* `zhongwen_pic = skin1_2.png` is 100×29 and is a **pink-bordered white rounded
  rectangle** — exactly a candidate strip. (The same file is also
  `[StatusBar] pic`, which is a deliberate reuse by the skin author.)
* `Scheme_H1`'s single `pic = skin2.png` is 84×56 and contains *both* regions
  stacked — pink wedge on top, white area below with a pink border.

The supplied screenshot (pink preedit strip above a white candidate strip with a
pink border) matches the split form, which is why **H2/V2 are preferred when the
skin provides them**.

> Note: measured sizes do **not** satisfy `left + right == width`. For
> `skin2_1.png` (70 wide) the borders are 13 and 27. Sogou's editor writes these
> as independent design parameters, and `skin2_1.png` also carries three
> transparent padding columns on the right. So the window minimum size must
> **not** be assumed equal to the bitmap size class-by-class; both strips are
> 9-sliced to the window and the window is only floored at the artwork's own
> size.

### 3.3 Unverified — treated as data, never as behaviour

| Parameter | Status |
|---|---|
| **`anchor=a,b`** | **No public implementation or documentation found.** Both integers are parsed into `Scheme::anchor` and reported by `ssf_preview`, but the renderer **never reads them**. There is deliberately no `switch(anchor)` heuristic anywhere. |
| `layout_horizontal[0]` / `layout_vertical[0]` (the leading `0`) | Stored as `NineSlice::mode_x` / `mode_y` and never used to switch behaviour. |
| `use_gdip`, `aero`, `glow`, `LargeFontSupport` | Parsed into `Skin` for diagnostics only. Nothing depends on them. |
| The `▽` / `≡` controls at the right of the candidate window | `skin.ini` has **no section describing them**, and none of the 27 PNGs in the Color-P skin match. They are almost certainly Sogou's own built-in UI rather than SSF resources. A placeholder hook exists (`LayoutOptions::trailing_controls_width`) but it is **off by default**, and nothing pretends this is SSF semantics. |
| Candidate-to-candidate spacing | Not exposed by `skin.ini`. Currently a derived constant in `SsfLayout.cpp` (`cand_gap`), kept in one place so it can be retuned against a reference screenshot. |

---

## 4. Rendering

### 4.1 Transparency

The composite surface is a plain RGBA buffer that starts **fully transparent**.
It is never pre-filled with an opaque rectangle, because that would show through
the rounded corners and destroy the skin's shadows.

For a DIB section created with a **negative** `biHeight` the byte order is BGRA,
and DirectWrite's `DrawTextLayout` expects **premultiplied** alpha, so
`SsfImage::ToPremultipliedBGRA()` performs the conversion before upload.

### 4.2 The alpha-repair pass

GDI and DirectWrite both zero the alpha byte of every pixel a glyph touches.
In a layered window that turns text into holes. After drawing,
`WeaselPanel::_SsfDoPaint()` walks the rectangles reported by the compositor and
raises alpha back to 255 **only where it was 0**, so anti-aliased skin pixels
around and between glyphs are left untouched.

The rectangles are deliberately padded by a couple of pixels: over-reporting is
harmless (the repair is idempotent and only ever raises alpha), while
under-reporting produces visible holes.

### 4.3 Fonts

| Role | Source |
|---|---|
| Point size | `skin.ini` `font_size` (one size for everything; Weasel's per-element sizes do not apply to a Sogou skin) |
| CJK face | `font_ch` |
| Latin face | `font_en` |

Each text format is created with the candidate faces as a comma-separated
DirectWrite family list, in the order `font_ch, font_en, Microsoft YaHei,
Microsoft YaHei UI, SimSun, Segoe UI`. A machine that lacks 汉仪细中圆简 therefore
still renders legible Chinese rather than tofu boxes.

Line height and ascent are read back from a `GetLineMetrics()` call on the very
format that will draw the glyphs, so the baseline used for layout is the
baseline DirectWrite will use. The probe string is Latin (`"Ag"`) on purpose: a
CJK probe makes DirectWrite report the font's full CJK line box, which for a
14pt Sogou skin is several pixels taller than the row the skin was authored
around, and the text then sits visibly low in the candidate strip.

### 4.4 Colours

* Preedit → `pinyin_color`
* Highlighted candidate (label **and** text) → `zhongwen_first_color`
* Other candidates → `zhongwen_color`
* Comments and the page indicator → `comphint_color`

### 4.5 DPI

`LayoutOptions::scale` scales the **whole** pipeline — 9-slice borders, insets,
gaps, fonts and the native-size floor — so the skin keeps its proportions
instead of being squashed or having text clipped.

At 100% DPI the scale is exactly 1.0, which is the mode the pixel-accuracy
targets are defined against; bitmap pixels map 1:1 with no resampling.
At higher DPI everything scales together.

---

## 5. Offscreen preview tool

Tuning a bitmap skin through a live IME is slow. `ssf_preview.exe` drives the
same parser, layout engine and compositor and writes a PNG:

```
dist\ssf_preview.exe D:\RimeUser\Color-P --out preview.png
dist\ssf_preview.exe D:\RimeUser\Color-P --scheme H1 --out h1.png
dist\ssf_preview.exe D:\RimeUser\Color-P --scheme V2 --out v2.png
dist\ssf_preview.exe D:\RimeUser\Color-P --inspect
dist\ssf_preview.exe D:\RimeUser\Color-P --debug --preedit "ni" \
    --candidates "你好|你号" --labels "1.|2." --out short.png
```

Options: `--scheme H1|H2|V1|V2`, `--preedit`, `--candidates` (`|`-separated),
`--labels`, `--comments`, `--highlight N`, `--scale F`, `--out FILE`,
`--status` / `--no-status`, `--inspect`, `--debug`.

It links only the platform-free SSF sources plus GDI+/GDI, so it needs no ATL,
WTL, Boost, WinSparkle or librime.

> Caveat: the preview measures text with **GDI** while the live IME uses
> **DirectWrite**. Advance widths can differ by a pixel. That is fine for
> geometry work; use the live IME for final typography checks.

Build it with `build-ssf-preview.ps1`.

---

## 6. Logging

Diagnostics are compiled out by default. Define `WEASEL_SSF_LOGGING` to enable
messages such as:

```
[SSF] loaded skin '【竹子】Color-P' from D:\RimeUser\Color-P
[SSF] font_size=14 font_ch=汉仪细中圆简 font_en=Arial
[SSF] missing image: skin2.png
[SSF] disabled
```

They go to the debug output (`DebugStream`), not to a file.

---

## 7. Failure behaviour

A broken skin must never take the host application down. Accordingly:

* Missing / unreadable `skin.ini` → the layer reports itself inactive and the
  stock renderer runs. The failed skin id is remembered so the filesystem is not
  re-probed on every keystroke.
* Missing image → that layer is skipped. A missing split background falls back to
  the single `pic`, and a scheme with no usable artwork at all still draws a flat
  legible surface rather than nothing.
* Undecodable image → treated as missing, and the miss is cached so a broken skin
  does not retry the filesystem on every repaint.
* Unknown section or key → ignored.
* Malformed numbers → the key's default is used.
* Negative or absurd insets / borders → clamped in `ApplySkinDefaults()` and
  again in the 9-slice routine, which also guarantees no rectangle escapes the
  destination and no negative-size rectangle is produced.
* Missing font → DirectWrite falls back through the family list.

---

## 8. Tests

`test/TestSsfSkin/` covers the parser and layout layers:

* INI parsing of `[Display]`, `[Scheme_H1]`, `[Scheme_H2]`, `[Scheme_V1]`,
  `[Scheme_V2]`, `[StatusBar]`
* `ParseIntList`, `ParsePoint`, `ParseMargin4`, `ParseSogouColor`,
  `ParseNineSliceH/V`
* colour byte order (`0x6e6cff` → RGB 255,108,110)
* UTF-16LE / UTF-8 / GBK decoding of Chinese text
* missing sections, missing keys, malformed values
* 9-slice with destination == source, destination larger, very short and very
  long candidate lists, and degenerate borders
* that no rectangle is negative and no source coordinate escapes the bitmap

Build: `build-ssf-tests.ps1`, run: `dist\ssf_tests.exe` (see
`docs/build-weasel.md`).
