#include <Python.h>
#include <iostream>
#include <windows.h>

int main() {
    SetConsoleOutputCP(CP_UTF8);

    Py_Initialize();

		HMODULE hDLL = LoadLibraryA("D:\\Desktop\\Pycp\\backend\\bin\\PycpRuntime.dll");

    PyObject* moduleName = PyUnicode_FromString("PycpRuntime4Python");
    PyObject* module = PyImport_Import(moduleName);
    Py_DECREF(moduleName);

    if (!module) {
        // 必须打印或清理异常状态
        PyErr_Print();   // 或 PyErr_Clear();
        Py_Finalize();
        return -1;
    }

    // module 非 NULL，安全使用
    PyObject* dir_list = PyObject_Dir(module);
    PyObject* repr_str = PyObject_Repr(dir_list);
    const char* c_s = PyUnicode_AsUTF8(repr_str);
    printf("dir(module) = %s\n", c_s);

    Py_DECREF(repr_str);
    Py_DECREF(dir_list);
    Py_DECREF(module);

    Py_Finalize();
    return 0;
}