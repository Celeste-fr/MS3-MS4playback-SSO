// The device's periodic work stays cheap (2026-10-12: with the plain set's 28 copies Live 12.4.6's main thread climbed to
// ~100 % within minutes and the window stopped responding; 5 copies: ~40 % to 100 % in 10 min; none: ~1 %). Each copy's
// heartbeat (once a second) and the hub's transport report (25 a second) made new LiveAPI objects every time; Max for
// Live's JS runs on Live's main thread. Here: a plain set's technique tracks (8 clip slots each, a written clip, a timing
// record), every copy's heartbeat for 120 s and the hub's report for 10 s, against the stand-in Live (fakelive.js), which
// counts the LiveAPI objects made and the property reads. Run: node tools/live/test/test_load.js.

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

const REC = ["msl-timing", 1, 86, 30, 86, 2, 4 * UNITS, 5 * UNITS, 62, 0, 86, 60 * UNITS, 61 * UNITS, 64, 0, 0];

function plainSet(n) {
      const live = new FakeLive();
      const shared = {};
      const copies = [];
      for (let i = 0; i < n; ++i) {
            const t = live.track("Part " + i + " – Long", { playing_slot_index: -1, fired_slot_index: -1, back_to_arranger: 0 });
            for (let k = 0; k < 8; ++k)
                  live.clipSlot(t);
            live.clip(t, "Long", 0, 64);
            const dev = live.device(t, "MxDeviceMidiEffect", "MuseScore Link");
            live.thisDevice = dev.id;
            const d = loadDevice(live, shared, dev.id);
            d.bang();
            d.message("prefix", [String(100 + i) + "mslp"]);
            d.message("dspsr", [44100]);
            d.message("dspvs", [64]);
            d.message("timing", REC);
            copies.push(d);
            }
      live.settle();
      return { live, copies };
      }

test("28 copies, 120 heartbeats each: no LiveAPI object made after loading; under 4 property reads a copy a second", () => {
      const s = plainSet(28);
      // (the first seconds: whatever loading makes)
      for (let sec = 0; sec < 6; ++sec)
            for (const d of s.copies)
                  d.call("beat()");
      s.live.settle();
      const made = s.live.made, reads = s.live.calls.length;
      s.live.song.is_playing = 1;
      for (let sec = 0; sec < 120; ++sec) {
            s.live.song.current_song_time = sec * 2;
            for (const d of s.copies)
                  d.call("beat()");
            s.live.settle();
            }
      assert.strictEqual(s.live.made - made, 0, "LiveAPI objects made in 120 s: " + (s.live.made - made));
      const perCopySecond = (s.live.calls.length - reads) / 28 / 120;
      assert.ok(perCopySecond < 4, "property reads a copy a second: " + perCopySecond);
      // (and the timing record went into each copy's buffer once, not every second)
      assert.strictEqual(s.copies[0].out.filter((m) => m[0] === 9 && m[1] === "tlast").length, 1);
      const pokes = s.live.buffers["100mslt"];
      assert.ok(pokes && pokes.size === 61 * 480 + 1 + 3);
      });

test("the hub's transport report, 25 a second: no LiveAPI object made", () => {
      const s = plainSet(2);
      const hub = s.copies.find((d) => d.api.state().isHub);
      hub.call("report()");
      const made = s.live.made;
      s.live.song.is_playing = 1;
      for (let i = 0; i < 250; ++i) {
            s.live.song.current_song_time = i * 0.08;
            hub.call("report()");
            }
      assert.strictEqual(s.live.made - made, 0);
      assert.strictEqual(hub.sent("/live/transport").length, 251);
      });

test("a copy with nothing to do (no record, no velocity curves, no lanes): a heartbeat reads nothing from Live", () => {
      const live = new FakeLive();
      const t = live.track("Flute", {});
      for (let k = 0; k < 8; ++k)
            live.clipSlot(t);
      const dev = live.device(t, "MxDeviceMidiEffect", "MuseScore Link");
      live.thisDevice = dev.id;
      const d = loadDevice(live, {}, dev.id);
      d.bang();
      d.message("prefix", ["200mslp"]);
      live.settle();
      d.call("beat()");
      live.settle();
      const reads = live.calls.length, made = live.made;
      for (let sec = 0; sec < 4; ++sec)
            d.call("beat()");
      live.settle();
      // (the hub's own work aside: its transport report, the edits it follows; none here)
      assert.strictEqual(live.made - made, 0);
      assert.strictEqual(live.calls.length - reads, 0, JSON.stringify(live.calls.slice(reads)));
      });

if (failures) {
      console.log(failures + " failed");
      process.exit(1);
      }
console.log("all passed");
