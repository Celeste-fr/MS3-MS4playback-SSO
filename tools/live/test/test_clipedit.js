// Tests of the device's side of editing a Live clip in MuseScore (MuseScoreLink.js: Edit in MuseScore,
// writes by note id, conflicts) against the stand-in Live (fakelive.js). Run: node tools/live/test/test_clipedit.js
// MuseScore's side: mtest/libmscore/liveintegration (clipEdit*). What only Live can show: LIVE.md.

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

// a set: a Violin track with an arrangement clip of the owner's (humanized), shown in the Detail View; a
// Drums track with a Drum Rack and a session clip; the device on both (the first is the hub)
function setUp() {
      const live = new FakeLive();
      live.song.tempo = 96;
      const vln = live.track("Violin");
      const drums = live.track("Beats");
      const d1 = live.device(vln, "MxDeviceMidiEffect", "MuseScore Link");
      live.device(vln, "PluginDevice", "Kontakt 8");
      const d2 = live.device(drums, "MxDeviceMidiEffect", "MuseScore Link");
      live.device(drums, "DrumGroupDevice", "Drum Rack");
      const clip = live.clip(vln, "Idea", 16, 28);        // 12 beats long, from bar 5
      clip.notes = [
            live.note({ pitch: 67, start_time: 0.013, duration: 0.95, velocity: 87.3, probability: 0.75, velocity_deviation: 3.5,
                        release_velocity: 40 }),
            live.note({ pitch: 69, start_time: 1.02, duration: 0.97, velocity: 80.6 }),
            live.note({ pitch: 71, start_time: 1.991, duration: 1.03, velocity: 91.2, mute: 1 }),
            ];
      live.view.detail_clip = clip.id;
      const slot = live.clipSlot(drums);
      const beat = live.add({ kind: "clip", name: "Beat", notes: [live.note({ pitch: 36, start_time: 0, duration: 0.25 })],
                              parent: slot.id, is_midi_clip: 1, signature_numerator: 7, signature_denominator: 8,
                              loop_start: 0, loop_end: 3.5, start_marker: 0, end_marker: 3.5, looping: 1 });
      slot.clip = beat.id;
      const shared = {};
      const hub = loadDevice(live, shared, d1.id);
      hub.bang();
      const other = loadDevice(live, shared, d2.id);
      other.bang();
      return { live, vln, drums, clip, slot, beat, hub, other };
      }

// what the hub sent MuseScore for a clip: begin's fields and the notes (9 values each)
function received(dev) {
      const begin = dev.sent("/live/clip/begin").pop();
      const notes = [];
      for (const m of dev.sent("/live/clip/notes").filter((x) => x[1] === begin[1]))     // (the latest reading)
            for (let i = 3; i + 8 < m.length; i += 9)
                  notes.push(m.slice(i, i + 9));
      return { begin, notes };
      }

// MuseScore's write: ops as [op, id, mask, pitch, start ticks, duration ticks, velocity, mute]
function write(dev, key, n, ops) {
      const chunks = Math.ceil(ops.length / 32);
      dev.message("/ms/clip/write", [key, n, ops.length, chunks]);
      for (let c = 0; c < chunks; ++c)
            dev.message("/ms/clip/ops", [key, n, c].concat([].concat(...ops.slice(c * 32, (c + 1) * 32))));
      dev.work();
      }

test("Edit in MuseScore: the Detail View's clip, every field of every note, the track, tempo and time", () => {
      const s = setUp();
      s.hub.message("edit", []);
      s.hub.work();
      const { begin, notes } = received(s.hub);
      const key = "c" + s.clip.id;
      // key gen track clip drums bpm num den end loopStart loopEnd looping notes chunks hash
      assert.deepStrictEqual(begin.slice(0, 1), [key]);
      assert.deepStrictEqual(begin.slice(2, 13), ["Violin", "Idea", 0, 96, 4, 4, 12, 0, 12, 0, 3]);
      assert.strictEqual(begin[13], 1);
      assert.strictEqual(begin[14], s.hub.api.hashNotes(s.clip.notes));
      assert.deepStrictEqual(notes[0], [s.clip.notes[0].note_id, 67, 0.013, 0.95, 87.3, 0, 0.75, 3.5, 40]);
      assert.deepStrictEqual(notes[2].slice(0, 6), [s.clip.notes[2].note_id, 71, 1.991, 1.03, 91.2, 1]);
      assert.ok(s.hub.api.state().edits[key]);
      // reading changed nothing in Live
      assert.ok(!s.live.calls.some((c) => c[0] === "set" || (c[0] === "call" && c[2] !== "get_all_notes_extended")));
      });

test("the button on another copy: the hub sends the clip", () => {
      const s = setUp();
      s.other.message("edit", []);
      assert.strictEqual(s.other.sent("/live/clip/begin").length, 0);
      s.hub.api.workStep();
      s.hub.work();
      assert.strictEqual(received(s.hub).begin[2], "Violin");
      });

test("a session clip (the highlighted slot) on a track with a Drum Rack: a drum clip in 7/8", () => {
      const s = setUp();
      s.live.view.detail_clip = 0;
      s.live.view.highlighted_clip_slot = s.slot.id;
      s.hub.message("edit", []);
      s.hub.work();
      const { begin } = received(s.hub);
      assert.deepStrictEqual(begin.slice(2, 12), ["Beats", "Beat", 1, 96, 7, 8, 3.5, 0, 3.5, 1]);
      });

test("no clip selected: nothing sent, the status says what to do", () => {
      const s = setUp();
      s.live.view.detail_clip = 0;
      s.hub.message("edit", []);
      s.hub.work();
      assert.strictEqual(s.hub.sent("/live/clip/begin").length, 0);
      assert.ok(s.hub.out.some((m) => m[0] === 2 && /select a MIDI clip/.test(m[2])));
      });

test("a write by note id: Live's own data kept, only the edited field changed; removal; an addition with its id", () => {
      const s = setUp();
      s.hub.message("edit", []);
      s.hub.work();
      const key = "c" + s.clip.id;
      const [a, b, c] = s.clip.notes.map((n) => Object.assign({}, n));
      s.live.calls.length = 0;
      write(s.hub, key, 1, [[0, c.note_id, 1, 70, 960, 480, 100, 0],         // pitch only (start and length ignored)
                            [1, b.note_id, 0, 0, 0, 0, 0, 0],                  // removed
                            [2, 0, 31, 60, 4800, 240, 100, 0]]);               // added at beat 10, an eighth
      const notes = s.clip.notes;
      assert.strictEqual(notes.length, 3);
      assert.deepStrictEqual(notes.find((n) => n.note_id === a.note_id), a);            // untouched
      const cc = notes.find((n) => n.note_id === c.note_id);
      assert.deepStrictEqual(cc, Object.assign({}, c, { pitch: 70 }));                  // micro-timing, mute kept
      const added = notes.find((n) => n.pitch === 60);
      assert.strictEqual(added.start_time, 10);
      assert.strictEqual(added.duration, 0.5);
      // by id, in Live 11's calls; the modification carries Live's own note
      const calls = s.live.calls.filter((x) => x[0] === "call").map((x) => x[2]);
      assert.deepStrictEqual(calls.filter((x) => x !== "get_all_notes_extended"),
                             ["remove_notes_by_id", "apply_note_modifications", "add_new_notes"]);
      assert.ok(!calls.includes("remove_notes_extended"));
      const reply = s.hub.sent("/live/clip/written").pop();
      assert.deepStrictEqual(reply, [key, 1, "ok", s.hub.api.hashNotes(notes), added.note_id]);
      assert.deepStrictEqual(s.live.errors, []);
      });

test("the same write sent again: applied once, answered again", () => {
      const s = setUp();
      s.hub.message("edit", []);
      s.hub.work();
      const key = "c" + s.clip.id;
      write(s.hub, key, 1, [[2, 0, 31, 60, 0, 480, 90, 0]]);
      write(s.hub, key, 1, [[2, 0, 31, 60, 0, 480, 90, 0]]);
      assert.strictEqual(s.clip.notes.filter((n) => n.pitch === 60).length, 1);
      const replies = s.hub.sent("/live/clip/written");
      assert.strictEqual(replies.length, 2);
      assert.deepStrictEqual(replies[0], replies[1]);
      });

test("add_new_notes returning no ids: the new notes found by what was added", () => {
      const s = setUp();
      s.live.addReturnsIds = false;
      s.hub.message("edit", []);
      s.hub.work();
      const key = "c" + s.clip.id;
      write(s.hub, key, 1, [[2, 0, 31, 62, 480, 480, 90, 0], [2, 0, 31, 60, 480, 480, 90, 0]]);
      const reply = s.hub.sent("/live/clip/written").pop();
      const id62 = s.clip.notes.find((n) => n.pitch === 62).note_id;
      const id60 = s.clip.notes.find((n) => n.pitch === 60).note_id;
      assert.deepStrictEqual(reply.slice(4), [id62, id60]);
      });

test("a change in Live while edited: a conflict, no more writes until reloaded", () => {
      const s = setUp();
      s.hub.message("edit", []);
      s.hub.work();
      const key = "c" + s.clip.id;
      s.clip.notes[0].velocity = 64;                          // the owner edits the clip in Live
      s.hub.api.checkEdits();
      assert.strictEqual(s.hub.sent("/live/clip/conflict").length, 1);
      assert.strictEqual(s.hub.sent("/live/clip/conflict")[0][0], key);
      const before = JSON.stringify(s.clip.notes);
      write(s.hub, key, 1, [[0, s.clip.notes[1].note_id, 1, 50, 0, 0, 0, 0]]);
      assert.strictEqual(JSON.stringify(s.clip.notes), before);            // not overwritten
      assert.strictEqual(s.hub.sent("/live/clip/written").pop()[2], "conflict");
      // MuseScore reloads: the clip again, as it is now; writes go through again
      s.hub.message("/ms/clip/reload", [key]);
      s.hub.work();
      assert.strictEqual(s.hub.sent("/live/clip/begin").length, 2);
      assert.strictEqual(received(s.hub).notes.find((n) => n[1] === 67)[4], 64);
      write(s.hub, key, 2, [[0, s.clip.notes[1].note_id, 1, 50, 0, 0, 0, 0]]);
      assert.strictEqual(s.hub.sent("/live/clip/written").pop()[2], "ok");
      assert.strictEqual(s.clip.notes[1].pitch, 50);
      // a change detected at the moment of a write, before the check: refused too
      s.clip.notes[2].pitch = 40;
      write(s.hub, key, 3, [[1, s.clip.notes[0].note_id, 0, 0, 0, 0, 0, 0]]);
      assert.strictEqual(s.hub.sent("/live/clip/written").pop()[2], "conflict");
      assert.strictEqual(s.clip.notes.length, 3);
      });

test("MuseScore's own writes are no conflict; the clip deleted: gone; closed: forgotten", () => {
      const s = setUp();
      s.hub.message("edit", []);
      s.hub.work();
      const key = "c" + s.clip.id;
      write(s.hub, key, 1, [[0, s.clip.notes[0].note_id, 8, 0, 0, 0, 120, 0]]);
      s.hub.api.checkEdits();
      assert.strictEqual(s.hub.sent("/live/clip/conflict").length, 0);
      s.hub.message("/ms/clip/close", [key]);
      assert.ok(!s.hub.api.state().edits[key]);
      s.hub.message("edit", []);
      s.hub.work();
      s.vln.clips = [];
      delete s.live.objects[s.clip.id];
      s.hub.api.checkEdits();
      assert.deepStrictEqual(s.hub.sent("/live/clip/gone").pop(), [key]);
      write(s.hub, key, 2, [[1, 1, 0, 0, 0, 0, 0, 0]]);
      assert.strictEqual(s.hub.sent("/live/clip/written").pop()[2], "gone");
      });

test("clip edits leave the MuseScore clips' work alone (Live plays the score)", () => {
      const s = setUp();
      s.hub.message("edit", []);
      s.hub.message("/ms/clip/edit", []);
      assert.ok(s.hub.api.state().work.every((w) => w.kind === "edit"));
      s.hub.work();
      assert.strictEqual(s.vln.clips.length, 1);
      assert.strictEqual(s.live.objects[s.vln.clips[0]].name, "Idea");
      });

console.log(failures ? failures + " failed" : "all passed");
process.exitCode = failures ? 1 : 0;
