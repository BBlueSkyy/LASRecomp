// CHECKPOINT: implementation prepared against LLVM 18.1.8 public interfaces.
// No completed LLVM library build or Android execution was available after the
// workspace reset. Keep this source behind the default-OFF experimental flag.
#include "embedded_compiler.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/Job.h"
#include "clang/Driver/Tool.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/FrontendTool/Utils.h"
#include "lld/Common/Driver.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <mutex>
#include <utility>

LLD_HAS_DRIVER(elf)
namespace las::android {
namespace {
std::mutex compiler_mutex;
std::once_flag initialization;
bool poisoned = false;
void initialize() {
    // Deliberately reference only the backend included in the APK.
    LLVMInitializeAArch64TargetInfo();
    LLVMInitializeAArch64Target();
    LLVMInitializeAArch64TargetMC();
    LLVMInitializeAArch64AsmPrinter();
    LLVMInitializeAArch64AsmParser();
    llvm::CrashRecoveryContext::Enable();
}
std::vector<const char *> pointers(const std::vector<std::string> &arguments) {
    std::vector<const char *> result;
    result.reserve(arguments.size());
    for (const auto &argument : arguments) result.push_back(argument.c_str());
    return result;
}
bool readable(const std::string &path, std::string &why) {
    if (std::ifstream(path, std::ios::binary).good()) return true;
    why = "Embedded compiler input is missing: " + path;
    return false;
}
bool elf(const std::string &path, bool shared, std::string &why) {
    unsigned char header[64] = {};
    std::ifstream stream(path, std::ios::binary);
    stream.read(reinterpret_cast<char *>(header), sizeof(header));
    if (stream.gcount() == sizeof(header) && header[0] == 0x7f && header[1] == 'E' &&
        header[2] == 'L' && header[3] == 'F' && header[4] == 2 && header[5] == 1 &&
        header[16] == (shared ? 3 : 1) && header[17] == 0 && header[18] == 183 && header[19] == 0)
        return true;
    why += "\nCompiler output is not an AArch64 ELF " + std::string(shared ? "shared library" : "object") + ": " + path;
    return false;
}
void fatal_error(void *context, const char *message, bool) {
    auto &why = *static_cast<std::string *>(context);
    why += "\nLLVM fatal error: ";
    why += message;
    if (auto *recovery = llvm::CrashRecoveryContext::GetCurrent()) recovery->HandleExit(70);
    // This branch is outside the supported compiler invocation boundary.
    std::abort();
}
bool usable(std::string &why) {
    if (!poisoned) return true;
    why = "Embedded compiler suffered a fatal error; restart the preparation process before retrying.";
    return false;
}
} // namespace

bool compile_unit(const std::string &source, const std::string &object,
                  const CompilerPaths &paths, const std::string &identity,
                  std::string &why) {
    std::lock_guard<std::mutex> lock(compiler_mutex);
    why.clear();
    if (!usable(why)) return false;
    if (identity.size() != 16) { why = "Invalid generated-module identity"; return false; }
    for (unsigned char c : identity)
        if (!std::isxdigit(c)) { why = "Invalid generated-module identity"; return false; }
    if (!readable(source, why) || !readable(paths.asset_root + "/runtime/cpu.h", why)) return false;
    std::call_once(initialization, initialize);
    std::remove(object.c_str());
    bool success = false;
    llvm::install_fatal_error_handler(fatal_error, &why);
    llvm::CrashRecoveryContext recovery;
    bool recovered = recovery.RunSafelyOnThread([&] {
        llvm::raw_string_ostream diagnostics(why);
        llvm::IntrusiveRefCntPtr<clang::DiagnosticOptions> options(new clang::DiagnosticOptions);
        options->ShowColors = false;
        llvm::IntrusiveRefCntPtr<clang::DiagnosticIDs> ids(new clang::DiagnosticIDs);
        clang::DiagnosticsEngine engine(ids, options,
            new clang::TextDiagnosticPrinter(diagnostics, options.get()), true);
        clang::driver::Driver driver("embedded-clang", "aarch64-linux-android28", engine);
        std::vector<std::string> arguments = {
            "embedded-clang", "--target=aarch64-linux-android28", "-x", "c", "-std=gnu11",
            "-O1", "-fPIC", "-fno-strict-aliasing", "-fvisibility=hidden",
            "-Wno-unused-label", "-Wno-return-type", "-mno-outline-atomics",
            "-DGAMELIB_ID=\"" + identity + "\"", "-nostdinc", "-I", paths.asset_root + "/runtime",
            "-isystem", paths.asset_root + "/resource/include",
            "-isystem", paths.asset_root + "/sysroot/usr/include/aarch64-linux-android",
            "-isystem", paths.asset_root + "/sysroot/usr/include",
            "-resource-dir", paths.asset_root + "/resource", "-c", source, "-o", object
        };
        auto argv = pointers(arguments);
        std::unique_ptr<clang::driver::Compilation> compilation(driver.BuildCompilation(argv));
        if (!compilation || engine.hasErrorOccurred() || compilation->getJobs().size() != 1) return;
        const auto &job = *compilation->getJobs().begin();
        if (std::string(job.getCreator().getName()) != "clang") {
            why += "\nUnexpected external tool requested by compiler driver";
            return;
        }
        auto command = llvm::ArrayRef<const char *>(job.getArguments());
        if (command.empty() || std::string(command.front()) != "-cc1") {
            why += "\nExpected an in-process Clang cc1 invocation";
            return;
        }
        auto invocation = std::make_shared<clang::CompilerInvocation>();
        if (!clang::CompilerInvocation::CreateFromArgs(*invocation, command.drop_front(), engine)) return;
        clang::CompilerInstance compiler;
        compiler.setInvocation(std::move(invocation));
        compiler.createDiagnostics(new clang::TextDiagnosticPrinter(diagnostics, &compiler.getDiagnosticOpts()), true);
        if (!compiler.hasDiagnostics()) return;
        success = clang::ExecuteCompilerInvocation(&compiler);
    }, 16 * 1024 * 1024);
    llvm::remove_fatal_error_handler();
    if (!recovered) {
        poisoned = true;
        why += "\nCompiler failure; preparation process must restart before retrying.";
    }
    if (recovered && success && elf(object, false, why)) return true;
    std::remove(object.c_str());
    if (why.empty()) why = "Embedded Clang could not compile " + source;
    return false;
}

bool link_module(const std::vector<std::string> &objects, const std::string &output,
                 const CompilerPaths &paths, std::string &why) {
    std::lock_guard<std::mutex> lock(compiler_mutex);
    why.clear();
    if (!usable(why)) return false;
    if (objects.empty()) { why = "No generated objects to link"; return false; }
    const std::string library = paths.asset_root + "/sysroot/usr/lib/aarch64-linux-android/28/";
    const std::string builtins = paths.asset_root + "/resource/lib/linux/libclang_rt.builtins-aarch64-android.a";
    const std::string runtime = paths.native_library_dir + "/liblasrecomp.so";
    if (!readable(runtime, why) || !readable(builtins, why)) return false;
    for (const auto &object : objects) if (!readable(object, why)) return false;
    std::call_once(initialization, initialize);
    std::remove(output.c_str());
    std::vector<std::string> arguments = {"ld.lld", "-m", "aarch64linux", "-shared", "--no-undefined",
        "-z", "relro", "-z", "now", "-z", "noexecstack", "-z", "max-page-size=16384",
        "-z", "common-page-size=16384", "--soname", "lasgame.so", "--build-id=sha1", "-o", output,
        library + "crtbegin_so.o"};
    arguments.insert(arguments.end(), objects.begin(), objects.end());
    arguments.insert(arguments.end(), {runtime, "--start-group", builtins,
        library + "libm.so", library + "libc.so", library + "libdl.so", "--end-group",
        library + "crtend_so.o"});
    auto argv = pointers(arguments);
    llvm::raw_string_ostream diagnostics(why);
    const lld::DriverDef drivers[] = {{lld::Gnu, &lld::elf::link}};
    const auto result = lld::lldMain(argv, diagnostics, diagnostics, drivers);
    if (!result.canRunAgain) {
        poisoned = true;
        why += "\nLLD failure; preparation process must restart before retrying.";
    }
    if (result.retCode == 0 && result.canRunAgain && elf(output, true, why)) return true;
    std::remove(output.c_str());
    if (why.empty()) why = "Embedded LLD could not link the generated module";
    return false;
}
} // namespace las::android
