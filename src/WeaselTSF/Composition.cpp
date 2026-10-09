#include "stdafx.h"
#include "WeaselTSF.h"
#include "EditSession.h"
#include "ResponseParser.h"
#include "CandidateList.h"
#include "HostCompatibility.h"

namespace {

bool RectTouchesMonitor(const RECT& rc) {
  if (rc.bottom <= rc.top)
    return false;
  RECT probe = rc;
  if (probe.right <= probe.left)
    probe.right = probe.left + 1;
  return ::MonitorFromRect(&probe, MONITOR_DEFAULTTONULL) != nullptr;
}

bool GetForegroundCaretRect(HWND foreground,
                            LONG preferred_height,
                            RECT& result) {
  if (!foreground)
    return false;

  GUITHREADINFO info = {};
  info.cbSize = sizeof(info);
  const DWORD thread_id = ::GetWindowThreadProcessId(foreground, nullptr);
  if (!thread_id || !::GetGUIThreadInfo(thread_id, &info))
    return false;

  HWND caret_window = info.hwndCaret;
  RECT caret = info.rcCaret;
  if (caret_window) {
    POINT top_left = {caret.left, caret.top};
    POINT bottom_right = {caret.right, caret.bottom};
    if (!::ClientToScreen(caret_window, &top_left) ||
        !::ClientToScreen(caret_window, &bottom_right))
      return false;
    caret = {top_left.x, top_left.y, bottom_right.x, bottom_right.y};
  } else {
    // Some WinUI controls expose no hwndCaret but still maintain the Win32
    // caret position on their focused child window.
    // GetCaretPos is thread-local: never translate another thread's caret
    // through the foreground child window.
    if (thread_id != GetCurrentThreadId()) return false;
    caret_window = info.hwndFocus ? info.hwndFocus : ::GetFocus();
    POINT point = {};
    if (!caret_window || !::GetCaretPos(&point) ||
        !::ClientToScreen(caret_window, &point))
      return false;
    caret = {point.x, point.y, point.x, point.y};
  }

  if (caret.bottom <= caret.top)
    caret.bottom = caret.top + (preferred_height > 0 ? preferred_height : 20);
  if (caret.right < caret.left)
    caret.right = caret.left;
  if (!RectTouchesMonitor(caret))
    return false;
  result = caret;
  return true;
}

}  // namespace

/* Start Composition */
class CStartCompositionEditSession : public CEditSession {
 public:
  CStartCompositionEditSession(com_ptr<WeaselTSF> pTextService,
                               com_ptr<ITfContext> pContext,
                               BOOL fCUASWorkaroundEnabled,
                               BOOL candidateUI)
      : CEditSession(pTextService, pContext) {
    _fCUASWorkaroundEnabled = fCUASWorkaroundEnabled;
    _candidateUI = candidateUI;
  }

  /* ITfEditSession */
  STDMETHODIMP DoEditSession(TfEditCookie ec);

 private:
  BOOL _fCUASWorkaroundEnabled;
  BOOL _candidateUI;
};

STDMETHODIMP CStartCompositionEditSession::DoEditSession(TfEditCookie ec) {
  HRESULT hr = E_FAIL;
  com_ptr<ITfInsertAtSelection> pInsertAtSelection;
  com_ptr<ITfRange> pRangeComposition;
  if (_pContext->QueryInterface(IID_ITfInsertAtSelection,
                                (LPVOID*)&pInsertAtSelection) != S_OK)
    return hr;
  if (pInsertAtSelection->InsertTextAtSelection(ec, TF_IAS_QUERYONLY, NULL, 0,
                                                &pRangeComposition) != S_OK)
    return hr;

  com_ptr<ITfContextComposition> pContextComposition;
  com_ptr<ITfComposition> pComposition;
  if (_pContext->QueryInterface(IID_ITfContextComposition,
                                (LPVOID*)&pContextComposition) != S_OK)
    return hr;
  if ((pContextComposition->StartComposition(
           ec, pRangeComposition, _pTextService, &pComposition) == S_OK) &&
      (pComposition != NULL)) {
    _pTextService->_SetComposition(pComposition);

    /* set selection */
    TF_SELECTION tfSelection;
    pRangeComposition->Collapse(ec, TF_ANCHOR_END);
    tfSelection.range = pRangeComposition;
    tfSelection.style.ase = TF_AE_NONE;
    tfSelection.style.fInterimChar = FALSE;
    _pContext->SetSelection(ec, 1, &tfSelection);

    // The old composition's range is still visible while its asynchronous
    // end session is pending. Position only after the new composition has
    // actually been created, not from the response handler's stale range.
    if (_candidateUI)
      _pTextService->_UpdateCompositionWindow(_pContext);
  }

  return hr;
}

void WeaselTSF::_StartComposition(com_ptr<ITfContext> pContext,
                                  BOOL fCUASWorkaroundEnabled,
                                  BOOL candidateUI) {
  com_ptr<CStartCompositionEditSession> pStartCompositionEditSession;
  pStartCompositionEditSession.Attach(
      new CStartCompositionEditSession(this, pContext, fCUASWorkaroundEnabled,
                                        candidateUI));
  // A TSF text transaction is also required for literal commits. It must not
  // allocate a candidate HWND, load fonts/skin, or request candidate extents.
  if (candidateUI)
    _cand->StartUI();
  if (pStartCompositionEditSession != nullptr) {
    HRESULT hr;
    pContext->RequestEditSession(_tfClientId, pStartCompositionEditSession,
                                 TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr);
  }
}

/* End Composition */
class CEndCompositionEditSession : public CEditSession {
 public:
  CEndCompositionEditSession(com_ptr<WeaselTSF> pTextService,
                             com_ptr<ITfContext> pContext,
                             com_ptr<ITfComposition> pComposition,
                             BOOL clear = TRUE)
      : CEditSession(pTextService, pContext), _clear(clear) {
    _pComposition = pComposition;
  }

  /* ITfEditSession */
  STDMETHODIMP DoEditSession(TfEditCookie ec);

 private:
  com_ptr<ITfComposition> _pComposition;
  BOOL _clear;
};

STDMETHODIMP CEndCompositionEditSession::DoEditSession(TfEditCookie ec) {
  /* Clear the dummy text we set before, if any. */
  if (_pComposition == nullptr)
    return S_OK;
  // Avoid null pointer dereference
  if (!_pTextService || !_pContext)
    return S_OK;

  _pTextService->_ClearCompositionDisplayAttributes(ec, _pContext);

  com_ptr<ITfRange> pCompositionRange;
  if (_clear && _pComposition->GetRange(&pCompositionRange) == S_OK)
    pCompositionRange->SetText(ec, 0, L"", 0);

  // Drop ownership before EndComposition(). Some applications notify
  // OnCompositionTerminated synchronously while the old composition ends.
  // Keeping it as the current composition makes that normal notification
  // look like an external abort and can clear a new Rime composition during
  // auto-commit.
  if (_pTextService && _pTextService->_IsCurrentComposition(_pComposition))
    _pTextService->_FinalizeComposition();
  _pComposition->EndComposition(ec);
  return S_OK;
}

void WeaselTSF::_EndComposition(com_ptr<ITfContext> pContext,
                                BOOL clear,
                                BOOL endUI) {
  CEndCompositionEditSession* pEditSession;
  HRESULT hr;
  com_ptr<ITfComposition> pComposition = _pComposition;

  if (endUI)
    _cand->EndUI();
  if ((pEditSession = new CEndCompositionEditSession(
           this, pContext, pComposition, clear)) != NULL) {
    pContext->RequestEditSession(_tfClientId, pEditSession,
                                 TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr);
    pEditSession->Release();
  }
}

/* Get Text Extent */
class CGetTextExtentEditSession : public CEditSession {
 public:
  CGetTextExtentEditSession(com_ptr<WeaselTSF> pTextService,
                            com_ptr<ITfContext> pContext,
                            com_ptr<ITfContextView> pContextView,
                            com_ptr<ITfComposition> pComposition,
                            bool enhancedPosition)
      : CEditSession(pTextService, pContext) {
    _pContextView = pContextView;
    _pComposition = pComposition;
    _enhancedPosition = enhancedPosition;
    _generation = pTextService->_CaretGeneration();
  }

  /* ITfEditSession */
  STDMETHODIMP DoEditSession(TfEditCookie ec);

 private:
  com_ptr<ITfContextView> _pContextView;
  com_ptr<ITfComposition> _pComposition;
  bool _enhancedPosition;
  unsigned long long _generation;
};

STDMETHODIMP CGetTextExtentEditSession::DoEditSession(TfEditCookie ec) {
  if (weasel_host::DesktopHasNoTextFocus()) return S_OK;
  if (_generation != _pTextService->_CaretGeneration() ||
      (_pComposition && !_pTextService->_IsCurrentComposition(_pComposition)))
    return S_OK;
  DWORD foreground_process = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &foreground_process);
  if (foreground_process != GetCurrentProcessId()) return S_OK;
  // A queued extent request may run after focus has moved to another field.
  com_ptr<ITfDocumentMgr> focused_document;
  com_ptr<ITfContext> focused_context;
  auto thread_manager = _pTextService->_GetThreadMgr();
  if (!thread_manager ||
      FAILED(thread_manager->GetFocus(&focused_document)) || !focused_document ||
      FAILED(focused_document->GetTop(&focused_context)) ||
      focused_context != _pContext)
    return S_OK;
  com_ptr<ITfRange> pRangeComposition;
  com_ptr<ITfRange> pRange;
  RECT rc = {};
  BOOL fClipped = FALSE;
  TF_SELECTION selection = {};
  ULONG nSelection = 0;

  if (FAILED(_pContext->GetSelection(ec, TF_DEFAULT_SELECTION, 1, &selection,
                                     &nSelection)))
    return E_FAIL;
  if (nSelection != 1 || !selection.range) return S_FALSE;
  com_ptr<ITfRange> selected_range;
  selected_range.Attach(selection.range);

  if (_pComposition != nullptr && _pComposition->GetRange(&pRange) == S_OK) {
    pRange->Collapse(ec, TF_ANCHOR_START);
  } else {
    // composition end
    // note: selection.range is always an empty range
    pRange = selected_range;
    pRange->Collapse(ec, selection.style.ase == TF_AE_START
                             ? TF_ANCHOR_START : TF_ANCHOR_END);
  }

  const HRESULT extent_result =
      _pContextView->GetTextExt(ec, pRange, &rc, &fClipped);
  bool valid_extent = extent_result == S_OK && !fClipped &&
                      RectTouchesMonitor(rc);
  HWND foreground = ::GetForegroundWindow();
  RECT view_rect = {};
  bool valid_view = SUCCEEDED(_pContextView->GetScreenExt(&view_rect)) &&
      view_rect.right > view_rect.left && view_rect.bottom > view_rect.top;
  if (!valid_view && foreground) {
    POINT origin = {};
    valid_view = GetClientRect(foreground, &view_rect) &&
                 ClientToScreen(foreground, &origin);
    if (valid_view) OffsetRect(&view_rect, origin.x, origin.y);
  }
  const bool placeholder = valid_view &&
      weasel_host::PlaceholderExtent(rc, view_rect);
  if (placeholder) valid_extent = false;

  if (valid_extent && _enhancedPosition && foreground) {
    RECT foreground_rect = {};
    if (::GetWindowRect(foreground, &foreground_rect)) {
      // Allow normal non-client borders and shadows, but reject coordinates
      // from a stale/incorrect view. The old fallback mixed a top-level window
      // origin with a child caret coordinate, which could move the popup by
      // hundreds of pixels.
      ::InflateRect(&foreground_rect, 32, 32);
      if (rc.left < foreground_rect.left || rc.left > foreground_rect.right ||
          rc.top < foreground_rect.top || rc.top > foreground_rect.bottom)
        valid_extent = false;
    }
  }

  if (!valid_extent) {
    const LONG preferred_height =
        extent_result == S_OK && rc.bottom > rc.top ? rc.bottom - rc.top : 20;
    RECT caret_rect = {};
    const bool real_caret = GetForegroundCaretRect(foreground, preferred_height, caret_rect) &&
        !(valid_view && weasel_host::PlaceholderExtent(caret_rect, view_rect));
    if (real_caret) {
      rc = caret_rect;
    } else {
      // No usable TSF extent and no real caret.  Fall back to an anchor near
      // the bottom of the host window rather than sending no position at all:
      // the candidate list refuses to show without one, so bailing out here
      // left the user typing blind in self-drawn/WinUI hosts.  A slightly
      // wrong anchor is strictly better than no window; it does not claim to
      // be the real caret.
      RECT anchor_view = view_rect;
      if (!valid_view && foreground && ::GetWindowRect(foreground, &anchor_view) &&
          anchor_view.right > anchor_view.left &&
          anchor_view.bottom > anchor_view.top) {
        valid_view = true;
      }
      if (valid_view && anchor_view.right - anchor_view.left > 200 &&
          anchor_view.bottom - anchor_view.top > 100)
        rc = weasel_host::FallbackAnchor(anchor_view);
      else
        return S_OK;
    }
  }

  // The candidate stays at composition start; the mode tip follows the actual
  // insertion caret. Sharing their anchors made the tip jump with candidate
  // layout and scroll changes.
  RECT caret = rc;
  selected_range->Collapse(ec, selection.style.ase == TF_AE_START
                                   ? TF_ANCHOR_START : TF_ANCHOR_END);
  RECT selection_rect = {};
  BOOL selection_clipped = FALSE;
  if (SUCCEEDED(_pContextView->GetTextExt(ec, selected_range, &selection_rect,
                                         &selection_clipped)) &&
      !selection_clipped && RectTouchesMonitor(selection_rect) &&
      !(valid_view && weasel_host::PlaceholderExtent(selection_rect, view_rect)))
    caret = selection_rect;
  _pTextService->_SetCompositionPosition(rc, caret);
  return S_OK;
}

/* Composition Window Handling */
BOOL WeaselTSF::_UpdateCompositionWindow(com_ptr<ITfContext> pContext) {
  if (!pContext || !_cand) return FALSE;
  ++_caretGeneration;  // latest queued read wins; older layout events are stale
  com_ptr<ITfContextView> pContextView;
  if (pContext->GetActiveView(&pContextView) != S_OK)
    return FALSE;
  com_ptr<CGetTextExtentEditSession> pEditSession;
  pEditSession.Attach(
      new CGetTextExtentEditSession(this, pContext, pContextView, _pComposition,
                                    _cand->style().enhanced_position));
  if (pEditSession == NULL) {
    return FALSE;
  }
  HRESULT hr = E_FAIL;
  const HRESULT request = pContext->RequestEditSession(
      _tfClientId, pEditSession, TF_ES_ASYNCDONTCARE | TF_ES_READ, &hr);
  return SUCCEEDED(request) && SUCCEEDED(hr);
}

void WeaselTSF::_InvalidateCaret() {
  ++_caretGeneration;
  _modeTipTrackUntil = 0;
  _sentPositionValid = false;
  _fTestKeyDownPending = _fTestKeyUpPending = FALSE;
  _testedContext = nullptr;
  if (_cand) _cand->InvalidatePosition();
}

void WeaselTSF::_SetCompositionPosition(const RECT& rc, const RECT& caret) {
  /* Test if rect is valid.
   * If it is invalid during CUAS test, we need to apply CUAS workaround */
  if (!_fCUASWorkaroundTested) {
    _fCUASWorkaroundTested = TRUE;
    if (rc.top == rc.bottom) {
      _fCUASWorkaroundEnabled = TRUE;
      return;
    }
  }
  if (!_sentPositionValid || !EqualRect(&_lastSentPosition, &caret)) {
    m_client.UpdateInputPosition(caret);
    _lastSentPosition = caret;
    _sentPositionValid = m_client.IsConnected();
  }
  _cand->UpdateInputPosition(rc);
}

/* Inline Preedit */
class CInlinePreeditEditSession : public CEditSession {
 public:
  CInlinePreeditEditSession(com_ptr<WeaselTSF> pTextService,
                            com_ptr<ITfContext> pContext,
                            com_ptr<ITfComposition> pComposition,
                            const std::shared_ptr<weasel::Context> context)
      : CEditSession(pTextService, pContext),
        _pComposition(pComposition),
        _context(context) {}

  /* ITfEditSession */
  STDMETHODIMP DoEditSession(TfEditCookie ec);

 private:
  com_ptr<ITfComposition> _pComposition;
  const std::shared_ptr<weasel::Context> _context;
};

STDMETHODIMP CInlinePreeditEditSession::DoEditSession(TfEditCookie ec) {
  std::wstring preedit = _context->preedit.str;

  com_ptr<ITfRange> pRangeComposition;
  if (_pComposition == nullptr)
    return E_FAIL;
  if ((_pComposition->GetRange(&pRangeComposition)) != S_OK)
    return E_FAIL;

  if ((pRangeComposition->SetText(ec, 0, preedit.c_str(),
                                  static_cast<LONG>(preedit.length()))) != S_OK)
    return E_FAIL;

  /* TODO: Check the availability and correctness of these values */
  int sel_cursor = -1;
  for (size_t i = 0; i < _context->preedit.attributes.size(); i++) {
    if (_context->preedit.attributes.at(i).type == weasel::HIGHLIGHTED) {
      sel_cursor = _context->preedit.attributes.at(i).range.cursor;
      break;
    }
  }

  _pTextService->_SetCompositionDisplayAttributes(ec, _pContext,
                                                  pRangeComposition);

  /* Set caret */
  LONG cch;
  TF_SELECTION tfSelection;
  if (sel_cursor < 0) {
    pRangeComposition->Collapse(ec, TF_ANCHOR_END);
  } else {
    pRangeComposition->Collapse(ec, TF_ANCHOR_START);
    pRangeComposition->ShiftStart(ec, sel_cursor, &cch, NULL);
  }
  tfSelection.range = pRangeComposition;
  tfSelection.style.ase = TF_AE_NONE;
  tfSelection.style.fInterimChar = FALSE;
  _pContext->SetSelection(ec, 1, &tfSelection);

  return S_OK;
}

BOOL WeaselTSF::_ShowInlinePreedit(
    com_ptr<ITfContext> pContext,
    const std::shared_ptr<weasel::Context> context) {
  com_ptr<CInlinePreeditEditSession> pEditSession;
  pEditSession.Attach(
      new CInlinePreeditEditSession(this, pContext, _pComposition, context));
  if (pEditSession != NULL) {
    HRESULT hr;
    pContext->RequestEditSession(_tfClientId, pEditSession,
                                 TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr);
  }
  return TRUE;
}

/* Update Composition */
class CInsertTextEditSession : public CEditSession {
 public:
  CInsertTextEditSession(com_ptr<WeaselTSF> pTextService,
                         com_ptr<ITfContext> pContext,
                         com_ptr<ITfComposition> pComposition,
                         const std::wstring& text)
      : CEditSession(pTextService, pContext),
        _text(text),
        _pComposition(pComposition) {}

  /* ITfEditSession */
  STDMETHODIMP DoEditSession(TfEditCookie ec);

 private:
  std::wstring _text;
  com_ptr<ITfComposition> _pComposition;
};

STDMETHODIMP CInsertTextEditSession::DoEditSession(TfEditCookie ec) {
  com_ptr<ITfRange> pRange;
  TF_SELECTION tfSelection;
  HRESULT hRet = S_OK;

  if (_pComposition == nullptr)
    return E_FAIL;
  if (FAILED(_pComposition->GetRange(&pRange)))
    return E_FAIL;

  if (FAILED(pRange->SetText(ec, 0, _text.c_str(),
                             static_cast<LONG>(_text.length()))))
    return E_FAIL;

  /* update the selection to an insertion point just past the inserted text. */
  pRange->Collapse(ec, TF_ANCHOR_END);

  tfSelection.range = pRange;
  tfSelection.style.ase = TF_AE_NONE;
  tfSelection.style.fInterimChar = FALSE;

  _pContext->SetSelection(ec, 1, &tfSelection);

  return hRet;
}

BOOL WeaselTSF::_InsertText(com_ptr<ITfContext> pContext,
                            const std::wstring& text) {
  CInsertTextEditSession* pEditSession;
  HRESULT hr;

  if ((pEditSession = new CInsertTextEditSession(this, pContext, _pComposition,
                                                 text)) != NULL) {
    pContext->RequestEditSession(_tfClientId, pEditSession,
                                 TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr);
    pEditSession->Release();
  }

  return TRUE;
}

void WeaselTSF::_UpdateComposition(com_ptr<ITfContext> pContext) {
  HRESULT hr;

  _pEditSessionContext = pContext;

  _pEditSessionContext->RequestEditSession(
      _tfClientId, this, TF_ES_ASYNCDONTCARE | TF_ES_READWRITE, &hr);
  _async_edit = !!(hr == TF_S_ASYNC);
}

/* Composition State */
STDMETHODIMP WeaselTSF::OnCompositionTerminated(TfEditCookie ecWrite,
                                                ITfComposition* pComposition) {
  // NOTE:
  // This will be called when an edit session ended up with an empty composition
  // string, Even if it is closed normally. Silly M$.

  // EndComposition() may generate this callback for the composition we just
  // closed. Only an active, matching composition is an external termination.
  if (!_IsCurrentComposition(pComposition))
    return S_OK;

  // A host may terminate the empty TSF composition used for a non-inline
  // preedit. Keep Rime's composing state; the next key will create a fresh
  // TSF composition. Only an inactive Rime session should be aborted here.
  if (_status.composing) {
    _FinalizeComposition();
    return S_OK;
  }

  _AbortComposition();
  return S_OK;
}

void WeaselTSF::_AbortComposition(bool clear) {
  m_client.ClearComposition();
  if (_IsComposing()) {
    // _EndComposition 内部直接 pContext->RequestEditSession(...)，不判空。
    // _pEditSessionContext 要到第一次 _UpdateComposition 才被赋值，此前为
    // nullptr；在这种状态下终止组合会解引用空指针。没有上下文时只能丢弃本地
    // 引用：宿主那边的组合范围没有被清掉，但至少不会崩，且 Rime 会话已清空。
    if (_pEditSessionContext != nullptr)
      _EndComposition(_pEditSessionContext, clear);
    else
      _FinalizeComposition();
  }
  _committed = TRUE;
  _cand->Destroy();
}

void WeaselTSF::_FinalizeComposition() {
  _pComposition = nullptr;
}

void WeaselTSF::_SetComposition(com_ptr<ITfComposition> pComposition) {
  _pComposition = pComposition;
}

BOOL WeaselTSF::_IsComposing() {
  return _pComposition != NULL;
}

BOOL WeaselTSF::_IsCurrentComposition(ITfComposition* pComposition) {
  return _pComposition != nullptr && _pComposition == pComposition;
}
