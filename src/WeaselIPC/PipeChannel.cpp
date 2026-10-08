#include "stdafx.h"

#include <PipeChannel.h>

using namespace weasel;
using namespace std;
using namespace boost;

#define _ThrowLastError throw ::GetLastError()
#define _ThrowCode(__c) throw __c
#define _ThrowIfNot(__c)                 \
  {                                      \
    DWORD err;                           \
    if ((err = ::GetLastError()) != __c) \
      throw err;                         \
  }

namespace {
void CloseThreadPipe(HANDLE* pipe) {
  if (pipe && *pipe != INVALID_HANDLE_VALUE) CloseHandle(*pipe);
  delete pipe;
}
}

PipeChannelBase::PipeChannelBase(std::wstring&& pn_cmd,
                                 size_t bs = 4 * 1024,
                                 SECURITY_ATTRIBUTES* s = NULL)
    : pname(pn_cmd), hpipe_ptr(&CloseThreadPipe), buff_size(bs), sa(s) {};

PipeChannelBase::~PipeChannelBase() {
  // Thread-specific pointers are cleaned up automatically
}

bool PipeChannelBase::_Ensure() {
  try {
    HANDLE* phandle = _GetPipeHandle();
    if (_Invalid(*phandle)) {
      *phandle = _Connect(pname.c_str());
      return !_Invalid(*phandle);
    }
  } catch (...) {
    return false;
  }

  return true;
}

HANDLE PipeChannelBase::_Connect(const wchar_t* name) {
  HANDLE pipe = INVALID_HANDLE_VALUE;
  const ULONGLONG deadline = GetTickCount64() + 100;
  while (_Invalid(pipe = _TryConnect())) {
    if (GetTickCount64() >= deadline) throw DWORD(ERROR_TIMEOUT);
    ::WaitNamedPipe(name, 10);
  }
  DWORD mode = PIPE_READMODE_MESSAGE;
  if (!SetNamedPipeHandleState(pipe, &mode, NULL, NULL)) {
    const DWORD error = GetLastError();
    CloseHandle(pipe);
    throw error;
  }
  return pipe;
}

void PipeChannelBase::_Reconnect() {
  HANDLE* phandle = _GetPipeHandle();
  _FinalizePipe(*phandle);
  _Ensure();
}

HANDLE PipeChannelBase::_TryConnect() {
  auto pipe = ::CreateFile(pname.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL,
                           OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
  if (!_Invalid(pipe)) {
    // connected to the pipe
    return pipe;
  }
  // being busy is not really an error since we just need to wait.
  _ThrowIfNot(ERROR_PIPE_BUSY);
  // All pipe instances are busy
  return INVALID_HANDLE_VALUE;
}

bool PipeChannelBase::_Transfer(HANDLE pipe, void* data, DWORD size,
                                DWORD& transferred, bool writing) {
  // Server workers own synchronous handles. Only clients run inside a host's
  // input thread and require cancelable, bounded waits.
  const bool client = pipe == *_GetPipeHandle();
  OVERLAPPED operation = {};
  if (client) {
    operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!operation.hEvent) return false;
  }
  BOOL ok = writing ? WriteFile(pipe, data, size, &transferred,
                                client ? &operation : nullptr)
                    : ReadFile(pipe, data, size, &transferred,
                               client ? &operation : nullptr);
  DWORD error = ok ? ERROR_SUCCESS : GetLastError();
  if (client && !ok && error == ERROR_IO_PENDING) {
    const DWORD wait = WaitForSingleObject(operation.hEvent, 1500);
    if (wait == WAIT_OBJECT_0) {
      ok = GetOverlappedResult(pipe, &operation, &transferred, FALSE);
      error = ok ? ERROR_SUCCESS : GetLastError();
    } else {
      CancelIoEx(pipe, &operation);
      // The OVERLAPPED/buffer must remain alive until local pipe cancellation
      // completes. This is completion cleanup, not another server reply wait.
      GetOverlappedResult(pipe, &operation, &transferred, TRUE);
      ok = FALSE;
      error = ERROR_TIMEOUT;
    }
  }
  if (operation.hEvent) CloseHandle(operation.hEvent);
  SetLastError(error);
  return !!ok;
}

size_t PipeChannelBase::_WritePipe(HANDLE pipe, size_t s, char* b) {
  DWORD lwritten = 0;
  if (!_Transfer(pipe, b, static_cast<DWORD>(s), lwritten, true)) {
    _ThrowLastError;
  }
  if (lwritten != s) throw DWORD(ERROR_WRITE_FAULT);
  return lwritten;
}

void PipeChannelBase::_FinalizePipe(HANDLE& p) {
  if (!_Invalid(p)) {
    DisconnectNamedPipe(p);
    CloseHandle(p);
  }
  p = INVALID_HANDLE_VALUE;
}

void PipeChannelBase::_Receive(HANDLE pipe, LPVOID msg, size_t rec_len) {
  DWORD lread = 0;
  BOOL success = _Transfer(pipe, msg, static_cast<DWORD>(rec_len), lread, false);
  const DWORD header_bytes = lread;
  if (!success) {
    _ThrowIfNot(ERROR_MORE_DATA);

    auto ctx = _GetContext();
    memset(ctx->buffer.get(), 0, buff_size);
    success = _Transfer(pipe, ctx->buffer.get(), static_cast<DWORD>(buff_size), lread, false);
    if (!success) {
      _ThrowLastError;
    }
  }
  else {
    memset(_GetContext()->buffer.get(), 0, buff_size);
  }
  if (header_bytes != rec_len) throw DWORD(ERROR_INVALID_DATA);
  _GetContext()->has_body = false;
}

HANDLE PipeChannelBase::_ConnectServerPipe(std::wstring& pn) {
  HANDLE pipe =
      CreateNamedPipe(pn.c_str(), PIPE_ACCESS_DUPLEX,
                      PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                      PIPE_UNLIMITED_INSTANCES, buff_size, buff_size, 0, sa);
  if (pipe == INVALID_HANDLE_VALUE) _ThrowLastError;
  if (!::ConnectNamedPipe(pipe, NULL) && GetLastError() != ERROR_PIPE_CONNECTED) {
    const DWORD error = GetLastError();
    CloseHandle(pipe);
    throw error;
  }
  return pipe;
}
