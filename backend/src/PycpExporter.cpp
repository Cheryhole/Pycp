/*
使用PyBind11将Pycp运行时库导出为Python模块
*/

// PycpExporter.cpp
#include <memory>
#include <string>
#include "Pycp.hpp"	
#include "pybind11/pybind11.h"
#include "pybind11/embed.h"

namespace py = pybind11;

// 辅助函数：将原始指针转换为 shared_ptr
template<typename T>
std::shared_ptr<T> to_shared(T* ptr) {
    return std::shared_ptr<T>(ptr);
}

// 辅助函数：从shared_ptr获取原始指针用于构造函数
template<typename T>
T* from_shared(std::shared_ptr<T> ptr) {
    return ptr.get();
}

PYBIND11_MODULE(PycpRuntime4Python, rt) {
		// py::gil_scoped_acquire acquire;

    py::enum_<PycpType>(rt, "PycpType")
        .value("PYCP_TP_OBJECT", PycpType::PYCP_TP_OBJECT)
        .value("PYCP_TP_NONE", PycpType::PYCP_TP_NONE)
        .value("PYCP_TP_INTEGER", PycpType::PYCP_TP_INTEGER)
        .value("PYCP_TP_STRING", PycpType::PYCP_TP_STRING)
        .value("PYCP_TP_FUNCTION", PycpType::PYCP_TP_FUNCTION)
        .export_values();

    py::class_<PycpObject, std::shared_ptr<PycpObject>>(rt, "PycpObject")
        .def(py::init<>())
        .def("__integer__", [](std::shared_ptr<PycpObject> self) {
            return py::cast(to_shared(self->__integer__()));
        }, py::return_value_policy::take_ownership)
        .def("__string__", [](std::shared_ptr<PycpObject> self) {
            return py::cast(to_shared(self->__string__()));
        }, py::return_value_policy::take_ownership)
        .def("__negation__", [](std::shared_ptr<PycpObject> self) {
            return py::cast(to_shared(self->__negation__()));
        }, py::return_value_policy::take_ownership)
        .def("__call__", [](std::shared_ptr<PycpObject> self, std::shared_ptr<PycpObject> args) {
            return py::cast(to_shared(self->__call__(args.get())));
        }, py::return_value_policy::take_ownership)
        .def("__addition__", [](std::shared_ptr<PycpObject> self, std::shared_ptr<PycpObject> other) {
            return py::cast(to_shared(self->__addition__(other.get())));
        }, py::return_value_policy::take_ownership)
        .def("__subtraction__", [](std::shared_ptr<PycpObject> self, std::shared_ptr<PycpObject> other) {
            return py::cast(to_shared(self->__subtraction__(other.get())));
        }, py::return_value_policy::take_ownership)
        .def("__multiplication__", [](std::shared_ptr<PycpObject> self, std::shared_ptr<PycpObject> other) {
            return py::cast(to_shared(self->__multiplication__(other.get())));
        }, py::return_value_policy::take_ownership)
        .def("__division__", [](std::shared_ptr<PycpObject> self, std::shared_ptr<PycpObject> other) {
            return py::cast(to_shared(self->__division__(other.get())));
        }, py::return_value_policy::take_ownership)
        .def_property_readonly("type", [](std::shared_ptr<PycpObject> self) {
            return self->type;
        });

    py::class_<PycpInteger, PycpObject, std::shared_ptr<PycpInteger>>(rt, "PycpInteger")
        .def(py::init<>())
        .def(py::init<int64_t>())
        .def(py::init<const std::string&>())
        .def(py::init([](std::shared_ptr<PycpInteger> ptr) { 
            return std::make_shared<PycpInteger>(ptr.get()); 
        }))
        .def(py::init([](std::shared_ptr<PycpObject> ptr) { 
            return std::make_shared<PycpInteger>(ptr.get()); 
        }))
        .def("get_value", &PycpInteger::get_value)
        .def("__integer__", [](std::shared_ptr<PycpInteger> self) {
            return py::cast(to_shared(self->__integer__()));
        }, py::return_value_policy::take_ownership)
        .def("__string__", [](std::shared_ptr<PycpInteger> self) {
            return py::cast(to_shared(self->__string__()));
        }, py::return_value_policy::take_ownership)
        .def("__negation__", [](std::shared_ptr<PycpInteger> self) {
            return py::cast(to_shared(self->__negation__()));
        }, py::return_value_policy::take_ownership)
        .def("__addition__", [](std::shared_ptr<PycpInteger> self, std::shared_ptr<PycpObject> other) {
            return py::cast(to_shared(self->__addition__(other.get())));
        }, py::return_value_policy::take_ownership)
        .def("__subtraction__", [](std::shared_ptr<PycpInteger> self, std::shared_ptr<PycpObject> other) {
            return py::cast(to_shared(self->__subtraction__(other.get())));
        }, py::return_value_policy::take_ownership)
        .def("__multiplication__", [](std::shared_ptr<PycpInteger> self, std::shared_ptr<PycpObject> other) {
            return py::cast(to_shared(self->__multiplication__(other.get())));
        }, py::return_value_policy::take_ownership)
        .def("__division__", [](std::shared_ptr<PycpInteger> self, std::shared_ptr<PycpObject> other) {
            return py::cast(to_shared(self->__division__(other.get())));
        }, py::return_value_policy::take_ownership);

    py::class_<PycpString, PycpObject, std::shared_ptr<PycpString>>(rt, "PycpString")
        .def(py::init<>())
        .def(py::init<const std::string&>())
        .def(py::init([](std::shared_ptr<PycpString> ptr) { 
            return std::make_shared<PycpString>(ptr.get()); 
        }))
        .def(py::init([](std::shared_ptr<PycpObject> ptr) { 
            return std::make_shared<PycpString>(ptr.get()); 
        }))
        .def("get_value", &PycpString::get_value)
        .def("__integer__", [](std::shared_ptr<PycpString> self) {
            return py::cast(to_shared(self->__integer__()));
        }, py::return_value_policy::take_ownership)
        .def("__string__", [](std::shared_ptr<PycpString> self) {
            return py::cast(to_shared(self->__string__()));
        }, py::return_value_policy::take_ownership)
        .def("__addition__", [](std::shared_ptr<PycpString> self, std::shared_ptr<PycpObject> other) {
            return py::cast(to_shared(self->__addition__(other.get())));
        }, py::return_value_policy::take_ownership)
        .def("__multiplication__", [](std::shared_ptr<PycpString> self, std::shared_ptr<PycpObject> other) {
            return py::cast(to_shared(self->__multiplication__(other.get())));
        }, py::return_value_policy::take_ownership);

    py::class_<PycpNone, PycpObject, std::shared_ptr<PycpNone>>(rt, "PycpNone")
        .def(py::init<>())
        .def_property_readonly_static("instance", [](py::object) {
            return std::shared_ptr<PycpNone>(PycpNone::instance, [](PycpNone*){});
        })
        .def("__integer__", [](std::shared_ptr<PycpNone> self) {
            return py::cast(to_shared(self->__integer__()));
        }, py::return_value_policy::take_ownership)
        .def("__string__", [](std::shared_ptr<PycpNone> self) {
            return py::cast(to_shared(self->__string__()));
        }, py::return_value_policy::take_ownership);

    py::class_<PycpFunction, PycpObject, std::shared_ptr<PycpFunction>>(rt, "PycpFunction")
        .def(py::init<const char*>())
        .def(py::init<const char*, std::function<PycpObject*(PycpObject*)>>())
        .def("__call__", [](std::shared_ptr<PycpFunction> self, std::shared_ptr<PycpObject> args) {
            return py::cast(to_shared(self->__call__(args.get())));
        }, py::return_value_policy::take_ownership)
        .def("get_name", &PycpFunction::get_name);

    py::class_<PycpBuiltinFunction>(rt, "PycpBuiltinFunction")
        .def_property_readonly_static("print", [](py::object) {
            return std::shared_ptr<PycpFunction>(PycpBuiltinFunction::print, [](PycpFunction*){});
        });

		rt.def("PycpInitialize", &PycpInitialize);
    rt.def("PycpFinalize", &PycpFinalize);
}