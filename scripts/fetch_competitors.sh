#!/usr/bin/env bash
# Fetch competitor HTTP parsers for benchmarking.
# Usage: ./fetch_competitors.sh
set -euo pipefail

DEPS="$(dirname "$0")/../deps"
mkdir -p "$DEPS"
cd "$DEPS"

echo "==> Fetching picohttpparser (h2o/picohttpparser)..."
if [ ! -f picohttpparser.h ]; then
    curl -fsSL -o picohttpparser.h \
        https://raw.githubusercontent.com/h2o/picohttpparser/master/picohttpparser.h
    curl -fsSL -o picohttpparser.c \
        https://raw.githubusercontent.com/h2o/picohttpparser/master/picohttpparser.c
fi

echo "==> Fetching http_parser (nodejs/http-parser)..."
if [ ! -f http_parser.h ]; then
    curl -fsSL -o http_parser.h \
        https://raw.githubusercontent.com/nodejs/http-parser/main/http_parser.h
    curl -fsSL -o http_parser.c \
        https://raw.githubusercontent.com/nodejs/http-parser/main/http_parser.c
fi

echo "==> Fetching llhttp (nodejs/llhttp) release sources..."
if [ ! -f llhttp.h ]; then
    # llhttp uses a release tarball with single-header-style amalgamation
    curl -fsSL -o llhttp.tar.gz \
        https://github.com/nodejs/llhttp/archive/refs/tags/release/v9.2.1.tar.gz
    tar xzf llhttp.tar.gz --strip-components=1 \
        llhttp-release-v9.2.1/src/llhttp.h \
        llhttp-release-v9.2.1/src/llhttp.c \
        llhttp-release-v9.2.1/src/api.c \
        llhttp-release-v9.2.1/src/helpers.c \
        2>/dev/null || {
        # fallback: try the build/ directory layout
        tar xzf llhttp.tar.gz
        cp llhttp-release-v9.2.1/build/llhttp.h . 2>/dev/null || true
        cp llhttp-release-v9.2.1/build/c/llhttp.c . 2>/dev/null || true
        cp llhttp-release-v9.2.1/src/*.c . 2>/dev/null || true
        cp llhttp-release-v9.2.1/src/llhttp.h . 2>/dev/null || true
        rm -rf llhttp-release-v9.2.1
    }
    rm -f llhttp.tar.gz llhttp-release-v9.2.1 -rf
fi

echo "==> Done. Fetched:"
ls -1 *.h *.c 2>/dev/null || true
