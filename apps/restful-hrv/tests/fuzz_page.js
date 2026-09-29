// Opens the settings page with hostile and malformed links and checks that
// nothing runs, nothing is fetched, and the page still renders.
//
// The fragment is written by whoever sends the link, so it is untrusted input.
// Needs Playwright with a Chromium it can launch:
//
//   node tests/fuzz_page.js
'use strict';
const path = require('path');
let chromium;
try {
  ({ chromium } = require('playwright'));
} catch (e) {
  ({ chromium } = require(require('child_process').execSync('npm root -g').toString().trim() + '/playwright'));
}

const PAGE = 'file://' + path.join(__dirname, '..', '..', '..', 'docs', 'restful-hrv', 'index.html');
const rec = (t, v, r) => [t & 255, (t >>> 8) & 255, (t >>> 16) & 255, (t >>> 24) & 255, v & 255, v >> 8, r & 255, (r >> 8) & 255];
const b64 = bytes => Buffer.from(bytes).toString('base64url');
const good = b64([].concat(rec(1790000000, 38, 2), rec(1790003600, 41, 0), rec(1790007200, 30, 1)));
const xss = '<img src=x onerror="window.__pwned=1">';
const statusWith = obj => encodeURIComponent(JSON.stringify(obj));

let seed = 99;
const rnd = () => (seed = (seed * 16807) % 2147483647) / 2147483647;
const randomBytes = n => Array.from({ length: n }, () => Math.floor(rnd() * 256));

const cases = {
  'script in every status field': `v=2&d=${good}&s=` + statusWith({
    lastTickAt: xss, sleepLastSeenAt: xss, restfulLastSeenAt: xss, sleepEvents: xss, hrvEvents: xss,
    hrvEventsMeasuring: xss, hrvZeroEvents: xss, episodes: xss, hrvRequestOk: xss, lastOutcome: xss,
    lastSampleCount: xss, lastRejected: xss }),
  'script as updated': `v=2&updated=${encodeURIComponent(xss)}&d=${good}`,
  'script in d': `v=2&d=${encodeURIComponent(xss)}`,
  'status is not JSON': `v=2&d=${good}&s=%7Bnope`,
  'status is a bad escape': `v=2&d=${good}&s=%E0%A4%A`,
  'status is an array': `v=2&d=${good}&s=` + statusWith([1, 2, 3]),
  'status is a number': `v=2&d=${good}&s=42`,
  'status is null': `v=2&d=${good}&s=null`,
  'unknown version': `v=9&d=${good}`,
  'v1 with odd length': `v=1&d=${b64(randomBytes(13))}`,
  'random bytes': `v=2&d=${b64(randomBytes(4000))}`,
  'timestamps at the extremes': `v=2&d=${b64([].concat(rec(0, 1, 0), rec(0xFFFFFFFF, 65535, 0xFFFF)))}`,
  'huge payload (200 kB)': `v=2&d=${b64(randomBytes(150000))}`,
  'clearing in a browser': `v=2&d=${good}&clearing=1`,
  'empty fragment': '',
  'garbage fragment': '&&&==&x&=&v',
};

(async () => {
  const browser = await chromium.launch();
  let failures = 0;
  for (const [name, fragment] of Object.entries(cases)) {
    for (const inApp of [true, false]) {
      const ctx = await browser.newContext({ viewport: { width: 390, height: 844 } });
      if (inApp) await ctx.addInitScript(() => { window._localStorage = {}; });
      const page = await ctx.newPage();
      const errors = [];
      const requests = [];
      page.on('pageerror', e => errors.push(e.message));
      page.on('dialog', d => { errors.push('dialog: ' + d.message()); d.dismiss(); });
      page.on('request', r => { if (!r.url().startsWith(PAGE)) requests.push(r.url()); });
      await page.goto(PAGE + '#' + fragment);
      await page.waitForTimeout(150);
      const state = await page.evaluate(() => ({
        pwned: !!window.__pwned,
        images: document.querySelectorAll('img').length,
        rendered: document.getElementById('content') !== null && document.body.innerText.length > 0,
      }));
      const bad = state.pwned || state.images || !state.rendered || errors.length || requests.length;
      if (bad) failures++;
      console.log(`${bad ? 'FAIL' : 'ok  '} ${name}${inApp ? ' (in app)' : ''}` +
                  (bad ? ' ' + JSON.stringify({ state, errors, requests }) : ''));
      await ctx.close();
    }
  }
  await browser.close();
  console.log(failures ? failures + ' failures' : 'no failures');
  process.exit(failures ? 1 : 0);
})();
