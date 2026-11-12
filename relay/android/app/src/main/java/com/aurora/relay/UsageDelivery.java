package com.aurora.relay;

/** Local diagnostics only: never records report IDs, payloads or console credentials. */
final class UsageDelivery {
 interface Store { Snapshot load(); void save(Snapshot snapshot); }
 static final class Snapshot {
  final String status,error;final long received,rejected,confirmed,failed;final int http;
  Snapshot(String status,String error,long received,long rejected,long confirmed,long failed,int http){this.status=status;this.error=error;this.received=received;this.rejected=rejected;this.confirmed=confirmed;this.failed=failed;this.http=http;}
 }
 private static volatile Snapshot value=new Snapshot("No usage reports received yet","",0,0,0,0,0);
 private static Store store;
 static synchronized void restore(Store next){if(store!=null)return;store=next;try{Snapshot loaded=next.load();if(loaded!=null)value=loaded;}catch(RuntimeException ignored){}}
 static Snapshot snapshot(){return value;}
 static String jsonFields(){Snapshot s=value;return "\"usage\":{\"received\":"+s.received+",\"rejected\":"+s.rejected+",\"confirmed\":"+s.confirmed+",\"http\":"+s.http+",\"error\":\""+s.error+"\"}";}
 private static void save(Snapshot next){value=next;if(store!=null)try{store.save(next);}catch(RuntimeException ignored){}}
 static synchronized void received(){Snapshot s=value;save(new Snapshot(s.status,s.error,s.received+1,s.rejected,s.confirmed,s.failed,s.http));}
 static synchronized void rejected(String reason){Snapshot s=value;save(new Snapshot("Report rejected: "+reason,s.error,s.received,s.rejected+1,s.confirmed,s.failed,s.http));}
 static synchronized void sending(){Snapshot s=value;save(new Snapshot("Sending saved play time",s.error,s.received,s.rejected,s.confirmed,s.failed,s.http));}
 static synchronized void result(int status,String error){
  Snapshot s=value;boolean success=status==204;
  String message=success?"Cloudflare confirmed delivery":status==410?"Report expired (older than 90 days)":status>0?"HTTP "+status+" · saved on 3DS for retry":error+" · saved on 3DS for retry";
  save(new Snapshot(message,error,s.received,s.rejected,s.confirmed+(success?1:0),s.failed+(success?0:1),status));
 }
}
