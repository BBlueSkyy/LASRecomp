// One identity-address layout shared by the runtime and its generated module.
// Desktop retains the original base. Android selects its paired build profile
// with -DLARECOMP_GUEST_BASE; no guest-to-host pointer conversion is required.
#pragma once
#ifndef LARECOMP_GUEST_BASE
#define LARECOMP_GUEST_BASE 0x7000000000ull
#endif
#define GUEST_LO      (LARECOMP_GUEST_BASE + 0ull)
#define GUEST_HI      (GUEST_LO + 0x0320000000ull)
#define MAIN_BASE     GUEST_LO
#define SDK_BASE      (GUEST_LO + 0x0004000000ull)
#define HLE_BASE      (GUEST_LO + 0x0080000000ull)
#define HLE_SIZE      0x0000100000ull
#define TLS_BASE      (GUEST_LO + 0x0090000000ull)
#define TLS_SIZE      0x0000100000ull
#define HLE_HEAP      (GUEST_LO + 0x0092000000ull)
#define HLE_HEAP_SIZE 0x000E000000ull
#define ALIAS_BASE    (GUEST_LO + 0x0100000000ull)
#define ALIAS_SIZE    0x0100000000ull
#define STACK_BASE    (GUEST_LO + 0x0200000000ull)
#define HEAP_BASE     (GUEST_LO + 0x0240000000ull)
#define HEAP_MAX      0x00D0000000ull
#if LARECOMP_GUEST_BASE <= 0 || (LARECOMP_GUEST_BASE & 0x3fff) || (LARECOMP_GUEST_BASE + 0x0320000000ull > (1ull << 39))
#error "guest layout must be 16 KiB aligned, positive and fit in the 39-bit guest VA"
#endif
