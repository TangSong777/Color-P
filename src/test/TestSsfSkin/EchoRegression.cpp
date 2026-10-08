#include <WeaselIPC.h>
#include <boost/interprocess/streams/bufferstream.hpp>
#include <boost/thread.hpp>
#include <boost/thread/tss.hpp>
#include <thread>
#include <cstdio>
#include <PipeChannel.h>
#define private public
#include "../../WeaselIPC/WeaselClientImpl.h"
#undef private
struct PipeAccess : weasel::PipeChannelBase {
  static auto nameMember() { return &PipeAccess::pname; }
};
int main() {
  int failures=0;
  for(int mode=0;mode<2;++mode) {
    std::wstring name=L"\\\\.\\pipe\\ColorPEchoTest-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(mode);
    HANDLE pipe=CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX,PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT,1,65536,65536,0,nullptr);
    std::thread server([=]{
      ConnectNamedPipe(pipe,nullptr); weasel::PipeMessage msg{}; DWORD count=0;
      ReadFile(pipe,&msg,sizeof(msg),&count,nullptr);
      if(mode==0) {DWORD response=37;WriteFile(pipe,&response,sizeof(response),&count,nullptr);FlushFileBuffers(pipe);}
      DisconnectNamedPipe(pipe);CloseHandle(pipe);
    });
    weasel::ClientImpl client;client.channel.*PipeAccess::nameMember()=name;client.channel.Connect();client.session_id=37;
    bool ready=client.Echo();
    if(ready!=(mode==0)) ++failures;
    if(mode==1 && client.session_id!=0) ++failures;
    client.session_id=0;client.channel.Disconnect();server.join();
  }
  printf("Real ClientImpl Echo: %d failures\n",failures);return failures?1:0;
}
