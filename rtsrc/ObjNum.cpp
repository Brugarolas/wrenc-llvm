//
// Created by znix on 24/07/22.
//

#include "ObjNum.h"

#include "Errors.h"
#include "ObjRange.h"
#include "SlabObjectAllocator.h"

#include <math.h>

ObjNumClass::~ObjNumClass() {}

// Don't inherit methods from our parent, since we have the funny thing with the number receivers
ObjNumClass::ObjNumClass() : ObjNativeClass("Num", "ObjNumClass") {}

ObjNumClass *ObjNumClass::Instance() {
	static ObjNumClass cls;
	return &cls;
}

bool ObjNumClass::InheritsMethods() { return false; }

// Clang-tidy will think all these functions can be made static, which we don't want
// NOLINTBEGIN(readability-convert-member-functions-to-static)

bool ObjNumClass::Is(double receiver, ObjClass *cls) {
	// All numbers are pretending to be an instance of Num, and we're that instance.
	// Thus a simple pointer check will do.
	return cls == this;
}

std::string ObjNumClass::ToString(double receiver) {
	// Copied and modified from wren_value.c:wrenNumToString

	// Edge case: If the value is NaN or infinity, different versions of libc
	// produce different outputs (some will format it signed and some won't). To
	// get reliable output, handle it ourselves.
	if (isnan(receiver)) {
		return "nan";
	}
	if (isinf(receiver)) {
		return receiver > 0.0 ? "infinity" : "-infinity";
	}

	// This is large enough to hold any double converted to a string using
	// "%.14g". Example:
	//
	//     -1.12345678901234e-1022
	//
	// So we have:
	//
	// + 1 char for sign
	// + 1 char for digit
	// + 1 char for "."
	// + 14 chars for decimal digits
	// + 1 char for "e"
	// + 1 char for "-" or "+"
	// + 4 chars for exponent
	// + 1 char for "\0"
	// = 24
	char buffer[24];
	snprintf(buffer, sizeof(buffer), "%.14g", receiver);
	return buffer;
}

double ObjNumClass::OperatorMinus(double receiver) { return -receiver; }

bool ObjNumClass::OperatorBoolNegate(double receiver) {
	// Also return false, since numbers are 'truthy' values (same as everything except (IIRC) null and false).
	// Define here since Obj's methods aren't inherited.
	return false;
}

double ObjNumClass::OperatorBitwiseNegate(double receiver) {
	// https://wren.io/modules/core/num.html
	// Truncate to a u32, negate, and cast back
	uint32_t intValue = (uint32_t)receiver;
	uint32_t negated = ~intValue;
	double result = (double)negated;
	return result;
}

double ObjNumClass::OperatorPlus(double receiver, double other) { return receiver + other; }
double ObjNumClass::OperatorMinus(double receiver, double other) { return receiver - other; }
double ObjNumClass::OperatorMultiply(double receiver, double other) { return receiver * other; }
double ObjNumClass::OperatorDivide(double receiver, double other) { return receiver / other; }

// All of these cast without warning - see DEF_NUM_BITWISE in Wren
double ObjNumClass::OperatorAnd(double receiver, double other) {
	uint32_t left = (uint32_t)receiver;
	uint32_t right = (uint32_t)other;
	return left & right;
}
double ObjNumClass::OperatorOr(double receiver, double other) {
	uint32_t left = (uint32_t)receiver;
	uint32_t right = (uint32_t)other;
	return left | right;
}
double ObjNumClass::OperatorXOr(double receiver, double other) {
	uint32_t left = (uint32_t)receiver;
	uint32_t right = (uint32_t)other;
	return left ^ right;
}
double ObjNumClass::OperatorLeftShift(double receiver, double other) {
	uint32_t left = (uint32_t)receiver;
	uint32_t right = (uint32_t)other;
	return left << right;
}
double ObjNumClass::OperatorRightShift(double receiver, double other) {
	uint32_t left = (uint32_t)receiver;
	uint32_t right = (uint32_t)other;
	return left >> right;
}

// Equals and not-equals are a bit different to the others - it's not an error to pass an object to them
bool ObjNumClass::OperatorEqualTo(double receiver, Value other) {
	if (is_object(other))
		return false;
	return receiver == get_number_value(other);
}
bool ObjNumClass::OperatorNotEqual(double receiver, Value other) { return !OperatorEqualTo(receiver, other); }

bool ObjNumClass::OperatorLessThan(double receiver, double other) { return receiver < other; }
bool ObjNumClass::OperatorLessThanEq(double receiver, double other) { return receiver <= other; }
bool ObjNumClass::OperatorGreaterThan(double receiver, double other) { return receiver > other; }
bool ObjNumClass::OperatorGreaterThanEq(double receiver, double other) { return receiver >= other; }

ObjRange *ObjNumClass::OperatorDotDot(double receiver, double other) {
	return SlabObjectAllocator::GetInstance()->AllocateNative<ObjRange>(receiver, other, true);
}
ObjRange *ObjNumClass::OperatorDotDotDot(double receiver, double other) {
	return SlabObjectAllocator::GetInstance()->AllocateNative<ObjRange>(receiver, other, false);
}

// Trig stuff
double ObjNumClass::Pi() { return M_PI; }
double ObjNumClass::Sin(double receiver) { return sin(receiver); }
double ObjNumClass::Cos(double receiver) { return cos(receiver); }
double ObjNumClass::Tan(double receiver) { return tan(receiver); }
double ObjNumClass::Asin(double receiver) { return asin(receiver); }
double ObjNumClass::Acos(double receiver) { return acos(receiver); }
double ObjNumClass::Atan(double receiver) { return atan(receiver); }
double ObjNumClass::Atan(double receiver, double divisor) { return atan2(receiver, divisor); }

// Misc getter functions
double ObjNumClass::Abs(double receiver) { return abs(receiver); }
double ObjNumClass::Sqrt(double receiver) { return sqrt(receiver); }
double ObjNumClass::Cbrt(double receiver) { return cbrt(receiver); }
double ObjNumClass::Round(double receiver) { return round(receiver); }
double ObjNumClass::Floor(double receiver) { return floor(receiver); }
double ObjNumClass::Ceil(double receiver) { return ceil(receiver); }
double ObjNumClass::Log(double receiver) { return log(receiver); }
double ObjNumClass::Log2(double receiver) { return log2(receiver); }
double ObjNumClass::Sign(double receiver) {
	if (receiver > 0)
		return 1;
	if (receiver < 0)
		return -1;
	return 0;
}
double ObjNumClass::Fraction(double receiver) {
	double result = receiver - (int)receiver;
	if (result == 0 && receiver < 0)
		return -0.0; // We have to return the same sign
	return result;
}
double ObjNumClass::Exp(double receiver) { return exp(receiver); }

bool ObjNumClass::IsInteger(double receiver) { return (int)receiver == receiver; }
bool ObjNumClass::IsNan(double receiver) { return isnan(receiver); }
bool ObjNumClass::IsInfinity(double receiver) { return isinf(receiver); }

// Misc non-getter number functions
double ObjNumClass::Pow(double receiver, double power) { return pow(receiver, power); }
double ObjNumClass::Clamp(double receiver, double minValue, double maxValue) {
	return std::min(maxValue, std::max(receiver, minValue));
}
double ObjNumClass::Min(double receiver, double other) { return std::min(receiver, other); }
double ObjNumClass::Max(double receiver, double other) { return std::max(receiver, other); }

// NOLINTEND(readability-convert-member-functions-to-static)
