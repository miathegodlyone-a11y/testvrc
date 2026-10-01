#pragma once
#include <sys/mman.h>
#include <cstring>
#include <cstdint>
#include <unistd.h>

// Minimal ARM64 inline hook — no external dependencies.
//
// Patch layout at target (16 bytes):
//   +0  LDR X16, #8    ; load hook addr from target+8
//   +4  BR  X16        ; jump to hook
//   +8  <hook addr>    ; 8-byte absolute address
//
// Trampoline (24 bytes, in executable mmap):
//   +0  <original instruction 0>   ; copied from target+0
//   +4  <original instruction 1>   ; copied from target+4
//   +8  LDR X16, #8               ; load return addr from tramp+16
//   +12 BR  X16                    ; jump back past patch
//   +16 <target+8>                 ; 8-byte return address
//
// Limitation: the first 2 original instructions must NOT be PC-relative
// (ADRP, ADR, B, BL, CBZ, CBNZ, TBZ, TBNZ). Function prologues almost
// never start with these, but verify with objdump if unsure.

static constexpr uint32_t LDR_X16_8  = 0x58000050; // LDR X16, #8
static constexpr uint32_t BR_X16     = 0xD61F0200; // BR  X16

static inline void flush_cache(void *p, size_t n) {
    __builtin___clear_cache((char *)p, (char *)p + n);
}

static inline bool unprotect(void *addr, size_t len) {
    uintptr_t page = (uintptr_t)addr & ~((uintptr_t)getpagesize() - 1);
    return mprotect((void *)page, len + getpagesize(), PROT_READ | PROT_WRITE | PROT_EXEC) == 0;
}

// hook_func — install inline hook.
//   target : address of function to hook
//   hook   : replacement function (same signature)
//   orig   : out — pointer to trampoline (call this to reach original)
// Returns true on success.
static bool hook_func(void *target, void *hook, void **orig) {
    if (!target || !hook || !orig) return false;

    // Allocate executable trampoline
    uint8_t *tramp = (uint8_t *)mmap(nullptr, 24,
        PROT_READ | PROT_WRITE | PROT_EXEC,
        MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (tramp == MAP_FAILED) return false;

    // Build trampoline
    memcpy(tramp, target, 8);                           // original instructions
    *(uint32_t *)(tramp + 8)  = LDR_X16_8;             // LDR X16, #8
    *(uint32_t *)(tramp + 12) = BR_X16;                 // BR  X16
    *(uintptr_t *)(tramp + 16) = (uintptr_t)target + 8; // return past patch
    flush_cache(tramp, 24);

    *orig = tramp;

    // Patch target
    if (!unprotect(target, 16)) { munmap(tramp, 24); return false; }
    *(uint32_t *)((uint8_t *)target + 0) = LDR_X16_8;
    *(uint32_t *)((uint8_t *)target + 4) = BR_X16;
    *(uintptr_t *)((uint8_t *)target + 8) = (uintptr_t)hook;
    flush_cache(target, 16);

    return true;
}
