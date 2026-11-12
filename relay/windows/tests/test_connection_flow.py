#!/usr/bin/env python3
"""Deterministic tests for the production retry policy and disconnect command."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
s=(ROOT/'src/main.cpp').read_text()
a=s.index('      if (id == DISCONNECT || id == REST) {')
b=s.index('      if (engine->active())',a)
branch=s[a:b]
code=r'''
#include "connection_retry.hpp"
#include <cassert>
#include <iostream>
#include <vector>
#include <stdexcept>
using namespace relay;
struct Fake {
 bool running=true; int calls=0,cleanups=0,failures=0; std::vector<unsigned> waits;
 std::vector<std::string> statuses; std::string error;
 auto run(){return [this]{return running;};}
 auto status(){return [this](const std::string& s){statuses.push_back(s);};}
 auto wait(){return [this](unsigned ms){waits.push_back(ms);return running;};}
 auto cleanup(){return [this]{++cleanups;};}
 auto fail(){return [this](const std::string& e){++failures;error=e;};}
};
constexpr int DISCONNECT=1,REST=2,IDYES=6,IDNO=7,IDCANCEL=2;
struct Engine {bool online=true;int stops=0;bool rest=false;bool connected(){return online;}void stop(bool r){++stops;rest=r;}};
Engine e;Engine* engine=&e;bool disconnectPromptOpen=false;int answer=IDCANCEL,prompts=0;
int disconnect_choice(){++prompts;assert(disconnectPromptOpen);return answer;}
template<class F>void async(F f){f();}
int route(int id){
BRANCH
 return 1;
}
int main(){
 {Fake f;int wake=0;assert(wake_before_connect(true,f.run(),[]{return false;},[&]{++wake;},f.status(),f.wait()));assert(wake==1&&f.waits==std::vector<unsigned>{15000});}
 {Fake f;int wake=0;assert(wake_before_connect(true,f.run(),[]{return true;},[&]{++wake;},f.status(),f.wait()));assert(!wake&&f.waits.empty());}
 {Fake f;assert(wake_before_connect(false,f.run(),[]{assert(false);return false;},[]{assert(false);},f.status(),f.wait()));assert(f.waits.empty());}
 {Fake f;assert(wake_before_connect(true,f.run(),[]{return false;},[]{throw std::runtime_error("wake failed");},f.status(),f.wait()));assert(f.waits==std::vector<unsigned>{15000});}
 {Fake f;assert(!wake_before_connect(true,f.run(),[]{return false;},[]{},f.status(),[&](unsigned ms){assert(ms==15000);f.running=false;return false;}));}
 {Fake f;connect_with_retries(f.run(),[&]()->ConnectionEnd{++f.calls;assert(f.failures==0);return {false,"not ready"};},f.cleanup(),f.status(),f.wait(),f.fail());assert(f.calls==3&&f.cleanups==3&&f.failures==1);assert((f.waits==std::vector<unsigned>{15000,15000}));assert(f.error.find("after 3 attempts")!=std::string::npos);}
 {Fake f;connect_with_retries(f.run(),[&]()->ConnectionEnd{++f.calls;throw std::runtime_error("start failed");},f.cleanup(),f.status(),f.wait(),f.fail());assert(f.calls==3&&f.cleanups==3&&f.failures==1);}
 {Fake f;connect_with_retries(f.run(),[&]()->ConnectionEnd{++f.calls;return {true,"session ended"};},f.cleanup(),f.status(),f.wait(),f.fail());assert(f.calls==1&&f.waits.empty()&&f.error=="session ended");}
 {Fake f;connect_with_retries(f.run(),[&]()->ConnectionEnd{++f.calls;return {};},f.cleanup(),f.status(),[&](unsigned ms){assert(ms==15000);f.running=false;return false;},f.fail());assert(f.calls==1&&f.cleanups==1&&f.failures==0);}
 {Fake f;f.running=false;connect_with_retries(f.run(),[&]()->ConnectionEnd{++f.calls;return {};},f.cleanup(),f.status(),f.wait(),f.fail());assert(f.calls==0&&f.failures==0);}
 {Fake f;connect_with_retries(f.run(),[&]()->ConnectionEnd{++f.calls;f.running=false;return {};},f.cleanup(),f.status(),f.wait(),f.fail());assert(f.calls==1&&f.cleanups==1&&f.failures==0&&f.waits.empty());}
 answer=IDYES;route(DISCONNECT);assert(e.stops==1&&e.rest&&prompts==1&&!disconnectPromptOpen);
 e={};answer=IDNO;route(DISCONNECT);assert(e.stops==1&&!e.rest);
 e={};answer=IDCANCEL;route(DISCONNECT);assert(e.stops==0);
 e={};e.online=false;int previous=prompts;route(DISCONNECT);assert(e.stops==1&&!e.rest&&prompts==previous);
 e={};route(REST);assert(e.stops==1&&e.rest&&prompts==previous);
 e={};disconnectPromptOpen=true;route(DISCONNECT);assert(e.stops==0&&prompts==previous);
 std::cout<<"Passed: wake/ready/cancel, three attempts, 15-second gaps, startup errors, established disconnect, Rest/leave/cancel choices.\n";
}
'''.replace('BRANCH',branch)
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'flow.cpp';p.write_text(code);exe=Path(tmp)/'flow'
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-I',str(ROOT/'src'),str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
