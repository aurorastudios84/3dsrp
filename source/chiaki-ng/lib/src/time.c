// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/time.h>

#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __3DS__
#include <3ds.h>
#endif

CHIAKI_EXPORT uint64_t chiaki_time_now_monotonic_us()
{
#if _WIN32
	LARGE_INTEGER f;
	if(!QueryPerformanceFrequency(&f))
		return 0;
	LARGE_INTEGER v;
	if(!QueryPerformanceCounter(&v))
		return 0;
	v.QuadPart *= 1000000;
	v.QuadPart /= f.QuadPart;
	return v.QuadPart;
#elif defined(__3DS__)
	/* newlib for devkitARM ships no clock_gettime, so read the ARM11 tick
	 * counter directly. libctru's os.h documents svcGetSystemTick() as running
	 * at SYSCLOCK_ARM11 (268111856 Hz) -- see the "in \ref svcGetSystemTick"
	 * note on the SYSCLOCK_ARM11 macro. Dividing by SYSCLOCK_ARM11/4 made the
	 * monotonic clock run 4x too fast, which shrank every cond-var timeout to a
	 * quarter of its real value (main.c and mvd_sink.c already divide by the
	 * full SYSCLOCK_ARM11). */
	uint64_t ticks = (uint64_t)svcGetSystemTick();
	return ticks / SYSCLOCK_ARM11 * 1000000ULL + ticks % SYSCLOCK_ARM11 * 1000000ULL / SYSCLOCK_ARM11;
#else
	struct timespec time;
	clock_gettime(CLOCK_MONOTONIC, &time);
	return time.tv_sec * 1000000 + time.tv_nsec / 1000;
#endif
}
