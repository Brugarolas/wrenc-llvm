//
// Created by znix on 10/07/2022.
//

#pragma once

#include <string>

#include "Obj.h"
#include "ObjClass.h"

class ObjString : public Obj {
  public:
	static ObjNativeClass *Class();

	ObjString();

	static ObjString *New(const std::string &value);
	static ObjString *New(std::string &&value);

	WREN_METHOD() Value ToString();
	WREN_METHOD(getter) int Count();
	WREN_METHOD() std::string OperatorPlus(Value other);

	std::string m_value;
};
