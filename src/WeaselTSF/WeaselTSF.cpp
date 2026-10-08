#include "stdafx.h"

#include <WeaselIPCData.h>
#include <thread>
#include <shellapi.h>
#include <tlhelp32.h>
#include "WeaselTSF.h"
#include <WeaselModeDebug.h>
#include "CandidateList.h"
#include "LanguageBar.h"
#include "Compartment.h"
#include "ResponseParser.h"

static void error_message(const WCHAR* msg) {
  static DWORD next_tick = 0;
  DWORD now = GetTickCount();
  if (now > next_tick) {
    next_tick = now + 10000;  // (ms)
    MessageBox(NULL, msg, get_weasel_ime_name().c_str(), MB_ICONERROR | MB_OK);
  }
}

WeaselTSF::WeaselTSF() {
  _cRef = 1;

  _dwThreadMgrEventSinkCookie = TF_INVALID_COOKIE;

  _dwTextEditSinkCookie = TF_INVALID_COOKIE;
  _dwTextLayoutSinkCookie = TF_INVALID_COOKIE;
  _dwThreadFocusSinkCookie = TF_INVALID_COOKIE;
  _fTestKeyDownPending = FALSE;
  _fTestKeyUpPending = FALSE;

  _fCUASWorkaroundTested = _fCUASWorkaroundEnabled = FALSE;

  _cand = new CCandidateList(this);

  DllAddRef();
}

WeaselTSF::~WeaselTSF() {
  DllRelease();
}

STDMETHODIMP WeaselTSF::QueryInterface(REFIID riid, void** ppvObject) {
  if (ppvObject == NULL)
    return E_INVALIDARG;

  *ppvObject = NULL;

  if (IsEqualIID(riid, IID_IUnknown) ||
      IsEqualIID(riid, IID_ITfTextInputProcessor))
    *ppvObject = (ITfTextInputProcessor*)this;
  else if (IsEqualIID(riid, IID_ITfTextInputProcessorEx))
    *ppvObject = (ITfTextInputProcessorEx*)this;
  else if (IsEqualIID(riid, IID_ITfThreadMgrEventSink))
    *ppvObject = (ITfThreadMgrEventSink*)this;
  else if (IsEqualIID(riid, IID_ITfTextEditSink))
    *ppvObject = (ITfTextEditSink*)this;
  else if (IsEqualIID(riid, IID_ITfTextLayoutSink))
    *ppvObject = (ITfTextLayoutSink*)this;
  else if (IsEqualIID(riid, IID_ITfKeyEventSink))
    *ppvObject = (ITfKeyEventSink*)this;
  else if (IsEqualIID(riid, IID_ITfCompositionSink))
    *ppvObject = (ITfCompositionSink*)this;
  else if (IsEqualIID(riid, IID_ITfEditSession))
    *ppvObject = (ITfEditSession*)this;
  else if (IsEqualIID(riid, IID_ITfThreadFocusSink))
    *ppvObject = (ITfThreadFocusSink*)this;
  else if (IsEqualIID(riid, IID_ITfDisplayAttributeProvider))
    *ppvObject = (ITfDisplayAttributeProvider*)this;

  if (*ppvObject) {
    AddRef();
    return S_OK;
  }
  return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) WeaselTSF::AddRef() {
  return ++_cRef;
}

STDMETHODIMP_(ULONG) WeaselTSF::Release() {
  LONG cr = --_cRef;

  assert(_cRef >= 0);

  if (_cRef == 0)
    delete this;

  return cr;
}

STDMETHODIMP WeaselTSF::Activate(ITfThreadMgr* pThreadMgr,
                                 TfClientId tfClientId) {
  return ActivateEx(pThreadMgr, tfClientId, 0U);
}

STDMETHODIMP WeaselTSF::Deactivate() {
  m_client.EndSession();

  _InitTextEditSink(com_ptr<ITfDocumentMgr>());

  _UninitThreadMgrEventSink();

  _UninitKeyEventSink();
  _UninitPreservedKey();

  _UninitLanguageBar();

  _UninitCompartment();

  _UninitThreadMgrEventSink();

  _pThreadMgr = NULL;

  _tfClientId = TF_CLIENTID_NULL;

  _cand->DestroyAll();

  return S_OK;
}

STDMETHODIMP WeaselTSF::ActivateEx(ITfThreadMgr* pThreadMgr,
                                   TfClientId tfClientId,
                                   DWORD dwFlags) {
  com_ptr<ITfDocumentMgr> pDocMgrFocus;
  _activateFlags = dwFlags;

  _pThreadMgr = pThreadMgr;
  _tfClientId = tfClientId;

  if (!_InitThreadMgrEventSink())
    goto ExitError;

  if ((_pThreadMgr->GetFocus(&pDocMgrFocus) == S_OK) &&
      (pDocMgrFocus != NULL)) {
    _InitTextEditSink(pDocMgrFocus);
  }

  if (!_InitKeyEventSink())
    goto ExitError;

  // if (!_InitDisplayAttributeGuidAtom())
  //	goto ExitError;
  //	some app might init failed because it not provide DisplayAttributeInfo,
  // like some opengl stuff
  _InitDisplayAttributeGuidAtom();

  if (!_InitPreservedKey())
    goto ExitError;

  if (!_InitLanguageBar())
    goto ExitError;

  if (!_IsKeyboardOpen())
    _SetKeyboardOpen(TRUE);

  if (!_InitCompartment())
    goto ExitError;
  if (!_InitThreadFocusSink())
    goto ExitError;

  // Eagerly start and initialise the engine before the user can type.  The
  // older lazy/retry path waited for several key events, leaking those keys as
  // plain English into the target application.
  _EnsureServerConnected(100);

  return S_OK;

ExitError:
  Deactivate();
  return E_FAIL;
}

STDMETHODIMP WeaselTSF::OnSetThreadFocus() {
  ModeDbg(L"[MODEDBG-TSF] OnSetThreadFocus");
  std::wstring _ToggleImeOnOpenClose{};
  RegGetStringValue(HKEY_CURRENT_USER, L"Software\\Rime\\weasel",
                    L"ToggleImeOnOpenClose", _ToggleImeOnOpenClose);
  _isToOpenClose = (_ToggleImeOnOpenClose == L"yes");
  _EnsureServerConnected(50);
  if (m_client.Echo()) {
    m_client.ProcessKeyEvent(0);
    weasel::ResponseParser parser(NULL, NULL, &_status, NULL, &_cand->style());
    bool ok = m_client.GetResponseData(std::ref(parser));
    if (ok)
      _UpdateLanguageBar(_status);
  }
  return S_OK;
}
STDMETHODIMP WeaselTSF::OnKillThreadFocus() {
  ModeDbg(L"[MODEDBG-TSF] OnKillThreadFocus");
  _AbortComposition();
  m_client.FocusOut();
  return S_OK;
}
BOOL WeaselTSF::_InitThreadFocusSink() {
  com_ptr<ITfSource> pSource;
  if (FAILED(_pThreadMgr->QueryInterface(&pSource)))
    return FALSE;
  if (FAILED(pSource->AdviseSink(IID_ITfThreadFocusSink,
                                 (ITfThreadFocusSink*)this,
                                 &_dwThreadFocusSinkCookie)))
    return FALSE;
  return TRUE;
}
void WeaselTSF::_UninitThreadFocusSink() {
  com_ptr<ITfSource> pSource;
  if (FAILED(_pThreadMgr->QueryInterface(&pSource)))
    return;
  if (FAILED(pSource->UnadviseSink(_dwThreadFocusSinkCookie)))
    return;
}

STDMETHODIMP WeaselTSF::OnActivated(REFCLSID clsid,
                                    REFGUID guidProfile,
                                    BOOL isActivated) {
  if (!IsEqualCLSID(clsid, c_clsidTextService)) {
    return S_OK;
  }

  if (isActivated) {
    DebugStream() << L"[MODEDBG] OnActivated(TRUE): resetting to Chinese on the "
                     L"server side\n";
    // Language-profile activation is the normal path when the user switches
    // to Weasel.  Have the service ready before the next physical key event.
    if (_EnsureServerConnected(100)) {
      m_client.FocusIn(0x80000000u);
      m_client.ProcessKeyEvent(0);
      weasel::ResponseParser parser(NULL, NULL, &_status, NULL, &_cand->style());
      m_client.GetResponseData(std::ref(parser));
      DebugStream() << L"[MODEDBG] OnActivated(TRUE): server reported "
                       L"ascii_mode="
                    << (_status.ascii_mode ? 1 : 0) << L"\n";
    } else {
      DebugStream() << L"[MODEDBG] OnActivated(TRUE): server not connected, "
                       L"reset skipped\n";
    }
    _ShowLanguageBar(TRUE);
    _UpdateLanguageBar(_status);
  } else {
    DebugStream() << L"[MODEDBG] OnActivated(FALSE): leaving Weasel\n";
    m_client.FocusOut(0x80000000u);
    _DeleteCandidateList();
    _ShowLanguageBar(FALSE);
  }
  return S_OK;
}

void WeaselTSF::_Reconnect() {
  m_client.Disconnect();
  m_client.Connect(NULL);
  m_client.StartSession();
  weasel::ResponseParser parser(NULL, NULL, &_status, NULL, &_cand->style());
  bool ok = m_client.GetResponseData(std::ref(parser));
  if (ok) {
    _UpdateLanguageBar(_status);
  }
}

namespace {

bool WeaselServerIsRunning() {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return false;
  PROCESSENTRY32 process = {};
  process.dwSize = sizeof(process);
  bool found = false;
  if (Process32First(snapshot, &process)) {
    do {
      if (_wcsicmp(process.szExeFile, L"WeaselServer.exe") == 0) {
        found = true;
        break;
      }
    } while (Process32Next(snapshot, &process));
  }
  CloseHandle(snapshot);
  return found;
}

void StartWeaselServerIfNeeded(const std::wstring& root_dir) {
  HANDLE mutex = CreateMutexW(nullptr, TRUE, L"WeaselServerStartupMutex");
  if (!mutex) return;
  const bool owns_mutex = GetLastError() != ERROR_ALREADY_EXISTS;
  if (owns_mutex && !WeaselServerIsRunning()) {
    const std::wstring server = root_dir + L"\\WeaselServer.exe";
    ShellExecuteW(nullptr, L"open", server.c_str(), nullptr,
                  root_dir.c_str(), SW_HIDE);
  }
  if (owns_mutex) ReleaseMutex(mutex);
  CloseHandle(mutex);
}

}  // namespace

bool WeaselTSF::_EnsureServerConnected(DWORD wait_ms) {
  // Actual transactions validate the connection and invalidate it on failure.
  // Do not double IPC traffic with an Echo on every key down and key up.
  const ULONGLONG now = GetTickCount64();
  if (m_client.IsConnected() && now - _connectionCheckedAt < 1000) return true;
  if (m_client.Echo()) { _connectionCheckedAt = now; return true; }

  _Reconnect();
  if (m_client.Echo()) { _connectionCheckedAt = GetTickCount64(); return true; }

  // Start immediately on the first failed connection rather than after six
  // leaked keystrokes.  During profile activation this work is deliberately
  // completed before focus returns to the application.
  StartWeaselServerIfNeeded(_GetRootDir());

  const DWORD started_at = GetTickCount();
  do {
    _Reconnect();
    if (m_client.Echo()) { _connectionCheckedAt = GetTickCount64(); return true; }
    if (wait_ms == 0 || GetTickCount() - started_at >= wait_ms) break;
    Sleep(25);
  } while (true);

  return false;
}

