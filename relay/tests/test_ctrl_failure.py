#!/usr/bin/env python3
"""Classify startup errors and reset between attempts without retaining raw logs."""
from pathlib import Path
import tempfile,subprocess
r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'check.cpp').write_text(r'''
#include "ctrl_failure.hpp"
#include <cassert>
#include <thread>
int main(){
 RelayCtrlFailure f;assert(f.detail().empty());
 f.observe("Ctrl connect failed: Timeout");assert(f.detail()=="TCP connection: timed out");
 f.observe("Ctrl has failed while waiting for ctrl startup");assert(f.detail()=="TCP connection: timed out");
 f.reset();assert(f.detail().empty());
 f.observe("Ctrl connect failed: Connection Refused");assert(f.detail()=="TCP connection: connection refused");
 f.observe("Failed to receive ctrl request response: Disconnected");assert(f.detail()=="control handshake response: console closed connection");
 f.observe("Failed to receive ctrl request response: Timeout");assert(f.detail()=="control handshake response: timed out");
 f.observe("Ctrl http response was not successful. HTTP code was 503");assert(f.detail()=="console rejected control handshake (HTTP 503)");
 f.observe("Failed to parse ctrl request response");assert(f.detail()=="invalid control handshake response");
 f.observe("Failed to send ctrl request");assert(f.detail()=="control handshake request could not be sent");
 f.reset();f.observe("Ctrl http response was not successful. HTTP code was 403");assert(f.http()==403&&f.retryable());
 f.observe("Ctrl application reason: 0x80108b09");assert(f.application()==0x80108b09&&!f.retryable());
 assert(f.detail()=="console rejected control handshake (HTTP 403); console reason 0x80108b09");
 f.reset();assert(f.http()==0&&f.application()==0&&!f.retryable());
 f.observe("Reported Application Reason: 0x80108b15 (Remote Play on Console crashed)");assert(!f.retryable()&&f.application()==0x80108b15);
 f.reset();f.observe("Ctrl http response was not successful. HTTP code was 401");assert(!f.retryable());
 f.reset();f.observe("Ctrl http response was not successful. HTTP code was 503");assert(f.retryable());
 f.reset();f.observe("Failed to parse ctrl request response");assert(!f.retryable());
 f.reset();f.observe("Failed to receive ctrl request response: Timeout");assert(f.retryable());
 f.reset();f.observe("RP-Auth: SECRET");f.observe("Account ID: PRIVATE");f.observe("Ctrl received http header as response");assert(f.detail().empty());
 f.observe("Ctrl connect failed: anything SECRET");assert(f.detail()=="TCP connection: network or protocol error");
 for(int i=0;i<100;i++){f.reset();assert(f.detail().empty());f.observe("Failed to receive ctrl request response: Timeout");assert(f.detail()=="control handshake response: timed out");}
 std::thread writer([&]{for(int i=0;i<1000;i++){f.reset();f.observe("Ctrl connect failed: Timeout");}});
 for(int i=0;i<1000;i++)f.detail();writer.join();
}
''')
 subprocess.run(['c++','-std=c++17','-pthread','-O1','-g','-fsanitize=address,undefined','-I'+str(r/'common'),p/'check.cpp','-o',p/'check'],check=True);subprocess.run([p/'check'],check=True)
print('PASS TCP/handshake/HTTP classifications, per-attempt reset, ignored unrelated/raw text and concurrent access')
