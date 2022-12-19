//
// Created by znix on 10/07/2022.
//

#include "ObjString.h"
#include "Errors.h"
#include "ObjBool.h"
#include "WrenRuntime.h"

class ObjStringClass : public ObjNativeClass {
  public:
	ObjStringClass() : ObjNativeClass("String", "ObjString") {}
};

ObjNativeClass *ObjString::Class() {
	static ObjStringClass type;
	return &type;
}

ObjString::ObjString() : Obj(Class()) {}

ObjString *ObjString::New(const std::string &value) {
	ObjString *obj = WrenRuntime::Instance().New<ObjString>();
	obj->m_value = value;
	return obj;
}

ObjString *ObjString::New(std::string &&value) {
	ObjString *obj = WrenRuntime::Instance().New<ObjString>();
	obj->m_value = std::move(value);
	return obj;
}

Value ObjString::ToString() { return encode_object(this); }

int ObjString::ByteCount_() { return m_value.size(); }

std::string ObjString::OperatorPlus(Value other) { return m_value + Obj::ToString(other); }

std::string ObjString::OperatorSubscript(int index) {
	// TODO return the unicode codepoint, not the single byte!
	ValidateIndex(index, "Subscript");
	return m_value.substr(index, 1);
}

int ObjString::ByteAt_(int index) {
	ValidateIndex(index, "Index");

	// Cast to unsigned so we don't get negatives
	return (uint8_t)m_value[index];
}

Value ObjString::IterateImpl(Value previous, bool unicode) const {
	// Empty strings are obviously empty
	if (m_value.empty())
		return encode_object(ObjBool::Get(false));

	// First iteration? Start at the start.
	if (previous == NULL_VAL)
		return encode_number(0);

	int position = errors::validateInt(previous, "Iterator");

	// TODO unicode handling
	position++;

	if (position >= m_value.size())
		return encode_object(ObjBool::Get(false));

	return encode_number(position);
}

Value ObjString::Iterate(Value previous) { return IterateImpl(previous, true); }

Value ObjString::IterateByte_(Value previous) { return IterateImpl(previous, false); }

std::string ObjString::IteratorValue(int iterator) {
	// Note the values from iterateByte_ are only used in wren_core by StringByteSequence, and they're
	// passed into byteAt_ - so IteratorValue doesn't have to care about them.
	return OperatorSubscript(iterator);
}

void ObjString::ValidateIndex(int index, const char *argName) const {
	if (index < 0 || index >= (int)m_value.size()) {
		// TODO throw Wren error with this exact message
		errors::wrenAbort("%s out of bounds.\n", argName);
	}
}
