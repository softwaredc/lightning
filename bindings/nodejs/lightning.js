'use strict';

const path = require('path');
const fs = require('fs');

// ---------------------------------------------------------------------------
// Load native library via FFI (using ffi-napi or a minimal ctypes approach)
// ---------------------------------------------------------------------------

let libPath = process.env.LIGHTNING_LIB;
if (!libPath || !fs.existsSync(libPath)) {
  // search relative to project root
  const root = path.resolve(__dirname, '..', '..', '..');
  const candidates = [
    path.join(root, 'build', 'liblightning.so'),
    path.join(root, 'build', 'liblightning.dylib'),
    path.join(root, 'build', 'liblightning.dll'),
    '/usr/lib/liblightning.so',
    '/usr/local/lib/liblightning.so',
  ];
  for (const c of candidates) {
    if (fs.existsSync(c)) { libPath = c; break; }
  }
}

if (!libPath) {
  throw new Error(
    'liblightning not found. Build it first: cmake -B build && cmake --build build\n' +
    'Or set LIGHTNING_LIB=/path/to/liblightning.so'
  );
}

// Use ffi-napi for FFI
let ffi;
try {
  ffi = require('ffi-napi');
} catch (e) {
  throw new Error(
    'ffi-napi is required for the Node.js binding. Install it:\n' +
    '  npm install ffi-napi'
  );
}

const lib = ffi.Library(libPath, {
  lht_parse_request: ['int', [
    'char *', 'size_t',
    'char **', 'size_t *',
    'char **', 'size_t *',
    'int *',
    'void *', 'size_t *',
  ]],
  lht_parse_response: ['int', [
    'char *', 'size_t',
    'int *',
    'char **', 'size_t *',
    'int *',
    'void *', 'size_t *',
  ]],
  lht_simd_backend: ['string', []],
  lht_has_sse42: ['int', []],
  lht_has_avx2: ['int', []],
});

// Header struct: { char *name; size_t name_len; char *value; size_t value_len; }
const HEADER_SIZE = 8 + 8 + 8 + 8; // 4 pointers/sizes on 64-bit
const MAX_HEADERS = 256;

function refAlloc(type, size) {
  return Buffer.alloc(size);
}

function parseRequest(buf) {
  const headerBuf = Buffer.alloc(HEADER_SIZE * MAX_HEADERS);
  const numHeaders = refAlloc('size_t', 8);
  numHeaders.writeBigUInt64LE(BigInt(MAX_HEADERS), 0);

  const method = Buffer.alloc(8);
  const methodLen = Buffer.alloc(8);
  const url = Buffer.alloc(8);
  const urlLen = Buffer.alloc(8);
  const minor = Buffer.alloc(4);
  const input = Buffer.from(buf);

  const ret = lib.lht_parse_request(
    input, input.length,
    method, methodLen,
    url, urlLen,
    minor,
    headerBuf, numHeaders
  );

  if (ret < 0) {
    const msgs = {-1:'need more data', -2:'malformed input', -3:'too many headers', -4:'invalid argument'};
    throw new Error(`lightning error ${ret}: ${msgs[ret] || 'unknown'}`);
  }

  const mLen = Number(methodLen.readBigUInt64LE(0));
  const mPtr = Number(method.readBigUInt64LE(0));
  const pLen = Number(urlLen.readBigUInt64LE(0));
  const pPtr = Number(url.readBigUInt64LE(0));

  const methodStr = Buffer.from(mPtr, mLen).toString('latin1');
  const pathStr = Buffer.from(pPtr, pLen).toString('latin1');

  const count = Number(numHeaders.readBigUInt64LE(0));
  const headers = [];
  for (let i = 0; i < count; i++) {
    const off = i * HEADER_SIZE;
    const nPtr = Number(headerBuf.readBigUInt64LE(off));
    const nLen = Number(headerBuf.readBigUInt64LE(off + 8));
    const vPtr = Number(headerBuf.readBigUInt64LE(off + 16));
    const vLen = Number(headerBuf.readBigUInt64LE(off + 24));
    headers.push({
      name: Buffer.from(nPtr, nLen).toString('latin1'),
      value: Buffer.from(vPtr, vLen).toString('latin1'),
    });
  }

  return {
    method: methodStr,
    path: pathStr,
    minorVersion: minor.readInt32LE(0),
    headers,
    bodyOffset: ret,
  };
}

function simdBackend() {
  return lib.lht_simd_backend();
}

module.exports = { parseRequest, simdBackend };
