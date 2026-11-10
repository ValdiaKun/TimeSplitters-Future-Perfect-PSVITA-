/* SPDX-License-Identifier: GPL-3.0-only
 * Original, opt-in native GXM attribute/texture/depth/blend fixture.
 * Programs are generated from authored Cg by the pinned MIT open-shacccg host
 * pipeline. No firmware, proprietary compiler, game shader or runtime compiler
 * is used. This is cross-buildable code, not hardware/gameplay validation.
 * The narrow compiler profile supports float4 POSITION + float2 TEXCOORD, with
 * four-lane reflected PA slots. COLOR varyings remain explicitly unsupported.
 */
#include <psp2/gxm.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "usse_arena.h"
#include "offline_programs.h"
#define W 64u
#define H 64u
#define PAGE 4096u
#define USSE_BYTES (64u*1024u)
#define HOST_LIMIT (128u*1024u)
enum { VDM,VERTEX_RING,FRAGMENT_RING,RING_USSE,PATCH_BUFFER,VERTEX_USSE,
    FRAGMENT_USSE,COLOR,DEPTH,STENCIL,GEOMETRY,INDICES,TEXTURES,BLOCK_COUNT };
typedef struct { float position[4],uv[4]; } Vertex;
_Static_assert(sizeof(float)==4 && sizeof(Vertex)==32 && offsetof(Vertex,uv)==16,"native vertex ABI");
typedef struct { SceUID uid; void *base; unsigned mapping,offset; } Block;
typedef union HostHeader { max_align_t alignment; struct { size_t bytes; } value; } HostHeader;
typedef struct {
    int initialized,scene,callback_error,error;
    const char *stage;
    Block blocks[BLOCK_COUNT];
    size_t host_used;
    TsfpUsseArena vertex_arena,fragment_arena;
    void *context_host;
    SceGxmContext *context;
    SceGxmRenderTarget *target;
    SceGxmColorSurface color;
    SceGxmDepthStencilSurface depth;
    SceGxmTexture checker,white;
    SceGxmShaderPatcher *patcher;
    SceGxmShaderPatcherId vertex_id,texture_id,tint_id;
    SceGxmVertexProgram *vertex;
    SceGxmFragmentProgram *texture,*tint_opaque,*tint_blend;
    const SceGxmProgramParameter *tint;
} Gpu;
static Gpu gpu;
static void barrier(void) { __asm__ volatile("dsb sy" ::: "memory"); }
static void unused_display(const void *data) { (void)data; }
static void *host_alloc(void *user,SceSize size) {
    Gpu *g=user;
    if (!size || (size_t)size>HOST_LIMIT-sizeof(HostHeader) ||
        (size_t)size+sizeof(HostHeader)>HOST_LIMIT-g->host_used) return NULL;
    HostHeader *header=malloc(sizeof(*header)+size); if (!header) return NULL;
    header->value.bytes=sizeof(*header)+size; g->host_used+=header->value.bytes; return header+1;
}
static void host_free(void *user,void *memory) {
    if (!memory) return;
    Gpu *g=user; HostHeader *header=(HostHeader *)memory-1;
    if (header->value.bytes>g->host_used) { g->callback_error=1; return; }
    g->host_used-=header->value.bytes; free(header);
}
static void *vertex_alloc(void *user,SceSize size,unsigned *offset) {
    return tsfp_usse_arena_alloc(&((Gpu *)user)->vertex_arena,size,16,offset);
}
static void vertex_free(void *user,void *memory) {
    if (!memory) return;
    Gpu *g=user; if (tsfp_usse_arena_release(&g->vertex_arena,memory)<0) g->callback_error=1;
}
static void *fragment_alloc(void *user,SceSize size,unsigned *offset) {
    return tsfp_usse_arena_alloc(&((Gpu *)user)->fragment_arena,size,16,offset);
}
static void fragment_free(void *user,void *memory) {
    if (!memory) return;
    Gpu *g=user; if (tsfp_usse_arena_release(&g->fragment_arena,memory)<0) g->callback_error=1;
}
static int allocate(Gpu *g,unsigned index,unsigned bytes,unsigned mapping) {
    Block *b=&g->blocks[index];
    if (!bytes || bytes>UINT32_MAX-(PAGE-1)) return -1;
    bytes=(bytes+PAGE-1)&~(PAGE-1);
    b->uid=sceKernelAllocMemBlock("tsfp-stream",SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,bytes,NULL);
    if (b->uid<0) return b->uid;
    int error=sceKernelGetMemBlockBase(b->uid,&b->base); if (error<0) return error;
    if (!b->base) return -1;
    if (mapping==1) error=sceGxmMapMemory(b->base,bytes,SCE_GXM_MEMORY_ATTRIB_RW);
    else if (mapping==2) error=sceGxmMapVertexUsseMemory(b->base,bytes,&b->offset);
    else error=sceGxmMapFragmentUsseMemory(b->base,bytes,&b->offset);
    if (error<0) return error;
    b->mapping=mapping; return 0;
}
/* Never free dependencies after a failed native destroy. On uncertainty the
 * static owner and all remaining native resources live until process exit. */
static int cleanup(Gpu *g) {
    int error;
#define RELEASE(label,expression) do { g->stage=(label); error=(expression); if (error<0) return error; } while (0)
    if (g->context) { RELEASE("destroy-context",sceGxmDestroyContext(g->context)); g->context=NULL; }
    if (g->tint_blend) { RELEASE("release-blend",sceGxmShaderPatcherReleaseFragmentProgram(g->patcher,g->tint_blend)); g->tint_blend=NULL; }
    if (g->tint_opaque) { RELEASE("release-tint",sceGxmShaderPatcherReleaseFragmentProgram(g->patcher,g->tint_opaque)); g->tint_opaque=NULL; }
    if (g->texture) { RELEASE("release-texture",sceGxmShaderPatcherReleaseFragmentProgram(g->patcher,g->texture)); g->texture=NULL; }
    if (g->vertex) { RELEASE("release-vertex",sceGxmShaderPatcherReleaseVertexProgram(g->patcher,g->vertex)); g->vertex=NULL; }
    if (g->tint_id) { RELEASE("unregister-tint",sceGxmShaderPatcherUnregisterProgram(g->patcher,g->tint_id)); g->tint_id=NULL; }
    if (g->texture_id) { RELEASE("unregister-texture",sceGxmShaderPatcherUnregisterProgram(g->patcher,g->texture_id)); g->texture_id=NULL; }
    if (g->vertex_id) { RELEASE("unregister-vertex",sceGxmShaderPatcherUnregisterProgram(g->patcher,g->vertex_id)); g->vertex_id=NULL; }
    if (g->patcher) { RELEASE("destroy-patcher",sceGxmShaderPatcherDestroy(g->patcher)); g->patcher=NULL; }
    if (g->callback_error || g->host_used || g->vertex_arena.live || g->fragment_arena.live) { g->stage="patcher-ownership"; return -1; }
    if (g->target) { RELEASE("destroy-target",sceGxmDestroyRenderTarget(g->target)); g->target=NULL; }
    for (unsigned i=BLOCK_COUNT;i>0;--i) {
        Block *b=&g->blocks[i-1];
        if (b->mapping==1) RELEASE("unmap-memory",sceGxmUnmapMemory(b->base));
        else if (b->mapping==2) RELEASE("unmap-vertex-usse",sceGxmUnmapVertexUsseMemory(b->base));
        else if (b->mapping==3) RELEASE("unmap-fragment-usse",sceGxmUnmapFragmentUsseMemory(b->base));
        b->mapping=0;
        if (b->uid>=0) { RELEASE("free-memory",sceKernelFreeMemBlock(b->uid)); b->uid=-1; }
    }
    if (g->initialized) { RELEASE("terminate",sceGxmTerminate()); g->initialized=0; }
#undef RELEASE
    free(g->context_host); g->context_host=NULL; return 0;
}
static int parameter(const SceGxmProgramParameter *p,SceGxmParameterCategory category,unsigned components,unsigned resource) {
    return p && sceGxmProgramParameterGetCategory(p)==category &&
        sceGxmProgramParameterGetType(p)==SCE_GXM_PARAMETER_TYPE_F32 &&
        sceGxmProgramParameterGetComponentCount(p)==components &&
        sceGxmProgramParameterGetArraySize(p)==1 && sceGxmProgramParameterGetResourceIndex(p)==resource;
}
static int texture_init(SceGxmTexture *texture,const void *pixels) {
    int e=sceGxmTextureInitLinear(texture,pixels,SCE_GXM_TEXTURE_FORMAT_A8B8G8R8,8,8,1); if (e<0) return e;
    e=sceGxmTextureSetMinFilter(texture,SCE_GXM_TEXTURE_FILTER_POINT); if (e<0) return e;
    e=sceGxmTextureSetMagFilter(texture,SCE_GXM_TEXTURE_FILTER_POINT); if (e<0) return e;
    e=sceGxmTextureSetUAddrMode(texture,SCE_GXM_TEXTURE_ADDR_CLAMP); if (e<0) return e;
    return sceGxmTextureSetVAddrMode(texture,SCE_GXM_TEXTURE_ADDR_CLAMP);
}
static void quad(Vertex *vertices,float extent,float depth) {
    const Vertex shape[4]={{{-extent,extent,depth,1},{0,0,0,0}},
        {{extent,extent,depth,1},{1,0,0,0}},{{-extent,-extent,depth,1},{0,1,0,0}},
        {{extent,-extent,depth,1},{1,1,0,0}}};
    memcpy(vertices,shape,sizeof(shape));
}
static int draw(Gpu *g,const Vertex *vertices,const SceGxmTexture *texture,SceGxmFragmentProgram *program,
    const float *tint,SceGxmDepthFunc depth) {
    sceGxmSetVertexProgram(g->context,g->vertex); sceGxmSetFragmentProgram(g->context,program);
    sceGxmSetFrontDepthFunc(g->context,depth); sceGxmSetBackDepthFunc(g->context,depth);
    sceGxmSetFrontDepthWriteEnable(g->context,SCE_GXM_DEPTH_WRITE_ENABLED);
    sceGxmSetBackDepthWriteEnable(g->context,SCE_GXM_DEPTH_WRITE_ENABLED);
    if (tint) {
        void *uniform=NULL; int error=sceGxmReserveFragmentDefaultUniformBuffer(g->context,&uniform); if (error<0) return error;
        error=sceGxmSetUniformDataF(uniform,g->tint,0,4,tint); if (error<0) return error;
    }
    int error=sceGxmSetVertexStream(g->context,0,vertices); if (error<0) return error;
    error=sceGxmSetFragmentTexture(g->context,0,texture); if (error<0) return error;
    return sceGxmDraw(g->context,SCE_GXM_PRIMITIVE_TRIANGLES,SCE_GXM_INDEX_FORMAT_U16,g->blocks[INDICES].base,6);
}
static int sample(const Gpu *g,unsigned x,unsigned y,unsigned r,unsigned green,unsigned b,unsigned a,unsigned tolerance) {
    const unsigned char *p=(const unsigned char *)g->blocks[COLOR].base+(y*W+x)*4;
    const unsigned expected[]={r,green,b,a}; int ok=1;
    for (unsigned i=0;i<4;++i) if (abs((int)p[i]-(int)expected[i])>(int)tolerance) ok=0;
    printf("gxm-stream sample%u,%u rgba=%u,%u,%u,%u expected=%u,%u,%u,%u tolerance=%u %s\n",
        x,y,p[0],p[1],p[2],p[3],r,green,b,a,tolerance,ok ? "pass" : "FAIL");
    return ok;
}
static int run(void) {
    Gpu *g=&gpu;
    memset(g,0,sizeof(*g)); for (unsigned i=0;i<BLOCK_COUNT;++i) g->blocks[i].uid=-1;
    int result=0;
#define CHECK(label,expression) do { g->stage=(label); int e=(expression); if (e<0) { g->error=e; goto fail; } } while (0)
    const SceGxmProgram *vp=tsfp_offline_texture_vertex,*fp=tsfp_offline_texture_fragment,*tp=tsfp_offline_tint_fragment;
    if (!vp || !fp || !tp) { g->stage="missing-program"; g->error=-1; goto fail; }
    CHECK("vertex-image",sceGxmProgramCheck(vp)); CHECK("texture-image",sceGxmProgramCheck(fp)); CHECK("tint-image",sceGxmProgramCheck(tp));
    const SceGxmProgramParameter *position=sceGxmProgramFindParameterByName(vp,"aPosition");
    const SceGxmProgramParameter *uv=sceGxmProgramFindParameterByName(vp,"aTexcoord");
    const SceGxmProgramParameter *tex=sceGxmProgramFindParameterByName(fp,"tex");
    const SceGxmProgramParameter *tinted_tex=sceGxmProgramFindParameterByName(tp,"tex");
    g->tint=sceGxmProgramFindParameterByName(tp,"tint");
    if (sceGxmProgramGetType(vp)!=SCE_GXM_VERTEX_PROGRAM || sceGxmProgramGetType(fp)!=SCE_GXM_FRAGMENT_PROGRAM ||
        sceGxmProgramGetType(tp)!=SCE_GXM_FRAGMENT_PROGRAM || sceGxmProgramGetParameterCount(vp)!=2 ||
        sceGxmProgramGetParameterCount(fp)!=1 || sceGxmProgramGetParameterCount(tp)!=2 ||
        !parameter(position,SCE_GXM_PARAMETER_CATEGORY_ATTRIBUTE,4,0) ||
        !parameter(uv,SCE_GXM_PARAMETER_CATEGORY_ATTRIBUTE,4,4) ||
        !parameter(tex,SCE_GXM_PARAMETER_CATEGORY_SAMPLER,4,0) ||
        !parameter(tinted_tex,SCE_GXM_PARAMETER_CATEGORY_SAMPLER,4,0) ||
        !parameter(g->tint,SCE_GXM_PARAMETER_CATEGORY_UNIFORM,4,0) ||
        sceGxmProgramParameterGetSemantic(position)!=SCE_GXM_PARAMETER_SEMANTIC_POSITION ||
        sceGxmProgramParameterGetSemantic(uv)!=SCE_GXM_PARAMETER_SEMANTIC_TEXCOORD ||
        sceGxmProgramParameterGetContainerIndex(g->tint)!=14 ||
        sceGxmProgramParameterIsSamplerCube(tex) || sceGxmProgramParameterIsSamplerCube(tinted_tex)) {
        g->stage="shader-interface"; g->error=-1; goto fail;
    }
    SceGxmInitializeParams init={0}; init.flags=SCE_GXM_INITIALIZE_FLAG_DEFAULT;
    init.displayQueueMaxPendingCount=1; init.displayQueueCallback=unused_display; init.displayQueueCallbackDataSize=4;
    init.parameterBufferSize=256u*1024u;
    CHECK("initialize",sceGxmInitialize(&init)); g->initialized=1;
    CHECK("vdm",allocate(g,VDM,SCE_GXM_DEFAULT_VDM_RING_BUFFER_SIZE,1));
    CHECK("vertex-ring",allocate(g,VERTEX_RING,SCE_GXM_DEFAULT_VERTEX_RING_BUFFER_SIZE,1));
    CHECK("fragment-ring",allocate(g,FRAGMENT_RING,SCE_GXM_DEFAULT_FRAGMENT_RING_BUFFER_SIZE,1));
    CHECK("ring-usse",allocate(g,RING_USSE,SCE_GXM_DEFAULT_FRAGMENT_USSE_RING_BUFFER_SIZE,3));
    g->context_host=malloc(SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE);
    if (!g->context_host) { g->stage="host-context"; g->error=-1; goto fail; }
    SceGxmContextParams cp={0}; cp.hostMem=g->context_host; cp.hostMemSize=SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE;
    cp.vdmRingBufferMem=g->blocks[VDM].base; cp.vdmRingBufferMemSize=SCE_GXM_DEFAULT_VDM_RING_BUFFER_SIZE;
    cp.vertexRingBufferMem=g->blocks[VERTEX_RING].base; cp.vertexRingBufferMemSize=SCE_GXM_DEFAULT_VERTEX_RING_BUFFER_SIZE;
    cp.fragmentRingBufferMem=g->blocks[FRAGMENT_RING].base; cp.fragmentRingBufferMemSize=SCE_GXM_DEFAULT_FRAGMENT_RING_BUFFER_SIZE;
    cp.fragmentUsseRingBufferMem=g->blocks[RING_USSE].base; cp.fragmentUsseRingBufferMemSize=SCE_GXM_DEFAULT_FRAGMENT_USSE_RING_BUFFER_SIZE;
    cp.fragmentUsseRingBufferOffset=g->blocks[RING_USSE].offset;
    CHECK("context",sceGxmCreateContext(&cp,&g->context));
    SceGxmRenderTargetParams target={0}; target.width=W; target.height=H; target.scenesPerFrame=1;
    target.multisampleMode=SCE_GXM_MULTISAMPLE_NONE; target.driverMemBlock=-1;
    CHECK("target",sceGxmCreateRenderTarget(&target,&g->target));
    CHECK("color",allocate(g,COLOR,W*H*4,1)); CHECK("depth",allocate(g,DEPTH,W*H*4,1));
    CHECK("stencil",allocate(g,STENCIL,W*H*4,1));
    CHECK("color-surface",sceGxmColorSurfaceInit(&g->color,SCE_GXM_COLOR_FORMAT_A8B8G8R8,
        SCE_GXM_COLOR_SURFACE_LINEAR,SCE_GXM_COLOR_SURFACE_SCALE_NONE,SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT,W,H,W,g->blocks[COLOR].base));
    CHECK("disable-dither",sceGxmColorSurfaceSetDitherMode(&g->color,SCE_GXM_COLOR_SURFACE_DITHER_DISABLED));
    CHECK("depth-surface",sceGxmDepthStencilSurfaceInit(&g->depth,SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24,
        SCE_GXM_DEPTH_STENCIL_SURFACE_TILED,W,g->blocks[DEPTH].base,g->blocks[STENCIL].base));
    sceGxmDepthStencilSurfaceSetBackgroundDepth(&g->depth,1);
    sceGxmDepthStencilSurfaceSetBackgroundStencil(&g->depth,0);
    CHECK("patch-buffer",allocate(g,PATCH_BUFFER,USSE_BYTES,1));
    CHECK("vertex-usse",allocate(g,VERTEX_USSE,USSE_BYTES,2)); CHECK("fragment-usse",allocate(g,FRAGMENT_USSE,USSE_BYTES,3));
    CHECK("vertex-arena",tsfp_usse_arena_init(&g->vertex_arena,g->blocks[VERTEX_USSE].base,USSE_BYTES,g->blocks[VERTEX_USSE].offset));
    CHECK("fragment-arena",tsfp_usse_arena_init(&g->fragment_arena,g->blocks[FRAGMENT_USSE].base,USSE_BYTES,g->blocks[FRAGMENT_USSE].offset));
    SceGxmShaderPatcherParams patch={0}; patch.userData=g; patch.hostAllocCallback=host_alloc; patch.hostFreeCallback=host_free;
    patch.bufferMem=g->blocks[PATCH_BUFFER].base; patch.bufferMemSize=USSE_BYTES;
    patch.vertexUsseAllocCallback=vertex_alloc; patch.vertexUsseFreeCallback=vertex_free;
    patch.vertexUsseMem=g->blocks[VERTEX_USSE].base; patch.vertexUsseMemSize=USSE_BYTES; patch.vertexUsseOffset=g->blocks[VERTEX_USSE].offset;
    patch.fragmentUsseAllocCallback=fragment_alloc; patch.fragmentUsseFreeCallback=fragment_free;
    patch.fragmentUsseMem=g->blocks[FRAGMENT_USSE].base; patch.fragmentUsseMemSize=USSE_BYTES; patch.fragmentUsseOffset=g->blocks[FRAGMENT_USSE].offset;
    CHECK("patcher",sceGxmShaderPatcherCreate(&patch,&g->patcher));
    CHECK("register-vertex",sceGxmShaderPatcherRegisterProgram(g->patcher,vp,&g->vertex_id));
    CHECK("register-texture",sceGxmShaderPatcherRegisterProgram(g->patcher,fp,&g->texture_id));
    CHECK("register-tint",sceGxmShaderPatcherRegisterProgram(g->patcher,tp,&g->tint_id));
    const SceGxmVertexAttribute attrs[2]={{0,0,SCE_GXM_ATTRIBUTE_FORMAT_F32,4,0},{0,16,SCE_GXM_ATTRIBUTE_FORMAT_F32,4,4}};
    const SceGxmVertexStream stream={sizeof(Vertex),SCE_GXM_INDEX_SOURCE_INDEX_16BIT};
    CHECK("vertex-program",sceGxmShaderPatcherCreateVertexProgram(g->patcher,g->vertex_id,attrs,2,&stream,1,&g->vertex));
    SceGxmBlendInfo blend={0}; blend.colorMask=SCE_GXM_COLOR_MASK_ALL;
    blend.colorFunc=SCE_GXM_BLEND_FUNC_ADD; blend.alphaFunc=SCE_GXM_BLEND_FUNC_ADD;
    blend.colorSrc=SCE_GXM_BLEND_FACTOR_ONE; blend.colorDst=SCE_GXM_BLEND_FACTOR_ZERO;
    blend.alphaSrc=SCE_GXM_BLEND_FACTOR_ONE; blend.alphaDst=SCE_GXM_BLEND_FACTOR_ZERO;
    CHECK("texture-program",sceGxmShaderPatcherCreateFragmentProgram(g->patcher,g->texture_id,
        SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,SCE_GXM_MULTISAMPLE_NONE,&blend,vp,&g->texture));
    CHECK("tint-program",sceGxmShaderPatcherCreateFragmentProgram(g->patcher,g->tint_id,
        SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,SCE_GXM_MULTISAMPLE_NONE,&blend,vp,&g->tint_opaque));
    blend.colorSrc=SCE_GXM_BLEND_FACTOR_SRC_ALPHA; blend.colorDst=SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alphaSrc=SCE_GXM_BLEND_FACTOR_ONE; blend.alphaDst=SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    CHECK("blend-program",sceGxmShaderPatcherCreateFragmentProgram(g->patcher,g->tint_id,
        SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,SCE_GXM_MULTISAMPLE_NONE,&blend,vp,&g->tint_blend));
    CHECK("geometry",allocate(g,GEOMETRY,PAGE,1)); CHECK("indices",allocate(g,INDICES,PAGE,1));
    CHECK("textures",allocate(g,TEXTURES,PAGE,1));
    Vertex *vertices=g->blocks[GEOMETRY].base;
    quad(vertices,1,1); quad(vertices+4,.75f,.5f); quad(vertices+8,.75f,.75f); quad(vertices+12,.75f,.25f);
    const uint16_t indices[]={0,1,2,2,1,3}; memcpy(g->blocks[INDICES].base,indices,sizeof(indices));
    uint32_t *pixels=g->blocks[TEXTURES].base;
    for (unsigned y=0;y<8;++y) for (unsigned x=0;x<8;++x) {
        pixels[y*8+x]=y<4 ? (x<4 ? UINT32_C(0xff0000ff) : UINT32_C(0xff00ff00)) :
            (x<4 ? UINT32_C(0xffff0000) : UINT32_C(0xffffffff));
        pixels[64+y*8+x]=UINT32_C(0xffffffff);
    }
    CHECK("checker-texture",texture_init(&g->checker,pixels)); CHECK("white-texture",texture_init(&g->white,pixels+64));
    barrier();
    CHECK("begin-scene",sceGxmBeginScene(g->context,0,g->target,NULL,NULL,NULL,&g->color,&g->depth)); g->scene=1;
    sceGxmSetViewport(g->context,32,32,32,-32,.5f,.5f); sceGxmSetCullMode(g->context,SCE_GXM_CULL_NONE);
    sceGxmSetFrontPolygonMode(g->context,SCE_GXM_POLYGON_MODE_TRIANGLE_FILL);
    sceGxmSetBackPolygonMode(g->context,SCE_GXM_POLYGON_MODE_TRIANGLE_FILL);
    sceGxmSetFrontStencilRef(g->context,0); sceGxmSetBackStencilRef(g->context,0);
    sceGxmSetFrontStencilFunc(g->context,SCE_GXM_STENCIL_FUNC_ALWAYS,SCE_GXM_STENCIL_OP_KEEP,
        SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,0xff,0xff);
    sceGxmSetBackStencilFunc(g->context,SCE_GXM_STENCIL_FUNC_ALWAYS,SCE_GXM_STENCIL_OP_KEEP,
        SCE_GXM_STENCIL_OP_KEEP,SCE_GXM_STENCIL_OP_KEEP,0xff,0xff);
    const float black[]={0,0,0,1},blue[]={0,0,1,1},green[]={0,1,0,128.0f/255.0f};
    CHECK("draw-background",draw(g,vertices,&g->white,g->tint_opaque,black,SCE_GXM_DEPTH_FUNC_ALWAYS));
    CHECK("draw-checker",draw(g,vertices+4,&g->checker,g->texture,NULL,SCE_GXM_DEPTH_FUNC_LESS));
    CHECK("draw-occluded",draw(g,vertices+8,&g->white,g->tint_opaque,blue,SCE_GXM_DEPTH_FUNC_LESS));
    CHECK("draw-blended",draw(g,vertices+12,&g->white,g->tint_blend,green,SCE_GXM_DEPTH_FUNC_LESS));
    g->stage="end-scene"; result=sceGxmEndScene(g->context,NULL,NULL);
    if (result<0) { printf("gxm-stream end-scene error0x%08x; retain resources until exit\n",(unsigned)result); return result; }
    g->scene=0; printf("gxm-stream waiting for native completion before readback\n"); fflush(stdout);
    sceGxmFinish(g->context); barrier();
    int pixels_ok=1;
    pixels_ok&=sample(g,16,16,127,128,0,255,1); pixels_ok&=sample(g,48,16,0,255,0,255,1);
    pixels_ok&=sample(g,16,48,0,128,127,255,1); pixels_ok&=sample(g,48,48,127,255,127,255,1);
    pixels_ok&=sample(g,2,2,0,0,0,255,0); if (!pixels_ok) result=-1;
    goto release;
fail:
    result=g->error; printf("gxm-stream stage=%s error0x%08x\n",g->stage,(unsigned)result);
    if (g->scene) {
        int error=sceGxmEndScene(g->context,NULL,NULL);
        if (error<0) { printf("gxm-stream scene recovery error0x%08x; retain resources until exit\n",(unsigned)error); return result; }
        g->scene=0;
    }
    if (g->context) sceGxmFinish(g->context);
release:
    {
        int error=cleanup(g);
        if (error<0) { printf("gxm-stream cleanup=%s error0x%08x; retain remaining resources until exit\n",g->stage,(unsigned)error); return error; }
    }
#undef CHECK
    if (!result) printf("gxm-stream native pixel checks passed; not game/render-performance validation\n");
    return result;
}
int main(void) {
    printf("TSFP experimental offline-Cg GXM stream fixture: no physical validation assumed\n");
    int result=run();
    printf("gxm-stream result=%d (no display submission, title execution or FPS claim)\n",result);
    fflush(stdout);
    sceKernelExitProcess(result ? 1 : 0); return result ? 1 : 0;
}
