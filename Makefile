# lightning - simple Makefile wrapper around CMake
# Usage: make [test|bench|clean|sanitize]

BUILD  ?= build
CC     ?= cc
CFLAGS ?= -O2

.PHONY: all test bench clean sanitize examples

all: $(BUILD)/Makefile
	@cmake --build $(BUILD) -j

$(BUILD)/Makefile:
	@cmake -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release

test: $(BUILD)/Makefile
	@cmake --build $(BUILD) -j
	@cd $(BUILD) && ctest --output-on-failure

bench: $(BUILD)/Makefile
	@cmake --build $(BUILD) -j
	@./$(BUILD)/benchmarks/bench_parser

sanitize:
	@cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DLHT_SANITIZE=ON
	@cmake --build build-sanitize -j
	@cd build-sanitize && ctest --output-on-failure

examples: $(BUILD)/Makefile
	@cmake --build $(BUILD) -j --target lightning_examples

clean:
	@rm -rf $(BUILD) build-sanitize
