// Tests of the device's side of written clips (MuseScoreLink.js › Written clips, protocol 9; libmscore/livetiming.h): the
// timing record Create Live Set keeps in the device's saved data ([pattr Timing]), the ring of codes the patcher reads
// (a note-on's on channel 1, a note-off's on channel 2, at the written note's song tick and the one on each side), the
// patcher's gates and defaults, the bias for Live's Track Delay; the patcher's own path: test_patch.js › written clips.
// Against the stand-in Live (fakelive.js). Run: node tools/live/test/test_timing.js. MuseScore's side: tst_liveequivalence
// plainWritten.

"use strict";
const assert = require("assert");
const { FakeLive, loadDevice } = require("./fakelive");

const TICKS = 480, UNITS = 3840;
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

// a technique track ("Violins 1 – Long") with MuseScore Link on it, playing the arrangement
function setUp() {
      const live = new FakeLive();
      const t = live.track("Violins 1 – Long", { playing_slot_index: -1, fired_slot_index: -1, back_to_arranger: 0 });
      const dev = live.device(t, "MxDeviceMidiEffect", "MuseScore Link");
      live.thisDevice = dev.id;
      const d = loadDevice(live, {}, dev.id);
      d.bang();
      d.message("prefix", ["001mslp"]);
      d.message("dspsr", [44100]);
      d.message("dspvs", [64]);
      return { live, t, d };
      }

// a record as LiveTiming::record writes it: lookahead 86, most notes on 0 off 86; two exceptions
const REC = ["msl-timing", 1, 86, 0, 86, 2,
             4 * UNITS, 5 * UNITS, 62, 30.5, 86,             // beat 4: capped (on 30.5 ms later), its end as most
             6 * UNITS, 7 * UNITS, 64, 0, 0];                // beat 6: the note after it starts early: its end cut
const nine = (s) => s.d.out.filter((m) => m[0] === 9).map((m) => m.slice(1));

test("the record: lookahead, defaults, exceptions (start end pitch on off)", () => {
      const s = setUp();
      const r = JSON.parse(JSON.stringify(s.d.api.timParse(REC)));      // (the script's context's objects)
      assert.deepStrictEqual(r, { look: 86, on: 0, off: 86, ex: [
            { start: 4 * UNITS, end: 5 * UNITS, pitch: 62, on: 30.5, off: 86 },
            { start: 6 * UNITS, end: 7 * UNITS, pitch: 64, on: 0, off: 0 }] });
      assert.strictEqual(s.d.api.timParse(["msl-lanes", 2]), null);
      assert.strictEqual(s.d.api.timParse(["msl-timing", 2, 0, 0, 0, 0]), null);      // (another version: none)
      });

test("the cells: an exception's code (1 + ms) at its tick and on each side; a cell's own note wins; else 0 (the default)", () => {
      const s = setUp();
      const r = s.d.api.timParse(REC);
      const c = s.d.api.timCells(r, 4 * TICKS - 5, 3 * TICKS + 10);
      const at = (tick) => tick - (4 * TICKS - 5);
      assert.strictEqual(c.on[at(4 * TICKS)], 31.5);
      assert.strictEqual(c.on[at(4 * TICKS - 1)], 31.5);
      assert.strictEqual(c.on[at(4 * TICKS + 1)], 31.5);
      assert.strictEqual(c.on[at(4 * TICKS + 2)], 0);
      assert.strictEqual(c.off[at(5 * TICKS)], 87);
      assert.strictEqual(c.on[at(6 * TICKS)], 1);           // (on 0 ms: code 1, not the default's 0 though the same)
      assert.strictEqual(c.off[at(7 * TICKS)], 1);
      // two notes a tick apart: each keeps its own cell
      const r2 = s.d.api.timParse(["msl-timing", 1, 50, 0, 50, 2, 8, 16, 60, 10, 50, 16, 24, 61, 20, 50]);
      const c2 = s.d.api.timCells(r2, 0, 6);
      assert.deepStrictEqual(Array.from(c2.on.slice(0, 4)), [11, 11, 21, 21]);
      });

test("a record from the set: the gates open, the defaults and the bias (the vector plus the lookahead) go to the patcher; the ring is filled", () => {
      const s = setUp();
      assert.deepStrictEqual(nine(s).filter((m) => /^t/.test(m[0])), []);            // (none without a record)
      s.live.song.current_song_time = 3.5;
      s.d.message("timing", REC);
      const n = nine(s);
      assert.ok(n.some((m) => m[0] === "tgate" && m[1] === 1));
      assert.ok(n.some((m) => m[0] === "tdef" && m[1] === 0 && m[2] === 86));
      const bias = n.filter((m) => m[0] === "tbias").pop()[1];
      const vector = 64 / 44100 * 120 / 60 * TICKS, look = 0.086 * 120 / 60 * TICKS;
      assert.ok(Math.abs(bias - (vector + look)) < 1e-9);
      const buf = s.live.buffers["001mslt"];
      assert.strictEqual(buf.channels, 2);
      const ring = s.d.api.VEL_RING;
      assert.strictEqual(buf.chans[0][(4 * TICKS) % ring], 31.5);
      assert.strictEqual(buf.chans[1][(5 * TICKS) % ring], 87);
      assert.strictEqual(buf.chans[0][(6 * TICKS) % ring], 1);
      assert.strictEqual(buf.chans[1][(7 * TICKS) % ring], 1);
      assert.strictEqual(buf.chans[0][(5 * TICKS) % ring], 0);
      });

test("a session clip playing on the track: the gates closed (every note straight on); back to the arrangement: open", () => {
      const s = setUp();
      s.d.message("timing", REC);
      s.t.playing_slot_index = 0;
      s.d.call("beat()");
      assert.deepStrictEqual(nine(s).filter((m) => m[0] === "tgate").pop(), ["tgate", 2]);
      s.t.playing_slot_index = -1;
      s.d.call("beat()");
      assert.deepStrictEqual(nine(s).filter((m) => m[0] === "tgate").pop(), ["tgate", 1]);
      });

test("TIM_SEES_TRACK_DELAY false: the bias is the vector alone", () => {
      const s = setUp();
      s.d.call("TIM_SEES_TRACK_DELAY = false");
      s.d.message("timing", REC);
      const bias = nine(s).filter((m) => m[0] === "tbias").pop()[1];
      assert.ok(Math.abs(bias - 64 / 44100 * 120 / 60 * TICKS) < 1e-9);
      });

if (failures) {
      console.log(failures + " failed");
      process.exit(1);
      }
console.log("all passed");
