#ifndef LAS_PYTHON_GENERATOR_H
#define LAS_PYTHON_GENERATOR_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef void (*las_python_progress_fn)(void *opaque, const char *message);
/* Synchronous, serialized worker operation. Returns 0 on success, -1 on error.
 * python_root contains python3.14/ and tools/; native_lib_dir is the APK's
 * nativeLibraryDir. The interpreter persists until the preparation process exits.
 * Never call this from the UI thread or the game process. */
int las_python_generate(const char *python_root, const char *native_lib_dir,
                        const char *game_dir, const char *output_dir,
                        las_python_progress_fn progress, void *opaque,
                        char *error, size_t error_size);
typedef int (*las_python_compile_fn)(void *, const char *, const char *, char *, size_t);
typedef int (*las_python_link_fn)(void *, const char *const *, size_t, const char *, char *, size_t);
int las_python_build(const char *python_root, const char *native_lib_dir,
                     const char *game_dir, const char *compiler_assets, const char *identity,
                     las_python_compile_fn compile, las_python_link_fn link,
                     las_python_progress_fn progress, void *opaque, char *error, size_t error_size);
#ifdef __cplusplus
}
#endif
#endif
