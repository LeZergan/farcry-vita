#include <vitaGL.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/ctrl.h>

int main(int argc, char *argv[]) {
	vglInit(0x400000); /* 4MB legacy pool, plenty for a clear + one triangle */

	glViewport(0, 0, 960, 544);

	float hue = 0.0f;

	while (1) {
		SceCtrlData pad;
		sceCtrlPeekBufferPositive(0, &pad, 1);
		if (pad.buttons & SCE_CTRL_START)
			break;

		hue += 0.01f;
		if (hue > 1.0f) hue -= 1.0f;

		glClearColor(0.05f, 0.05f, 0.15f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		/* default identity projection/modelview: use NDC coords directly
		   so the triangle is guaranteed visible regardless of matrix setup */
		glBegin(GL_TRIANGLES);
			glColor3f(1.0f, hue, 0.0f);
			glVertex2f(0.0f, 0.6f);

			glColor3f(0.0f, 1.0f, hue);
			glVertex2f(-0.6f, -0.6f);

			glColor3f(hue, 0.0f, 1.0f);
			glVertex2f(0.6f, -0.6f);
		glEnd();

		vglSwapBuffers(GL_FALSE);
	}

	sceKernelExitProcess(0);
	return 0;
}
