#pragma once
#include <string>
#include <vector>

// Optional LLVM 18.1.8 implementation. This checkpoint must remain disabled
// until the Android LLVM libraries and end-to-end device fixtures are verified.
namespace las::android {
struct CompilerPaths {
    // Extracted compiler-assets directory, containing runtime/, resource/ and sysroot/.
    std::string asset_root;
    // ApplicationInfo.nativeLibraryDir; liblasrecomp.so must already be installed here.
    std::string native_library_dir;
};
bool compile_unit(const std::string &source, const std::string &object,
                  const CompilerPaths &paths, const std::string &identity,
                  std::string &why);
bool link_module(const std::vector<std::string> &objects, const std::string &output,
                 const CompilerPaths &paths, std::string &why);
} // namespace las::android
