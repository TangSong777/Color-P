#include "stdafx.h"
#include <WeaselUI.h>
#include "WeaselPanel.h"
#include "ssf/SsfVisibility.h"

using namespace weasel;

class weasel::UIImpl {
 public:
  WeaselPanel panel;

  UIImpl(weasel::UI& ui) : panel(ui), shown(false) {}
  ~UIImpl() { Hide(); }
  void Refresh(bool preserve_tip = false) {
    if (!panel.IsWindow())
      return;
    if (timer && !preserve_tip) {
      Hide();
      KillTimer(panel.m_hWnd, AUTOHIDE_TIMER);
      timer = 0;
    }
    panel.Refresh();
  }
  void Show();
  void Hide();
  void ShowWithTimeout(size_t millisec);
  bool IsShown() const { return shown; }

  static VOID CALLBACK OnTimer(_In_ HWND hwnd,
                               _In_ UINT uMsg,
                               _In_ UINT_PTR idEvent,
                               _In_ DWORD dwTime);
  static const int AUTOHIDE_TIMER = 20121220;
  bool timer = false;
  ULONGLONG hide_deadline = 0;
  bool shown;
};

void UIImpl::Show() {
  if (!panel.IsWindow())
    return;
  // Refuse to show a window that has no caret anchor yet.  A panel is created at
  // the screen origin and _RepositionWindow() will not move it while m_inputPos
  // is still the default CRect(), so showing it in that state is exactly the
  // "candidate window flashes in the top-left corner" seen after switching input
  // methods.  The anchor always arrives before the composition that is supposed
  // to be visible, so staying hidden here loses nothing; the caller shows the
  // window again on the frame that follows the position update.
  if (!panel.HasInputAnchor()) {
    Hide();
    return;
  }
  panel.ShowWindow(SW_SHOWNA);
  shown = true;
  if (timer) {
    KillTimer(panel.m_hWnd, AUTOHIDE_TIMER);
    RemovePropW(panel.m_hWnd, L"WeaselModeTipOwner");
    timer = 0;
  }
}

void UIImpl::Hide() {
  hide_deadline = 0;
  if (!panel.IsWindow()) {
    shown = false;
    timer = false;
    return;
  }
  panel.ShowWindow(SW_HIDE);
  shown = false;
  if (timer) {
    KillTimer(panel.m_hWnd, AUTOHIDE_TIMER);
    RemovePropW(panel.m_hWnd, L"WeaselModeTipOwner");
    timer = 0;
  }
}

void UIImpl::ShowWithTimeout(size_t millisec) {
  if (!panel.IsWindow())
    return;
  DLOG(INFO) << "ShowWithTimeout: " << millisec;
  // A queued callback can survive KillTimer. Check the latest deadline too.
  KillTimer(panel.m_hWnd, AUTOHIDE_TIMER);
  timer = false;
  hide_deadline = GetTickCount64() + millisec;
  if (!SetPropW(panel.m_hWnd, L"WeaselModeTipOwner", this)) {
    Hide();
    return;
  }
  timer = SetTimer(panel.m_hWnd, AUTOHIDE_TIMER,
                   static_cast<UINT>(millisec), &UIImpl::OnTimer) != 0;
  if (!timer) {
    RemovePropW(panel.m_hWnd, L"WeaselModeTipOwner");
    Hide();
    return;
  }
  panel.ShowWindow(SW_SHOWNA);
  shown = true;
}
VOID CALLBACK UIImpl::OnTimer(_In_ HWND hwnd,
                              _In_ UINT uMsg,
                              _In_ UINT_PTR idEvent,
                              _In_ DWORD dwTime) {
  DLOG(INFO) << "OnTimer:";
  UIImpl* self = static_cast<UIImpl*>(GetPropW(hwnd, L"WeaselModeTipOwner"));
  const ULONGLONG now = GetTickCount64();
  if (self && self->timer && now < self->hide_deadline) {
    // A stale/early callback must wait only the remaining time, not another
    // complete periodic interval (which could leave the tip up for 4s).
    if (!SetTimer(hwnd, idEvent,
                  static_cast<UINT>(self->hide_deadline - now),
                  &UIImpl::OnTimer))
      self->Hide();
    return;
  }
  KillTimer(hwnd, idEvent);
  RemovePropW(hwnd, L"WeaselModeTipOwner");
  if (self) {
    self->timer = false;
    self->Hide();
    self->shown = false;
  }
}

HWND UI::panel_hwnd() const {
  if (!pimpl_ || !pimpl_->panel.IsWindow()) return nullptr;
  return pimpl_->panel.m_hWnd;
}

bool UI::Create(HWND parent) {
  if (pimpl_ && pimpl_->panel.IsWindow()) {
    // Reuse the surface, but follow document/view ownership changes.
    SetWindowLongPtr(pimpl_->panel.m_hWnd, GWLP_HWNDPARENT,
                     reinterpret_cast<LONG_PTR>(parent));
    return true;
  }

  if (!pimpl_) pimpl_ = new UIImpl(*this);
  if (!pimpl_)
    return false;

  pimpl_->panel.Create(
      parent, 0, 0, WS_POPUP,
      WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
      0U, 0);
  return pimpl_->panel.IsWindow() != FALSE;
}

void UI::Destroy(bool full) {
  if (pimpl_) {
    pimpl_->Hide();
    // destroy panel
    if (pimpl_->panel.IsWindow()) {
      pimpl_->panel.DestroyWindow();
    }
    if (full) {
      delete pimpl_;
      pimpl_ = 0;
      pDWR.reset();
    }
  }
}

bool UI::GetIsReposition() {
  if (pimpl_)
    return pimpl_->panel.GetIsReposition();
  else
    return false;
}

void UI::Show() {
  if (ssf::SuppressEmptyWindow(style_.ssf_enabled, ctx_.empty(),
                               status_.show_mode_tip)) {
    Hide();
    return;
  }
  if (pimpl_) {
    pimpl_->Show();
  }
}

void UI::Hide() {
  if (pimpl_) {
    pimpl_->Hide();
  }
}

void UI::ShowWithTimeout(size_t millisec) {
  if (status_.show_mode_tip && millisec > 2000) millisec = 2000;
  if (ssf::SuppressEmptyWindow(style_.ssf_enabled, ctx_.empty(),
                               status_.show_mode_tip)) {
    Hide();
    return;
  }
  if (pimpl_) {
    pimpl_->ShowWithTimeout(millisec);
  }
}

bool UI::IsCountingDown() const {
  return pimpl_ && pimpl_->timer != 0;
}

bool UI::IsShown() const {
  return pimpl_ && pimpl_->IsShown();
}

void UI::Refresh() {
  if (pimpl_) {
    pimpl_->Refresh();
  }
}

void UI::UpdateInputPosition(RECT const& rc) {
  if (pimpl_ && pimpl_->panel.IsWindow()) {
    pimpl_->panel.MoveTo(rc);
  }
}

void UI::Update(const Context& ctx, const Status& status) {
  const bool preserve_tip = IsShown() && IsCountingDown() &&
      status_.show_mode_tip && status.show_mode_tip &&
      ctx_.empty() && ctx.empty();
  if (ctx_ == ctx && status_ == status) {
    // A capture overlay can destroy the HWND without changing Rime's data.
    // Repaint a recreated/hidden surface before it is shown again.
    if (!IsShown() &&
        !ssf::SuppressEmptyWindow(style_.ssf_enabled, ctx.empty(),
                                 status.show_mode_tip))
      Refresh();
    return;
  }
  ctx_ = ctx;
  status_ = status;
  if (ssf::SuppressEmptyWindow(style_.ssf_enabled, ctx_.empty(),
                               status_.show_mode_tip)) {
    // Cache the idle state, but do not lay out or repaint a notification HWND
    // on each numeric commit. Only dismiss an existing legitimate popup once.
    if (IsShown() || IsCountingDown()) Hide();
    return;
  }
  if (style_.candidate_abbreviate_length > 0) {
    for (auto& c : ctx_.cinfo.candies) {
      if (c.str.length() > (size_t)style_.candidate_abbreviate_length) {
        c.str =
            c.str.substr(0, (size_t)style_.candidate_abbreviate_length - 1) +
            L"..." + c.str.substr(c.str.length() - 1);
      }
    }
  }
  if (pimpl_) pimpl_->Refresh(preserve_tip);
}
