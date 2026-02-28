/*
使用PyBind11将类构造函数重载转换为C风格的函数签名，便于Python调用
*/

// PycpExporter.cpp
#include <memory>
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
		py::gil_scoped_acquire acquire;

		py::enum_<PycpType>(rt, "PycpType")
        .value("PYCP_OBJECT", PycpType::PYCP_OBJECT)
        .value("PYCP_NONE", PycpType::PYCP_NONE)
        .value("PYCP_INTEGER", PycpType::PYCP_INTEGER)
        .value("PYCP_STRING", PycpType::PYCP_STRING)
        .value("PYCP_FUNCTION", PycpType::PYCP_FUNCTION)
        .export_values();  // 可选：导出枚举值到模块作用域

    py::class_<PycpObject, std::shared_ptr<PycpObject>>(rt, "PycpObject")
        .def(py::init<>())
        .def("inc_ref_cnt", &PycpObject::inc_ref_cnt)
        .def("dec_ref_cnt", &PycpObject::dec_ref_cnt)
        .def("__integer__", [](std::shared_ptr<PycpObject> self) {
            return py::cast(to_shared(self->__integer__()));
        }, py::return_value_policy::take_ownership)
        .def("__string__", [](std::shared_ptr<PycpObject> self) {
            return py::cast(to_shared(self->__string__()));
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
            return self->type;  // 使用lambda返回枚举值
        });

    py::class_<PycpInteger, PycpObject, std::shared_ptr<PycpInteger>>(rt, "PycpInteger")
        // 构造函数重载 - 使用原始指针版本
        .def(py::init<>())
        .def(py::init<int>())
        .def(py::init<const std::string&>())
        .def(py::init([](std::shared_ptr<PycpInteger> ptr) { 
            return std::make_shared<PycpInteger>(ptr.get()); 
        }))
        .def(py::init([](std::shared_ptr<PycpString> ptr) { 
            return std::make_shared<PycpInteger>(ptr.get()); 
        }))
        // 成员方法
        .def("get_value", &PycpInteger::get_value)
        .def("__integer__", [](std::shared_ptr<PycpInteger> self) {
            return py::cast(to_shared(self->__integer__()));
        }, py::return_value_policy::take_ownership)
        .def("__string__", [](std::shared_ptr<PycpInteger> self) {
            return py::cast(to_shared(self->__string__()));
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
        // 构造函数重载 - 使用原始指针版本
        .def(py::init<>())
        .def(py::init<const std::string&>())
        .def(py::init([](std::shared_ptr<PycpString> ptr) { 
            return std::make_shared<PycpString>(ptr.get()); 
        }))
        .def(py::init([](std::shared_ptr<PycpInteger> ptr) { 
            return std::make_shared<PycpString>(ptr.get()); 
        }))
        // 成员方法
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
}