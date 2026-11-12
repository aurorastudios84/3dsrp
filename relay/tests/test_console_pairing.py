#!/usr/bin/env python3
"""Exercise the actual JNI saved-pair validator with real Pair ABI and a fake JNI table."""
from pathlib import Path
import subprocess,tempfile,shutil
root=Path(__file__).resolve().parents[1]
source=(root/'android/app/src/main/cpp/bridge.cpp').read_text()
a=source.index('struct Pair ');b=source.index('extern "C" JNIEXPORT jstring JNICALL Java_com_aurora_relay_RelayEngine_nativeWake',a)
java=Path(shutil.which('javac')).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)
 (p/'check.cpp').write_text('''#include <jni.h>
#include <chiaki/regist.h>
#include <cstdio>
#include <cstring>
#include <cassert>
'''+source[a:b]+r'''
static jsize length(JNIEnv*,jarray){return 8;}
static const char *chars(JNIEnv*,jstring s,jboolean*){return (const char*)s;}
static void release(JNIEnv*,jstring,const char*){}
static void bytes(JNIEnv*,jbyteArray a,jsize start,jsize count,jbyte *out){memcpy(out,(char*)a+start,count);}
int main(){
 JNINativeInterface_ f{};f.GetArrayLength=length;f.GetStringUTFChars=chars;f.ReleaseStringUTFChars=release;f.GetByteArrayRegion=bytes;JNIEnv env{&f};
 uint8_t aid[8]={1};Pair p;p.account[0]=1;p.host.target=CHIAKI_TARGET_PS5_1;strcpy(p.address,"192.168.1.99");
 auto check=[&](const char *host,bool ps5){return Java_com_aurora_relay_RelayEngine_nativeHasPair(&env,nullptr,(jstring)host,(jbyteArray)aid,(jstring)"pair.bin",ps5);};
 auto save=[&](){FILE *file=fopen("pair.bin","wb");assert(file);assert(fwrite(&p,1,sizeof(p),file)==sizeof(p));fclose(file);};
 assert(!check("192.168.1.99",true));save();
 assert(check("192.168.1.99",true));assert(check("home.example.com",true)); // same console, different route
 assert(!check("home.example.com",false));aid[0]=2;assert(!check("192.168.1.99",true));aid[0]=1;
 p.host.target=CHIAKI_TARGET_PS4_10;save();assert(check("203.0.113.1",false));assert(!check("203.0.113.1",true));
 FILE *file=fopen("pair.bin","wb");fwrite(&p,1,8,file);fclose(file);assert(!check("192.168.1.99",false));
 p.magic=0;save();assert(!check("192.168.1.99",false));
}
''')
 subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined','-I'+str(java/'include'),'-I'+str(java/'include/linux'),'-I'+str(root/'vendor/chiaki-upstream/lib/include'),'-I'+str(root/'android/app/src/main/cpp/generated'),str(p/'check.cpp'),'-o',str(p/'check')],check=True)
 subprocess.run([p/'check'],cwd=p,check=True)
print('PASS real saved-pair validator: local/internet reuse, PS4/PS5 separation, account mismatch, missing/truncated/corrupt files')
