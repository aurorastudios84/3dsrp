#!/usr/bin/env python3
"""Check pause thresholds, first-event handling and recovery without wall-clock sleeps."""
from pathlib import Path
import tempfile,subprocess
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'TimingCheck.java').write_text('''package com.aurora.relay;
public class TimingCheck { public static void main(String[] args) {
 FrameTiming t=new FrameTiming();t.event(1000000);t.event(1033333);t.event(1108333);
 t.event(1258333);t.event(1558333);t.event(1591666);t.work(123);t.work(12);
 if(!t.snapshot().equals("[6,300000,3,2,123,1,3]"))throw new AssertionError(t.snapshot());
 FrameTiming first=new FrameTiming();first.event(Long.MAX_VALUE/2);
 if(!first.snapshot().equals("[1,0,0,0,0,0,0]"))throw new AssertionError(first.snapshot());
 System.out.println("PASS timing pause thresholds, first event, consecutive stalls and work maxima");
}}''')
 subprocess.run(['javac','-d',tmp,str(root/'android/app/src/main/java/com/aurora/relay/FrameTiming.java'),str(p/'TimingCheck.java')],check=True)
 subprocess.run(['java','-cp',tmp,'com.aurora.relay.TimingCheck'],check=True)
