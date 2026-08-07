/* First real link attempt: boot the actual compiled CrySystem against a
   default SSystemInitParams, exactly like FARCRY/Main.cpp's static
   (non-DLL) path does. The goal right now is purely to find out what's
   still undefined when everything compiled so far gets linked together
   -- not a full, working boot yet. */
#include <ISystem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/clib.h>

int main(int argc, char *argv[]) {
	sceClibPrintf("[BOOTTRACE] main: before delay\n");
	sceKernelDelayThread(300000); /* 300ms -- test whether the crash timing is wall-clock-tied */
	sceClibPrintf("[BOOTTRACE] main: after delay, before CreateSystemInterface\n");
	SSystemInitParams sip;
	ISystem *pSystem = CreateSystemInterface(sip);
	if (pSystem) {
		pSystem->Release();
		return 0;
	}
	return 1;
}
