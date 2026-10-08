#include "stdafx.h"

#include "WeaselTSF.h"
#include "CandidateList.h"
#include <KeyEvent.h>
#include <math.h>

using namespace std;
using namespace weasel;

CCandidateList::CCandidateList(com_ptr<WeaselTSF> pTextService)
    : _ui(make_unique<UI>()), _tsf(pTextService), _pbShow(TRUE) {
  _cRef = 1;
}

CCandidateList::~CCandidateList() {}

STDMETHODIMP CCandidateList::QueryInterface(REFIID riid, void** ppvObj) {
  if (ppvObj == nullptr) {
    return E_INVALIDARG;
  }

  *ppvObj = nullptr;

  if (IsEqualIID(riid, IID_ITfUIElement) ||
      IsEqualIID(riid, IID_ITfCandidateListUIElement) ||
      IsEqualIID(riid, IID_ITfCandidateListUIElementBehavior)) {
    *ppvObj = (ITfCandidateListUIElementBehavior*)this;
  } else if (IsEqualIID(riid, IID_IUnknown)) {
    // All interfaces return this same canonical IUnknown identity.
    *ppvObj = (ITfIntegratableCandidateListUIElement*)this;
  }

  // This custom build deliberately never exposes
  // ITfIntegratableCandidateListUIElement.  Windows Search queries this
  // interface before the first server response can carry the SSF style, then
  // caches the result for the lifetime of the candidate object.  Advertising
  // it conditionally therefore races style initialization and lets SearchHost
  // replace the native panel with its own legacy candidate presentation.

  if (*ppvObj) {
    AddRef();
    return S_OK;
  }

  return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) CCandidateList::AddRef(void) {
  return ++_cRef;
}

STDMETHODIMP_(ULONG) CCandidateList::Release(void) {
  LONG cr = --_cRef;

  assert(_cRef >= 0);

  if (_cRef == 0) {
    delete this;
  }

  return cr;
}

STDMETHODIMP CCandidateList::GetDescription(BSTR* pbstr) {
  if (!pbstr) return E_INVALIDARG;
  *pbstr = SysAllocString(L"Candidate List");
  return *pbstr ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CCandidateList::GetGUID(GUID* pguid) {
  if (!pguid) return E_INVALIDARG;
  /// 36c3c795-7159-45aa-ab12-30229a51dbd3
  *pguid = {0x36c3c795,
            0x7159,
            0x45aa,
            {0xab, 0x12, 0x30, 0x22, 0x9a, 0x51, 0xdb, 0xd3}};
  return S_OK;
}

STDMETHODIMP CCandidateList::Show(BOOL showCandidateWindow) {
  if (showCandidateWindow && _positionValid && !_tsf->_IsKeyboardDisabled()) {
    // The native popup is destroyed -- but the decoded skin, fonts and
    // DirectWrite resources are kept -- whenever the user leaves Weasel or a
    // composition ends (see _DisposeUIWindow).  Rebuild only the window here,
    // which is cheap, instead of letting the next composition pay for
    // re-decoding the whole skin.
    if (_ui->panel_hwnd() == nullptr)
      _MakeUIWindow();
    _ui->Show();
  } else {
    _ui->Hide();
  }
  return S_OK;
}

STDMETHODIMP CCandidateList::IsShown(BOOL* pIsShow) {
  if (!pIsShow) return E_INVALIDARG;
  *pIsShow = _ui->IsShown();
  return S_OK;
}

STDMETHODIMP CCandidateList::GetUpdatedFlags(DWORD* pdwFlags) {
  if (!pdwFlags)
    return E_INVALIDARG;

  *pdwFlags = TF_CLUIE_DOCUMENTMGR | TF_CLUIE_COUNT | TF_CLUIE_SELECTION |
              TF_CLUIE_STRING | TF_CLUIE_CURRENTPAGE;
  return S_OK;
}

STDMETHODIMP CCandidateList::GetDocumentMgr(ITfDocumentMgr** ppdim) {
  if (!ppdim) return E_INVALIDARG;
  *ppdim = nullptr;
  auto pThreadMgr = _tsf->_GetThreadMgr();
  if (pThreadMgr == nullptr) {
    return E_FAIL;
  }
  if (FAILED(pThreadMgr->GetFocus(ppdim)) || (*ppdim == nullptr)) {
    return E_FAIL;
  }
  return S_OK;
}

STDMETHODIMP CCandidateList::GetCount(UINT* pCandidateCount) {
  if (!pCandidateCount) return E_INVALIDARG;
  *pCandidateCount = static_cast<UINT>(_ui->ctx().cinfo.candies.size());
  return S_OK;
}

STDMETHODIMP CCandidateList::GetSelection(UINT* pSelectedCandidateIndex) {
  if (!pSelectedCandidateIndex) return E_INVALIDARG;
  *pSelectedCandidateIndex = _ui->ctx().cinfo.highlighted;
  return S_OK;
}

STDMETHODIMP CCandidateList::GetString(UINT uIndex, BSTR* pbstr) {
  if (!pbstr) return E_INVALIDARG;
  *pbstr = nullptr;
  auto& cinfo = _ui->ctx().cinfo;
  if (uIndex >= cinfo.candies.size())
    return E_INVALIDARG;

  auto& str = cinfo.candies[uIndex].str;
  *pbstr = SysAllocStringLen(str.c_str(), static_cast<UINT>(str.size()));
  return *pbstr ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP CCandidateList::GetPageIndex(UINT* pIndex,
                                          UINT uSize,
                                          UINT* puPageCnt) {
  if (!puPageCnt)
    return E_INVALIDARG;
  *puPageCnt = 1;
  if (pIndex) {
    if (uSize < *puPageCnt) {
      return E_INVALIDARG;
    }
    *pIndex = 0;
  }
  return S_OK;
}

STDMETHODIMP CCandidateList::SetPageIndex(UINT* pIndex, UINT uPageCnt) {
  if (!pIndex)
    return E_INVALIDARG;
  return S_OK;
}

STDMETHODIMP CCandidateList::GetCurrentPage(UINT* puPage) {
  if (!puPage) return E_INVALIDARG;
  *puPage = 0;
  return S_OK;
}

STDMETHODIMP CCandidateList::SetSelection(UINT nIndex) {
  if (nIndex >= _ui->ctx().cinfo.candies.size()) return E_INVALIDARG;
  _ui->ctx().cinfo.highlighted = nIndex;
  return S_OK;
}

STDMETHODIMP CCandidateList::Finalize(void) {
  Destroy();
  return S_OK;
}

STDMETHODIMP CCandidateList::Abort(void) {
  _tsf->_AbortComposition(true);
  Destroy();
  return S_OK;
}

STDMETHODIMP CCandidateList::SetIntegrationStyle(GUID guidIntegrationStyle) {
  return S_OK;
}

STDMETHODIMP CCandidateList::GetSelectionStyle(
    TfIntegratableCandidateListSelectionStyle* ptfSelectionStyle) {
  if (!ptfSelectionStyle) return E_INVALIDARG;
  *ptfSelectionStyle = _selectionStyle;
  return S_OK;
}

STDMETHODIMP CCandidateList::OnKeyDown(WPARAM wParam,
                                       LPARAM lParam,
                                       BOOL* pIsEaten) {
  if (!pIsEaten) return E_INVALIDARG;
  *pIsEaten = FALSE;
  return S_OK;
}

STDMETHODIMP CCandidateList::ShowCandidateNumbers(BOOL* pIsShow) {
  if (!pIsShow) return E_INVALIDARG;
  *pIsShow = TRUE;
  return S_OK;
}

STDMETHODIMP CCandidateList::FinalizeExactCompositionString() {
  _tsf->_AbortComposition(false);
  return E_NOTIMPL;
}

void CCandidateList::UpdateUI(const Context& ctx, const Status& status) {
  if (_uiStarting || _uiEnding) return;
  if (ctx.empty() && !status.show_mode_tip) {
    // A literal commit has no candidate presentation. End a previous UI once,
    // if present, but do not schedule layout/paint work for the empty response.
    if (_uiStarted || _nativeForced)
      EndUI();
    _ui->ctx() = ctx;
    _ui->status() = status;
    return;
  }
  // Re-register after a host ended the UI element.  If only the native popup
  // was destroyed by a capture overlay, recreate that popup without nesting
  // BeginUIElement/EndUIElement calls inside a TSF callback.
  if (status.composing && !_uiStarted && !_nativeForced)
    StartUI();
  else if (status.composing && (_uiStarted || _nativeForced) && _ShouldShowNative() &&
           _nativeWindowDestroyed) {
    _ui->style() = _style;
    _MakeUIWindow();
  }

  // ResponseParser receives _style so a response can arrive before the
  // native panel is created.  Synchronize it on every frame, including the
  // first composition frame, instead of relying on an unsafe shared COM/UI
  // object reference.
  _ui->style() = _style;
  if (_ui->style().inline_preedit) {
    _ui->style().client_caps |= weasel::INLINE_PREEDIT_CAPABLE;
  } else {
    _ui->style().client_caps &= ~weasel::INLINE_PREEDIT_CAPABLE;
  }

  /// In UWP, candidate window will only be shown
  /// if it is owned by active view window
  //_UpdateOwner();
  _ui->Update(ctx, status);
  if (_uiStarted)
    _UpdateUIElement();

  if (status.composing && (_uiStarted || _nativeForced))
    Show(_ShouldShowNative());
  else
    Show(FALSE);
}

void CCandidateList::UpdateStyle(const UIStyle& sty) {
  _style = sty;
  _ui->style() = sty;
}

void CCandidateList::UpdateInputPosition(RECT const& rc) {
  const bool first = !_positionValid;
  _positionValid = true;
  // Remember the anchor even when the native popup does not exist yet: a
  // composing response calls Update() before this, so the window can be created
  // in between and would otherwise stay at the screen origin until the next
  // caret update.  That was the brief top-left flash after switching back.
  _lastInputPos = rc;
  _ui->UpdateInputPosition(rc);
  if (first && _ui->status().composing && (_uiStarted || _nativeForced))
    Show(_ShouldShowNative());
}

void CCandidateList::Destroy() {
  // A client/focus transition may call Destroy while TSF is iterating its UI
  // elements.  Do not re-enter EndUIElement here; only dispose the native
  // popup and let TSF own the UI-element lifecycle.
  Show(FALSE);
  // Destroy() is reached twice for one composition in the ordinary
  // focus-change sequence (once from _AbortComposition, once when the next
  // composition starts).  Disposing an already disposed window and re-arming
  // the flag each time made the popup get rebuilt and destroyed again on every
  // composition.  With the window already gone there is nothing to do, and
  // leaving the flag unarmed lets Show(TRUE) rebuild exactly once, on demand.
  if (_ui->panel_hwnd() == nullptr)
    return;
  _DisposeUIWindow();
  _nativeWindowDestroyed = (_uiStarted || _nativeForced) && _ShouldShowNative();
}

void CCandidateList::DestroyAll() {
  EndUI();
  _DisposeUIWindowAll();
}
UIStyle& CCandidateList::style() {
  return _style;
}

HWND CCandidateList::_GetActiveWnd() {
  com_ptr<ITfDocumentMgr> pDocumentMgr;
  com_ptr<ITfContext> pContext;
  com_ptr<ITfContextView> pContextView;
  com_ptr<ITfThreadMgr> pThreadMgr = _tsf->_GetThreadMgr();

  HWND w = NULL;

  // Reset current context
  _pContextDocument = nullptr;

  if (pThreadMgr != nullptr && SUCCEEDED(pThreadMgr->GetFocus(&pDocumentMgr)) &&
      pDocumentMgr != nullptr &&
      SUCCEEDED(pDocumentMgr->GetTop(&pContext)) &&
      pContext != nullptr &&
      SUCCEEDED(pContext->GetActiveView(&pContextView)) &&
      pContextView != nullptr) {
    // Set current context
    _pContextDocument = pContext;
    pContextView->GetWnd(&w);
  }

  if (w == NULL)
    w = ::GetFocus();
  return w;
}

HRESULT CCandidateList::_UpdateUIElement() {
  HRESULT hr = S_OK;

  com_ptr<ITfUIElementMgr> pUIElementMgr;
  com_ptr<ITfThreadMgr> pThreadMgr = _tsf->_GetThreadMgr();
  if (nullptr == pThreadMgr) {
    return S_OK;
  }
  hr = pThreadMgr->QueryInterface(IID_ITfUIElementMgr, (void**)&pUIElementMgr);

  if (hr == S_OK) {
    pUIElementMgr->UpdateUIElement(uiid);
  }

  return S_OK;
}

void CCandidateList::StartUI() {
  if (_uiStarted || _nativeForced || _uiStarting || _uiEnding)
    return;

  if (!_ui->uiCallback())
    _ui->SetUICallBack([this](size_t* const sel, size_t* const hov,
                              bool* const next, bool* const scroll_next) {
      _tsf->HandleUICallback(sel, hov, next, scroll_next);
    });

  // Keep the candidate surface native in every host.  Registering with
  // ITfUIElementMgr allows SearchHost and other integrated controls to hide
  // this window and render a host-owned list instead.  The native window can
  // safely start before the first style response; UpdateUI synchronizes the
  // style before the first visible frame.
  _StartStandaloneUI();
}

void CCandidateList::_StartStandaloneUI() {
  _nativeForced = true;
  _nativeWindowDestroyed = false;
  _ui->style() = _style;
  _MakeUIWindow();
}

void CCandidateList::EndUI() {
  if (_uiEnding || (!_uiStarted && !_nativeForced)) return;
  _uiEnding = true;
  const bool was_started = _uiStarted;
  // Clear local state first: EndUIElement is allowed to synchronously invoke
  // client callbacks, and those callbacks must not end the same element twice.
  _uiStarted = false;
  _nativeWindowDestroyed = false;
  _nativeForced = false;
  if (was_started) {
    com_ptr<ITfThreadMgr> pThreadMgr = _tsf->_GetThreadMgr();
    if (pThreadMgr) {
      com_ptr<ITfUIElementMgr> emgr;
      if (SUCCEEDED(pThreadMgr->QueryInterface(&emgr)) && emgr != NULL)
        emgr->EndUIElement(uiid);
    }
  }
  Show(FALSE);
  _DisposeUIWindow();
  _uiEnding = false;
}

com_ptr<ITfContext> CCandidateList::GetContextDocument() {
  return _pContextDocument;
}

void CCandidateList::_DisposeUIWindow() {
  if (_ui == nullptr) {
    return;
  }

  // Deliberately NOT Destroy(true).  This path runs every time the user leaves
  // Weasel -- focussing another window and, importantly, switching to another
  // keyboard layout with Win+Space -- and also when a composition ends.
  // Destroy(true) deletes the UIImpl, and with it the WeaselPanel that owns the
  // decoded Color-P images, the GDI+ session and the DirectWrite resources.
  // Switching back then had to rebuild all of that unattended, synchronously,
  // on the user's first keystroke: measured at ~185 ms even with every page
  // still cached, and the 0.5-2 s the user actually sees once Windows has
  // evicted those pages during a long idle period.
  //
  // The default Destroy(false) still destroys the native popup window, so
  // nothing stale is left on screen.  Everything decoded survives inside the
  // UIImpl, _MakeUIWindow() re-creates the window cheaply on the next
  // composition, and full teardown still happens where it belongs:
  // _DisposeUIWindowAll() -> Destroy(true) on service shutdown.
  _ui->Destroy();
}

void CCandidateList::_DisposeUIWindowAll() {
  if (_ui == nullptr) {
    return;
  }

  // call _ui->Destroy(true) to clean resources
  _ui->Destroy(true);
}

void CCandidateList::_MakeUIWindow() {
  HWND p = _GetActiveWnd();
  _nativeWindowDestroyed = !_ui->Create(p);
  // Place the new window at the last known caret anchor before anything can
  // paint it.  A freshly created panel starts at the screen origin, and the
  // composing response that created it already delivered its caret position
  // while no window existed, so without this the popup flashes at (0,0) until
  // the next caret update arrives.
  if (_positionValid)
    _ui->UpdateInputPosition(_lastInputPos);
}

void WeaselTSF::_UpdateUI(const Context& ctx, const Status& status) {
  _cand->UpdateUI(ctx, status);
}

void WeaselTSF::_StartUI() {
  _cand->StartUI();
}

void WeaselTSF::_EndUI() {
  _cand->EndUI();
}

void WeaselTSF::_ShowUI() {
  _cand->Show(TRUE);
}

void WeaselTSF::_HideUI() {
  _cand->Show(FALSE);
}

com_ptr<ITfContext> WeaselTSF::_GetUIContextDocument() {
  return _cand->GetContextDocument();
}

void WeaselTSF::_DeleteCandidateList() {
  _cand->Destroy();
}

void WeaselTSF::_SelectCandidateOnCurrentPage(size_t index) {
  m_client.SelectCandidateOnCurrentPage(index);
  // simulate a VK_SELECT presskey to get data back and DoEditSession
  // the simulated keycode must be the one make TranslateKeycode Non-Zero return
  // fix me: are there any better ways?
  INPUT inputs[2];
  inputs[0].type = INPUT_KEYBOARD;
  inputs[0].ki = {VK_SELECT, 0, 0, 0, 0};
  inputs[1].type = INPUT_KEYBOARD;
  inputs[1].ki = {VK_SELECT, 0, KEYEVENTF_KEYUP, 0, 0};
  ::SendInput(sizeof(inputs) / sizeof(INPUT), inputs, sizeof(INPUT));
}

void WeaselTSF::_HandleMousePageEvent(bool* const nextPage,
                                      bool* const scrollNextPage) {
  // from scrolling event
  if (scrollNextPage) {
    if (_cand->style().paging_on_scroll)
      m_client.ChangePage(!(*scrollNextPage));
    else {
      UINT current_select = 0, cand_count = 0;
      _cand->GetSelection(&current_select);
      _cand->GetCount(&cand_count);
      bool is_reposition = _cand->GetIsReposition();
      int offset = *scrollNextPage ? 1 : -1;
      offset = offset * (is_reposition ? -1 : 1);
      int index = (int)current_select + offset;
      if (index >= 0 && index < (int)cand_count)
        m_client.HighlightCandidateOnCurrentPage((size_t)index);
      else {
        KeyEvent ke{0, 0};
        ke.keycode = (index < 0) ? ibus::Up : ibus::Down;
        m_client.ProcessKeyEvent(ke);
      }
    }
  } else {  // from click event
    m_client.ChangePage(!(*nextPage));
  }
  _UpdateComposition(_pEditSessionContext);
}

void WeaselTSF::_HandleMouseHoverEvent(const size_t index) {
  UINT current_select = 0;
  _cand->GetSelection(&current_select);

  if (index != current_select) {
    m_client.HighlightCandidateOnCurrentPage(index);
    _UpdateComposition(_pEditSessionContext);
  }
}

void WeaselTSF::HandleUICallback(size_t* const sel,
                                 size_t* const hov,
                                 bool* const next,
                                 bool* const scroll_next) {
  if (sel)
    _SelectCandidateOnCurrentPage(*sel);
  else if (hov)
    _HandleMouseHoverEvent(*hov);
  else if (next || scroll_next)
    _HandleMousePageEvent(next, scroll_next);
}
