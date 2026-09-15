// The phone side of Restful HRV.
//
// The background worker takes the measurements, but background workers have no
// AppMessage, so the only moment the phone can learn about them is while the
// watchapp itself is open. The watchapp pushes its whole history then; this
// file caches it, and hands it to the settings page when that is opened.
//
// Nothing here talks to a server. The settings page is a static file and the
// measurements travel to it in the URL fragment, which browsers never send to
// the host - so the numbers stay on the phone.
//
// Ordering and error handling in this file are load-bearing rather than
// fussiness. Tapping the gear icon starts this script for configuration and
// then waits for Pebble.openURL(); if anything throws first, the phone sits on
// "Loading watch app" forever with nothing to say why. So the configuration
// handler is registered before anything else can fail, it never depends on the
// rest working, and every other entry point swallows its own errors.

var CONFIG_URL = 'https://mrmastodon.github.io/Pebble-Watch-apps/restful-hrv/';

var STORAGE_RECORDS = 'hrvRecords';
var STORAGE_UPDATED = 'hrvUpdatedAt';

// Matches HrvRecord in src/common/hrv_common.h: a 4-byte little-endian UTC
// timestamp followed by a 2-byte little-endian RMSSD in milliseconds.
var RECORD_BYTES = 6;

function log(message) {
  // Visible in `pebble logs --phone <ip>`, which is the only way to see what
  // this script is doing on a real phone.
  try {
    console.log('Restful HRV: ' + message);
  } catch (e) { /* nothing sensible to do if even logging fails */ }
}

function decodeRecords(bytes) {
  var records = [];
  for (var i = 0; i + RECORD_BYTES <= bytes.length; i += RECORD_BYTES) {
    // Bitwise operators in JS work on 32-bit *signed* integers, so the top byte
    // is added rather than OR-ed in - otherwise every timestamp from 2038
    // onwards comes back negative.
    var timestamp = ((bytes[i] & 0xff) |
                     ((bytes[i + 1] & 0xff) << 8) |
                     ((bytes[i + 2] & 0xff) << 16)) >>> 0;
    timestamp += (bytes[i + 3] & 0xff) * 0x1000000;
    var rmssd = (bytes[i + 4] & 0xff) | ((bytes[i + 5] & 0xff) << 8);
    records.push([timestamp, rmssd]);
  }
  return records;
}

function saveRecords(records) {
  try {
    localStorage.setItem(STORAGE_RECORDS, JSON.stringify(records));
    localStorage.setItem(STORAGE_UPDATED, String(Math.floor(Date.now() / 1000)));
  } catch (e) {
    log('could not cache records: ' + e);
  }
}

function loadRecords() {
  try {
    var raw = localStorage.getItem(STORAGE_RECORDS);
    return raw ? JSON.parse(raw) : [];
  } catch (e) {
    return [];
  }
}

function loadUpdatedAt() {
  try {
    return parseInt(localStorage.getItem(STORAGE_UPDATED), 10) || 0;
  } catch (e) {
    return 0;
  }
}

// base64url, so the payload survives being a URL fragment untouched.
function toBase64Url(bytes) {
  var chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_';
  var out = '';
  for (var i = 0; i < bytes.length; i += 3) {
    var b0 = bytes[i] & 0xff;
    var b1 = (i + 1 < bytes.length) ? bytes[i + 1] & 0xff : 0;
    var b2 = (i + 2 < bytes.length) ? bytes[i + 2] & 0xff : 0;
    out += chars[b0 >> 2];
    out += chars[((b0 & 0x03) << 4) | (b1 >> 4)];
    out += (i + 1 < bytes.length) ? chars[((b1 & 0x0f) << 2) | (b2 >> 6)] : '';
    out += (i + 2 < bytes.length) ? chars[b2 & 0x3f] : '';
  }
  return out;
}

function encodeRecords(records) {
  var bytes = [];
  for (var i = 0; i < records.length; i++) {
    var timestamp = records[i][0];
    var rmssd = records[i][1];
    bytes.push(timestamp & 0xff,
               (timestamp >>> 8) & 0xff,
               (timestamp >>> 16) & 0xff,
               (timestamp >>> 24) & 0xff,
               rmssd & 0xff,
               (rmssd >>> 8) & 0xff);
  }
  return toBase64Url(bytes);
}

// Registered first and deliberately self-contained: whatever else in this file
// is broken, tapping the gear icon has to end in an openURL call.
Pebble.addEventListener('showConfiguration', function() {
  var url = CONFIG_URL;
  try {
    var records = loadRecords();
    // The page is told when this cache was last refreshed, so it can say so
    // rather than silently presenting stale numbers as current.
    url = CONFIG_URL + '#v=1&updated=' + loadUpdatedAt() + '&d=' + encodeRecords(records);
    log('opening settings with ' + records.length + ' measurements');
  } catch (e) {
    // An empty page that loads beats a spinner that never resolves.
    log('could not build settings URL, opening it empty: ' + e);
  }
  Pebble.openURL(url);
});

Pebble.addEventListener('ready', function() {
  // The watchapp launches this script, so at its own launch it has no way of
  // knowing whether anything is listening yet. Saying so here, rather than
  // having the watch guess, is what makes the transfer reliable.
  //
  // But this also runs when the script was started just to show the settings
  // page, with no watchapp running to receive it. That send is expected to
  // fail, and must not take the rest of the script down with it.
  try {
    Pebble.sendAppMessage(
      { PhoneReady: 1 },
      function() { log('announced readiness to the watch'); },
      function(err) { log('watchapp not listening: ' + JSON.stringify(err)); });
  } catch (e) {
    log('could not announce readiness: ' + e);
  }
});

Pebble.addEventListener('appmessage', function(e) {
  try {
    var payload = e && e.payload && e.payload.HrvHistory;
    if (!payload) {
      return;
    }
    var records = decodeRecords(payload);
    if (records.length > 0) {
      saveRecords(records);
      log('cached ' + records.length + ' measurements');
    }
  } catch (err) {
    log('could not handle message from watch: ' + err);
  }
});

Pebble.addEventListener('webviewclosed', function() {
  // The page is read-only - there is nothing to send back to the watch.
});

log('script loaded');
