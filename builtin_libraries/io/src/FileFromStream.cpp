#include "PycpFile.hpp"
#include "PycpABI.hpp"
#include "PycpGC.hpp"

#include <istream>
#include <ostream>

namespace Pycp {

// 文件对象工厂：由已有流构造（不拥有流）。
FileObject* File_FromStream(const std::string& name, void* in, void* out) {
	return New<FileObject>(name, static_cast<std::istream*>(in),
	                       static_cast<std::ostream*>(out));
}

} // namespace Pycp
