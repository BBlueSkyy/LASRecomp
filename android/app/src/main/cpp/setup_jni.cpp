// Opt-in preparation process: all compilation stays in this process. No guest
// address reservation, SDL runtime, or game code executes until it exits.
#include <jni.h>
#include <cstdio>
#include <exception>
#include <string>
#include <vector>
#include "embedded_compiler.h"
#include "python_generator.h"
extern "C" {
#include "gamelib.h"
#include "platform/android_prepare.h"
}

namespace {
struct Text {
    JNIEnv *env;
    jstring value;
    const char *text;
    Text(JNIEnv *e, jstring v) : env(e), value(v), text(v ? e->GetStringUTFChars(v, nullptr) : nullptr) {}
    ~Text() { if (text) env->ReleaseStringUTFChars(value, text); }
};
struct Job {
    las::android::CompilerPaths paths;
    JNIEnv *env;
    jobject progress;
    jmethodID on_progress;
};
void report(void *opaque, const char *message) {
    auto &job = *static_cast<Job *>(opaque);
    std::fprintf(stderr, "[prepare] %s\n", message);
    if (!job.progress || !job.on_progress || job.env->ExceptionCheck()) return;
    jstring text = job.env->NewStringUTF(message);
    if (text) { job.env->CallVoidMethod(job.progress, job.on_progress, text); job.env->DeleteLocalRef(text); }
}
int compile(void *opaque, const char *source, const char *object, char *error, size_t size) {
    auto &job = *static_cast<Job *>(opaque);
    std::string why;
    if (las::android::compile_unit(source, object, job.paths, GAMELIB_ID, why)) return 0;
    std::snprintf(error, size, "%s", why.c_str());
    return -1;
}
int link(void *opaque, const char *const *objects, size_t count, const char *output, char *error, size_t size) {
    auto &job = *static_cast<Job *>(opaque);
    std::vector<std::string> paths(objects, objects + count);
    std::string why;
    if (las::android::link_module(paths, output, job.paths, why)) return 0;
    std::snprintf(error, size, "%s", why.c_str());
    return -1;
}
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_bblueskyy_lasrecomp_PreparationActivity_buildIdentity(JNIEnv *env, jclass) {
    return env->NewStringUTF(GAMELIB_ID);
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_bblueskyy_lasrecomp_PreparationActivity_prepare(JNIEnv *env, jclass, jstring game,
                                                      jstring assets, jstring native_dir, jobject progress) {
    Text root(env, game), tools(env, assets), libraries(env, native_dir);
    if (!root.text || !tools.text || !libraries.text) return env->NewStringUTF("Missing private preparation path");
    Job job{{std::string(tools.text) + "/compiler", libraries.text}, env, progress, nullptr};
    if (progress) {
        jclass type = env->GetObjectClass(progress);
        job.on_progress = env->GetMethodID(type, "onProgress", "(Ljava/lang/String;)V");
        env->DeleteLocalRef(type);
        if (env->ExceptionCheck()) return nullptr;
    }
    char error[8192] = {0};
    try {
        report(&job, "Preparing the imported RomFS image");
        if (!android_prepare_game(root.text, error, sizeof error)) return env->NewStringUTF(error);
        const std::string python = std::string(tools.text) + "/python";
        if (las_python_build(python.c_str(), libraries.text, root.text, job.paths.asset_root.c_str(),
                             GAMELIB_ID, compile, link, report, &job, error, sizeof error))
            return env->NewStringUTF(error);
        return nullptr;
    } catch (const std::exception &exception) {
        return env->NewStringUTF(exception.what());
    }
}
