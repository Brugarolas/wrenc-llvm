//
// The handling for currently-edited files. Helper classes are prefixed
// with 'A'.
//
// Created by znix on 27/02/23.
//

#pragma once

#include "tree_sitter/api.h"

#include <memory>
#include <unordered_map>
#include <vector>

class AScope;
struct ALocalVariable;
struct APointQueryResult;
struct AutoCompleteResult;
struct AClassDef;
struct AMethod;

/// Represents a Wren file that is actively being edited. The structure of the
/// file is not compacted to save space, to make changing it easier.
class ActiveFile {
  public:
	void Update(TSTree *tree, const std::string &text);

	/// Find the scope directly enclosing the specified point.
	/// Returns null if no such scope can be found.
	APointQueryResult PointQuery(TSPoint point);

	/// Run auto-completion at a given point.
	AutoCompleteResult AutoComplete(TSPoint point);

	std::string GetNodeText(TSNode node);

  private:
	// FIXME figure out how to store the file contents properly
	std::string m_contents;

	AScope *m_rootScope = nullptr;

	TSTree *m_currentTree = nullptr;

	std::vector<std::unique_ptr<AScope>> m_scopePool;

	std::vector<std::unique_ptr<AClassDef>> m_classPool;

	// The NodeID-to-scope mappings.
	// Not sure if we're supposed to use the node ID or not, but it looks
	// awfully convenient.
	std::unordered_map<const void *, AScope *> m_scopeMappings;

	// Recursively walk and parse a set of 'regular' nodes. These are nodes
	// that don't open up a scope - see the block comment in the implementation
	// file for more information.
	void WalkNodes(TSTreeCursor *cursor, AScope *scope, TSSymbol parentSym, int debugDepth);

	// Build a scope from a suitable node. This is effectively a version of
	// WalkNodes that's called for blocks.
	AScope *BuildScope(TSTreeCursor *cursor, int debugDepth);

	// This is the part of WalkNodes that iterates through all the child nodes.
	void WalkChildren(TSTreeCursor *cursor, AScope *scope, int debugDepth);
};

class AScope {
  public:
	/// The node that defines this scope, such as a stmt_block or a
	/// class definition.
	TSNode node = {};

	/// The scope containing this scope.
	AScope *parent = nullptr;

	/// The scopes contained within this scope.
	std::vector<AScope *> subScopes;

	/// The local variables contained in this scope.
	std::unordered_map<std::string, ALocalVariable> locals;

	/// If this scope represents a class definition, this indicates which
	/// one it is.
	AClassDef *classDef = nullptr;
};

struct ALocalVariable {
  public:
	/// If this variable is the one generated by a class definition, this
	/// points to the involved class.
	AClassDef *classDef = nullptr;
};

struct AClassDef {
  public:
	std::string name;

	std::vector<AMethod> methods;
};

struct AMethod {
  public:
	std::string name;
	TSNode node = {};
};

struct APointQueryResult {
	// The list of nodes, starting from the root node and getting more and
	// more fine-grained.
	std::vector<TSNode> nodes;

	// The scope directly containing the last-level node.
	AScope *enclosingScope = nullptr;

	// Get the nth-from-last node. 0 is equivalent to nodes.back(), 1 is the
	// equivalent of nodes.at(nodes.size()-2), and so on.
	TSNode NodeRev(int positionFromBack) { return nodes.at(nodes.size() - 1 - positionFromBack); }
};

struct AutoCompleteResult {
	struct Entry {
		std::string identifier;
		// TODO type (variable, function call, etc)
	};

	enum Context {
		INVALID,
		VARIABLE_LOAD,
	};

	std::vector<Entry> entries;
	Context context = INVALID;
};
