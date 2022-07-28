//
// Generated entrypoint functions
// These are functions that the generated code calls, with the exception of setupGenEntry().
//
// Created by znix on 21/07/22.
//

#include "GenEntry.h"
#include "ClassDescription.h"
#include "CoreClasses.h"
#include "ObjBool.h"
#include "ObjClass.h"
#include "ObjFn.h"
#include "ObjList.h"
#include "ObjManaged.h"
#include "ObjNum.h"
#include "ObjString.h"
#include "ObjSystem.h"
#include "WrenRuntime.h"
#include "common.h"

#include <string.h>
#include <vector>

// These are the functions in question
extern "C" {
Value wren_sys_var_Bool = NULL_VAL;   // NOLINT(readability-identifier-naming)
Value wren_sys_var_Object = NULL_VAL; // NOLINT(readability-identifier-naming)
Value wren_sys_var_Class = NULL_VAL;  // NOLINT(readability-identifier-naming)
Value wren_sys_var_Fn = NULL_VAL;     // NOLINT(readability-identifier-naming)
Value wren_sys_var_List = NULL_VAL;   // NOLINT(readability-identifier-naming)
Value wren_sys_var_Num = NULL_VAL;    // NOLINT(readability-identifier-naming)
Value wren_sys_var_String = NULL_VAL; // NOLINT(readability-identifier-naming)
Value wren_sys_var_System = NULL_VAL; // NOLINT(readability-identifier-naming)

Value wren_sys_bool_false = NULL_VAL; // NOLINT(readability-identifier-naming)
Value wren_sys_bool_true = NULL_VAL;  // NOLINT(readability-identifier-naming)

void *wren_virtual_method_lookup(Value receiver, uint64_t signature);        // NOLINT(readability-identifier-naming)
Value wren_init_string_literal(const char *literal, int length);             // NOLINT(readability-identifier-naming)
void wren_register_signatures_table(const char *signatures);                 // NOLINT(readability-identifier-naming)
Value wren_init_class(const char *name, uint8_t *dataBlock);                 // NOLINT(readability-identifier-naming)
Value wren_alloc_obj(Value classVar);                                        // NOLINT(readability-identifier-naming)
int wren_class_get_field_offset(Value classVar);                             // NOLINT(readability-identifier-naming)
ClosureSpec *wren_register_closure(void *specData);                          // NOLINT(readability-identifier-naming)
Value wren_create_closure(ClosureSpec *spec, void *stack, ObjFn **listHead); // NOLINT(readability-identifier-naming)
Value **wren_get_closure_upvalue_pack(ObjFn *closure);                       // NOLINT(readability-identifier-naming)
ObjFn *wren_get_closure_chain_next(ObjFn *closure);                          // NOLINT(readability-identifier-naming)
void *wren_alloc_upvalue_storage(int numClosures);                           // NOLINT(readability-identifier-naming)
}

void *wren_virtual_method_lookup(Value receiver, uint64_t signature) {
	ObjClass *type;

	if (is_object(receiver)) {
		Obj *object = (Obj *)get_object_value(receiver);
		if (!object) {
			std::string name = ObjClass::LookupSignatureFromId({signature}, true);
			fprintf(stderr, "Cannot call method '%s' on null receiver\n", name.c_str());
			abort();
		}
		type = object->type;
	} else {
		// If it's not an object it must be a number, so say the receiver's type happens to be that
		type = ObjNumClass::Instance();
	}

	FunctionTable::Entry *func = type->LookupMethod(SignatureId{signature});

	if (!func) {
		std::string name = ObjClass::LookupSignatureFromId({signature}, true);
		fprintf(stderr, "On receiver of type %s, could not find method %s\n", type->name.c_str(), name.c_str());
		abort();
	}

	// printf("On receiver of type %s invoke %lx func %p\n", object->type->name.c_str(), signature, func->func);

	return func->func;
}

Value wren_init_string_literal(const char *literal, int length) {
	ObjString *str = WrenRuntime::Instance().New<ObjString>();
	str->m_value = std::string(literal, length);
	return encode_object(str);
}

void wren_register_signatures_table(const char *signatures) {
	while (true) {
		// If there's an empty string, that signifies the end of the table
		if (*signatures == 0)
			break;

		// Find and process this string
		std::string signature = signatures;
		signatures += signature.size() + 1; // +1 for trailing null

		// Looking up a signature is enough to register it
		ObjClass::FindSignatureId(signature);
	}
}

Value wren_init_class(const char *name, uint8_t *dataBlock) {
	std::unique_ptr<ClassDescription> spec = std::make_unique<ClassDescription>();
	spec->Parse(dataBlock);

	ObjManagedClass *cls = WrenRuntime::Instance().New<ObjManagedClass>(name, std::move(spec));

	for (const ClassDescription::MethodDecl &method : cls->spec->methods) {
		ObjClass *target = method.isStatic ? cls->type : cls;
		target->AddFunction(method.name, method.func);
	}

	return encode_object(cls);
}

Value wren_alloc_obj(Value classVar) {
	if (!is_object(classVar)) {
		fprintf(stderr, "Cannot call wren_alloc_object with number argument\n");
		abort();
	}

	ObjManagedClass *cls = dynamic_cast<ObjManagedClass *>(get_object_value(classVar));
	if (!cls) {
		fprintf(stderr, "Cannot call wren_alloc_object with null or non-ObjManagedClass type\n");
		abort();
	}

	// We have to allocate managed objects specially, to account for their variable-sized field area
	void *mem = WrenRuntime::Instance().AllocateMem(cls->size, alignof(ObjManaged));
	memset(mem, 0, cls->size);                   // Zero as a matter of good practice
	ObjManaged *obj = new (mem) ObjManaged(cls); // Initialise with placement-new

	// Null-initialise all the fields
	Value *fieldsEnd = (Value *)((uint64_t)obj + cls->size);
	for (Value *i = obj->fields; i < fieldsEnd; i++) {
		*i = NULL_VAL;
	}

	return encode_object(obj);
}

int wren_class_get_field_offset(Value classVar) {
	if (!is_object(classVar)) {
		fprintf(stderr, "Cannot call wren_class_get_field_offset with number argument\n");
		abort();
	}

	ObjManagedClass *cls = dynamic_cast<ObjManagedClass *>(get_object_value(classVar));
	if (!cls) {
		fprintf(stderr, "Cannot call wren_class_get_field_offset with null or non-ObjManagedClass type\n");
		abort();
	}

	return cls->fieldOffset;
}

ClosureSpec *wren_register_closure(void *specData) {
	// Leaks memory, but it'd never be freed anyway since it gets put in a module-level global
	return new ClosureSpec(specData);
}

Value wren_create_closure(ClosureSpec *spec, void *stack, ObjFn **listHead) {
	if (spec == nullptr) {
		fprintf(stderr, "Cannot pass null spec to wren_create_closure\n");
		abort();
	}

	// Stack may be null if we have no upvalues
	ObjFn *closure = WrenRuntime::Instance().New<ObjFn>(spec, stack);

	// Add this object to the linked list of all the other functions of the same type that have been created
	// This is used for tracking which closures need to be fixed up when their upvalues escape.
	// If this closure doesn't use upvalues, then listHead will be null as there's no need to track it.
	if (listHead) {
		closure->upvalueFixupList = *listHead;
		*listHead = closure;
	}

	return closure->ToValue();
}

Value **wren_get_closure_upvalue_pack(ObjFn *closure) { return closure->upvaluePointers.data(); }
ObjFn *wren_get_closure_chain_next(ObjFn *closure) {
	// TODO implement
	return nullptr;
}

// Allocate space for a closed upvalue on the heap, move the value there, and update all the closures that
// point to that value over to the new storage location.
void *wren_alloc_upvalue_storage(int numClosures) {
	// TODO reference-counting stuff
	return WrenRuntime::Instance().AllocateMem(sizeof(Value) * numClosures, 8);
}

void setupGenEntry() {
	wren_sys_var_Bool = ObjBool::Class()->ToValue();
	wren_sys_var_Object = CoreClasses::Instance()->Object().ToValue();
	wren_sys_var_Class = CoreClasses::Instance()->RootClass().ToValue();
	wren_sys_var_Fn = ObjFn::Class()->ToValue();
	wren_sys_var_List = ObjList::Class()->ToValue();
	wren_sys_var_Num = ObjNumClass::Instance()->ToValue();
	wren_sys_var_String = ObjString::Class()->ToValue();
	wren_sys_var_System = CoreClasses::Instance()->System()->ToValue();

	wren_sys_bool_true = ObjBool::Get(true)->ToValue();
	wren_sys_bool_false = ObjBool::Get(false)->ToValue();
}
