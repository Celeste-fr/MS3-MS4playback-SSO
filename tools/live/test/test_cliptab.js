// Tests of the device's side of a clip tab playing through its Live track (MuseScoreLink.js, protocol 4) and of
// the link's loss and return, against the stand-in Live (fakelive.js). The patcher's MIDI path (route /ms/midi ->
// forward -> the track's [receive] -> midiout) is in test_patch.js. Run: node tools/live/test/test_cliptab.js
// MuseScore's side: mtest/libmscore/liveintegration (clipTab*, linkWatch).

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

// a Synth track (the hub's copy on it) with an arrangement clip shown in the Detail View, a Bass track with a copy and
// a session clip, a Pad track without the device and a clip of its own
function setUp() {
      const live = new FakeLive();
      const synth = live.track("BuzzWave Lead");
      const bass = live.track("Bass");
      const pad = live.track("Pad");
      const d1 = live.device(synth, "MxDeviceMidiEffect", "MuseScore Link");
      live.device(synth, "InstrumentGroupDevice", "BuzzWave");
      const d2 = live.device(bass, "MxDeviceMidiEffect", "MuseScore Link");
      live.device(pad, "InstrumentGroupDevice", "Pad Rack");
      const clip = live.clip(synth, "Hook", 8, 16);
      clip.notes = [live.note({ pitch: 64, start_time: 0, duration: 1 }), live.note({ pitch: 67, start_time: 1, duration: 1 })];
      const padClip = live.clip(pad, "Chords", 0, 8);
      padClip.notes = [live.note({ pitch: 60, start_time: 0, duration: 4 })];
      const slot = live.clipSlot(bass);
      const bassClip = live.add({ kind: "clip", name: "Line", notes: [live.note({ pitch: 40, start_time: 0, duration: 0.5 })],
                                  parent: slot.id, is_midi_clip: 1, signature_numerator: 4, signature_denominator: 4,
                                  loop_start: 0, loop_end: 4, start_marker: 0, end_marker: 4, looping: 1 });
      slot.clip = bassClip.id;
      live.view.detail_clip = clip.id;
      const shared = {};
      const hub = loadDevice(live, shared, d1.id);
      hub.bang();
      const other = loadDevice(live, shared, d2.id);
      other.bang();
      return { live, synth, bass, pad, clip, padClip, slot, bassClip, hub, other, shared };
      }

function editClip(s, dev, clip) {
      s.live.view.detail_clip = clip ? clip.id : 0;
      dev.message("edit", []);
      dev.work();
      }

// what Live's transport was asked (nothing, for a clip tab)
function transportCalls(live) {
      return live.calls.filter((c) => (c[0] === "set" && /current_song_time|is_playing|start_marker|loop_|clip_trigger/.test(c[2]))
                                      || (c[0] === "call" && /playing|fire|jump|scrub|stop/.test(c[2])));
      }

test("every copy names its [receive] after its track (outlet 7), and says its protocol in the registry", () => {
      const s = setUp();
      assert.deepStrictEqual(s.hub.out.find((m) => m[0] === 7), [7, "set", "msl_m" + s.synth.id]);
      assert.deepStrictEqual(s.other.out.find((m) => m[0] === 7), [7, "set", "msl_m" + s.bass.id]);
      const r = JSON.parse(s.shared.musescore_link.devices);
      assert.ok(Object.values(r).every((e) => e.protocol === 5));
      assert.strictEqual(s.hub.sent("/live/hello")[0][1], 5);
      });

test("the hub's udpreceive goes into the patcher's [route /ms/midi] (msl_in) when it has one, else through a deferlow", () => {
      const s = setUp();
      const into = { name: "msl_in" };
      s.hub.patcher.getnamed = (n) => (n === "msl_in" ? into : null);
      s.hub.call("openPort()");
      const udp = s.hub.patcher.made.filter((o) => o.cls === "udpreceive").pop();
      assert.ok(s.hub.patcher.lines.some((l) => l[0] === udp && l[2] === into));
      });

test("Edit in MuseScore: the clip's track and whether a copy plays MuseScore's notes on it (arrangement, session, none)", () => {
      const s = setUp();
      editClip(s, s.hub, s.clip);
      assert.deepStrictEqual(s.hub.sent("/live/clip/track").pop(), ["c" + s.clip.id, s.synth.id, 1, "BuzzWave Lead"]);
      s.live.view.highlighted_clip_slot = s.slot.id;
      editClip(s, s.hub, null);
      assert.deepStrictEqual(s.hub.sent("/live/clip/track").pop(), ["c" + s.bassClip.id, s.bass.id, 1, "Bass"]);
      editClip(s, s.hub, s.padClip);
      assert.deepStrictEqual(s.hub.sent("/live/clip/track").pop(), ["c" + s.padClip.id, s.pad.id, 0, "Pad"]);
      // (begin and the notes came first)
      const order = s.hub.out.filter((m) => m[0] === 0).map((m) => m[1]);
      assert.ok(order.lastIndexOf("/live/clip/begin") < order.lastIndexOf("/live/clip/track"));
      });

test("a copy added to or removed from the clip's track: said again at the next check, once", () => {
      const s = setUp();
      s.live.view.highlighted_clip_slot = s.slot.id;
      editClip(s, s.hub, null);
      const key = "c" + s.bassClip.id;
      s.other.call("notifydeleted()");
      s.hub.api.checkEdits();
      s.hub.api.checkEdits();
      const after = s.hub.sent("/live/clip/track").filter((m) => m[0] === key);
      assert.deepStrictEqual(after.slice(1), [[key, s.bass.id, 0, "Bass"]]);
      const d3 = s.live.device(s.bass, "MxDeviceMidiEffect", "MuseScore Link");
      loadDevice(s.live, s.shared, d3.id).bang();
      s.hub.api.checkEdits();
      assert.deepStrictEqual(s.hub.sent("/live/clip/track").pop(), [key, s.bass.id, 1, "Bass"]);
      });

test("a copy of an older device (no protocol 4) on the track doesn't count", () => {
      const s = setUp();
      const r = JSON.parse(s.shared.musescore_link.devices);
      for (const k in r)
            if (r[k].track === s.bass.id)
                  delete r[k].protocol;
      s.shared.musescore_link.devices = JSON.stringify(r);
      s.live.view.highlighted_clip_slot = s.slot.id;
      editClip(s, s.hub, null);
      assert.strictEqual(s.hub.sent("/live/clip/track").pop()[2], 0);
      });

test("/ms/midi reaching the script (an older patcher): passed on by name to the track's copies", () => {
      const s = setUp();
      s.hub.message("/ms/midi", [s.synth.id, 0x90, 60, 100]);
      assert.deepStrictEqual(s.live.messages.pop(), ["msl_m" + s.synth.id, 0x90, 60, 100]);
      });

test("the hub goes: /live/bye; another copy takes over; MuseScore hands it the clips (adopt): same notes in sync, changed ones a conflict", () => {
      const s = setUp();
      editClip(s, s.hub, s.clip);
      const key = "c" + s.clip.id;
      const hash = s.hub.sent("/live/clip/begin").pop()[14];
      const session = s.hub.sent("/live/hello")[0][0];
      s.hub.call("notifydeleted()");
      assert.deepStrictEqual(s.hub.sent("/live/bye"), [[session]]);
      s.other.runTasks();
      assert.strictEqual(s.other.api.state().isHub, true);
      assert.notStrictEqual(s.other.sent("/live/hello").pop()[0], session);
      // MuseScore: /ms/clip/adopt with the hash it knows (the hub's copy on the Synth track went with it: no copy there now)
      s.other.message("/ms/clip/adopt", [key, hash]);
      assert.deepStrictEqual(s.other.sent("/live/clip/conflict"), []);
      assert.deepStrictEqual(s.other.sent("/live/clip/track").pop(), [key, s.synth.id, 0, "BuzzWave Lead"]);
      // and it takes MuseScore's writes from here on
      s.other.message("/ms/clip/write", [key, 1, 1, 1]);
      s.other.message("/ms/clip/ops", [key, 1, 0, 0, s.clip.notes[0].note_id, 1, 65, 0, 0, 0, 0]);
      s.other.work();
      assert.strictEqual(s.other.sent("/live/clip/written").pop()[2], "ok");
      assert.strictEqual(s.clip.notes[0].pitch, 65);
      // a clip changed in Live meanwhile: a conflict at once
      const s2 = setUp();
      editClip(s2, s2.hub, s2.clip);
      s2.hub.call("notifydeleted()");
      s2.other.runTasks();
      s2.clip.notes[1].velocity = 30;
      s2.other.message("/ms/clip/adopt", ["c" + s2.clip.id, s2.hub.sent("/live/clip/begin").pop()[14]]);
      assert.strictEqual(s2.other.sent("/live/clip/conflict").length, 1);
      // a clip deleted meanwhile: gone
      s2.other.message("/ms/clip/adopt", ["c9999", 0]);
      assert.deepStrictEqual(s2.other.sent("/live/clip/gone").pop(), ["c9999"]);
      });

test("nothing of it touches Live's transport, song time, launch or the clip's markers", () => {
      const s = setUp();
      editClip(s, s.hub, s.clip);
      s.hub.api.checkEdits();
      s.hub.message("/ms/midi", [s.synth.id, 0x90, 60, 100]);
      s.hub.message("/ms/clip/adopt", ["c" + s.clip.id, 0]);
      assert.deepStrictEqual(transportCalls(s.live), []);
      });

console.log(failures ? failures + " failed" : "all passed");
process.exitCode = failures ? 1 : 0;
