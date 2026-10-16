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
static int initialized;

static PyObject *report_progress(PyObject *self, PyObject *args) {
    (void)self;
    const char *message;
    if (!PyArg_ParseTuple(args, "s", &message)) return NULL;
    if (current_progress) current_progress(current_opaque, message);
    Py_RETURN_NONE;
}

static PyMethodDef progress_method = {"report", report_progress, METH_VARARGS, NULL};

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

int las_python_generate(const char *root, const char *native_dir, const char *game,
                        const char *output, las_python_progress_fn progress, void *opaque,
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
    PyObject *module = PyImport_ImportModule("embedded_generate");
    PyObject *setup = module ? PyObject_GetAttrString(module, "install_native_loader") : NULL;
    PyObject *setup_result = setup ? PyObject_CallFunction(setup, "s", native_dir) : NULL;
    PyObject *generate = setup_result ? PyObject_GetAttrString(module, "generate") : NULL;
    PyObject *callback = generate ? PyCFunction_New(&progress_method, NULL) : NULL;
    PyObject *generated = callback ? PyObject_CallFunction(generate, "ssO", game, output, callback) : NULL;
    if (generated) result = 0;
    else python_error(error, error_size);
    Py_XDECREF(generated); Py_XDECREF(callback); Py_XDECREF(generate);
    Py_XDECREF(setup_result); Py_XDECREF(setup); Py_XDECREF(module);
    current_progress = NULL; current_opaque = NULL;
    PyGILState_Release(gil);
unlock:
    pthread_mutex_unlock(&python_lock);
    return result;
}
