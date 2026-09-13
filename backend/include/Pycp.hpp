#ifndef PYCP_HPP
#define PYCP_HPP

#include "PycpObject.hpp"
#include "PycpNone.hpp"
#include "PycpInteger.hpp"
#include "PycpFloat.hpp"
#include "PycpDecimal.hpp"
#include "PycpString.hpp"
#include "PycpException.hpp"
#include "PycpFunction.hpp"

#include "PycpConfig.hpp"
#include "PycpGC.hpp"
#include "PycpABI.hpp"

#include "PycpManager.hpp"

namespace Pycp{

using ObjectPtr = Object*;
using IntegerPtr = Integer*;
using FloatPtr = Float*;
using DecimalPtr = Decimal*;
using StringPtr = String*;
using NonePtr = None*;
using FunctionPtr = Function*;

} // namespace Pycp

#endif // PYCP_HPP