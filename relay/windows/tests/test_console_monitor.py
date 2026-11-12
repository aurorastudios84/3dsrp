#!/usr/bin/env python3
"""Test actual monitor state/filter logic without sockets or a worker thread."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'src/console_monitor.hpp').read_text().replace('#pragma once','').replace('#include <chiaki/discovery.h>','')
# Expose internals for focused state tests; disable only background scheduling.
source=source.replace('class ConsoleMonitor {','class ConsoleMonitor { public:').replace('std::thread worker;','NoThread worker;')
shim=r'''
#include <string>
#include <atomic>
#include <cassert>
#include <iostream>
#include <netdb.h>
#include <arpa/inet.h>
using ULONGLONG=unsigned long long;
ULONGLONG now=200;ULONGLONG GetTickCount64(){return now;}
struct NoThread {template<class F>NoThread(F){} bool joinable(){return false;}void join(){}};
struct Console {std::string id,local;bool ps5=true;};
constexpr int CHIAKI_DISCOVERY_HOST_STATE_UNKNOWN=0,CHIAKI_DISCOVERY_HOST_STATE_READY=1,CHIAKI_DISCOVERY_HOST_STATE_STANDBY=2;
constexpr int CHIAKI_DISCOVERY_PORT_PS5=9302,CHIAKI_DISCOVERY_PORT_PS4=987,CHIAKI_ERR_SUCCESS=0,CHIAKI_DISCOVERY_CMD_SRCH=0;
#define CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS5 "ps5"
#define CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS4 "ps4"
const char* InetNtopA(int f,const void*a,char*b,size_t n){return inet_ntop(f,a,b,n);}
struct ChiakiDiscoveryHost {const char*host_addr;bool ps5;int state;};
bool chiaki_discovery_host_is_ps5(ChiakiDiscoveryHost*h){return h->ps5;}
struct ChiakiLog{};struct ChiakiDiscovery{};struct ChiakiDiscoveryThread{};
struct ChiakiDiscoveryPacket {int cmd;char*protocol_version;};
void chiaki_log_init(ChiakiLog*,int,void*,void*){}
int chiaki_discovery_init(ChiakiDiscovery*,ChiakiLog*,int){return 1;}
void chiaki_discovery_fini(ChiakiDiscovery*){}
int chiaki_discovery_thread_start(ChiakiDiscoveryThread*,ChiakiDiscovery*,void(*)(ChiakiDiscoveryHost*,void*),void*){return 1;}
int chiaki_discovery_send(ChiakiDiscovery*,ChiakiDiscoveryPacket*,sockaddr*,size_t){return 1;}
void chiaki_discovery_thread_stop(ChiakiDiscoveryThread*){}
'''
tests=r'''
int main(){
 relay::ConsoleMonitor monitor;Console a{"first","192.168.1.4",true};
 assert(monitor.state(a)=="Checking...");auto generation=monitor.generation;
 monitor.checked=true;assert(monitor.state(a)=="Status unavailable");
 monitor.observed=CHIAKI_DISCOVERY_HOST_STATE_READY;monitor.observedAt=100;
 assert(monitor.state(a)=="On");
 monitor.observed=CHIAKI_DISCOVERY_HOST_STATE_STANDBY;assert(monitor.state(a)=="Rest mode");
 now=15100;assert(monitor.state(a)=="Status unavailable");
 Console b{"second","192.168.1.5",true};assert(monitor.state(b)=="Checking...");assert(monitor.generation==generation+1&&monitor.observedAt==0);
 assert(monitor.state(Console{})=="Status unavailable");assert(monitor.target.host.empty());
 relay::ConsoleMonitor::Probe probe{"192.168.1.4",true};
 ChiakiDiscoveryHost reply{"192.168.1.5",true,CHIAKI_DISCOVERY_HOST_STATE_STANDBY};
 relay::ConsoleMonitor::reply(&reply,&probe);assert(probe.state==CHIAKI_DISCOVERY_HOST_STATE_UNKNOWN);
 reply.host_addr="192.168.1.4";reply.ps5=false;relay::ConsoleMonitor::reply(&reply,&probe);assert(probe.state==CHIAKI_DISCOVERY_HOST_STATE_UNKNOWN);
 reply.ps5=true;relay::ConsoleMonitor::reply(&reply,&probe);assert(probe.state==CHIAKI_DISCOVERY_HOST_STATE_STANDBY);
 reply.state=CHIAKI_DISCOVERY_HOST_STATE_READY;relay::ConsoleMonitor::reply(&reply,&probe);assert(probe.state==CHIAKI_DISCOVERY_HOST_STATE_READY);
 std::cout<<"Passed: On/Rest mode, stale and missing replies, selection reset, matching address and console type.\n";
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'monitor.cpp';p.write_text(shim+source+tests);exe=Path(tmp)/'monitor'
 subprocess.run(['g++','-std=c++17','-pthread','-Wall','-Wextra',str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
