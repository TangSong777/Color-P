#pragma once
#include <WeaselIPCData.h>
#include <WeaselUI.h>
#include "StandardLayout.h"
#include "Layout.h"
#include "GdiplusBlur.h"

// Sogou SSF skin compatibility layer. Optional and additive: when no SSF skin
// is configured the panel keeps using the stock Weasel rendering path
// unchanged.
#include "ssf/SsfImageLoader.h"
#include "ssf/SsfIniParser.h"
#include "ssf/SsfLayoutAdapter.h"
#include "ssf/SsfRenderer.h"
#include "ssf/SsfSkin.h"
#include "ssf/SsfVisibility.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

using namespace weasel;

typedef CWinTraits<WS_POPUP | WS_CLIPSIBLINGS | WS_DISABLED,
                   WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE |
                       WS_EX_LAYERED>
    CWeaselPanelTraits;

enum class BackType {
  TEXT = 0,
  CAND = 1,
  BACKGROUND = 2  // background
};

class WeaselPanel
    : public CWindowImpl<WeaselPanel, CWindow, CWeaselPanelTraits>,
      CDoubleBufferImpl<WeaselPanel> {
 public:
  BEGIN_MSG_MAP(WeaselPanel)
  MESSAGE_HANDLER(WM_CREATE, OnCreate)
  MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
  MESSAGE_HANDLER(WM_TIMER, OnTimerMessage)
  MESSAGE_HANDLER(WM_APP + 0x517, OnAvoidCandidate)
  MESSAGE_HANDLER(WM_SHOWWINDOW, OnPanelShow)
  MESSAGE_HANDLER(WM_DPICHANGED, OnDpiChanged)
  MESSAGE_HANDLER(WM_MOUSEACTIVATE, OnMouseActivate)
  MESSAGE_HANDLER(WM_LBUTTONUP, OnLeftClickedUp)
  MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLeftClickedDown)
  MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
  MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
  MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
  CHAIN_MSG_MAP(CDoubleBufferImpl<WeaselPanel>)
  END_MSG_MAP()

  LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnTimerMessage(UINT uMsg, WPARAM wParam, LPARAM lParam,
                         BOOL& bHandled);
  LRESULT OnDpiChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnAvoidCandidate(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnPanelShow(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnMouseActivate(UINT uMsg,
                          WPARAM wParam,
                          LPARAM lParam,
                          BOOL& bHandled);
  LRESULT OnLeftClickedUp(UINT uMsg,
                          WPARAM wParam,
                          LPARAM lParam,
                          BOOL& bHandled);
  LRESULT OnLeftClickedDown(UINT uMsg,
                            WPARAM wParam,
                            LPARAM lParam,
                            BOOL& bHandled);
  LRESULT OnMouseWheel(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnMouseLeave(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

  WeaselPanel(weasel::UI& ui);
  ~WeaselPanel();

  void MoveTo(RECT const& rc);
  // False until a caret anchor has actually been supplied.  A freshly created
  // panel sits at the screen origin and _RepositionWindow() deliberately refuses
  // to move it while the anchor is still the default CRect(), so showing it in
  // that state is what produced the brief top-left candidate flash after
  // switching input methods.
  bool HasInputAnchor() const {
    return !(m_inputPos.left == 0 && m_inputPos.top == 0 &&
             m_inputPos.right == 0 && m_inputPos.bottom == 0);
  }
  void Refresh();
  void DoPaint(CDCHandle dc);
  bool GetIsReposition() { return m_istorepos; }
  void RedrawWindow();

  static VOID CALLBACK OnTimer(_In_ HWND hwnd,
                               _In_ UINT uMsg,
                               _In_ UINT_PTR idEvent,
                               _In_ DWORD dwTime);
  static const int AUTOREV_TIMER = 20240315;
  static UINT_PTR ptimer;

 private:
  template <typename T>
  int DPI_SCALE(T t) {
    return (int)(t * dpiScaleLayout);
  }
  void _InitFontRes(bool forced = false);
  void _CaptureRect(CRect& rect);
  bool m_mouse_entry = false;
  CPoint m_lastMousePos = {-1, -1};
  void _CreateLayout();
  void _ResizeWindow();
  void _RepositionWindow(const bool& adj = false);
  void _MoveModeTipTo(const RECT& rc);
  bool _DrawPreedit(const Text& text, CDCHandle dc, const CRect& rc);
  bool _DrawPreeditBack(const Text& text, CDCHandle dc, const CRect& rc);
  bool _DrawCandidates(CDCHandle& dc, bool back = false);
  void _HighlightText(CDCHandle& dc,
                      const CRect& rc,
                      const COLORREF& color,
                      const COLORREF& shadowColor,
                      const int& radius,
                      const BackType& type,
                      const IsToRoundStruct& rd,
                      const COLORREF& bordercolor);
  void _TextOut(const CRect& rc,
                const std::wstring& psz,
                const size_t& cch,
                const int& inColor,
                IDWriteTextFormat1* const pTextFormat = NULL);

  bool _LayerUpdate(const CRect& rc, CDCHandle dc);

  // --- Sogou SSF skin support -------------------------------------------
  //
  // Active only when a skin is configured and loaded successfully. Every entry
  // point checks SsfActive() first, so the stock path is untouched otherwise.

  bool SsfActive() const { return ssf_skin_loaded_ || ssf_fallback_; }

  // (Re)load the skin when the configuration changed. Returns true when SSF is
  // active afterwards.
  bool _SsfReloadSkin();
  // Create the DirectWrite formats described by the skin (font_ch / font_en /
  // font_size) and resolve them against installed fonts.
  void _SsfInitFonts();
  // Measure one string with the skin's font for `kind`.
  CSize _SsfMeasure(const std::wstring& text,
                    weasel::ssf::ITextMeasurer::FontKind kind);
  int _SsfAscent(weasel::ssf::ITextMeasurer::FontKind kind);
  // Paint the whole candidate window through the SSF renderer. Returns true
  // when something was drawn.
  bool _SsfDoPaint(CDCHandle dc);
  // Draw the SSF text runs for the current layout onto `memDC`.
  void _SsfDrawText(CDCHandle memDC);

  weasel::ssf::Skin ssf_skin_storage_;
  bool ssf_skin_loaded_ = false;
  bool ssf_fallback_ = false;
  ULONGLONG ssf_retry_after_ = 0;
  std::map<std::wstring, ComPtr<IDWriteTextLayout>> ssf_text_cache_[4];
  ComPtr<IDWriteTextLayout> _SsfTextLayout(const std::wstring& text, int kind);
  struct DibSurface {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ original = nullptr;
    void* bits = nullptr;
    int width = 0, height = 0;
    void Clear() {
      if (original && dc) ::SelectObject(dc, original);
      if (bitmap) ::DeleteObject(bitmap);
      if (dc) ::DeleteDC(dc);
      dc = nullptr; bitmap = nullptr; original = nullptr; bits = nullptr;
      width = height = 0;
    }
    ~DibSurface() { Clear(); }
    bool Ensure(int w, int h);
  } ssf_dib_;
  std::vector<uint8_t> ssf_upload_;
  std::wstring ssf_loaded_skin_id_;  // style.ssf_skin at load time
  std::wstring ssf_skin_dir_;
  weasel::ssf::SsfRenderer ssf_renderer_;
  weasel::ssf::SsfLayoutAdapter* ssf_adapter_ = nullptr;
  // Per-kind text formats: PINYIN, CANDIDATE, LABEL, COMMENT.
  ComPtr<IDWriteTextFormat1> ssf_formats_[4];
  int ssf_format_point_[4] = {0, 0, 0, 0};
  // Vertical metrics of those formats, read once from the font collection.
  int ssf_ascent_[4] = {0, 0, 0, 0};
  int ssf_line_height_[4] = {0, 0, 0, 0};

  weasel::Layout* m_layout;
  weasel::Context& m_ctx;
  weasel::Context& m_octx;
  weasel::Status& m_status;
  weasel::UIStyle& m_style;
  weasel::UIStyle& m_ostyle;
  const bool& m_in_server;

  CRect m_inputPos;
  RECT m_lastCandidateRect = {};
  int m_offsetys[MAX_CANDIDATES_COUNT];  // offset y for candidates when
                                         // vertical layout over bottom
  int m_offsety_preedit;
  int m_offsety_aux;
  bool m_istorepos;

  CIcon m_iconDisabled;
  CIcon m_iconEnabled;
  CIcon m_iconAlpha;
  CIcon m_iconFull;
  CIcon m_iconHalf;
  std::wstring m_current_zhung_icon;
  std::wstring m_current_ascii_icon;
  std::wstring m_current_half_icon;
  std::wstring m_current_full_icon;
  // for gdiplus drawings
  Gdiplus::GdiplusStartupInput _m_gdiplusStartupInput;
  ULONG_PTR _m_gdiplusToken;

  UINT dpi;

  CRect rcw;
  BYTE m_candidateCount;
  BYTE m_lastCandidateCount;

  bool hide_candidates;
  bool m_sticky;
  // for multi font_face & font_point
  PDWR pDWR;
  std::function<void(size_t* const, size_t* const, bool* const, bool* const)>&
      _UICallback;
  float bar_scale_ = 1.0;
  float dpiScaleLayout = 1.0f;
  int m_hoverIndex = -1;
  HMONITOR m_hMonitor = NULL;
  bool m_redraw_by_monitor_change = false;
};
