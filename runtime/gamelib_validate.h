// Validation at the native game module boundary; no guest code is executed here.
#pragma once
#include "gamelib.h"

// The callback must confirm the entire range is mapped by this module, readable
// (executable when code is true). It is called before any pointer is dereferenced.
typedef bool (*GameLibRange)(const void *ptr, size_t size, bool code, void *context);
bool gamelib_validate_tables(const GameLibTables *tables, GameLibRange range, void *context,
                             char *why, size_t why_size);

// ELF64 little-endian ET_DYN, expected e_machine, valid load ranges and a defined
// lasgame_tables function. This is a format check, not a sandbox for native code.
bool gamelib_validate_elf(const char *path, unsigned machine, char *why, size_t why_size);
