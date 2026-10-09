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
      assert.ok(Object.values(r).every((e) => e.protocol === 8));
      assert.strictEqual(s.hub.sent("/live/hello")[0][1], 8);
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

// a clip tab's track audible while MuseScore plays (/ms/cliptab/audible; the owner, 2026-10-03, option A)
function mutesAndSolos(live) {
      return live.calls.filter((c) => c[0] === "set" && (c[2] === "mute" || c[2] === "solo"));
      }

test("audible: a muted track is un-muted while MuseScore plays and muted again at Stop", () => {
      const s = setUp();
      s.synth.mute = 1;
      s.hub.message("/ms/cliptab/audible", [1, s.synth.id]);
      assert.strictEqual(s.synth.mute, 0);
      s.hub.message("/ms/cliptab/audible", [1, s.synth.id]);       // (the heartbeat: nothing set again)
      assert.strictEqual(mutesAndSolos(s.live).length, 1);
      s.hub.message("/ms/cliptab/audible", [0, s.synth.id]);
      assert.strictEqual(s.synth.mute, 1);
      assert.strictEqual(s.hub.api.audibleState(), null);
      // a track not muted: nothing set at all; a stop for another track changes nothing
      s.live.calls.length = 0;
      s.hub.message("/ms/cliptab/audible", [1, s.bass.id]);
      s.hub.message("/ms/cliptab/audible", [0, s.synth.id]);
      assert.ok(s.hub.api.audibleState());
      s.hub.message("/ms/cliptab/audible", [0, s.bass.id]);
      assert.deepStrictEqual(mutesAndSolos(s.live), []);
      assert.deepStrictEqual(transportCalls(s.live), []);
      });

test("audible: the group tracks it is in are un-muted too, and muted again after", () => {
      const s = setUp();
      const inner = s.live.track("Leads");
      const outer = s.live.track("Synths");
      s.synth.group = inner.id;
      inner.group = outer.id;
      inner.mute = 1;
      outer.mute = 1;
      s.hub.message("/ms/cliptab/audible", [1, s.synth.id]);
      assert.deepStrictEqual([s.synth.mute, inner.mute, outer.mute], [0, 0, 0]);
      s.hub.message("/ms/cliptab/audible", [0, s.synth.id]);
      assert.deepStrictEqual([s.synth.mute, inner.mute, outer.mute], [0, 1, 1]);
      });

test("audible: another track soloed: this one soloed too while playing, the others left soloed; un-soloed after", () => {
      const s = setUp();
      s.pad.solo = 1;
      s.hub.message("/ms/cliptab/audible", [1, s.synth.id]);
      assert.deepStrictEqual([s.synth.solo, s.pad.solo, s.bass.solo], [1, 1, 0]);
      s.hub.message("/ms/cliptab/audible", [0, s.synth.id]);
      assert.deepStrictEqual([s.synth.solo, s.pad.solo, s.bass.solo], [0, 1, 0]);
      // a soloed return track counts too; a soloed group the track is in: nothing to solo
      const ret = s.live.returnTrack("A-Reverb");
      s.pad.solo = 0;
      ret.solo = 1;
      s.hub.message("/ms/cliptab/audible", [1, s.synth.id]);
      assert.strictEqual(s.synth.solo, 1);
      s.hub.message("/ms/cliptab/audible", [0, s.synth.id]);
      assert.deepStrictEqual([s.synth.solo, ret.solo], [0, 1]);
      const grp = s.live.track("Group");
      s.synth.group = grp.id;
      grp.solo = 1;
      ret.solo = 0;
      s.hub.message("/ms/cliptab/audible", [1, s.synth.id]);
      assert.strictEqual(s.synth.solo, 0);
      s.hub.message("/ms/cliptab/audible", [0, s.synth.id]);
      // no solo anywhere: nothing soloed
      grp.solo = 0;
      s.live.calls.length = 0;
      s.hub.message("/ms/cliptab/audible", [1, s.synth.id]);
      assert.deepStrictEqual(mutesAndSolos(s.live), []);
      });

test("audible: a Live that un-solos the others (exclusive solo) gets them soloed again at Stop", () => {
      const s = setUp();
      s.pad.solo = 1;
      const tracks = [s.synth, s.bass, s.pad];
      for (const t of tracks) {                 // (the stand-in made exclusive: soloing a track un-solos every other)
            let v = t.solo;
            Object.defineProperty(t, "solo", { get() { return v; }, set(x) {
                  v = x;
                  if (x && s.live.exclusive)
                        for (const o of tracks)
                              if (o !== t)
                                    o.solo = 0;
                  } });
            }
      s.pad.solo = 1;
      s.live.exclusive = true;
      s.hub.message("/ms/cliptab/audible", [1, s.synth.id]);
      assert.deepStrictEqual([s.synth.solo, s.pad.solo], [1, 0]);
      s.live.exclusive = false;
      s.hub.message("/ms/cliptab/audible", [0, s.synth.id]);
      assert.deepStrictEqual([s.synth.solo, s.pad.solo], [0, 1]);
      });

test("audible: a change the user makes while MuseScore plays is kept", () => {
      const s = setUp();
      s.synth.mute = 1;
      s.pad.solo = 1;
      s.hub.message("/ms/cliptab/audible", [1, s.synth.id]);
      assert.deepStrictEqual([s.synth.mute, s.synth.solo], [0, 1]);
      s.synth.solo = 0;                                             // (the user un-solos it, and mutes it again, in Live)
      s.synth.mute = 1;
      s.hub.message("/ms/cliptab/audible", [1, s.synth.id]);       // (the heartbeat sets nothing again)
      assert.deepStrictEqual([s.synth.mute, s.synth.solo], [1, 0]);
      s.hub.message("/ms/cliptab/audible", [0, s.synth.id]);
      assert.deepStrictEqual([s.synth.mute, s.synth.solo, s.pad.solo], [1, 0, 1]);
      // the user un-mutes it: stays un-muted
      s.hub.message("/ms/cliptab/audible", [1, s.synth.id]);
      s.synth.solo = 1;
      s.hub.message("/ms/cliptab/audible", [0, s.synth.id]);
      assert.strictEqual(s.synth.mute, 1);
      assert.strictEqual(s.synth.solo, 0);
      });

test("audible: MuseScore silent for 7 s (quit, link lost), the track's copy or the hub deleted: put back", () => {
      const s = setUp();
      s.synth.mute = 1;
      s.bass.mute = 1;
      let t = 1e12;
      for (const d of [s.hub, s.other])
            d.call("now = function() { return " + t + "; }");
      s.hub.message("/ms/cliptab/audible", [1, s.synth.id]);
      assert.strictEqual(s.synth.mute, 0);
      t += 6000;
      s.hub.call("now = function() { return " + t + "; }");
      s.hub.api.checkAudible();
      assert.strictEqual(s.synth.mute, 0);                          // (6 s: still playing; AUDIBLE_STALE_MS 7000)
      s.hub.message("/ms/cliptab/audible", [1, s.synth.id]);
      t += 7500;
      s.hub.call("now = function() { return " + t + "; }");
      s.hub.call("beat()");                                         // (the hub's second)
      assert.strictEqual(s.synth.mute, 1);
      assert.strictEqual(s.hub.api.audibleState(), null);
      // the bass track's copy deleted while playing through it
      s.other.call("now = function() { return " + t + "; }");
      s.other.call("beat()");
      s.hub.message("/ms/cliptab/audible", [1, s.bass.id]);
      assert.strictEqual(s.bass.mute, 0);
      s.other.call("notifydeleted()");
      s.hub.call("beat()");
      assert.strictEqual(s.bass.mute, 1);
      // the hub deleted while playing through its own track: put back before it goes
      s.hub.message("/ms/cliptab/audible", [1, s.synth.id]);
      assert.strictEqual(s.synth.mute, 0);
      s.hub.call("notifydeleted()");
      assert.strictEqual(s.synth.mute, 1);
      // no copy on the track: nothing changed
      s.pad.mute = 1;
      const s2 = setUp();
      s2.pad.mute = 1;
      s2.hub.message("/ms/cliptab/audible", [1, s2.pad.id]);
      assert.strictEqual(s2.pad.mute, 1);
      });

console.log(failures ? failures + " failed" : "all passed");
process.exitCode = failures ? 1 : 0;
