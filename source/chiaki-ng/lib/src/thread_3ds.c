// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
/*
 * 3DS (devkitARM / libctru) backend for chiaki's threading abstraction.
 *
 * lib/CMakeLists.txt compiles this file *instead of* src/thread.c when
 * CHIAKI_IS_3DS is set: libctru has no pthreads, so the whole abstraction is
 * reimplemented on kernel objects. Semantics kept identical to pthreads for
 * everything the library relies on:
 *
 *   thread  : libctru thread; its return value is stashed in ChiakiThread by a
 *             trampoline so chiaki_thread_join can hand it back.
 *   mutex   : kernel mutex (svcCreateMutex) -- always recursive, trylock is a
 *             zero-nanosecond wait, so chiaki_mutex_init(rec) is honoured in
 *             behaviour (kernel mutexes are recursive) but rec is not needed.
 */

#include <chiaki/thread.h>
#include <chiaki/time.h>

#include <3ds.h>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

/* Result code svcWaitSynchronization returns when the timeout expires. */
#define CHIAKI_3DS_SVC_TIMEOUT 0x09401BFE

/* 128 KiB per thread: 32 KiB proved too small once the session thread ran the
 * full Takion connect + ctrl + stream path (hard-reset crash). Priority 0x30 ==
 * the usual main-thread priority, core -2 == let the OS pick (0x30 is also used
 * by the Switch port's threads). */
static const size_t CHIAKI_3DS_THREAD_STACK = 0x20000;
static const int CHIAKI_3DS_THREAD_PRIO = 0x30;
static const int CHIAKI_3DS_THREAD_CORE = -2;

static void chiaki_3ds_thread_entry(void *arg)
{
	ChiakiThread *thread = arg;
	__atomic_store_n(&thread->native_handle, threadGetHandle(threadGetCurrent()), __ATOMIC_RELEASE);

	thread->ret = thread->func(thread->arg);

}

CHIAKI_EXPORT ChiakiErrorCode chiaki_thread_create(ChiakiThread *thread, ChiakiThreadFunc func, void *arg)
{

	thread->func = func;
	thread->arg = arg;
	thread->ret = NULL;
	__atomic_store_n(&thread->native_handle, 0, __ATOMIC_RELEASE);
	thread->thread = threadCreate(chiaki_3ds_thread_entry, thread, CHIAKI_3DS_THREAD_STACK,
		CHIAKI_3DS_THREAD_PRIO, CHIAKI_3DS_THREAD_CORE, false);

	if(!thread->thread)
		return CHIAKI_ERR_THREAD;
	return CHIAKI_ERR_SUCCESS;
}

static void chiaki_3ds_thread_release(ChiakiThread *thread)
{

	threadFree(thread->thread);
	thread->thread = NULL;
	__atomic_store_n(&thread->native_handle, 0, __ATOMIC_RELEASE);

}

static Result chiaki_3ds_thread_wait(ChiakiThread *thread, uint64_t timeout_ms)
{
	uint64_t start = chiaki_time_now_monotonic_ms();
	Handle handle;
	while(!(handle = __atomic_load_n(&thread->native_handle, __ATOMIC_ACQUIRE)))
	{
		if(timeout_ms != UINT64_MAX && chiaki_time_now_monotonic_ms() - start >= timeout_ms)
			return CHIAKI_3DS_SVC_TIMEOUT;
		svcSleepThread(1000000);
	}
	uint64_t elapsed = chiaki_time_now_monotonic_ms() - start;
	uint64_t remaining = timeout_ms > elapsed ? timeout_ms - elapsed : 0;
	u64 timeout_ns = timeout_ms == UINT64_MAX ? U64_MAX : remaining * 1000000ULL;
	return svcWaitSynchronization(handle, timeout_ns);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_thread_join(ChiakiThread *thread, void **retval)
{

	Result result = chiaki_3ds_thread_wait(thread, UINT64_MAX);

	if(result != 0)
		return CHIAKI_ERR_THREAD;
	chiaki_3ds_thread_release(thread);
	if(retval)
		*retval = thread->ret;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_thread_timedjoin(ChiakiThread *thread, void **retval, uint64_t timeout_ms)
{
	/* timeout_ms * 1e6 overflows for "no timeout"; and the timeout result is not
	 * flagged by R_FAILED() (see chiaki_3ds_cond_wait). */

	Result r = chiaki_3ds_thread_wait(thread, timeout_ms);

	if((u32)r == CHIAKI_3DS_SVC_TIMEOUT)
		return CHIAKI_ERR_TIMEOUT;
	if(r != 0)
		return CHIAKI_ERR_THREAD;
	chiaki_3ds_thread_release(thread);
	if(retval)
		*retval = thread->ret;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_thread_set_name(ChiakiThread *thread, const char *name)
{
	/* libctru exposes no thread-naming call; nothing to do. */
	(void)thread;
	(void)name;
	return CHIAKI_ERR_SUCCESS;
}

static ChiakiThreadAffinityFunc g_affinity_cb = NULL;
static void *g_affinity_cb_user = NULL;

CHIAKI_EXPORT void chiaki_thread_set_affinity(ChiakiThreadName name)
{

	if(g_affinity_cb)
		g_affinity_cb(name, g_affinity_cb_user);
}

CHIAKI_EXPORT void chiaki_thread_set_affinity_cb(ChiakiThreadAffinityFunc func, void *user)
{
	g_affinity_cb = func;
	g_affinity_cb_user = user;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_mutex_init(ChiakiMutex *mutex, bool rec)
{
	(void)rec; /* kernel mutexes are recursive */
	if(R_FAILED(svcCreateMutex(&mutex->handle, false)))
		return CHIAKI_ERR_UNKNOWN;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_mutex_fini(ChiakiMutex *mutex)
{
	svcCloseHandle(mutex->handle);
	mutex->handle = 0;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_mutex_lock(ChiakiMutex *mutex)
{

	Result result = svcWaitSynchronization(mutex->handle, U64_MAX);

	if(R_FAILED(result))
		return CHIAKI_ERR_UNKNOWN;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_mutex_trylock(ChiakiMutex *mutex)
{
	Result r = svcWaitSynchronization(mutex->handle, 0);
	if((u32)r == CHIAKI_3DS_SVC_TIMEOUT)
		return CHIAKI_ERR_MUTEX_LOCKED;
	if(R_SUCCEEDED(r))
		return CHIAKI_ERR_SUCCESS;
	return CHIAKI_ERR_UNKNOWN;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_mutex_unlock(ChiakiMutex *mutex)
{
	Result result = svcReleaseMutex(mutex->handle);

	if(R_FAILED(result))
		return CHIAKI_ERR_UNKNOWN;
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_cond_init(ChiakiCond *cond)
{
	__atomic_store_n(&cond->sequence, 0, __ATOMIC_SEQ_CST);
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_cond_fini(ChiakiCond *cond)
{
	(void)cond;
	return CHIAKI_ERR_SUCCESS;
}

static ChiakiErrorCode chiaki_3ds_cond_wait(ChiakiCond *cond, ChiakiMutex *mutex, uint64_t timeout_ms)
{
	uint32_t sequence = __atomic_load_n(&cond->sequence, __ATOMIC_SEQ_CST);
	uint64_t start = chiaki_time_now_monotonic_ms();

	ChiakiErrorCode err = chiaki_mutex_unlock(mutex);
	if(err != CHIAKI_ERR_SUCCESS) {  return err; }
	while(__atomic_load_n(&cond->sequence, __ATOMIC_SEQ_CST) == sequence)
	{
		if(timeout_ms != UINT64_MAX && chiaki_time_now_monotonic_ms() - start >= timeout_ms)
		{ err = CHIAKI_ERR_TIMEOUT; break; }
		svcSleepThread(2000000);
	}

	ChiakiErrorCode locked = chiaki_mutex_lock(mutex);

	if(locked != CHIAKI_ERR_SUCCESS) err = locked;

	return err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_cond_wait(ChiakiCond *cond, ChiakiMutex *mutex)
{
	return chiaki_3ds_cond_wait(cond, mutex, UINT64_MAX);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_cond_timedwait(ChiakiCond *cond, ChiakiMutex *mutex, uint64_t timeout_ms)
{
	return chiaki_3ds_cond_wait(cond, mutex, timeout_ms);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_cond_wait_pred(ChiakiCond *cond, ChiakiMutex *mutex, ChiakiCheckPred check_pred, void *check_pred_user)
{
	while(!check_pred(check_pred_user))
	{
		ChiakiErrorCode err = chiaki_cond_wait(cond, mutex);
		if(err != CHIAKI_ERR_SUCCESS)
			return err;
	}
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_cond_timedwait_pred(ChiakiCond *cond, ChiakiMutex *mutex, uint64_t timeout_ms, ChiakiCheckPred check_pred, void *check_pred_user)
{
	uint64_t start = chiaki_time_now_monotonic_ms();
	uint64_t elapsed = 0;
	while(!check_pred(check_pred_user))
	{
		uint64_t remaining = timeout_ms > elapsed ? timeout_ms - elapsed : 0;
		/* Nothing left on the clock and the predicate is still false: report the
		 * timeout instead of looping on zero-length waits forever. */
		if(remaining == 0)
			return CHIAKI_ERR_TIMEOUT;
		ChiakiErrorCode err = chiaki_cond_timedwait(cond, mutex, remaining);
		if(err == CHIAKI_ERR_TIMEOUT)
			return CHIAKI_ERR_TIMEOUT;
		if(err != CHIAKI_ERR_SUCCESS)
			return err;
		elapsed = chiaki_time_now_monotonic_ms() - start;
	}
	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_cond_signal(ChiakiCond *cond)
{
	(void)__atomic_add_fetch(&cond->sequence, 1, __ATOMIC_SEQ_CST);

	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_cond_broadcast(ChiakiCond *cond)
{
	return chiaki_cond_signal(cond);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_bool_pred_cond_init(ChiakiBoolPredCond *cond)
{
	cond->pred = false;

	ChiakiErrorCode err = chiaki_mutex_init(&cond->mutex, false);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	err = chiaki_cond_init(&cond->cond);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		chiaki_mutex_fini(&cond->mutex);
		return err;
	}

	return CHIAKI_ERR_SUCCESS;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_bool_pred_cond_fini(ChiakiBoolPredCond *cond)
{
	ChiakiErrorCode err = chiaki_cond_fini(&cond->cond);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	return chiaki_mutex_fini(&cond->mutex);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_bool_pred_cond_lock(ChiakiBoolPredCond *cond)
{
	return chiaki_mutex_lock(&cond->mutex);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_bool_pred_cond_unlock(ChiakiBoolPredCond *cond)
{
	return chiaki_mutex_unlock(&cond->mutex);
}

bool bool_pred_cond_check_pred(void *user)
{
	ChiakiBoolPredCond *bool_pred_cond = user;
	return bool_pred_cond->pred;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_bool_pred_cond_wait(ChiakiBoolPredCond *cond)
{
	return chiaki_cond_wait_pred(&cond->cond, &cond->mutex, bool_pred_cond_check_pred, cond);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_bool_pred_cond_timedwait(ChiakiBoolPredCond *cond, uint64_t timeout_ms)
{
	return chiaki_cond_timedwait_pred(&cond->cond, &cond->mutex, timeout_ms, bool_pred_cond_check_pred, cond);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_bool_pred_cond_signal(ChiakiBoolPredCond *cond)
{
	ChiakiErrorCode err = chiaki_bool_pred_cond_lock(cond);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	cond->pred = true;

	err = chiaki_bool_pred_cond_unlock(cond);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	return chiaki_cond_signal(&cond->cond);
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_bool_pred_cond_broadcast(ChiakiBoolPredCond *cond)
{
	ChiakiErrorCode err = chiaki_bool_pred_cond_lock(cond);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	cond->pred = true;

	err = chiaki_bool_pred_cond_unlock(cond);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	return chiaki_cond_broadcast(&cond->cond);
}
