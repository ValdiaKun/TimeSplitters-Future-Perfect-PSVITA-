/* SPDX-License-Identifier: GPL-3.0-only
 * Asset-free native GXM framebuffer lifecycle smoke test.
 * CPU-generated image, NOT GPU drawing and NOT the TimeSplitters renderer.
 * Based on publicly documented VitaSDK GXM/display interfaces.
 */
#include <psp2/gxm.h>
#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/processmgr.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define W 960u
#define H 544u
#define STRIDE 1024u
#define BUFS 2u
#define FRAME_BYTES (4u * STRIDE * H)
#define MEM_ALIGN (256u * 1024u)
#define FRAME_COUNT 180u

typedef struct {
    SceUID uid;
    void *data;
    SceGxmSyncObject *sync;
    SceGxmColorSurface color;
    int mapped;
} Frame;

static Frame frames[BUFS];
static int initialized;
static unsigned submitted;

static void display_callback(const void *data) {
    const void *address = NULL;
    memcpy(&address, data, sizeof(address));
    SceDisplayFrameBuf fb;
    memset(&fb, 0, sizeof(fb));
    fb.size = sizeof(fb);
    fb.base = (void *)address;
    fb.pitch = STRIDE;
    fb.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
    fb.width = W;
    fb.height = H;
    sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
}

static void cleanup(void) {
    if (initialized) {
        /* Queue must finish before its sync objects and memory can be freed. */
        sceGxmDisplayQueueFinish();
    }
    for (unsigned i = 0; i < BUFS; ++i) {
        if (frames[i].sync) {
            sceGxmSyncObjectDestroy(frames[i].sync);
            frames[i].sync = NULL;
        }
        if (frames[i].mapped) {
            sceGxmUnmapMemory(frames[i].data);
            frames[i].mapped = 0;
        }
        if (frames[i].uid >= 0) {
            sceKernelFreeMemBlock(frames[i].uid);
            frames[i].uid = -1;
            frames[i].data = NULL;
        }
    }
    if (initialized) {
        sceGxmTerminate();
        initialized = 0;
    }
}

static int initialize(void) {
    for (unsigned i = 0; i < BUFS; ++i) {
        frames[i].uid = -1;
    }
    SceGxmInitializeParams params;
    memset(&params, 0, sizeof(params));
    params.flags = SCE_GXM_INITIALIZE_FLAG_DEFAULT;
    params.displayQueueMaxPendingCount = 1;
    params.displayQueueCallback = display_callback;
    params.displayQueueCallbackDataSize = sizeof(void *);
    params.parameterBufferSize = SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE;
    if (sceGxmInitialize(&params) < 0) return -1;
    initialized = 1;
    const unsigned size = (FRAME_BYTES + MEM_ALIGN - 1) & ~(MEM_ALIGN - 1);
    for (unsigned i = 0; i < BUFS; ++i) {
        Frame *f = &frames[i];
        f->uid = sceKernelAllocMemBlock("tsfp-gxm-smoke",
            SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, size, NULL);
        if (f->uid < 0) return -2;
        if (sceKernelGetMemBlockBase(f->uid, &f->data) < 0 || !f->data)
            return -3;
        if (sceGxmMapMemory(f->data, size, SCE_GXM_MEMORY_ATTRIB_READ |
            SCE_GXM_MEMORY_ATTRIB_WRITE) < 0) return -4;
        f->mapped = 1;
        if (sceGxmColorSurfaceInit(&f->color, SCE_GXM_COLOR_FORMAT_A8B8G8R8,
            SCE_GXM_COLOR_SURFACE_LINEAR, SCE_GXM_COLOR_SURFACE_SCALE_NONE,
            SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT, W, H, STRIDE, f->data) < 0)
            return -5;
        if (sceGxmSyncObjectCreate(&f->sync) < 0) return -6;
        memset(f->data, 0, FRAME_BYTES);
    }
    return 0;
}

static void paint(Frame *f, unsigned tick) {
    uint32_t *pixels = (uint32_t *)f->data;
    for (unsigned y=0; y<H; ++y)
        for (unsigned x=0; x<W; ++x)
            pixels[y*STRIDE+x] = 0xff000000u |
                (((x + tick) & 0xffu) << 16) |
                (((y + tick) & 0xffu) << 8) |
                ((tick*3u) & 0xffu);
}

int main(void) {
    int rc = initialize();
    if (rc < 0) {
        printf("tsfp-gxm-smoke init failure %d\n", rc);
        cleanup();
        sceKernelExitProcess(1);
        return 1;
    }
    unsigned front = 0, back = 1;
    for (unsigned tick = 0; tick < FRAME_COUNT; ++tick) {
        paint(&frames[back], tick);
        sceGxmPadHeartbeat(&frames[back].color, frames[back].sync);
        void *address = frames[back].data;
        rc = sceGxmDisplayQueueAddEntry(frames[front].sync,
            frames[back].sync, &address);
        if (rc < 0) {
            printf("tsfp-gxm-smoke queue failure %d\n", rc);
            break;
        }
        ++submitted;
        front = back;
        back = (back + 1) % BUFS;
        sceDisplayWaitVblankStart();
    }
    printf("tsfp-gxm-smoke submitted %u cpu-painted frames, no GPU draw\n",
           submitted);
    cleanup();
    sceKernelExitProcess(rc < 0 ? 1 : 0);
    return rc < 0 ? 1 : 0;
}
