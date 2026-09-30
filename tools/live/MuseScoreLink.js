// MuseScore Link: the Max for Live device for "Live plays the score" (LIVE.md; MuseScore's side:
// libmscore/liveclips.h, mscore/liveclips.h).
//
// One copy goes on each MIDI track that plays a library part, BEFORE the instrument (Kontakt). In
// every copy the patcher (not this script) turns the carrier notes of MuseScore's clips (keys 116-127)
// into their MIDI controllers, in Max's scheduler, sample-timed with the notes; this script is not in
// that path. One copy (the first loaded; another takes over when it goes) is the hub:
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
// The Live Object Model is used from Max's low-priority thread only (messages from udpreceive go
// through deferlow; the Tasks run there).
//
// Plain ECMAScript 5 so it runs in [js] and [v8] alike, and in the Node tests (tools/live/test).

autowatch = 0;
inlets = 1;
outlets = 3;      // 0: OSC to MuseScore (udpsend), 1: udpsend's host / port, 2: status text

var PROTOCOL = 1;
var UNITS = 3840;                       // LiveClips::UNITS_PER_BEAT
var BATCH = 500;                        // notes per add_new_notes call
var HUB_STALE_MS = 5000;

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
var heartbeat = null;
var worker = null;
var reporter = null;
var initialised = false;

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
      r[me.key] = { track: me.track, device: me.device, beat: now() };
      saveRegistry(r);
      heartbeat = new Task(beat, this);
      heartbeat.interval = 1000;
      heartbeat.repeat();
      elect();
      if (!isHub)
            status("MuseScore Link: on this track (the hub is another copy)");
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
            }
      else
            elect();
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
      if (!deferrer) {
            deferrer = p.newdefault(20, 600, "deferlow");
            p.connect(deferrer, 0, self.box, 0);
            }
      receiver = p.newdefault(20, 570, "udpreceive", udpPort);
      p.connect(receiver, 0, deferrer, 0);
      outlet(1, "host", "127.0.0.1");
      outlet(1, "port", udpPort + 1);
      }

// the Port box ("port 9001")
function setPort(n) {
      n = Math.floor(num(n));
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
            g.hub = null;
            g.hubBeat = 0;
            }
      if (heartbeat) heartbeat.cancel();
      if (worker) worker.cancel();
      if (reporter) reporter.cancel();
      }

//---------------------------------------------------------
//   MuseScore's messages
//---------------------------------------------------------

function anything() {
      var a = arrayfromargs(arguments);
      if (messagename === "port")
            return setPort(a[0]);
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
      else if (address === "/ms/clear")
            work.push({ kind: "clear", key: str(a[1]), port: str(a[2]), channel: num(a[3]), part: str(a[4]), clip: str(a[5]),
                        main: true });
      else if (address === "/ms/play") {
            var song = new LiveAPI("live_set");
            song.set("current_song_time", Math.max(0, num(a[0])));
            if (!num(song.get("is_playing")))
                  song.call("continue_playing");
            }
      else if (address === "/ms/stop")
            new LiveAPI("live_set").call("stop_playing");
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
            }
      catch (e) {
            if (w.kind === "clip")
                  send("/live/applied", w.clip.key, w.clip.hash, "error: " + e, "");
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
            }
      var c = ours.api;
      c.set("muted", 0);
      c.call("remove_notes_extended", 0, 128, 0, t.length + 1);
      for (i = 0; i < t.notes.length; i += BATCH)
            c.call("add_new_notes", { notes: t.notes.slice(i, i + BATCH) });
      setMonitor(tr);
      placed[t.key] = { track: num(tr.id), clip: t.clip };
      send("/live/applied", t.key, t.hash, deviceCheck(tr), trackName);
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
      var t = now();
      if (playing || playing !== lastTransport.playing || Math.abs(b - lastTransport.beat) > 1e-6 || t - lastTransport.sent > 1000) {
            send("/live/transport", playing, b, num(song.get("tempo")));
            lastTransport = { playing: playing, beat: b, sent: t };
            }
      }

// (Node tests)
if (typeof module !== "undefined")
      module.exports = { handle: handle, workStep: workStep, displayName: displayName, ids: ids, loosePort: loosePort,
                         findTrack: findTrack, writeSong: writeSong, report: report, state: function() {
                               return { isHub: isHub, work: work, pending: pending, placed: placed, mode: mode, me: me };
                               } };
