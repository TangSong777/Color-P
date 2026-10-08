#include "stdafx.h"
#include "WeaselTSF.h"
#include "CandidateList.h"

static BOOL IsRangeCovered(TfEditCookie ec,
                           ITfRange* pRangeTest,
                           ITfRange* pRangeCover) {
  LONG lResult;

  if (pRangeCover->CompareStart(ec, pRangeTest, TF_ANCHOR_START, &lResult) !=
          S_OK ||
      lResult > 0)
    return FALSE;
  if (pRangeCover->CompareEnd(ec, pRangeTest, TF_ANCHOR_END, &lResult) !=
          S_OK ||
      lResult < 0)
    return FALSE;
  return TRUE;
}

STDMETHODIMP WeaselTSF::OnEndEdit(ITfContext* pContext,
                                  TfEditCookie ecReadOnly,
                                  ITfEditRecord* pEditRecord) {
  BOOL fSelectionChanged;
  IEnumTfRanges* pEnumTextChanges;
  ITfRange* pRange;

  /* did the selection change? */
  if (pEditRecord->GetSelectionStatus(&fSelectionChanged) == S_OK &&
      fSelectionChanged) {
    if (_IsComposing()) {
      /* if the caret moves out of composition range, stop the composition */
      TF_SELECTION tfSelection = {};
      ULONG cFetched = 0;

      if (pContext->GetSelection(ecReadOnly, TF_DEFAULT_SELECTION, 1,
                                 &tfSelection, &cFetched) == S_OK &&
          cFetched == 1) {
        ITfRange* pRangeComposition;
        if (_pComposition->GetRange(&pRangeComposition) == S_OK) {
          if (!IsRangeCovered(ecReadOnly, tfSelection.range, pRangeComposition)) {
            // A mouse click moved the caret outside the composition range.
            // Ending only the TSF range hides the UI but leaves Rime's session
            // composing, so a later Space commits stale letters. Abort both
            // sides of the session, matching Sogou's click-away behaviour.
            // Use the callback's context rather than _pEditSessionContext:
            // this notification can race a document-manager switch.
            m_client.ClearComposition();
            _EndComposition(pContext, true);
            _committed = TRUE;
            _cand->Destroy();
          }
          pRangeComposition->Release();
        }
        if (tfSelection.range) tfSelection.range->Release();
      }
    }
  }

  /* text modification? */
  if (pEditRecord->GetTextAndPropertyUpdates(TF_GTP_INCL_TEXT, NULL, 0,
                                             &pEnumTextChanges) == S_OK) {
    if (pEnumTextChanges->Next(1, &pRange, NULL) == S_OK) {
      pRange->Release();
    }
    pEnumTextChanges->Release();
  }
  return S_OK;
}

STDMETHODIMP WeaselTSF::OnLayoutChange(ITfContext* pContext,
                                       TfLayoutCode lcode,
                                       ITfContextView* pContextView) {
  if (pContext != _pTextEditSinkContext)
    return S_OK;

  // The mode-tip image is shown with an empty composition.  Such a context
  // still emits layout changes when the caret moves (for example in Windows
  // Search or after a click), so keep asking TSF for the current selection
  // extent whenever the SSF renderer is active.  This also covers ordinary
  // composing input and avoids leaving the image at the last caret position.
  const bool track_ssf_caret =
      _ShouldTrackIdleCaret() && _cand && _cand->style().ssf_enabled &&
      !_cand->style().ssf_skin.empty();
  if (lcode == TF_LC_CHANGE &&
      ((_IsComposing() && _status.composing) || track_ssf_caret))
    _UpdateCompositionWindow(pContext);
  return S_OK;
}

BOOL WeaselTSF::_InitTextEditSink(com_ptr<ITfDocumentMgr> pDocMgr) {
  _InvalidateCaret();
  com_ptr<ITfSource> pSource;
  BOOL fRet;

  /* clear out any previous sink first */
  if (_dwTextEditSinkCookie != TF_INVALID_COOKIE) {
    _pTextEditSinkContext->QueryInterface(&pSource);
    if (pSource != nullptr) {
      pSource->UnadviseSink(_dwTextEditSinkCookie);
      pSource->UnadviseSink(_dwTextLayoutSinkCookie);
    }
    _pTextEditSinkContext = nullptr;
    _dwTextEditSinkCookie = TF_INVALID_COOKIE;
  }
  if (pDocMgr == NULL)
    return TRUE;

  if (pDocMgr->GetTop(&_pTextEditSinkContext) != S_OK)
    return FALSE;

  if (_pTextEditSinkContext == NULL)
    return TRUE;

  fRet = FALSE;

  pSource.Release();

  if (_pTextEditSinkContext->QueryInterface(IID_ITfSource, (void**)&pSource) ==
      S_OK) {
    if (pSource->AdviseSink(IID_ITfTextEditSink, (ITfTextEditSink*)this,
                            &_dwTextEditSinkCookie) == S_OK)
      fRet = TRUE;
    else
      _dwTextEditSinkCookie = TF_INVALID_COOKIE;
    if (pSource->AdviseSink(IID_ITfTextLayoutSink, (ITfTextLayoutSink*)this,
                            &_dwTextLayoutSinkCookie) == S_OK) {
      fRet = TRUE;
    } else
      _dwTextLayoutSinkCookie = TF_INVALID_COOKIE;
  }
  if (fRet == FALSE) {
    _pTextEditSinkContext = nullptr;
  }

  return fRet;
}
