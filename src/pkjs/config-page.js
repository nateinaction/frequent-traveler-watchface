// Returns the HTML string for the Frequent Traveller phone config page.
// index.js opens it as a data: URL; the page posts its result back through the
// pebblejs://close# protocol.
//
// All IANA -> UTC-offset resolution happens here, in the phone's webview,
// because this is the only place in the app with a working Intl (see index.js).

var TZ_LIST = [
  { tz: 'Pacific/Pago_Pago',              name: 'American Samoa',                 short: 'PPG' },
  { tz: 'Pacific/Honolulu',               name: 'Hawaii',                         short: 'HNL' },
  { tz: 'America/Anchorage',              name: 'Alaska',                         short: 'ANC' },
  { tz: 'America/Los_Angeles',            name: 'Pacific Time (US & Canada)',     short: 'LAX' },
  { tz: 'America/Denver',                 name: 'Mountain Time (US & Canada)',    short: 'DEN' },
  { tz: 'America/Phoenix',                name: 'Arizona',                        short: 'PHX' },
  { tz: 'America/Chicago',                name: 'Central Time (US & Canada)',     short: 'CHI' },
  { tz: 'America/Mexico_City',            name: 'Mexico City',                    short: 'MEX' },
  { tz: 'America/New_York',               name: 'Eastern Time (US & Canada)',     short: 'NYC' },
  { tz: 'America/Toronto',                name: 'Toronto',                        short: 'YYZ' },
  { tz: 'America/Caracas',                name: 'Caracas',                        short: 'CCS' },
  { tz: 'America/Halifax',                name: 'Atlantic Time (Halifax)',        short: 'HFX' },
  { tz: 'America/Argentina/Buenos_Aires', name: 'Buenos Aires',                   short: 'BUE' },
  { tz: 'America/Sao_Paulo',              name: 'Sao Paulo',                      short: 'SAO' },
  { tz: 'America/St_Johns',               name: 'Newfoundland',                   short: 'YYT' },
  { tz: 'Atlantic/South_Georgia',         name: 'South Georgia',                  short: 'GRY' },
  { tz: 'Atlantic/Azores',                name: 'Azores',                         short: 'AZO' },
  { tz: 'Atlantic/Cape_Verde',            name: 'Cape Verde',                     short: 'CV'  },
  { tz: 'Europe/London',                  name: 'London, Dublin, Lisbon',         short: 'LDN' },
  { tz: 'Europe/Berlin',                  name: 'Berlin, Paris, Madrid, Rome',    short: 'BER' },
  { tz: 'Europe/Athens',                  name: 'Athens, Helsinki',               short: 'ATH' },
  { tz: 'Europe/Istanbul',                name: 'Istanbul',                       short: 'IST' },
  { tz: 'Africa/Lagos',                   name: 'Lagos, Kinshasa',                short: 'LOS' },
  { tz: 'Africa/Cairo',                   name: 'Cairo',                          short: 'CAI' },
  { tz: 'Africa/Nairobi',                 name: 'Nairobi',                        short: 'NBO' },
  { tz: 'Africa/Johannesburg',            name: 'Johannesburg',                   short: 'JNB' },
  { tz: 'Europe/Moscow',                  name: 'Moscow',                         short: 'MOW' },
  { tz: 'Asia/Tehran',                    name: 'Tehran',                         short: 'THR' },
  { tz: 'Asia/Dubai',                     name: 'Dubai, Abu Dhabi',               short: 'DXB' },
  { tz: 'Asia/Kabul',                     name: 'Kabul',                          short: 'KBL' },
  { tz: 'Asia/Karachi',                   name: 'Karachi, Islamabad',             short: 'KHI' },
  { tz: 'Asia/Kolkata',                   name: 'India (Mumbai, Delhi)',          short: 'BOM' },
  { tz: 'Asia/Kathmandu',                 name: 'Kathmandu',                      short: 'KTM' },
  { tz: 'Asia/Dhaka',                     name: 'Dhaka',                          short: 'DAC' },
  { tz: 'Asia/Yangon',                    name: 'Yangon',                         short: 'RGN' },
  { tz: 'Asia/Bangkok',                   name: 'Bangkok, Hanoi, Jakarta',        short: 'BKK' },
  { tz: 'Asia/Shanghai',                  name: 'Beijing, Shanghai, Hong Kong',   short: 'PEK' },
  { tz: 'Asia/Singapore',                 name: 'Singapore, Kuala Lumpur',        short: 'SIN' },
  { tz: 'Asia/Tokyo',                     name: 'Tokyo, Seoul',                   short: 'TYO' },
  { tz: 'Australia/Perth',                name: 'Perth',                          short: 'PER' },
  { tz: 'Australia/Adelaide',             name: 'Adelaide',                       short: 'ADL' },
  { tz: 'Australia/Darwin',               name: 'Darwin',                         short: 'DRW' },
  { tz: 'Australia/Sydney',               name: 'Sydney',                         short: 'SYD' },
  { tz: 'Australia/Melbourne',            name: 'Melbourne',                      short: 'MEL' },
  { tz: 'Australia/Brisbane',             name: 'Brisbane',                       short: 'BNE' },
  { tz: 'Pacific/Noumea',                 name: 'Noumea, Solomon Is.',            short: 'NOU' },
  { tz: 'Pacific/Auckland',               name: 'Auckland, Wellington',           short: 'AKL' },
  { tz: 'Pacific/Tongatapu',              name: 'Nukualofa',                      short: 'TBU' },
  { tz: 'Pacific/Kiritimati',             name: 'Kiritimati',                     short: 'CXI' }
];

module.exports = function getConfigPageHtml(initialConfig) {
  // Serialize the config + TZ list into the page as a JSON island.
  var bootstrap = {
    config: initialConfig || { localTz: '', zones: [] },
    tzList: TZ_LIST
  };
  var json = JSON.stringify(bootstrap)
    .replace(/</g, '\\u003c')   // safe for embedding inside a <script> element
    .replace(/>/g, '\\u003e')
    .replace(/&/g, '\\u0026');

  return [
    '<!DOCTYPE html>',
    '<html lang="en"><head><meta charset="UTF-8">',
    '<meta name="viewport" content="width=device-width, initial-scale=1">',
    '<title>Frequent Traveller \u2013 Settings</title>',
    '<style>',
    'body{margin:0;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;',
    '  background:#111;color:#eee;padding:16px;font-size:15px;}',
    'h1{font-size:18px;margin:0 0 16px;}',
    'h2{font-size:13px;color:#aaa;text-transform:uppercase;letter-spacing:.08em;',
    '  margin:20px 0 8px;font-weight:600;}',
    '.field{display:block;margin-bottom:10px;}',
    '.field>span{display:block;font-size:12px;color:#bbb;margin-bottom:4px;}',
    '.check{display:flex;align-items:center;cursor:pointer;margin-bottom:10px;}',
    '.check input{width:auto;margin-right:10px;transform:scale(1.3);}',
    'input[type=text],select{width:100%;background:#0f0f0f;border:1px solid #333;',
    '  color:#eee;border-radius:6px;padding:8px;font-size:14px;box-sizing:border-box;',
    '  font-family:inherit;}',
    'button{background:#2b2b2b;color:#eee;border:1px solid #3a3a3a;border-radius:6px;',
    '  padding:8px 12px;font-size:14px;cursor:pointer;font-family:inherit;}',
    'button.primary{background:#d02b2b;color:#fff;border-color:#d02b2b;font-weight:600;}',
    'button:active{transform:scale(.97);}',
    'button[disabled]{opacity:.45;}',
    'table{width:100%;border-collapse:collapse;margin-top:4px;}',
    'th,td{padding:4px 4px;text-align:left;vertical-align:middle;font-size:12px;}',
    'th{color:#888;font-weight:500;border-bottom:1px solid #2a2a2a;}',
    'td.idx{width:24px;color:#666;}',
    'td.lbl{width:82px;}',
    'td.rm{width:32px;text-align:right;}',
    'td .rm-btn{background:transparent;border:1px solid #444;color:#aaa;',
    '  border-radius:4px;padding:2px 8px;}',
    '.row-buttons{margin-top:12px;display:flex;gap:8px;}',
    '.footer{margin-top:24px;display:flex;gap:8px;}',
    '.footer button{flex:1;padding:12px;}',
    '.hint{font-size:11px;color:#888;margin-top:6px;line-height:1.4;}',
    '.swatch{display:inline-block;width:10px;height:10px;border-radius:2px;',
    '  margin-right:6px;vertical-align:middle;}',
    '</style></head><body>',
    '<h1>Frequent Traveller \u2013 Settings</h1>',
    '<p class="hint">Your local time (<span class="swatch" style="background:#d02b2b"></span>red) ',
    'and UTC (<span class="swatch" style="background:#2b6fd0"></span>blue) are always shown. ',
    'Anything you add below fills the rest of the screen.</p>',

    '<h2>Local timezone</h2>',
    '<label class="field"><select id="localTz"></select></label>',
    '<label class="field"><span>Label for the red row</span>',
    '<input type="text" id="localLabel" maxlength="12" placeholder="LOCAL"></label>',
    '<p class="hint">Picking an IANA zone gives the correct current offset, including DST, when you save. This also drives the date in the header.</p>',

    '<h2>Appearance</h2>',
    '<label class="check"><input type="checkbox" id="h24"><span>Use 24-hour clock</span></label>',
    '<label class="check"><input type="checkbox" id="dark"><span>Dark background</span></label>',
    '<p class="hint">The background is white by default. In 12-hour mode times show an a/p suffix.</p>',

    '<h2>Extra timezones</h2>',
    '<table><thead><tr><th class="idx">#</th><th class="lbl">Label</th><th>Timezone</th><th></th></tr></thead>',
    '<tbody id="zonesBody"></tbody></table>',
    '<div class="row-buttons">',
    '<button id="addZone">+ Add zone</button>',
    '<button id="resetZones">Reset defaults</button>',
    '<button id="clearZones">Remove all</button>',
    '</div>',
    '<p class="hint">Up to 6 extra zones (8 rows in total). Fewer zones means bigger text. Labels are capped at 12 characters. A <b>+1</b> or <b>-1</b> is appended automatically when the date in that zone differs from your local date.</p>',

    '<div class="footer">',
    '<button id="cancel">Cancel</button>',
    '<button id="save" class="primary">Save</button>',
    '</div>',

    '<script id="boot" type="application/json">', json, '</script>',
    '<script>(function(){',
    'var BOOT=JSON.parse(document.getElementById("boot").textContent);',
    'var TZ_LIST=BOOT.tzList;',
    'var TZ_BY=Object.create(null);TZ_LIST.forEach(function(t){TZ_BY[t.tz]=t;});',
    'var config=BOOT.config||{localTz:"",zones:[]};',
    'if(!config.zones)config.zones=[];',
    'if(!config.localTz){try{config.localTz=Intl.DateTimeFormat().resolvedOptions().timeZone||"UTC";}catch(e){config.localTz="UTC";}}',
    // If localTz isn't in our curated list (some browser-detected oddball), add it.
    'if(!TZ_BY[config.localTz]){TZ_LIST.push({tz:config.localTz,name:config.localTz,short:"LOC"});TZ_BY[config.localTz]=TZ_LIST[TZ_LIST.length-1];}',

    'function pad(n){return n<10?"0"+n:""+n;}',
    // Current UTC offset of `tz` in minutes: format the instant in that zone,
    // read it back as if it were UTC, and difference the two.
    'function offsetMin(tz,t){try{var dtf=new Intl.DateTimeFormat("en-US",{timeZone:tz,year:"numeric",month:"2-digit",day:"2-digit",hour:"2-digit",minute:"2-digit",second:"2-digit",hour12:false});var p=dtf.formatToParts(new Date(t));var m={};p.forEach(function(x){if(x.type!=="literal")m[x.type]=x.value;});var h=+m.hour;if(h===24)h=0;return Math.round((Date.UTC(+m.year,+m.month-1,+m.day,h,+m.minute,+m.second)-t)/60000);}catch(e){return 0;}}',
    'function fmtOff(min){var s=min<0?"-":"+";var a=Math.abs(min);var h=Math.floor(a/60);var mm=a%60;return"GMT"+s+pad(h)+(mm?":"+pad(mm):"");}',
    'function esc(s){return String(s).replace(/[&<>"\']/g,function(c){return{"&":"&amp;","<":"&lt;",">":"&gt;","\\"":"&quot;","\'":"&#39;"}[c];});}',

    'function tzOptions(selected){var now=Date.now();var arr=TZ_LIST.map(function(t){return{tz:t.tz,name:t.name,off:offsetMin(t.tz,now)};}).sort(function(a,b){return a.off-b.off||a.name.localeCompare(b.name);});return arr.map(function(t){return\'<option value="\'+esc(t.tz)+\'"\'+(t.tz===selected?" selected":"")+\'>(\'+fmtOff(t.off)+") "+esc(t.name)+"</option>";}).join("");}',

    'document.getElementById("localTz").innerHTML=tzOptions(config.localTz);',
    'document.getElementById("localTz").addEventListener("change",function(e){config.localTz=e.target.value;});',

    'var labelEl=document.getElementById("localLabel");',
    'labelEl.value=config.localLabel||"LOCAL";',
    'labelEl.addEventListener("input",function(e){config.localLabel=e.target.value.slice(0,12);});',

    'var h24El=document.getElementById("h24");',
    'h24El.checked=(typeof config.h24==="boolean")?config.h24:true;',
    'h24El.addEventListener("change",function(e){config.h24=e.target.checked;});',

    'var darkEl=document.getElementById("dark");',
    'darkEl.checked=(typeof config.dark==="boolean")?config.dark:false;',
    'darkEl.addEventListener("change",function(e){config.dark=e.target.checked;});',

    // Zero extra zones is legal here — local + UTC still fill the screen.
    'var MAX_ZONES=6;',
    'if(config.zones.length>MAX_ZONES)config.zones=config.zones.slice(0,MAX_ZONES);',

    'var tbody=document.getElementById("zonesBody");',
    'var addBtn=document.getElementById("addZone");',
    'function updateAddBtn(){addBtn.disabled=config.zones.length>=MAX_ZONES;addBtn.textContent=addBtn.disabled?"Max "+MAX_ZONES+" zones":"+ Add zone";}',
    'function renderRows(){tbody.innerHTML="";config.zones.forEach(function(z,i){var tr=document.createElement("tr");tr.innerHTML=\'<td class="idx">\'+(i+1)+\'</td>\'+\'<td class="lbl"><input type="text" maxlength="12" data-i="\'+i+\'" data-k="label" value="\'+esc(z.label||"")+\'"></td>\'+\'<td><select data-i="\'+i+\'" data-k="tz">\'+tzOptions(z.tz)+"</select></td>"+\'<td class="rm"><button class="rm-btn" data-rm="\'+i+\'">\\u00d7</button></td>\';tbody.appendChild(tr);});tbody.querySelectorAll("input,select").forEach(function(el){el.addEventListener("input",onEdit);el.addEventListener("change",onEdit);});tbody.querySelectorAll("[data-rm]").forEach(function(b){b.addEventListener("click",function(){config.zones.splice(+b.dataset.rm,1);renderRows();});});updateAddBtn();}',

    // Editing the timezone re-suggests its airport code, but only if the label
    // still matches the previous zone's suggestion (i.e. wasn't hand-edited).
    'function onEdit(e){var i=+e.target.dataset.i;var k=e.target.dataset.k;if(k==="tz"){var oldShort=(TZ_BY[config.zones[i].tz]||{}).short;config.zones[i].tz=e.target.value;var ne=TZ_BY[e.target.value];if(ne&&config.zones[i].label===oldShort){config.zones[i].label=ne.short;renderRows();}}else{config.zones[i].label=e.target.value.slice(0,12);}}',

    'addBtn.addEventListener("click",function(){if(config.zones.length>=MAX_ZONES)return;var d=TZ_BY["America/New_York"]||TZ_LIST[0];config.zones.push({tz:d.tz,label:d.short});renderRows();});',
    'document.getElementById("resetZones").addEventListener("click",function(){config.zones=[{tz:"America/New_York",label:"NYC"},{tz:"Europe/London",label:"LDN"},{tz:"Asia/Tokyo",label:"TYO"}];renderRows();});',
    'document.getElementById("clearZones").addEventListener("click",function(){config.zones=[];renderRows();});',

    // On save: resolve every IANA name to a current offset (Intl works here in
    // the webview) so pkjs can ship numbers to the watch without touching Intl.
    'document.getElementById("save").addEventListener("click",function(){var now=Date.now();var out={localTz:config.localTz,localOffset:offsetMin(config.localTz,now),localLabel:(config.localLabel||"LOCAL").slice(0,12),h24:!!config.h24,dark:!!config.dark,zones:config.zones.map(function(z){return{tz:z.tz,label:z.label,offset:offsetMin(z.tz,now)};})};document.location="pebblejs://close#"+encodeURIComponent(JSON.stringify(out));});',
    'document.getElementById("cancel").addEventListener("click",function(){document.location="pebblejs://close";});',

    'renderRows();',
    '})();</script>',
    '</body></html>'
  ].join('');
};
