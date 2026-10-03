// Tests of the device's side of clip tabs' velocity curves (MuseScoreLink.js › velocity curves, protocol 6) and of what
// it keeps in the set through the MuseScore Envelopes script (› kept in the set): the shaping math (scale, absolute,
// before the first point, ramps and curves as automation.cpp has them), the ring the patcher reads for a session clip
// (its loop, a launch to come) and for arrangement clips, other clips left alone, the curve kept per clip and given
// back after the set is opened again (MuseScore not running), a track's lanes kept by the script instead of the [pattr]
// stores. Against the stand-in Live (fakelive.js) and a stand-in of the script (Track.set_data as core.py uses it).
// Run: node tools/live/test/test_velocity.js. The script's own side: test_envelopes.py › Keep; MuseScore's:
// tst_liveintegration clipVelocity*.

"use strict";
const assert = require("assert");
const { FakeLive, loadDevice } = require("./fakelive");

const TICKS = 480;
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

// the MuseScore Envelopes script as core.py keeps values: per track index, per `what`, the atoms (Track.set_data)
class FakeScript {
      constructor() { this.data = {}; this.incoming = {}; this.puts = 0; }
      // every message a device sent on outlet 8, in order (each device's own list: consumed)
      take(dev, live) {
            const msgs = dev.out.filter((m) => m[0] === 8 && !m.taken);
            for (const m of msgs) {
                  m.taken = true;
                  const a = Array.isArray(m[1]) ? m[1] : m.slice(1);
                  this.receive(a, dev, live);
                  }
            }
      receive(a, dev) {
            const address = a[0];
            if (address === "/ms/keep/put") {
                  const [t, what, stamp, chunk, chunks] = a.slice(1, 6);
                  const k = t + ":" + what;
                  let inc = this.incoming[k];
                  if (!inc || inc.stamp !== stamp)
                        inc = this.incoming[k] = { stamp, chunks, parts: {} };
                  inc.parts[chunk] = a.slice(6);
                  if (Object.keys(inc.parts).length < chunks)
                        return;
                  delete this.incoming[k];
                  let v = [];
                  for (let c = 0; c < chunks; ++c)
                        v = v.concat(inc.parts[c]);
                  this.data[k] = v.length ? v : null;
                  ++this.puts;
                  }
            else if (address === "/ms/keep/ping")
                  dev.message("/live/keep/pong", [2]);
            else if (address === "/ms/keep/ask") {
                  const what = a[2];
                  let n = 0;
                  for (const k of Object.keys(this.data)) {
                        const [t, w] = k.split(":");
                        if (w !== what || !this.data[k])
                              continue;
                        ++n;
                        // (two chunks, as a long value would come)
                        const v = this.data[k], h = Math.ceil(v.length / 2);
                        dev.message("/live/keep/data", [what, Number(t), n, 0, 2].concat(v.slice(0, h)));
                        dev.message("/live/keep/data", [what, Number(t), n, 1, 2].concat(v.slice(h)));
                        }
                  dev.message("/live/keep/end", [what, n]);
                  }
            }
      }

// "Synth" (MuseScore Link, a synth) with a looping session clip "Loop" (4 beats) in slot 0, a second one "Other" in slot
// 1, and an arrangement clip "Arr" at beats 8-12 (its loop 0-4)
function setUp(shared, script) {
      const live = new FakeLive();
      live.song.clip_trigger_quantization = 4;        // (1 bar, Live's default)
      const synth = live.track("Synth", { playing_slot_index: -1, fired_slot_index: -1, back_to_arranger: 0 });
      const dev = live.device(synth, "MxDeviceMidiEffect", "MuseScore Link");
      live.device(synth, "InstrumentVector", "Wavetable");
      const slots = [live.clipSlot(synth), live.clipSlot(synth)];
      const mk = (slot, name) => {
            const c = live.add({ kind: "clip", name: name, notes: [0, 1, 2, 3].map((b) => live.note({ pitch: 60 + b, start_time: b, duration: 0.5 })),
                                 parent: slot.id, is_midi_clip: 1, signature_numerator: 4, signature_denominator: 4,
                                 loop_start: 0, loop_end: 4, start_marker: 0, end_marker: 4, looping: 1, legato: 0,
                                 launch_quantization: 0, start_time: 0, playing_position: 0 });
            slot.clip = c.id;
            return c;
            };
      const loop = mk(slots[0], "Loop");
      const other = mk(slots[1], "Other");
      const arr = live.clip(synth, "Arr", 8, 12);
      Object.assign(arr, { loop_start: 0, loop_end: 4, looping: 1, legato: 0 });
      const hub = loadDevice(live, shared || {}, dev.id);
      hub.bang();
      hub.message("prefix", ["001mslp"]);
      hub.message("dspsr", [44100]);
      hub.message("dspvs", [64]);
      if (script) {
            hub.call("beat()");                       // (the heartbeat pings the script)
            for (let i = 0; i < 3; ++i)
                  script.take(hub, live);
            }
      return { live, synth, dev, slots, loop, other, arr, hub };
      }

// a clip edited in MuseScore (its key), as the button does it
function editClip(s, clip) {
      s.live.view.detail_clip = clip.id;
      s.hub.message("edit", []);
      s.hub.work();
      return "c" + clip.id;
      }

// a clip's record (MuseScoreLink.js velClip): mode, output, points (tick value curve c1x c1y c2x c2y), originals
function record(mode, output, points, originals) {
      const a = [mode, output, points.length];
      for (const p of points)
            a.push(p[0], p[1], p[2] || 0, 1 / 3, 1 / 3, 2 / 3, 2 / 3);
      a.push((originals || []).length);
      for (const o of originals || [])
            a.push(...o);
      return a;
      }

function setVel(s, key, serial, atoms) {
      s.hub.message("/ms/vel/set", [key, serial, 0, 1].concat(atoms));
      s.live.settle();
      }

// the code the patcher reads for song beat b (its index rounded: the bias is one vector, as snapshot~ reads it)
function codeAt(s, b) {
      const buf = s.live.buffers["001mslv"];
      return buf.data[Math.round(b * TICKS) % s.hub.api.VEL_RING];
      }

test("the shaping: scale (0-200 %, 100 % at the lane's middle), absolute (1-127), none before the first point", () => {
      const api = setUp().hub.api;
      assert.strictEqual(api.velCode(0, -1), 0);                       // before the first point: as it is
      assert.strictEqual(api.velShape(100, 0), 100);
      assert.strictEqual(api.velShape(100, api.velCode(0, 0.5)), 100); // the middle: unchanged
      assert.strictEqual(api.velShape(100, api.velCode(0, 0.25)), 50);
      assert.strictEqual(api.velShape(90, api.velCode(0, 1)), 127);    // 180 -> 127
      assert.strictEqual(api.velShape(100, api.velCode(0, 0)), 1);     // 0 -> 1 (0 would be a note-off)
      assert.strictEqual(api.velShape(37, api.velCode(1, 0.5)), 64);   // absolute: the note's own velocity left out
      assert.strictEqual(api.velShape(37, api.velCode(1, 0)), 1);
      assert.strictEqual(api.velShape(37, api.velCode(1, 1)), 127);
      assert.strictEqual(api.velShape(0, api.velCode(0, 0.25)), 0);    // a note-off stays one
      });

test("the lane's value as automation.cpp has it: steps, a straight ramp, a curve, before the first point", () => {
      const api = setUp().hub.api;
      const pts = [{ tick: 480, value: 0.2, curve: 1, c1x: 1 / 3, c1y: 1 / 3, c2x: 2 / 3, c2y: 2 / 3 },
                   { tick: 960, value: 0.6, curve: 1, c1x: 0.25, c1y: 0.75, c2x: 0.5, c2y: 1 },   // a curve rising early
                   { tick: 1440, value: 1, curve: 0, c1x: 1 / 3, c1y: 1 / 3, c2x: 2 / 3, c2y: 2 / 3 },
                   { tick: 1920, value: 0.1, curve: 0, c1x: 1 / 3, c1y: 1 / 3, c2x: 2 / 3, c2y: 2 / 3 }];
      assert.strictEqual(api.velValueAt(pts, 0), -1);
      assert.strictEqual(api.velValueAt(pts, 480), 0.2);
      assert.ok(Math.abs(api.velValueAt(pts, 720) - 0.4) < 1e-12);    // the straight ramp's middle
      assert.ok(api.velValueAt(pts, 1200) > 0.8);                       // the curve's middle: above the line's 0.8
      assert.strictEqual(api.velValueAt(pts, 1500), 1);                 // a step holds
      assert.strictEqual(api.velValueAt(pts, 1920), 0.1);
      assert.strictEqual(api.velValueAt(pts, 99999), 0.1);
      });

test("a session clip's ring: from its launch, its loop, the curve at each note's clip time", () => {
      const s = setUp();
      const key = editClip(s, s.loop);
      // scale: 50 % (u 0.25) from clip beat 0, 80 % (u 0.4) from beat 2
      setVel(s, key, 1, record(0, 0, [[0, 0.25], [960, 0.4]]));
      assert.deepStrictEqual(s.hub.sent("/live/vel/set").slice(-1), [[key, 1, "ok", 0]]);   // (no script: not kept)
      // playing since song beat 4 (started then: start_time 4), now at beat 5.5
      Object.assign(s.synth, { playing_slot_index: 0 });
      Object.assign(s.loop, { start_time: 4, playing_position: 1.5 });
      s.live.song.is_playing = 1;
      s.live.song.current_song_time = 5.5;
      s.hub.api.velFill();
      const api = s.hub.api;
      // beat 6 = clip beat 2 (80 %), beat 7 = 3 (80 %), beat 8 = the loop's start (50 %), 9 (50 %), 10 (80 %)
      assert.strictEqual(api.velShape(100, codeAt(s, 6)), 80);
      assert.strictEqual(api.velShape(100, codeAt(s, 7)), 80);
      assert.strictEqual(api.velShape(100, codeAt(s, 8)), 50);
      assert.strictEqual(api.velShape(100, codeAt(s, 9)), 50);
      assert.strictEqual(api.velShape(100, codeAt(s, 7.99)), 80);      // (a note just before the loop's end)
      assert.strictEqual(codeAt(s, 10), 0);                             // (past the window: the next fill's)     // (just before the loop: still its end)
      });

test("another clip on the track is left alone; a launch to come is shaped from its quantized start", () => {
      const s = setUp();
      const key = editClip(s, s.loop);
      setVel(s, key, 1, record(1, 0, [[0, 0.5]]));                    // absolute 64 all through
      // "Other" plays (unshaped) and "Loop" is fired at beat 5.3: it starts at the next bar, beat 8
      Object.assign(s.synth, { playing_slot_index: 1, fired_slot_index: 0 });
      Object.assign(s.other, { start_time: 4, playing_position: 1.3 });
      s.live.song.is_playing = 1;
      s.live.song.current_song_time = 5.3;
      s.hub.api.velFill();
      for (const b of [6, 7, 7.9])
            assert.strictEqual(codeAt(s, b), 0, "Other at beat " + b);
      for (const b of [8, 9, 9.25])
            assert.strictEqual(s.hub.api.velShape(100, codeAt(s, b)), 64, "Loop at beat " + b);
      // its own launch quantization over the song's (ClipLaunchQuantization: 1/4 = 8): beat 6
      s.loop.launch_quantization = 8;
      s.hub.api.velFill();
      assert.strictEqual(codeAt(s, 5.9), 0);
      assert.strictEqual(s.hub.api.velShape(100, codeAt(s, 6)), 64);
      assert.strictEqual(s.hub.api.velGrid(13, 4), 0.125);             // (q_thirtytwoth)
      });

test("arrangement clips: shaped over their own span only (clip time from their start, their loop)", () => {
      const s = setUp();
      const key = editClip(s, s.arr);
      setVel(s, key, 1, record(0, 0, [[0, 0.25], [960, 1]]));
      s.live.song.is_playing = 1;
      s.live.song.current_song_time = 7.2;
      s.hub.api.velFill();
      assert.strictEqual(codeAt(s, 7.5), 0);                            // before the clip
      assert.strictEqual(s.hub.api.velShape(100, codeAt(s, 8)), 50);
      assert.strictEqual(s.hub.api.velShape(100, codeAt(s, 9)), 50);
      assert.strictEqual(s.hub.api.velShape(100, codeAt(s, 8)), 50);
      assert.strictEqual(s.hub.api.velShape(60, codeAt(s, 8.25)), 30);
      // the session overrides the arrangement (back_to_arranger 1): nothing
      s.synth.back_to_arranger = 1;
      s.hub.api.velFill();
      assert.strictEqual(codeAt(s, 8), 0);
      });

test("output \"write\": the notes carry the curve, the device leaves them alone (but keeps the record)", () => {
      const s = setUp();
      const key = editClip(s, s.loop);
      setVel(s, key, 1, record(0, 1, [[0, 0.25]], [[s.loop.notes[0].note_id, 60, 0, 100, 50]]));
      Object.assign(s.synth, { playing_slot_index: 0 });
      Object.assign(s.loop, { start_time: 0, playing_position: 0.5 });
      s.live.song.is_playing = 1;
      s.live.song.current_song_time = 0.5;
      s.hub.api.velFill();
      assert.strictEqual(codeAt(s, 1), 0);
      s.hub.message("/ms/vel/ask", [key]);
      const curve = s.hub.sent("/live/vel/curve").slice(-1)[0];
      assert.deepStrictEqual(curve.slice(0, 5), [key, 1, 0, 0, 1]);
      const back = curve.slice(5);
      assert.strictEqual(back[1], 1);                                   // write
      assert.deepStrictEqual(back.slice(-5), [s.loop.notes[0].note_id, 60, 0, 100, 50]);
      });

test("kept in the set by the script: no [pattr] store set, given back after the set is opened again without MuseScore", () => {
      const script = new FakeScript();
      const shared = {};
      const s = setUp(shared, script);
      assert.ok(s.hub.call("scriptAlive()"));
      const key = editClip(s, s.loop);
      setVel(s, key, 1, record(0, 0, [[0, 0.25], [960, 0.4]]));
      script.take(s.hub, s.live);
      assert.deepStrictEqual(s.hub.sent("/live/vel/set").slice(-1), [[key, 1, "ok", 1]]);
      assert.ok(script.data["0:vel"]);
      assert.strictEqual(script.data["0:vel"][0], "msl-vel");
      // the arrangement clip's curve too: one value for the track, both clips in it
      const akey = editClip(s, s.arr);
      setVel(s, akey, 1, record(1, 0, [[0, 1]]));
      script.take(s.hub, s.live);
      assert.strictEqual(script.data["0:vel"][2], 2);
      // the set saved and opened again: a new Live, a new device, the Global empty, MuseScore not there
      const s2 = setUp({}, null);
      for (let i = 0; i < 3; ++i) {
            s2.hub.call("beat()");
            script.take(s2.hub, s2.live);
            }
      s2.live.settle();
      Object.assign(s2.synth, { playing_slot_index: 0 });
      Object.assign(s2.loop, { start_time: 0, playing_position: 0.2 });
      s2.live.song.is_playing = 1;
      s2.live.song.current_song_time = 0.2;
      s2.hub.api.velFill();
      assert.strictEqual(s2.hub.api.velShape(100, codeAt(s2, 1)), 50);
      assert.strictEqual(s2.hub.api.velShape(100, codeAt(s2, 2)), 80);
      Object.assign(s2.synth, { playing_slot_index: -1 });
      s2.live.song.current_song_time = 7.5;
      s2.hub.api.velFill();
      assert.strictEqual(s2.hub.api.velShape(30, codeAt(s2, 8.5)), 127);
      // and MuseScore opening the clip again reads it back
      const key2 = editClip(s2, s2.loop);
      s2.hub.message("/ms/vel/ask", [key2]);
      const c = s2.hub.sent("/live/vel/curve").slice(-1)[0];
      assert.deepStrictEqual(c.slice(0, 2), [key2, 1]);
      assert.deepStrictEqual(c.slice(5, 8), [0, 0, 2]);
      });

test("an ask before the script's values came waits for them", () => {
      const script = new FakeScript();
      script.data["0:vel"] = ["msl-vel", 1, 1, 0, 0].concat(record(0, 0, [[0, 0.75]]));
      const s = setUp({}, null);
      const key = editClip(s, s.loop);
      s.hub.call("beat()");                             // the ping
      const ping = s.hub.out.filter((m) => m[0] === 8);
      assert.ok(ping.length);
      // the pong comes and the hub asks for the set's values; MuseScore asks before they are in: no answer yet
      script.take(s.hub, s.live);
      s.hub.message("/ms/vel/ask", [key]);
      assert.strictEqual(s.hub.sent("/live/vel/curve").length, 0);
      script.take(s.hub, s.live);                       // (the script answers: the values, then the end)
      s.live.settle();
      const c = s.hub.sent("/live/vel/curve").slice(-1)[0];
      assert.deepStrictEqual(c.slice(0, 2), [key, 1]);
      });

test("a track's lanes: kept by the script (no [pattr] store, so no Live undo step), restored after a reopen", () => {
      const script = new FakeScript();
      const s = setUp({}, script);
      // MuseScore's lanes for a route on this track: one lane on the Wavetable's parameter… (as test_params.js does)
      const wt = s.live.objects[s.synth.devices[1]];
      s.live.param(wt, "Filter Freq", 0.3, 0, 1);
      s.hub.message("/ms/track", [1, "1:1", "Synth", 1, "Synth", "MuseScore: Synth", 1, 4 * 3840, 0, 0, 1]);
      s.hub.work();
      s.hub.message("/ms/params", [1, "1:1", 1, 77]);
      s.hub.message("/ms/pvals", [1, "1:1", 0, "live:1/0", -1, 0, 1, 0, 0.25, 3840, 0.75]);
      s.live.settle();
      script.take(s.hub, s.live);
      assert.strictEqual(s.hub.out.filter((m) => m[0] === 6).length, 0, "a [pattr] store was set");
      assert.ok(script.data["0:lanes"], "the lanes reached the script");
      assert.strictEqual(script.data["0:lanes"][0], "msl-lanes");
      // the set opened again (MuseScore not running): the lanes come back from the script
      const s2 = setUp({}, null);
      for (let i = 0; i < 3; ++i) {
            s2.hub.call("beat()");
            script.take(s2.hub, s2.live);
            }
      s2.live.settle();
      const st = s2.hub.api.state();
      assert.ok(st.keptByScript);
      assert.ok(st.saved && st.saved.routes["1:1"], "the route's lanes restored");
      });

test("the patcher's constants agree with the script's (make_device.py VEL_RING)", () => {
      const fs = require("fs");
      const path = require("path");
      const s = setUp();
      const py = fs.readFileSync(path.join(__dirname, "..", "make_device.py"), "utf8");
      assert.ok(/VEL_RING = 2 \* math\.ceil\(2 \* 999 \/ 60 \* 480\)/.test(py));
      assert.strictEqual(s.hub.api.VEL_RING, 2 * Math.ceil(2 * 999 / 60 * 480));
      const pat = JSON.parse(fs.readFileSync(path.join(__dirname, "..", "MuseScoreLink.maxpat"), "utf8")).patcher;
      assert.ok(pat.boxes.some((b) => b.box.text === "loadmess " + s.hub.api.VEL_RING));
      });

if (failures) {
      console.log(failures + " failed");
      process.exit(1);
      }
console.log("all passed");
