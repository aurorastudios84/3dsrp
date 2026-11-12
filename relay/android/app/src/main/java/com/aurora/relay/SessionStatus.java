package com.aurora.relay;

/** A terminal error must survive late startup messages and shutdown callbacks. */
final class SessionStatus {
 interface Listener {void message(String text);}
 private final Listener listener;
 private volatile String failure;
 private boolean closed;
 SessionStatus(Listener listener){this.listener=listener;}
 synchronized void update(String text){if(!closed&&failure==null)listener.message(text);}
 synchronized void fail(String text){if(!closed&&failure==null){failure=text;listener.message(text);}}
 String failure(){return failure;}
 synchronized void close(){closed=true;}
}
