#include "stdafx.h"
#include "WeaselTSF.h"
#include "CandidateList.h"
#include "ResponseParser.h"

STDMETHODIMP WeaselTSF::DoEditSession(TfEditCookie ec) {
  const bool previous_ascii_mode = _last_ascii_mode;
  const bool had_ascii_mode = _ascii_mode_seen;
  // get commit string from server
  std::wstring commit;
  weasel::Config config;
  auto context = std::make_shared<weasel::Context>();
  weasel::ResponseParser parser(&commit, context.get(), &_status, &config,
                                &_cand->style());

  bool ok = m_client.GetResponseData(std::ref(parser));

  // An unavailable response is not an empty composition.  Keep the last
  // complete frame until a valid response arrives instead of painting an
  // empty context with the previous composing=true status.
  if (!ok)
    return S_OK;

  const bool ascii_mode_changed =
      had_ascii_mode && previous_ascii_mode != _status.ascii_mode;
  if (ascii_mode_changed) _modeTipTrackUntil = GetTickCount64() + 2000;
  _last_ascii_mode = _status.ascii_mode;
  _ascii_mode_seen = true;

  _UpdateLanguageBar(_status);

  bool compositionEnded = false;
  if (ok) {
    compositionEnded = false;
    if (!commit.empty()) {
      // For auto-selecting, commit and preedit can both exist.
      // Commit the old TSF composition. If Rime immediately has a new
      // preedit (top-word input), _EndComposition() drops the local pointer
      // synchronously, so the following state check starts a new TSF
      // composition instead of observing the old one.
      if (!_IsComposing()) {
        _StartComposition(_pEditSessionContext,
                          _fCUASWorkaroundEnabled && !config.inline_preedit,
                          FALSE);
      }
      _InsertText(_pEditSessionContext, commit);
      // Keep the candidate UI alive while the replacement composition is
      // being created; otherwise the key-down path destroys the old window
      // and the new one cannot be positioned until key-up.
      _EndComposition(_pEditSessionContext, false, !_status.composing);
      compositionEnded = true;
      _committed = TRUE;
    } else {
      _committed = FALSE;
    }
    if (_status.composing && (compositionEnded || !_IsComposing())) {
      _StartComposition(_pEditSessionContext,
                        _fCUASWorkaroundEnabled && !config.inline_preedit,
                        TRUE);
    } else if (!_status.composing && _IsComposing()) {
      _EndComposition(_pEditSessionContext, true);
    }
    if (_IsComposing() && config.inline_preedit) {
      _ShowInlinePreedit(_pEditSessionContext, context);
    }
  }

  if (_status.composing)
    _UpdateCompositionWindow(_pEditSessionContext);
  // Keep the existing candidate window alive during top-word input, but
  // publish the new candidates in this key-down edit session. Positioning is
  // still updated by the queued read session after the new composition is
  // created.
  _UpdateUI(*context, _status);
  if (ascii_mode_changed && !_status.composing && _pEditSessionContext)
    _UpdateCompositionWindow(_pEditSessionContext);

  return TRUE;
}
