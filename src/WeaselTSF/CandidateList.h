#pragma once
#include <WeaselUI.h>
#include "ctffunc.h"

class WeaselTSF;

class CCandidateList : public ITfIntegratableCandidateListUIElement,
                       public ITfCandidateListUIElementBehavior {
 public:
  CCandidateList(com_ptr<WeaselTSF> pTextService);
  ~CCandidateList();

  // IUnknown
  STDMETHODIMP QueryInterface(REFIID riid, _Outptr_ void** ppvObj);
  STDMETHODIMP_(ULONG) AddRef(void);
  STDMETHODIMP_(ULONG) Release(void);

  // ITfUIElement
  STDMETHODIMP GetDescription(BSTR* pbstr);
  STDMETHODIMP GetGUID(GUID* pguid);
  STDMETHODIMP Show(BOOL showCandidateWindow);
  STDMETHODIMP IsShown(BOOL* pIsShow);

  // ITfCandidateListUIElement
  STDMETHODIMP GetUpdatedFlags(DWORD* pdwFlags);
  STDMETHODIMP GetDocumentMgr(ITfDocumentMgr** ppdim);
  STDMETHODIMP GetCount(UINT* pCandidateCount);
  STDMETHODIMP GetSelection(UINT* pSelectedCandidateIndex);
  STDMETHODIMP GetString(UINT uIndex, BSTR* pbstr);
  STDMETHODIMP GetPageIndex(UINT* pIndex, UINT uSize, UINT* puPageCnt);
  STDMETHODIMP SetPageIndex(UINT* pIndex, UINT uPageCnt);
  STDMETHODIMP GetCurrentPage(UINT* puPage);

  // ITfCandidateListUIElementBehavior methods
  STDMETHODIMP SetSelection(UINT nIndex);
  STDMETHODIMP Finalize(void);
  STDMETHODIMP Abort(void);

  // ITfIntegratableCandidateListUIElement methods
  STDMETHODIMP SetIntegrationStyle(GUID guidIntegrationStyle);
  STDMETHODIMP GetSelectionStyle(
      _Out_ TfIntegratableCandidateListSelectionStyle* ptfSelectionStyle);
  STDMETHODIMP OnKeyDown(_In_ WPARAM wParam,
                         _In_ LPARAM lParam,
                         _Out_ BOOL* pIsEaten);
  STDMETHODIMP ShowCandidateNumbers(_Out_ BOOL* pIsShow);
  STDMETHODIMP FinalizeExactCompositionString();

  /* Update */
  void UpdateUI(const weasel::Context& ctx, const weasel::Status& status);
  void UpdateStyle(const weasel::UIStyle& sty);
  void UpdateInputPosition(RECT const& rc);
  void InvalidatePosition() { _positionValid = false; Show(FALSE); }
  void Destroy();
  void DestroyAll();
  void StartUI();
  void EndUI();

  com_ptr<ITfContext> GetContextDocument();
  bool GetIsReposition() {
    if (_ui)
      return _ui->GetIsReposition();
    else
      return false;
  }

  weasel::UIStyle& style();

 private:
  // void _UpdateOwner();
  HWND _GetActiveWnd();
  HRESULT _UpdateUIElement();

  // for CCandidateList::EndUI(), after ending composition ||
  // WeaselTSF::_EndUI()
  void _DisposeUIWindow();
  // for CCandidateList::Destroy(), when inputing app exit
  void _DisposeUIWindowAll();
  void _MakeUIWindow();
  void _StartStandaloneUI();
  bool _ShouldShowNative() const {
    // StartUI always owns a native popup in this build. Keep _pbShow for the
    // existing UI lifecycle, but never let a host hide the custom surface.
    return _nativeForced || _pbShow;
  }

  std::unique_ptr<weasel::UI> _ui;
  DWORD _cRef;
  com_ptr<WeaselTSF> _tsf;
  DWORD uiid = 0;
  TfIntegratableCandidateListSelectionStyle _selectionStyle =
      STYLE_ACTIVE_SELECTION;

  BOOL _pbShow;
  bool _uiStarted = false;
  bool _uiStarting = false;
  bool _uiEnding = false;
  // A capture overlay can destroy only the native panel while TSF still owns
  // the UI element.  Keep that state separate from the UI-element lifetime.
  bool _nativeWindowDestroyed = false;
  // Some UWP/WinUI hosts can reject BeginUIElement while still allowing a
  // normal popup window. Keep that native SSF window alive independently of
  // the host-owned UI-element lifetime.
  bool _nativeForced = false;
  bool _positionValid = false;
  // Most recent caret anchor handed to UpdateInputPosition.  A composing
  // response delivers Update() before UpdateInputPosition(), so when the native
  // popup does not exist yet the position is dropped (UI::UpdateInputPosition
  // skips a panel that is not a window) and the freshly created popup stays at
  // the screen origin until the next caret update.  Keep the value so
  // _MakeUIWindow() can place the new window before anything paints.
  RECT _lastInputPos = {};
  weasel::UIStyle _style;

  com_ptr<ITfContext> _pContextDocument;
};
