/* First real link attempt: boot the actual compiled CrySystem against a
   default SSystemInitParams, exactly like FARCRY/Main.cpp's static
   (non-DLL) path does. The goal right now is purely to find out what's
   still undefined when everything compiled so far gets linked together
   -- not a full, working boot yet. */
#include <ISystem.h>

int main(int argc, char *argv[]) {
	SSystemInitParams sip;
	ISystem *pSystem = CreateSystemInterface(sip);
	if (pSystem) {
		pSystem->Release();
		return 0;
	}
	return 1;
}
