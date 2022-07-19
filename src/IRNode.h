//
// Created by znix on 10/07/2022.
//

#pragma once

#include "CcValue.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Declarations
class IRExpr;
class IRStmt;
class LocalVariable; // From Scope.h
class FieldVariable; // From SymbolTable.h
class Signature;     // From CompContext.h
class Compiler;      // From wren_compiler.cpp
class ClassInfo;     // From ClassInfo.h
class StmtUpvalueImport;

// //////////////////// //
// //// INTERFACES //// //
// //////////////////// //

class VarDecl {
  public:
	// Describes where a variable is declared.
	enum ScopeType {
		// A local variable in the current function.
		SCOPE_LOCAL,

		// A local variable declared in an enclosing function.
		SCOPE_UPVALUE,

		// A top-level module variable.
		SCOPE_MODULE
	};

	virtual ~VarDecl();
	virtual std::string Name() const = 0;
	virtual ScopeType Scope() const = 0;
};

// //////////////////// //
// //// TOP-LEVEL  //// //
// //////////////////// //

class IRNode {
  public:
	virtual ~IRNode();
};

class IRFn : public IRNode {
  public:
	std::vector<LocalVariable *> locals; // Locals may have duplicate names from different scopes, hence vector not map
	std::unordered_map<VarDecl *, StmtUpvalueImport *> upvalues;

	// A list of all the upvalue imports that haven't been placed in the AST tree, and will be placed later.
	std::vector<StmtUpvalueImport *> unInsertedImports;

	// Locals used as temporaries by the compiler, which aren't checked for name conflicts.
	std::vector<LocalVariable *> temporaries;

	// The arity, or number of arguments, of the function/method (not including the receiver)
	int arity = 0;

	// The thing that gets run when this function is called
	IRStmt *body = nullptr;
};

class IRClass : public IRNode {
  public:
	std::unique_ptr<ClassInfo> info;
};

/// Global variable declaration
class IRGlobalDecl : public IRNode, public VarDecl {
  public:
	std::string name;
	std::string Name() const override;
	ScopeType Scope() const override;

	/// If this variable hasn't been properly declared (eg it's used in a method, which is valid as it could
	/// be declared later in the file in the global scope) then this is set to the line number of the first
	/// line where it was used.
	std::optional<int> undeclaredLineUsed;
};

/// Represents importing a given module. This forces the given module to be parsed and compiled.
/// Variables we import are bound to 'proxies': module-level variables are created, and when the
/// import is evaluated the proxies are set to the appropriate value from the imported module.
/// Note that it's legal to observe an imported variable before the import occurs:
///   class Foo {
///     static thing() {
///       System.print(Bar)
///     }
///   }
///   Foo.thing()
///   import "test2.wren" for Bar
/// In this case, the programme will print "null" since Bar hasn't been imported yet, and proxies
/// easily let us mirror that behaviour. When the "import" directive is actually executed it produces
/// a StmtLoadModule node which runs the module, if it hasn't already been loaded, and then sets
/// up the proxies.
///
/// There are also be cases where it's possible to (during our Wren 'linking' step, not the system
/// linker) recognise that a variable always has a specific value and optimise based on that, eg:
///  module1.wren:
///   var PI = 3.14
///   class A {
///     static fancyPrint(value) {
///       print(value)
///     }
///   }
///  module2.wren:
///   import "module1" for PI, A
///   A.fancyPrint(PI)
/// In this case it should be possible to compile down to a single print(3.14) call. For a variable
/// to be optimised like this, the following must be true:
/// * The variable is defined in module1 and never modified
/// * The variable must be imported to module2 before it's used
/// * We still have to follow import loop rules (though we can probably slack on those for now)
class IRImport : public IRNode {
  public:
	std::string moduleName;
};

// //////////////////// //
// //// STATEMENTS //// //
// //////////////////// //

/**
 * Represents an action that can be executed at runtime.
 */
class IRStmt : public IRNode {};

/// Assign a value to a local or global variable
class StmtAssign : public IRStmt {
  public:
	StmtAssign() {}
	StmtAssign(VarDecl *var, IRExpr *expr) : var(var), expr(expr) {}

	VarDecl *var = nullptr;
	IRExpr *expr = nullptr;
};

/// Assign a value to an object's fields
class StmtFieldAssign : public IRStmt {
  public:
	StmtFieldAssign() {}
	StmtFieldAssign(FieldVariable *var, IRExpr *object, IRExpr *value) : var(var), object(object), value(value) {}

	FieldVariable *var = nullptr;
	IRExpr *object = nullptr;
	IRExpr *value = nullptr;
};

/**
 * Reference a variable from the enclosing function. Only after this statement
 * is executed may the upvalue be accessed.
 *
 * If the function returns without this statement ever executing then the
 * variable in the outer function is never referenced, saving an allocation. Thus
 * this should be done as late as possible.
 *
 * This is inserted into the IR tree in a post-parse pass, put it in IRFn's
 * unInsertedImports array during parsing.
 */
class StmtUpvalueImport : public IRStmt, public VarDecl {
  public:
	StmtUpvalueImport(VarDecl *parent) : parent(parent) {}

	std::string Name() const override;
	ScopeType Scope() const override;

	/// The variable this upvalue references. Must either be a local variable or another upvalue import.
	VarDecl *parent = nullptr;
};

/// Statement that evaluates an expression and throws away the result. This is an adapter of sorts for IRExpr-s.
class StmtEvalAndIgnore : public IRStmt {
  public:
	StmtEvalAndIgnore(IRExpr *expr) : expr(expr) {}

	IRExpr *expr = nullptr;
};

/// A group of statements. Mainly for returning multiple statements as a single pointer.
class StmtBlock : public IRStmt {
  public:
	/// Adds a statement, doing nothing if it's nullptr.
	void Add(IRStmt *stmt);

	/// Wraps an expression in an StmtEvalAndIgnore, doing nothing if it's nullptr.
	void Add(Compiler *forAlloc, IRExpr *expr);

	std::vector<IRStmt *> statements;
};

/// Not really a statement, this designates a point the jump instruction can jump to
class StmtLabel : public IRStmt {};

/// Jump to a label, possibly conditionally.
class StmtJump : public IRStmt {
  public:
	StmtJump(StmtLabel *target, IRExpr *condition) : target(target), condition(condition) {}
	StmtJump() = default;

	StmtLabel *target = nullptr;
	IRExpr *condition = nullptr; /// Unconditional if nullptr. Otherwise, if it evaluates to null or false, won't jump.
	bool looping = false; /// If this jump is part of a loop, this is true. Otherwise the jump MUST only go 'forwards'.
};

class StmtReturn : public IRStmt {
  public:
	explicit StmtReturn(IRExpr *value) : value(value) {}

	IRExpr *value = nullptr;
};

/// Forces a module's main function to be run. See [IRImport].
class StmtLoadModule : public IRStmt {
  public:
	struct VarImport {
		std::string name; // Name in the module we're importing from
		VarDecl *bindTo = nullptr;
	};

	// The import this load triggers
	IRImport *import = nullptr;

	// The variables to import by name
	std::vector<VarImport> variables;
};

// //////////////////// //
// //// EXPRESSIONS /// //
// //////////////////// //

class IRExpr : public IRNode {};

class ExprConst : public IRExpr {
  public:
	ExprConst();
	ExprConst(CcValue value);

	CcValue value;
};

class ExprLoad : public IRExpr {
  public:
	ExprLoad() = default;
	ExprLoad(VarDecl *var) : var(var) {}

	VarDecl *var;
};

/// Read a value from an object's fields
class ExprFieldLoad : public IRExpr {
  public:
	ExprFieldLoad() {}
	ExprFieldLoad(FieldVariable *var, IRExpr *object) : var(var), object(object) {}

	FieldVariable *var = nullptr;
	IRExpr *object = nullptr;
};

/// Either a function or a method call, depending on whether the receiver is null or not
class ExprFuncCall : public IRExpr {
  public:
	Signature *signature = nullptr; /// The signature of the method to call. MUST be unique-ified by CompContext
	std::vector<IRExpr *> args;     /// The list of arguments to pass, must match the function's arity at runtime
	IRExpr *receiver = nullptr; /// Object the method will be called on. Null is valid and indicates a function call.
	bool super = false;         /// Should call the parent class's method? Only allowed where receiver==this
};

/// Create a closure over a function, binding any upvalues. This is used even when there are no upvalues, and
/// if optimisations are performed on that it won't be during parsing.
class ExprClosure : public IRExpr {
  public:
	IRFn *func = nullptr;
};

/// Returns the 'this' value.
class ExprLoadReceiver : public IRExpr {};

/// Run a collection of statements to initialise a temporary variable, which is then
/// used as the result of this expression.
/// This is to be used for things like list initialisers and as such MUST NOT return, break
/// or jump outside of itself (jumps between points inside the block are fine though, eg if statements).
/// After parsing, these are all removed and placed directly ahead of the statement they're used in.
class ExprRunStatements : public IRExpr {
  public:
	IRStmt *statement = nullptr;
	LocalVariable *temporary = nullptr;
};

/// Logical inverse, yields ¬input
class ExprLogicalNot : public IRExpr {
  public:
	ExprLogicalNot(IRExpr *input) : input(input) {}
	IRExpr *input = nullptr;
};

/// Allocates the memory for a new object. If this is a foreign object, the foreign allocation method
/// is also called.
class ExprAllocateInstanceMemory : public IRExpr {
  public:
	IRClass *target = nullptr;
};
