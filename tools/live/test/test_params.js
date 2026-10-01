// Tests of the device's plug-in parameter lanes (MuseScoreLink.js › Parameter lanes): the hub takes MuseScore's
// /ms/params and /ms/pvals, hands each track's lanes to the copy on that track (the Global, messnamed), which
// fills its buffer~ tables and takes the parameters with live.remote~; the hub answers /live/papplied.
// Against the stand-in Live (fakelive.js). Run: node tools/live/test/test_params.js
// What only Live can show: phasor~'s tick period, buffer~ sizing, live.remote~ in a MIDI effect (the report).

"use strict";
const assert = require("assert");
const { FakeLive, loadDevice } = require("./fakelive");

const UNITS = 3840;
const PAIRS = 100;                // pairs a /ms/pvals packet at most
let failures = 0;
function test(name, fn) {
      try {
            fn();
            console.log("PASS " + name);
            }
      catch (e) {
            ++failures;
            console.log("FAIL " + name + "\n  " + (e.stack || e));
            }
      }

// Violins 1 (by MIDI From) and Flute (by name), each: MuseScore Link, then Kontakt with its Configured parameters
function setUp() {
      const live = new FakeLive();
      const vln = live.track("Violins 1", { inputType: "MuseScore A", inputChannel: "Ch. 1" });
      const fl = live.track("Flute");
      const d1 = live.device(vln, "MxDeviceMidiEffect", "MuseScore Link");
      const k1 = live.device(vln, "PluginDevice", "Kontakt 8");
      const d2 = live.device(fl, "MxDeviceMidiEffect", "MuseScore Link");
      const k2 = live.device(fl, "PluginDevice", "Kontakt 8");
      const p = {
            on: live.param(k1, "Device On", 1, 0, 1),
            vib: live.param(k1, "Vibrato", 0.3, 0, 1),
            mic: live.param(k1, "Mic 1 level", 0.25, 0, 2),
            tone: live.param(k2, "#002 Tone", 0.6, 0, 1),
            flmic: live.param(k2, "003 Mic 1 Level", 0.1, 0, 1),
            };
      const shared = {};
      const hub = loadDevice(live, shared, d1.id);
      hub.bang();
      const other = loadDevice(live, shared, d2.id);
      other.bang();
      hub.message("prefix", ["001mslp"]);
      other.message("prefix", ["002mslp"]);
      // the song (8 beats) and the two clips, as MuseScore sends them first
      hub.message("/ms/song", [1, 120, 8 * UNITS, 0, 0, 1]);
      clip(hub, 1, "0:1", "MuseScore A", 1, "Violins 1");
      clip(hub, 1, "0:2", "MuseScore A", 2, "Flute");
      live.settle();
      return { live, vln, fl, k1, k2, p, hub, other, shared };
      }

function clip(dev, gen, key, port, ch, part) {
      dev.message("/ms/track", [gen, key, port, ch, part, "MuseScore: " + part, 1, 8 * UNITS, 1, 1, 7]);
      dev.message("/ms/notes", [gen, key, 0, 60, 0, UNITS, 80, 0]);
      }

// MuseScore's datagrams for a route's lanes: [{ title, events: [[beats, value] …] }]
function packets(gen, key, hash, lanes) {
      const out = [["/ms/params", [gen, key, lanes.length, hash]]];
      lanes.forEach((l, i) => {
            const chunks = Math.max(1, Math.ceil(l.events.length / PAIRS));
            for (let c = 0; c < chunks; ++c) {
                  const args = [gen, key, i, l.title, l.id === undefined ? -1 : l.id, c, chunks];
                  for (const [beat, v] of l.events.slice(c * PAIRS, (c + 1) * PAIRS))
                        args.push(Math.round(beat * UNITS), v);
                  out.push(["/ms/pvals", args]);
                  }
            });
      return out;
      }
function sendParams(dev, gen, key, hash, lanes) {
      for (const [a, args] of packets(gen, key, hash, lanes))
            dev.message(a, args);
      }

const buf = (s, name) => s.live.buffers[name];
const papplied = (s, key) => s.hub.sent("/live/papplied").filter((m) => m[0] === key);
const near = (a, b) => Math.abs(a - b) < 1e-6;

test("the hub says protocol 3", () => {
      const s = setUp();
      assert.strictEqual(s.hub.sent("/live/hello")[0][1], 3);
      });

test("two tracks: each copy fills its own tables (steps, the base before the first, the range) and takes its parameters", () => {
      const s = setUp();
      const many = [];                                 // 250 events: three packets
      for (let i = 0; i < 250; ++i)
            many.push([2 + i / 100, i % 2 ? 0.8 : 0.4]);
      sendParams(s.hub, 1, "0:1", 111, [{ title: "Vibrato", events: [[2, 0.8], [4, 0.2]] },
                                         { title: "Mic 1 level", events: many }]);
      sendParams(s.hub, 1, "0:2", 222, [{ title: "Tone", events: [[1, 1]] }, { title: "Mic 1 level", events: [[0, 0.5]] }]);
      s.live.settle();
      // Violins 1 (the hub's own track): 120 bpm, 500 ms a beat; the song 4000 ms
      const vib = buf(s, "001mslp0"), mic = buf(s, "001mslp1");
      assert.strictEqual(vib.size, 4001);
      assert.ok(near(vib.data[0], 0.3) && near(vib.data[999], 0.3));          // Live's value before the first event
      assert.ok(near(vib.data[1000], 0.8) && near(vib.data[1999], 0.8));
      assert.ok(near(vib.data[2000], 0.2) && near(vib.data[4000], 0.2));
      // Mic 1 level: 0..2; event 249 at beat 4.49 = 2245 ms (the third packet), 0.8 -> 1.6; the song's length
      assert.strictEqual(mic.size, 4001);
      assert.ok(near(mic.data[999], 0.25));
      assert.ok(near(mic.data[1000], 0.8) && near(mic.data[1005], 1.6));
      assert.ok(near(mic.data[2244], 0.8) && near(mic.data[2245], 1.6) && near(mic.data[4000], 1.6));
      assert.deepStrictEqual(s.hub.slotIds(), [[0, s.p.vib.id], [1, s.p.mic.id]]);
      // Flute (the other copy): "Tone" is "#002 Tone", "Mic 1 level" "003 Mic 1 Level"
      const tone = buf(s, "002mslp0"), flmic = buf(s, "002mslp1");
      assert.ok(near(tone.data[499], 0.6) && near(tone.data[500], 1));
      assert.ok(near(flmic.data[0], 0.5));
      assert.deepStrictEqual(s.other.slotIds(), [[0, s.p.tone.id], [1, s.p.flmic.id]]);
      // the ms factor: 16384 quarters in ms at 120 bpm
      assert.ok(s.other.out.some((m) => m[0] === 4 && m[1] === 16384 * 500));
      assert.deepStrictEqual(papplied(s, "0:1"), [["0:1", 111, "ok", "Violins 1"]]);
      assert.deepStrictEqual(papplied(s, "0:2"), [["0:2", 222, "ok", "Flute"]]);
      assert.deepStrictEqual(s.live.errors, []);
      });

test("no song length known: a table runs to the last event + 1 s", () => {
      const s = setUp();
      s.hub.message("/ms/song", [2, 120, 0, 0, 0, 2]);
      s.live.settle();
      sendParams(s.hub, 1, "0:1", 5, [{ title: "Vibrato", events: [[0, 0], [3, 1]] }]);
      s.live.settle();
      assert.strictEqual(buf(s, "001mslp0").size, 1500 + 1000 + 1);
      });

test("a title not in the track's panel: \"missing: …\", the others still driven", () => {
      const s = setUp();
      sendParams(s.hub, 1, "0:1", 5, [{ title: "Expression", events: [[0, 1]] }, { title: "Vibrato", events: [[0, 1]] },
                                       { title: "Mic 9 level", events: [[0, 1]] }]);
      s.live.settle();
      assert.deepStrictEqual(papplied(s, "0:1"), [["0:1", 5, "missing: Expression, Mic 9 level", "Violins 1"]]);
      assert.deepStrictEqual(s.hub.slotIds(), [[0, s.p.vib.id]]);
      assert.ok(near(buf(s, "001mslp0").data[0], 1));
      });

test("Live naming Kontakt's slot by its number only (\"#004\", Live 12.2 on the VM): found by the parameter id", () => {
      const s = setUp();
      const slot = s.live.param(s.k1, "#004", 0.5, 0, 1);
      sendParams(s.hub, 1, "0:1", 6, [{ title: "Tightness", id: 4, events: [[0, 1]] }, { title: "Release", id: 9, events: [[0, 1]] }]);
      s.live.settle();
      assert.deepStrictEqual(papplied(s, "0:1"), [["0:1", 6, "missing: Release", "Violins 1"]]);
      assert.deepStrictEqual(s.hub.slotIds(), [[0, slot.id]]);
      });

test("lanes 0: every parameter let go (id 0) and Live's value put back", () => {
      const s = setUp();
      sendParams(s.hub, 1, "0:1", 5, [{ title: "Vibrato", events: [[0, 1]] }, { title: "Mic 1 level", events: [[0, 1]] }]);
      s.live.settle();
      s.p.vib.value = 1;                               // (as live.remote~ left it)
      sendParams(s.hub, 2, "0:1", 6, []);
      s.live.settle();
      assert.deepStrictEqual(s.hub.slotIds().slice(2), [[0, 0], [1, 0]]);
      assert.strictEqual(s.p.vib.value, 0.3);
      assert.strictEqual(s.p.mic.value, 0.25);
      assert.deepStrictEqual(papplied(s, "0:1").pop(), ["0:1", 6, "ok", "Violins 1"]);
      // and again: the base read anew
      s.p.vib.value = 0.7;
      sendParams(s.hub, 3, "0:1", 7, [{ title: "Vibrato", events: [[2, 0]] }]);
      s.live.settle();
      assert.ok(near(buf(s, "001mslp0").data[0], 0.7));
      });

test("a changed lane: the same slot, its table rewritten, the parameter not taken again; the base kept", () => {
      const s = setUp();
      sendParams(s.hub, 1, "0:1", 5, [{ title: "Vibrato", events: [[2, 1]] }, { title: "Mic 1 level", events: [[0, 1]] }]);
      s.live.settle();
      s.p.vib.value = 0.9;                             // (driven: Live's value moves)
      sendParams(s.hub, 2, "0:1", 6, [{ title: "Mic 1 level", events: [[0, 0.5]] }, { title: "Vibrato", events: [[1, 0]] }]);
      s.live.settle();
      assert.deepStrictEqual(s.hub.slotIds(), [[0, s.p.vib.id], [1, s.p.mic.id]]);
      const vib = buf(s, "001mslp0");
      assert.ok(near(vib.data[499], 0.3) && near(vib.data[500], 0));
      assert.ok(near(buf(s, "001mslp1").data[0], 1));
      });

test("a chunk sent twice counts once; an older gen's late chunk is dropped; a newer gen replaces one incomplete", () => {
      const s = setUp();
      const ev = [];
      for (let i = 0; i < 150; ++i)
            ev.push([i / 50, i / 150]);
      const p1 = packets(1, "0:1", 1, [{ title: "Vibrato", events: ev }]);
      s.hub.message(...p1[0]);
      s.hub.message(...p1[1]);
      s.hub.message(...p1[1]);                         // chunk 0 again: not complete
      s.live.settle();
      assert.strictEqual(papplied(s, "0:1").length, 0);
      const p2 = packets(2, "0:1", 2, [{ title: "Vibrato", events: [[1, 0.5]] }]);
      for (const [a, args] of p2)
            s.hub.message(a, args);
      s.hub.message(...p1[2]);                         // gen 1's last chunk, late
      s.live.settle();
      assert.deepStrictEqual(papplied(s, "0:1"), [["0:1", 2, "ok", "Violins 1"]]);
      assert.ok(near(buf(s, "001mslp0").data[500], 0.5));
      // complete: chunk 0 twice, then chunk 1
      const p3 = packets(3, "0:1", 3, [{ title: "Vibrato", events: ev }]);
      s.hub.message(...p3[0]);
      s.hub.message(...p3[1]);
      s.hub.message(...p3[1]);
      s.hub.message(...p3[2]);
      s.live.settle();
      assert.deepStrictEqual(papplied(s, "0:1").pop(), ["0:1", 3, "ok", "Violins 1"]);
      assert.ok(near(buf(s, "001mslp0").data[2980], 149 / 150));   // (beat 2.98, from the second chunk)
      });

test("more lanes than 16 slots: the rest said, the 16 driven", () => {
      const s = setUp();
      const lanes = [{ title: "Vibrato", events: [[0, 1]] }];
      for (let i = 1; i <= 17; ++i) {
            s.live.param(s.live.objects[s.k1.id], "Macro " + i, 0, 0, 1);
            lanes.push({ title: "Macro " + i, events: [[0, 0.5]] });
            }
      sendParams(s.hub, 1, "0:1", 9, lanes);
      s.live.settle();
      assert.deepStrictEqual(papplied(s, "0:1"), [["0:1", 9, "too many lanes (16 at most): Macro 16, Macro 17", "Violins 1"]]);
      assert.strictEqual(s.hub.slotIds().length, 16);
      assert.ok(s.hub.slotIds().every(([k, id]) => k >= 0 && k < 16 && id > 0));
      });

test("a track without a plug-in, a route with no track, a track without the device", () => {
      const s = setUp();
      const bare = s.live.track("Harp", { inputType: "MuseScore A", inputChannel: "Ch. 3" });
      const d3 = s.live.device(bare, "MxDeviceMidiEffect", "MuseScore Link");
      const harp = loadDevice(s.live, s.shared, d3.id);
      harp.bang();
      harp.message("prefix", ["003mslp"]);
      clip(s.hub, 1, "0:3", "MuseScore A", 3, "Harp");
      s.live.settle();
      sendParams(s.hub, 1, "0:3", 4, [{ title: "Vibrato", events: [[0, 1]] }]);
      s.live.settle();
      assert.deepStrictEqual(papplied(s, "0:3"), [["0:3", 4, "no plug-in on the track", "Harp"]]);
      sendParams(s.hub, 1, "9:9", 4, [{ title: "Vibrato", events: [[0, 1]] }]);
      s.live.settle();
      assert.ok(papplied(s, "9:9")[0][2].startsWith("no track"));
      const cel = s.live.track("Celli", { inputType: "MuseScore B", inputChannel: "Ch. 4" });
      s.live.param(s.live.device(cel, "PluginDevice", "Kontakt 8"), "Vibrato", 0, 0, 1);
      clip(s.hub, 1, "1:4", "MuseScore B", 4, "Celli");
      sendParams(s.hub, 1, "1:4", 8, [{ title: "Vibrato", events: [[0, 1]] }]);
      s.live.settle();
      assert.strictEqual(papplied(s, "1:4").length, 0);
      s.hub.api.state().waiting["1:4"].since -= 4000;   // (3 s without an answer)
      s.live.settle();
      assert.ok(papplied(s, "1:4")[0][2].startsWith("no MuseScore Link device"));
      });

test("a copy loaded later finds its track's lanes; two copies on a track: the first drives", () => {
      const s = setUp();
      sendParams(s.hub, 1, "0:2", 3, [{ title: "Tone", events: [[0, 0]] }]);
      s.live.settle();
      // a second copy on Flute, after the first: lets go
      const d = s.live.device(s.fl, "MxDeviceMidiEffect", "MuseScore Link");
      const late = loadDevice(s.live, s.shared, d.id);
      late.message("prefix", ["004mslp"]);
      late.bang();
      s.live.settle();
      assert.deepStrictEqual(late.slotIds(), []);
      assert.deepStrictEqual(s.other.slotIds(), [[0, s.p.tone.id]]);
      // the first goes: the late one, now first, takes over from the entry at its next beat
      s.other.call("notifydeleted()");
      s.fl.devices = s.fl.devices.filter((x) => x !== s.other.api.state().me.device);
      late.call("paramsCheck(true)");
      s.live.settle();
      assert.deepStrictEqual(late.slotIds(), [[0, s.p.tone.id]]);
      assert.ok(near(buf(s, "004mslp0").data[0], 0));
      });

test("Live's tempo changed: the tables rewritten at the next beat, the ms factor with them", () => {
      const s = setUp();
      sendParams(s.hub, 1, "0:1", 5, [{ title: "Vibrato", events: [[2, 0.8]] }]);
      s.live.settle();
      s.live.song.tempo = 60;
      s.hub.runTasks();                                // (the heartbeat, among the hub's Tasks)
      s.live.settle();
      const vib = buf(s, "001mslp0");
      assert.strictEqual(vib.size, 8001);
      assert.ok(near(vib.data[1999], 0.3) && near(vib.data[2000], 0.8));
      assert.ok(s.hub.out.some((m) => m[0] === 4 && m[1] === 16384 * 1000));
      assert.deepStrictEqual(s.hub.slotIds(), [[0, s.p.vib.id]]);      // not taken again
      });

test("resync: MuseScore's lanes again are answered again, nothing taken twice", () => {
      const s = setUp();
      const lanes = [{ title: "Vibrato", events: [[2, 0.8]] }];
      sendParams(s.hub, 1, "0:1", 5, lanes);
      s.live.settle();
      s.hub.call("resync()");
      assert.strictEqual(s.hub.sent("/live/resync").length, 1);
      sendParams(s.hub, 2, "0:1", 5, lanes);
      s.live.settle();
      assert.deepStrictEqual(papplied(s, "0:1"), [["0:1", 5, "ok", "Violins 1"], ["0:1", 5, "ok", "Violins 1"]]);
      assert.deepStrictEqual(s.hub.slotIds(), [[0, s.p.vib.id]]);
      });

test("a route cleared: its parameters let go, nothing answered", () => {
      const s = setUp();
      sendParams(s.hub, 1, "0:2", 3, [{ title: "Tone", events: [[0, 0]] }]);
      s.live.settle();
      s.hub.message("/ms/clear", [2, "0:2", "MuseScore A", 2, "Flute", "MuseScore: Flute"]);
      s.live.settle();
      assert.deepStrictEqual(s.other.slotIds(), [[0, s.p.tone.id], [0, 0]]);
      assert.strictEqual(papplied(s, "0:2").length, 1);
      });

test("probes: the ms position (snapshot~), a LOM path, a call", () => {
      const s = setUp();
      s.hub.message("/ms/probe", [7, "pos"]);
      assert.ok(s.hub.out.some((m) => m[0] === 5 && m[1] === "bang"));
      s.hub.message("posvalue", [1234.5]);
      assert.ok(/^pos 1234.5 ms; factor 8192000; tempo 120/.test(s.hub.sent("/live/probe").pop()[1]));
      s.hub.message("/ms/probe", [8, "live_set tracks 0"]);
      const t = s.hub.sent("/live/probe").pop();
      assert.strictEqual(t[0], 8);
      assert.ok(t[1].includes("type Track"));
      s.hub.message("/ms/probecall", [9, "live_set tracks 1", "get", "name"]);
      assert.deepStrictEqual(s.hub.sent("/live/probe").pop(), [9, '["Flute"]']);
      s.live.song.is_playing = 1;
      s.hub.message("/ms/probecall", [10, "live_set", "stop_playing"]);
      assert.strictEqual(s.live.song.is_playing, 0);
      s.hub.message("/ms/probecall", [11, "live_set", "no_such_call"]);
      assert.ok(s.hub.sent("/live/probe").pop()[1].startsWith("error"));
      s.hub.message("/ms/probe", [12, "state"]);
      assert.ok(s.hub.sent("/live/probe").pop()[1].includes("\"prefix\":\"001mslp\""));
      });

test("titles match as Vst3Plugin::looseTitle", () => {
      const s = setUp();
      const l = s.hub.api.looseTitle;
      assert.strictEqual(l("#002 Tone"), "tone");
      assert.strictEqual(l("07 Mic 1 level"), l("MIC 1 Level"));
      assert.strictEqual(l("3: Vibrato"), "vibrato");
      assert.strictEqual(l("4) Release"), "release");
      assert.strictEqual(l("Mic 1 level"), "mic1level");
      });

console.log(failures ? failures + " failed" : "all passed");
process.exitCode = failures ? 1 : 0;
