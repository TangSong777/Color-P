#include <windows.h>
#include <thread>
#include <cstdio>
#include <string>
#include <PipeChannel.h>

int main() {
  int failures = 0;
  auto scenario = [&](int mode) {
    std::wstring name = L"\\\\.\\pipe\\ColorPRegression-" +
        std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(mode);
    HANDLE pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1, 65536, 65536, 0, nullptr);
    std::thread server([=]() {
      if (!ConnectNamedPipe(pipe, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) return;
      DWORD input = 0, size = 0;
      ReadFile(pipe, &input, sizeof(input), &size, nullptr);
      if (mode == 1) Sleep(1750); // connected server that never responds
      else if (mode == 0) {
        struct Response { DWORD result; wchar_t body[8]; } response{37, L"ok\n"};
        WriteFile(pipe, &response, sizeof(response), &size, nullptr); FlushFileBuffers(pipe);
      } // mode 2 breaks the connection after reading one request
      DisconnectNamedPipe(pipe); CloseHandle(pipe);
    });
    weasel::PipeChannel<DWORD> channel{std::wstring(name)};
    DWORD request = 42;
    const auto start = GetTickCount64();
    bool threw = false;
    try {
      DWORD result = channel.Transact(request);
      if (mode != 0 || result != 37 || std::wstring(channel.ReceivePayload()) != L"ok\n") ++failures;
    } catch (...) {
      threw = true;
      if (mode == 0 || channel.Connected()) ++failures;
    }
    const auto elapsed = GetTickCount64() - start;
    if (elapsed > 1700 || (mode != 0 && !threw)) ++failures;
    channel.Disconnect(); server.join();
    std::printf("Pipe scenario %d: %llu ms, failure=%d\n", mode, elapsed, threw);
  };
  scenario(0); scenario(1); scenario(2);
  // Busy listener must also terminate rather than spinning indefinitely.
  std::wstring busy = L"\\\\.\\pipe\\ColorPBusy-" + std::to_wstring(GetCurrentProcessId());
  HANDLE listener = CreateNamedPipeW(busy.c_str(), PIPE_ACCESS_DUPLEX,
      PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1, 4096, 4096, 0, nullptr);
  HANDLE occupied = CreateFileW(busy.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  weasel::PipeChannel<DWORD> blocked(std::move(busy));
  auto start = GetTickCount64();
  if (blocked.Connect() || GetTickCount64() - start > 300) ++failures;
  CloseHandle(occupied); CloseHandle(listener);
  std::printf("Pipe regression: %d failures\n", failures);
  return failures ? 1 : 0;
}

