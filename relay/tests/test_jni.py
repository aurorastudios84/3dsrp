#!/usr/bin/env python3
"""Exercise the actual binding lifetime with a fake Android JavaVM."""
from pathlib import Path
import subprocess,tempfile,shutil
ROOT=Path(__file__).resolve().parents[1]
import os
header=Path(os.environ['ANDROID_HOME'])/'ndk/27.1.12297006/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/jni.h'
with tempfile.TemporaryDirectory(prefix='relay-jni-test-') as tmp:
 p=Path(tmp);shutil.copyfile(header,p/'jni.h')
 (p/'test.cpp').write_text(r'''
#include "jni_binding.hpp"
#include <atomic>
#include <cassert>
#include <thread>
#include <vector>
static std::atomic<unsigned> attaches,detaches,gets;
static thread_local bool attached=false,fail_attach=false;
static thread_local uintptr_t cookie;
static jint get(JavaVM*,void **env,jint version){gets++;assert(version==JNI_VERSION_1_6);*env=attached?(void*)&cookie:nullptr;return attached?JNI_OK:JNI_EDETACHED;}
static jint attach(JavaVM*,JNIEnv **env,void*){attaches++;if(fail_attach)return JNI_ERR;attached=true;*env=(JNIEnv*)&cookie;return JNI_OK;}
static jint detach(JavaVM*){assert(attached);attached=false;detaches++;return JNI_OK;}
int main(){
 JNIInvokeInterface functions{};functions.GetEnv=get;functions.AttachCurrentThread=attach;functions.DetachCurrentThread=detach;
 JavaVM vm{&functions};attached=true;JNIEnv *main=relay_jni_env(&vm);assert(main==(JNIEnv*)&cookie);
 assert(relay_jni_env(&vm)==main&&!attaches&&!detaches&&gets==1);
 std::vector<std::thread> workers;
 for(unsigned i=0;i<8;i++)workers.emplace_back([&]{JNIEnv *e=relay_jni_env(&vm);assert(e==(JNIEnv*)&cookie);for(unsigned n=0;n<20000;n++)assert(relay_jni_env(&vm)==e);});
 for(auto &worker:workers)worker.join();
 assert(attaches==8&&detaches==8&&gets==9);
 std::thread failed([&]{fail_attach=true;assert(relay_jni_env(&vm)==nullptr);});failed.join();
 assert(attaches==9&&detaches==8);
 assert(relay_jni_env(&vm)==main&&detaches==8); /* Java-owned thread untouched */
}
''')
 subprocess.run(['c++','-std=c++17','-O1','-g','-pthread','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(p),'-I'+str(ROOT/'common'),p/'test.cpp','-o',p/'test'],check=True)
 subprocess.run([p/'test'],check=True)
print('PASS actual JNI cache: 160000 lookups, one attach/detach per native thread, Java ownership and attach-failure cleanup')
