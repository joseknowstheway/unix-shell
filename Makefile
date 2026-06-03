# Makefile — Unix Shell (mysh)
#
# Targets:
#   make          build the optimized binary ./mysh
#   make debug    build ./mysh-debug with -g + UndefinedBehaviorSanitizer
#   make leaks    build the debug binary and run it under Apple's `leaks` tool
#   make clean    remove all build artifacts
#
# Release and debug use SEPARATE output names on purpose: make decides what to
# rebuild by comparing file timestamps, not compiler flags. A single shared
# output name would let a stale debug binary masquerade as a release build (and
# vice versa) whenever you switched targets.
#
# This project has zero external dependencies — only the C standard library and
# POSIX — so there are no -I/-L/-l flags to chase. $(wildcard src/*.c) means new
# source files (parser.c today, executor.c, builtins.c, jobs.c... later) are
# picked up automatically with no edit to this file.
#
# WHY NO AddressSanitizer? The natural choice for a C project is
# -fsanitize=address, but on macOS 26 (Darwin 25) ASan hangs at startup inside
# FindDynamicShadowStart — the routine that mmaps its shadow-memory region —
# spinning at ~100% CPU before main() ever runs. That's a known incompatibility
# between Apple clang's bundled ASan runtime and the current OS, not a bug in our
# code. So the debug build uses UBSan (which works), and memory-leak checking is
# done with Apple's native `leaks` tool via `make leaks`.

CC           = cc
CFLAGS       = -Wall -Wextra -std=c11
SRC          = $(wildcard src/*.c)
TARGET       = mysh
DEBUG_TARGET = mysh-debug

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

# Debug build: symbols + UndefinedBehaviorSanitizer (signed overflow, bad
# shifts, null-pointer use, out-of-bounds in many cases). Written to its own
# binary so it never overwrites the release build.
debug: $(DEBUG_TARGET)

$(DEBUG_TARGET): $(SRC)
	$(CC) $(CFLAGS) -g -fsanitize=undefined -o $(DEBUG_TARGET) $(SRC)

# Leak check: drive the debug binary with a scripted session and let `leaks`
# inspect the heap at exit. A clean run ends with "0 leaks for 0 total leaked
# bytes". Override the script with:  make leaks LEAK_SCRIPT='cmd1\ncmd2\nexit\n'
LEAK_SCRIPT ?= pwd\necho leak-check\nls\nexit\n
leaks: $(DEBUG_TARGET)
	printf '$(LEAK_SCRIPT)' | leaks --atExit -- ./$(DEBUG_TARGET)

clean:
	rm -f $(TARGET) $(DEBUG_TARGET)
	rm -rf $(TARGET).dSYM $(DEBUG_TARGET).dSYM

.PHONY: debug leaks clean
