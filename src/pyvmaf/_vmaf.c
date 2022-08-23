#define PY_SSIZE_T_CLEAN

#include <Python.h>
#include <libvmaf/libvmaf.h>

typedef struct {
    PyObject_HEAD
    VmafContext *context;
    VmafModel *models[255];
    uint8_t model_cnt;
    VmafConfiguration config;
} VmafObject;

typedef struct VmafModelInternal {
    char *path;
    char *name;
} VmafModelInternal;

static PyTypeObject Vmaf_Type;

static int
do_pyvmaf_model_load(
    VmafObject *self, const char *name, const char *version, int flags) {
    VmafModelInternal *m;
    int i = 0;
    if (self->model_cnt == 255) {
        PyErr_SetString(PyExc_RuntimeError, "Cannot load more than 255 models");
        return -1;
    }
    for (i = 0; i < self->model_cnt; i++) {
        m = (VmafModelInternal *)(self->models[i]);
        if (!strcmp(m->name, name)) {
            PyErr_Format(PyExc_ValueError, "duplicate model name \"%s\"", name);
            return -1;
        }
    }
    VmafModel *model = NULL;
    VmafModelConfig cfg = {.name = name, .flags = flags};
    int err = vmaf_model_load(&model, &cfg, version);
    if (err) {
        PyErr_Format(PyExc_RuntimeError, "could not load model %s", version);
        model = NULL;
        return err;
    }
    err = vmaf_use_features_from_model(self->context, model);
    if (err) {
        PyErr_Format(
            PyExc_RuntimeError,
            "problem loading feature extractors from model %s",
            version);
    }
    self->models[self->model_cnt] = model;
    self->model_cnt++;
    return err;
}

PyObject *
VmafNew(PyObject *self_, PyObject *args) {
    VmafObject *self = NULL;
    const char *model_version = NULL;
    int py_log_level = 0;
    int vmaf_log_level = 0;
    int err = 0;
    int flags = 0;

    if (!PyArg_ParseTuple(args, "|zII", &model_version, &flags, &py_log_level)) {
        return NULL;
    }

    switch (py_log_level) {
        case 10:
            vmaf_log_level = VMAF_LOG_LEVEL_DEBUG;
            break;
        case 20:
            vmaf_log_level = VMAF_LOG_LEVEL_INFO;
            break;
        case 30:
            vmaf_log_level = VMAF_LOG_LEVEL_WARNING;
            break;
        case 40:
        case 50:
            vmaf_log_level = VMAF_LOG_LEVEL_ERROR;
            break;
        default:
            vmaf_log_level = VMAF_LOG_LEVEL_NONE;
    }

    self = PyObject_New(VmafObject, &Vmaf_Type);

    if (self) {
        self->context = NULL;
        self->model_cnt = 0;
        memset(self->models, 0, sizeof(self->models));
        self->config.log_level = vmaf_log_level;
        self->config.n_threads = 0;
        self->config.n_subsample = 0;
        self->config.cpumask = 0;

        Py_BEGIN_ALLOW_THREADS
        err = vmaf_init(&self->context, self->config);
        Py_END_ALLOW_THREADS

        if (err) {
            self->context = NULL;
            PyErr_SetString(PyExc_RuntimeError, "could not create context");
            return NULL;
        }

        if (model_version == NULL) {
            model_version = "vmaf_v0.6.1";
        }

        err = do_pyvmaf_model_load(self, "vmaf", model_version, flags);

        if (err)
            return NULL;

        return (PyObject *)self;
    }

    PyErr_SetString(PyExc_RuntimeError, "could not create vmaf object");
    return NULL;
}

PyObject *
_pyvmaf_model_load(VmafObject *self, PyObject *args) {
    const char *model_name = NULL;
    const char *model_version = NULL;
    int err = 0;
    int flags = 0;

    if (!PyArg_ParseTuple(args, "ss|I", &model_name, &model_version, &flags)) {
        return NULL;
    }
    err = do_pyvmaf_model_load(self, model_name, model_version, flags);
    if (err)
        return NULL;

    Py_RETURN_NONE;
}

PyObject *
_pyvmaf_dealloc(VmafObject *self) {
    for (int i = 0; i < 2; i++) {
        if (self->models[i]) {
            vmaf_model_destroy(self->models[i]);
            self->models[i] = NULL;
        }
    }
    if (self->context) {
        vmaf_close(self->context);
        self->context = NULL;
    }
    Py_RETURN_NONE;
}

static PyObject *
py_mkstemp() {
    PyObject *tempfile = NULL;
    tempfile = PyImport_ImportModule("tempfile");
    PyObject *ret = PyObject_CallMethod(tempfile, "mkstemp", "()");
    PyGILState_STATE gil_state = PyGILState_Ensure();
    PyGILState_Release(gil_state);
    Py_XDECREF(tempfile);
    Py_INCREF(ret);
    return ret;
}

PyObject *
fread_to_bytes(const char *filename) {
    struct stat info;
    FILE *fp;
    PyObject *py_bytes;
    if (stat(filename, &info) == -1) {
        PyErr_Format(PyExc_RuntimeError, "could not open file", filename);
        return NULL;
    }

    fp = fopen(filename, "rb");
    if (fp == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "Can't open file");
        return NULL;
    }
    py_bytes = PyBytes_FromStringAndSize(NULL, info.st_size);
    if (py_bytes == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to allocate array");
        fclose(fp);
        return NULL;
    }
    if (fread(PyBytes_AsString(py_bytes), info.st_size, 1, fp) <= 0) {
        PyErr_SetString(PyExc_RuntimeError, "Something went wrong during fread");
        fclose(fp);
        Py_DECREF(py_bytes);
        return NULL;
    }
    fclose(fp);
    Py_INCREF(py_bytes);
    return py_bytes;
}

static int
load_pic(
    VmafPicture *pic,
    uint8_t *bytes,
    Py_ssize_t size,
    unsigned int width,
    unsigned int height) {
    int err;
    Py_ssize_t pixels = width * height;
    Py_ssize_t offset = 0;

    err = vmaf_picture_alloc(pic, VMAF_PIX_FMT_YUV444P, 8, width, height);
    if (err) {
        PyErr_SetString(PyExc_RuntimeError, "could not allocate picture");
        return err;
    }
    for (Py_ssize_t i = 0; i < pixels; i++) {
        offset = i * 3;
        ((uint8_t *)pic->data[0])[i] = bytes[offset];
        ((uint8_t *)pic->data[1])[i] = bytes[offset + 1];
        ((uint8_t *)pic->data[2])[i] = bytes[offset + 2];
    }
    return 0;
}

PyObject *
_pyvmaf_add_feature(VmafObject *self, PyObject *args) {
    const char *name = NULL;
    PyObject *odict;
    PyObject *ret = NULL;

    if (!PyArg_ParseTuple(
            args, "et|O!", "utf-8", (char **)&name, &PyDict_Type, (PyObject *)&odict)) {
        return NULL;
    }

    VmafFeatureDictionary *opts_dict = NULL;
    PyObject *key, *val;
    Py_ssize_t pos = 0;
    int err = 0;

    while (odict != NULL && PyDict_Next(odict, &pos, &key, &val)) {
        if (key == NULL || val == NULL) {
            PyErr_SetString(PyExc_ValueError, "Could not read options dict");
            goto end;
        }
        if (key->ob_type != &PyUnicode_Type || val->ob_type != &PyUnicode_Type) {
            PyErr_SetString(PyExc_ValueError, "options dict key-values must be str");
            goto end;
        }

        err = vmaf_feature_dictionary_set(
            &opts_dict, PyUnicode_AsUTF8(key), PyUnicode_AsUTF8(val));
        if (err) {
            PyErr_Format(
                PyExc_RuntimeError,
                "Problem parsing feature %s=%s",
                PyUnicode_AsUTF8(key),
                PyUnicode_AsUTF8(val));
            goto end;
        }
    }
    err = vmaf_use_feature(self->context, name, opts_dict);

    ret = Py_None;

end:
    Py_BEGIN_ALLOW_THREADS
    Py_BLOCK_THREADS
    PyMem_Free((void *)name);
    Py_UNBLOCK_THREADS
    Py_END_ALLOW_THREADS
    return ret;
}

PyObject *
_pyvmaf_calculate(VmafObject *self, PyObject *args) {
    uint8_t *ref_yuv_bytes;
    uint8_t *dist_yuv_bytes;
    Py_ssize_t ref_yuv_size;
    Py_ssize_t dist_yuv_size;
    unsigned int width;
    unsigned int height;
    int err;
    VmafPicture pic_ref, pic_dist;
    PyObject *ret = NULL;
    PyObject *mkstemp_ret = NULL;

    if (!PyArg_ParseTuple(
            args,
            "z#z#II",
            (char **)&ref_yuv_bytes,
            &ref_yuv_size,
            (char **)&dist_yuv_bytes,
            &dist_yuv_size,
            &width,
            &height)) {
        return NULL;
    }

    Py_ssize_t expected_size = width * height * 3;
    if (ref_yuv_size != expected_size) {
        PyErr_SetString(PyExc_RuntimeError, "reference image unexpected size");
    }
    if (dist_yuv_size != expected_size) {
        PyErr_SetString(PyExc_RuntimeError, "distorted image unexpected size");
    }

    err = load_pic(&pic_ref, ref_yuv_bytes, ref_yuv_size, width, height);
    if (err)
        goto end;
    err = load_pic(&pic_dist, dist_yuv_bytes, dist_yuv_size, width, height);
    if (err)
        goto end;

    double vmaf_score;
    Py_BEGIN_ALLOW_THREADS
    Py_BLOCK_THREADS

    err = vmaf_read_pictures(self->context, &pic_ref, &pic_dist, 0);
    if (err) {
        PyErr_SetString(PyExc_RuntimeError, "Problem reading pictures");
        goto end;
    }
    err = vmaf_read_pictures(self->context, NULL, NULL, 0);
    if (err) {
        PyErr_SetString(PyExc_RuntimeError, "Problem flushing context");
        goto end;
    }

    for (int i = 0; i < self->model_cnt; i++) {
        err = vmaf_score_pooled(
            self->context, self->models[i], VMAF_POOL_METHOD_MEAN, &vmaf_score, 0, 0);
        if (err) {
            PyErr_SetString(PyExc_RuntimeError, "problem generating pooled VMAF score");
            goto end;
        }
    }

    mkstemp_ret = py_mkstemp();
    const char *filename = PyUnicode_AsUTF8(PyTuple_GET_ITEM(mkstemp_ret, 1));
    err = vmaf_write_output(self->context, filename, VMAF_OUTPUT_FORMAT_JSON);
    if (err) {
        PyErr_SetString(PyExc_RuntimeError, "Error writing output");
        unlink(filename);
        goto end;
    }
    ret = fread_to_bytes(filename);
    unlink(filename);
end:
    Py_UNBLOCK_THREADS;
    Py_END_ALLOW_THREADS
    Py_XDECREF(mkstemp_ret);

    return ret;
}

static struct PyMethodDef _pyvmaf_methods[] = {
    {"calculate", (PyCFunction)_pyvmaf_calculate, METH_VARARGS},
    {"add_feature", (PyCFunction)_pyvmaf_add_feature, METH_VARARGS},
    {"model_load", (PyCFunction)_pyvmaf_model_load, METH_VARARGS},
    {NULL, NULL}};

// Vmaf type definition
static PyTypeObject Vmaf_Type = {
    // clang-format off
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "Vmaf",
    // clang-format on
    .tp_basicsize = sizeof(VmafObject),
    .tp_dealloc = (destructor)_pyvmaf_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_methods = _pyvmaf_methods,
};

#if PY_VERSION_HEX >= 0x03000000
#define MOD_ERROR_VAL NULL
#define MOD_SUCCESS_VAL(val) val
#define MOD_INIT(name) PyMODINIT_FUNC PyInit_##name(void)
#define MOD_DEF(ob, name, methods)          \
    static struct PyModuleDef moduledef = { \
        PyModuleDef_HEAD_INIT,              \
        name,                               \
        NULL,                               \
        -1,                                 \
        methods,                            \
    };                                      \
    ob = PyModule_Create(&moduledef);
#else
#define MOD_ERROR_VAL
#define MOD_SUCCESS_VAL(val)
#define MOD_INIT(name) void init##name(void)
#define MOD_DEF(ob, name, methods) ob = Py_InitModule(name, methods);
#endif

static PyMethodDef vmafMethods[] = {{"Vmaf", VmafNew, METH_VARARGS}, {NULL, NULL}};

static int
setup_module(PyObject *m) {
    if (PyType_Ready(&Vmaf_Type) < 0) {
        return -1;
    }
    return 0;
}

MOD_INIT(_vmaf) {
    PyObject *m;

    MOD_DEF(m, "_vmaf", vmafMethods)

    if (m == NULL || setup_module(m) < 0) {
        return MOD_ERROR_VAL;
    }

    return MOD_SUCCESS_VAL(m);
}
