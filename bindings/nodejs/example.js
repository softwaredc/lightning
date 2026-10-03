'use strict';

const lightning = require('./lightning');

console.log(`SIMD backend: ${lightning.simdBackend()}`);
console.log();

const reqData = Buffer.from(
  'GET /api/users?id=42 HTTP/1.1\r\n' +
  'Host: example.com\r\n' +
  'User-Agent: lightning-node/1.0\r\n' +
  'Accept: */*\r\n' +
  'Connection: keep-alive\r\n' +
  '\r\n'
);

const req = lightning.parseRequest(reqData);
console.log('=== Request ===');
console.log(`Method:  ${req.method}`);
console.log(`Path:    ${req.path}`);
console.log(`Version: HTTP/1.${req.minorVersion}`);
console.log(`Headers (${req.headers.length}):`);
for (const h of req.headers) {
  console.log(`  ${h.name}: ${h.value}`);
}
