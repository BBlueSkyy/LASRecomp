#include "python_generator.h"
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t python_lock = PTHREAD_MUTEX_INITIALIZER;
static las_python_progress_fn current_progress;
static void *current_opaque;
static las_python_compile_fn current_compile;
static las_python_link_fn current_link;
static int initialized;

static PyObject *report_progress(PyObject *self, PyObject *args) {
    (void)self;
    const char *message;
    if (!PyArg_ParseTuple(args, "s", &message)) return NULL;
    if (current_progress) current_progress(current_opaque, message);
    Py_RETURN_NONE;
}

static PyMethodDef progress_method = {"report", report_progress, METH_VARARGS, NULL};

static PyObject *compile_object(PyObject *self, PyObject *args) {
    (void)self;
    const char *source, *object;
    if (!PyArg_ParseTuple(args, "ss", &source, &object)) return NULL;
    char error[8192] = {0};
    if (!current_compile || current_compile(current_opaque, source, object, error, sizeof error)) {
        PyErr_SetString(PyExc_RuntimeError, error[0] ? error : "Embedded Clang callback failed");
        return NULL;
    }
    Py_RETURN_NONE;
}
static PyObject *link_objects(PyObject *self, PyObject *args) {
    (void)self;
    PyObject *objects;
    const char *output;
    if (!PyArg_ParseTuple(args, "Os", &objects, &output)) return NULL;
    PyObject *sequence = PySequence_Fast(objects, "Expected generated objects");
    if (!sequence) return NULL;
    Py_ssize_t count = PySequence_Fast_GET_SIZE(sequence);
    const char **paths = calloc((size_t)count + 1, sizeof *paths);
    if (!paths) { Py_DECREF(sequence); return PyErr_NoMemory(); }
    for (Py_ssize_t i = 0; i < count; ++i) {
        paths[i] = PyUnicode_AsUTF8(PySequence_Fast_GET_ITEM(sequence, i));
        if (!paths[i]) { free(paths); Py_DECREF(sequence); return NULL; }
    }
    char error[8192] = {0};
    int failed = !current_link || current_link(current_opaque, paths, (size_t)count, output, error, sizeof error);
    free(paths);
    Py_DECREF(sequence);
    if (failed) { PyErr_SetString(PyExc_RuntimeError, error[0] ? error : "Embedded LLD callback failed"); return NULL; }
    Py_RETURN_NONE;
}
static PyMethodDef compile_method = {"compile", compile_object, METH_VARARGS, NULL};
static PyMethodDef link_method = {"link", link_objects, METH_VARARGS, NULL};

static void python_error(char *error, size_t size) {
    PyObject *type = NULL, *value = NULL, *trace = NULL;
    PyErr_Fetch(&type, &value, &trace);
    PyErr_NormalizeException(&type, &value, &trace);
    PyObject *text = value ? PyObject_Str(value) : NULL;
    if (error && size) snprintf(error, size, "Python generation failed: %s",
                               text ? PyUnicode_AsUTF8(text) : "unknown Python exception");
    Py_XDECREF(text); Py_XDECREF(type); Py_XDECREF(value); Py_XDECREF(trace);
    PyErr_Clear();
}

static int append_path(PyConfig *config, const char *root, const char *suffix) {
    size_t length = strlen(root) + strlen(suffix) + 2;
    char *path = malloc(length);
    if (!path) return -1;
    snprintf(path, length, "%s/%s", root, suffix);
    wchar_t *wide = Py_DecodeLocale(path, NULL);
    free(path);
    if (!wide) return -1;
    PyStatus status = PyWideStringList_Append(&config->module_search_paths, wide);
    PyMem_RawFree(wide);
    return PyStatus_Exception(status) ? -1 : 0;
}

static int invoke(const char *root, const char *native_dir, const char *game,
                  const char *output, const char *identity, las_python_compile_fn compile,
                  las_python_link_fn link, las_python_progress_fn progress, void *opaque,
                  char *error, size_t error_size) {
    if (error && error_size) *error = 0;
    if (!root || !native_dir || !game || !output) {
        if (error && error_size) snprintf(error, error_size, "Missing embedded generation path");
        return -1;
    }
    pthread_mutex_lock(&python_lock);
    int result = -1;
    if (!initialized) {
        PyConfig config;
        PyConfig_InitIsolatedConfig(&config);
        config.install_signal_handlers = 0;
        config.write_bytecode = 0;
        config.site_import = 0;
        config.parse_argv = 0;
        config.module_search_paths_set = 1;
        PyStatus status = PyConfig_SetBytesString(&config, &config.home, root);
        if (!PyStatus_Exception(status) &&
            (append_path(&config, root, "python3.14") || append_path(&config, root, "tools"))) {
            status = PyStatus_Error("Cannot configure embedded Python search paths");
        }
        if (!PyStatus_Exception(status)) status = Py_InitializeFromConfig(&config);
        if (PyStatus_Exception(status)) {
            if (error && error_size) snprintf(error, error_size, "Python initialization failed: %s", status.err_msg ? status.err_msg : "unknown error");
            PyConfig_Clear(&config);
            goto unlock;
        }
        PyConfig_Clear(&config);
        initialized = 1;
        PyEval_SaveThread();
    }
    PyGILState_STATE gil = PyGILState_Ensure();
    current_progress = progress;
    current_opaque = opaque;
    current_compile = compile; current_link = link;
    PyObject *module = PyImport_ImportModule("embedded_generate");
    PyObject *setup = module ? PyObject_GetAttrString(module, "install_native_loader") : NULL;
    PyObject *setup_result = setup ? PyObject_CallFunction(setup, "s", native_dir) : NULL;
    PyObject *cache = setup_result && identity ? PyImport_ImportModule("compilation_cache") : NULL;
    PyObject *generate = setup_result && (!identity || cache) ? PyObject_GetAttrString(identity ? cache : module,
                                                  identity ? "build_cached" : "generate") : NULL;
    PyObject *callback = generate ? PyCFunction_New(&progress_method, NULL) : NULL;
    PyObject *compile_callback = identity && callback ? PyCFunction_New(&compile_method, NULL) : NULL;
    PyObject *link_callback = compile_callback ? PyCFunction_New(&link_method, NULL) : NULL;
    PyObject *generated = NULL;
    if (identity && callback && compile_callback && link_callback)
        generated = PyObject_CallFunction(generate, "sssOOO", game, output, identity,
                                           compile_callback, link_callback, callback);
    else if (!identity && callback) generated = PyObject_CallFunction(generate, "ssO", game, output, callback);
    Py_XDECREF(compile_callback); Py_XDECREF(link_callback); Py_XDECREF(cache);
    if (generated) result = 0;
    else python_error(error, error_size);
    Py_XDECREF(generated); Py_XDECREF(callback); Py_XDECREF(generate);
    Py_XDECREF(setup_result); Py_XDECREF(setup); Py_XDECREF(module);
    current_progress = NULL; current_opaque = NULL; current_compile = NULL; current_link = NULL;
    PyGILState_Release(gil);
unlock:
    pthread_mutex_unlock(&python_lock);
    return result;
}

int las_python_generate(const char *root, const char *native_dir, const char *game,
                        const char *output, las_python_progress_fn progress, void *opaque,
                        char *error, size_t error_size) {
    return invoke(root, native_dir, game, output, NULL, NULL, NULL, progress, opaque, error, error_size);
}
int las_python_build(const char *root, const char *native_dir, const char *game,
                     const char *assets, const char *identity, las_python_compile_fn compile,
                     las_python_link_fn link, las_python_progress_fn progress, void *opaque,
                     char *error, size_t error_size) {
    if (!identity || !compile || !link) {
        if (error && error_size) snprintf(error, error_size, "Missing embedded compiler callbacks");
        return -1;
    }
    return invoke(root, native_dir, game, assets, identity, compile, link, progress, opaque, error, error_size);
}
