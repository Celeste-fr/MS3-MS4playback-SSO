// Tests of the MuseScore Link device's logic (MuseScoreLink.js) against a stand-in Live Set
// (fakelive.js). Run: node tools/live/test/test_device.js
// What they can't show: that Live and Max behave as the stand-in does (see LIVE.md › What to check).

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

// a set with two library tracks (Kontakt on each, the device before it on the first), a track
// with a clip of the owner's, and the part by name only
function setUp() {
      const live = new FakeLive();
      const vln = live.track("Violins 1", { inputType: "MuseScore A", inputChannel: "Ch. 1" });
      const fl = live.track("Flute", { inputType: "All Ins", inputChannel: "All Channels" });
      const own = live.track("Harp", { inputType: "MuseScore A", inputChannel: "Ch. 3" });
      live.clip(own, "my harp idea", 8, 16);
      const d1 = live.device(vln, "MxDeviceMidiEffect", "MuseScore Link");
      live.device(vln, "PluginDevice", "Kontakt 8");
      live.device(fl, "PluginDevice", "Kontakt 8");
      const d2 = live.device(fl, "MxDeviceMidiEffect", "MuseScore Link");     // after Kontakt
      const shared = {};
      const hub = loadDevice(live, shared, d1.id);
      hub.bang();
      const other = loadDevice(live, shared, d2.id);
      other.bang();
      return { live, vln, fl, own, hub, other, shared };
      }

// MuseScore's datagrams for one clip, as udpreceive hands them on (address, then the arguments)
function sendClip(dev, gen, key, port, ch, part, clip, main, lengthBeats, notes, hash) {
      const chunks = Math.ceil(notes.length / 48);
      dev.message("/ms/track", [gen, key, port, ch, part, clip, main ? 1 : 0, lengthBeats * UNITS, notes.length, chunks, hash]);
      for (let c = 0; c < chunks; ++c) {
            const args = [gen, key, c];
            for (const n of notes.slice(c * 48, (c + 1) * 48))
                  args.push(n[0], n[1], n[2], n[3], n[4] || 0);
            dev.message("/ms/notes", args);
            }
      }

test("one copy is the hub; another takes over when it goes", () => {
      const s = setUp();
      assert.strictEqual(s.hub.api.state().isHub, true);
      assert.strictEqual(s.other.api.state().isHub, false);
      // the hub alone makes a udpreceive, on 9001, through a deferlow; udpsend answers on 9002
      assert.deepStrictEqual(s.hub.patcher.made.map((o) => o.cls).sort(), ["deferlow", "udpreceive"]);
      assert.strictEqual(s.hub.patcher.made.find((o) => o.cls === "udpreceive").arg, 9001);
      assert.strictEqual(s.other.patcher.made.length, 0);
      assert.ok(s.hub.out.some((m) => m[0] === 1 && m[1] === "port" && m[2] === 9002));
      assert.strictEqual(s.hub.sent("/live/hello").length, 1);
      s.hub.call("notifydeleted()");
      s.other.runTasks();
      assert.strictEqual(s.other.api.state().isHub, true);
      assert.strictEqual(s.other.patcher.made.find((o) => o.cls === "udpreceive").arg, 9001);
      });

test("a non-hub copy ignores MuseScore's messages", () => {
      const s = setUp();
      sendClip(s.other, 1, "0:1", "MuseScore A", 1, "Violins 1", "MuseScore: Violins 1", true, 8, [[60, 0, 3840, 90]], 5);
      assert.strictEqual(s.other.api.state().work.length, 0);
      });

test("a clip on the track of the part's port and channel, notes in beats, the device checked", () => {
      const s = setUp();
      const notes = [];
      for (let i = 0; i < 100; ++i)
            notes.push([60 + (i % 12), i * 960, 900, 80, i === 3 ? 1 : 0]);
      notes.push([127, 0, 8 * UNITS, 2]);                   // a carrier: UACC 1 over the whole clip
      sendClip(s.hub, 7, "0:1", "MuseScore A", 1, "Violins 1", "MuseScore: Violins 1", true, 30, notes, 12345);
      s.hub.work();
      assert.deepStrictEqual(s.live.errors, []);
      assert.strictEqual(s.vln.clips.length, 1);
      const c = s.live.objects[s.vln.clips[0]];
      assert.strictEqual(c.name, "MuseScore: Violins 1");
      assert.strictEqual(c.start_time, 0);
      assert.strictEqual(c.end_time, 30);
      assert.strictEqual(c.muted, 0);
      assert.strictEqual(c.notes.length, 101);
      const n5 = c.notes.find((n) => n.start_time === 5 * 960 / UNITS);
      assert.strictEqual(n5.pitch, 65);
      assert.strictEqual(n5.duration, 900 / UNITS);
      assert.strictEqual(n5.velocity, 80);
      assert.strictEqual(c.notes.find((n) => n.start_time === 3 * 960 / UNITS).mute, 1);
      assert.strictEqual(c.notes.find((n) => n.pitch === 127).duration, 8);
      assert.strictEqual(s.vln.current_monitoring_state, 1);                 // clips: Auto
      assert.deepStrictEqual(s.hub.sent("/live/applied").pop(), ["0:1", 12345, "ok", "Violins 1"]);
      // the notes went in batches, each a dictionary with a notes list
      const adds = s.live.calls.filter((x) => x[2] === "add_new_notes");
      assert.ok(adds.length >= 1 && adds.every((x) => Array.isArray(x[3].notes)));
      });

test("the same clip again: notes replaced; a new length: made again; never over the owner's clips", () => {
      const s = setUp();
      sendClip(s.hub, 1, "0:1", "MuseScore A", 1, "Violins 1", "MuseScore: Violins 1", true, 30, [[60, 0, 3840, 90]], 1);
      s.hub.work();
      const first = s.vln.clips[0];
      sendClip(s.hub, 2, "0:1", "MuseScore A", 1, "Violins 1", "MuseScore: Violins 1", true, 30, [[62, 0, 3840, 90]], 2);
      s.hub.work();
      assert.strictEqual(s.vln.clips[0], first);                              // kept
      assert.deepStrictEqual(s.live.objects[first].notes.map((n) => n.pitch), [62]);
      sendClip(s.hub, 3, "0:1", "MuseScore A", 1, "Violins 1", "MuseScore: Violins 1", true, 40, [[64, 0, 3840, 90]], 3);
      s.hub.work();
      assert.strictEqual(s.vln.clips.length, 1);
      assert.notStrictEqual(s.vln.clips[0], first);
      assert.strictEqual(s.live.objects[s.vln.clips[0]].end_time, 40);
      // the harp's track has a clip of the owner's in the way: left alone, and said
      sendClip(s.hub, 4, "0:3", "MuseScore A", 3, "Harp", "MuseScore: Harp", true, 40, [[50, 0, 3840, 90]], 4);
      s.hub.work();
      assert.deepStrictEqual(s.live.errors, []);
      assert.strictEqual(s.own.clips.length, 1);
      assert.strictEqual(s.live.objects[s.own.clips[0]].name, "my harp idea");
      assert.ok(s.hub.sent("/live/applied").pop()[2].startsWith("other clips"));
      });

test("a part found by name; an extra patch not; the device after Kontakt said", () => {
      const s = setUp();
      sendClip(s.hub, 1, "0:2", "MuseScore A", 2, "Flute", "MuseScore: Flute", true, 10, [[72, 0, 3840, 90]], 9);
      s.hub.work();
      assert.strictEqual(s.fl.clips.length, 1);
      assert.ok(s.hub.sent("/live/applied").pop()[2].includes("after the instrument"));
      sendClip(s.hub, 2, "0:4", "MuseScore A", 4, "Flute", "MuseScore: Flute – Flute Legato", false, 10, [[72, 0, 3840, 90]], 10);
      s.hub.work();
      assert.ok(s.hub.sent("/live/applied").pop()[2].startsWith("no track"));
      assert.strictEqual(s.fl.clips.length, 1);
      });

test("a track without the device: the clip is written, and the missing device said", () => {
      const s = setUp();
      const vc = s.live.track("Celli", { inputType: "MuseScore B", inputChannel: "Ch. 16" });
      s.live.device(vc, "PluginDevice", "Kontakt 8");
      sendClip(s.hub, 1, "1:16", "MuseScore B", 16, "Violoncellos", "MuseScore: Violoncellos", true, 10, [[48, 0, 3840, 90]], 3);
      s.hub.work();
      assert.strictEqual(vc.clips.length, 1);
      assert.ok(s.hub.sent("/live/applied").pop()[2].startsWith("no MuseScore Link device"));
      });

test("a route gone: its clip deleted, nothing else", () => {
      const s = setUp();
      sendClip(s.hub, 1, "0:3", "MuseScore A", 3, "Harp", "MuseScore: Harp", true, 4, [[50, 0, 3840, 90]], 4);
      s.hub.work();                                           // (the owner's clip is at 8-16: no overlap)
      assert.strictEqual(s.own.clips.length, 2);
      s.hub.message("/ms/clear", [2, "0:3", "MuseScore A", 3, "Harp", "MuseScore: Harp"]);
      s.hub.work();
      assert.strictEqual(s.own.clips.length, 1);
      assert.strictEqual(s.live.objects[s.own.clips[0]].name, "my harp idea");
      });

test("mode stream: Monitor In on the tracks it wrote (clips silent); clips: Auto", () => {
      const s = setUp();
      sendClip(s.hub, 1, "0:1", "MuseScore A", 1, "Violins 1", "MuseScore: Violins 1", true, 4, [[60, 0, 3840, 90]], 1);
      s.hub.work();
      s.hub.message("/ms/mode", ["stream"]);
      s.hub.work();
      assert.strictEqual(s.vln.current_monitoring_state, 0);
      s.hub.message("/ms/mode", ["clips"]);
      s.hub.work();
      assert.strictEqual(s.vln.current_monitoring_state, 1);
      });

test("tempo and locators: set while stopped, the owner's locators kept, old ones of ours removed", () => {
      const s = setUp();
      const song = s.live.song;
      const mine = s.live.add({ kind: "cue", time: 4, name: "verse" });
      const old = s.live.add({ kind: "cue", time: 100, name: "MS 40" });
      song.cues.push(mine.id, old.id);
      song.current_song_time = 7;
      const cues = [[0, "MS 1"], [4 * UNITS, "MS 2"], [8 * UNITS, "MS 3 A"]];
      const args = [5, 0];
      for (const c of cues)
            args.push(c[0], c[1]);
      song.is_playing = 1;
      s.hub.message("/ms/song", [5, 72.5, 12 * UNITS, 3, 1, 777]);
      s.hub.message("/ms/cues", args);
      s.hub.api.workStep();
      assert.strictEqual(song.tempo, 72.5);                   // the tempo at once
      assert.strictEqual(song.cues.length, 2);                // the locators wait until Live stops
      song.is_playing = 0;
      s.hub.work();
      const byTime = {};
      for (const id of song.cues)
            byTime[s.live.objects[id].time] = s.live.objects[id].name;
      assert.deepStrictEqual(byTime, { 0: "MS 1", 4: "verse", 8: "MS 3 A" });
      assert.strictEqual(song.current_song_time, 7);          // the playhead put back
      assert.deepStrictEqual(s.hub.sent("/live/applied").pop(), ["song", 777, "ok", ""]);
      // again: nothing changes
      s.hub.message("/ms/song", [6, 72.5, 12 * UNITS, 3, 1, 777]);
      args[0] = 6;
      s.hub.message("/ms/cues", args);
      s.hub.work();
      assert.strictEqual(song.cues.length, 3);
      });

test("transport: reported while playing, and on a change while stopped; MuseScore's Play and Stop", () => {
      const s = setUp();
      s.live.song.is_playing = 1;
      s.live.song.current_song_time = 12.5;
      s.hub.api.report();
      s.hub.api.report();
      const t = s.hub.sent("/live/transport");
      assert.strictEqual(t.length, 2);
      assert.deepStrictEqual(t[0], [1, 12.5, 120]);
      s.live.song.is_playing = 0;
      s.hub.api.report();
      s.hub.api.report();
      assert.strictEqual(s.hub.sent("/live/transport").length, 3);          // the stop, once
      s.hub.message("/ms/play", [33.25]);
      assert.strictEqual(s.live.song.current_song_time, 33.25);
      assert.strictEqual(s.live.song.is_playing, 1);
      s.hub.message("/ms/stop", []);
      assert.strictEqual(s.live.song.is_playing, 0);
      });

test("a clip that arrives incomplete isn't drawn; a newer one replaces it", () => {
      const s = setUp();
      s.hub.message("/ms/track", [1, "0:1", "MuseScore A", 1, "Violins 1", "MuseScore: Violins 1", 1, 4 * UNITS, 60, 2, 1]);
      s.hub.message("/ms/notes", [1, "0:1", 0].concat([].concat(...Array(48).fill([60, 0, 100, 90, 0]))));
      s.hub.work();
      assert.strictEqual(s.vln.clips.length, 0);
      sendClip(s.hub, 2, "0:1", "MuseScore A", 1, "Violins 1", "MuseScore: Violins 1", true, 4, [[61, 0, 100, 90]], 2);
      s.hub.message("/ms/notes", [1, "0:1", 1, 60, 0, 100, 90, 0]);   // (the old one's last chunk, late)
      s.hub.work();
      assert.deepStrictEqual(s.live.objects[s.vln.clips[0]].notes.map((n) => n.pitch), [61]);
      });

test("helpers: LOM dictionaries, id lists, port names", () => {
      const s = setUp();
      const a = s.hub.api;
      assert.strictEqual(a.displayName(['{"input_routing_type": {"display_name": "MuseScore A", "identifier": 3}}']), "MuseScore A");
      assert.strictEqual(a.displayName('{"display_name":"Ch. 2"}'), "Ch. 2");
      assert.strictEqual(a.displayName("Ch. 2"), "Ch. 2");
      assert.deepStrictEqual(Array.from(a.ids(["id", 3, "id", 7])), [3, 7]);
      assert.deepStrictEqual(Array.from(a.ids("id 3 id 0")), [3]);
      assert.strictEqual(a.loosePort("Ext: MuseScore A"), a.loosePort("MuseScore A"));
      });

if (failures) {
      console.log(failures + " failed");
      process.exit(1);
      }
console.log("all passed");
