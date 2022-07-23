//
// Created by znix on 24/07/22.
//

#pragma once

#include "ObjClass.h"

/// The metaclass that we pretend numbers have. Since numbers aren't actual objects, this is all going to be
/// a bit weird and a bit magic with lots of help from the compiler to make this illusion work.
/// Note this class name is used in gen_bindings.py, be sure to change it if you rename this class.
class ObjNumClass : public ObjNativeClass {
  public:
	~ObjNumClass();
	ObjNumClass();

	static ObjNumClass *Instance();

	WREN_METHOD() double OperatorPlus(double receiver, double other);
	WREN_METHOD() double OperatorMinus(double receiver, double other);
	WREN_METHOD() double OperatorMultiply(double receiver, double other);
	WREN_METHOD() double OperatorDivide(double receiver, double other);
};
