//
// Created by znix on 22/07/22.
//

#pragma once

#include "ObjClass.h"

#include <vector>

class ObjListIterator;

class ObjList : public Obj {
  public:
	ObjList();
	virtual ~ObjList();

	static ObjClass *Class();

	void MarkGCValues(GCMarkOps *ops) override;

	void ValidateIndex(int index, const char *argName) const;

	WREN_METHOD() static ObjList *New();

	WREN_METHOD() Value Add(Value toAdd);
	WREN_METHOD() Value Insert(int index, Value toAdd);

	WREN_METHOD() std::string Join();
	WREN_METHOD() std::string Join(std::string joiner);

	WREN_METHOD() Value Iterate(Value current);
	WREN_METHOD() Value IteratorValue(int current);

	WREN_METHOD() Value OperatorSubscript(int index);

	std::vector<Value> items;
};
