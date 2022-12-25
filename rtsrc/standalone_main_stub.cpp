//
// This is a 'stub' that contains the main function, to actually call into the main Wren function.
// This is only necessary for standalone Wren programmes - when embedding Wren, you're the one writing
// the main() function.
//
// Created by znix on 21/07/22.
//

#include "WrenRuntime.h"
#include "common/common.h"

#include <stdio.h>

typedef Value (*wren_main_func_t)();

// Generated by the QBE backend when a module is marked as main
extern "C" wren_main_func_t wrenStandaloneMainModule;

// Zero it, and let this definition be replaced when we link something else in
__attribute__((weak)) wren_main_func_t wrenStandaloneMainModule = nullptr;

int main(int argc, char **argv) {
	WrenRuntime::Initialise();

	// Initialise and run the main module
	WrenRuntime::Instance().GetOrInitModule((void *)wrenStandaloneMainModule);

	return 0;
}
