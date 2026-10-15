#pragma once
#include <stdbool.h>
#include <stddef.h>
// Validate extracted files and create the original runtime's raw RomFS image.
// Uses no game code and never reports guest execution.
bool android_prepare_game(const char *root, char *why, size_t why_size);
