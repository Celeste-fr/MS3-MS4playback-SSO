// Tests of the device's side of a clip tab playing at the song's tempo (MuseScoreLink.js, protocol 6): an edited
// arrangement clip's place in the song (/live/clip/span) and Live's tempo reported as soon as it changes
// (/live/transport). Against the stand-in Live (fakelive.js). Run: node tools/live/test/test_cliptempo.js.
// MuseScore's side: mtest/libmscore/liveintegration (clipTempo*).

"use strict";
const assert = require("assert");
const { FakeLive, loadDevice } = require("./fakelive");

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

// "Keys" (the hub) with an arrangement clip from beat 8 to 16 and a session clip
function setUp() {
      const live = new FakeLive();
      const keys = live.track("Keys");
      const d1 = live.device(keys, "MxDeviceMidiEffect", "MuseScore Link");
      const arr = live.clip(keys, "Swell", 8, 16);
      const slot = live.clipSlot(keys);
      const clip = live.add({ kind: "clip", name: "Riff", notes: [], parent: slot.id, is_midi_clip: 1,
                              signature_numerator: 4, signature_denominator: 4, loop_start: 0, loop_end: 4,
                              start_marker: 0, end_marker: 4, looping: 1 });
      slot.clip = clip.id;
      const hub = loadDevice(live, {}, d1.id);
      hub.bang();
      return { live, keys, arr, slot, clip, hub };
      }

function edit(s, clip) {
      s.live.view.detail_clip = clip.id;
      s.hub.message("edit", []);
      s.hub.work();
      }

test("an arrangement clip: its place in the song right after its place in the set", () => {
      const s = setUp();
      edit(s, s.arr);
      const key = "c" + s.arr.id;
      assert.deepStrictEqual(s.hub.sent("/live/clip/span"), [[key, 8, 16, 0, 8, 0, 8, 0]]);
      const order = s.hub.out.filter((m) => m[0] === 0).map((m) => m[1]).filter((a) => /^\/live\/clip\/(where|span)/.test(a));
      assert.deepStrictEqual(order, ["/live/clip/where", "/live/clip/span"]);
      });

test("a session clip: no place in the song", () => {
      const s = setUp();
      edit(s, s.clip);
      assert.deepStrictEqual(s.hub.sent("/live/clip/span"), []);
      });

test("the clip moved, looped or its start marker moved in Live: its place again (once each), nothing when unchanged", () => {
      const s = setUp();
      edit(s, s.arr);
      const key = "c" + s.arr.id;
      s.hub.api.checkEdits();
      assert.strictEqual(s.hub.sent("/live/clip/span").length, 1);
      s.arr.start_time = 12;
      s.arr.end_time = 20;
      s.hub.api.checkEdits();
      s.hub.api.checkEdits();
      assert.deepStrictEqual(s.hub.sent("/live/clip/span").slice(1), [[key, 12, 20, 0, 8, 0, 8, 0]]);
      s.arr.looping = 1;
      s.arr.loop_start = 2;
      s.arr.loop_end = 6;
      s.arr.start_marker = 1;
      s.arr.end_time = 30;
      s.hub.api.checkEdits();
      assert.deepStrictEqual(s.hub.sent("/live/clip/span").slice(2), [[key, 12, 30, 1, 8, 2, 6, 1]]);
      assert.strictEqual(s.hub.sent("/live/clip/where").length, 1);       // (the place in the set: unchanged)
      });

test("Live's tempo changed while stopped: /live/transport at once (not only once a second)", () => {
      const s = setUp();
      s.hub.api.report();
      const n = s.hub.sent("/live/transport").length;
      s.hub.api.report();
      assert.strictEqual(s.hub.sent("/live/transport").length, n);        // nothing changed (within the second)
      s.live.song.tempo = 97.5;
      s.hub.api.report();
      const t = s.hub.sent("/live/transport");
      assert.strictEqual(t.length, n + 1);
      assert.deepStrictEqual(t[t.length - 1], [0, 0, 97.5]);
      });

if (failures) {
      console.log(failures + " failed");
      process.exit(1);
      }
console.log("all passed");
