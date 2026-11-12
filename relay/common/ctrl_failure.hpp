#pragma once
#include <mutex>
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdint>

/* In-memory classification only. Never retain raw log text, addresses, headers,
 * registration material or payloads. A new session clears the previous cause. */
class RelayCtrlFailure {
 std::mutex lock_;
 std::string cause_;
 int http_=0;uint32_t application_=0;
 static const char *category(const char *message) {
  if(std::strstr(message,"Timeout")||std::strstr(message,"timeout"))return "timed out";
  if(std::strstr(message,"Refused")||std::strstr(message,"refused"))return "connection refused";
  if(std::strstr(message,"Canceled")||std::strstr(message,"canceled"))return "canceled";
  if(std::strstr(message,"Disconnected")||std::strstr(message,"disconnected"))return "console closed connection";
  return "network or protocol error";
 }
public:
 void reset(){std::lock_guard<std::mutex> guard(lock_);cause_.clear();http_=0;application_=0;}
 void observe(const char *message){
  if(!message)return;
  unsigned application=0;
  if(std::sscanf(message,"Ctrl application reason: %x",&application)==1||
     std::sscanf(message,"Reported Application Reason: %x",&application)==1){
   std::lock_guard<std::mutex> guard(lock_);application_=application;return;
  }
  std::string next;int http=0;
  if(std::strncmp(message,"Ctrl connect failed: ",21)==0)next=std::string("TCP connection: ")+category(message);
  else if(std::strncmp(message,"Failed to receive ctrl request response: ",40)==0)next=std::string("control handshake response: ")+category(message);
  else if(std::strcmp(message,"Failed to send ctrl request")==0)next="control handshake request could not be sent";
  else if(std::strcmp(message,"Failed to parse ctrl request response")==0)next="invalid control handshake response";
  else if(std::strcmp(message,"Session ctrl socket creation failed.")==0)next="control socket could not be created";
  else if(std::strcmp(message,"Ctrl got invalid sockaddr")==0)next="unsupported control address type";
  else if(std::strcmp(message,"Ctrl failed to alloc sockaddr")==0)next="control address allocation failed";
  else if(std::strcmp(message,"Ctrl failed to decrypt RP-Server-Type")==0)next="control handshake decryption failed";
  else {
   int code=0;
   if(std::sscanf(message,"Ctrl http response was not successful. HTTP code was %d",&code)==1&&code>=100&&code<=599)
    {next="console rejected control handshake (HTTP "+std::to_string(code)+")";http=code;}
  }
  if(!next.empty()){std::lock_guard<std::mutex> guard(lock_);cause_=next;if(http)http_=http;}
 }
 std::string detail(){
  std::lock_guard<std::mutex> guard(lock_);std::string result=cause_;
  if(application_){char code[16];std::snprintf(code,sizeof(code),"0x%08x",application_);if(!result.empty())result+="; ";result+=std::string("console reason ")+code;}
  return result;
 }
 bool retryable(){
  std::lock_guard<std::mutex> guard(lock_);
  if(application_)return false;
  if(http_)return http_==403||http_==500||http_==502||http_==503||http_==504;
  return cause_=="TCP connection: timed out"||cause_=="TCP connection: connection refused"||
   cause_=="control handshake response: timed out"||cause_=="control handshake response: console closed connection";
 }
 uint32_t application(){std::lock_guard<std::mutex> guard(lock_);return application_;}
 int http(){std::lock_guard<std::mutex> guard(lock_);return http_;}
};
