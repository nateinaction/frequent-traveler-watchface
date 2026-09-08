// Frequent Traveler — phone-side JS companion.
//
// This file deliberately does NOT use `Intl.DateTimeFormat`. The Pebble
// emulator's JS host (pypkjs) crashes on Intl construction with a fatal OOM.
// Real phone Pebble apps have full Intl, but to keep the emulator usable we
// route all IANA -> offset resolution through the config page (HTML in a phone
// webview, which does have a working Intl).
//
// Data flow:
//   1. User opens Settings -> the config page builds its dropdowns via Intl,
//      and on Save posts back a payload that already contains the resolved
//      offset (in minutes) for the local zone and every selected world zone.
//   2. pkjs persists that payload and forwards the offsets to the watch over
//      AppMessage. No Intl required here.
//   3. The watch uses those fixed offsets until Settings is saved again.

var getConfigPageHtml = require('./config-page.js');

// Extra zones only: the watch always draws the local band above these, so the
// on-screen row count is this plus one.
var MAX_ZONES = 6;
var MAX_LABEL_LEN = 12;
// A band nobody has recolored is white, like the watchface background.
var DEFAULT_COLOR = 0xFFFFFF;
var STORAGE_KEY = 'frequentTravelerConfig';
// The key shipped misspelled. Anyone who saved settings before the rename has
// their config under the old one, so reads fall back to it; the next save
// moves them over.
var LEGACY_STORAGE_KEY = 'frequentTravellerConfig';

// The shipped demo: black bands with UTC picked out in blue, under a white
// local band. Offsets here are only the pre-Settings guess; the config page
// resolves the real ones (with DST) the first time you save.
var DEFAULT_ZONES = [
  { tz: 'Europe/Paris',     label: 'PAR', offset:  120, color: 0x000000 },
  { tz: 'Asia/Tokyo',       label: 'TYO', offset:  540, color: 0x000000 },
  { tz: 'Etc/UTC',          label: 'UTC', offset:    0, color: 0x0055AA },
  { tz: 'America/New_York', label: 'NYC', offset: -240, color: 0x000000 },
  { tz: 'America/Los_Angeles', label: 'LAX', offset: -420, color: 0x000000 }
];

function phoneLocalOffset() {
  return -new Date().getTimezoneOffset();
}

function cloneZone(z) {
  return { tz: z.tz, label: z.label, offset: z.offset, color: z.color };
}

function defaultConfig() {
  return {
    // pkjs avoids Intl for emulator compatibility, so the config page detects
    // the IANA name when Settings opens. The numeric offset alone is enough to
    // render the local band correctly on first launch.
    localTz:     '',
    localOffset: phoneLocalOffset(),
    localColor:  DEFAULT_COLOR,
    h24:         true,
    zones:       DEFAULT_ZONES.map(cloneZone)
  };
}

function normalize(c) {
  if (typeof c.localOffset !== 'number') c.localOffset = phoneLocalOffset();
  if (typeof c.localTz     !== 'string') c.localTz = '';
  if (typeof c.localColor  !== 'number' || !isFinite(c.localColor)) {
    c.localColor = DEFAULT_COLOR;
  }
  c.localColor = (c.localColor | 0) & 0xFFFFFF;
  if (typeof c.h24         !== 'boolean') c.h24 = true;
  if (!Array.isArray(c.zones)) c.zones = [];
  if (c.zones.length > MAX_ZONES) c.zones = c.zones.slice(0, MAX_ZONES);
  c.zones.forEach(function (z) {
    if (typeof z.offset !== 'number') z.offset = 0;
    if (typeof z.label  !== 'string') z.label = '';
    if (typeof z.tz     !== 'string') z.tz = 'UTC';
    if (typeof z.color  !== 'number' || !isFinite(z.color)) z.color = DEFAULT_COLOR;
    z.color = (z.color | 0) & 0xFFFFFF;
    z.label = z.label.slice(0, MAX_LABEL_LEN);
  });
  return c;
}

function loadConfig() {
  try {
    var s = localStorage.getItem(STORAGE_KEY);
    if (!s) s = localStorage.getItem(LEGACY_STORAGE_KEY);
    if (s) return normalize(JSON.parse(s));
  } catch (e) {
    console.log('loadConfig failed: ' + e);
  }
  return defaultConfig();
}

function saveConfig(c) {
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(c));
  } catch (e) {
    console.log('saveConfig failed: ' + e);
  }
}

function buildDict(config) {
  var zones = (config.zones || []).slice(0, MAX_ZONES);
  var dict = {
    'LOCAL_OFFSET': (config.localOffset | 0),
    'LOCAL_COLOR':  (((typeof config.localColor === 'number' ? config.localColor
                                                            : DEFAULT_COLOR) | 0)
                     & 0xFFFFFF),
    'NUM_ZONES':    zones.length,
    'H24':          (config.h24 ? 1 : 0)
  };
  for (var i = 0; i < zones.length; i++) {
    dict['Z_LABEL_'  + i] = String(zones[i].label || '').slice(0, MAX_LABEL_LEN);
    dict['Z_OFFSET_' + i] = zones[i].offset | 0;
    dict['Z_COLOR_'  + i] = ((typeof zones[i].color === 'number' ? zones[i].color
                                                                : DEFAULT_COLOR) | 0) & 0xFFFFFF;
  }
  return dict;
}

// One retry, then give up: the watch keeps its persisted config, and the next
// `ready` event or REQUEST_CONFIG will try again anyway.
function sendToWatch(config) {
  var dict = buildDict(config);
  console.log('FT sendToWatch: ' + JSON.stringify(dict));
  Pebble.sendAppMessage(dict,
    function (e) { console.log('FT delivered txn=' + e.data.transactionId); },
    function (e) {
      console.log('FT delivery failed: ' + (e && e.error && e.error.message));
      setTimeout(function () {
        Pebble.sendAppMessage(dict,
          function () { console.log('FT retry delivered.'); },
          function () { console.log('FT retry failed; keeping persisted config.'); });
      }, 3000);
    });
}

Pebble.addEventListener('ready', function () {
  console.log('FT JS ready');
  sendToWatch(loadConfig());
});

Pebble.addEventListener('appmessage', function (e) {
  if (e && e.payload && e.payload.REQUEST_CONFIG) {
    sendToWatch(loadConfig());
  }
});

Pebble.addEventListener('showConfiguration', function () {
  var html = getConfigPageHtml(loadConfig());
  Pebble.openURL('data:text/html;charset=utf-8,' + encodeURIComponent(html));
});

Pebble.addEventListener('webviewclosed', function (e) {
  if (!e || !e.response) return;
  var config = null;
  try {
    config = JSON.parse(e.response);
  } catch (e1) {
    try {
      config = JSON.parse(decodeURIComponent(e.response));
    } catch (e2) {
      console.log('FT bad config payload: ' + e2);
      return;
    }
  }
  if (!config) return;
  config = normalize(config);
  saveConfig(config);
  sendToWatch(config);
});
