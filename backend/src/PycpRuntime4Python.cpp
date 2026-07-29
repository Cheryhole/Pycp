#include "Pycp.hpp"	
#include "pybind11/pybind11.h"
#include "pybind11/stl.h"
#include "pybind11/functional.h"

namespace py = pybind11;
using namespace Pycp;

PYBIND11_MODULE(PycpRuntime4Python, m) {
    m.doc() = "Pycp Runtime Library - Python bindings (manual memory management)";
    
    // enum class Type（已修复）
    py::enum_<Type>(m, "Type")
        .value("OBJECT", Type::OBJECT)
        .value("NONE", Type::NONE)
        .value("INTEGER", Type::INTEGER)
        .value("STRING", Type::STRING)
        .value("FUNCTION", Type::FUNCTION);

    // Object 基类：完全不动
    py::class_<Object>(m, "Object")
        .def(py::init<Type>(), py::arg("type") = Type::OBJECT)
        .def_readonly("type", &Object::type)
        .def("__integer__", &Object::__integer__, py::return_value_policy::reference)
        .def("__string__", &Object::__string__, py::return_value_policy::reference)
        .def("__negation__", &Object::__negation__, py::return_value_policy::reference)
        .def("__call__", &Object::__call__, py::arg("args"), py::return_value_policy::reference)
        .def("__addition__", &Object::__addition__, py::arg("other"), py::return_value_policy::reference)
        .def("__subtraction__", &Object::__subtraction__, py::arg("other"), py::return_value_policy::reference)
        .def("__multiplication__", &Object::__multiplication__, py::arg("other"), py::return_value_policy::reference)
        .def("__division__", &Object::__division__, py::arg("other"), py::return_value_policy::reference);

    // Integer：完全不动
    py::class_<Integer, Object>(m, "Integer")
        .def(py::init<>())
        .def(py::init<int64_t>(), py::arg("value"))
        .def(py::init<const std::string&>(), py::arg("value"))
        .def(py::init<Integer*>(), py::arg("value"))
        .def(py::init<Object*>(), py::arg("obj"))
        .def("get_value", &Integer::get_value)
        .def("__integer__", &Integer::__integer__, py::return_value_policy::reference)
        .def("__string__", &Integer::__string__, py::return_value_policy::reference)
        .def("__negation__", &Integer::__negation__, py::return_value_policy::reference)
        .def("__addition__", &Integer::__addition__, py::arg("other"), py::return_value_policy::reference)
        .def("__subtraction__", &Integer::__subtraction__, py::arg("other"), py::return_value_policy::reference)
        .def("__multiplication__", &Integer::__multiplication__, py::arg("other"), py::return_value_policy::reference)
        .def("__division__", &Integer::__division__, py::arg("other"), py::return_value_policy::reference)
        .def_static("Initialize", &Integer::Initialize)
        .def_static("Finalize", &Integer::Finalize)
        .def_property_readonly_static("instances", [](py::object) {
            py::list result;
            for (int i = 0; i < PYCP_INTEGER_INSTANCES; i++) {
                if (Integer::instances[i]) {
                    // 全局整数实例：reference
                    result.append(py::cast(Integer::instances[i], py::return_value_policy::reference));
                }
            }
            return result;
        });

    // String：完全不动
    py::class_<String, Object>(m, "String")
        .def(py::init<>())
        .def(py::init<const std::string&>(), py::arg("value"))
        .def(py::init<String*>(), py::arg("value"))
        .def(py::init<Object*>(), py::arg("obj"))
        .def("get_value", &String::get_value)
        .def("__integer__", &String::__integer__, py::return_value_policy::reference)
        .def("__string__", &String::__string__, py::return_value_policy::reference)
        .def("__addition__", &String::__addition__, py::arg("other"), py::return_value_policy::reference)
        .def("__multiplication__", &String::__multiplication__, py::arg("other"), py::return_value_policy::reference);

    // ====================== 唯一修改：None 全局单例 → reference ======================
    py::class_<None, Object>(m, "_None")
        .def(py::init<>())
        .def_property_readonly_static("instance", [](py::object) {
            return py::cast(None::instance, py::return_value_policy::reference); // 全局指针
        })
        .def("__integer__", &None::__integer__, py::return_value_policy::reference)
        .def("__string__", &None::__string__, py::return_value_policy::reference)
        .def_static("Initialize", &None::Initialize)
        .def_static("Finalize", &None::Finalize);

    // ====================== 唯一修改：Function 全局资源 → reference ======================
    py::class_<Function, Object>(m, "Function")
        .def(py::init<>())
        .def(py::init<const char*>(), py::arg("name"))
        .def(py::init<const char*, CFunction_t>(), py::arg("name"), py::arg("func"))
        .def("__call__", &Function::__call__, py::arg("args"), py::return_value_policy::reference)
        .def("get_name", &Function::get_name)
        .def_static("Initialize", &Function::Initialize)
        .def_static("Finalize", &Function::Finalize);

    // ====================== 唯一修改：BuiltinFunction 全局指针 → reference ======================
    py::class_<BuiltinFunction>(m, "BuiltinFunction")
        .def_property_readonly_static("print", [](py::object) {
            return py::cast(BuiltinFunction::print, py::return_value_policy::reference); // 全局指针
        });

    m.def("AsString", &AsString);

    // 全局初始化/销毁
    m.def("Initialize", &Pycp::Initialize);
    m.def("Finalize", &Pycp::Finalize);

    m.attr("PYCP_INTEGER_INSTANCES") = py::int_(PYCP_INTEGER_INSTANCES);
    m.attr("__version__") = "1.0.0";
}