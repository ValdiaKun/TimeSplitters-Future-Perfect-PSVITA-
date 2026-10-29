/* SPDX-License-Identifier: GPL-3.0-only */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "recomp_types.h"
#include "memory.h"
#include "string_ops.h"
#include "memory_test_support.h"
#include "string_native_oracle.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
recomp_func_t string_fixture(unsigned kind, unsigned width, unsigned mode);
void fixture_string_seed(void);
void fixture_string_cld(void); void fixture_string_std(void);
void fixture_string_branch(void); void fixture_string_carry(void);
recomp_func_t string_flag_fixture(unsigned kind, unsigned width, unsigned mode);
#define FLAGS_MASK 0xcd5u
/* Independent byte model: canonical backing locations, never host guest pointers. */
typedef struct Image { unsigned char zero[64], low[512], end[64], contiguous[512], contiguous_zero[64]; } Image;
static const uint32_t regions[] = {0,0x40000,0x03ffffc0,0x80040000,0x80000000};
static unsigned char *image_byte(Image *image, uint32_t address) {
    uint32_t offset;
    if (address < 0x74000000) {
        offset = address % 0x04000000;
        if (offset < 64) return image->zero + offset;
        if (offset >= 0x40000 && offset < 0x40200) return image->low + offset - 0x40000;
        if (offset >= 0x03ffffc0) return image->end + offset - 0x03ffffc0;
    } else if ((address >= 0x80000000 && address < 0x80000040) ||
               (address >= 0xf0000000 && address < 0xf0000040)) {
        return image->contiguous_zero + (address & 0x0fffffff);
    } else if ((address >= 0x80040000 && address < 0x80040200) ||
               (address >= 0xf0040000 && address < 0xf0040200)) {
        offset = address & 0x0fffffff;
        return image->contiguous + offset - 0x40000;
    } else return NULL;
    assert(!"string test accessed a valid but unmodeled backing byte");
    return NULL;
}
static int model_valid(Image *image, uint32_t address, unsigned width) {
    if ((uint64_t)address + width > UINT64_C(0x100000000)) return 0;
    for (unsigned i = 0; i < width; ++i) if (!image_byte(image,address+i)) return 0;
    return 1;
}
static uint32_t model_load(Image *image, uint32_t address, unsigned width) {
    uint32_t value = 0;
    for (unsigned i = 0; i < width; ++i) value |= (uint32_t)*image_byte(image,address+i) << (8u*i);
    return value;
}
static void model_store(Image *image, uint32_t address, unsigned width, uint32_t value) {
    for (unsigned i = 0; i < width; ++i) *image_byte(image,address+i) = (unsigned char)(value >> (8u*i));
}
static uint32_t expected_flags(uint32_t left, uint32_t right, unsigned width, uint32_t previous) {
    unsigned bits = width * 8u;
    uint64_t limit = UINT64_C(1) << bits;
    uint32_t mask = (uint32_t)(limit - 1u), sign = (uint32_t)(limit >> 1);
    left &= mask; right &= mask;
    uint32_t result = (left - right) & mask;
    int64_t l = (int64_t)left - (left & sign ? (int64_t)limit : 0);
    int64_t r = (int64_t)right - (right & sign ? (int64_t)limit : 0);
    int64_t difference = l - r;
    unsigned ones = 0;
    for (unsigned bit = 0; bit < 8; ++bit) ones += (result >> bit) & 1u;
    return (previous & ~0x8d5u) | (left < right) | ((ones % 2 == 0) ? 4u : 0u) |
        ((left & 15u) < (right & 15u) ? 16u : 0u) | (!result ? 64u : 0u) |
        (result & sign ? 128u : 0u) |
        (difference < -(int64_t)(limit/2) || difference >= (int64_t)(limit/2) ? 2048u : 0u);
}
static void model(unsigned kind, unsigned width, unsigned repeat, StringState *state, Image *image, int intel_fault_flags) {
    uint32_t entry_flags=state->flags;
    if (repeat && !state->count) return;
    uint32_t step = state->flags & 0x400 ? 0u-width : width;
    for (;;) {
        uint32_t left = state->accumulator, right;
        uint32_t fault_address = state->source;
        if ((kind == TSFP_MOVS || kind == TSFP_CMPS || kind == TSFP_LODS) &&
            !model_valid(image,state->source,width)) goto fault;
        if (kind == TSFP_MOVS || kind == TSFP_CMPS || kind == TSFP_LODS)
            left = model_load(image,state->source,width);
        fault_address = state->destination;
        if (kind != TSFP_LODS && !model_valid(image,state->destination,width)) goto fault;
        if (kind == TSFP_MOVS || kind == TSFP_STOS) model_store(image,state->destination,width,left);
        else if (kind == TSFP_CMPS || kind == TSFP_SCAS) {
            right = model_load(image,state->destination,width);
            state->flags = expected_flags(left,right,width,state->flags);
        } else {
            uint32_t mask = width == 4 ? UINT32_MAX : (1u << (width*8u)) - 1u;
            state->accumulator = (state->accumulator & ~mask) | (left & mask);
        }
        if (kind == TSFP_MOVS || kind == TSFP_CMPS || kind == TSFP_LODS) state->source += step;
        if (kind != TSFP_LODS) state->destination += step;
        if (!repeat) return;
        if (!--state->count) return;
        if ((kind == TSFP_CMPS || kind == TSFP_SCAS) &&
            (!!(state->flags & 64u) != (repeat == TSFP_REP_EQUAL))) return;
        continue;
    fault:
        if (intel_fault_flags && repeat && (kind==TSFP_CMPS || kind==TSFP_SCAS)) state->flags=entry_flags;
        state->fault = 1;
        state->fault_address = fault_address;
        state->fault_width = width;
        return;
    }
}
static void image_transfer(Image *image, int publish) {
    unsigned char *parts[] = {image->zero,image->low,image->end,image->contiguous,image->contiguous_zero};
    const size_t sizes[] = {64,512,64,512,64};
    for (unsigned i = 0; i < 5; ++i) {
        if (publish) tsfp_guest_write(regions[i],parts[i],sizes[i]);
        else tsfp_guest_read(regions[i],parts[i],sizes[i]);
    }
}
static void compare_state(const StringState *actual, const StringState *expected) {
    if (actual->source != expected->source || actual->destination != expected->destination ||
        actual->count != expected->count || actual->accumulator != expected->accumulator ||
        (actual->flags & FLAGS_MASK) != (expected->flags & FLAGS_MASK) || actual->fault != expected->fault) {
        fprintf(stderr,"string state mismatch src=%08x/%08x dst=%08x/%08x count=%u/%u eax=%08x/%08x flags=%x/%x fault=%d/%d\n",
            actual->source,expected->source,actual->destination,expected->destination,actual->count,expected->count,
            actual->accumulator,expected->accumulator,actual->flags,expected->flags,actual->fault,expected->fault);
        assert(!"string state differs from oracle");
    }
}
static void execute(void *context) { (*(recomp_func_t *)context)(); }
static unsigned cases;
static void run_case(unsigned kind, unsigned width, unsigned repeat, StringState input, const Image *initial, int use_native) {
    Image expected_image = *initial, actual_image;
    StringState expected = input;
    model(kind,width,repeat,&expected,&expected_image,1);
#ifdef TSFP_PC_MAPPED_ORACLE
    if (use_native) {
        image_transfer((Image *)initial,1);
        StringState native = input;
        native_string_oracle(kind,width,repeat,&native);
        StringState native_expected=input;
        Image native_image=*initial;
        model(kind,width,repeat,&native_expected,&native_image,!native_string_amd());
        compare_state(&native,&native_expected);
        image_transfer(&actual_image,0);
        if (memcmp(&actual_image,&expected_image,sizeof(actual_image)) != 0) {
            fprintf(stderr,"native string image mismatch kind=%u width=%u repeat=%u src=%08x dst=%08x count=%u flags=%x\n",kind,width,repeat,input.source,input.destination,input.count,input.flags);
            for (size_t n=0;n<sizeof(actual_image);++n)
                if (((unsigned char *)&actual_image)[n]!=((unsigned char *)&expected_image)[n]) {
                    fprintf(stderr,"image byte=%zu actual=%02x expected=%02x\n",n,((unsigned char *)&actual_image)[n],((unsigned char *)&expected_image)[n]); break;
                }
            assert(!"native string image differs from byte model");
        }
    }
#else
    (void)use_native;
#endif
    image_transfer((Image *)initial,1);
    g_esi = input.source; g_edi = input.destination; g_ecx = input.count; g_eax = input.accumulator;
    g_df = !!(input.flags & 0x400u); g_tsfp_string_flags = input.flags;
    g_ebx = 0xc0ffee; g_edx = 0xbadcafe;
    g_esp = 0x1f000; PUSH32(g_esp,0x10ffe);
    recomp_func_t function = string_fixture(kind,width,repeat);
    assert(function);
    if (expected.fault) test_expect_fault(execute,&function,expected.fault_address,expected.fault_width);
    else function();
    StringState actual = {g_esi,g_edi,g_ecx,g_eax,g_tsfp_string_flags,0,0,expected.fault};
    compare_state(&actual,&expected);
    assert(g_ebx == 0xc0ffee && g_edx == 0xbadcafe && g_df == !!(input.flags & 0x400u));
    assert(g_esp == (expected.fault ? 0x1effc : 0x1f000));
    image_transfer(&actual_image,0);
    assert(memcmp(&actual_image,&expected_image,sizeof(actual_image)) == 0);
    ++cases;
    if (kind == TSFP_CMPS || kind == TSFP_SCAS) {
        /* Run actual generated condition consumers, not only the helper flags. */
        image_transfer((Image *)initial,1);
        g_esi=input.source; g_edi=input.destination; g_ecx=input.count; g_eax=input.accumulator;
        g_tsfp_string_flags=input.flags; g_edx=0xbadcafe; g_esp=0x1f000; PUSH32(g_esp,0x10ffe);
        recomp_func_t consumer=string_flag_fixture(kind,width,repeat);
        if (expected.fault) test_expect_fault(execute,&consumer,expected.fault_address,expected.fault_width);
        else {
            consumer();
            assert(g_eax == ((expected.accumulator & ~0xff00u) | (((expected.flags & 0xd5u) | 2u) << 8)));
            assert((g_edx & 255u) == !!(expected.flags & 0x800u));
        }
        assert(g_esi==expected.source && g_edi==expected.destination && g_ecx==expected.count);
        assert((g_tsfp_string_flags & FLAGS_MASK)==(expected.flags & FLAGS_MASK));
        image_transfer(&actual_image,0);
        assert(memcmp(&actual_image,&expected_image,sizeof(actual_image))==0);
    }
}
void test_strings(void) {
    Image initial;
    unsigned char *bytes = (unsigned char *)&initial;
    for (unsigned i = 0; i < sizeof(initial); ++i) bytes[i] = (unsigned char)(i*13u+5u);
    for (unsigned kind = 1; kind <= 5; ++kind) for (unsigned width = 1; width <= 4; width *= 2)
    for (unsigned repeat = 0; repeat <= 2; ++repeat) for (unsigned direction = 0; direction <= 1; ++direction) {
        StringState input = {0x40040,0x40140,5,0xdeadbeef,0x893u | (direction ? 0x400u : 0u),0,0,0};
        run_case(kind,width,repeat,input,&initial,1);
        /* Zero count with invalid pointers must not read, write or change flags. */
        input.source = 0xfd000000; input.destination = 0xff000000; input.count = 0;
        if (repeat) run_case(kind,width,repeat,input,&initial,1);
        /* Register-address wrap after a successful iteration, then a fault. */
        input.source = 0; input.destination = 0; input.count = 2; input.flags |= 0x400u;
        run_case(kind,width,repeat,input,&initial,0);
        /* Cross a physical RAM-view edge in either direction. */
        input.source = direction ? 0x04000002 : 0x03fffffe;
        input.destination = direction ? 0x04000004 : 0x03fffffd;
        input.count = 4; input.flags = direction ? 0xc93 : 0x893;
        run_case(kind,width,repeat,input,&initial,1);
        /* Virtual ranges differ but physical mirror bytes overlap. */
        input.source = 0x04040040;
        input.destination = input.source + (direction ? 0u-width : width);
        input.count = 5;
        run_case(kind,width,repeat,input,&initial,1);
        /* Forward alias overlap by one byte is not a bulk memmove. */
        input.source = 0x04040040; input.destination = direction ? 0x4003f : 0x40041;
        input.count = 5; run_case(kind,width,repeat,input,&initial,1);
        input.destination = input.source; run_case(kind,width,repeat,input,&initial,1);
        /* Contiguous and tiled aliases, still separate from ordinary RAM. */
        input.source = 0x80040040; input.destination = 0xf0040042; input.count = 4;
        run_case(kind,width,repeat,input,&initial,1);
        /* A valid first element followed by an invalid destination. */
        input.source = 0x40040; input.destination = 0x74000000-width; input.count = 3; input.flags &= ~0x400u;
        run_case(kind,width,repeat,input,&initial,1);
        input.count = UINT32_MAX;
        if (kind == TSFP_LODS) input.source = 0x74000000-width;
        if (repeat) run_case(kind,width,repeat,input,&initial,1);
        /* A completed source read followed by the first unbacked source. */
        input.source = 0x74000000-width; input.destination = 0x40040; input.count = 3;
        run_case(kind,width,repeat,input,&initial,1);
        /* Backward progress faults inside the reserved window, not below it. */
        input.source = kind == TSFP_LODS ? 0x80000000 : 0x40040;
        input.destination = 0x80000000; input.count = 3; input.flags |= 0x400u;
        Image backward = initial;
        uint32_t first = kind == TSFP_SCAS ? input.accumulator : model_load(&backward,input.source,width);
        if (kind == TSFP_CMPS || kind == TSFP_SCAS)
            model_store(&backward,input.destination,width,repeat == 2 ? ~first : first);
        run_case(kind,width,repeat,input,&backward,1);
        /* Source fault has priority over an invalid destination. */
        input.source = 0xfd000000; input.destination = 0xff000000; input.count = 2;
        run_case(kind,width,repeat,input,&initial,1);
    }
    /* Every low RAM view transition, both directions, all element widths. */
    for (unsigned view = 1; view < TSFP_XBOX_RAM_VIEWS; ++view)
    for (unsigned width = 1; width <= 4; width *= 2) for (unsigned direction = 0; direction < 2; ++direction) {
        uint32_t edge = view * TSFP_XBOX_RAM_SIZE;
        StringState input = {direction ? edge+1 : edge-1,0x40040,5,0x12345678,
                             0x893u | (direction ? 0x400u : 0),0,0,0};
        run_case(TSFP_MOVS,width,1,input,&initial,1);
        input.destination = input.source; input.source = 0x40040;
        run_case(TSFP_MOVS,width,1,input,&initial,1);
    }
    /* Force equality, mismatch, overflow and borrow at each width and mode. */
    for (unsigned kind = 3; kind <= 4; ++kind) for (unsigned width = 1; width <= 4; width *= 2)
    for (unsigned mode = 0; mode <= 2; ++mode) for (unsigned variant = 0; variant < 4; ++variant) {
        Image image = initial;
        uint32_t sign = 1u << (width*8u-1u);
        uint32_t left = variant == 0 ? 0 : variant == 1 ? sign : variant == 2 ? 0 : 0x7f;
        uint32_t right = variant == 0 ? 0 : variant == 1 ? 1 : variant == 2 ? 1 : 0x80;
        for (unsigned i = 0; i < 5; ++i) {
            model_store(&image,0x40040+i*width,width,left);
            model_store(&image,0x40140+i*width,width,i == 2 ? right : left);
        }
        StringState input = {0x40040,0x40140,5,left,0x893,0,0,0};
        run_case(kind,width,mode,input,&image,1);
    }
    /* Seed from a real prior CMP, then execute a zero-count REP comparison. */
    g_eax = 0x80000000; g_ebx = 1; g_ecx = 0; g_esi = 0xfd000000; g_edi = 0xff000000;
    g_edx = 0; g_df = 1; g_tsfp_string_flags = 2;
    g_esp = 0x1f000; PUSH32(g_esp,0x10ffe); fixture_string_seed();
    assert((g_tsfp_string_flags & FLAGS_MASK) == 0xc14 && (g_eax & 0xff00) == 0x1600 && (g_edx & 255) == 1);
    assert(g_ecx == 0 && g_esi == 0xfd000000 && g_edi == 0xff000000);
    for (unsigned zero=0; zero<2; ++zero) {
        g_esi=0x40040; g_edi=0x40040; g_ecx=zero ? 0 : 3;
        g_eax=0; g_ebx=0x100; g_tsfp_string_flags=3; g_df=0;
        g_esp=0x1f000; PUSH32(g_esp,0x10ffe); fixture_string_branch();
        assert(g_ebx == (zero ? 1u : 2u));
        g_esi=0x40040; g_edi=0x40040; g_ecx=zero ? 0 : 3;
        g_ebx=0x100; g_tsfp_string_flags=3;
        g_esp=0x1f000; PUSH32(g_esp,0x10ffe); fixture_string_carry();
        assert(g_ebx == (zero ? 0x101u : 0x100u));
    }
    image_transfer(&initial,1);
    g_esi=0x40040; g_edi=0x40140; g_ecx=3; g_df=1; g_tsfp_string_flags=0xc93;
    g_esp=0x1f000; PUSH32(g_esp,0x10ffe); fixture_string_cld();
    assert(g_df==0 && g_esi==0x40043 && g_edi==0x40143 && g_ecx==0 && g_tsfp_string_flags==0x893);
    assert(tsfp_guest_load_u8(0x40142)==tsfp_guest_load_u8(0x40042));
    image_transfer(&initial,1);
    g_esi=0x40042; g_edi=0x40142; g_ecx=3; g_df=0; g_tsfp_string_flags=0x893;
    g_esp=0x1f000; PUSH32(g_esp,0x10ffe); fixture_string_std();
    assert(g_df==0 && g_esi==0x4003f && g_edi==0x4013f && g_ecx==0 && g_tsfp_string_flags==0x893);
    assert(tsfp_guest_load_u8(0x40140)==tsfp_guest_load_u8(0x40040));
    printf("string-fixtures-v1: cases=%u bytes=pass flags=pass progress=pass faults=pass\n",cases);
}
