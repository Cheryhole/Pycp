#ifndef PYCP_HPP
#define PYCP_HPP

#include "PycpObject.hpp"
#include "PycpNone.hpp"
#include "PycpInteger.hpp"
#include "PycpString.hpp"
#include "PycpException.hpp"
#include "PycpFunction.hpp"

#include "PycpManager.hpp"

namespace Pycp{

using ObjectPtr = Object*;
using IntegerPtr = Integer*;
using StringPtr = String*;
using NonePtr = None*;
using FunctionPtr = Function*;

} // namespace Pycp

#endif // PYCP_HPP