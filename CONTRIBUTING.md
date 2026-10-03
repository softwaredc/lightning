# Contributing to lightning

Thanks for your interest in improving lightning! This is a small guide to get you started.

## Build from source

```bash
git clone https://github.com/softwaredc/lightning.git
cd lightning
cmake -B build
cmake --build build -j
```

## Run tests

```bash
# Normal tests
cd build && ctest --output-on-failure

# With sanitizers (recommended before submitting)
cmake -B build-san -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -g"
cmake --build build-san -j
cd build-san && ctest --output-on-failure
```

## Code style

- Follow the existing style. Run `clang-format` if unsure.
- 4-space indent, no tabs, 80-column limit.
- Braces on their own line (Allman style).

## Submitting changes

1. Fork the repo, create a branch (`git checkout -b fix/my-bugfix`).
2. Write tests for your change. All tests must pass, including ASan/UBSan.
3. Commit with a clear message. No `--no-verify`.
4. Open a PR. Describe what changed and why.

## Reporting bugs

Open an issue with:

- Minimal reproducer (input bytes, expected vs actual)
- Build config (compiler, flags)
- ASan/UBSan output if applicable

## Areas that need help

- ARM NEON SIMD backend
- HTTP/2 header compression (HPACK) integration
- More language bindings (Ruby, Java, Zig)
- Real-world fuzzing with OSS-Fuzz
