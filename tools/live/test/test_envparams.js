// Tests of the device's side of automation lanes on any Live track (MuseScoreLink.js, protocol 5): the track's
// parameters (/live/params) and an edited clip's place (/live/clip/where) for MuseScore and the MuseScore Envelopes
// script; a route's "live:<d>/<p>" lane driving that parameter of its track. Against the stand-in Live (fakelive.js).
// Run: node tools/live/test/test_envparams.js. The script's side: test_envelopes.py; MuseScore's: tst_liveintegration.

"use strict";
const assert = require("assert");
const { FakeLive, loadDevice } = require("./fakelive");

const UNITS = 3840;
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

// "Keys" (a synth rack: MuseScore Link, Operator, an EQ) with a session clip in its second slot; "Pad" with an
// arrangement clip; the device on Keys only (the hub)
function setUp() {
      const live = new FakeLive();
      const keys = live.track("Keys");
      const pad = live.track("Pad");
      const d1 = live.device(keys, "MxDeviceMidiEffect", "MuseScore Link");
      const op = live.device(keys, "Operator", "Operator");
      const eq = live.device(keys, "Eq8", "EQ Eight");
      const p = {
            on: live.param(op, "Device On", 1, 0, 1),
            algo: live.param(op, "Algorithm", 0, 0, 10),
            tone: live.param(op, "Tone", 0.7, 0, 1),
            gain: live.param(eq, "1 Gain A", 0, -15, 15),
            };
      p.algo.is_quantized = 1;
      live.clipSlot(keys);
      const slot = live.clipSlot(keys);
      const clip = live.add({ kind: "clip", name: "Riff", notes: [live.note({ pitch: 60, start_time: 0, duration: 1 })],
                              parent: slot.id, is_midi_clip: 1, signature_numerator: 4, signature_denominator: 4,
                              loop_start: 0, loop_end: 4, start_marker: 0, end_marker: 4, looping: 1 });
      slot.clip = clip.id;
      const arr = live.clip(pad, "Swell", 8, 16);
      const hub = loadDevice(live, {}, d1.id);
      hub.bang();
      hub.message("prefix", ["001mslp"]);
      return { live, keys, pad, op, eq, p, slot, clip, arr, hub };
      }

function params(dev, key) {
      const out = [];
      let hash = null;
      for (const m of dev.sent("/live/params").filter((x) => x[0] === key)) {
            hash = m[1];
            for (let i = 4; i + 5 < m.length; i += 6)
                  out.push(m.slice(i, i + 6));
            }
      return { hash, list: out };
      }

test("Edit in MuseScore on a session clip: its place and the track's parameters (mixer, devices; not MuseScore Link)", () => {
      const s = setUp();
      s.live.view.detail_clip = s.clip.id;
      s.hub.message("edit", []);
      s.hub.work();
      const key = "c" + s.clip.id;
      assert.deepStrictEqual(s.hub.sent("/live/clip/where"), [[key, 0, 1]]);
      const { list } = params(s.hub, key);
      assert.deepStrictEqual(list, [
            [-1, 0, "Mixer › Volume", 0, 1, 0], [-1, 1, "Mixer › Pan", -1, 1, 0],
            [1, 0, "Operator › Device On", 0, 1, 0], [1, 1, "Operator › Algorithm", 0, 10, 1],
            [1, 2, "Operator › Tone", 0, 1, 0], [2, 0, "EQ Eight › 1 Gain A", -15, 15, 0]]);
      // (16 a packet: one here; the order: begin, notes, track, where, params)
      const order = s.hub.out.filter((m) => m[0] === 0).map((m) => m[1]).filter((a) => /^\/live\/(clip|params)/.test(a));
      assert.deepStrictEqual(order.slice(-4), ["/live/clip/notes", "/live/clip/track", "/live/clip/where", "/live/params"]);
      });

test("an arrangement clip: slot -1 (Live's API has no envelopes for it; MuseScore says so)", () => {
      const s = setUp();
      s.live.view.detail_clip = s.arr.id;
      s.hub.message("edit", []);
      s.hub.work();
      assert.deepStrictEqual(s.hub.sent("/live/clip/where"), [["c" + s.arr.id, 1, -1]]);
      assert.deepStrictEqual(params(s.hub, "c" + s.arr.id).list.map((x) => x[2]), ["Mixer › Volume", "Mixer › Pan"]);
      });

test("a device added in Live: the parameters again (once); the track moved: the place again", () => {
      const s = setUp();
      s.live.view.detail_clip = s.clip.id;
      s.hub.message("edit", []);
      s.hub.work();
      const key = "c" + s.clip.id;
      s.hub.api.checkEdits();
      assert.strictEqual(s.hub.sent("/live/params").length, 1);          // nothing changed: nothing sent
      assert.strictEqual(s.hub.sent("/live/clip/where").length, 1);
      const comp = s.live.device(s.keys, "Compressor2", "Compressor");
      s.live.param(comp, "Threshold", 0.5, 0, 1);
      s.hub.api.checkEdits();
      s.hub.api.checkEdits();
      const sent = s.hub.sent("/live/params").filter((m) => m[0] === key);
      assert.strictEqual(sent.length, 2);
      assert.notStrictEqual(sent[0][1], sent[1][1]);                       // (another hash)
      assert.ok(params(s.hub, key).list.some((x) => x[2] === "Compressor › Threshold"));
      // a track inserted before Keys
      const t = s.live.track("New");
      s.live.song.tracks = [t.id].concat(s.live.song.tracks.filter((x) => x !== t.id));
      s.hub.api.checkEdits();
      assert.deepStrictEqual(s.hub.sent("/live/clip/where").pop(), [key, 1, 1]);
      });

test("a route's lanes titled live:<d>/<p> drive that parameter of its track, no plug-in needed; the mixer's too", () => {
      const s = setUp();
      s.hub.message("/ms/song", [1, 120, 8 * UNITS, 0, 0, 1]);
      s.hub.message("/ms/track", [1, "0:1", "", 1, "Keys", "MuseScore: Keys", 1, 8 * UNITS, 1, 1, 7]);
      s.hub.message("/ms/notes", [1, "0:1", 0, 60, 0, UNITS, 80, 0]);
      s.live.settle();
      // its parameters came after the clip was placed
      assert.ok(params(s.hub, "0:1").list.some((x) => x[2] === "Operator › Tone"));
      const lanes = [["live:1/2", [[0, 0.2], [2, 0.9]]], ["live:-1/0", [[0, 0.5]]], ["live:7/0", [[0, 1]]]];
      s.hub.message("/ms/params", [2, "0:1", lanes.length, 77]);
      lanes.forEach(([title, ev], i) => {
            const args = [2, "0:1", i, title, -1, 0, 1];
            for (const [b, v] of ev)
                  args.push(b * UNITS, v);
            s.hub.message("/ms/pvals", args);
            });
      s.live.settle();
      const vol = s.live.objects[s.live.mixer(s.keys).volume];
      assert.deepStrictEqual(s.hub.slotIds(), [[0, s.p.tone.id], [1, vol.id]]);
      const tone = s.live.buffers["001mslp0"];
      assert.ok(Math.abs(tone.data[0] - 0.2) < 1e-6 && Math.abs(tone.data[1000] - 0.9) < 1e-6);
      const st = s.hub.sent("/live/papplied").filter((m) => m[0] === "0:1").pop();
      assert.deepStrictEqual(st, ["0:1", 77, "missing: live:7/0", "Keys"]);
      assert.deepStrictEqual(s.live.errors, []);
      });

test("a route's clip written again: its parameters not again; /ms/params/ask: all again", () => {
      const s = setUp();
      s.hub.message("/ms/song", [1, 120, 8 * UNITS, 0, 0, 1]);
      for (const gen of [1, 2]) {
            s.hub.message("/ms/track", [gen, "0:1", "", 1, "Keys", "MuseScore: Keys", 1, 8 * UNITS, 1, 1, 7 + gen]);
            s.hub.message("/ms/notes", [gen, "0:1", 0, 60 + gen, 0, UNITS, 80, 0]);
            s.live.settle();
            }
      assert.strictEqual(s.hub.sent("/live/params").filter((m) => m[0] === "0:1").length, 1);
      s.hub.message("/ms/params/ask", []);
      s.hub.call("checkRouteParams()");
      assert.strictEqual(s.hub.sent("/live/params").filter((m) => m[0] === "0:1").length, 2);
      });

if (failures)
      process.exitCode = 1;
console.log(failures ? failures + " failed" : "all passed");
