#ifndef KERNEL_MM_H
#define KERNEL_MM_H

#include <stdatomic.h>
#include <stdbool.h>
#include "kernel/memory.h"
#include "misc.h"

// Maximum anonymous mmap pages across ALL processes (host memory cap).
// Prevents iOS app from being killed by jetsam.
// 0 = no limit. Non-zero = hard limit in pages (4KB each).
// Go runtime alone needs ~1.1GB for page summary reservations (PROT_NONE),
// which do NOT count here — the mmap path exempts PROT_NONE explicitly.
// 524288 pages = 2GB.
//
// [T-ish-anon-cap-above-jetsam] Keep this BELOW the iOS jetsam threshold, or
// the whole mechanism is decorative. 5d8e1e1a raised it 2GB → 4GB for Node,
// which put it above the point where iOS kills the app: a runaway guest
// allocation now reached jetsam before ever reaching the cap, so the guest
// never got the ENOMEM that would have failed one command, and the user lost
// the entire app instead. Observed 2026-08-24: a guest compile grew the app
// footprint ~40MB/s from 135MB to 3.36GB over ~100s and was SIGKILLed, twice,
// with anon_page_count still far under 1048576.
//
// Node does not actually need 4GB: exec.c injects --max-old-space-size=512,
// so V8's heap is bounded at 512MB regardless of what this allows.
//
// 2GB is the pre-5d8e1e1a value and leaves headroom under jetsam on the
// smallest supported device while staying far above any legitimate workload.
//
// [T-ish-anon-cap-dynamic] This constant is now the CEILING, not the limit.
// A fixed number cannot sit on the right side of jetsam on every device: the
// threshold scales with RAM (iPhone 8 ≈ 1.4GB foreground vs iPhone 17 Pro
// 6GB+), and 2GB is decorative on the former while over-conservative on the
// latter. The effective limit lives in `anon_page_limit` below: the host
// derives it from os_proc_available_memory() at kernel boot and installs it
// via ish_set_anon_page_limit(), which clamps to at most this ceiling.
// Builds whose host never calls the setter (tests, Linux) keep the ceiling.
#define ANON_MMAP_LIMIT_PAGES 524288

#if ANON_MMAP_LIMIT_PAGES > 0
extern _Atomic long anon_page_count;
// Effective limit in guest pages. Always in (0, ANON_MMAP_LIMIT_PAGES].
extern _Atomic long anon_page_limit;
// Install a host-derived limit (guest 4KB pages). Values ≤ 0 are ignored;
// values above ANON_MMAP_LIMIT_PAGES are clamped to it.
void ish_set_anon_page_limit(long pages);
// Check-and-add `pages` against anon_page_limit. Returns false (and adds
// nothing) when the limit would be exceeded. All allocation sites that CAN
// fail gracefully must go through this instead of a bare fetch_add.
bool anon_pages_reserve(long pages);
void anon_pages_unreserve(long pages);
#endif

// uses mem.lock instead of having a lock of its own
struct mm {
    atomic_uint refcount;
    struct mem mem;

    addr_t vdso; // immutable
    addr_t start_brk; // immutable
    addr_t brk;

    // crap for procfs
    addr_t argv_start;
    addr_t argv_end;
    addr_t env_start;
    addr_t env_end;
    addr_t auxv_start;
    addr_t auxv_end;
    addr_t stack_start;
    struct fd *exefile;

    // Main executable load bias + entry point (ARM64 only — used to
    // precisely identify V8's self-abort BRK site in node at signal time).
    addr_t exe_bias;
    addr_t exe_entry;
};

// Create a new address space
struct mm *mm_new(void);
// Clone (COW) the address space
struct mm *mm_copy(struct mm *mm);
// Increment the refcount
void mm_retain(struct mm *mem);
// Decrement the refcount, destroy everything in the space if 0
void mm_release(struct mm *mem);

#endif
