package com.aurora.relay;

import java.net.URL;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.time.LocalDate;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import javax.net.ssl.HttpsURLConnection;

/** HTTPS delivery only. Consent, identity, accounting and durable recovery belong to the handheld. */
final class UsageProxy implements AutoCloseable {
 interface Transport { int post(String json) throws Exception; }
 interface Reply { void ack(int run,int serial,int outcome); }
 static final String ENDPOINT="https://3dsrp-usage.aurorastudios84.workers.dev/v3/session";
 private final Transport transport;
 private final Reply reply;
 private final ThreadPoolExecutor executor=new ThreadPoolExecutor(1,1,10,TimeUnit.SECONDS,new ArrayBlockingQueue<>(8),r->{Thread t=new Thread(r,"usage-delivery");t.setDaemon(true);t.setPriority(Thread.MIN_PRIORITY);return t;});
 private final Map<Integer,Job> jobs=new HashMap<>();
 private int run;private boolean closed;
 volatile String deliveryStatus="Waiting for a usage report";
 volatile long acknowledged;
 UsageProxy(Reply reply){this(UsageProxy::https,reply);}
 UsageProxy(Transport transport,Reply reply){this.transport=transport;this.reply=reply;executor.allowCoreThreadTimeOut(true);}
 synchronized void reset(int nextRun){if(!jobs.isEmpty())UsageDelivery.result(0,closed?"Relay stopped before confirmation":"Relay connection changed before confirmation");for(Job job:jobs.values())job.cancelled=true;jobs.clear();executor.getQueue().clear();run=nextRun;}
 static boolean uuid(String s){return s.matches("[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}");}
 static String validationError(byte[] packet,int n,int run){
  if(n!=Protocol.HEADER+96||!Protocol.valid(packet,n))return "packet size or header";
  ByteBuffer b=ByteBuffer.wrap(packet);
  if(b.getInt(20)!=96||b.getShort(16)!=0||b.getShort(18)!=1)return "fragment header";
  if(b.getInt(32)!=run)return "handheld run does not match";
  if(packet[118]!=1||packet[119]!=0)return "report format version";
  String id=new String(packet,36,36,StandardCharsets.US_ASCII),session=new String(packet,72,36,StandardCharsets.US_ASCII),day=new String(packet,108,10,StandardCharsets.US_ASCII);
  if(!uuid(id)||!uuid(session))return "invalid anonymous ID";
  long seconds=Integer.toUnsignedLong(b.getInt(120)),generation=Integer.toUnsignedLong(b.getInt(124));
  if(seconds>86400||generation>0xffffff)return "duration or consent generation";
  if(!day.matches("[0-9]{4}-[0-9]{2}-[0-9]{2}"))return "activity date";
  try{LocalDate.parse(day);}catch(Exception ex){return "activity date";}
  return "";
 }
 static String body(byte[] packet,int n,int run){
  if(!validationError(packet,n,run).isEmpty())return null;
  ByteBuffer b=ByteBuffer.wrap(packet);
  String id=new String(packet,36,36,StandardCharsets.US_ASCII),session=new String(packet,72,36,StandardCharsets.US_ASCII),day=new String(packet,108,10,StandardCharsets.US_ASCII);
  long seconds=Integer.toUnsignedLong(b.getInt(120));
  return "{\"app\":\"original_3ds\",\"installation_id\":\""+id+"\",\"session_id\":\""+session+"\",\"day\":\""+day+"\",\"seconds\":"+seconds+"}";
 }
 synchronized void report(byte[] packet,int n){
  if(closed)return;UsageDelivery.received();String reason=validationError(packet,n,run);if(!reason.isEmpty()){deliveryStatus="Report rejected: "+reason;UsageDelivery.rejected(reason);return;}
  String json=body(packet,n,run);
  int serial=ByteBuffer.wrap(packet).getInt(28);if(serial==0||jobs.containsKey(serial))return;
  Job job=new Job(run,serial,json);jobs.put(serial,job);deliveryStatus="Sending saved play time";UsageDelivery.sending();
  try{executor.execute(job);}catch(java.util.concurrent.RejectedExecutionException ex){jobs.remove(serial);deliveryStatus="Busy · saved on 3DS for retry";UsageDelivery.result(0,"Delivery queue busy");reply.ack(run,serial,-1);}
 }
 synchronized void cancel(byte[] packet,int n){
  if(n!=Protocol.HEADER+4)return;ByteBuffer b=ByteBuffer.wrap(packet);
  if(b.getInt(20)!=4||b.getShort(16)!=0||b.getShort(18)!=1||b.getInt(32)!=run)return;
  Job job=jobs.remove(b.getInt(28));if(job!=null){job.cancelled=true;executor.remove(job);}
 }
 private final class Job implements Runnable {
  final int clientRun,serial;final String json;volatile boolean cancelled;
  Job(int run,int serial,String json){clientRun=run;this.serial=serial;this.json=json;}
  public void run(){
   int outcome=-1,status=0;String error="";
   try{if(!cancelled){status=transport.post(json);outcome=status==204?1:status==410?2:-1;}}catch(Exception failure){error=transportError(failure);}
   synchronized(UsageProxy.this){if(jobs.get(serial)==this)jobs.remove(serial);if(!cancelled&&!closed&&run==clientRun){
    if(outcome==1){acknowledged++;deliveryStatus="Cloudflare confirmed delivery";}
    else if(status==410)deliveryStatus="Report expired (older than 90 days)";
    else deliveryStatus=status>0?"HTTP "+status+" · saved on 3DS for retry":"Network unavailable · saved on 3DS for retry";
    UsageDelivery.result(status,error);reply.ack(clientRun,serial,outcome);
   }}
  }
 }
 static String transportError(Exception error){
  if(error instanceof java.net.UnknownHostException)return "DNS lookup failed";
  if(error instanceof javax.net.ssl.SSLHandshakeException)return "TLS handshake failed";
  if(error instanceof javax.net.ssl.SSLException)return "TLS connection failed";
  if(error instanceof java.net.SocketTimeoutException)return "HTTPS request timed out";
  if(error instanceof java.net.ConnectException)return "HTTPS connection failed";
  return error.getClass().getSimpleName();
 }
 @Override public synchronized void close(){closed=true;reset(run);executor.shutdownNow();}
 private static int https(String json)throws Exception {
  HttpsURLConnection connection=(HttpsURLConnection)new URL(ENDPOINT).openConnection();
  try{
   connection.setInstanceFollowRedirects(false);connection.setConnectTimeout(2000);connection.setReadTimeout(2000);
   connection.setRequestMethod("POST");connection.setRequestProperty("Content-Type","application/json");connection.setRequestProperty("User-Agent","3DSRP-Usage");connection.setDoOutput(true);
   byte[] body=json.getBytes(StandardCharsets.UTF_8);connection.setFixedLengthStreamingMode(body.length);
   try(java.io.OutputStream out=connection.getOutputStream()){out.write(body);}
   return connection.getResponseCode();
  }finally{connection.disconnect();}
 }
}
