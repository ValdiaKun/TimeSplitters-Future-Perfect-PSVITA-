/* SPDX-License-Identifier: GPL-3.0-only
 * Asset-free native GXM vertical slice: GPU-executed, shader-driven indexed draws.
 *
 * What this exercises (cross-built only; NOT yet run on a physical Vita):
 *   context + ring buffers, render target, double-buffered CDRAM color surfaces,
 *   shader patcher with bounded USSE arenas, registered precompiled GXP
 *   programs, indexed sceGxmDraw inside sceGxmBeginScene/EndScene, and display
 *   queue submission. Pixels are produced by the GPU fragment program, not by
 *   the CPU (contrast with gxm_smoke.c).
 *
 * What it is NOT: the TimeSplitters renderer, a D3D8 adapter, a vertex-stream
 * test (the vendored vitaGL clear shaders derive their geometry from the vertex
 * INDEX and uniforms), a texture/depth/blend test, or a performance result.
 * No runtime shader compiler (libshacccg.suprx) is used or needed.
 *
 * Shader provenance: ports/vita/src/shaders/clear_{v,f}.h (vitaGL, ADAPT).
 * API usage follows the publicly documented VitaSDK GXM interfaces.
 */
#include <psp2/gxm.h>
#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/processmgr.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "usse_arena.h"
#include "shaders/clear_v.h"
#include "shaders/clear_f.h"

#define W 960u
#define H 544u
#define STRIDE 1024u
#define BUFS 2u
#define FRAME_BYTES (4u * STRIDE * H)
#define CDRAM_ALIGN (256u * 1024u)
#define PAGE_ALIGN (4u * 1024u)
#define USSE_POOL_BYTES (64u * 1024u)
#define FRAME_COUNT 180u
#define INDEX_COUNT 4u

typedef enum { MAP_NONE, MAP_GXM, MAP_VERTEX_USSE, MAP_FRAGMENT_USSE } MapKind;

typedef struct {
    SceUID uid;
    void *base;
    unsigned size;
    MapKind kind;
    unsigned usse_offset;
} Block;

typedef struct {
    TsfpUsseArena vertex;
    TsfpUsseArena fragment;
} UsseArenas;

typedef struct {
    Block block;
    SceGxmSyncObject *sync;
    SceGxmColorSurface color;
} Frame;

typedef struct {
    int gxm_initialized;
    void *context_host;
    Block vdm, vertex_ring, fragment_ring, fragment_usse_ring;
    SceGxmContext *context;
    SceGxmRenderTarget *target;
    Frame frames[BUFS];
    Block vertex_usse, fragment_usse, indices;
    UsseArenas arenas;
    SceGxmShaderPatcher *patcher;
    SceGxmShaderPatcherId vertex_id, fragment_id;
    int vertex_registered, fragment_registered;
    SceGxmVertexProgram *vertex_program;
    SceGxmFragmentProgram *fragment_program;
    const SceGxmProgramParameter *p_position, *p_depth, *p_color;
} Gpu;

static Gpu gpu;
static unsigned submitted;

static unsigned round_up(unsigned v, unsigned a) { return (v + a - 1u) & ~(a - 1u); }

static void block_free(Block *b) {
    if (b->base) {
        switch (b->kind) {
        case MAP_GXM: sceGxmUnmapMemory(b->base); break;
        case MAP_VERTEX_USSE: sceGxmUnmapVertexUsseMemory(b->base); break;
        case MAP_FRAGMENT_USSE: sceGxmUnmapFragmentUsseMemory(b->base); break;
        default: break;
        }
    }
    if (b->uid >= 0) sceKernelFreeMemBlock(b->uid);
    memset(b, 0, sizeof(*b));
    b->uid = -1;
}

/* attribs is only used for MAP_GXM. Failure leaves *b fully released. */
static int block_alloc(Block *b, const char *name, SceKernelMemBlockType type,
                       unsigned size, unsigned align, MapKind kind,
                       unsigned attribs) {
    memset(b, 0, sizeof(*b));
    b->uid = -1;
    b->size = round_up(size, align);
    b->uid = sceKernelAllocMemBlock(name, type, b->size, NULL);
    if (b->uid < 0) { b->uid = -1; return -1; }
    if (sceKernelGetMemBlockBase(b->uid, &b->base) < 0 || !b->base) {
        block_free(b);
        return -2;
    }
    int rc = 0;
    switch (kind) {
    case MAP_GXM: rc = sceGxmMapMemory(b->base, b->size, attribs); break;
    case MAP_VERTEX_USSE:
        rc = sceGxmMapVertexUsseMemory(b->base, b->size, &b->usse_offset); break;
    case MAP_FRAGMENT_USSE:
        rc = sceGxmMapFragmentUsseMemory(b->base, b->size, &b->usse_offset); break;
    default: break;
    }
    if (rc < 0) {
        block_free(b); /* kind is still MAP_NONE: never unmap what failed to map */
        return -3;
    }
    b->kind = kind;
    return 0;
}

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

static void *patcher_host_alloc(void *user, SceSize size) { (void)user; return malloc(size); }
static void patcher_host_free(void *user, void *mem) { (void)user; free(mem); }

static void *patcher_vertex_usse_alloc(void *user, SceSize size, unsigned int *offset) {
    return tsfp_usse_arena_alloc(&((UsseArenas *)user)->vertex, (uint32_t)size, 16u, offset);
}
static void patcher_vertex_usse_free(void *user, void *mem) {
    (void)tsfp_usse_arena_release(&((UsseArenas *)user)->vertex, mem);
}
static void *patcher_fragment_usse_alloc(void *user, SceSize size, unsigned int *offset) {
    return tsfp_usse_arena_alloc(&((UsseArenas *)user)->fragment, (uint32_t)size, 16u, offset);
}
static void patcher_fragment_usse_free(void *user, void *mem) {
    (void)tsfp_usse_arena_release(&((UsseArenas *)user)->fragment, mem);
}

/* Teardown is idempotent and safe after any partial initialization. The GPU is
 * drained first; objects are then released in reverse dependency order. */
static void cleanup(void) {
    Gpu *g = &gpu;
    if (g->context) sceGxmFinish(g->context);
    if (g->gxm_initialized) sceGxmDisplayQueueFinish();

    if (g->patcher) {
        if (g->fragment_program)
            sceGxmShaderPatcherReleaseFragmentProgram(g->patcher, g->fragment_program);
        if (g->vertex_program)
            sceGxmShaderPatcherReleaseVertexProgram(g->patcher, g->vertex_program);
        g->fragment_program = NULL;
        g->vertex_program = NULL;
        if (g->fragment_registered)
            sceGxmShaderPatcherUnregisterProgram(g->patcher, g->fragment_id);
        if (g->vertex_registered)
            sceGxmShaderPatcherUnregisterProgram(g->patcher, g->vertex_id);
        g->fragment_registered = g->vertex_registered = 0;
        sceGxmShaderPatcherDestroy(g->patcher);
        g->patcher = NULL;
    }
    if (g->target) { sceGxmDestroyRenderTarget(g->target); g->target = NULL; }
    if (g->context) { sceGxmDestroyContext(g->context); g->context = NULL; }
    for (unsigned i = 0; i < BUFS; ++i) {
        if (g->frames[i].sync) {
            sceGxmSyncObjectDestroy(g->frames[i].sync);
            g->frames[i].sync = NULL;
        }
        block_free(&g->frames[i].block);
    }
    block_free(&g->indices);
    block_free(&g->fragment_usse);
    block_free(&g->vertex_usse);
    block_free(&g->fragment_usse_ring);
    block_free(&g->fragment_ring);
    block_free(&g->vertex_ring);
    block_free(&g->vdm);
    if (g->gxm_initialized) { sceGxmTerminate(); g->gxm_initialized = 0; }
    free(g->context_host);
    g->context_host = NULL;
}

/* Informational device-side check: a copy with a destroyed magic number must
 * not validate. The outcome is logged, not asserted, because the exact
 * sceGxmProgramCheck error code is not verified on hardware. */
static void log_invalid_shader_check(void) {
    static unsigned char corrupt[sizeof(clear_f)] __attribute__((aligned(16)));
    memcpy(corrupt, clear_f, sizeof(corrupt));
    corrupt[0] ^= 0xffu;
    printf("tsfp-gxm-draw invalid-shader check returned %d (expect < 0)\n",
           sceGxmProgramCheck((const SceGxmProgram *)corrupt));
}

static int initialize(void) {
    Gpu *g = &gpu;
    memset(g, 0, sizeof(*g));
    for (unsigned i = 0; i < BUFS; ++i) g->frames[i].block.uid = -1;
    g->vdm.uid = g->vertex_ring.uid = g->fragment_ring.uid = -1;
    g->fragment_usse_ring.uid = g->vertex_usse.uid = -1;
    g->fragment_usse.uid = g->indices.uid = -1;

    /* Vendored blobs must match their recorded sizes before the SDK sees them. */
    if (sizeof(clear_v) != size_clear_v || sizeof(clear_f) != size_clear_f) return -9;
    if (sceGxmProgramCheck((const SceGxmProgram *)clear_v) < 0) return -10;
    if (sceGxmProgramCheck((const SceGxmProgram *)clear_f) < 0) return -11;

    SceGxmInitializeParams init;
    memset(&init, 0, sizeof(init));
    init.flags = SCE_GXM_INITIALIZE_FLAG_DEFAULT;
    init.displayQueueMaxPendingCount = 1;
    init.displayQueueCallback = display_callback;
    init.displayQueueCallbackDataSize = sizeof(void *);
    init.parameterBufferSize = SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE;
    if (sceGxmInitialize(&init) < 0) return -1;
    g->gxm_initialized = 1;

    const SceKernelMemBlockType nc = SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_NC_RW;
    if (block_alloc(&g->vdm, "tsfp-vdm", nc, SCE_GXM_DEFAULT_VDM_RING_BUFFER_SIZE,
                    PAGE_ALIGN, MAP_GXM, SCE_GXM_MEMORY_ATTRIB_READ) < 0) return -2;
    if (block_alloc(&g->vertex_ring, "tsfp-vertex-ring", nc,
                    SCE_GXM_DEFAULT_VERTEX_RING_BUFFER_SIZE, PAGE_ALIGN, MAP_GXM,
                    SCE_GXM_MEMORY_ATTRIB_READ) < 0) return -3;
    if (block_alloc(&g->fragment_ring, "tsfp-fragment-ring", nc,
                    SCE_GXM_DEFAULT_FRAGMENT_RING_BUFFER_SIZE, PAGE_ALIGN, MAP_GXM,
                    SCE_GXM_MEMORY_ATTRIB_READ) < 0) return -4;
    if (block_alloc(&g->fragment_usse_ring, "tsfp-fragment-usse-ring", nc,
                    SCE_GXM_DEFAULT_FRAGMENT_USSE_RING_BUFFER_SIZE, PAGE_ALIGN,
                    MAP_FRAGMENT_USSE, 0) < 0) return -5;

    g->context_host = malloc(SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE);
    if (!g->context_host) return -6;
    SceGxmContextParams cp;
    memset(&cp, 0, sizeof(cp));
    cp.hostMem = g->context_host;
    cp.hostMemSize = SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE;
    cp.vdmRingBufferMem = g->vdm.base;
    cp.vdmRingBufferMemSize = g->vdm.size;
    cp.vertexRingBufferMem = g->vertex_ring.base;
    cp.vertexRingBufferMemSize = g->vertex_ring.size;
    cp.fragmentRingBufferMem = g->fragment_ring.base;
    cp.fragmentRingBufferMemSize = g->fragment_ring.size;
    cp.fragmentUsseRingBufferMem = g->fragment_usse_ring.base;
    cp.fragmentUsseRingBufferMemSize = g->fragment_usse_ring.size;
    cp.fragmentUsseRingBufferOffset = g->fragment_usse_ring.usse_offset;
    if (sceGxmCreateContext(&cp, &g->context) < 0) { g->context = NULL; return -7; }

    SceGxmRenderTargetParams rt;
    memset(&rt, 0, sizeof(rt));
    rt.width = W;
    rt.height = H;
    rt.scenesPerFrame = 1;
    rt.multisampleMode = SCE_GXM_MULTISAMPLE_NONE;
    rt.driverMemBlock = -1;
    if (sceGxmCreateRenderTarget(&rt, &g->target) < 0) { g->target = NULL; return -8; }

    const unsigned fb_size = round_up(FRAME_BYTES, CDRAM_ALIGN);
    for (unsigned i = 0; i < BUFS; ++i) {
        Frame *f = &g->frames[i];
        if (block_alloc(&f->block, "tsfp-gxm-draw-fb", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
                        fb_size, CDRAM_ALIGN, MAP_GXM, SCE_GXM_MEMORY_ATTRIB_RW) < 0)
            return -20;
        if (sceGxmColorSurfaceInit(&f->color, SCE_GXM_COLOR_FORMAT_A8B8G8R8,
            SCE_GXM_COLOR_SURFACE_LINEAR, SCE_GXM_COLOR_SURFACE_SCALE_NONE,
            SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT, W, H, STRIDE, f->block.base) < 0)
            return -21;
        if (sceGxmSyncObjectCreate(&f->sync) < 0) { f->sync = NULL; return -22; }
        memset(f->block.base, 0, FRAME_BYTES);
    }

    if (block_alloc(&g->vertex_usse, "tsfp-patcher-vusse", nc, USSE_POOL_BYTES,
                    PAGE_ALIGN, MAP_VERTEX_USSE, 0) < 0) return -30;
    if (block_alloc(&g->fragment_usse, "tsfp-patcher-fusse", nc, USSE_POOL_BYTES,
                    PAGE_ALIGN, MAP_FRAGMENT_USSE, 0) < 0) return -31;
    if (tsfp_usse_arena_init(&g->arenas.vertex, g->vertex_usse.base,
                             g->vertex_usse.size, g->vertex_usse.usse_offset) < 0 ||
        tsfp_usse_arena_init(&g->arenas.fragment, g->fragment_usse.base,
                             g->fragment_usse.size, g->fragment_usse.usse_offset) < 0)
        return -32;

    SceGxmShaderPatcherParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.userData = &g->arenas;
    pp.hostAllocCallback = patcher_host_alloc;
    pp.hostFreeCallback = patcher_host_free;
    pp.vertexUsseAllocCallback = patcher_vertex_usse_alloc;
    pp.vertexUsseFreeCallback = patcher_vertex_usse_free;
    pp.vertexUsseMem = g->vertex_usse.base;
    pp.vertexUsseMemSize = g->vertex_usse.size;
    pp.vertexUsseOffset = g->vertex_usse.usse_offset;
    pp.fragmentUsseAllocCallback = patcher_fragment_usse_alloc;
    pp.fragmentUsseFreeCallback = patcher_fragment_usse_free;
    pp.fragmentUsseMem = g->fragment_usse.base;
    pp.fragmentUsseMemSize = g->fragment_usse.size;
    pp.fragmentUsseOffset = g->fragment_usse.usse_offset;
    if (sceGxmShaderPatcherCreate(&pp, &g->patcher) < 0) { g->patcher = NULL; return -33; }

    if (sceGxmShaderPatcherRegisterProgram(g->patcher, (const SceGxmProgram *)clear_v,
                                           &g->vertex_id) < 0) return -40;
    g->vertex_registered = 1;
    if (sceGxmShaderPatcherRegisterProgram(g->patcher, (const SceGxmProgram *)clear_f,
                                           &g->fragment_id) < 0) return -41;
    g->fragment_registered = 1;

    g->p_position = sceGxmProgramFindParameterByName((const SceGxmProgram *)clear_v, "position");
    g->p_depth = sceGxmProgramFindParameterByName((const SceGxmProgram *)clear_v, "u_clear_depth");
    g->p_color = sceGxmProgramFindParameterByName((const SceGxmProgram *)clear_f, "u_clear_color");
    if (!g->p_position || !g->p_depth || !g->p_color) return -42;

    /* No vertex attributes or streams: the program is driven by the INDEX input. */
    if (sceGxmShaderPatcherCreateVertexProgram(g->patcher, g->vertex_id, NULL, 0, NULL, 0,
                                               &g->vertex_program) < 0) {
        g->vertex_program = NULL;
        return -43;
    }
    if (sceGxmShaderPatcherCreateFragmentProgram(g->patcher, g->fragment_id,
            SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4, SCE_GXM_MULTISAMPLE_NONE, NULL,
            sceGxmShaderPatcherGetProgramFromId(g->vertex_id), &g->fragment_program) < 0) {
        g->fragment_program = NULL;
        return -44;
    }

    if (block_alloc(&g->indices, "tsfp-indices", nc, INDEX_COUNT * sizeof(uint16_t),
                    PAGE_ALIGN, MAP_GXM, SCE_GXM_MEMORY_ATTRIB_READ) < 0) return -50;
    uint16_t *idx = (uint16_t *)g->indices.base;
    for (unsigned i = 0; i < INDEX_COUNT; ++i) idx[i] = (uint16_t)i;
    return 0;
}

/* One indexed, shader-driven rectangle. rect = {x(idx 0,3), x(idx 1,2),
 * y(idx 0,1), y(idx 2,3)} in normalized device coordinates. */
static int draw_rect(Gpu *g, const float rect[4], const float color[4]) {
    static const float depth = 1.0f;
    void *vertex_uniforms = NULL, *fragment_uniforms = NULL;
    sceGxmSetVertexProgram(g->context, g->vertex_program);
    sceGxmSetFragmentProgram(g->context, g->fragment_program);
    if (sceGxmReserveVertexDefaultUniformBuffer(g->context, &vertex_uniforms) < 0) return -1;
    if (sceGxmSetUniformDataF(vertex_uniforms, g->p_position, 0, 4, rect) < 0) return -2;
    if (sceGxmSetUniformDataF(vertex_uniforms, g->p_depth, 0, 1, &depth) < 0) return -3;
    if (sceGxmReserveFragmentDefaultUniformBuffer(g->context, &fragment_uniforms) < 0) return -4;
    if (sceGxmSetUniformDataF(fragment_uniforms, g->p_color, 0, 4, color) < 0) return -5;
    return sceGxmDraw(g->context, SCE_GXM_PRIMITIVE_TRIANGLE_FAN, SCE_GXM_INDEX_FORMAT_U16,
                      g->indices.base, INDEX_COUNT) < 0 ? -6 : 0;
}

static int render_frame(Gpu *g, Frame *f, unsigned tick) {
    static const float background[4] = {0.04f, 0.06f, 0.16f, 1.0f};
    static const float fullscreen[4] = {-1.0f, 1.0f, -1.0f, 1.0f};
    const float phase = (float)(tick % 120u) / 120.0f;
    const float left = -0.85f + 1.1f * phase;
    const float rect[4] = {left, left + 0.6f, -0.35f, 0.35f};
    const float color[4] = {phase, 1.0f - phase, 0.5f, 1.0f};

    if (sceGxmBeginScene(g->context, 0, g->target, NULL, NULL, f->sync, &f->color, NULL) < 0)
        return -1;
    /* No depth surface is attached; set state explicitly rather than rely on
     * GXM defaults that this slice has not verified on hardware. */
    sceGxmSetFrontDepthFunc(g->context, SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetBackDepthFunc(g->context, SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontDepthWriteEnable(g->context, SCE_GXM_DEPTH_WRITE_DISABLED);
    sceGxmSetBackDepthWriteEnable(g->context, SCE_GXM_DEPTH_WRITE_DISABLED);
    sceGxmSetFrontPolygonMode(g->context, SCE_GXM_POLYGON_MODE_TRIANGLE_FILL);
    sceGxmSetBackPolygonMode(g->context, SCE_GXM_POLYGON_MODE_TRIANGLE_FILL);
    sceGxmSetCullMode(g->context, SCE_GXM_CULL_NONE);
    int rc = draw_rect(g, fullscreen, background);
    if (rc == 0) rc = draw_rect(g, rect, color);
    /* The scene must always be ended once begun, even after a draw failure. */
    int end = sceGxmEndScene(g->context, NULL, NULL);
    return rc < 0 ? rc - 10 : (end < 0 ? -2 : 0);
}

int main(void) {
    log_invalid_shader_check();
    int rc = initialize();
    if (rc < 0) {
        printf("tsfp-gxm-draw init failure %d\n", rc);
        cleanup();
        sceKernelExitProcess(1);
        return 1;
    }
    unsigned front = 0, back = 1;
    for (unsigned tick = 0; tick < FRAME_COUNT; ++tick) {
        rc = render_frame(&gpu, &gpu.frames[back], tick);
        if (rc < 0) {
            printf("tsfp-gxm-draw render failure %d at frame %u\n", rc, tick);
            break;
        }
        void *address = gpu.frames[back].block.base;
        rc = sceGxmDisplayQueueAddEntry(gpu.frames[front].sync, gpu.frames[back].sync, &address);
        if (rc < 0) {
            printf("tsfp-gxm-draw queue failure %d\n", rc);
            break;
        }
        ++submitted;
        front = back;
        back = (back + 1) % BUFS;
        sceDisplayWaitVblankStart();
    }
    printf("tsfp-gxm-draw submitted %u frames, 2 GPU shader draws each (unverified on device)\n",
           submitted);
    cleanup();
    sceKernelExitProcess(rc < 0 ? 1 : 0);
    return rc < 0 ? 1 : 0;
}
