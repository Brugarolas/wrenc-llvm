#!/usr/bin/env python
# Copied and modified from upstream Wren
# See it's copyright file for more information.

from __future__ import print_function

import os.path
import subprocess
from argparse import ArgumentParser
from collections import defaultdict
import re
from subprocess import Popen, PIPE
import sys
from threading import Timer
from pathlib import Path
import platform
from typing import Optional

# Runs the tests.

parser = ArgumentParser()
parser.add_argument('--suffix', default='')
parser.add_argument('suite', nargs='?')
parser.add_argument('--show-passes', '-p', action='store_true', help='list the tests that pass')
parser.add_argument('--static-output', '-s', action='store_true', help="don't overwrite the status lines")

args = parser.parse_args(sys.argv[1:])

config = args.suffix.lstrip('_d')
is_debug = args.suffix.startswith('_d')

WRENCC_DIR = Path(__file__).parent.parent
WREN_DIR: Path = WRENCC_DIR / "lib" / "wren-main"
WREN_APP: Path = WRENCC_DIR / "build" / f"wrencc{args.suffix}"

WREN_APP_WITH_EXT = WREN_APP
if platform.system() == "Windows":
    WREN_APP_WITH_EXT += ".exe"

if not WREN_APP_WITH_EXT.is_file():
    print("The binary file 'wrencc' was not found, expected it to be at " + str(WREN_APP.absolute()))
    print("In order to run the tests, you need to build the compiler first!")
    sys.exit(1)

# print("Wren Test Directory - " + WREN_DIR)
# print("Wren Test App - " + WREN_APP)

EXPECT_PATTERN = re.compile(r'// expect: ?(.*)')
EXPECT_ERROR_PATTERN = re.compile(r'// expect error(?! line)')
EXPECT_ERROR_LINE_PATTERN = re.compile(r'// expect error line (\d+)')
EXPECT_RUNTIME_ERROR_PATTERN = re.compile(r'// expect (handled )?runtime error: (.+)')
ERROR_PATTERN = re.compile(r'\[.* line (\d+)] Error')
STACK_TRACE_PATTERN = re.compile(r'(?:\[\./)?test/.* line (\d+)] in')
STDIN_PATTERN = re.compile(r'// stdin: (.*)')
SKIP_PATTERN = re.compile(r'// skip: (.*)')
NONTEST_PATTERN = re.compile(r'// nontest')

passed = 0
failed = 0
num_skipped = 0
skipped = defaultdict(int)
expectations = 0


class Test:
    def __init__(self, path):
        self.path = path
        self.output = []
        self.compile_errors = set()
        self.runtime_error_line = 0
        self.runtime_error_message = None
        self.compile_error_expected = False
        self.runtime_error_status_expected = False
        self.input_bytes = None
        self.failures = []

    def parse(self):
        global num_skipped
        global skipped
        global expectations

        input_lines = []
        line_num = 1

        # Note #1: we have unicode tests that require utf-8 decoding.
        # Note #2: python `open` on 3.x modifies contents regarding newlines.
        # To prevent this, we specify newline='' and we don't use the
        # readlines/splitlines/etc family of functions, these
        # employ the universal newlines concept which does this.
        # We have tests that embed \r and \r\n for validation, all of which
        # get manipulated in a not helpful way by these APIs.

        with open(self.path, 'r', encoding="utf-8", newline='', errors='replace') as file:
            data = file.read()
            lines = re.split('\n|\r\n', data)
            for line in lines:
                if len(line) <= 0:
                    line_num += 1
                    continue

                match = EXPECT_PATTERN.search(line)
                if match:
                    self.output.append((match.group(1), line_num))
                    expectations += 1

                match = EXPECT_ERROR_PATTERN.search(line)
                if match:
                    self.compile_errors.add(line_num)

                    self.compile_error_expected = True
                    expectations += 1

                match = EXPECT_ERROR_LINE_PATTERN.search(line)
                if match:
                    self.compile_errors.add(int(match.group(1)))

                    self.compile_error_expected = True
                    expectations += 1

                match = EXPECT_RUNTIME_ERROR_PATTERN.search(line)
                if match:
                    self.runtime_error_line = line_num
                    self.runtime_error_message = match.group(2)
                    # If the runtime error isn't handled, it should exit with WREN_EX_SOFTWARE.
                    if match.group(1) != "handled ":
                        self.runtime_error_status_expected = True
                    expectations += 1

                match = STDIN_PATTERN.search(line)
                if match:
                    input_lines.append(match.group(1))

                match = SKIP_PATTERN.search(line)
                if match:
                    num_skipped += 1
                    skipped[match.group(1)] += 1
                    return False

                # Not a test file at all, so ignore it.
                match = NONTEST_PATTERN.search(line)
                if match:
                    return False

                line_num += 1

        # If any input is fed to the test in stdin, concatenate it into one string.
        if input_lines:
            self.input_bytes = "\n".join(input_lines).encode("utf-8")

        # If we got here, it's a valid test.
        return True

    def run(self, compiler_path: Path, type: str):
        testprog_path = self.compile(compiler_path)

        if testprog_path is None:
            # TODO handle errors like before
            return

        # Invoke wren and run the test.
        proc = Popen([testprog_path], stdin=PIPE, stdout=PIPE, stderr=PIPE)

        # If a test takes longer than five seconds, kill it.
        #
        # This is mainly useful for running the tests while stress testing the GC,
        # which can make a few pathological tests much slower.
        timed_out = [False]

        def kill_process(p):
            timed_out[0] = True
            p.kill()

        timer = Timer(5, kill_process, [proc])

        try:
            timer.start()
            out, err = proc.communicate(self.input_bytes)

            if timed_out[0]:
                self.failed("Timed out.")
            else:
                self.validate(type == "example", proc.returncode, out, err)
        finally:
            timer.cancel()

    def compile(self, cc: Path) -> Optional[Path]:
        # TODO auto-delete this
        dest_file = Path("/tmp/wrencc-test")

        # Run the compiler
        proc = subprocess.Popen(
            stdin=subprocess.DEVNULL,
            stdout=PIPE,
            stderr=PIPE,
            encoding="utf-8",
            args=[
                cc,
                "--module=test",
                "-o", dest_file,
                self.path,
            ],
        )
        out, err = proc.communicate(None)

        # TODO handle with the standard compile error handling stuff
        if out:
            print("Compiler output: " + out.strip())
        if err:
            print("Compiler error: " + err.strip())

        if proc.returncode != 0:
            self.failed("Compiler exited with non-zero return code")
            return None

        return dest_file

    def validate(self, is_example, exit_code, out, err):
        if self.compile_errors and self.runtime_error_message:
            self.failed("Test error: Cannot expect both compile and runtime errors.")
            return

        try:
            out = out.decode("utf-8").replace('\r\n', '\n')
            err = err.decode("utf-8").replace('\r\n', '\n')
        except:
            self.failed('Error decoding output.')

        error_lines = err.split('\n')

        # Validate that an expected runtime error occurred.
        if self.runtime_error_message:
            self.validate_runtime_error(error_lines)
        else:
            self.validate_compile_errors(error_lines)

        self.validate_exit_code(exit_code, error_lines)

        # Ignore output from examples.
        if is_example: return

        self.validate_output(out)

    def validate_runtime_error(self, error_lines):
        if len(error_lines) < 2:
            self.failed('Expected runtime error "{0}" and got none.',
                        self.runtime_error_message)
            return

        # Skip any compile errors. This can happen if there is a compile error in
        # a module loaded by the module being tested.
        line = 0
        while ERROR_PATTERN.search(error_lines[line]):
            line += 1

        if error_lines[line] != self.runtime_error_message:
            self.failed('Expected runtime error "{0}" and got:',
                        self.runtime_error_message)
            self.failed(error_lines[line])

        # Make sure the stack trace has the right line. Skip over any lines that
        # come from builtin libraries.
        match = False
        stack_lines = error_lines[line + 1:]
        for stack_line in stack_lines:
            match = STACK_TRACE_PATTERN.search(stack_line)
            if match: break

        if not match:
            self.failed('Expected stack trace and got:')
            for stack_line in stack_lines:
                self.failed(stack_line)
        else:
            stack_line = int(match.group(1))
            if stack_line != self.runtime_error_line:
                self.failed('Expected runtime error on line {0} but was on line {1}.',
                            self.runtime_error_line, stack_line)

    def validate_compile_errors(self, error_lines):
        # Validate that every compile error was expected.
        found_errors = set()
        for line in error_lines:
            match = ERROR_PATTERN.search(line)
            if match:
                error_line = float(match.group(1))
                if error_line in self.compile_errors:
                    found_errors.add(error_line)
                else:
                    self.failed('Unexpected error:')
                    self.failed(line)
            elif line != '':
                self.failed('Unexpected output on stderr:')
                self.failed(line)

        # Validate that every expected error occurred.
        for line in self.compile_errors - found_errors:
            self.failed('Missing expected error on line {0}.', line)

    def validate_exit_code(self, exit_code, error_lines):
        error_status = exit_code != 0
        if error_status == self.runtime_error_status_expected:
            return

        self.failed('Expecting non-zero return code? {0}. Got {1}. Stderr:',
                    self.runtime_error_status_expected, exit_code)
        self.failures += error_lines

    def validate_output(self, out):
        # Remove the trailing last empty line.
        out_lines = out.split('\n')
        if out_lines[-1] == '':
            del out_lines[-1]

        index = 0
        for line in out_lines:
            if sys.version_info < (3, 0):
                line = line.encode('utf-8')

            if index >= len(self.output):
                self.failed('Got output "{0}" when none was expected.', line)
            elif self.output[index][0] != line:
                self.failed('Expected output "{0}" on line {1} and got "{2}".',
                            self.output[index][0], self.output[index][1], line)
            index += 1

        while index < len(self.output):
            self.failed('Missing expected output "{0}" on line {1}.',
                        self.output[index][0], self.output[index][1])
            index += 1

    def failed(self, message, *args):
        if args:
            message = message.format(*args)
        self.failures.append(message)


def color_text(text, color):
    """Converts text to a string and wraps it in the ANSI escape sequence for
    color, if supported."""

    # No ANSI escapes on Windows.
    if sys.platform == 'win32':
        return str(text)

    return color + str(text) + '\033[0m'


def green(text):
    return color_text(text, '\033[32m')


def pink(text):
    return color_text(text, '\033[91m')


def red(text):
    return color_text(text, '\033[31m')


def yellow(text):
    return color_text(text, '\033[33m')


def walk(path: Path, callback, ignored=None):
    """
    Walks [dir], and executes [callback] on each file unless it is [ignored].
    """

    if not ignored:
        ignored = []
    ignored += [".", ".."]

    for file in [file for file in path.iterdir() if file not in ignored]:
        nfile: Path = path / file
        if nfile.is_dir():
            walk(nfile, callback)
        else:
            callback(nfile)


def print_line(line=None, keep=False):
    erase = not args.static_output
    if erase:
        # Erase the line.
        print('\033[2K', end='')
        # Move the cursor to the beginning.
        print('\r', end='')

    if line:
        print(line, end='')
        sys.stdout.flush()

    if not erase or keep:
        # If we're not going to overwrite the same line, we need to write
        # each message on it's own line.
        print("")


def run_script(app, path: Path, type):
    global passed
    global failed
    global num_skipped

    if os.path.splitext(path)[1] != '.wren':
        return

    rel_path = str(path.relative_to(WREN_DIR))

    # Check if we are just running a subset of the tests.
    if args.suite:
        if not rel_path.startswith(args.suite):
            return

    # Update the status line.
    print_line('({}) Passed: {} Failed: {} Skipped: {} '.format(
        os.path.relpath(app, WREN_DIR), green(passed), red(failed), yellow(num_skipped)))

    # Make a nice short path relative to the working directory.

    # Read the test and parse out the expectations.
    test = Test(path)

    if not test.parse():
        # It's a skipped or non-test file.
        return

    test.run(app, type)

    # Display the results.
    if len(test.failures) == 0:
        passed += 1
        if args.show_passes:
            print_line(green('PASS') + ': ' + rel_path, keep=True)
    else:
        failed += 1
        print_line(red('FAIL') + ': ' + rel_path, keep=True)
        for failure in test.failures:
            print('      ' + pink(failure))
        print('')


def run_test(path, example=False):
    run_script(WREN_APP, path, "test")


def run_api_test(path):
    run_script(WREN_APP, path, "api test")


def run_example(path: Path):
    rel = str(path.relative_to(WREN_DIR))

    # Don't run examples that require user input.
    if "animals" in rel: return
    if "guess_number" in rel: return

    # This one is annoyingly slow.
    if "skynet" in rel: return

    run_script(WREN_APP, path, "example")


def main():
    walk(WREN_DIR / 'test', run_test, ignored=['api', 'benchmark'])
    walk(WREN_DIR / 'test' / 'api', run_api_test)
    walk(WREN_DIR / 'example', run_example)

    print_line()
    if failed == 0:
        print('All ' + green(passed) + ' tests passed (' + str(expectations) +
              ' expectations).')
    else:
        print(green(passed) + ' tests passed. ' + red(failed) + ' tests failed.')

    for key in sorted(skipped.keys()):
        print('Skipped ' + yellow(skipped[key]) + ' tests: ' + key)

    if failed != 0:
        sys.exit(1)


if __name__ == "__main__":
    main()
