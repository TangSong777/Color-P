#include "stdafx.h"
#include "WeaselTSF.h"

STDMETHODIMP WeaselTSF::OnInitDocumentMgr(ITfDocumentMgr* pDocMgr) {
  return S_OK;
}

STDMETHODIMP WeaselTSF::OnUninitDocumentMgr(ITfDocumentMgr* pDocMgr) {
  return S_OK;
}

STDMETHODIMP WeaselTSF::OnSetFocus(ITfDocumentMgr* pDocMgrFocus,
                                   ITfDocumentMgr* pDocMgrPrevFocus) {
  // Switching to another editable target on the same thread does not trigger
  // OnKillThreadFocus. Clear an unfinished Rime composition before replacing
  // the edit sink, otherwise the hidden candidate window can be resumed and
  // committed with Space after focus returns.
  if (pDocMgrFocus != pDocMgrPrevFocus && _IsComposing())
    _AbortComposition();

  _InitTextEditSink(pDocMgrFocus);
  if (pDocMgrFocus != pDocMgrPrevFocus && _pTextEditSinkContext &&
      !_IsKeyboardDisabled() && _EnsureServerConnected(50)) {
    m_client.FocusIn();
    _UpdateCompositionWindow(_pTextEditSinkContext);
  }

  com_ptr<ITfDocumentMgr> pCandidateListDocumentMgr;
  com_ptr<ITfContext> pTfContext = _GetUIContextDocument();
  if ((nullptr != pTfContext) &&
      SUCCEEDED(pTfContext->GetDocumentMgr(&pCandidateListDocumentMgr))) {
    if (pCandidateListDocumentMgr != pDocMgrFocus) {
      _HideUI();
    } else {
      _ShowUI();
    }
  }

  return S_OK;
}

STDMETHODIMP WeaselTSF::OnPushContext(ITfContext* pContext) {
  // 输入焦点换到另一个输入框/控件时，TSF 会为新上下文 PushContext。
  // 若这里还留着上一处的组合，候选窗就会被绘制到新位置，却已经没有任何作用
  //（用户看到的「UI 跟着切过去，但按键无效」）。组合属于旧上下文，直接终止。
  if (_IsComposing() && pContext != _pEditSessionContext)
    _AbortComposition();
  return S_OK;
}

STDMETHODIMP WeaselTSF::OnPopContext(ITfContext* pContext) {
  // 组合所在的上下文被弹出，说明该输入框已经离开；不能留下可被重新显示的组合。
  if (_IsComposing() && pContext == _pEditSessionContext)
    _AbortComposition();
  return S_OK;
}

BOOL WeaselTSF::_InitThreadMgrEventSink() {
  ITfSource* pSource;
  if (_pThreadMgr->QueryInterface(IID_ITfSource, (void**)&pSource) != S_OK)
    return FALSE;
  if (pSource->AdviseSink(IID_ITfThreadMgrEventSink,
                          (ITfThreadMgrEventSink*)this,
                          &_dwThreadMgrEventSinkCookie) != S_OK) {
    _dwThreadMgrEventSinkCookie = TF_INVALID_COOKIE;
    pSource->Release();
    return FALSE;
  }
  pSource->Release();
  return TRUE;
}

void WeaselTSF::_UninitThreadMgrEventSink() {
  ITfSource* pSource;
  if (_dwThreadMgrEventSinkCookie == TF_INVALID_COOKIE)
    return;
  if (SUCCEEDED(_pThreadMgr->QueryInterface(IID_ITfSource, (void**)&pSource))) {
    pSource->UnadviseSink(_dwThreadMgrEventSinkCookie);
    pSource->Release();
  }
  _dwThreadMgrEventSinkCookie = TF_INVALID_COOKIE;
}
