#!/usr/bin/env python3
"""Exercise the actual editor procedure with a small host-side Win32 message shim.
Checks command routing and PIN state; this is not a Windows rendering test.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'src/main.cpp').read_text()
editor = source[source.index('enum EditorControlId {'):source.index('std::string pendingPin')]
shim = r'''
#include <array>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <cassert>
#include <iostream>
#include <cstdint>
using UINT=unsigned; using WPARAM=uintptr_t; using LPARAM=intptr_t;
using LRESULT=intptr_t; using LONG_PTR=intptr_t;
struct Widget { std::wstring text; LONG_PTR data=0; int selected=0; bool checked=false,readonly=false,visible=true,destroyed=false; };
using HWND=Widget*;
#define CALLBACK
#define LOWORD(x) ((unsigned)(x)&65535)
#define HIWORD(x) (((unsigned)(x)>>16)&65535)
constexpr int WM_NCCREATE=129,WM_CREATE=1,WM_COMMAND=273,WM_CLOSE=16;
constexpr int GWLP_USERDATA=0,WM_SETFONT=48,CB_SETCURSEL=334,EM_SETLIMITTEXT=197,EM_SETREADONLY=207;
constexpr int BM_GETCHECK=240,BST_CHECKED=1,SW_SHOW=5,SW_HIDE=0;
constexpr int WS_TABSTOP=1,ES_AUTOHSCROLL=2,ES_NUMBER=4,BS_AUTOCHECKBOX=8;
constexpr int BN_CLICKED=0,IDOK=1,IDCANCEL=2,MB_OK=0,MB_ICONERROR=16,TRUE=1;
int font=0,titleFont=0;
struct CREATESTRUCTW { void* lpCreateParams; };
struct Console {std::string id,name,local,remote,account; bool ps5=true;};
std::vector<std::unique_ptr<Widget>> widgets;
std::vector<std::wstring> errors;
LRESULT editor_proc(HWND,UINT,WPARAM,LPARAM);
LONG_PTR GetWindowLongPtrW(HWND w,int){return w->data;}
void SetWindowLongPtrW(HWND w,int,LONG_PTR d){w->data=d;}
LRESULT DefWindowProcW(HWND,UINT,WPARAM,LPARAM){return 0;}
void DestroyWindow(HWND w){w->destroyed=true;}
void ShowWindow(HWND w,int state){w->visible=state!=SW_HIDE;}
void SetFocus(HWND){}
void SetWindowTextW(HWND w,const wchar_t* s){w->text=s;}
LRESULT SendMessageW(HWND w,UINT m,WPARAM a,LPARAM){
 if(m==CB_SETCURSEL)w->selected=a;
 if(m==EM_SETREADONLY)w->readonly=a;
 if(m==BM_GETCHECK)return w->checked?BST_CHECKED:0;
 return 0;
}
int MessageBoxW(HWND,const wchar_t* s,const wchar_t*,int){errors.push_back(s);return 0;}
std::wstring wide(const std::string& s){return {s.begin(),s.end()};}
std::string utf8(const std::wstring& s){return {s.begin(),s.end()};}
std::wstring text(HWND w){assert(w);return w->text;}
int selection(HWND w){return w->selected;}
bool account_id(const std::string& s,std::array<uint8_t,8>& out){
 if(s=="account-a"||s=="equivalent-a"){out[0]=1;return true;}
 if(s=="account-b"){out[0]=2;return true;}return false;
}
HWND control(const wchar_t* kind,const wchar_t* value,int id,int,int,int,int,int=0,HWND parent=nullptr){
 widgets.push_back(std::make_unique<Widget>());HWND w=widgets.back().get();w->text=value;
 // EDIT controls notify their parent synchronously while being initialized.
 if(std::wstring(kind)==L"EDIT" && parent){
  editor_proc(parent,WM_COMMAND,id|(0x400u<<16),(LPARAM)w);
  editor_proc(parent,WM_COMMAND,id|(0x300u<<16),(LPARAM)w);
 }
 return w;
}
HWND label(const wchar_t* s,int x,int y,int width,int height=22,HWND p=nullptr){return control(L"STATIC",s,0,x,y,width,height,0,p);}
HWND combo(int id,int x,int y,int width,const std::vector<std::wstring>&,HWND p=nullptr){return control(L"COMBOBOX",L"",id,x,y,width,220,0,p);}
HWND button(const wchar_t* s,int id,int x,int y,int width=130,HWND p=nullptr){return control(L"BUTTON",s,id,x,y,width,30,0,p);}
'''
tests = r'''
void open(Editor& e,Widget& window,bool paired){
 e.paired=paired;e.console.name="My PlayStation";e.console.local="192.168.1.4";e.console.account="account-a";
 CREATESTRUCTW create{&e};editor_proc(&window,WM_NCCREATE,0,(LPARAM)&create);
 errors.clear();editor_proc(&window,WM_CREATE,0,0);
 assert(errors.empty()&&!window.destroyed&&!e.accepted);
}
void command(Widget& w,int id,int notification=0){editor_proc(&w,WM_COMMAND,id|(notification<<16),0);}
int main(){
 {Editor e;Widget w;open(e,w,false);assert(!e.pin->readonly&&!e.repair->visible);
  command(w,EDITOR_TYPE,1);command(w,EDITOR_NAME,0x300);assert(!w.destroyed&&errors.empty());
  e.local->text=L"";command(w,IDOK);assert(errors.size()==1&&!e.accepted);
  e.local->text=L"192.168.1.4";e.pin->text=L"12345678";command(w,IDOK);assert(e.accepted&&e.code=="12345678");}
 {Editor e;Widget w;open(e,w,true);assert(e.pin->readonly&&e.repair->visible);
  command(w,IDOK);assert(e.accepted&&e.code.empty());}
 {Editor e;Widget w;open(e,w,true);e.repair->checked=true;command(w,EDITOR_REPAIR);assert(!e.pin->readonly);
  command(w,IDOK);assert(errors.size()==1&&!e.accepted);
  e.pin->text=L"1234567";command(w,IDOK);assert(errors.size()==2&&!e.accepted);
  e.pin->text=L"12345678";command(w,IDOK);assert(e.accepted&&e.code=="12345678");}
 {Editor e;Widget w;open(e,w,true);e.repair->checked=true;command(w,EDITOR_REPAIR);e.pin->text=L"12345678";
  e.repair->checked=false;command(w,EDITOR_REPAIR);assert(e.pin->readonly&&e.pin->text.empty());}
 {Editor e;Widget w;open(e,w,true);e.account->text=L"account-b";command(w,IDOK);assert(!e.accepted&&errors.size()==1);}
 {Editor e;Widget w;open(e,w,true);e.type->selected=1;command(w,IDOK);assert(!e.accepted&&errors.size()==1);}
 {Editor e;Widget w;open(e,w,true);e.account->text=L"equivalent-a";command(w,IDOK);assert(e.accepted&&errors.empty());}
 {Editor e;Widget w;open(e,w,true);command(w,IDCANCEL);assert(w.destroyed&&!e.accepted&&errors.empty());}
 std::cout<<"Editor regression checks passed: initialization, Save/Cancel, paired PIN lock, re-pair, identity changes.\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path=Path(tmp)/'editor.cpp'; path.write_text(shim+'\n'+editor+'\n'+tests)
    binary=Path(tmp)/'editor-test'
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra',str(path),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
