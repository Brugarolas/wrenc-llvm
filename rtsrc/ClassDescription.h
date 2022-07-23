//
// Created by znix on 22/07/22.
//

#pragma once

#include <stdint.h>
#include <string>
#include <vector>

class ClassDescription {
  public:
	enum class Command {
		END = 0,
		ADD_METHOD,
	};

	static constexpr uint32_t FLAG_NONE = 0;
	static constexpr uint32_t FLAG_STATIC = 1 << 0;

	struct MethodDecl {
		std::string name;
		void *func;
		bool isStatic;
	};

	void Parse(uint8_t *data);

	std::vector<MethodDecl> methods;
};