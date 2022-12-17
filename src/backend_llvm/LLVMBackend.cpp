//
// Created by znix on 09/12/22.
//

#include "wrencc_config.h"

#ifdef USE_LLVM

#include "CompContext.h"
#include "HashUtil.h"
#include "LLVMBackend.h"
#include "Scope.h"
#include "common.h"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>

#include <fmt/format.h>

#include <map>

using CInt = llvm::ConstantInt;

// Wrap everything in a namespace to avoid any possibility of name collisions
namespace wren_llvm_backend {

struct UpvaluePackDef {
	// All the variables bound to upvalues in the relevant closure
	std::vector<UpvalueVariable *> variables;

	// The positions of the variables in the upvalue pack, the inverse of variables
	std::unordered_map<UpvalueVariable *, int> variableIds;
};

struct VisitorContext {
	/// For each local variable, stack memory is allocated for it (and later optimised away - we do this
	/// to avoid having to deal with SSA, and this is also how Clang does it) and the value for that
	/// stack address is stored here.
	///
	/// This does not contain entries for variables used by closures.
	std::map<LocalVariable *, llvm::Value *> localAddresses;

	/// For each variable that some closure uses, they're stored in a single large array. This contains
	/// the position of each of them in that array.
	std::map<LocalVariable *, int> closedAddressPositions;

	/// The array of closable variables
	llvm::Value *closableVariables = nullptr;

	/// The function's upvalue pack, used to reference upvalues from this closure's parent function.
	UpvaluePackDef *upvaluePack = nullptr;

	/// The array of upvalue value pointers.
	llvm::Value *upvaluePackPtr = nullptr;

	/// For each closure that references an upvalue from this function, there is a linked list of all the instances
	/// of that closure. This is used to change the upvalue pointers for that closure for the variables that are
	/// moved to heap storage.
	std::map<IRFn *, llvm::Value *> closureInstanceLists;

	llvm::Function *currentFunc = nullptr;
};

struct ExprRes {
	llvm::Value *value = nullptr;
};

struct StmtRes {};

struct FnData {
	llvm::Function *llvmFunc = nullptr;
	llvm::GlobalVariable *closureSpec = nullptr;
	std::unique_ptr<UpvaluePackDef> upvaluePackDef;

	/// Same meaning as VisitorContext.closedAddressPositions
	std::map<LocalVariable *, int> closedAddressPositions;
};

class LLVMBackendImpl : public LLVMBackend {
  public:
	LLVMBackendImpl();

	CompilationResult Generate(Module *aModule) override;

  private:
	llvm::Function *GenerateFunc(IRFn *func, bool initialiser);
	void GenerateInitialiser();

	llvm::Constant *GetStringConst(const std::string &str);
	llvm::Value *GetManagedStringValue(const std::string &str);
	llvm::Value *GetGlobalVariable(IRGlobalDecl *global);
	llvm::Value *GetLocalPointer(VisitorContext *ctx, LocalVariable *local);
	llvm::Value *GetUpvaluePointer(VisitorContext *ctx, UpvalueVariable *upvalue);

	ExprRes VisitExpr(VisitorContext *ctx, IRExpr *expr);
	StmtRes VisitStmt(VisitorContext *ctx, IRStmt *expr);

	ExprRes VisitExprConst(VisitorContext *ctx, ExprConst *node);
	ExprRes VisitExprLoad(VisitorContext *ctx, ExprLoad *node);
	ExprRes VisitExprFieldLoad(VisitorContext *ctx, ExprFieldLoad *node);
	ExprRes VisitExprFuncCall(VisitorContext *ctx, ExprFuncCall *node);
	ExprRes VisitExprClosure(VisitorContext *ctx, ExprClosure *node);
	ExprRes VisitExprLoadReceiver(VisitorContext *ctx, ExprLoadReceiver *node);
	ExprRes VisitExprRunStatements(VisitorContext *ctx, ExprRunStatements *node);
	ExprRes VisitExprAllocateInstanceMemory(VisitorContext *ctx, ExprAllocateInstanceMemory *node);
	ExprRes VisitExprSystemVar(VisitorContext *ctx, ExprSystemVar *node);
	ExprRes VisitExprGetClassVar(VisitorContext *ctx, ExprGetClassVar *node);

	StmtRes VisitStmtAssign(VisitorContext *ctx, StmtAssign *node);
	StmtRes VisitStmtFieldAssign(VisitorContext *ctx, StmtFieldAssign *node);
	StmtRes VisitStmtEvalAndIgnore(VisitorContext *ctx, StmtEvalAndIgnore *node);
	StmtRes VisitBlock(VisitorContext *ctx, StmtBlock *node);
	StmtRes VisitStmtLabel(VisitorContext *ctx, StmtLabel *node);
	StmtRes VisitStmtJump(VisitorContext *ctx, StmtJump *node);
	StmtRes VisitStmtReturn(VisitorContext *ctx, StmtReturn *node);
	StmtRes VisitStmtLoadModule(VisitorContext *ctx, StmtLoadModule *node);
	StmtRes VisitStmtRelocateUpvalues(VisitorContext *ctx, StmtRelocateUpvalues *node);

	llvm::LLVMContext m_context;
	llvm::IRBuilder<> m_builder;
	llvm::Module m_module;

	llvm::Function *m_initFunc = nullptr;

	llvm::FunctionCallee m_virtualMethodLookup;
	llvm::FunctionCallee m_createClosure;
	llvm::FunctionCallee m_allocUpvalueStorage;
	llvm::FunctionCallee m_getUpvaluePack;
	llvm::FunctionCallee m_getNextClosure;

	llvm::PointerType *m_pointerType = nullptr;
	llvm::Type *m_signatureType = nullptr;
	llvm::IntegerType *m_valueType = nullptr;
	llvm::IntegerType *m_int32Type = nullptr;
	llvm::IntegerType *m_int64Type = nullptr;

	llvm::ConstantInt *m_nullValue = nullptr;
	llvm::Constant *m_nullPointer = nullptr;

	std::map<std::string, llvm::GlobalVariable *> m_systemVars;
	std::map<std::string, llvm::GlobalVariable *> m_stringConstants;
	std::map<std::string, llvm::GlobalVariable *> m_managedStrings;
	std::map<IRGlobalDecl *, llvm::GlobalVariable *> m_globalVariables;

	// A set of all the system variables used in the code. Any other system variables will be removed.
	std::set<llvm::GlobalVariable *> m_usedSystemVars;

	std::map<IRFn *, FnData> m_fnData;
};

LLVMBackendImpl::LLVMBackendImpl() : m_builder(m_context), m_module("myModule", m_context) {
	m_valueType = llvm::Type::getInt64Ty(m_context);
	m_signatureType = llvm::Type::getInt64Ty(m_context);
	m_pointerType = llvm::PointerType::get(m_context, 0);
	m_int32Type = llvm::Type::getInt32Ty(m_context);
	m_int64Type = llvm::Type::getInt64Ty(m_context);

	m_nullValue = CInt::get(m_valueType, encode_object(nullptr));
	m_nullPointer = llvm::ConstantPointerNull::get(m_pointerType);

	std::vector<llvm::Type *> fnLookupArgs = {m_valueType, m_signatureType};
	llvm::FunctionType *fnLookupType = llvm::FunctionType::get(m_pointerType, fnLookupArgs, false);
	m_virtualMethodLookup = m_module.getOrInsertFunction("wren_virtual_method_lookup", fnLookupType);

	std::vector<llvm::Type *> newClosureArgs = {m_pointerType, m_pointerType, m_pointerType};
	llvm::FunctionType *newClosureTypes = llvm::FunctionType::get(m_valueType, newClosureArgs, false);
	m_createClosure = m_module.getOrInsertFunction("wren_create_closure", newClosureTypes);

	llvm::FunctionType *allocUpvalueStorageType = llvm::FunctionType::get(m_pointerType, {m_int32Type}, false);
	m_allocUpvalueStorage = m_module.getOrInsertFunction("wren_alloc_upvalue_storage", allocUpvalueStorageType);

	llvm::FunctionType *getUpvaluePackType = llvm::FunctionType::get(m_pointerType, {m_pointerType}, false);
	m_getUpvaluePack = m_module.getOrInsertFunction("wren_get_closure_upvalue_pack", getUpvaluePackType);

	llvm::FunctionType *getNextClosureType = llvm::FunctionType::get(m_pointerType, {m_pointerType}, false);
	m_getNextClosure = m_module.getOrInsertFunction("wren_get_closure_chain_next", getNextClosureType);
}

CompilationResult LLVMBackendImpl::Generate(Module *module) {
	// Create all the system variables with the correct linkage
	for (const auto &entry : ExprSystemVar::SYSTEM_VAR_NAMES) {
		std::string name = "wren_sys_var_" + entry.first;
		m_systemVars[entry.first] = new llvm::GlobalVariable(m_module, m_valueType, false,
		                                                     llvm::GlobalVariable::InternalLinkage, m_nullValue, name);
	}

	llvm::FunctionType *initFuncType = llvm::FunctionType::get(llvm::Type::getVoidTy(m_context), false);
	m_initFunc = llvm::Function::Create(initFuncType, llvm::Function::PrivateLinkage, "module_init", &m_module);

	for (IRFn *func : module->GetClosures()) {
		// Make a global variable for the ClosureSpec
		m_fnData[func].closureSpec =
		    new llvm::GlobalVariable(m_module, m_pointerType, false, llvm::GlobalVariable::InternalLinkage,
		                             m_nullPointer, "spec_" + func->debugName);

		// Make the upvalue pack for each function that needs them
		std::unique_ptr<UpvaluePackDef> pack = std::make_unique<UpvaluePackDef>();

		for (const auto &entry : func->upvalues) {
			// Assign an increasing series of IDs for each variable in an arbitrary order
			pack->variables.push_back(entry.second);
			pack->variableIds[entry.second] = pack->variables.size() - 1; // -1 to get the index of the last entry
		}

		// Note we always have to register an upvalue pack definition, even if it's empty - it's required for closures.
		m_fnData[func].upvaluePackDef = std::move(pack);
	}

	for (IRFn *func : module->GetFunctions()) {
		m_fnData[func].llvmFunc = GenerateFunc(func, func == module->GetMainFunction());
	}

	// Generate the initialiser last, when we know all the string constants etc
	GenerateInitialiser();

	// Identify this module as containing the main function - TODO with defineStandaloneMainFunc variable
	if (true) {
		// Emit a pointer to the main module function. This is picked up by the stub the programme gets linked to.
		// This stub (in rtsrc/standalone_main_stub.cpp) uses the OS's standard crti/crtn and similar objects to
		// make a working executable, and it'll load this pointer when we link this object to it.
		// Also, put it in .data not .rodata since it contains a relocation.
		// Const-casating is ugly, but it works.
		llvm::Function *main = m_fnData.at(const_cast<IRFn *>(module->GetMainFunction())).llvmFunc;
		new llvm::GlobalVariable(m_module, m_pointerType, true, llvm::GlobalVariable::ExternalLinkage, main,
		                         "wrenStandaloneMainFunc");
	}

	// Test it
	m_module.print(llvm::outs(), nullptr);

	// Verify the IR, to make sure we haven't done something strange
	if (llvm::verifyModule(m_module, &llvm::outs())) {
		fprintf(stderr, "LLVM IR Validation failed!\n");
		exit(1);
	}

	// Compile it - FIXME is this going to constantly re-initialise everything?
	llvm::InitializeAllTargetInfos();
	llvm::InitializeAllTargets();
	llvm::InitializeAllTargetMCs();
	llvm::InitializeAllAsmParsers();
	llvm::InitializeAllAsmPrinters();

	// Compile for the default target, TODO this should be configurable
	std::string targetTriple = llvm::sys::getDefaultTargetTriple();

	std::string lookupError;
	const llvm::Target *target = llvm::TargetRegistry::lookupTarget(targetTriple, lookupError);
	if (!target) {
		fprintf(stderr, "Failed to lookup target '%s'\n", targetTriple.c_str());
		exit(1);
	}

	// CPU features to use - eg SSE, AVX, NEON
	std::string cpu = "generic";
	std::string features = "";

	llvm::TargetOptions opt;
	std::optional<llvm::Reloc::Model> relocModel;
	llvm::TargetMachine *targetMachine = target->createTargetMachine(targetTriple, cpu, features, opt, relocModel);

	m_module.setDataLayout(targetMachine->createDataLayout());
	m_module.setTargetTriple(targetTriple);

	// Actually generate the code
	std::string filename = "/tmp/wren-output.o";
	std::error_code ec;
	llvm::raw_fd_ostream dest(filename, ec, llvm::sys::fs::OF_None);

	if (ec) {
		std::string msg = ec.message();
		fprintf(stderr, "Could not open file: %s", msg.c_str());
		exit(1);
	}

	// TODO switch to the new PassManager
	llvm::legacy::PassManager pass;
	llvm::CodeGenFileType fileType = llvm::CGFT_ObjectFile;

	if (targetMachine->addPassesToEmitFile(pass, dest, nullptr, fileType)) {
		fprintf(stderr, "TargetMachine can't emit a file of this type");
		exit(1);
	}

	pass.run(m_module);
	dest.flush();

	return CompilationResult{
	    .successful = true,
	    .tempFilename = filename,
	    .format = CompilationResult::OBJECT,
	};
}

llvm::Function *LLVMBackendImpl::GenerateFunc(IRFn *func, bool initialiser) {
	FnData &fnData = m_fnData[func];

	// Only take an upvalue pack argument if we actually need it
	bool takesUpvaluePack = fnData.upvaluePackDef && !fnData.upvaluePackDef->variables.empty();

	// Set up the function arguments
	std::vector<llvm::Type *> funcArgs;
	// TODO receiver
	if (takesUpvaluePack) {
		// If this function uses upvalues, they're passed as an argument
		funcArgs.push_back(m_pointerType);
	}

	// The 'regular' arguments, that the user would see
	funcArgs.insert(funcArgs.end(), func->arity, m_valueType);

	llvm::FunctionType *ft = llvm::FunctionType::get(m_valueType, funcArgs, false);

	llvm::Function *function = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, func->debugName, &m_module);
	llvm::BasicBlock *bb = llvm::BasicBlock::Create(m_context, "entry", function);
	m_builder.SetInsertPoint(bb);

	if (initialiser) {
		// Call the initialiser, which we'll generate later
		m_builder.CreateCall(m_initFunc->getFunctionType(), m_initFunc);
	}

	VisitorContext ctx = {};
	ctx.currentFunc = function;

	std::vector<LocalVariable *> closables;

	for (LocalVariable *local : func->locals) {
		if (local->upvalues.empty()) {
			// Normal local variable
			ctx.localAddresses[local] = m_builder.CreateAlloca(m_valueType, nullptr, local->Name());
		} else {
			// This variable is accessed by closures, so it gets stored in the array of closable variables.
			ctx.closedAddressPositions[local] = closables.size();
			closables.push_back(local);

			// Create linked lists of all the functions that use our variables as upvalues
			for (UpvalueVariable *upvalue : local->upvalues) {
				IRFn *closure = upvalue->containingFunction;
				if (ctx.closureInstanceLists.contains(closure))
					continue;

				ctx.closureInstanceLists[closure] =
				    m_builder.CreateAlloca(m_pointerType, nullptr, "closure_list_" + closure->debugName);
			}
		}
	}
	for (LocalVariable *local : func->temporaries) {
		ctx.localAddresses[local] = m_builder.CreateAlloca(m_valueType, nullptr, local->Name());
	}

	if (!closables.empty()) {
		ctx.closableVariables =
		    m_builder.CreateAlloca(m_valueType, CInt::get(m_int32Type, closables.size()), "closables");
	}

	// Copy across the position data, as it's used to generate the closure specs
	fnData.closedAddressPositions = ctx.closedAddressPositions;

	// Load the upvalue pack
	int nextArg = 0;
	// TODO 'this' arg
	if (takesUpvaluePack) {
		ctx.upvaluePack = fnData.upvaluePackDef.get();
		ctx.upvaluePackPtr = function->getArg(nextArg++);
		ctx.upvaluePackPtr->setName("upvalue_pack");
	}

	VisitStmt(&ctx, func->body);

	return function;
}

void LLVMBackendImpl::GenerateInitialiser() {
	llvm::BasicBlock *bb = llvm::BasicBlock::Create(m_context, "entry", m_initFunc);
	m_builder.SetInsertPoint(bb);

	// Remove any unused system variables, for ease of reading the LLVM IR
	for (const auto &entry : ExprSystemVar::SYSTEM_VAR_NAMES) {
		llvm::GlobalVariable *var = m_systemVars.at(entry.first);
		if (m_usedSystemVars.contains(var))
			continue;
		m_systemVars.erase(entry.first);
		m_module.getGlobalList().erase(var);
	}

	// Load the variables for all the core values
	std::vector<llvm::Type *> argTypes = {m_pointerType};
	llvm::FunctionType *sysLookupType = llvm::FunctionType::get(m_valueType, argTypes, false);
	llvm::FunctionCallee getSysVarFn = m_module.getOrInsertFunction("wren_get_core_class_value", sysLookupType);

	for (const auto &entry : m_systemVars) {
		std::vector<llvm::Value *> args = {GetStringConst(entry.first)};
		llvm::Value *result = m_builder.CreateCall(getSysVarFn, args, "var_" + entry.first);

		m_builder.CreateStore(result, entry.second);
	}

	// Create all the string constants

	argTypes = {m_pointerType, m_int32Type};
	llvm::FunctionType *newStringType = llvm::FunctionType::get(m_valueType, argTypes, false);
	llvm::FunctionCallee newStringFn = m_module.getOrInsertFunction("wren_init_string_literal", newStringType);

	for (const auto &entry : m_managedStrings) {
		// Create a raw C string
		llvm::Constant *strPtr = GetStringConst(entry.first);

		// And construct a string object from it
		std::vector<llvm::Value *> args = {strPtr, CInt::get(m_int32Type, entry.first.size())};
		llvm::Value *value = m_builder.CreateCall(newStringFn, args);

		m_builder.CreateStore(value, entry.second);
	}

	// Register the upvalues, creating ClosureSpec-s for each closure
	argTypes = {m_pointerType};
	llvm::FunctionType *newClosureType = llvm::FunctionType::get(m_pointerType, argTypes, false);
	llvm::FunctionCallee newClosureFn = m_module.getOrInsertFunction("wren_register_closure", newClosureType);
	for (const auto &entry : m_fnData) {
		IRFn *fn = entry.first;
		const FnData &fnData = entry.second;

		// Only produce ClosureSpecs for closures
		if (fnData.closureSpec == nullptr)
			continue;

		// For each upvalue, tell the runtime about it and save the description object it gives us. This object
		// is then used to closure objects wrapping this function.

		UpvaluePackDef *upvaluePack = fnData.upvaluePackDef.get();
		int numUpvalues = upvaluePack->variables.size();

		std::vector<llvm::Type *> specTypes = {m_pointerType, m_pointerType, m_int32Type, m_int32Type};
		specTypes.insert(specTypes.end(), numUpvalues, m_int64Type); // Add the upvalue indices
		llvm::StructType *closureSpecType = llvm::StructType::get(m_context, specTypes);

		// Generate the spec table
		std::vector<llvm::Constant *> structContent = {
		    fnData.llvmFunc,                     // function pointer
		    GetStringConst(fn->debugName),       // name C string
		    CInt::get(m_int32Type, fn->arity),   // Arity
		    CInt::get(m_int32Type, numUpvalues), // Upvalue count
		};
		for (UpvalueVariable *upvalue : upvaluePack->variables) {
			IRFn *parentFn = fn->parent;
			FnData &parentData = m_fnData.at(parentFn);

			LocalVariable *target = dynamic_cast<LocalVariable *>(upvalue->parent);
			if (!target) {
				fmt::print(stderr, "Upvalue {} has non-local parent scope {}.\n", upvalue->Name(),
				           upvalue->parent->Scope());
				abort();
			}

			if (!parentData.closedAddressPositions.contains(target)) {
				fmt::print(stderr, "Function {} doesn't have closeable local {}, used by closure {}.\n",
				           parentFn->debugName, target->Name(), fn->debugName);
				abort();
			}
			int index = parentData.closedAddressPositions.at(target);

			structContent.push_back(CInt::get(m_int64Type, index));
		}

		llvm::Constant *constant = llvm::ConstantStruct::get(closureSpecType, structContent);

		llvm::GlobalVariable *specData =
		    new llvm::GlobalVariable(m_module, constant->getType(), true, llvm::GlobalVariable::PrivateLinkage,
		                             constant, "closure_spec_" + fn->debugName);

		// And generate the registration code
		std::vector<llvm::Value *> args = {specData};
		llvm::Value *spec = m_builder.CreateCall(newClosureFn, args, fn->debugName);
		m_builder.CreateStore(spec, fnData.closureSpec);
	}

	// Functions must return!
	m_builder.CreateRetVoid();
}

llvm::Constant *LLVMBackendImpl::GetStringConst(const std::string &str) {
	auto iter = m_stringConstants.find(str);
	if (iter != m_stringConstants.end())
		return iter->second;

	std::vector<int8_t> data(str.size() + 1);
	std::copy(str.begin(), str.end(), data.begin());

	llvm::Constant *constant = llvm::ConstantDataArray::get(m_context, data);
	llvm::GlobalVariable *var = new llvm::GlobalVariable(m_module, constant->getType(), true,
	                                                     llvm::GlobalVariable::PrivateLinkage, constant, "str_" + str);

	m_stringConstants[str] = var;
	return var;
}

llvm::Value *LLVMBackendImpl::GetManagedStringValue(const std::string &str) {
	auto iter = m_managedStrings.find(str);
	if (iter != m_managedStrings.end())
		return iter->second;

	llvm::GlobalVariable *var = new llvm::GlobalVariable(
	    m_module, m_valueType, false, llvm::GlobalVariable::PrivateLinkage, m_nullValue, "strobj_" + str);
	m_managedStrings[str] = var;
	return var;
}

llvm::Value *LLVMBackendImpl::GetGlobalVariable(IRGlobalDecl *global) {
	auto iter = m_globalVariables.find(global);
	if (iter != m_globalVariables.end())
		return iter->second;

	llvm::GlobalVariable *var = new llvm::GlobalVariable(
	    m_module, m_valueType, false, llvm::GlobalVariable::PrivateLinkage, m_nullValue, "gbl_" + global->Name());
	m_globalVariables[global] = var;
	return var;
}

llvm::Value *LLVMBackendImpl::GetLocalPointer(VisitorContext *ctx, LocalVariable *local) {
	const auto iter = ctx->localAddresses.find(local);
	if (iter != ctx->localAddresses.end())
		return iter->second;

	// Check if it's a closed-over variable
	const auto iter2 = ctx->closedAddressPositions.find(local);
	if (iter2 != ctx->closedAddressPositions.end()) {
		std::vector<llvm::Value *> indices = {
		    // Select the item we're interested in.
		    CInt::get(m_int32Type, iter2->second),
		};
		return m_builder.CreateGEP(m_valueType, ctx->closableVariables, indices, "lv_ptr_" + local->Name());
	}

	fmt::print(stderr, "Found unallocated local variable: '{}'\n", local->Name());
	abort();
}

llvm::Value *LLVMBackendImpl::GetUpvaluePointer(VisitorContext *ctx, UpvalueVariable *upvalue) {
	if (!ctx->upvaluePack) {
		fmt::print(stderr, "Found UpvalueVariable without an upvalue pack!\n");
		abort();
	}

	auto upvalueIter = ctx->upvaluePack->variableIds.find(upvalue);
	if (upvalueIter == ctx->upvaluePack->variableIds.end()) {
		fmt::print(stderr, "Could not find upvalue in current pack for variable {}\n", upvalue->parent->Name());
		abort();
	}
	int position = upvalueIter->second;

	// Get a pointer pointing to the position in the upvalue pack where this variable is.
	// Recall the upvalue pack is an array of pointers, each one pointing to a value.
	std::vector<llvm::Value *> args = {CInt::get(m_int32Type, position)};
	llvm::Value *varPtrPtr =
	    m_builder.CreateGEP(m_pointerType, ctx->upvaluePackPtr, args, "uv_pptr_" + upvalue->Name());

	// The upvalue pack stores pointers, so at this point our variable is a Value** and we have to dereference
	// it twice. In the future we should store never-modified variables directly, so this would be Value*.
	// This chases the pointer in-place, that is in the same variable, to avoid the bother of declaring another one.
	return m_builder.CreateLoad(m_pointerType, varPtrPtr, "uv_ptr_" + upvalue->Name());
}

// Visitors
ExprRes LLVMBackendImpl::VisitExpr(VisitorContext *ctx, IRExpr *expr) {
#define DISPATCH(func, type)                                                                                           \
	do {                                                                                                               \
		if (typeid(*expr) == typeid(type))                                                                             \
			return func(ctx, dynamic_cast<type *>(expr));                                                              \
	} while (0)

	// Use both the function name and type for ease of searching and IDE indexing
	DISPATCH(VisitExprConst, ExprConst);
	DISPATCH(VisitExprLoad, ExprLoad);
	DISPATCH(VisitExprFieldLoad, ExprFieldLoad);
	DISPATCH(VisitExprFuncCall, ExprFuncCall);
	DISPATCH(VisitExprClosure, ExprClosure);
	DISPATCH(VisitExprLoadReceiver, ExprLoadReceiver);
	DISPATCH(VisitExprRunStatements, ExprRunStatements);
	DISPATCH(VisitExprAllocateInstanceMemory, ExprAllocateInstanceMemory);
	DISPATCH(VisitExprSystemVar, ExprSystemVar);
	DISPATCH(VisitExprGetClassVar, ExprGetClassVar);

#undef DISPATCH

	fmt::print("Unknown expr node {}\n", typeid(*expr).name());
	abort();
	return {};
}

StmtRes LLVMBackendImpl::VisitStmt(VisitorContext *ctx, IRStmt *expr) {
#define DISPATCH(func, type)                                                                                           \
	do {                                                                                                               \
		if (typeid(*expr) == typeid(type))                                                                             \
			return func(ctx, dynamic_cast<type *>(expr));                                                              \
	} while (0)

	DISPATCH(VisitStmtAssign, StmtAssign);
	DISPATCH(VisitStmtFieldAssign, StmtFieldAssign);
	DISPATCH(VisitStmtEvalAndIgnore, StmtEvalAndIgnore);
	DISPATCH(VisitBlock, StmtBlock);
	DISPATCH(VisitStmtLabel, StmtLabel);
	DISPATCH(VisitStmtJump, StmtJump);
	DISPATCH(VisitStmtReturn, StmtReturn);
	DISPATCH(VisitStmtLoadModule, StmtLoadModule);
	DISPATCH(VisitStmtRelocateUpvalues, StmtRelocateUpvalues);

#undef DISPATCH

	fmt::print("Unknown stmt node {}\n", typeid(*expr).name());
	abort();
	return {};
}

ExprRes LLVMBackendImpl::VisitExprConst(VisitorContext *ctx, ExprConst *node) {
	llvm::Value *value;
	switch (node->value.type) {
	case CcValue::NULL_TYPE:
		value = m_nullValue;
		break;
	case CcValue::STRING: {
		llvm::Value *ptr = GetManagedStringValue(node->value.s);
		// FIXME only use a short prefix of the string
		value = m_builder.CreateLoad(m_valueType, ptr, "strobj_" + node->value.s);
		break;
	}
	case CcValue::BOOL:
		abort();
		break;
	case CcValue::INT:
		value = CInt::get(m_valueType, encode_number(node->value.i));
		break;
	case CcValue::NUM:
		value = CInt::get(m_valueType, encode_number(node->value.n));
		break;
	default:
		fprintf(stderr, "Invalid constant node type %d\n", (int)node->value.type);
		abort();
		break;
	}

	return {value};
}
ExprRes LLVMBackendImpl::VisitExprLoad(VisitorContext *ctx, ExprLoad *node) {
	LocalVariable *local = dynamic_cast<LocalVariable *>(node->var);
	UpvalueVariable *upvalue = dynamic_cast<UpvalueVariable *>(node->var);
	IRGlobalDecl *global = dynamic_cast<IRGlobalDecl *>(node->var);

	if (local) {
		llvm::Value *ptr = GetLocalPointer(ctx, local);
		llvm::Value *value = m_builder.CreateLoad(m_valueType, ptr, node->var->Name() + "_value");
		return {value};
	} else if (upvalue) {
		llvm::Value *ptr = GetUpvaluePointer(ctx, upvalue);
		llvm::Value *var = m_builder.CreateLoad(m_valueType, ptr, "uv_" + upvalue->Name());
		return {var};
	} else if (global) {
		llvm::Value *ptr = GetGlobalVariable(global);
		llvm::Value *value = m_builder.CreateLoad(m_valueType, ptr, node->var->Name() + "_value");
		return {value};
	} else {
		fprintf(stderr, "Attempted to load non-local, non-global, non-upvalue variable %p\n", node->var);
		abort();
	}
}
ExprRes LLVMBackendImpl::VisitExprFieldLoad(VisitorContext *ctx, ExprFieldLoad *node) {
	printf("error: not implemented: VisitExprFieldLoad\n");
	abort();
	return {};
}
ExprRes LLVMBackendImpl::VisitExprFuncCall(VisitorContext *ctx, ExprFuncCall *node) {
	ExprRes receiver = VisitExpr(ctx, node->receiver);

	std::vector<llvm::Value *> args;
	args.push_back(receiver.value);
	for (IRExpr *expr : node->args) {
		ExprRes res = VisitExpr(ctx, expr);
		args.push_back(res.value);
	}

	std::string name = node->signature->ToString();
	// TODO put in signature list
	SignatureId signature = hash_util::findSignatureId(name);
	llvm::Value *sigValue = CInt::get(m_signatureType, signature.id);

	// Call the lookup function
	std::vector<llvm::Value *> lookupArgs = {receiver.value, sigValue};
	llvm::CallInst *func = m_builder.CreateCall(m_virtualMethodLookup, lookupArgs, "vptr_" + name);

	// Make the function type - TODO cache
	std::vector<llvm::Type *> argTypes(args.size(), m_valueType);
	llvm::FunctionType *type = llvm::FunctionType::get(m_valueType, argTypes, false);

	// Invoke it
	llvm::CallInst *result = m_builder.CreateCall(type, func, args);

	return {result};
}
ExprRes LLVMBackendImpl::VisitExprClosure(VisitorContext *ctx, ExprClosure *node) {
	llvm::Value *closables = m_nullPointer;
	llvm::Value *funcList = m_nullPointer;
	if (!node->func->upvalues.empty()) {
		closables = ctx->closableVariables;
	}
	if (ctx->closureInstanceLists.contains(node->func)) {
		funcList = ctx->closureInstanceLists[node->func];
	}

	llvm::Value *closureSpec = m_fnData.at(node->func).closureSpec;
	llvm::Value *specObj = m_builder.CreateLoad(m_pointerType, closureSpec, "closure_spec_" + node->func->debugName);
	std::vector<llvm::Value *> args = {specObj, closables, funcList};
	llvm::Value *closure = m_builder.CreateCall(m_createClosure, args, "closure_" + node->func->debugName);

	return {closure};
}
ExprRes LLVMBackendImpl::VisitExprLoadReceiver(VisitorContext *ctx, ExprLoadReceiver *node) {
	printf("error: not implemented: VisitExprLoadReceiver\n");
	abort();
	return {};
}
ExprRes LLVMBackendImpl::VisitExprRunStatements(VisitorContext *ctx, ExprRunStatements *node) {
	VisitStmt(ctx, node->statement);

	llvm::Value *ptr = GetLocalPointer(ctx, node->temporary);
	llvm::Value *value = m_builder.CreateLoad(m_valueType, ptr, "temp_value");

	return {value};
}
ExprRes LLVMBackendImpl::VisitExprAllocateInstanceMemory(VisitorContext *ctx, ExprAllocateInstanceMemory *node) {
	printf("error: not implemented: VisitExprAllocateInstanceMemory\n");
	abort();
	return {};
}
ExprRes LLVMBackendImpl::VisitExprSystemVar(VisitorContext *ctx, ExprSystemVar *node) {
	llvm::GlobalVariable *global = m_systemVars.at(node->name);
	m_usedSystemVars.insert(global);
	llvm::Value *value = m_builder.CreateLoad(m_valueType, global, "gbl_" + node->name);
	return {value};
}
ExprRes LLVMBackendImpl::VisitExprGetClassVar(VisitorContext *ctx, ExprGetClassVar *node) {
	printf("error: not implemented: VisitExprGetClassVar\n");
	abort();
	return {};
}

// Statements

StmtRes LLVMBackendImpl::VisitStmtAssign(VisitorContext *ctx, StmtAssign *node) {
	LocalVariable *local = dynamic_cast<LocalVariable *>(node->var);
	UpvalueVariable *upvalue = dynamic_cast<UpvalueVariable *>(node->var);
	IRGlobalDecl *global = dynamic_cast<IRGlobalDecl *>(node->var);

	llvm::Value *value = VisitExpr(ctx, node->expr).value;

	if (local) {
		llvm::Value *ptr = GetLocalPointer(ctx, local);
		m_builder.CreateStore(value, ptr);
	} else if (upvalue) {
		llvm::Value *ptr = GetUpvaluePointer(ctx, upvalue);
		m_builder.CreateStore(value, ptr);
	} else if (global) {
		llvm::Value *ptr = GetGlobalVariable(global);
		m_builder.CreateStore(value, ptr);
	} else {
		fprintf(stderr, "Attempted to store to non-local, non-global, non-upvalue variable %p\n", node->var);
		abort();
	}

	return {};
}
StmtRes LLVMBackendImpl::VisitStmtFieldAssign(VisitorContext *ctx, StmtFieldAssign *node) {
	printf("error: not implemented: VisitStmtFieldAssign\n");
	abort();
	return {};
}
StmtRes LLVMBackendImpl::VisitStmtEvalAndIgnore(VisitorContext *ctx, StmtEvalAndIgnore *node) {
	VisitExpr(ctx, node->expr);
	return {};
}
StmtRes LLVMBackendImpl::VisitBlock(VisitorContext *ctx, StmtBlock *node) {
	for (IRStmt *stmt : node->statements) {
		VisitStmt(ctx, stmt);
	}
	return {};
}
StmtRes LLVMBackendImpl::VisitStmtLabel(VisitorContext *ctx, StmtLabel *node) {
	printf("error: not implemented: VisitStmtLabel\n");
	abort();
	return {};
}
StmtRes LLVMBackendImpl::VisitStmtJump(VisitorContext *ctx, StmtJump *node) {
	printf("error: not implemented: VisitStmtJump\n");
	abort();
	return {};
}
StmtRes LLVMBackendImpl::VisitStmtReturn(VisitorContext *ctx, StmtReturn *node) {
	ExprRes value = VisitExpr(ctx, node->value);
	m_builder.CreateRet(value.value);
	return {};
}
StmtRes LLVMBackendImpl::VisitStmtLoadModule(VisitorContext *ctx, StmtLoadModule *node) {
	printf("error: not implemented: VisitStmtLoadModule\n");
	abort();
	return {};
}
StmtRes LLVMBackendImpl::VisitStmtRelocateUpvalues(VisitorContext *ctx, StmtRelocateUpvalues *node) {
	// WARNING WARNING: Complicated and fragile code ahead
	// Note: here, 'relocation' means a variable we're moving from the stack to the heap, so closures
	// can continue using it once the block it was declared in is gone.

	// For now, move all our locals (that are used as upvalues) to the heap if any of our closures use them. This is
	// quite trigger-happy to use heap memory, which isn't great from a performance standpoint. Here's a few ideas of
	// things we could do to improve the situation:
	// * Partition the upvalues based on which functions use them, and handle them completely separately.
	// * Use static analysis to find if at least one instance of a closure is always created, and if so then we
	//   can omit the checking.

	// First, build a list of the variables that are used by closures. Those that aren't can obviously just
	// be ignored.
	std::vector<LocalVariable *> closables;
	std::set<IRFn *> funcsSet;
	for (LocalVariable *var : node->variables) {
		if (var->upvalues.empty())
			continue;

		closables.push_back(var);
		for (UpvalueVariable *upvalue : var->upvalues) {
			funcsSet.insert(upvalue->containingFunction);
		}
	}
	std::vector<IRFn *> funcs;
	funcs.insert(funcs.begin(), funcsSet.begin(), funcsSet.end());

	// No upvalues? Nothing to do.
	if (funcs.empty())
		return {};

	// Create the basic blocks for the cases where we have to relocate, and a terminator that we end with.
	llvm::BasicBlock *relocCase = llvm::BasicBlock::Create(m_context, "do_reloc_closures", ctx->currentFunc);

	std::map<IRFn *, llvm::BasicBlock *> relocateSetups;
	std::map<IRFn *, llvm::BasicBlock *> relocateLoops;
	for (IRFn *fn : funcs) {
		relocateSetups[fn] =
		    llvm::BasicBlock::Create(m_context, "relocate_" + fn->debugName + "_start", ctx->currentFunc);
		relocateLoops[fn] =
		    llvm::BasicBlock::Create(m_context, "relocate_" + fn->debugName + "_loop", ctx->currentFunc);
	}

	llvm::BasicBlock *endCase = llvm::BasicBlock::Create(m_context, "end_reloc_closures", ctx->currentFunc);

	// Now check if any closures have been created
	llvm::Value *noneExist = nullptr;
	for (IRFn *fn : funcs) {
		llvm::Value *value =
		    m_builder.CreateICmpEQ(m_nullPointer, ctx->closureInstanceLists.at(fn), "has_fn_" + fn->debugName);

		if (noneExist) {
			noneExist = m_builder.CreateAnd(noneExist, value);
		} else {
			noneExist = value;
		}
	}
	m_builder.CreateCondBr(noneExist, endCase, relocCase);

	// Make the relocation case
	m_builder.SetInsertPoint(relocCase);

	// Allocate the memory to store the variables, and copy them all in
	llvm::Value *upvalueStorage =
	    m_builder.CreateCall(m_allocUpvalueStorage, {CInt::get(m_int32Type, closables.size())}, "upvalueStorage");
	std::map<LocalVariable *, llvm::Value *> heapPtrs;
	for (size_t i = 0; i < closables.size(); i++) {
		LocalVariable *var = closables.at(i);
		llvm::Value *oldPtr = GetLocalPointer(ctx, var);
		llvm::Value *value = m_builder.CreateLoad(m_valueType, oldPtr);
		llvm::Value *destPtr = m_builder.CreateGEP(m_valueType, upvalueStorage, CInt::get(m_int32Type, i));
		m_builder.CreateStore(value, destPtr);
		heapPtrs[var] = destPtr;
	}

	m_builder.CreateBr(relocateSetups.at(funcs.front()));

	// For each of the functions, loop through and relocate them
	for (size_t i = 0; i < funcs.size(); i++) {
		IRFn *fn = funcs.at(i);
		UpvaluePackDef *pack = m_fnData.at(fn).upvaluePackDef.get();
		llvm::Value *closureListPtr = ctx->closureInstanceLists.at(fn);

		llvm::BasicBlock *start = relocateSetups.at(fn);
		llvm::BasicBlock *loop = relocateLoops.at(fn);
		llvm::BasicBlock *next = fn == funcs.back() ? endCase : relocateSetups.at(funcs.at(i + 1));

		m_builder.SetInsertPoint(start);
		// Get the pointer to the first Obj in the linked list
		llvm::Value *startValue = m_builder.CreateLoad(m_pointerType, closureListPtr);
		llvm::Value *isNull = m_builder.CreateICmpEQ(m_nullPointer, startValue);
		m_builder.CreateCondBr(isNull, next, loop);

		// Generate the main loop
		m_builder.SetInsertPoint(loop);
		llvm::PHINode *thisObj = m_builder.CreatePHI(m_pointerType, 2, "relocPtr");
		thisObj->addIncoming(startValue, start);

		llvm::Value *upvaluePack = m_builder.CreateCall(m_getUpvaluePack, thisObj);
		for (size_t j = 0; j < pack->variables.size(); j++) {
			UpvalueVariable *upvalue = pack->variables.at(i);
			LocalVariable *parent = dynamic_cast<LocalVariable *>(upvalue->parent);

			if (!heapPtrs.contains(parent))
				continue;

			// Modify the pack, so that this slot points to the new location of the value on the heap
			llvm::Value *newPtr = heapPtrs.at(parent);
			llvm::Value *packPPtr = m_builder.CreateGEP(m_pointerType, upvaluePack, {CInt::get(m_int32Type, j)},
			                                            "pack_pptr_" + upvalue->Name());
			m_builder.CreateStore(newPtr, packPPtr);
		}

		// Find the next value
		llvm::Value *nextInstance = m_builder.CreateCall(m_getNextClosure, {thisObj}, "next_reloc_obj");
		thisObj->addIncoming(nextInstance, loop);

		// Check if we've reached the end, and if so then break - otherwise, repeat
		llvm::Value *endIsNull = m_builder.CreateICmpEQ(m_nullPointer, nextInstance);
		m_builder.CreateCondBr(endIsNull, next, loop);
	}

	// Continue compiling at the end block
	m_builder.SetInsertPoint(endCase);

	return {};
}

} // namespace wren_llvm_backend

// Wire it together with the public LLVMBackend class
LLVMBackend::~LLVMBackend() {}

std::unique_ptr<LLVMBackend> LLVMBackend::Create() {
	return std::unique_ptr<LLVMBackend>(new wren_llvm_backend::LLVMBackendImpl);
}

#endif // USE_LLVM
