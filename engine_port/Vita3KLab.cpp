// Linked only with FARCRY_VITA3K_LAB; never part of a normal hardware package.
#include <stdio.h>
#include <string.h>
#include <psp2/display.h>
#include <psp2/gxm.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>

void Vita3KLabInputBridge(SceCtrlData *pad)
{
    static SceUInt64 nextPoll = 0, expires = 0;
    static unsigned int lastSerial = 0, buttons = 0;
    static unsigned char axes[4] = {128, 128, 128, 128};
    const SceUInt64 now = sceKernelGetProcessTimeWide();
    if (now >= nextPoll)
    {
        nextPoll = now + 100000u;
        FILE *request = fopen("ux0:data/farcry/lab_input.request", "rb");
        if (request)
        {
            unsigned int serial, duration, mask, x, y, rx, ry;
            char trailing;
            const int fields = fscanf(request, "%u %u %u %u %u %u %u %c",
                &serial, &duration, &mask, &x, &y, &rx, &ry, &trailing);
            fclose(request);
            if (fields == 7 && serial > lastSerial && serial <= 128 && duration <= 10000 &&
                !(mask & ~0xf3f9u) && x <= 255 && y <= 255 && rx <= 255 && ry <= 255)
            {
                lastSerial = serial;
                buttons = mask;
                axes[0] = (unsigned char)x; axes[1] = (unsigned char)y;
                axes[2] = (unsigned char)rx; axes[3] = (unsigned char)ry;
                expires = now + (SceUInt64)duration * 1000;
                FILE *events = fopen("ux0:data/farcry/lab_input.log", "ab");
                if (events)
                {
                    fprintf(events, "[VITA][LABINPUT] serial=%u duration_ms=%u buttons=%u axes=%u,%u,%u,%u guest_us=%llu\n",
                        serial, duration, mask, x, y, rx, ry, (unsigned long long)now);
                    fclose(events);
                }
            }
        }
    }
    pad->buttons = now < expires ? buttons : 0;
    pad->lx = now < expires ? axes[0] : 128;
    pad->ly = now < expires ? axes[1] : 128;
    pad->rx = now < expires ? axes[2] : 128;
    pad->ry = now < expires ? axes[3] : 128;
}

extern "C" void __wrap_invoke_splashscreen(void)
{
    // vitaGL df63ce8 exports void invoke_splashscreen(void). Its intro creates
    // a second GXM context on the "vitaGL Splashscreen" thread. Vita3K 4058
    // rejected that context with 0x805B0001, then crashed at native +0x77bf24.
    // Leave vitaGL's zero-initialized is_splashscreen_active false. Gameplay,
    // the main GXM context, memory settings and render paths are unchanged.
    // Upstream source also offers the SKIP_SPLASHSCREEN build configuration.
    FILE *marker = fopen("ux0:data/farcry/lab_compat.txt", "wb");
    if (marker)
    {
        fputs("[VITA][LAB] vitaGL intro skipped: Vita3K secondary-context workaround\n", marker);
        fclose(marker);
    }
}

void Vita3KLabAfterPresentBridge(void)
{
    static SceUInt64 start = 0, nextPoll = 0;
    static unsigned int captures = 0, autoIndex = 0, lastRequest = 0;
    static bool automaticCaptures = true;
    static const unsigned int captureSeconds[] = { 5, 20, 40, 60, 90, 120 };
    const SceUInt64 now = sceKernelGetProcessTimeWide();
    if (!start)
    {
        start = now;
        FILE *policy = fopen("ux0:data/farcry/lab_capture.policy", "rb");
        if (policy)
        {
            char name[16] = {};
            if (fscanf(policy, "%15s", name) == 1)
                automaticCaptures = strcmp(name, "manual") != 0;
            fclose(policy);
        }
        FILE *first = fopen("ux0:data/farcry/lab_captures.txt", "ab");
        if (first) { fputs("[VITA][LAB] first_presented_frame\n", first); fclose(first); }
    }
    if (captures >= 8 || now < nextPoll) return;
    nextPoll = now + 1000000u;
    const unsigned int elapsed = (unsigned int)((now - start) / 1000000u);
    bool requested = false;
    char label[49] = "automatic";
    FILE *request = fopen("ux0:data/farcry/lab_capture.request", "rb");
    if (request)
    {
        unsigned int serial = 0;
        if (fscanf(request, "%u %48s", &serial, label) == 2 && serial != lastRequest)
        {
            lastRequest = serial;
            requested = true;
        }
        fclose(request);
    }
    const bool automatic = automaticCaptures && autoIndex < 6 && elapsed >= captureSeconds[autoIndex];
    if (!requested && !automatic) return;
    if (automatic) ++autoIndex;
    ++captures; // A failed readback also consumes an attempt; never retry forever.

    // Swap queues an asynchronous display callback and immediately advances the
    // buffer index. Drain that queue only for a capture: GetFrameBuf itself does
    // not wait, and could otherwise return a surface that is still being drawn.
    const int queueResult = sceGxmDisplayQueueFinish();
    SceDisplayFrameBuf frame;
    memset(&frame, 0, sizeof(frame));
    frame.size = sizeof(frame);
    const int result = queueResult < 0 ? queueResult :
        sceDisplayGetFrameBuf(&frame, SCE_DISPLAY_SETBUF_NEXTFRAME);
    FILE *events = fopen("ux0:data/farcry/lab_captures.txt", "ab");
    if (result < 0 || !frame.base || !frame.width || !frame.height ||
        frame.width > 960 || frame.height > 544 || frame.pitch < frame.width ||
        frame.pitch > 2048 || frame.pixelformat != SCE_DISPLAY_PIXELFORMAT_A8B8G8R8)
    {
        if (events) { fprintf(events, "capture=%u elapsed=%u error=0x%08x invalid framebuffer\n", captures, elapsed, result); fclose(events); }
        return;
    }
    // An emulated GPU surface is not necessarily current in its CPU alias,
    // even after the display queue drains. Request an actual GPU transfer into
    // a fresh, mapped, uncached block instead of copying stale backing bytes.
    // This transient allocation and synchronization are diagnostic-only.
    struct ReadbackBlock
    {
        SceUID id;
        void *pixels;
        bool mapped;
        ReadbackBlock() : id(-1), pixels(NULL), mapped(false) {}
        ~ReadbackBlock()
        {
            if (mapped) sceGxmUnmapMemory(pixels);
            if (id >= 0) sceKernelFreeMemBlock(id);
        }
    } block;
    const unsigned int bytes = (frame.width * frame.height * 4u + 4095u) & ~4095u;
    block.id = sceKernelAllocMemBlock("FarCryLabReadback", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, bytes, NULL);
    int copyResult = block.id;
    if (copyResult >= 0) copyResult = sceKernelGetMemBlockBase(block.id, &block.pixels);
    if (copyResult >= 0)
    {
        copyResult = sceGxmMapMemory(block.pixels, bytes, SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE);
        block.mapped = copyResult >= 0;
    }
    if (copyResult >= 0)
    {
        copyResult = sceGxmTransferCopy(frame.width, frame.height, 0, 0, SCE_GXM_TRANSFER_COLORKEY_NONE,
            SCE_GXM_TRANSFER_FORMAT_U8U8U8U8_ABGR, SCE_GXM_TRANSFER_LINEAR, frame.base, 0, 0, frame.pitch * 4,
            SCE_GXM_TRANSFER_FORMAT_U8U8U8U8_ABGR, SCE_GXM_TRANSFER_LINEAR, block.pixels, 0, 0, frame.width * 4,
            NULL, 0, NULL);
        if (copyResult >= 0) copyResult = sceGxmTransferFinish();
    }
    if (copyResult < 0)
    {
        if (events) { fprintf(events, "capture=%u elapsed=%u error=0x%08x transfer readback failed\n", captures, elapsed, copyResult); fclose(events); }
        return;
    }
    char filename[128], temporary[128];
    snprintf(filename, sizeof(filename), "ux0:data/farcry/lab_frame_%02u.ppm", captures);
    snprintf(temporary, sizeof(temporary), "ux0:data/farcry/lab_frame_%02u.tmp", captures);
    FILE *output = fopen(temporary, "wb");
    bool ok = output != NULL;
    if (output)
    {
        fprintf(output, "P6\n%u %u\n255\n", frame.width, frame.height);
        unsigned char row[960 * 3];
        const unsigned char *pixels = (const unsigned char *)block.pixels;
        for (unsigned int y = 0; y < frame.height && ok; ++y)
        {
            const unsigned char *source = pixels + y * frame.width * 4;
            for (unsigned int x = 0; x < frame.width; ++x)
            {
                row[x * 3] = source[x * 4];
                row[x * 3 + 1] = source[x * 4 + 1];
                row[x * 3 + 2] = source[x * 4 + 2];
            }
            ok = fwrite(row, 3, frame.width, output) == frame.width;
        }
        if (fclose(output) != 0) ok = false;
        if (ok) ok = rename(temporary, filename) == 0;
    }
    if (events)
    {
        fprintf(events, "capture=%u elapsed=%u label=%s dimensions=%ux%u written=%d transfer=1 diagnostic_us=%llu\n",
            captures, elapsed, requested ? label : "automatic", frame.width, frame.height, ok ? 1 : 0,
            (unsigned long long)(sceKernelGetProcessTimeWide() - now));
        fclose(events);
    }
}
