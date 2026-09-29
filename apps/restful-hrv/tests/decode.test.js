// Tests how the settings page reads records out of the link, including the
// Measure now flag in the top bit of the artefact count.
//
//   node tests/decode.test.js
'use strict';
const fs = require('fs');
const path = require('path');
const assert = require('assert');

const html = fs.readFileSync(path.join(__dirname, '..', '..', '..', 'docs', 'restful-hrv', 'index.html'), 'utf8');
const grab = name => {
  const a = html.indexOf('  function ' + name + '(');
  return html.slice(a, html.indexOf('\n  }\n', a) + 4);
};
const P = new Function(grab('fromBase64Url') + grab('decodeRecords') + grab('toCsv') +
                       '; return {fromBase64Url, decodeRecords, toCsv};')();

const rec = (t, v, r) => [t & 255, (t >>> 8) & 255, (t >>> 16) & 255, (t >>> 24) & 255, v & 255, v >> 8, r & 255, (r >> 8) & 255];

const out = P.decodeRecords([].concat(rec(1790000000, 38, 2), rec(1790000100, 41, 0x8000 | 3),
                                      rec(1790000200, 30, 300), rec(1790000300, 29, 0x8000 | 300)), 8);
assert.deepStrictEqual(out.map(r => [r.v, r.r, r.manual]), [[38, 2, false], [41, 3, true], [30, 300, false], [29, 300, true]]);

// Timestamps past 2038 stay positive.
assert.strictEqual(P.decodeRecords(rec(0xF0000000, 1, 0), 8)[0].t, 0xF0000000);

// Old six-byte records: no count, never manual.
const v1 = P.decodeRecords([0, 1, 2, 3, 40, 0], 6);
assert.deepStrictEqual([v1[0].r, v1[0].manual], [null, false]);

// A trailing partial record is ignored, not misread.
assert.strictEqual(P.decodeRecords(rec(1, 2, 3).concat([9, 9, 9]), 8).length, 1);

// base64url round trip, and junk characters are skipped rather than fatal.
const bytes = rec(1790000000, 38, 2);
const b64 = Buffer.from(bytes).toString('base64url');
assert.deepStrictEqual(P.fromBase64Url(b64), bytes);
assert.deepStrictEqual(P.fromBase64Url(b64.slice(0, 3) + '<>"\'' + b64.slice(3)), bytes);

// CSV: one row per measurement with the manual column.
const csv = P.toCsv([{ t: 1790000100, v: 41, r: 3, manual: true }, { t: 1790000000, v: 38, r: null, manual: false }]);
assert.strictEqual(csv, 'timestamp,iso_time,rmssd_ms,artefacts_removed,manual\n' +
  '1790000000,2026-09-21T14:13:20.000Z,38,,0\n1790000100,2026-09-21T14:15:00.000Z,41,3,1\n');

console.log('decode tests passed');
