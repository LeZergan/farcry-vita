/* Out-of-line definitions for CryCompat.h declarations that need a
   complete LARGE_INTEGER (only available once CryCommon/LinuxSpecific.h
   has actually been processed via the normal include chain -- CryCompat.h
   itself is force-included ahead of that, so can only declare these). */
#include "CryCompat.h"
#include <platform.h> /* pulls in LinuxSpecific.h -> the real LARGE_INTEGER definition */
#include <time.h>

bool QueryPerformanceCounter(LARGE_INTEGER *out) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	out->QuadPart = (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
	return true;
}

bool QueryPerformanceFrequency(LARGE_INTEGER *out) {
	out->QuadPart = 1000000000LL;
	return true;
}
