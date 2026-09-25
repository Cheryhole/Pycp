#ifndef PYCP_HPP
#define PYCP_HPP

#include "object/PycpObject.hpp"
#include "object/PycpNone.hpp"
#include "object/PycpInteger.hpp"
#include "object/PycpFloat.hpp"
#include "object/PycpDecimal.hpp"
#include "object/PycpString.hpp"
#include "object/PycpException.hpp"
#include "object/PycpFunction.hpp"

#include "object/PycpConfig.hpp"
#include "object/PycpGC.hpp"
#include "abi/PycpABI.hpp"

#include "object/PycpManager.hpp"

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