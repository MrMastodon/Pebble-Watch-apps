// Tests the settings page's statistics against tests/ref_stats.py.
//
// The functions under test are cut out of docs/restful-hrv/index.html (between
// its stats:begin and stats:end markers) and run as they are. Must run in the
// Europe/Oslo time zone, since nights are defined in local time:
//
//   TZ=Europe/Oslo node tests/page_stats.test.js
'use strict';
const fs = require('fs');
const path = require('path');
const assert = require('assert');
const { execFileSync } = require('child_process');

if (process.env.TZ !== 'Europe/Oslo') {
  console.error('Run with TZ=Europe/Oslo');
  process.exit(2);
}

const PAGE = path.join(__dirname, '..', '..', '..', 'docs', 'restful-hrv', 'index.html');
const html = fs.readFileSync(PAGE, 'utf8');
const block = html.slice(html.indexOf('/* stats:begin'), html.indexOf('/* stats:end'));
const S = new Function(block + '; return {median, mean, stdev, rollingMean, wakeDateOf, groupNights, summarize};')();

const reference = recs => JSON.parse(execFileSync('python3', [path.join(__dirname, 'ref_stats.py')],
                                                   { input: JSON.stringify(recs) }).toString());
const close = (a, b, msg) => assert.ok(a === b || Math.abs(a - b) < 1e-9, `${msg}: ${a} vs ${b}`);
const iso = d => `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`;
const at = (y, mo, d, h, mi) => Math.floor(new Date(y, mo - 1, d, h, mi).getTime() / 1000);

// Deterministic synthetic nights: 1-10 measurements each, some just before
// midnight, some on the morning after, a few manual readings in the afternoon.
function synthetic(nights, seed) {
  let s = seed;
  const rnd = () => (s = (s * 16807) % 2147483647) / 2147483647;
  const out = [];
  for (let n = 0; n < nights; n++) {
    const day = new Date(2026, 8, 1 + n);
    const k = 1 + Math.floor(rnd() * 10);
    for (let j = 0; j < k; j++) {
      const d = new Date(day);
      d.setHours(j === 0 && rnd() < 0.3 ? -1 : j, Math.floor(rnd() * 60));   // -1: 23:xx the evening before
      out.push({ t: Math.floor(d / 1000), v: Math.round(15 + rnd() * 60), r: Math.floor(rnd() * 4), manual: false });
    }
    if (rnd() < 0.2) {
      const d = new Date(day); d.setHours(15, 5);
      out.push({ t: Math.floor(d / 1000), v: 150, r: 0, manual: true });
    }
  }
  return out;
}

let n = 0;
const t = (name, f) => { f(); n++; console.log('ok', name); };

for (const [count, seed] of [[1, 3], [2, 5], [3, 7], [4, 11], [5, 13], [9, 17], [10, 19], [40, 23]]) {
  t(`${count} synthetic nights match the reference`, () => {
    const recs = synthetic(count, seed);
    const want = reference(recs);
    const nights = S.groupNights(recs);
    assert.deepStrictEqual(nights.map(x => iso(x.date)), want.keys);
    assert.deepStrictEqual(nights.map(x => x.value), want.medians);
    assert.deepStrictEqual(nights.map(x => x.measurements.length), want.counts);
    const s = S.summarize(nights);
    for (const k of ['baseline', 'delta', 'avgShort', 'avgLong', 'sdLong', 'cv']) {
      if (want[k] === null) assert.strictEqual(s[k], null, k);
      else close(s[k], want[k], k);
    }
  });
}

t('noon-to-noon boundaries, month and year roll-over, both DST changes', () => {
  const cases = [[[2026, 9, 21, 23, 53], '2026-09-22'], [[2026, 9, 22, 11, 59], '2026-09-22'],
                 [[2026, 9, 22, 12, 0], '2026-09-23'], [[2026, 9, 22, 0, 0], '2026-09-22'],
                 [[2026, 12, 31, 22, 0], '2027-01-01'], [[2026, 3, 28, 23, 30], '2026-03-29'],
                 [[2026, 3, 29, 11, 30], '2026-03-29'], [[2026, 10, 24, 12, 30], '2026-10-25'],
                 [[2026, 10, 25, 11, 59], '2026-10-25']];
  for (const [args, want] of cases) assert.strictEqual(iso(S.wakeDateOf(at(...args))), want, args.join(' '));
});

t('minimum nights before each figure is shown', () => {
  const mk = vals => vals.map((v, i) => ({ t: at(2026, 8, 1 + i, 3, 0), v, r: 0 }));
  let s = S.summarize(S.groupNights(mk([30, 40])));
  assert.strictEqual(s.avgShort, null); assert.strictEqual(s.baseline, null); assert.strictEqual(s.cv, null);
  s = S.summarize(S.groupNights(mk([30, 40, 50])));
  assert.strictEqual(s.avgShort, 40); assert.strictEqual(s.baseline, null);
  s = S.summarize(S.groupNights(mk([30, 40, 50, 20])));
  assert.strictEqual(s.baseline, 40); assert.strictEqual(s.delta, -20); assert.strictEqual(s.cv, null);
  s = S.summarize(S.groupNights(mk([1, 2, 3, 4, 5, 6, 7, 8, 9])));
  assert.strictEqual(s.sdLong, null);
  s = S.summarize(S.groupNights(mk([1, 2, 3, 4, 5, 6, 7, 8, 9, 10])));
  close(s.sdLong, 3.0276503540974917, 'sd 1..10');
});

t('the middle measurements behind each median', () => {
  const mk = (d, vals) => vals.map((v, i) => ({ t: at(2026, 9, d, 1 + i, 0), v, r: 0 }));
  const nights = S.groupNights(mk(25, [56, 38, 31, 33]).concat(mk(27, [27, 19, 25]), mk(20, [30, 40, 40, 50])));
  const by = Object.fromEntries(nights.map(x => [x.date.getDate(), x]));
  assert.strictEqual(by[25].value, 35.5); assert.deepStrictEqual(by[25].middle.map(r => r.v), [33, 38]);
  assert.deepStrictEqual(by[27].middle.map(r => r.v), [25]);
  assert.deepStrictEqual(by[20].middle.map(r => new Date(r.t * 1000).getHours()), [2, 3]);   // ties broken by time
  nights.forEach(x => x.middle.forEach(r => assert.ok(x.measurements.includes(r))));
});

t('manual readings never reach a night', () => {
  const only = [{ t: at(2026, 9, 21, 15, 0), v: 200, r: 0, manual: true }];
  assert.strictEqual(S.groupNights(only).length, 0);
  assert.strictEqual(S.summarize([]).last, null);
});

t('v1 records carry no artefact count', () => {
  assert.strictEqual(S.groupNights([{ t: at(2026, 9, 1, 3, 0), v: 30, r: null }])[0].rejected, null);
});

console.log(n + ' tests passed');
