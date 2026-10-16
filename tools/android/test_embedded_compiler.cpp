// Opt-in fixture for an LLVM-enabled host/device build. Not run at recovery.
// Usage: fixture COMPILER_ASSET_ROOT NATIVE_LIBRARY_DIR EXISTING_WORK_DIRECTORY
#include "embedded_compiler.h"
#include <fstream>
#include <iostream>

int main(int argc, char **argv) {
    if (argc != 4) return 2;
    las::android::CompilerPaths paths{argv[1], argv[2]};
    const std::string work = argv[3];
    const std::string source = work + "/compiler-fixture.c";
    const std::string object = work + "/compiler-fixture.o";
    const std::string module = work + "/compiler-fixture.so";
    const std::string bad = work + "/compiler-invalid.c";
    std::string why;
    { std::ofstream file(bad); file << "this is not C;\n"; }
    if (las::android::compile_unit(bad, object, paths, "0123456789abcdef", why) || why.empty()) {
        std::cerr << "invalid C did not produce a diagnostic\n";
        return 1;
    }
    { std::ofstream file(source); file << R"(
#include "gamelib.h"
static void guest(Cpu *c) { c->x[0] = rt_ticks(); }
GAMELIB_EXPORT const GameLibTables *lasgame_tables(void) {
    static const uint64_t main[] = {0x7000000000ull}, sdk[] = {0x7004000000ull}, count = 1;
    static const GuestFn functions[] = {guest};
    static const GameLibTables table = {GAMELIB_ABI, GAMELIB_ID, main, sdk,
        functions, functions, &count, &count};
    return &table;
}
)"; }
    // Repeat after a syntax error and repeat linking to exercise reentry.
    if (!las::android::compile_unit(source, object, paths, "0123456789abcdef", why)) {
        std::cerr << why << '\n'; return 1;
    }
    for (int i = 0; i != 2; ++i)
        if (!las::android::link_module({object}, module, paths, why)) {
            std::cerr << why << '\n'; return 1;
        }
    std::cout << module << '\n';
    return 0;
}
