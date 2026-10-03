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

test("the hub says protocol 6 (parameter lanes from 3 on, velocity curves from 6)", () => {
      const s = setUp();
      assert.strictEqual(s.hub.sent("/live/hello")[0][1], 6);
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

test("let go of a parameter Live also automates: Live's automation re-enabled, its value not set over it", () => {
      const s = setUp();
      s.p.vib.automation_state = 1;                    // (the track automates Vibrato)
      sendParams(s.hub, 1, "0:1", 5, [{ title: "Vibrato", events: [[0, 1]] }]);
      s.live.settle();
      s.live.calls.length = 0;
      sendParams(s.hub, 2, "0:1", 6, []);
      s.live.settle();
      assert.ok(s.live.calls.some((c) => c[0] === "call" && c[2] === "re_enable_automation"));
      assert.ok(!s.live.calls.some((c) => c[0] === "set" && c[1] === "param" && c[2] === "value"));
      assert.deepStrictEqual(s.hub.slotIds().pop(), [0, 0]);
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

// the lanes kept in the Live Set: outlet 6 "k atoms…" to [pattr Lanes] … (make_device.py STORES), given back as "lanes k …"
const stores = (dev) => dev.out.filter((m) => m[0] === 6).map((m) => (Array.isArray(m[1]) ? m[1] : m.slice(1)));

test("the lanes kept in the set: sent to the stores when applied, played from them without MuseScore", () => {
      const s = setUp();
      sendParams(s.hub, 1, "0:1", 111, [{ title: "Vibrato", events: [[2, 0.8], [4, 0.2]] }]);
      s.live.settle();
      const sent = stores(s.hub);
      assert.strictEqual(sent.length, 4);                       // every store: the value in store 0, the others empty
      assert.strictEqual(sent.map((m) => m[0]).join(), "0,1,2,3");
      assert.strictEqual(sent[0][1], "msl-lanes");
      assert.strictEqual(sent[0][5], 1);                         // one part
      assert.strictEqual(JSON.stringify(sent[1].slice(1)), JSON.stringify(["msl-lanes", 2, 0, 1, 0]));
      // the same lanes again: nothing sent; their echo from the stores: left alone
      sendParams(s.hub, 2, "0:1", 111, [{ title: "Vibrato", events: [[2, 0.8], [4, 0.2]] }]);
      s.live.settle();
      assert.strictEqual(stores(s.hub).length, 4);
      for (const m of sent)
            s.hub.message("lanes", m);
      assert.strictEqual(s.hub.call("savedSerial"), 0);
      // a new set: a copy of the device on the violin's track, no hub entry (MuseScore not running): plays the stores
      const t = setUp();
      t.live.settle();
      t.shared.musescore_link["p" + t.vln.id] = undefined;
      for (const m of sent)
            t.hub.message("lanes", m);
      t.live.settle();
      const vib = buf(t, "001mslp0");
      assert.ok(near(vib.data[999], 0.3) && near(vib.data[1000], 0.8) && near(vib.data[2000], 0.2));
      assert.deepStrictEqual(t.hub.slotIds(), [[0, t.p.vib.id]]);
      });

test("the lanes kept in the set: a long value over several stores, put together only when all parts are there", () => {
      const s = setUp();
      const many = [];                                           // (values nothing packs: 40000 atoms)
      let seed = 1;
      const rnd = () => (seed = (seed * 16807) % 2147483647) / 2147483647;
      for (let i = 0; i < 20000; ++i)
            many.push([i / 1000, Math.round(rnd() * 1000) / 1000]);
      sendParams(s.hub, 1, "0:1", 111, [{ title: "Vibrato", events: many }]);
      s.live.settle();
      const sent = stores(s.hub);
      assert.ok(sent.every((m) => m.length - 1 <= 30000));
      assert.strictEqual(sent[0][5], 2);                         // two parts
      const t = setUp();
      t.shared.musescore_link["p" + t.vln.id] = undefined;
      t.hub.message("lanes", sent[1]);                           // (the parts in any order)
      t.live.settle();
      assert.strictEqual(t.hub.call("saved"), null);
      t.hub.message("lanes", sent[0]);
      t.live.settle();
      const sv = t.hub.call("saved");
      assert.strictEqual(sv.routes["0:1"].lanes[0].ev.length, 40000);
      assert.ok(near(sv.routes["0:1"].lanes[0].ev[1], many[0][1]));
      // too long for the stores: nothing kept (an empty value), the lanes still played
      const u = setUp();
      const huge = [];
      for (let i = 0; i < 70000; ++i)
            huge.push([i / 1000, Math.round(rnd() * 1000) / 1000]);
      sendParams(u.hub, 1, "0:1", 111, [{ title: "Vibrato", events: huge }]);
      u.live.settle();
      const last = stores(u.hub).filter((m) => m[0] === 0).pop();
      assert.strictEqual(JSON.stringify(last.slice(1, 3)), JSON.stringify(["msl-lanes", 2]));
      assert.strictEqual(last[5], 1);
      assert.ok(last.length < 20);
      assert.strictEqual(u.hub.call("keptStatus").indexOf("too many"), 0);
      });

test("the blob as Create Live Set writes it (livesetwriter.cpp linkBlob, tst_liveintegration): the set plays its lanes", () => {
      // Max's dictionary as in the set; Max gives each store's list to [pattr], which outputs it as "lanes k …"
      const blob = '{\r\n\t"Port" : [ 9001 ],\r\n\t"Lanes" : [ "msl-lanes", 2, 47975, 0, 1, 8, 1, "0:1", 4243, 1, ' +
                   '"Vibrato", 1, 4, 0, 0.5, 3840, 1 ]\r\n}\r\n';
      const d = JSON.parse(blob);
      const t = setUp();
      t.shared.musescore_link["p" + t.vln.id] = undefined;
      t.hub.message("lanes", [0].concat(d.Lanes));
      t.live.settle();
      const vib = buf(t, "001mslp0");
      assert.ok(near(vib.data[0], 0.5) && near(vib.data[499], 0.5) && near(vib.data[500], 1));
      assert.strictEqual(vib.size, 4001);                         // the song: 8 beats at 120 bpm
      });

// MuseScore's events for a lane of points as rendermidi plays them (Automation::Lane::events: the points, then along
// a ramp each tick where the value reaches another step of a parameter's resolution, 1e-4), at 120 bpm: ticks ->
// units (480 ticks a beat)
const RES = 1e-4, PACK_DV = 1 / 127, TICK = UNITS / 480;
function rendered(points, bend) {
      const ev = [];
      let last = null;
      const put = (tick, v) => {
            ev.push(Math.round(tick / 480 * UNITS), v);
            last = v;
            };
      for (let i = 0; i < points.length; ++i) {
            const [tick, v, ramp] = points[i];
            put(tick, v);
            const nx = points[i + 1];
            if (!ramp || !nx)
                  continue;
            for (let t = tick + 1; t < nx[0]; ++t) {
                  const x = (t - tick) / (nx[0] - tick), y = bend ? Math.pow(x, 2.5) : x;   // (a curved ramp: some bend)
                  const val = v + (nx[1] - v) * y;
                  if (Math.round(val / RES) !== Math.round(last / RES))
                        put(t, val);
                  }
            }
      return ev;
      }

// the staircase a lane's events play: the value at each unit in [0, end)
function staircase(ev, end) {
      const out = new Float64Array(end);
      let v = 0, j = 0;
      for (let t = 0; t < end; ++t) {
            while (j + 1 < ev.length && ev[j] <= t) {
                  v = ev[j + 1];
                  j += 2;
                  }
            out[t] = v;
            }
      return out;
      }

// the largest difference between two staircases, away from (more than a tick from) the first's event times
function worstDiff(ev, back, end) {
      const a = staircase(ev, end), b = staircase(back, end), near2 = new Uint8Array(end);
      for (let i = 0; i < ev.length; i += 2)
            for (let d = -TICK; d <= TICK; ++d)
                  if (ev[i] + d >= 0 && ev[i] + d < end)
                        near2[ev[i] + d] = 1;
      let worst = 0;
      for (let t = 0; t < end; ++t)
            if (!near2[t])
                  worst = Math.max(worst, Math.abs(a[t] - b[t]));
      return worst;
      }

test("lanes packed for the set: straight ramps one run each, curves a few; played as MuseScore's staircase to one step", () => {
      const s = setUp();
      const pts = [];
      for (let b = 0; b < 64; b += 2)                            // a point every 2 beats, ramps up and down
            pts.push([b * 480, (b / 2) % 2 ? 0.2 : 0.9, true]);
      for (const bend of [false, true]) {
            const ev = rendered(pts, bend);
            const packed = s.hub.api.packLane(ev);
            const back = s.hub.api.unpackLane(packed);
            const worst = worstDiff(ev, back, 64 * UNITS);
            assert.ok(worst <= PACK_DV + 1e-9, "worst " + worst);       // (a step's value within one MIDI step, its time within a tick)
            if (!bend)
                  assert.ok(packed.length <= 32 * 6, packed.length + " atoms for " + ev.length / 2 + " events");
            else
                  assert.ok(packed.length < ev.length / 3, packed.length + " atoms for " + ev.length / 2 + " events");
            }
      });

test("a 10-minute piece with 10 curved lanes fits the stores and comes back", () => {
      const s = setUp();
      // 120 bpm, 10 min = 1200 beats; each lane: a point every 2 beats, curved ramps, a step now and then
      const lanes = [];
      for (let l = 0; l < 10; ++l) {
            const pts = [];
            for (let b = 0; b < 1200; b += 2)
                  pts.push([b * 480, ((b * 7 + l * 13) % 10) / 10, (b / 2 + l) % 5 !== 0]);
            const ev = rendered(pts, l % 2 === 1), events = [];
            for (let i = 0; i < ev.length; i += 2)
                  events.push([ev[i] / UNITS, ev[i + 1]]);
            lanes.push({ title: "Vibrato", events: events });
            }
      lanes.forEach((l, i) => { l.title = ["Vibrato", "Mic 1 level"][i % 2] + (i > 1 ? " " + i : ""); });
      s.hub.message("/ms/song", [2, 120, 1200 * UNITS, 0, 0, 2]);
      sendParams(s.hub, 1, "0:1", 111, lanes);
      s.live.settle();
      const atoms = stores(s.hub).filter((m) => m[1] === "msl-lanes" && m[5] > 0).reduce((n, m) => n + m.length - 1, 0);
      const raw = lanes.reduce((n, l) => n + 2 * l.events.length, 0);
      console.log("    10 min x 10 lanes: " + raw + " event atoms -> " + atoms + " stored atoms in " +
                  stores(s.hub).filter((m) => m[5] > 0).length + " store(s)");
      assert.strictEqual(s.hub.call("keptStatus"), "");
      // a duplicated device (the stores' values, no hub entry) has the same lanes
      const t = setUp();
      t.shared.musescore_link["p" + t.vln.id] = undefined;
      for (const m of stores(s.hub))
            t.hub.message("lanes", m);
      t.live.settle();
      const a = s.hub.call("saved").routes["0:1"].lanes, b = t.hub.call("saved").routes["0:1"].lanes;
      assert.strictEqual(b.length, 10);
      for (let l = 0; l < 10; ++l) {
            const worst = worstDiff(a[l].ev, b[l].ev, 1200 * UNITS);
            assert.ok(worst <= PACK_DV + 1e-6, "lane " + l + " worst " + worst);
            }
      });

test("the stores are set once MuseScore's edits pause (one Live undo step), playback follows each edit at once", () => {
      const s = setUp();
      const before = stores(s.hub).length;
      for (let g = 1; g <= 5; ++g) {
            sendParams(s.hub, g, "0:1", 100 + g, [{ title: "Vibrato", events: [[2, g / 10]] }]);
            s.hub.work();
            s.hub.runScheduled();                                  // (the copy's work: the table; not the store's delay)
            assert.ok(near(buf(s, "001mslp0").data[1000], g / 10));      // (played at once)
            }
      assert.strictEqual(stores(s.hub).length, before);           // nothing stored yet
      s.live.settle();                                             // (the delay's Task)
      const now = stores(s.hub).slice(before);
      assert.ok(now.length >= 1 && now.length <= 4);
      assert.ok(near(s.hub.call("saved").routes["0:1"].lanes[0].ev[1], 0.5));
      });

test("a straight ramp's steps pack to one run, as livesetwriter.cpp packLane does (tst_liveintegration)", () => {
      const s = setUp(), ev = [];
      for (let i = 0; i <= 32; ++i)
            ev.push(i * 240, Math.fround(0.9 - 0.7 * i / 32));
      ev.push(7680 + 3840, Math.fround(0.5));
      const p = s.hub.api.packLane(ev);
      assert.strictEqual(p.length, 8);
      assert.deepStrictEqual([p[0], p[1], p[3], p[6]], [-33, 0, 7680, 11520]);
      assert.strictEqual(p[4], ev[65]);
      });

console.log(failures ? failures + " failed" : "all passed");
process.exitCode = failures ? 1 : 0;
