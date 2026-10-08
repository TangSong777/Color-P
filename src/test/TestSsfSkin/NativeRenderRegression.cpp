#include <windows.h>
#include <atlbase.h>
#include <atlwin.h>
#include <wtl/atlapp.h>
#include <WeaselUI.h>
#include <cstdio>
CAppModule _Module;
int main() {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  _Module.Init(nullptr, GetModuleHandle(nullptr));
  HWND host=CreateWindowExW(0,L"STATIC",L"render regression",WS_POPUP,0,0,800,400,nullptr,nullptr,GetModuleHandle(nullptr),nullptr);
  DWORD warm=0, finish=0;
  {
    weasel::UI ui;
    auto& s=ui.style();
    s.ssf_enabled=true; s.ssf_skin=L"C:\\ProgramData\\ColorPWeasel\\Color-P"; s.ssf_status_bar=false;
    s.font_face=s.comment_font_face=L"Arial, 宋体"; s.label_font_face=L"宋体";
    s.font_point=s.label_font_point=s.comment_font_point=12;
    s.layout_type=weasel::UIStyle::LAYOUT_HORIZONTAL;
    ui.Create(host);
    weasel::Status status; status.composing=true;
    for(int i=0;i<1200;++i) {
      weasel::Context ctx; ctx.preedit.str=L"ni'hao"+std::to_wstring(i%113);
      for(int j=0;j<5;++j) {ctx.cinfo.candies.emplace_back(L"你好"+std::to_wstring(i%31));ctx.cinfo.comments.emplace_back(L"");ctx.cinfo.labels.emplace_back(std::to_wstring(j+1));}
      ui.Update(ctx,status); RECT pos={100+i%7,100,101+i%7,120}; ui.UpdateInputPosition(pos);
      if(i==199) warm=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    }
    finish=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    ui.Destroy(true);
  }
  DestroyWindow(host); _Module.Term(); CoUninitialize();
  std::printf("1200 native renderer updates: GDI warm=%lu final=%lu; bounded=%s\n",warm,finish,finish<=warm+4?"yes":"no");
  return finish<=warm+4?0:1;
}
