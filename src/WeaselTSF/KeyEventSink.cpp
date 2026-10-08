#include "stdafx.h"
#include "WeaselIPC.h"
#include "WeaselTSF.h"
#include <KeyEvent.h>
#include "CandidateList.h"
#include "HostCompatibility.h"

void WeaselTSF::_ProcessKeyEvent(WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
  // when _IsKeyboardDisabled don't eat the key,
  // when keyboard closable and keyboard closed, don't eat the key
  if ((_isToOpenClose && !_IsKeyboardOpen()) || _IsKeyboardDisabled()) {
    _InvalidateCaret();
    if (_IsComposing() || _status.composing) _AbortComposition();
    *pfEaten = FALSE;
    return;
  }

  // This is normally already ready from profile activation.  If a service was
  // restarted in the background, wait briefly here rather than leaking the
  // first pinyin letters into the application as ASCII.
  // Windows retains following keystrokes in the thread queue while this first
  // event waits. Do not dispatch/replay it until a session exists. Restrict the
  // cold-start grace to plain letter-down events; shortcuts never wait here.
  const bool firstLetter = wParam >= 'A' && wParam <= 'Z' &&
      !(lParam & (1LL << 31)) && !(GetKeyState(VK_CONTROL) & 0x8000) &&
      !(GetKeyState(VK_MENU) & 0x8000) && !(GetKeyState(VK_LWIN) & 0x8000) &&
      !(GetKeyState(VK_RWIN) & 0x8000);
  if (!_EnsureServerConnected(firstLetter ? 1500 : 100)) {
    *pfEaten = FALSE;
    return;
  }
  weasel::KeyEvent ke;
  GetKeyboardState(_lpbKeyState);
  if (!ConvertKeyEvent(static_cast<UINT>(wParam), lParam, _lpbKeyState, ke)) {
    /* Unknown key event */
    *pfEaten = FALSE;
  } else {
    // cheet key code when vertical auto reverse happened, swap up and down
    if (_cand->GetIsReposition()) {
      if (ke.keycode == ibus::Up)
        ke.keycode = ibus::Down;
      else if (ke.keycode == ibus::Down)
        ke.keycode = ibus::Up;
    }
    if (!keyCountToSimulate)
      *pfEaten = (BOOL)m_client.ProcessKeyEvent(ke);

    if (ke.keycode == ibus::Caps_Lock) {
      if (prevKeyEvent.keycode == ibus::Caps_Lock && prevfEaten == TRUE &&
          (ke.mask & ibus::RELEASE_MASK) && (!keyCountToSimulate)) {
        if ((GetKeyState(VK_CAPITAL) & 0x01)) {
          if (_committed || (!*pfEaten && _status.composing)) {
            keyCountToSimulate = 2;
            INPUT inputs[2];
            inputs[0].type = INPUT_KEYBOARD;
            inputs[0].ki = {VK_CAPITAL, 0, 0, 0, 0};
            inputs[1].type = INPUT_KEYBOARD;
            inputs[1].ki = {VK_CAPITAL, 0, KEYEVENTF_KEYUP, 0, 0};
            ::SendInput(sizeof(inputs) / sizeof(INPUT), inputs, sizeof(INPUT));
          }
        }
        *pfEaten = TRUE;
      }
      if (keyCountToSimulate)
        keyCountToSimulate--;
    }

    prevfEaten = *pfEaten;
    prevKeyEvent = ke;
  }
}

STDMETHODIMP WeaselTSF::OnSetFocus(BOOL fForeground) {
  if (fForeground && !_IsKeyboardDisabled()) {
    m_client.FocusIn();
    if (_pTextEditSinkContext && !_IsKeyboardDisabled())
      _UpdateCompositionWindow(_pTextEditSinkContext);
  } else {
    _InvalidateCaret();
    m_client.FocusOut();
    _AbortComposition();
  }

  return S_OK;
}

/* Some apps sends strange OnTestKeyDown/OnKeyDown combinations:
 *  Some sends OnKeyDown() only. (QQ2012)
 *  Some sends multiple OnTestKeyDown() for a single key event. (MS WORD 2010
 * x64)
 *
 * We assume every key event will eventually cause a OnKeyDown() call.
 * We use _fTestKeyDownPending to omit multiple OnTestKeyDown() calls,
 *  and for OnKeyDown() to check if the key has already been sent to the server.
 */

STDMETHODIMP WeaselTSF::OnTestKeyDown(ITfContext* pContext,
                                      WPARAM wParam,
                                      LPARAM lParam,
                                      BOOL* pfEaten) {
  _fTestKeyUpPending = FALSE;
  if (!_IsKeyboardDisabled() && _fTestKeyDownPending && _testedKey == wParam && _testedFlags == lParam &&
      _testedContext == pContext && _testedTime == GetMessageTime()) {
    *pfEaten = TRUE;
    return S_OK;
  }
  _ProcessKeyEvent(wParam, lParam, pfEaten);
  _UpdateComposition(pContext);
  _testedKey = wParam; _testedFlags = lParam;
  _testedContext = pContext; _testedTime = GetMessageTime();
  _fTestKeyDownPending = FALSE;
  if (*pfEaten)
    _fTestKeyDownPending = TRUE;
  return S_OK;
}

STDMETHODIMP WeaselTSF::OnKeyDown(ITfContext* pContext,
                                  WPARAM wParam,
                                  LPARAM lParam,
                                  BOOL* pfEaten) {
  _fTestKeyUpPending = FALSE;
  if (!_IsKeyboardDisabled() && _fTestKeyDownPending && weasel_host::SamePendingKey(
          _testedKey, _testedFlags, _testedContext, wParam, lParam, pContext)) {
    _fTestKeyDownPending = FALSE;
    *pfEaten = TRUE;
  } else {
    _fTestKeyDownPending = FALSE;
    _ProcessKeyEvent(wParam, lParam, pfEaten);
    _UpdateComposition(pContext);
  }
  return S_OK;
}

STDMETHODIMP WeaselTSF::OnTestKeyUp(ITfContext* pContext,
                                    WPARAM wParam,
                                    LPARAM lParam,
                                    BOOL* pfEaten) {
  _fTestKeyDownPending = FALSE;
  if (!_IsKeyboardDisabled() && _fTestKeyUpPending && _testedKey == wParam && _testedFlags == lParam &&
      _testedContext == pContext && _testedTime == GetMessageTime()) {
    *pfEaten = TRUE;
    return S_OK;
  }
  _ProcessKeyEvent(wParam, lParam, pfEaten);
  _UpdateComposition(pContext);
  _testedKey = wParam; _testedFlags = lParam;
  _testedContext = pContext; _testedTime = GetMessageTime();
  _fTestKeyUpPending = FALSE;
  if (*pfEaten)
    _fTestKeyUpPending = TRUE;
  return S_OK;
}

STDMETHODIMP WeaselTSF::OnKeyUp(ITfContext* pContext,
                                WPARAM wParam,
                                LPARAM lParam,
                                BOOL* pfEaten) {
  _fTestKeyDownPending = FALSE;
  if (!_IsKeyboardDisabled() && _fTestKeyUpPending && weasel_host::SamePendingKey(
          _testedKey, _testedFlags, _testedContext, wParam, lParam, pContext)) {
    _fTestKeyUpPending = FALSE;
    *pfEaten = TRUE;
  } else {
    _fTestKeyUpPending = FALSE;
    _ProcessKeyEvent(wParam, lParam, pfEaten);
    if (!_async_edit)
      _UpdateComposition(pContext);
  }
  return S_OK;
}

STDMETHODIMP WeaselTSF::OnPreservedKey(ITfContext* pContext,
                                       REFGUID rguid,
                                       BOOL* pfEaten) {
  *pfEaten = FALSE;
  return S_OK;
}

BOOL WeaselTSF::_InitKeyEventSink() {
  com_ptr<ITfKeystrokeMgr> pKeystrokeMgr;
  HRESULT hr;

  if (_pThreadMgr->QueryInterface(&pKeystrokeMgr) != S_OK)
    return FALSE;

  hr = pKeystrokeMgr->AdviseKeyEventSink(_tfClientId, (ITfKeyEventSink*)this,
                                         TRUE);

  return (hr == S_OK);
}

void WeaselTSF::_UninitKeyEventSink() {
  com_ptr<ITfKeystrokeMgr> pKeystrokeMgr;

  if (_pThreadMgr->QueryInterface(&pKeystrokeMgr) != S_OK)
    return;

  pKeystrokeMgr->UnadviseKeyEventSink(_tfClientId);
}

BOOL WeaselTSF::_InitPreservedKey() {
  return TRUE;
#if 0
	com_ptr<ITfKeystrokeMgr> pKeystrokeMgr;
	if (_pThreadMgr->QueryInterface(pKeystrokeMgr.GetAddressOf()) != S_OK)
	{
		return FALSE;
	}
	TF_PRESERVEDKEY preservedKeyImeMode;

	/* Define SHIFT ONLY for now */
	preservedKeyImeMode.uVKey = VK_SHIFT;
	preservedKeyImeMode.uModifiers = TF_MOD_ON_KEYUP;

	auto hr = pKeystrokeMgr->PreserveKey(
		_tfClientId,
		GUID_IME_MODE_PRESERVED_KEY,
		&preservedKeyImeMode, L"", 0);
	
	return SUCCEEDED(hr);
#endif
}

void WeaselTSF::_UninitPreservedKey() {}
