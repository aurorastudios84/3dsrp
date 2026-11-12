#pragma once
#include <jni.h>
/* One attachment for a native thread's lifetime. A thread which Android
 * already attached remains owned by Android and must never be detached here. */
class RelayJniBinding {
 JavaVM *vm_;
 bool owned_=false;
public:
 JNIEnv *env=nullptr;
 explicit RelayJniBinding(JavaVM *vm):vm_(vm){
  jint result=vm_->GetEnv((void**)&env,JNI_VERSION_1_6);
  if(result==JNI_EDETACHED){owned_=vm_->AttachCurrentThread(&env,nullptr)==JNI_OK;if(!owned_)env=nullptr;}
  else if(result!=JNI_OK)env=nullptr;
 }
 ~RelayJniBinding(){if(owned_)vm_->DetachCurrentThread();}
 RelayJniBinding(const RelayJniBinding&)=delete;
 RelayJniBinding& operator=(const RelayJniBinding&)=delete;
};
inline JNIEnv *relay_jni_env(JavaVM *vm){thread_local RelayJniBinding binding(vm);return binding.env;}
