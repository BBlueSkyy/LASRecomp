// Read-only build interface for the external Android static compiler. No game data.
#pragma once
#include <stdint.h>

#ifndef GAMELIB_ID
#define GAMELIB_ID "dev"
#endif
#define GAMELIB_ABI 1

// Fixed ELF data layout, readable on the build host without executing ARM64.
typedef struct {
    char magic[4];
    uint32_t format, abi, size;
    uint64_t guest_base;
    char id[17];
} GameLibBuildContract;

