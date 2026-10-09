// MuseScore Link: the Max for Live device for "Live plays the score" (LIVE.md; MuseScore's side:
// libmscore/liveclips.h, mscore/liveclips.h).
//
// One copy goes on each MIDI track that plays a library part, BEFORE the instrument (Kontakt). In
// every copy the patcher (not this script) turns the carrier notes of MuseScore's clips (keys 114-127: controllers, pitch bend)
// into their MIDI controllers, in Max's scheduler, sample-timed with the notes; this script is not in
// that path. It does so only on a track holding a MuseScore clip (protocol 8; below, "Carriers"): elsewhere (the plain
// set's technique tracks, any other track) every note, keys 114-127 too, passes unchanged.
// One copy (the first loaded; another takes over when it goes) is the hub:
//   - it listens to MuseScore (OSC over UDP on localhost, port 9001 by default; answers on port + 1);
//   - it writes each part's clip on its track: found by MIDI From = the part's MuseScore port and
//     channel, else (a part's main patch) by track name = part name; one arrangement clip from beat
//     0 over the whole score, named "MuseScore: <part>", its notes replaced at each change;
//   - it never touches a clip it didn't make (another name), and doesn't make one over a track's
//     other clips;
//   - it sets Live's tempo to the score's first and puts a locator ("MS 12") at each played bar,
//     only while Live is stopped, and only its own locators;
//   - it reports Live's transport (~25 a second while playing) so MuseScore follows, and starts or
//     stops Live when MuseScore's Play or Stop is pressed;
//   - "/ms/mode stream": MuseScore plays through Live (the tracks' Monitor on In, which silences
//     clips); "clips": Live plays the clips (Monitor on Auto).
//   - "Edit in MuseScore" (the button, on any copy): the clip in Live's Detail View (any MIDI clip, not
//     only MuseScore's) is read with every field and sent to MuseScore, which opens it as a score; the
//     hub then applies MuseScore's edits to it by note id (remove_notes_by_id, apply_note_modifications
//     with Live's own note data and only the edited fields changed, add_new_notes), checks the clip once
//     a second and reports a change made in Live as a conflict (nothing more is written until MuseScore
//     reads it again). LIVE.md › Editing Live clips in MuseScore; mscore/liveclipmodel.h.
//   - a clip tab plays through its track (protocol 4; mscore/liveclipmodel.h): MuseScore sends what it plays as
//     /ms/midi trackId status data1 data2; the hub's patcher (not this script) passes it on at once, in Max's
//     scheduler: [route /ms/midi] -> [forward] to "msl_m<trackId>"; every copy's [receive] is named after its
//     own track ("set msl_m<track id>" from outlet 7) -> iter -> midiout, into the track's chain before the
//     instrument (nothing recorded, no arming). The hub tells MuseScore each edited clip's track and whether a
//     copy of protocol 4+ is on it (/live/clip/track, again when that changes). Live's transport, song time and
//     clips are never touched for it. /live/bye when the hub goes; /ms/clip/adopt: a new hub takes over a clip.
//     While MuseScore plays through the track (/ms/cliptab/audible) the hub un-mutes it and its groups and solos it
//     when others are soloed, and puts each back after (below, "a clip tab's track audible").
//   - automation lanes of any Live track (protocol 5; /ms/params/ask: all of them again; the owner, 2026-10-02: "the automation display wouldn't only work
//     for sso, it works for any midi clip"): for each edited clip and each route's track the hub sends the track's
//     automatable parameters, /live/params key:s hash:i chunk:i chunks:i (d:i p:i name:s min:f max:f quantized:i) × n
//     (d -1: the mixer, p 0 volume, 1 pan; else devices[d].parameters[p]; MuseScore Link itself left out), again
//     when the track's devices change (checked once a second), and each edited clip's place, /live/clip/where key:s
//     track:i slot:i (the track's index; the session slot's, -1 for an arrangement clip), again when it moves.
//   - a clip tab plays at the song's tempo (protocol 6; mscore/cliptempo.h): for an edited arrangement clip the hub
//     sends its place in the song, /live/clip/span key:s start_time:f end_time:f start_marker:f end_marker:f
//     loop_start:f loop_end:f looping:i (after /live/clip/where, again when one changes: checked once a second), and
//     /live/transport goes out as soon as Live's tempo changes (also while stopped), so a clip tab follows it.
//     A clip tab's lanes are written into the clip's own envelopes by the MuseScore Envelopes Control Surface
//     script (tools/live/MuseScoreEnvelopes: Max for Live can't), MuseScore talking to it directly. A route's lane
//     titled "live:<d>/<p>" is that parameter of the track (no plug-in needed), driven as below.
//   - velocity curves of clip tabs (protocol 7): the copy on a clip's track changes the clip's note-ons as they pass
//     (its patcher, from a ring this script fills), and what the device keeps in the set goes into the track through
//     the MuseScore Envelopes script (Track.set_data: no Live undo step). Below, "velocity curves", "kept in the set".
//   - plug-in parameter lanes (MuseScore's automation of Kontakt's parameters): the LOM can't write clip
//     envelopes or arrangement automation, so each copy drives its own track's plug-in parameters
//     itself while Live plays. Below, "Parameter lanes".
// The Live Object Model is used from Max's low-priority thread only (messages from udpreceive go
// through deferlow; the Tasks run there).
//
// Parameter lanes (protocol 3):
//   MuseScore -> hub: /ms/params gen:i key:s lanes:i hash:i, then per lane /ms/pvals gen:i key:s lane:i
//     title:s pid:i (the plug-in's parameter id, -1: not known) chunk:i chunks:i (time:i value:f) × n (time in UNITS from the song start, value 0-1, a
//     step from that time on; ≤ 100 pairs a packet). lanes 0: the route drives nothing any more.
//   hub -> copies: only the hub hears MuseScore, so the lanes go through the Global: "p<track id>" holds
//     the track's entry, JSON { serial, length (beats), routes: { key: { hash, lanes: [{ title, ev:
//     [time, value, …] }] } } } (every route on that track), g.pserial the last serial. Then
//     messnamed("msl_params", track, serial); every copy's [receive msl_params] -> [deferlow] (no
//     re-entry into the sending script) -> "msl_params track serial" here; the copy on that track
//     schedules the work (a Task). Each copy also checks its entry's serial once a second (a missed
//     message, a copy loaded later).
//   copy -> hub: the copy writes "pr<track id>" = JSON { serial, results: { key: { status } } }; the hub
//     polls it (workStep) and answers /live/papplied key:s hash:i status:s track:s (status "ok", or
//     "missing: Vibrato, Mic 1 level" / "too many lanes (16 at most): …" / "no plug-in on the track",
//     "; " between; "no track …"; "no MuseScore Link device on the track" after 3 s without an answer,
//     and the real answer still sent when it comes). Only the first copy on a track (the track's device
//     order) drives; another releases.
//   in a copy: the patcher's song-position signal, [phasor~ @frequency 7864320 ticks @lock 1] (16384 quarter
//     notes, phase-locked to Live's transport) -> [*~ f], f = 16384 × 60000 / Live's tempo (outlet 4):
//     the song position in ms. 16 slots: [buffer~ ---mslp<k>] read by [index~] at that ms (step-hold,
//     1 ms) -> [live.remote~] k (its right inlet: "id n" takes the parameter, "id 0" releases). The
//     buffer holds the value in force at each ms of the song, in the parameter's range (min + v ×
//     (max - min)); before a lane's first event: the parameter's value as read from the LOM before
//     it was taken (kept while driven; put back when released). A title matches a parameter of the
//     track's plug-in (the first PluginDevice, else the first non-Max device after this one) as
//     Vst3Plugin::looseTitle compares them. The "---" prefix comes resolved from [loadmess prefix
//     ---mslp]. Tables are rewritten when Live's tempo changes (checked once a second).
//   debugging (hub): /ms/probe id:i what:s -> /live/probe id:i text:s (what "pos": the ms signal now,
//     through [snapshot~]; "state": this copy's slots and the Global's entries; else a LOM path: its
//     id, type and info); /ms/probecall id:i path:s fn:s args… -> /live/probe id text (call, or "get" /
//     "set" a property).
//
// Carriers (protocol 8): the patcher's [gate 2 2] in front of the carrier keys' [route]: outlet 1 the carriers' route,
//   outlet 2 (the patcher's initial state) every note straight on to the velocity shaper, unchanged. The copy sets it
//   (outlet 9 "gate 1" / "gate 2") from its own track: a clip named "MuseScore: …" (liveclips.cpp clipName: only
//   MuseScore's clips carry carriers; the plain set's clips are named after their technique) in the arrangement or in a
//   session slot -> 1, else 2. Checked when the copy loads, once a second (the clips' ids each time, their names when
//   the ids change and every CARRIER_NAMES_EVERY seconds: a rename), and at once when the hub makes or deletes a clip on
//   the track (messnamed("msl_carriers", track) -> every copy's [receive msl_carriers] -> deferlow). The whole track
//   counts: another clip on a track with a MuseScore clip has its keys 114-127 converted too (the hub makes no clip
//   over a track's other clips). A note on 114-127 sounding while the gate goes to 1 (a MuseScore clip made on its
//   track just then) loses its note-off, as the carriers' are dropped.
//
// Plain ECMAScript 5 so it runs in [js] and [v8] alike, and in the Node tests (tools/live/test).

autowatch = 0;
inlets = 1;
outlets = 10;     // 0: OSC to MuseScore (udpsend), 1: udpsend's host / port, 2: status text,
                  // 3: "k id n" to the slots' live.remote~ (route 0 … 15), 4: the ms factor ([*~]), 5: bang [snapshot~],
                  // 6: the lanes to keep in the Live Set ([pattr Lanes]),
                  // 7: "set msl_m<track id>" to the [receive] that plays MuseScore's notes for a clip tab,
                  // 8: OSC to the MuseScore Envelopes script ([udpsend 127.0.0.1 9005]: what is kept in the set),
                  // 9: the velocity shaper ("bias <ticks>", "log 0/1") and the carriers' gate ("gate 1/2": Carriers)

var PROTOCOL = 8;                       // 2: editing Live clips; 3: parameter lanes; 4: clip tabs play through their track;
                                        // 5: the tracks' parameters and the clips' places (automation lanes of any track);
                                        // 6: an arrangement clip's place in the song, Live's tempo reported as it changes;
                                        // 7: velocity curves of clip tabs, kept through the MuseScore Envelopes script;
                                        // 8: carriers converted only on a track holding a MuseScore clip (Carriers)
var CLIP_PREFIX = "MuseScore: ";        // liveclips.cpp clipName
var CARRIER_NAMES_EVERY = 5;            // s: the clips' names read again (a rename) without a change of their ids
var UNITS = 3840;                       // LiveClips::UNITS_PER_BEAT
var BATCH = 500;                        // notes per add_new_notes call
// (measured, numbers-measured 2026-10-03, Live 12.4.6: while Live froze or duplicated Kontakt + SSO tracks the hub's 1 s
// Task stalled up to 6.0 s (its hellos 6.0 s apart); at 5 s other copies took over and several hubs said hello at once.
// The longest stall plus one beat: 7 s. AUDIBLE_STALE_MS the same: the stall plus MuseScore's 1 s heartbeat)
var HUB_STALE_MS = 7000;

var self = this;
// shared by every copy of the device: which is the hub, and where each copy sits (as JSON: a
// Global's values are safest as strings)
var g = new Global("musescore_link");
function registry() {
      try { return JSON.parse(g.devices || "{}"); } catch (e) { return {}; }
      }
function saveRegistry(r) { g.devices = JSON.stringify(r); }

var me = { key: "d" + Math.floor(Math.random() * 1e9), track: 0, device: 0 };
var udpPort = 9001;
var isHub = false;
var session = "";
var receiver = null;          // hub: the udpreceive and deferlow it made
var deferrer = null;
var mode = "clips";
var pending = {};             // key -> a clip being received
var pendingSong = null;
var work = [];                // clips and the song to write, in order
var placed = {};              // key -> { track: id, clip: name } where the hub put it
var lastTransport = { playing: -1, beat: -1, sent: 0 };
var edits = {};               // key -> a clip edited in MuseScore
var editSerial = 0;
var lastEditRequest = "";
var heartbeat = null;
var worker = null;
var reporter = null;
var initialised = false;
// parameter lanes: the hub's side
var SLOTS = 16;                         // make_device.py SLOTS
var PERIOD_QUARTERS = 16384;            // the phasor~'s period (make_device.py PERIOD_TICKS / 480)
var MAX_MS = 3600000;                   // a table's length at most (an hour)
var POKE = 8192;                        // values a Buffer.poke
var NO_DEVICE_MS = 3000;
var routes = {};              // key -> the route as /ms/track gave it (to find its track)
var pendingParams = {};       // key -> lanes being received
var paramTracks = {};         // key -> the track its lanes were put on
var waiting = {};             // key -> lanes handed to a copy, its answer awaited
var songBeats = 0;            // the song's length (/ms/song)
var keepTasks = [];           // one-shot Tasks, kept referenced until they run
var probes = [];              // "pos" probes awaiting the snapshot~
// … every copy's
var prefix = "";              // the slots' buffer~ names' resolved "---mslp"
var slots = [];               // k -> { id, sig } the parameter slot k drives
var bases = {};               // parameter id -> { value, min, max } before it was driven
var seenSerial = -1;          // the entry last applied
var filledBpm = 0;
var paramTask = null;
// … and kept in the Live Set ([pattr Lanes], a Live parameter of type Blob, Stored Only: outlet 6 sets it, its value
// comes back as "lanes …" when the set opens), so the lanes play without MuseScore (LIVE.md › Automation lanes ›
// Without MuseScore). The track's lanes as last applied:
var saved = null;             // { length (beats), routes: { key: { hash, lanes: [{ title, pid, ev }] } } }
var savedSerial = 0;          // counts the values the set gave back (an entry's serial "saved<n>")
var SAVED_TAG = "msl-lanes";
var SAVED_VERSION = 2;                  // 1: (time value) pairs; 2: pairs and runs (packLane)
var STORE_DELAY_MS = 2000;              // the stores are set once MuseScore's lane edits pause this long (Live: one
                                        // undo step a pause, not one a keystroke; playback follows at once)
var STORES = 4;                         // make_device.py STORES: [pattr Lanes], [pattr Lanes2] …
var STORE_ATOMS = 30000;                // atoms a store at most (Live 12.2 crashed on a [pattr] set to 34010 atoms;
                                        // 24010 copied fine: the test VM, 2026-10-01)
var parts = [];                         // k -> the atoms store k gave back
var sentParts = [];                     // k -> the atoms last sent to store k (as JSON: its echo is left alone)
var keptStatus = "";                    // "" or why the lanes aren't kept in the set
var storeTask = null;                   // sets the stores STORE_DELAY_MS after the last change
var restoredLog = [];         // (debugging: the values the set gave back, for the "state" probe)

function now() { return new Date().getTime(); }
function num(v) { return Number(Array.isArray(v) ? v[0] : v); }
function str(v) {
      if (Array.isArray(v))
            return v.join(" ");
      return v === undefined || v === null ? "" : String(v);
      }
function loose(s) { return str(s).toLowerCase().replace(/[^a-z0-9]/g, ""); }
function loosePort(s) { return loose(str(s).replace(/^\s*ext:\s*/i, "")); }

// a LOM dictionary property (input_routing_type …): a JSON string, sometimes in an array
function displayName(v) {
      var s = str(v);
      try {
            var o = JSON.parse(s);
            var find = function(x) {
                  if (!x || typeof x !== "object")
                        return null;
                  if (typeof x.display_name === "string")
                        return x.display_name;
                  for (var k in x) {
                        var r = find(x[k]);
                        if (r !== null)
                              return r;
                        }
                  return null;
                  };
            var r = find(o);
            if (r !== null)
                  return r;
            }
      catch (e) {}
      var m = /"display_name"\s*:\s*"([^"]*)"/.exec(s);
      return m ? m[1] : s;
      }

// ["id", 3, "id", 7] -> [3, 7]
function ids(v) {
      var out = [];
      if (!Array.isArray(v))
            v = str(v).split(" ");
      for (var i = 0; i + 1 < v.length; i += 2)
            if (str(v[i]) === "id" && num(v[i + 1]) > 0)
                  out.push(num(v[i + 1]));
      return out;
      }

function status(text) {
      outlet(2, "set", text);
      }

function send() {
      var a = Array.prototype.slice.call(arguments);
      outlet.apply(this, [0].concat(a));
      }

//---------------------------------------------------------
//   the device's life
//---------------------------------------------------------

// live.thisdevice: the Live API is ready
function bang() {
      if (initialised)
            return;
      initialised = true;
      var dev = new LiveAPI("this_device");
      me.device = num(dev.id);
      var tr = new LiveAPI("this_device canonical_parent");
      me.track = num(tr.id);
      var r = registry();
      r[me.key] = { track: me.track, device: me.device, beat: now(), protocol: PROTOCOL };
      saveRegistry(r);
      outlet(7, "set", "msl_m" + me.track);         // (MuseScore's notes for a clip tab on this track)
      heartbeat = new Task(beat, this);
      heartbeat.interval = 1000;
      heartbeat.repeat();
      paramTask = new Task(applyParams, this);
      storeTask = new Task(flushStores, this);
      elect();
      if (!isHub)
            status("MuseScore Link: on this track (the hub is another copy)");
      outlet(4, msFactor(liveTempo()));
      if (g["klanes" + me.track])                   // (the script gave this track's values before this copy loaded)
            keptChanged("lanes");
      paramsCheck(!!saved);
      velTask = new Task(velFill, this);
      velObserve();
      velChanged();
      carriersCheck(true);
      }

function beat() {
      var r = registry();
      if (r[me.key]) {
            r[me.key].beat = now();
            saveRegistry(r);
            }
      if (isHub) {
            g.hubBeat = now();
            if (Math.floor(now() / 1000) % 2 === 0)
                  send("/live/hello", session, PROTOCOL);
            outlet(8, "/ms/keep/ping", udpPort);          // (the script answers /live/keep/pong: it keeps the values)
            checkEdits();
            checkRouteParams();
            checkAudible();
            velAnswerLate();
            }
      else
            elect();
      paramsCheck();
      velFill();
      carriersCheck(false);
      }

function elect() {
      if (isHub)
            return;
      if (!g.hub || !g.hubBeat || now() - g.hubBeat > HUB_STALE_MS || !registry()[g.hub])
            becomeHub();
      }

function becomeHub() {
      g.hub = me.key;
      g.hubBeat = now();
      isHub = true;
      session = "s" + Math.floor(Math.random() * 1e9);
      openPort();
      worker = new Task(workStep, this);
      worker.interval = 20;
      worker.repeat();
      reporter = new Task(report, this);
      reporter.interval = 40;
      reporter.repeat();
      send("/live/hello", session, PROTOCOL);
      status("MuseScore Link: hub, listening on UDP " + udpPort + ", waiting for MuseScore");
      }

// the hub alone listens: its udpreceive is made here (every copy binding the port would clash)
function openPort() {
      var p = self.patcher;
      if (!p)
            return;
      if (receiver)
            p.remove(receiver);
      // the patcher's [route /ms/midi] (MuseScore's notes, straight on to the tracks' copies; the rest through its
      // [deferlow] to this script), else (an older patcher) a deferlow made here
      var into = p.getnamed ? p.getnamed("msl_in") : null;
      if (!into && !deferrer) {
            deferrer = p.newdefault(20, 600, "deferlow");
            p.connect(deferrer, 0, self.box, 0);
            }
      receiver = p.newdefault(20, 570, "udpreceive", udpPort);
      p.connect(receiver, 0, into || deferrer, 0);
      outlet(1, "host", "127.0.0.1");
      outlet(1, "port", udpPort + 1);
      }

// the Port box ("port 9001")
function setPort(n) {
      n = Math.round(num(n));             // (a Float parameter in Live: 9001.0)
      if (!(n > 1023 && n < 65535) || n === udpPort)
            return;
      udpPort = n;
      if (isHub)
            openPort();
      }

function resync() {
      if (isHub)
            send("/live/resync");
      }

function notifydeleted() {
      var r = registry();
      delete r[me.key];
      saveRegistry(r);
      if (isHub) {
            try { restoreAudible(); } catch (e) {}    // (else the next hub does, the heartbeat stale)
            g.hub = null;
            g.hubBeat = 0;
            send("/live/bye", session);           // (MuseScore: the connection lost now, not in 6 s)
            }
      if (heartbeat) heartbeat.cancel();
      if (worker) worker.cancel();
      if (reporter) reporter.cancel();
      if (paramTask) paramTask.cancel();
      if (storeTask) storeTask.cancel();
      if (velTask) velTask.cancel();
      }

//---------------------------------------------------------
//   MuseScore's messages
//---------------------------------------------------------

// the "Edit in MuseScore" button (any copy: the hub does the work)
function edit() {
      if (isHub)
            work.push({ kind: "edit" });
      else {
            g.editRequest = me.key + ":" + now();
            status("MuseScore Link: asked the hub to send the clip to MuseScore");
            }
      }

function anything() {
      var a = arrayfromargs(arguments);
      if (messagename === "port")
            return setPort(a[0]);
      if (messagename === "edit")
            return edit();
      if (messagename === "prefix") {                         // ([loadmess prefix ---mslp], resolved)
            prefix = str(a[0]);
            return paramsCheck(true);
            }
      if (messagename === "msl_params") {                     // (the hub put lanes on a track)
            if (num(a[0]) === me.track && me.track)
                  paramsCheck();
            return;
            }
      if (messagename === "lanes")                            // ([pattr Lanes]: the value the Live Set kept)
            return restoreSaved(a);
      if (messagename === "posvalue")                         // ([snapshot~]: a "pos" probe's answer)
            return answerPos(num(a[0]));
      if (messagename === "msl_keep") {                       // (the hub got a track's kept values from the script)
            if (num(a[0]) === me.track && me.track)
                  keptChanged(str(a[1]));
            return;
            }
      if (messagename === "msl_vel") {                        // (a velocity curve on a track changed)
            if (num(a[0]) === me.track && me.track)
                  velChanged();
            return;
            }
      if (messagename === "msl_carriers") {                   // (the hub made or deleted a clip on a track)
            if (num(a[0]) === me.track && me.track)
                  carriersCheck(true);
            return;
            }
      if (messagename === "vlog")                             // (the shaper's note-ons, while the "vlog" probe asks)
            return velLog(a);
      if (messagename === "dspsr" || messagename === "dspvs") {   // ([dspstate~]: the sample rate, the vector size)
            if (messagename === "dspsr")
                  velSr = num(a[0]);
            else
                  velVs = num(a[0]);
            velSig = "";
            return;
            }
      if (!isHub)
            return;
      handle(messagename, a);
      }

function handle(address, a) {
      if (address === "/ms/mode") {
            mode = str(a[0]) === "stream" ? "stream" : "clips";
            work.push({ kind: "mode" });
            }
      else if (address === "/ms/song") {
            pendingSong = { gen: num(a[0]), bpm: num(a[1]), length: num(a[2]), count: num(a[3]), chunks: num(a[4]),
                            hash: num(a[5]), cues: [], got: 0 };
            songBeats = pendingSong.length / UNITS;
            if (pendingSong.chunks === 0)
                  queueSong();
            }
      else if (address === "/ms/cues") {
            if (!pendingSong || pendingSong.gen !== num(a[0]))
                  return;
            for (var i = 2; i + 1 < a.length; i += 2)
                  pendingSong.cues.push({ time: num(a[i]) / UNITS, name: str(a[i + 1]) });
            if (++pendingSong.got === pendingSong.chunks)
                  queueSong();
            }
      else if (address === "/ms/track") {
            var t = { gen: num(a[0]), key: str(a[1]), port: str(a[2]), channel: num(a[3]), part: str(a[4]), clip: str(a[5]),
                      main: num(a[6]) !== 0, length: num(a[7]) / UNITS, count: num(a[8]), chunks: num(a[9]), hash: num(a[10]),
                      notes: [], got: 0 };
            pending[t.key] = t;
            routes[t.key] = { key: t.key, port: t.port, channel: t.channel, part: t.part, clip: t.clip, main: t.main };
            if (t.chunks === 0)
                  queueClip(t);
            }
      else if (address === "/ms/notes") {
            var p = pending[str(a[1])];
            if (!p || p.gen !== num(a[0]))
                  return;
            for (var k = 3; k + 4 < a.length; k += 5)
                  p.notes.push({ pitch: num(a[k]), start_time: num(a[k + 1]) / UNITS, duration: num(a[k + 2]) / UNITS,
                                 velocity: num(a[k + 3]), mute: num(a[k + 4]) ? 1 : 0 });
            if (++p.got === p.chunks)
                  queueClip(p);
            }
      else if (address === "/ms/clear") {
            work.push({ kind: "clear", key: str(a[1]), port: str(a[2]), channel: num(a[3]), part: str(a[4]), clip: str(a[5]),
                        main: true });
            if (paramTracks[str(a[1])])                         // (its parameters released, nothing answered)
                  queueParams({ gen: num(a[0]), key: str(a[1]), lanes: 0, hash: 0, got: {}, done: 0, silent: true });
            }
      else if (address === "/ms/params") {
            var q = { gen: num(a[0]), key: str(a[1]), lanes: Math.max(0, num(a[2])), hash: num(a[3]), got: {}, done: 0 };
            pendingParams[q.key] = q;
            if (q.lanes === 0)
                  queueParams(q);
            }
      else if (address === "/ms/pvals")
            paramValues(a);
      else if (address === "/ms/probe")
            probe(num(a[0]), str(a[1]));
      else if (address === "/ms/probecall")
            probeCall(num(a[0]), str(a[1]), str(a[2]), a.slice(3));
      else if (address === "/ms/play") {
            var song = new LiveAPI("live_set");
            song.set("current_song_time", Math.max(0, num(a[0])));
            if (!num(song.get("is_playing")))
                  song.call("continue_playing");
            }
      else if (address === "/ms/stop")
            new LiveAPI("live_set").call("stop_playing");
      else if (address === "/ms/params/ask") {        // (MuseScore started again: every track's parameters, next second)
            for (var pk in placed)
                  placed[pk].devs = null;
            for (var ek in edits)
                  edits[ek].devs = null;
            }
      else if (address === "/ms/clip/edit")
            work.push({ kind: "edit" });
      else if (address === "/ms/clip/write") {
            var e = edits[str(a[0])];
            if (!e)
                  return send("/live/clip/written", str(a[0]), num(a[1]), "gone", 0);
            if (num(a[1]) === e.lastWrite && e.reply) {           // sent again: applied once, answered again
                  send.apply(this, e.reply);
                  return;
                  }
            e.incoming = { write: num(a[1]), count: num(a[2]), chunks: num(a[3]), got: 0, ops: [] };
            if (e.incoming.chunks === 0)
                  queueWrite(e);
            }
      else if (address === "/ms/clip/ops") {
            var ed = edits[str(a[0])];
            if (!ed || !ed.incoming || ed.incoming.write !== num(a[1]))
                  return;
            for (var j = 3; j + 7 < a.length; j += 8)
                  ed.incoming.ops.push({ op: num(a[j]), id: num(a[j + 1]), mask: num(a[j + 2]), pitch: num(a[j + 3]),
                                         start: num(a[j + 4]), duration: num(a[j + 5]), velocity: num(a[j + 6]),
                                         mute: num(a[j + 7]) });
            if (++ed.incoming.got === ed.incoming.chunks)
                  queueWrite(ed);
            }
      else if (address === "/ms/clip/reload") {
            if (edits[str(a[0])])
                  work.push({ kind: "reload", key: str(a[0]) });
            }
      else if (address === "/ms/clip/close")
            delete edits[str(a[0])];
      else if (address === "/ms/clip/adopt")
            adoptEdit(str(a[0]), num(a[1]));
      else if (address === "/ms/cliptab/audible") {
            if (num(a[0]))
                  makeAudible(num(a[1]));
            else {
                  var au = audibleState();
                  if (au && au.track === num(a[1]))
                        restoreAudible();
                  }
            }
      else if (address === "/ms/midi")            // (only with an older patcher: its [route /ms/midi] passes them on)
            messnamed("msl_m" + num(a[0]), num(a[1]), num(a[2]), num(a[3]));
      // what is kept in the set: the MuseScore Envelopes script's answers (it sends to this port too)
      else if (address === "/live/keep/pong") {
            var fresh = !scriptAlive();
            g.keepAlive = now();
            if (fresh || !keepAsked)                  // (the script came: what it keeps of this set)
                  askKept();
            }
      else if (address === "/live/keep/data")
            keptData(a);
      else if (address === "/live/keep/end") {
            if (str(a[0]) === "vel") {
                  velReady = true;
                  velAnswerLate(true);
                  }
            }
      // velocity curves of clip tabs (protocol 7)
      else if (address === "/ms/vel/set") {
            var vin = velIncoming[str(a[0])];
            if (!vin || vin.serial !== num(a[1]))
                  vin = velIncoming[str(a[0])] = { serial: num(a[1]), chunks: num(a[3]), parts: {}, n: 0 };
            if (vin.parts[num(a[2])] === undefined)
                  ++vin.n;
            vin.parts[num(a[2])] = a.slice(4);
            if (vin.n >= vin.chunks) {
                  delete velIncoming[str(a[0])];
                  var all = [];
                  for (var vc = 0; vc < vin.chunks; ++vc)
                        all = all.concat(vin.parts[vc] || []);
                  velSet(str(a[0]), vin.serial, all);
                  }
            }
      else if (address === "/ms/vel/ask") {
            if (scriptAlive() && !velReady)
                  velAsks.push({ key: str(a[0]), since: now() });     // (answered once the script gave the set's)
            else
                  velAnswer(str(a[0]));
            }
      }

// a packet of one lane's events: a chunk sent again replaces itself; another gen's are dropped
function paramValues(a) {
      var q = pendingParams[str(a[1])];
      var lane = num(a[2]);
      if (!q || q.gen !== num(a[0]) || !(lane >= 0 && lane < q.lanes))
            return;
      var l = q.got[lane];
      if (!l)
            l = q.got[lane] = { title: str(a[3]), pid: num(a[4]), chunks: num(a[6]), parts: {}, n: 0, complete: false };
      var c = num(a[5]);
      if (l.chunks > 0 && !(c >= 0 && c < l.chunks))
            return;
      var ev = [];
      for (var k = 7; k + 1 < a.length; k += 2)
            ev.push(num(a[k]), num(a[k + 1]));
      if (!l.parts[c])
            ++l.n;
      l.parts[c] = ev;
      if (!l.complete && l.n >= l.chunks) {
            l.complete = true;
            if (++q.done === q.lanes)
                  queueParams(q);
            }
      }

function queueParams(q) {
      delete pendingParams[q.key];
      var lanes = [];
      for (var i = 0; i < q.lanes; ++i) {
            var l = q.got[i], ev = [];
            for (var c = 0; c < Math.max(1, l.chunks); ++c)
                  ev = ev.concat(l.parts[c] || []);
            lanes.push({ title: l.title, pid: l.pid, ev: ev });
            }
      var w = { kind: "params", key: q.key, hash: q.hash, lanes: lanes, silent: !!q.silent };
      for (i = 0; i < work.length; ++i)
            if (work[i].kind === "params" && work[i].key === q.key) {
                  work[i] = w;
                  return;
                  }
      work.push(w);
      }

function queueWrite(e) {
      var w = e.incoming;
      e.incoming = null;
      work.push({ kind: "write", key: e.key, write: w });
      }

function queueClip(t) {
      delete pending[t.key];
      // a newer version of the same clip replaces one still waiting
      for (var i = 0; i < work.length; ++i)
            if (work[i].kind === "clip" && work[i].key === t.key) {
                  work[i] = { kind: "clip", key: t.key, clip: t };
                  return;
                  }
      work.push({ kind: "clip", key: t.key, clip: t });
      }

function queueSong() {
      var s = pendingSong;
      pendingSong = null;
      for (var i = 0; i < work.length; ++i)
            if (work[i].kind === "song") {
                  work[i] = { kind: "song", song: s };
                  return;
                  }
      work.unshift({ kind: "song", song: s });
      }

// one piece of work a turn (the Live API is slow: UDP keeps flowing between)
function workStep() {
      if (g.editRequest && g.editRequest !== lastEditRequest) {     // (a button on another copy)
            lastEditRequest = g.editRequest;
            if (isHub)
                  work.push({ kind: "edit" });
            }
      pollParams();
      if (!work.length)
            return;
      var w = work.shift();
      try {
            if (w.kind === "clip")
                  writeClip(w.clip);
            else if (w.kind === "clear")
                  clearClip(w);
            else if (w.kind === "song") {
                  if (!writeSong(w.song))
                        work.push(w);           // (Live is playing: the locators wait)
                  }
            else if (w.kind === "mode")
                  applyMode();
            else if (w.kind === "edit")
                  startEdit();
            else if (w.kind === "reload")
                  sendClip(edits[w.key]);
            else if (w.kind === "write")
                  applyWrite(edits[w.key], w.write);
            else if (w.kind === "params")
                  writeParams(w);
            }
      catch (e) {
            if (w.kind === "clip")
                  send("/live/applied", w.clip.key, w.clip.hash, "error: " + e, "");
            else if (w.kind === "write")
                  send("/live/clip/written", w.key, w.write.write, "error: " + e, 0);
            else if (w.kind === "params" && !w.silent)
                  send("/live/papplied", w.key, w.hash, "error: " + e, "");
            post("MuseScore Link: " + e + "\n");
            }
      }

//---------------------------------------------------------
//   tracks and clips
//---------------------------------------------------------

function tracks() {
      var song = new LiveAPI("live_set");
      var n = song.getcount("tracks");
      var out = [];
      for (var i = 0; i < n; ++i)
            out.push(new LiveAPI("live_set tracks " + i));
      return out;
      }

// the track that plays a part: MIDI From = its port and channel, else (main patch) its name
function findTrack(t) {
      var all = tracks();
      var i;
      for (i = 0; i < all.length; ++i) {
            var tr = all[i];
            if (num(tr.get("has_midi_input")) !== 1)
                  continue;
            if (!t.port)
                  break;
            var type = displayName(tr.get("input_routing_type"));
            var ch = displayName(tr.get("input_routing_channel"));
            var lt = loosePort(type), lp = loosePort(t.port);
            if (lt && lp && (lt === lp || lt.indexOf(lp) === 0) && loose(ch) === loose("Ch. " + t.channel))
                  return { api: tr, how: "MIDI input" };
            }
      // by name: a main patch's track is named after the part; another patch's (an extra, such as the
      // Performance legato, or a copy for another tuning) after its clip, "<part> – <patch>", or after the
      // patch alone when only one track has that name
      var names = [];
      if (t.main)
            names.push(loose(t.part));
      else {
            var full = str(t.clip).replace(/^MuseScore:\s*/, "");
            names.push(loose(full));
            }
      var midi = [];
      for (i = 0; i < all.length; ++i)
            if (num(all[i].get("has_midi_input")) === 1)
                  midi.push(all[i]);
      for (i = 0; i < midi.length; ++i)
            if (names.indexOf(loose(midi[i].get("name"))) >= 0)
                  return { api: midi[i], how: "name" };
      if (!t.main) {
            var dash = str(t.clip).indexOf(" – ");
            var patch = dash >= 0 ? loose(str(t.clip).substring(dash + 3)) : "";
            var hits = [];
            for (i = 0; patch && i < midi.length; ++i)
                  if (loose(midi[i].get("name")) === patch)
                        hits.push(midi[i]);
            if (hits.length === 1)
                  return { api: hits[0], how: "patch name" };
            }
      return null;
      }

function clipsOf(tr) {
      var out = [];
      var l = ids(tr.get("arrangement_clips"));
      for (var i = 0; i < l.length; ++i) {
            var c = new LiveAPI("id " + l[i]);
            out.push({ id: l[i], api: c, name: str(c.get("name")), start: num(c.get("start_time")), end: num(c.get("end_time")) });
            }
      return out;
      }

// is a MuseScore Link device on the track, before the instrument?
function deviceCheck(tr) {
      var mine = [];
      var r = registry();
      for (var k in r)
            if (r[k].track === num(tr.id) && now() - r[k].beat < HUB_STALE_MS)
                  mine.push(r[k].device);
      if (!mine.length)
            return "no MuseScore Link device on the track (its controllers would reach the instrument as notes)";
      var devs = ids(tr.get("devices"));
      var link = -1, instrument = -1;
      for (var i = 0; i < devs.length; ++i) {
            if (mine.indexOf(devs[i]) >= 0 && link < 0)
                  link = i;
            if (instrument < 0 && str(new LiveAPI("id " + devs[i]).get("class_name")) === "PluginDevice")
                  instrument = i;
            }
      if (link >= 0 && instrument >= 0 && link > instrument)
            return "the MuseScore Link device is after the instrument: move it before";
      return "ok";
      }

function setMonitor(tr) {
      try {
            tr.set("current_monitoring_state", mode === "clips" ? 1 : 0);     // 1: Auto, 0: In
            }
      catch (e) {}
      }

function writeClip(t) {
      var found = findTrack(t);
      if (!found) {
            send("/live/applied", t.key, t.hash, "no track (MIDI From " + t.port + " / Ch. " + t.channel
                 + ", or a track named " + (t.main ? t.part : str(t.clip).replace(/^MuseScore:\s*/, "")) + ")", "");
            return;
            }
      var tr = found.api;
      var trackName = str(tr.get("name"));
      var clips = clipsOf(tr);
      var ours = null, i;
      for (i = 0; i < clips.length; ++i)
            if (clips[i].name === t.clip)
                  ours = clips[i];
      var fits = ours && Math.abs(ours.start) < 1e-6 && Math.abs(ours.end - t.length) < 1e-6;
      if (!fits) {
            for (i = 0; i < clips.length; ++i)
                  if (clips[i] !== ours && clips[i].start < t.length && clips[i].end > 0) {
                        send("/live/applied", t.key, t.hash, "other clips on track " + trackName + " (the clip isn't made over them)",
                             trackName);
                        return;
                        }
            if (ours)
                  tr.call("delete_clip", "id", ours.id);
            var before = ids(tr.get("arrangement_clips"));
            tr.call("create_midi_clip", 0, t.length);
            var after = ids(tr.get("arrangement_clips"));
            var id = 0;
            for (i = 0; i < after.length; ++i)
                  if (before.indexOf(after[i]) < 0)
                        id = after[i];
            if (!id) {
                  send("/live/applied", t.key, t.hash, "error: Live made no clip (a frozen track? Live 12.1.10 or later needed)",
                       trackName);
                  return;
                  }
            ours = { id: id, api: new LiveAPI("id " + id) };
            ours.api.set("name", t.clip);
            carriersChanged(num(tr.id));
            }
      var c = ours.api;
      c.set("muted", 0);
      c.call("remove_notes_extended", 0, 128, 0, t.length + 1);
      for (i = 0; i < t.notes.length; i += BATCH)
            c.call("add_new_notes", { notes: t.notes.slice(i, i + BATCH) });
      setMonitor(tr);
      var was = placed[t.key];
      placed[t.key] = { track: num(tr.id), clip: t.clip, devs: was && was.track === num(tr.id) ? was.devs : null };
      send("/live/applied", t.key, t.hash, deviceCheck(tr), trackName);
      sendParams(placed[t.key], t.key, tr);
      status("MuseScore Link: hub; last clip " + t.clip + " on " + trackName);
      }

function clearClip(w) {
      var found = findTrack(w);
      if (!found)
            return;
      var clips = clipsOf(found.api);
      for (var i = 0; i < clips.length; ++i)
            if (clips[i].name === w.clip)
                  found.api.call("delete_clip", "id", clips[i].id);
      delete placed[w.key];
      carriersChanged(num(found.api.id));
      }

function applyMode() {
      for (var k in placed) {
            var tr = new LiveAPI("id " + placed[k].track);
            if (num(tr.id) > 0)
                  setMonitor(tr);
            }
      }

//---------------------------------------------------------
//   tempo and locators
//---------------------------------------------------------

function writeSong(s) {
      var song = new LiveAPI("live_set");
      if (Math.abs(num(song.get("tempo")) - s.bpm) > 1e-4)
            song.set("tempo", s.bpm);
      if (num(song.get("is_playing")))
            return false;
      var back = num(song.get("current_song_time"));
      var cueList = function() {
            var l = ids(song.get("cue_points"));
            var out = [];
            for (var i = 0; i < l.length; ++i) {
                  var c = new LiveAPI("id " + l[i]);
                  out.push({ id: l[i], api: c, time: num(c.get("time")), name: str(c.get("name")) });
                  }
            return out;
            };
      var managed = function(name) { return /^MS \d/.test(name); };
      var toggleAt = function(time) {
            song.set("current_song_time", time);
            song.call("set_or_delete_cue");
            };
      var want = {};
      for (var i = 0; i < s.cues.length; ++i)
            want[s.cues[i].time.toFixed(6)] = s.cues[i].name;
      var have = cueList();
      var at = {};
      for (i = 0; i < have.length; ++i) {
            var key = have[i].time.toFixed(6);
            if (managed(have[i].name) && !(key in want))
                  toggleAt(have[i].time);             // ours, not wanted: goes
            else
                  at[key] = have[i];
            }
      for (i = 0; i < s.cues.length; ++i) {
            var k2 = s.cues[i].time.toFixed(6);
            var there = at[k2];
            if (there) {
                  if (managed(there.name) && there.name !== s.cues[i].name)
                        there.api.set("name", s.cues[i].name);
                  continue;                           // (a locator of the owner's there: left)
                  }
            toggleAt(s.cues[i].time);
            var now2 = cueList();
            for (var j = 0; j < now2.length; ++j)
                  if (Math.abs(now2[j].time - s.cues[i].time) < 1e-6 && !managed(now2[j].name))
                        now2[j].api.set("name", s.cues[i].name);
            }
      song.set("current_song_time", back);
      send("/live/applied", "song", s.hash, "ok", "");
      return true;
      }

//---------------------------------------------------------
//   Live's transport, for MuseScore to follow
//---------------------------------------------------------

function report() {
      var song = new LiveAPI("live_set");
      var playing = num(song.get("is_playing")) ? 1 : 0;
      var b = num(song.get("current_song_time"));
      var bpm = num(song.get("tempo"));
      var t = now();
      // (Live's tempo changed: at once, also while stopped, for the clip tabs that follow it; protocol 6)
      if (playing || playing !== lastTransport.playing || Math.abs(b - lastTransport.beat) > 1e-6 || t - lastTransport.sent > 1000
          || bpm !== lastTransport.bpm) {
            send("/live/transport", playing, b, bpm);
            lastTransport = { playing: playing, beat: b, sent: t, bpm: bpm };
            }
      }

//---------------------------------------------------------
//   editing a clip in MuseScore
//---------------------------------------------------------

var FIELDS = ["note_id", "pitch", "start_time", "duration", "velocity", "mute", "probability", "velocity_deviation",
              "release_velocity"];
var TICKS = 480;                        // MuseScore's ticks a beat (the edits' times)
var CLIP_NOTES_PER_PACKET = 24;         // LiveClipEdit::NOTES_PER_PACKET

// a LOM call's dictionary: a JSON string, maybe in an array, or already an object
function dict(v) {
      if (v && typeof v === "object" && !Array.isArray(v))
            return v;
      var s = Array.isArray(v) ? v.join(" ") : str(v);
      try { return JSON.parse(s); } catch (e) { return null; }
      }

function readNotes(clip) {
      var d = dict(clip.call("get_all_notes_extended"));
      return d && Array.isArray(d.notes) ? d.notes : [];
      }

// the clip's notes, whatever their order (FNV-1a over every field, by id)
function hashNotes(notes) {
      var l = notes.slice().sort(function(a, b) { return a.note_id - b.note_id; });
      var h = 0x811c9dc5;
      for (var i = 0; i < l.length; ++i) {
            var s = "";
            for (var k = 0; k < FIELDS.length; ++k)
                  s += String(l[i][FIELDS[k]]) + ",";
            for (var c = 0; c < s.length; ++c) {
                  h ^= s.charCodeAt(c);
                  h = (h + ((h << 1) + (h << 4) + (h << 7) + (h << 8) + (h << 24))) >>> 0;
                  }
            }
      return h | 0;
      }

// in the Arrangement View, the selected track's arrangement clip under the insert marker (the playhead while playing):
// a click on a clip selects its track and puts the marker there, while the Detail View keeps showing the clip it last
// showed (the owner, 2026-10-04: a track's second clip clicked, Edit in MuseScore opened its first)
function arrangementClip() {
      var app = new LiveAPI("live_app view");
      if (num(app.id) > 0 && str(app.get("focused_document_view")) === "Session")
            return null;
      var tr = new LiveAPI("live_set view selected_track");
      if (!(num(tr.id) > 0))
            return null;
      var t = num(new LiveAPI("live_set").get("current_song_time"));
      var l = ids(tr.get("arrangement_clips"));
      for (var i = 0; i < l.length; ++i) {
            var c = new LiveAPI("id " + l[i]);
            if (num(c.get("start_time")) <= t && t < num(c.get("end_time")))
                  return c;
            }
      return null;
      }

// the clip to edit: in the Arrangement View the clicked one (arrangementClip), else the clip shown in the Detail View
// (an arrangement or a session clip), else the highlighted session slot's
function selectedClip() {
      var a = arrangementClip();
      if (a)
            return a;
      var c = new LiveAPI("live_set view detail_clip");
      if (num(c.id) > 0)
            return c;
      var slot = new LiveAPI("live_set view highlighted_clip_slot");
      if (num(slot.id) > 0 && num(slot.get("has_clip"))) {
            var l = ids(slot.get("clip"));
            if (l.length)
                  return new LiveAPI("id " + l[0]);
            }
      return null;
      }

// the clip's track (an arrangement clip's parent; a session clip's slot's parent)
function trackOf(clip) {
      var p = ids(clip.get("canonical_parent"));
      if (!p.length)
            return null;
      var o = new LiveAPI("id " + p[0]);
      if (str(o.type) === "ClipSlot") {
            p = ids(o.get("canonical_parent"));
            o = p.length ? new LiveAPI("id " + p[0]) : null;
            }
      return o;
      }

// a drum clip: a Drum Rack on its track, or a drum-like track name
function isDrums(track, name) {
      if (track) {
            var devs = ids(track.get("devices"));
            for (var i = 0; i < devs.length; ++i)
                  if (str(new LiveAPI("id " + devs[i]).get("class_name")) === "DrumGroupDevice")
                        return true;
            }
      return /drum|kit|perc|beat/i.test(name);
      }

function startEdit() {
      var clip = selectedClip();
      if (!clip) {
            status("MuseScore Link: select a MIDI clip first (its notes shown in the Clip View), then Edit in MuseScore");
            return;
            }
      if (!num(clip.get("is_midi_clip"))) {
            status("MuseScore Link: that is an audio clip; only MIDI clips can be edited in MuseScore");
            return;
            }
      var key = "c" + num(clip.id);
      var e = edits[key];
      if (!e) {
            e = edits[key] = { key: key, clipId: num(clip.id), notes: [], hash: 0, conflict: false, lastWrite: 0, reply: null,
                               incoming: null };
            }
      sendClip(e);
      }

// the clip to MuseScore (a new edit, the button again, or a reload after a conflict)
function sendClip(e) {
      if (!e)
            return;
      var clip = new LiveAPI("id " + e.clipId);
      if (!(num(clip.id) > 0)) {
            send("/live/clip/gone", e.key);
            delete edits[e.key];
            return;
            }
      var tr = trackOf(clip);
      var trackName = tr ? str(tr.get("name")) : "";
      var clipName = str(clip.get("name"));
      var song = new LiveAPI("live_set");
      var notes = readNotes(clip);
      e.notes = notes;
      e.hash = hashNotes(notes);
      e.conflict = false;
      e.gen = ++editSerial;
      var num_ = num(clip.get("signature_numerator")) || num(song.get("signature_numerator")) || 4;
      var den = num(clip.get("signature_denominator")) || num(song.get("signature_denominator")) || 4;
      var end = Math.max(num(clip.get("end_marker")), num(clip.get("loop_end")));
      var chunks = Math.ceil(notes.length / CLIP_NOTES_PER_PACKET);
      send("/live/clip/begin", e.key, e.gen, trackName, clipName, isDrums(tr, trackName) ? 1 : 0, num(song.get("tempo")),
           num_, den, end, num(clip.get("loop_start")), num(clip.get("loop_end")), num(clip.get("looping")) ? 1 : 0,
           notes.length, chunks, e.hash);
      for (var c = 0; c < chunks; ++c) {
            var args = ["/live/clip/notes", e.key, e.gen, c];
            var part = notes.slice(c * CLIP_NOTES_PER_PACKET, (c + 1) * CLIP_NOTES_PER_PACKET);
            for (var i = 0; i < part.length; ++i) {
                  var n = part[i];
                  args.push(num(n.note_id), num(n.pitch), num(n.start_time), num(n.duration), num(n.velocity),
                            num(n.mute) ? 1 : 0, n.probability === undefined ? 1 : num(n.probability),
                            num(n.velocity_deviation || 0), n.release_velocity === undefined ? 64 : num(n.release_velocity));
                  }
            send.apply(this, args);
            }
      e.trackId = tr ? num(tr.id) : 0;
      e.trackName = trackName;
      e.copy = copyOn(e.trackId);
      send("/live/clip/track", e.key, e.trackId, e.copy ? 1 : 0, trackName);
      sendWhere(e, clip, tr, true);
      e.devs = null;
      sendParams(e, e.key, tr);
      status("MuseScore Link: " + clipName + " (" + trackName + ") sent to MuseScore");
      }

// a MuseScore Link copy of protocol 4+ (it plays MuseScore's notes) on the track, alive
function copyOn(trackId) {
      if (!trackId)
            return false;
      var r = registry();
      for (var k in r)
            if (r[k].track === trackId && (r[k].protocol || 0) >= 4 && now() - r[k].beat < HUB_STALE_MS)
                  return true;
      return false;
      }

//---------------------------------------------------------
//   a clip tab's track audible while MuseScore plays through it (the owner, 2026-10-03, option A: "as long as it
//   returns to the previous state after MuseScore stops playing")
//   /ms/cliptab/audible 1 <track id>: at MuseScore's Play and each second while it plays. The hub un-mutes the track
//   (mute is also the Track Activator) and the group tracks it is in, and, when another track (or a return track)
//   is soloed and neither this track nor a group it is in is, solos this track. Each property changed is kept, with
//   the value it had and the value set, in the Global (g.audible: a new hub can put it back too).
//   /ms/cliptab/audible 0 <track id> (MuseScore's Stop, the tab closed or routed elsewhere, MuseScore quitting), no
//   heartbeat for AUDIBLE_STALE_MS (MuseScore gone, the link lost), the track's copy gone or the hub deleted: each
//   property put back to the value it had, only where it still has the value set (one the user changed meanwhile is
//   left as the user set it). Live's transport is never touched.
//   Solo: Live's "Exclusive Solo" preference is applied by its control surfaces (Ableton's own Remote Scripts un-solo
//   the other tracks themselves when song.exclusive_solo is on), not by the LOM's solo setter, so setting solo here
//   leaves the others soloed. Should Live un-solo any all the same, each is kept as changed (1 -> 0) and soloed again
//   when MuseScore stops. Not yet checked in Live 12.4.6 itself (LIVE.md › Clip tabs play through Live).
//---------------------------------------------------------

var AUDIBLE_STALE_MS = 7000;                // (measured: HUB_STALE_MS)

function audibleState() {
      try { return g.audible ? JSON.parse(g.audible) : null; } catch (e) { return null; }
      }
function saveAudible(a) { g.audible = a ? JSON.stringify(a) : ""; }

function lomValue(id, prop) {
      var o = new LiveAPI("id " + id);
      return num(o.id) > 0 ? num(o.get(prop)) : NaN;
      }

// the track's group tracks, the innermost first (Track.group_track; 0 or none: not in a group)
function groupsOf(trackId) {
      var out = [];
      var id = trackId;
      for (var n = 0; n < 32; ++n) {
            var t = new LiveAPI("id " + id);
            if (!(num(t.id) > 0))
                  break;
            var gid = ids(t.get("group_track"))[0];
            if (!gid || out.indexOf(gid) >= 0)
                  break;
            out.push(gid);
            id = gid;
            }
      return out;
      }

// the soloed tracks and return tracks
function soloedTracks() {
      var song = new LiveAPI("live_set");
      var all = ids(song.get("tracks")).concat(ids(song.get("return_tracks")));
      var out = [];
      for (var i = 0; i < all.length; ++i)
            if (lomValue(all[i], "solo") === 1)
                  out.push(all[i]);
      return out;
      }

function makeAudible(trackId) {
      var a = audibleState();
      if (a && a.track === trackId) {               // (the heartbeat: nothing set again, a change the user makes stays)
            a.beat = now();
            saveAudible(a);
            return;
            }
      if (a)
            restoreAudible();
      if (!copyOn(trackId))                          // (MuseScore plays nothing there)
            return;
      var changes = [];
      function change(id, prop, v) {
            var o = new LiveAPI("id " + id);
            if (!(num(o.id) > 0))
                  return;
            var before = num(o.get(prop));
            if (before === v)
                  return;
            o.set(prop, v);
            changes.push({ id: id, prop: prop, before: before, set: v });
            }
      var chain = [trackId].concat(groupsOf(trackId));
      for (var i = 0; i < chain.length; ++i)
            change(chain[i], "mute", 0);
      var soloed = soloedTracks();
      var chainSoloed = false;
      for (var j = 0; j < chain.length; ++j)
            if (soloed.indexOf(chain[j]) >= 0)
                  chainSoloed = true;
      if (soloed.length && !chainSoloed) {
            change(trackId, "solo", 1);
            for (var k = 0; k < soloed.length; ++k)  // (un-soloed by Live after all: soloed again at the end)
                  if (lomValue(soloed[k], "solo") === 0)
                        changes.push({ id: soloed[k], prop: "solo", before: 1, set: 0 });
            }
      saveAudible({ track: trackId, beat: now(), changes: changes });
      status("MuseScore Link: MuseScore plays through track " + trackId + " (" + changes.length
             + " mute/solo change(s), put back at Stop)");
      }

function restoreAudible() {
      var a = audibleState();
      if (!a)
            return;
      saveAudible(null);
      for (var i = 0; i < a.changes.length; ++i) {
            var c = a.changes[i];
            try {
                  var o = new LiveAPI("id " + c.id);
                  if (num(o.id) > 0 && num(o.get(c.prop)) === c.set)
                        o.set(c.prop, c.before);
                  }
            catch (e) {}
            }
      status("MuseScore Link: track " + a.track + "'s mute and solo as they were");
      }

// the hub, each second: MuseScore silent too long, or the track's copy gone
function checkAudible() {
      var a = audibleState();
      if (a && (now() - a.beat > AUDIBLE_STALE_MS || !copyOn(a.track)))
            restoreAudible();
      }

// a new hub takes over a clip MuseScore edits (the copy that was the hub went): hash as MuseScore knows the notes
function adoptEdit(key, hash) {
      var e = edits[key];
      if (!e) {
            var id = Number(key.replace(/^c/, ""));
            var clip = id > 0 ? new LiveAPI("id " + id) : null;
            if (!clip || !(num(clip.id) > 0)) {
                  send("/live/clip/gone", key);
                  return;
                  }
            e = edits[key] = { key: key, clipId: id, notes: [], hash: 0, conflict: false, lastWrite: 0, reply: null,
                               incoming: null };
            e.notes = readNotes(clip);
            e.hash = hashNotes(e.notes);
            var tr = trackOf(clip);
            e.trackId = tr ? num(tr.id) : 0;
            e.trackName = tr ? str(tr.get("name")) : "";
            }
      if (e.hash !== hash) {
            e.conflict = true;
            send("/live/clip/conflict", key, e.hash);
            }
      e.copy = copyOn(e.trackId);
      send("/live/clip/track", key, e.trackId, e.copy ? 1 : 0, e.trackName);
      }

function reply(e, args) {
      e.reply = ["/live/clip/written"].concat(args);
      send.apply(this, e.reply);
      }

function applyWrite(e, w) {
      if (!e || !w)
            return;
      e.lastWrite = w.write;
      var clip = new LiveAPI("id " + e.clipId);
      if (!(num(clip.id) > 0)) {
            reply(e, [e.key, w.write, "gone", 0]);
            delete edits[e.key];
            return;
            }
      var before = readNotes(clip);
      if (e.conflict || hashNotes(before) !== e.hash) {
            e.conflict = true;
            reply(e, [e.key, w.write, "conflict", hashNotes(before)]);
            return;
            }
      var byId = {};
      for (var i = 0; i < before.length; ++i)
            byId[before[i].note_id] = before[i];
      var removes = [], mods = [], adds = [];
      for (i = 0; i < w.ops.length; ++i) {
            var o = w.ops[i];
            if (o.op === 1) {
                  if (byId[o.id])
                        removes.push(o.id);
                  }
            else if (o.op === 0) {
                  var b = byId[o.id];
                  if (!b)
                        continue;
                  var n = {};
                  for (var k in b)                // Live's own note: only what was edited changes
                        n[k] = b[k];
                  if (o.mask & 1) n.pitch = o.pitch;
                  if (o.mask & 2) n.start_time = o.start / TICKS;
                  if (o.mask & 4) n.duration = o.duration / TICKS;
                  if (o.mask & 8) n.velocity = o.velocity;
                  if (o.mask & 16) n.mute = o.mute ? 1 : 0;
                  mods.push(n);
                  }
            else if (o.op === 2)
                  adds.push({ pitch: o.pitch, start_time: o.start / TICKS, duration: o.duration / TICKS, velocity: o.velocity,
                              mute: o.mute ? 1 : 0 });
            }
      if (removes.length)
            clip.call.apply(clip, ["remove_notes_by_id"].concat(removes));
      if (mods.length)
            clip.call("apply_note_modifications", { notes: mods });
      var added = [];
      if (adds.length) {
            var r = clip.call("add_new_notes", { notes: adds });
            var l = Array.isArray(r) ? r : (dict(r) && dict(r).note_ids) || str(r).replace(/[\[\],]/g, " ").split(/\s+/);
            for (i = 0; i < l.length; ++i)
                  if (str(l[i]) !== "" && !isNaN(Number(l[i])))
                        added.push(Number(l[i]));
            }
      var after = readNotes(clip);
      if (added.length !== adds.length) {         // (no ids returned: the new notes, matched to what was added)
            added = [];
            var had = {};
            for (i = 0; i < before.length; ++i)
                  had[before[i].note_id] = true;
            var fresh = after.filter(function(x) { return !had[x.note_id]; });
            for (i = 0; i < adds.length; ++i) {
                  var best = -1, bd = 1e9;
                  for (var j = 0; j < fresh.length; ++j) {
                        if (fresh[j].pitch !== adds[i].pitch)
                              continue;
                        var dd = Math.abs(fresh[j].start_time - adds[i].start_time) + Math.abs(fresh[j].duration - adds[i].duration);
                        if (dd < bd) {
                              bd = dd;
                              best = j;
                              }
                        }
                  if (best >= 0) {
                        added.push(fresh[best].note_id);
                        fresh.splice(best, 1);
                        }
                  }
            }
      e.notes = after;
      e.hash = hashNotes(after);
      reply(e, [e.key, w.write, "ok", e.hash].concat(added));
      status("MuseScore Link: " + (removes.length + mods.length + adds.length) + " note change(s) from MuseScore written");
      }

// once a second: a clip edited in MuseScore changed in Live (or went)?
function checkEdits() {
      for (var key in edits) {
            var e = edits[key];
            var copy = copyOn(e.trackId);                 // (a copy added to or removed from its track)
            if (e.trackId && copy !== e.copy) {
                  e.copy = copy;
                  send("/live/clip/track", key, e.trackId, copy ? 1 : 0, e.trackName || "");
                  }
            if (e.conflict)
                  continue;
            var clip = new LiveAPI("id " + e.clipId);
            if (!(num(clip.id) > 0)) {
                  send("/live/clip/gone", key);
                  delete edits[key];
                  continue;
                  }
            var etr = trackOf(clip);
            sendWhere(e, clip, etr, false);
            sendParams(e, key, etr);
            var h = hashNotes(readNotes(clip));
            if (h !== e.hash) {
                  e.conflict = true;
                  send("/live/clip/conflict", key, h);
                  status("MuseScore Link: " + str(clip.get("name")) + " changed in Live: MuseScore stops writing to it");
                  }
            }
      }

//---------------------------------------------------------
//   the tracks' parameters and the clips' places (protocol 5)
//---------------------------------------------------------

var PARAMS_PER_PACKET = 16;             // about 0.8 kB a datagram

// the clip's place as Live's Python API finds it: the track's index, the session slot's (-1: an arrangement clip)
function whereOf(clip, tr) {
      var t = -1, s = -1;
      if (tr) {
            t = ids(new LiveAPI("live_set").get("tracks")).indexOf(num(tr.id));
            var p = ids(clip.get("canonical_parent"));
            if (p.length && str(new LiveAPI("id " + p[0]).type) === "ClipSlot")
                  s = ids(tr.get("clip_slots")).indexOf(p[0]);
            }
      return { track: t, slot: s };
      }

function sendWhere(e, clip, tr, always) {
      var w = whereOf(clip, tr);
      if (always || !e.where || e.where.track !== w.track || e.where.slot !== w.slot) {
            e.where = w;
            send("/live/clip/where", e.key, w.track, w.slot);
            always = true;
            }
      // an arrangement clip's place in the song (protocol 6)
      if (w.slot >= 0 || w.track < 0)
            return;
      var sp = spanOf(clip);
      var k = sp.join(" ");
      if (!always && e.span === k)
            return;
      e.span = k;
      send.apply(this, ["/live/clip/span", e.key].concat(sp));
      }

// start_time, end_time (song beats), start_marker, end_marker, loop_start, loop_end (clip beats), looping
function spanOf(clip) {
      return [num(clip.get("start_time")), num(clip.get("end_time")), num(clip.get("start_marker")),
              num(clip.get("end_marker")), num(clip.get("loop_start")), num(clip.get("loop_end")),
              num(clip.get("looping")) ? 1 : 0];
      }

function isLink(dev) {
      return /^Mx/.test(str(dev.get("class_name"))) && /^MuseScore Link/.test(str(dev.get("name")));
      }

// what a lane can automate on the track: the mixer's volume and pan, then every device's parameters (the device's
// own name before the parameter's), MuseScore Link's left out
function trackParams(tr) {
      var out = [{ d: -1, p: 0, name: "Mixer › Volume", min: 0, max: 1, q: 0 },
                 { d: -1, p: 1, name: "Mixer › Pan", min: -1, max: 1, q: 0 }];
      var devs = ids(tr.get("devices"));
      for (var i = 0; i < devs.length; ++i) {
            var dv = new LiveAPI("id " + devs[i]);
            if (isLink(dv))
                  continue;
            var dn = str(dv.get("name"));
            var pl = ids(dv.get("parameters"));
            for (var j = 0; j < pl.length; ++j) {
                  var pa = new LiveAPI("id " + pl[j]);
                  out.push({ d: i, p: j, name: dn + " › " + str(pa.get("name")), min: num(pa.get("min")) || 0,
                             max: num(pa.get("max")), q: num(pa.get("is_quantized")) ? 1 : 0 });
                  }
            }
      return out;
      }

function hashParams(list) {
      var h = 2166136261;
      var text = "";
      for (var i = 0; i < list.length; ++i)
            text += list[i].d + "/" + list[i].p + "/" + list[i].name + "/" + list[i].min + "/" + list[i].max + "/" + list[i].q + ";";
      for (var k = 0; k < text.length; ++k) {
            h ^= text.charCodeAt(k) & 0xff;
            h = Math.imul ? (Math.imul(h, 16777619) >>> 0) : ((h * 16777619) % 4294967296) >>> 0;
            }
      return h | 0;
      }

// the track's parameters to MuseScore (key: an edited clip's or a route's), when its devices changed since `holder`
// last sent them (holder.devs: the device ids then)
function sendParams(holder, key, tr) {
      var devs = tr ? ids(tr.get("devices")).join(",") : "-";
      if (holder.devs === devs)
            return;
      holder.devs = devs;
      var list = tr ? trackParams(tr) : [];
      var h = hashParams(list);
      var chunks = Math.max(1, Math.ceil(list.length / PARAMS_PER_PACKET));
      for (var c = 0; c < chunks; ++c) {
            var args = ["/live/params", key, h, c, chunks];
            var part = list.slice(c * PARAMS_PER_PACKET, (c + 1) * PARAMS_PER_PACKET);
            for (var i = 0; i < part.length; ++i)
                  args.push(part[i].d, part[i].p, part[i].name, part[i].min, part[i].max, part[i].q);
            send.apply(this, args);
            }
      }

// the routes' tracks: their devices changed (a device added in Live)
function checkRouteParams() {
      for (var k in placed) {
            var tr = new LiveAPI("id " + placed[k].track);
            if (num(tr.id) > 0)
                  sendParams(placed[k], k, tr);
            }
      }

// a lane titled "live:<d>/<p>": that parameter of the track (protocol 5), its LOM id (0: none)
function liveParam(tr, title) {
      var m = /^live:(-?\d+)\/(\d+)$/.exec(str(title));
      if (!m || !tr)
            return 0;
      var d = Number(m[1]), p = Number(m[2]);
      if (d < 0) {
            var mx = ids(tr.get("mixer_device"));
            if (!mx.length)
                  return 0;
            var x = ids(new LiveAPI("id " + mx[0]).get(p === 0 ? "volume" : "panning"));
            return x.length ? x[0] : 0;
            }
      var devs = ids(tr.get("devices"));
      if (d >= devs.length)
            return 0;
      var pl = ids(new LiveAPI("id " + devs[d]).get("parameters"));
      return p < pl.length ? pl[p] : 0;
      }

//---------------------------------------------------------
//   parameter lanes: the hub
//---------------------------------------------------------

function entry(track) {
      try { return JSON.parse(g["p" + track] || "null"); } catch (e) { return null; }
      }

// a route's lanes into its track's entry (null: out of it); the copies told. Returns the entry's serial
function putRoute(track, key, route) {
      var e = entry(track) || { routes: {} };
      if (route)
            e.routes[key] = route;
      else
            delete e.routes[key];
      g.pserial = (Number(g.pserial) || 0) + 1;
      e.serial = g.pserial;
      e.length = songBeats;
      g["p" + track] = JSON.stringify(e);
      if (typeof messnamed === "function")
            messnamed("msl_params", track, e.serial);
      return e.serial;
      }

function writeParams(w) {
      var tr = null;
      if (placed[w.key])
            tr = new LiveAPI("id " + placed[w.key].track);
      else if (routes[w.key]) {
            var found = findTrack(routes[w.key]);
            tr = found ? found.api : null;
            }
      var track = tr && num(tr.id) > 0 ? num(tr.id) : 0;
      var old = paramTracks[w.key];
      if (old && old !== track)
            putRoute(old, w.key, null);
      if (!track) {
            delete paramTracks[w.key];
            if (!w.silent)
                  send("/live/papplied", w.key, w.hash, w.lanes.length ? "no track (its clip's track isn't known)" : "ok", "");
            return;
            }
      var serial = putRoute(track, w.key, w.lanes.length ? { hash: w.hash, lanes: w.lanes } : null);
      if (w.lanes.length)
            paramTracks[w.key] = track;
      else
            delete paramTracks[w.key];
      if (!w.silent)
            waiting[w.key] = { track: track, serial: serial, hash: w.hash, name: str(tr.get("name")), since: now(), told: false };
      }

// the copies' answers (each copy writes "pr<track>"): /live/papplied
function pollParams() {
      for (var key in waiting) {
            var w = waiting[key];
            var r = null;
            try { r = JSON.parse(g["pr" + w.track] || "null"); } catch (e) {}
            if (r && r.serial >= w.serial) {
                  var res = r.results && r.results[key];
                  send("/live/papplied", key, w.hash, res ? res.status : "ok", w.name);
                  delete waiting[key];
                  }
            else if (!w.told && now() - w.since > NO_DEVICE_MS) {
                  w.told = true;
                  send("/live/papplied", key, w.hash, "no MuseScore Link device on the track (its parameters can't be driven)",
                       w.name);
                  }
            }
      }

//---------------------------------------------------------
//   parameter lanes: each copy, its own track
//---------------------------------------------------------

function liveTempo() {
      try { return num(new LiveAPI("live_set").get("tempo")) || 120; } catch (e) { return 120; }
      }
function msFactor(bpm) { return PERIOD_QUARTERS * 60000 / bpm; }

// Vst3Plugin::looseTitle: lower case, a slot number in front left out, then a-z 0-9 only
function looseTitle(t) {
      return str(t).toLowerCase().replace(/^\s*#?\d+\s*[:.)-]?\s+/, "").replace(/[^a-z0-9]/g, "");
      }

// the entry changed (or Live's tempo, or the prefix came): the work, in a Task
function paramsCheck(force) {
      if (!me.track)
            return;
      var e = currentEntry();
      var used = false;
      for (var k = 0; k < SLOTS; ++k)
            if (slots[k])
                  used = true;
      var due = force || (e && e.serial !== seenSerial) || (used && Math.abs(liveTempo() - filledBpm) > 1e-6);
      if (!due)
            return;
      if (paramTask)
            paramTask.schedule(0);
      else
            applyParams();
      }

// the first registered copy on the track (in the track's device order) drives it
function drivesTrack(tr) {
      var mine = [];
      var r = registry();
      for (var k in r)
            if (r[k].track === me.track && (k === me.key || now() - r[k].beat < HUB_STALE_MS))
                  mine.push(r[k].device);
      var devs = ids(tr.get("devices"));
      for (var i = 0; i < devs.length; ++i)
            if (mine.indexOf(devs[i]) >= 0)
                  return devs[i] === me.device;
      return true;
      }

// the track's plug-in: the first PluginDevice, else the first device after this one that isn't a Max device
function pluginOf(tr) {
      var devs = ids(tr.get("devices"));
      var i, after = -1;
      for (i = 0; i < devs.length; ++i)
            if (/PluginDevice$/.test(str(new LiveAPI("id " + devs[i]).get("class_name"))))
                  return new LiveAPI("id " + devs[i]);
      for (i = 0; i < devs.length; ++i)
            if (devs[i] === me.device)
                  after = i;
      for (i = after + 1; after >= 0 && i < devs.length; ++i)
            if (!/^Mx/.test(str(new LiveAPI("id " + devs[i]).get("class_name"))))
                  return new LiveAPI("id " + devs[i]);
      return null;
      }

function release(k) {
      var s = slots[k];
      if (!s)
            return;
      outlet(3, k, "id", 0);
      var b = bases[s.id];
      if (b) {
            // Live's own automation of it comes back (tried in Live 12.2: live.remote~ overrides the track's
            // automation while it drives, and setting the value leaves that automation overridden); else Live's
            // value back as it was
            // (re-enabled a little later: right after "id 0" live.remote~ still holds it, and Live keeps the last
            // driven value until something re-enables its automation)
            try {
                  var api = new LiveAPI("id " + s.id);
                  if (num(api.get("automation_state")) > 0) {
                        var pid = s.id;
                        var later = new Task(function() {
                              try { new LiveAPI("id " + pid).call("re_enable_automation"); } catch (e2) {}
                              }, self);
                        later.schedule(150);
                        keepTasks.push(later);            // (held until it ran)
                        if (keepTasks.length > 32)
                              keepTasks.shift();
                        }
                  else
                        api.set("value", b.value);
                  } catch (e) {}
            delete bases[s.id];
            }
      slots[k] = null;
      }

function applyParams() {
      if (!me.track)
            return;
      var e = currentEntry();
      if (!e || !prefix)
            return;                           // (the prefix comes from loadmess: then again)
      var tr = new LiveAPI("id " + me.track);
      var bpm = liveTempo();
      outlet(4, msFactor(bpm));
      var k, had = 0;
      for (k = 0; k < SLOTS; ++k)
            if (slots[k])
                  ++had;
      if (!drivesTrack(tr)) {                 // (another copy on this track does it)
            for (k = 0; k < SLOTS; ++k)
                  release(k);
            seenSerial = e.serial;
            return;
            }
      var plugin = pluginOf(tr);
      var params = [];                        // { id, name, loose }
      if (plugin) {
            var pl = ids(plugin.get("parameters"));
            for (var i = 0; i < pl.length; ++i) {
                  var name = str(new LiveAPI("id " + pl[i]).get("name"));
                  params.push({ id: pl[i], name: name, exact: str(name).toLowerCase().replace(/[^a-z0-9]/g, ""),
                                loose: looseTitle(name) });
                  }
            }
      // by title; else by the plug-in's parameter id where Live names the parameter by its slot only (Kontakt in
      // Live 12.2: "#001" for the slot whose title MuseScore's host reads as "Vibrato", id 1)
      var find = function(title, pid) {
            var x = str(title).toLowerCase().replace(/[^a-z0-9]/g, ""), y = looseTitle(title), j, m;
            for (j = 0; x && j < params.length; ++j)
                  if (params[j].exact === x)
                        return params[j].id;
            for (j = 0; y && j < params.length; ++j)
                  if (params[j].loose === y)
                        return params[j].id;
            for (j = 0; pid >= 0 && j < params.length; ++j) {
                  m = /^\s*#?0*(\d+)\b/.exec(params[j].name);
                  if (m && Number(m[1]) === pid)
                        return params[j].id;
                  }
            return 0;
            };
      var want = [], wanted = {}, results = {};
      for (var key in e.routes) {
            var route = e.routes[key], missing = [], extra = [], noPlugin = false;
            for (var l = 0; l < route.lanes.length; ++l) {
                  var lane = route.lanes[l], id = 0;
                  if (/^live:/.test(str(lane.title)))
                        id = liveParam(tr, lane.title);
                  else if (!plugin) {
                        noPlugin = true;
                        continue;
                        }
                  else
                        id = find(lane.title, lane.pid === undefined ? -1 : num(lane.pid));
                  if (!id)
                        missing.push(lane.title);
                  else if (wanted[id])
                        continue;                         // (two lanes on one parameter: the first)
                  else if (want.length >= SLOTS)
                        extra.push(lane.title);
                  else {
                        wanted[id] = true;
                        want.push({ id: id, lane: lane });
                        }
                  }
            var st = [];
            if (noPlugin)
                  st.push("no plug-in on the track");
            if (missing.length)
                  st.push("missing: " + missing.join(", "));
            if (extra.length)
                  st.push("too many lanes (" + SLOTS + " at most): " + extra.join(", "));
            results[key] = { status: st.length ? st.join("; ") : "ok" };
            }
      // slots: a parameter keeps its slot; the others go
      var at = {};
      for (k = 0; k < SLOTS; ++k) {
            if (slots[k] && wanted[slots[k].id])
                  at[slots[k].id] = k;
            else
                  release(k);
            }
      for (i = 0; i < want.length; ++i) {
            var w = want[i];
            if (at[w.id] === undefined)
                  for (k = 0; k < SLOTS; ++k)
                        if (!slots[k]) {
                              slots[k] = { id: w.id, sig: "", fresh: true };
                              at[w.id] = k;
                              break;
                              }
            k = at[w.id];
            var p = new LiveAPI("id " + w.id);
            if (!bases[w.id])                       // (read before it is taken)
                  bases[w.id] = { value: num(p.get("value")), min: num(p.get("min")), max: num(p.get("max")) };
            fillSlot(k, w.lane, bases[w.id], bpm, num(e.length));
            if (slots[k].fresh) {
                  slots[k].fresh = false;
                  outlet(3, k, "id", w.id);
                  }
            }
      filledBpm = bpm;
      seenSerial = e.serial;
      if (!e.fromSet)
            keep(e);
      g["pr" + me.track] = JSON.stringify({ serial: e.serial, results: results });
      if (want.length || had)
            status("MuseScore Link: " + want.length + " plug-in parameter" + (want.length === 1 ? "" : "s") + " driven"
                   + (isHub ? " (hub)" : ""));
      }

//---------------------------------------------------------
//   parameter lanes kept in the Live Set
//---------------------------------------------------------

// the lanes this copy plays: the hub's entry for the track (MuseScore's word in this Live session), else those
// the set kept (MuseScore not running, or not yet connected)
function currentEntry() {
      var e = entry(me.track);
      if (e)
            return e;
      if (!saved)
            return null;
      return { serial: "saved" + savedSerial, length: saved.length, routes: saved.routes, fromSet: true };
      }

// the hub's entry applied: kept in [pattr Lanes] (Live saves it with the set)
function keep(e) {
      var next = { length: num(e.length) || 0, routes: e.routes || {} };
      if (saved && JSON.stringify(saved) === JSON.stringify(next))
            return;
      saved = next;
      if (scriptAlive()) {                      // kept in the track by the MuseScore Envelopes script: no undo step
            keptByScript = true;
            keptStatus = "";
            if (storeTask)
                  storeTask.cancel();
            keepToScript(me.track, "lanes", saved && Object.keys(saved.routes).length ? encodeSaved(saved) : []);
            return;
            }
      if (storeTask)
            storeTask.schedule(STORE_DELAY_MS);   // (again: the delay starts over)
      else
            flushStores();
      }

// the lanes into the stores (only the parts that changed)
function flushStores() {
      var chunks = splitSaved(encodeSaved(saved));
      keptStatus = chunks ? "" : "too many lane events to keep in the Live Set (they play while MuseScore runs)";
      if (!chunks)
            chunks = splitSaved(encodeSaved(null));
      for (var k = 0; k < STORES; ++k) {      // (Live: one undo step, "Change in MuseScore Link")
            var part = k < chunks.length ? chunks[k] : [SAVED_TAG, SAVED_VERSION, 0, k, 0];
            var j = JSON.stringify(part);
            if (sentParts[k] === j)
                  continue;
            sentParts[k] = j;
            outlet(6, [k].concat(part));
            }
      }

// the encoded lanes in the stores' parts: each "msl-lanes 2 <stamp> <k> <parts> …" (the stamp: parts of one value);
// null: too long
function splitSaved(a) {
      var data = a.slice(2), n = Math.max(1, Math.ceil(data.length / (STORE_ATOMS - 5)));
      if (n > STORES)
            return null;
      var stamp = Math.floor(Math.random() * 1e9), out = [];
      for (var k = 0; k < n; ++k)
            out.push([SAVED_TAG, SAVED_VERSION, stamp, k, n].concat(data.slice(k * (STORE_ATOMS - 5), (k + 1) * (STORE_ATOMS - 5))));
      return out;
      }

// the saved value as atoms (numbers and symbols: no JSON symbol to intern in Max at each change):
//   msl-lanes 2 <length beats> <routes> then per route: <key> <hash> <lanes>, per lane: <title> <pid> <atoms>
//   then the lane's events packed (packLane) in <atoms> atoms (version 1: <pairs>, then (time value) × pairs)
function encodeSaved(sv) {
      var a = [SAVED_TAG, SAVED_VERSION, sv ? num(sv.length) || 0 : 0];
      var keys = [];
      if (sv)
            for (var k in sv.routes)
                  keys.push(k);
      a.push(keys.length);
      for (var i = 0; i < keys.length; ++i) {
            var r = sv.routes[keys[i]];
            a.push(keys[i], num(r.hash) || 0, r.lanes.length);
            for (var l = 0; l < r.lanes.length; ++l) {
                  var lane = r.lanes[l];
                  var packed = packLane(lane.ev);
                  a.push(str(lane.title), lane.pid === undefined ? -1 : num(lane.pid), packed.length);
                  for (var j = 0; j < packed.length; ++j)
                        a.push(packed[j]);
                  }
            }
      return a;
      }

// a lane's events (time value …, as MuseScore sends them: a point, then along a ramp each tick where the value
// reaches another step of the parameter's resolution, 1e-4: automation.h PARAM_RESOLUTION) in fewer atoms: an event "time value" (time >= 0), or a run of m >= 3 evenly spaced
// steps "-m t1 v1 tm vm vh": step k (0 … m-1) at round(t1 + k (tm - t1) / (m - 1)), its value on the parabola
// through v1 (k = 0), vh (k = h = floor((m - 1) / 2)) and vm (k = m - 1). A straight ramp is one run, a curved one
// one or a few. A run is taken while each step's value is within half of PACK_DV of the original (PACK_DV: one MIDI
// step, 1/127 of the parameter's range: the owner's criterion for what Live plays of MuseScore's curves, 2026-10-03,
// as the clip envelopes' straight pieces) and, where its time is more than PACK_DT (one tick, 8 units: the grid a lane's
// events sit on) off the original's, MuseScore's staircase at the run's time is within the other half of it (a ramp
// reaches its steps at uneven ticks): what the set plays without MuseScore is MuseScore's staircase to within one MIDI step and one tick (with MuseScore running, the copies
// play the events as sent).
// livesetwriter.cpp packLane does the same (Create Live Set)
var PACK_DV = 1 / 127;
var PACK_DT = 8;
var PACK_MAX = 4096;                    // steps a run at most
function runValue(k, m, v1, vh, vm) {
      var h = Math.floor((m - 1) / 2), e = m - 1;
      if (h === 0)
            return v1 + k * (vm - v1) / e;
      return v1 * (k - h) * (k - e) / (h * e) - vh * k * (k - e) / (h * (e - h)) + vm * k * (k - h) / (e * (e - h));
      }
function runFits(ev, i, j) {
      var m = j - i + 1, t1 = num(ev[2 * i]), tm = num(ev[2 * j]);
      if (!(tm > t1))
            return false;
      var v1 = num(ev[2 * i + 1]), vm = num(ev[2 * j + 1]), vh = num(ev[2 * (i + Math.floor((m - 1) / 2)) + 1]);
      for (var k = 1; k < m - 1; ++k) {
            var t = Math.round(t1 + k * (tm - t1) / (m - 1)), o = num(ev[2 * (i + k) + 1]);
            if (Math.abs(runValue(k, m, v1, vh, vm) - o) > PACK_DV / 2)
                  return false;
            if (Math.abs(t - num(ev[2 * (i + k)])) > PACK_DT && Math.abs(stairAt(ev, i, j, t) - o) > PACK_DV / 2)
                  return false;
            }
      return true;
      }
// the staircase of events i … j at time t (t1 <= t): the value of the last event at or before it
function stairAt(ev, i, j, t) {
      var lo = i, hi = j;
      while (lo < hi) {
            var h = Math.ceil((lo + hi) / 2);
            if (num(ev[2 * h]) <= t)
                  lo = h;
            else
                  hi = h - 1;
            }
      return num(ev[2 * lo + 1]);
      }
// the longest run from step i: the largest j (i + 2 <= j < n, j - i < PACK_MAX) that fits, found by doubling the run
// while it fits, then halving between the last that fit and the first that didn't (a ramp's steps come every tick:
// trying each length would take the square of its length); -1: none
function lastFit(ev, i, n) {
      var limit = Math.min(n - 1, i + PACK_MAX - 1);
      if (i + 2 > limit || !runFits(ev, i, i + 2))
            return -1;
      var good = i + 2, bad = limit + 1, step = 1;
      while (good < limit) {
            var c = Math.min(limit, good + step);
            if (runFits(ev, i, c)) {
                  good = c;
                  step *= 2;
                  }
            else {
                  bad = c;
                  break;
                  }
            }
      while (bad - good > 1) {
            var h = Math.floor((good + bad) / 2);
            if (runFits(ev, i, h))
                  good = h;
            else
                  bad = h;
            }
      return good;
      }
function packLane(ev) {
      var n = Math.floor(ev.length / 2), out = [], i = 0;
      while (i < n) {
            var best = lastFit(ev, i, n);
            if (best >= 0) {
                  var m = best - i + 1;
                  out.push(-m, num(ev[2 * i]), num(ev[2 * i + 1]), num(ev[2 * best]), num(ev[2 * best + 1]),
                           num(ev[2 * (i + Math.floor((m - 1) / 2)) + 1]));
                  i = best + 1;
                  }
            else {
                  out.push(num(ev[2 * i]), num(ev[2 * i + 1]));
                  ++i;
                  }
            }
      return out;
      }
function unpackLane(a) {
      var ev = [], p = 0;
      while (p < a.length) {
            var x = num(a[p]);
            if (x >= 0) {
                  if (p + 1 >= a.length)
                        return null;
                  ev.push(x, num(a[p + 1]));
                  p += 2;
                  continue;
                  }
            var m = -x;
            if (p + 5 >= a.length || m < 3)
                  return null;
            var t1 = num(a[p + 1]), v1 = num(a[p + 2]), tm = num(a[p + 3]), vm = num(a[p + 4]), vh = num(a[p + 5]);
            for (var k = 0; k < m; ++k)
                  ev.push(Math.round(t1 + k * (tm - t1) / (m - 1)), runValue(k, m, v1, vh, vm));
            p += 6;
            }
      return ev;
      }

// the atoms back (null: not this format, or cut short)
function decodeSaved(a) {
      if (!a || a.length < 4 || str(a[0]) !== SAVED_TAG || !(num(a[1]) === 1 || num(a[1]) === 2))
            return null;
      var version = num(a[1]);
      var sv = { length: num(a[2]) || 0, routes: {} };
      var p = 4, n = num(a[3]);
      for (var i = 0; i < n; ++i) {
            if (p + 3 > a.length)
                  return null;
            var key = str(a[p]), route = { hash: num(a[p + 1]), lanes: [] }, lanes = num(a[p + 2]);
            p += 3;
            for (var l = 0; l < lanes; ++l) {
                  if (p + 3 > a.length)
                        return null;
                  var lane = { title: str(a[p]), pid: num(a[p + 1]), ev: [] }, count = num(a[p + 2]);
                  p += 3;
                  if (version === 1)
                        count *= 2;           // (pairs)
                  if (p + count > a.length)
                        return null;
                  lane.ev = version === 1 ? a.slice(p, p + count).map(num) : unpackLane(a.slice(p, p + count));
                  if (!lane.ev)
                        return null;
                  p += count;
                  route.lanes.push(lane);
                  }
            sv.routes[key] = route;
            }
      return sv;
      }

// a store's value ("lanes <k> …"): the set opened, the device pasted or duplicated, Live's undo. When the parts of
// one value are all there, they are the lanes (an echo of what the script set is left alone)
function restoreSaved(args) {
      var k = num(args[0]), part = args.slice(1);
      restoredLog.push("lanes " + k + ": " + part.length + " atoms: " + str(part.slice(0, 6)));
      if (restoredLog.length > 20)
            restoredLog.shift();
      try {                                   // (debugging: the "saved" probe)
            var info = JSON.parse(g.savedInfo || "{}");
            info[me.key] = restoredLog.slice(-4);
            g.savedInfo = JSON.stringify(info);
            }
      catch (e) {}
      if (!(k >= 0 && k < STORES))
            return;
      if (keptByScript)                       // (the script keeps this track's lanes: an older store's value is stale)
            return;
      if (sentParts[k] === JSON.stringify(part))
            return;
      parts[k] = part;
      sentParts[k] = undefined;
      // the parts of one value: same stamp, 0 … n-1
      var p0 = parts[0];
      if (!p0 || str(p0[0]) !== SAVED_TAG || !(num(p0[1]) === 1 || num(p0[1]) === 2))
            return;
      var stamp = num(p0[2]), n = num(p0[4]), data = [];
      if (n === 0) {                          // (nothing kept)
            if (saved) {
                  saved = null;
                  ++savedSerial;
                  paramsCheck(true);
                  }
            return;
            }
      for (var i = 0; i < n; ++i) {
            var pi = parts[i];
            if (!pi || num(pi[2]) !== stamp || num(pi[3]) !== i || num(pi[4]) !== n)
                  return;                     // (another part still to come)
            data = data.concat(pi.slice(5));
            }
      var sv = decodeSaved([SAVED_TAG, num(p0[1])].concat(data));
      saved = sv && Object.keys(sv.routes).length ? sv : null;
      ++savedSerial;
      paramsCheck(true);
      }

// slot k's table: the value in force at each ms of the song, in the parameter's range
function fillSlot(k, lane, base, bpm, lengthBeats) {
      var msPerBeat = 60000 / bpm, span = base.max - base.min;
      var ev = [];
      for (var i = 0; i + 1 < lane.ev.length; i += 2)
            ev.push({ at: Math.max(0, Math.round(lane.ev[i] / UNITS * msPerBeat)), v: base.min + lane.ev[i + 1] * span, n: i });
      ev.sort(function(a, b) { return a.at - b.at || a.n - b.n; });
      var last = ev.length ? ev[ev.length - 1].at : 0;
      var n = Math.min(MAX_MS, Math.ceil(Math.max(lengthBeats * msPerBeat, last + 1000)) + 1);
      var sig = n + "|" + bpm + "|" + base.value + "|" + base.min + "|" + base.max + "|" + lane.ev.join(",");
      if (slots[k].sig === sig)
            return;
      var b = new Buffer(prefix + k);
      b.send("sizeinsamps", n, 1);
      var v = base.value, j = 0, chunk = [];
      for (i = 0; i < n; ++i) {
            while (j < ev.length && ev[j].at <= i)
                  v = ev[j++].v;
            chunk.push(v);
            if (chunk.length === POKE || i === n - 1) {
                  b.poke(1, i + 1 - chunk.length, chunk);
                  chunk = [];
                  }
            }
      slots[k].sig = sig;
      }

//---------------------------------------------------------
//   kept in the set by the MuseScore Envelopes script (protocol 7; tools/live/MuseScoreEnvelopes/core.py › /ms/keep)
//   The owner, 2026-10-03: "if we can achieve possibly zero [undo steps], I'd be all for it". A track's values go
//   into the track with Live's Python Track.set_data (no undo step; Max for Live's Object Model has no set_data), so
//   when the script answers (/ms/keep/ping every 2 s with the hello) a copy sends its lanes there at once instead of
//   setting the [pattr] stores (one Live undo step a pause). The hub asks for every track's values when the script
//   first answers (/ms/keep/ask "lanes" and "vel"), puts each into the Global ("klanes<track id>", "kvel<track id>",
//   the atoms as JSON) and tells the copies ([receive msl_keep]). Without the script the stores work as before.
//---------------------------------------------------------

// a datagram to the script at most: Max's [udpsend] sends up to its maxpacketsize, default 5096 bytes (Max 9's
// udpsend reference); core.py KEEP_PACKET is the same
var KEEP_PACKET = 5096;
var keepAsked = false;        // hub: the script was asked for this set's values
var keptIn = {};              // hub: "<what>:<track>" -> { stamp, chunks, parts, n } being received
var keptByScript = false;     // copy: its lanes are kept by the script (the [pattr] stores' values are older)

function scriptAlive() {
      var t = Number(g.keepAlive || 0);
      return t > 0 && now() - t < HUB_STALE_MS;
      }

function trackIndex(trackId) {
      return ids(new LiveAPI("live_set").get("tracks")).indexOf(trackId);
      }

function utf8Length(s) {
      var n = 0;
      for (var i = 0; i < s.length; ++i) {
            var c = s.charCodeAt(i);
            n += c < 0x80 ? 1 : c < 0x800 ? 2 : (c >= 0xD800 && c < 0xDC00) ? 2 : 3;   // (a surrogate pair: 4 in all)
            }
      return n;
      }

// an atom's bytes in an OSC message, its type tag's byte included (OSC 1.0: int32 / float32 4 bytes, a string its
// UTF-8 bytes and a 0, padded to 4)
function atomBytes(a) {
      if (typeof a === "number")
            return 5;
      return (Math.floor(utf8Length(str(a)) / 4) + 1) * 4 + 1;
      }

// atoms in parts that each fit a KEEP_PACKET datagram after `header` bytes
function chunkAtoms(atoms, header) {
      var parts = [], cur = [], size = header;
      for (var i = 0; i < atoms.length; ++i) {
            var s = atomBytes(atoms[i]);
            if (cur.length && size + s > KEEP_PACKET) {
                  parts.push(cur);
                  cur = [];
                  size = header;
                  }
            cur.push(atoms[i]);
            size += s;
            }
      parts.push(cur);
      return parts;
      }

// an OSC message's bytes before its atoms: the address, the fixed arguments with their type tags, the tags' ","
// and their padding (at most 4 bytes more)
function oscHeader(address, fixed) {
      var n = atomBytes(address) - 1 + 1 + 4;
      for (var i = 0; i < fixed.length; ++i)
            n += atomBytes(fixed[i]);
      return n;
      }

// a track's value of `what` to the script (no atoms: removed)
function keepToScript(trackId, what, atoms) {
      var t = trackIndex(trackId);
      if (t < 0)
            return false;
      var stamp = Math.floor(Math.random() * 1e9);
      var parts = chunkAtoms(atoms, oscHeader("/ms/keep/put", [t, what, stamp, 0, 0]));
      for (var c = 0; c < parts.length; ++c)
            outlet(8, ["/ms/keep/put", t, what, stamp, c, parts.length].concat(parts[c]));
      return true;
      }

function askKept() {
      keepAsked = true;
      velReady = false;
      outlet(8, "/ms/keep/ask", udpPort, "lanes");
      outlet(8, "/ms/keep/ask", udpPort, "vel");
      }

// /live/keep/data what track stamp chunk chunks atoms…
function keptData(a) {
      var what = str(a[0]), t = num(a[1]), k = what + ":" + t;
      var inc = keptIn[k];
      if (!inc || inc.stamp !== num(a[2]))
            inc = keptIn[k] = { stamp: num(a[2]), chunks: num(a[4]), parts: {}, n: 0 };
      if (inc.parts[num(a[3])] === undefined)
            ++inc.n;
      inc.parts[num(a[3])] = a.slice(5);
      if (inc.n < inc.chunks)
            return;
      delete keptIn[k];
      var atoms = [];
      for (var c = 0; c < inc.chunks; ++c)
            atoms = atoms.concat(inc.parts[c] || []);
      var tl = ids(new LiveAPI("live_set").get("tracks"));
      if (!(t >= 0 && t < tl.length))
            return;
      g["k" + what + tl[t]] = JSON.stringify(atoms);
      if (typeof messnamed === "function")
            messnamed(what === "vel" ? "msl_vel" : "msl_keep", tl[t], what);
      }

// copy: the script's value of `what` for this track
function keptChanged(what) {
      if (what !== "lanes")
            return velChanged();
      var atoms = null;
      try { atoms = JSON.parse(g["klanes" + me.track] || "null"); } catch (e) {}
      if (!atoms)
            return;
      var sv = decodeSaved(atoms);
      keptByScript = true;
      if (storeTask)
            storeTask.cancel();
      saved = sv && Object.keys(sv.routes).length ? sv : null;
      ++savedSerial;
      paramsCheck(true);
      }

//---------------------------------------------------------
//   velocity curves of clip tabs (protocol 7; the owner, 2026-10-03: "I want to be able to automate the velocity in
//   MuseScore, and I can toggle override the existing note velocities vs. use data saved in MuseScore Link")
//
//   One curve a clip (a clip tab's "Velocity" lane, mscore/liveclipmodel.h › Velocity lane), in one of two modes:
//   scale (the lane's value u 0-1 multiplies each note's own velocity by 2u: 0-200 %, 100 % = unchanged) or absolute
//   (the note's velocity is round(127 u), 1 at least); before the lane's first point notes keep their own. Two outputs:
//   "write" (MuseScore writes the velocities into the clip's notes: nothing here) and "shape" (the notes stay; this
//   device changes their note-ons as they pass, below).
//     MuseScore -> hub
//       /ms/vel/set key:s serial:i chunk:i chunks:i (atom) × n   the clip's record (velClip below; none: removed)
//         -> /live/vel/set key:s serial:i status:s kept:i         ("ok" or why it can't shape; kept 1: in the set)
//       /ms/vel/ask key:s
//         -> /live/vel/curve key:s found:i kept:i chunk:i chunks:i (atom) × n
//   A clip's record, as atoms: mode (0 scale, 1 absolute), output (0 shape, 1 write), points n, then per point tick
//   (480 a beat, clip time) value (0-1) curve (0 step, 1 ramp) c1x c1y c2x c2y (Live's Bézier, automation.h), then
//   originals m, per note: note id, pitch, start (UNITS, clip time), velocity before the curve, velocity written.
//   A track's value ("vel", in the Global "kvel<track id>" and kept by the script): "msl-vel" 1 clips, per clip its
//   place: kind (0 a session slot, 1 an arrangement clip) and where (the slot's index; the arrangement clip's start
//   in UNITS), then its record.
//
//   Shaping (the copy on the clip's track; the first copy on a track: drivesTrack): the patcher's MIDI path (not
//   this script) changes each note-on: its velocity v by the code c read from [buffer~ ---mslv] at the song position
//   ([snapshot~] of the song-position phasor, banged by the note): 0 v as it is; c >= 1: round(v (c - 1)), 1-127;
//   c < 0: -c. The buffer is a ring of VEL_RING cells, one a MuseScore tick of song time (480 a beat), cell = tick mod
//   VEL_RING; this script fills it every second (and at once when a curve or the track's clips change) from the song
//   position up to VEL_AHEAD_S seconds ahead, with what plays on the track then: the session clip playing
//   (playing_slot_index; its clip time from playing_position, its loop), the fired one from its launch (the launch
//   quantization's next boundary), or the arrangement clips (track.back_to_arranger 0). Other clips: 0, unchanged.
//---------------------------------------------------------

// Live's tempo at most: 999 BPM (Live 12.4.6 on the test VM, 2026-10-03: song.tempo took 20 and 999, refused 10, 1000
// and 5000 with "Tempo out of range")
var VEL_MAX_BPM = 999;
// the ring is filled from the song position this far ahead: two fills (the fill comes every second: the device's
// heartbeat) so a late one still finds the next filled
var VEL_AHEAD_S = 2;
// two windows at the fastest tempo: the window just filled and the one before it, which notes may still read
var VEL_RING = 2 * Math.ceil(VEL_AHEAD_S * VEL_MAX_BPM / 60 * TICKS);
var velReady = false;        // hub: the script gave this set's curves (or it doesn't answer)
var velIncoming = {};         // hub: key -> a /ms/vel/set being received
var velAsks = [];             // hub: /ms/vel/ask waiting for the script's values
var velTask = null;
var velRec = null;            // copy: this track's record, parsed
var velSig = "";              // copy: what is in the ring (its window and inputs), to skip a refill
var velSized = false;
var velActive = false;        // copy: the ring holds codes (not all 0)
var velLogOn = -1;            // copy: the shaper's note-on log as last set (the "vlog on" probe)
var velBiasTicks = 0;         // copy: the index's bias, one signal vector in ticks (make_device.py: snapshot~ reads the
                              // vector before the note's)
var velSr = 0;                // ([dspstate~]) the sample rate
var velVs = 0;                // ([dspstate~]) the signal vector size
var velObservers = [];

// the clip record's atoms -> { mode, output, points: [{ tick, value, curve, c1x, c1y, c2x, c2y }], originals: [...] }
function velClip(a, p) {
      p = p || 0;
      var r = { mode: num(a[p]), output: num(a[p + 1]), points: [], originals: [] };
      var n = num(a[p + 2]);
      p += 3;
      for (var i = 0; i < n; ++i, p += 7)
            r.points.push({ tick: num(a[p]), value: num(a[p + 1]), curve: num(a[p + 2]), c1x: num(a[p + 3]), c1y: num(a[p + 4]),
                            c2x: num(a[p + 5]), c2y: num(a[p + 6]) });
      var m = num(a[p]);
      ++p;
      for (i = 0; i < m; ++i, p += 5)
            r.originals.push([num(a[p]), num(a[p + 1]), num(a[p + 2]), num(a[p + 3]), num(a[p + 4])]);
      r.end = p;
      return r;
      }

function velClipAtoms(r) {
      var a = [r.mode, r.output, r.points.length];
      for (var i = 0; i < r.points.length; ++i) {
            var q = r.points[i];
            a.push(q.tick, q.value, q.curve, q.c1x, q.c1y, q.c2x, q.c2y);
            }
      a.push(r.originals.length);
      for (i = 0; i < r.originals.length; ++i)
            a = a.concat(r.originals[i]);
      return a;
      }

// a track's value -> { clips: [{ kind, where, rec }] } (null: none or not this format)
function velTrack(a) {
      if (!a || a.length < 3 || str(a[0]) !== "msl-vel" || num(a[1]) !== 1)
            return null;
      var out = { clips: [] }, n = num(a[2]), p = 3;
      for (var i = 0; i < n; ++i) {
            if (p + 5 > a.length)
                  return null;
            var c = { kind: num(a[p]), where: num(a[p + 1]) };
            c.rec = velClip(a, p + 2);
            p = c.rec.end;
            delete c.rec.end;
            out.clips.push(c);
            }
      return out;
      }

function velTrackAtoms(t) {
      var a = ["msl-vel", 1, t.clips.length];
      for (var i = 0; i < t.clips.length; ++i)
            a = a.concat([t.clips[i].kind, t.clips[i].where]).concat(velClipAtoms(t.clips[i].rec));
      return a;
      }

function velTrackOf(trackId) {
      try { return velTrack(JSON.parse(g["kvel" + trackId] || "null")); } catch (e) { return null; }
      }

// a clip's place in its track: a session slot's index, else the arrangement clip's start (UNITS)
function velPlace(clip, tr) {
      var p = ids(clip.get("canonical_parent"));
      if (p.length && str(new LiveAPI("id " + p[0]).type) === "ClipSlot")
            return { kind: 0, where: ids(tr.get("clip_slots")).indexOf(p[0]) };
      return { kind: 1, where: Math.round(num(clip.get("start_time")) * UNITS) };
      }

function velFind(t, place) {
      if (!t)
            return -1;
      for (var i = 0; i < t.clips.length; ++i)
            if (t.clips[i].kind === place.kind && t.clips[i].where === place.where)
                  return i;
      return -1;
      }

// hub: MuseScore's record for an edited clip
function velSet(key, serial, atoms) {
      var e = edits[key];
      var clip = e ? new LiveAPI("id " + e.clipId) : null;
      if (!clip || !(num(clip.id) > 0))
            return send("/live/vel/set", key, serial, "gone", 0);
      var tr = trackOf(clip);
      if (!tr)
            return send("/live/vel/set", key, serial, "no track", 0);
      var tid = num(tr.id), place = velPlace(clip, tr);
      var t = velTrackOf(tid) || { clips: [] };
      var i = velFind(t, place);
      var rec = atoms.length >= 4 ? velClip(atoms, 0) : null;
      if (rec)
            delete rec.end;
      if (rec && (rec.points.length || rec.originals.length)) {
            if (i >= 0)
                  t.clips[i].rec = rec;
            else
                  t.clips.push({ kind: place.kind, where: place.where, rec: rec });
            }
      else if (i >= 0)
            t.clips.splice(i, 1);
      var value = t.clips.length ? velTrackAtoms(t) : [];
      g["kvel" + tid] = JSON.stringify(value);
      if (typeof messnamed === "function")
            messnamed("msl_vel", tid);
      var kept = scriptAlive() && keepToScript(tid, "vel", value);
      var st = "ok";
      if (rec && rec.output === 0 && rec.points.length && !copyOn(tid))
            st = "no MuseScore Link on the clip's track: Live plays the notes unshaped";
      send("/live/vel/set", key, serial, st, kept ? 1 : 0);
      }

// hub: the stored curve of an edited clip
function velAnswer(key) {
      var e = edits[key];
      var clip = e ? new LiveAPI("id " + e.clipId) : null;
      var tr = clip && num(clip.id) > 0 ? trackOf(clip) : null;
      var rec = null;
      if (tr) {
            var t = velTrackOf(num(tr.id));
            var i = velFind(t, velPlace(clip, tr));
            rec = i >= 0 ? t.clips[i].rec : null;
            }
      var kept = scriptAlive() ? 1 : 0;
      var atoms = rec ? velClipAtoms(rec) : [];
      var parts = chunkAtoms(atoms, oscHeader("/live/vel/curve", [key, 0, 0, 0, 0]));
      for (var c = 0; c < parts.length; ++c)
            send.apply(this, ["/live/vel/curve", key, rec ? 1 : 0, kept, c, parts.length].concat(parts[c]));
      }

// hub: asks waiting for the script (answered when its values came, or once it stopped answering)
function velAnswerLate(ready) {
      if (!velAsks.length || (!ready && scriptAlive() && !velReady))
            return;
      var l = velAsks;
      velAsks = [];
      for (var i = 0; i < l.length; ++i)
            velAnswer(l[i].key);
      }

// the lane's value at a tick (automation.cpp Lane::valueAt, curveAt): -1 before the first point
function velBez(p1, p2, t) {
      var u = 1 - t;
      return 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t;
      }
function velDiag(x, y) { return Math.abs(x - y) <= 1e-6; }
function velCurveAt(c1x, c1y, c2x, c2y, x) {
      if (x <= 0)
            return 0;
      if (x >= 1)
            return 1;
      if (velDiag(c1x, c1y) && velDiag(c2x, c2y))
            return x;
      var lo = 0, hi = 1;
      for (var i = 0; i < 60; ++i) {                    // (automation.cpp: the same bisection)
            var t = 0.5 * (lo + hi);
            if (velBez(c1x, c2x, t) < x)
                  lo = t;
            else
                  hi = t;
            }
      return velBez(c1y, c2y, 0.5 * (lo + hi));
      }
function velValueAt(points, tick) {
      if (!points.length || tick < points[0].tick)
            return -1;
      var k = 0;
      while (k + 1 < points.length && points[k + 1].tick <= tick)
            ++k;
      var p = points[k], q = points[k + 1];
      if (!q || p.curve === 0 || q.tick === p.tick)
            return p.value;
      var x = (tick - p.tick) / (q.tick - p.tick);
      var curved = !(velDiag(p.c1x, p.c1y) && velDiag(p.c2x, p.c2y));
      var y = curved ? velCurveAt(p.c1x, p.c1y, p.c2x, p.c2y, x) : x;
      return p.value + (q.value - p.value) * y;
      }

// the shaper's code for a lane value (see above; liveclipmodel.cpp shapeVelocity reads it the same way)
function velCode(mode, u) {
      if (u < 0)
            return 0;
      if (mode === 1)
            return -Math.max(1, Math.min(127, Math.round(127 * u)));
      return 1 + 2 * u;
      }

// what the patcher does with a note-on's velocity (the tests; make_device.py's expr)
function velShape(v, c) {
      if (v <= 0 || c === 0)
            return v;
      if (c < 0)
            return Math.floor(-c);
      return Math.min(127, Math.max(1, Math.floor(v * (c - 1) + 0.5)));
      }

// a clip's time in ticks (480 a beat) after `x` beats of playing from clip time `from` (beats): its loop, or null once
// it ended. Rounded to ticks before the loop is applied: a note at the loop's end is the loop's start
function velClipTime(info, from, x) {
      var t = Math.round((from + x) * TICKS);
      var ls = Math.round(info.loopStart * TICKS), le = Math.round(info.loopEnd * TICKS);
      if (info.looping) {
            if (t >= le && le > ls)
                  t = ls + (t - le) % (le - ls);
            return t;
            }
      return t < Math.round(info.endMarker * TICKS) ? t : null;
      }

function velClipInfo(clip) {
      return { looping: num(clip.get("looping")) !== 0, loopStart: num(clip.get("loop_start")), loopEnd: num(clip.get("loop_end")),
               startMarker: num(clip.get("start_marker")), endMarker: num(clip.get("end_marker")) };
      }

// the launch quantization's grid in beats (a quarter note a beat): Live's Song.Quantization values (q_no_q 0,
// q_8_bars 1, q_4_bars 2, q_2_bars 3, q_bar 4, q_half 5, q_half_triplet 6, q_quarter 7, q_quarter_triplet 8,
// q_eight 9, q_eight_triplet 10, q_sixtenth 11, q_sixtenth_triplet 12, q_thirtytwoth 13: Live 12.4.6's Python API);
// a clip's launch_quantization is ClipLaunchQuantization (q_global 0, q_none 1, then as the Song's + 1)
function velGrid(q, barBeats) {
      var g8 = [0, 8 * barBeats, 4 * barBeats, 2 * barBeats, barBeats, 2, 4 / 3, 1, 2 / 3, 0.5, 1 / 3, 0.25, 1 / 6, 0.125];
      return q >= 0 && q < g8.length ? g8[q] : barBeats;
      }

function velLaunchAt(clip, song, nowB) {
      if (!num(song.get("is_playing")))
            return nowB;
      var q = clip ? num(clip.get("launch_quantization")) : 0;
      q = q === 0 ? num(song.get("clip_trigger_quantization")) : q - 1;
      var bar = (num(song.get("signature_numerator")) || 4) * 4 / (num(song.get("signature_denominator")) || 4);
      var grid = velGrid(q, bar);
      return grid > 0 ? Math.ceil(nowB / grid - 1e-9) * grid : nowB;
      }

// what the track plays at song beat b, from now on: [{ from, to, rec, info, at, clipFrom }] (clip time at song beat
// b: velClipTime(info, clipFrom, b - at))
function velPlan(tr, t, song, nowB) {
      var plan = [];
      var shaped = function(kind, where) {
            var i = velFind(t, { kind: kind, where: where });
            var r = i >= 0 ? t.clips[i].rec : null;
            return r && r.output === 0 && r.points.length ? r : null;
            };
      var slots = ids(tr.get("clip_slots"));
      var slotClip = function(k) {
            if (!(k >= 0 && k < slots.length))
                  return null;
            var c = ids(new LiveAPI("id " + slots[k]).get("clip"));
            return c.length ? new LiveAPI("id " + c[0]) : null;
            };
      var psi = num(tr.get("playing_slot_index")), fsi = num(tr.get("fired_slot_index"));
      var switchAt = Infinity;
      if (fsi >= 0 || fsi === -2) {
            var fc = fsi >= 0 ? slotClip(fsi) : null;
            switchAt = velLaunchAt(fc, song, nowB);
            var fr = fc ? shaped(0, fsi) : null;
            if (fr) {
                  var fi = velClipInfo(fc);
                  plan.push({ from: switchAt, to: Infinity, rec: fr, info: fi, at: switchAt, clipFrom: fi.startMarker });
                  }
            }
      if (psi >= 0) {
            var pc = slotClip(psi), pr = pc ? shaped(0, psi) : null;
            if (pr) {
                  // a session clip's start_time is "the time the clip was started" (Live's API): it plays from its start
                  // marker since then; a legato clip (launched at the position of the clip before) from where it is now
                  var pi = velClipInfo(pc);
                  if (num(pc.get("legato")))
                        plan.push({ from: -Infinity, to: switchAt, rec: pr, info: pi, at: nowB,
                                    clipFrom: num(pc.get("playing_position")) });
                  else
                        plan.push({ from: -Infinity, to: switchAt, rec: pr, info: pi, at: num(pc.get("start_time")),
                                    clipFrom: pi.startMarker });
                  }
            }
      else if (!num(tr.get("back_to_arranger"))) {
            var ac = ids(tr.get("arrangement_clips"));
            for (var i = 0; i < ac.length; ++i) {
                  var c = new LiveAPI("id " + ac[i]);
                  var s = num(c.get("start_time")), r = shaped(1, Math.round(s * UNITS));
                  if (!r)
                        continue;
                  var info = velClipInfo(c);
                  plan.push({ from: s, to: Math.min(num(c.get("end_time")), switchAt), rec: r, info: info, at: s,
                              clipFrom: info.startMarker });
                  }
            }
      return plan;
      }

// the code for song beat b
function velCodeAt(plan, b) {
      for (var i = 0; i < plan.length; ++i) {
            var p = plan[i];
            if (b < p.from || b >= p.to)
                  continue;
            var ct = velClipTime(p.info, p.clipFrom, b - p.at);
            if (ct === null)
                  return 0;
            return velCode(p.rec.mode, velValueAt(p.rec.points, ct));
            }
      return 0;
      }

function velChanged() {
      velRec = velTrackOf(me.track);
      velSig = "";
      if (velTask)
            velTask.schedule(0);
      else
            velFill();
      }

// Live tells when the track's playing or fired clip changes and when the transport starts: the ring at once
function velObserve() {
      if (typeof LiveAPI === "undefined" || !me.track)
            return;
      var cb = function() { velSig = ""; if (velTask) velTask.schedule(0); };
      try {
            var props = [["id " + me.track, "playing_slot_index"], ["id " + me.track, "fired_slot_index"], ["live_set", "is_playing"]];
            for (var i = 0; i < props.length; ++i) {
                  var o = new LiveAPI(cb, props[i][0]);
                  o.property = props[i][1];
                  velObservers.push(o);
                  }
            }
      catch (e) {}
      }

function velRingName() { return prefix.replace(/mslp$/, "mslv"); }

// the ring from the song position on (each second, and at once on a change)
function velFill() {
      if (!me.track || !prefix)
            return;
      var logOn = g.vlog ? 1 : 0;
      if (logOn !== velLogOn) {
            velLogOn = logOn;
            outlet(9, "log", logOn);
            }
      var t = velRec;
      var tr = new LiveAPI("id " + me.track);
      var any = false;
      if (t)
            for (var i = 0; i < t.clips.length; ++i)
                  if (t.clips[i].rec.output === 0 && t.clips[i].rec.points.length)
                        any = true;
      var b = new Buffer(velRingName());
      if (!velSized) {
            b.send("sizeinsamps", VEL_RING, 1);
            velSized = true;
            }
      if (!any || !drivesTrack(tr)) {
            if (velActive) {
                  var zeros = [];
                  for (i = 0; i < VEL_RING; ++i)
                        zeros.push(0);
                  for (i = 0; i < VEL_RING; i += POKE)
                        b.poke(1, i, zeros.slice(i, Math.min(VEL_RING, i + POKE)));
                  velActive = false;
                  }
            return;
            }
      var song = new LiveAPI("live_set");
      var nowB = num(song.get("current_song_time"));
      var bpm = liveTempo();
      var plan = velPlan(tr, t, song, nowB);
      var t0 = Math.floor(nowB * TICKS), cells = Math.min(VEL_RING / 2, Math.ceil(VEL_AHEAD_S * bpm / 60 * TICKS));
      var vals = [];
      for (i = 0; i < cells; ++i)
            vals.push(velCodeAt(plan, (t0 + i) / TICKS));
      var start = t0 % VEL_RING;
      for (i = 0; i < cells; i += POKE) {
            var part = vals.slice(i, Math.min(cells, i + POKE));
            var at = (start + i) % VEL_RING;
            var first = Math.min(part.length, VEL_RING - at);
            b.poke(1, at, part.slice(0, first));
            if (first < part.length)
                  b.poke(1, 0, part.slice(first));
            }
      velActive = true;
      velBiasTicks = velSr > 0 ? velVs / velSr * bpm / 60 * TICKS : 0;
      outlet(9, "bias", velBiasTicks);
      }

// ("vlog" probe) the shaper's notes: "vlog <pitch> <phase> <velocity in> <velocity out>", kept as [song beat (from
// the phase), pitch, velocity in, velocity out]
function velLog(a) {
      var o = {};
      try { o = JSON.parse(g.vlogged || "{}"); } catch (e) {}
      var l = o[me.track] || [];
      l.push([Math.round(num(a[1]) * PERIOD_QUARTERS * 1e6) / 1e6, num(a[0]), num(a[2]), num(a[3])]);
      if (l.length > 64)
            l.shift();
      o[me.track] = l;
      g.vlogged = JSON.stringify(o);
      }

//---------------------------------------------------------
//   debugging in real Live (hub): /ms/probe, /ms/probecall
//---------------------------------------------------------

function probeText(v) {
      if (v === undefined || v === null)
            return "";
      if (typeof v === "object")
            try { return JSON.stringify(v); } catch (e) {}
      return str(v);
      }

function probe(id, what) {
      if (what === "pos") {
            probes.push(id);
            outlet(5, "bang");                  // ([snapshot~] answers "posvalue <ms>")
            return;
            }
      if (what === "saved") {                   // (the lanes kept in the set: every copy's, through the Global)
            return send("/live/probe", id, str(g.savedInfo).substring(0, 7000));
            }
      if (what === "vlog on" || what === "vlog off") {    // (every copy: the shaper's note-ons to the script, or not)
            g.vlog = what === "vlog on" ? 1 : 0;
            return send("/live/probe", id, what);
            }
      if (what === "vlog") {                    // (every copy's logged note-ons, through the Global)
            return send("/live/probe", id, str(g.vlogged).substring(0, 7000));
            }
      if (what === "vel") {                     // (the velocity state: the script, the Global's records)
            var vs = { alive: scriptAlive(), keepAsked: keepAsked, velReady: velReady, asks: velAsks.length, sr: velSr, vs: velVs,
                       bias: velBiasTicks, active: velActive, records: {} };
            for (var vk in g)
                  if (/^k(vel|lanes)\d+$/.test(vk))
                        vs.records[vk] = str(g[vk]).substring(0, 600);
            return send("/live/probe", id, probeText(vs).substring(0, 7000));
            }
      if (what === "state") {
            var o = { me: me, prefix: prefix, slots: slots, bases: bases, seenSerial: seenSerial, filledBpm: filledBpm,
                      saved: saved ? { length: saved.length, routes: Object.keys(saved.routes), atoms: encodeSaved(saved).length }
                                   : null, savedSerial: savedSerial, restoredLog: restoredLog,
                      waiting: waiting, paramTracks: paramTracks, entries: {} };
            var r = registry();
            for (var k in r)
                  if (!o.entries[r[k].track])
                        o.entries[r[k].track] = { p: str(g["p" + r[k].track]).substring(0, 1500),
                                                  pr: str(g["pr" + r[k].track]) };
            return send("/live/probe", id, probeText(o).substring(0, 7000));
            }
      try {
            var api = new LiveAPI(what);
            var text = "id " + num(api.id) + " type " + str(api.type) + " path " + str(api.path) + "\n" + str(api.info);
            send("/live/probe", id, text.substring(0, 7000));
            }
      catch (e) {
            send("/live/probe", id, "error: " + e);
            }
      }

function answerPos(ms) {
      var t = "pos " + ms + " ms; factor " + msFactor(liveTempo()) + "; tempo " + liveTempo() + "; song time "
              + num(new LiveAPI("live_set").get("current_song_time"));
      while (probes.length)
            send("/live/probe", probes.shift(), t);
      }

function probeCall(id, path, fn, args) {
      try {
            var api = new LiveAPI(path);
            var r;
            if (fn === "get")
                  r = api.get(str(args[0]));
            else if (fn === "set")
                  r = api.set.apply(api, [str(args[0])].concat(args.slice(1)));
            else
                  r = api.call.apply(api, [fn].concat(args));
            send("/live/probe", id, probeText(r).substring(0, 7000));
            }
      catch (e) {
            send("/live/probe", id, "error: " + e);
            }
      }

//---------------------------------------------------------
//   Carriers: converted only on a track holding a MuseScore clip
//---------------------------------------------------------

var carrierGate = 0;          // what the gate was last set to (0: not yet)
var carrierSig = "";          // the track's clips' ids at the last reading of their names
var carrierNamed = 0;         // when their names were read
var carrierOurs = false;

// the hub made or deleted a clip on the track: its copies look again at once
function carriersChanged(track) {
      if (typeof messnamed === "function")
            messnamed("msl_carriers", track);
      }

// the clips on this copy's track: arrangement and session, by id
function carrierClipIds(tr) {
      var l = ids(tr.get("arrangement_clips"));
      var slots = ids(tr.get("clip_slots"));
      for (var i = 0; i < slots.length; ++i) {
            var c = ids(new LiveAPI("id " + slots[i]).get("clip"));
            if (c.length)
                  l.push(c[0]);
            }
      return l;
      }

function carriersCheck(force) {
      if (typeof LiveAPI === "undefined" || !me.track)
            return;
      var ours = carrierOurs;
      try {
            var tr = new LiveAPI("id " + me.track);
            if (!(num(tr.id) > 0))
                  return;
            var l = carrierClipIds(tr);
            var sig = l.join(",");
            if (force || sig !== carrierSig || now() - carrierNamed >= CARRIER_NAMES_EVERY * 1000) {
                  ours = false;
                  for (var i = 0; i < l.length && !ours; ++i)
                        if (str(new LiveAPI("id " + l[i]).get("name")).indexOf(CLIP_PREFIX) === 0)
                              ours = true;
                  carrierSig = sig;
                  carrierNamed = now();
                  }
            }
      catch (e) {
            return;
            }
      carrierOurs = ours;
      var gate = ours ? 1 : 2;
      if (gate !== carrierGate || force) {
            carrierGate = gate;
            outlet(9, "gate", gate);
            }
      }

// (Node tests)
if (typeof module !== "undefined")
      module.exports = { handle: handle, workStep: workStep, displayName: displayName, ids: ids, loosePort: loosePort,
                         findTrack: findTrack, writeSong: writeSong, report: report, hashNotes: hashNotes,
                         checkEdits: checkEdits, edit: edit, looseTitle: looseTitle, applyParams: applyParams,
                         pollParams: pollParams, restoreSaved: restoreSaved, encodeSaved: encodeSaved, packLane: packLane,
                         unpackLane: unpackLane, flushStores: flushStores,
                         checkAudible: checkAudible, audibleState: audibleState,
                         velShape: velShape, velCode: velCode, velValueAt: velValueAt, velClip: velClip,
                         velClipAtoms: velClipAtoms, velTrack: velTrack, velTrackAtoms: velTrackAtoms, velFill: velFill,
                         velClipTime: velClipTime, velGrid: velGrid, chunkAtoms: chunkAtoms, beat: beat,
                         carriersCheck: carriersCheck,
                         VEL_RING: VEL_RING, KEEP_PACKET: KEEP_PACKET,
                         decodeSaved: decodeSaved, state: function() {
                               return { isHub: isHub, work: work, pending: pending, placed: placed, mode: mode, me: me,
                                        edits: edits, slots: slots, bases: bases, waiting: waiting, prefix: prefix,
                                        pendingParams: pendingParams, saved: saved, keptByScript: keptByScript,
                                        velRec: velRec, velReady: velReady, velAsks: velAsks,
                                        carrierGate: carrierGate };
                               } };
