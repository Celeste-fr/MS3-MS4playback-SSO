// A stand-in for Live with the MuseScore Link device, on the real UDP ports: MuseScore (a real
// build, "Live plays the score" on) talks to it as it would to Live. The device's script runs as in
// the tests (fakelive.js); the set has one MIDI track per name given, each with the device before
// Kontakt, MIDI From "All Ins" (so found by name), or "<port>/<channel>" when given as name=port/ch.
//
//   node fake_live_server.js [--port 9001] [--play-at <s> --play-from <beat> --stop-at <s>] Violin Flute …
//
// Editing a Live clip in MuseScore: --edit-clip <track> puts a clip of the owner's ("Idea", 8 beats, a
// humanized melody with Live-only fields set) on that track and shows it in the Detail View; --edit-at <s>
// presses the device's Edit in MuseScore button then; --live-change-at <s> changes the clip in "Live" (the
// first note's velocity), as the owner would while it is edited in MuseScore.
//
// It prints every message from MuseScore, and at the end (Ctrl+C or --quit-at <s>) the clips
// written, as JSON on stdout (lines starting with "CLIPS ").

"use strict";
const dgram = require("dgram");
const { FakeLive, loadDevice } = require("./fakelive");

const argv = process.argv.slice(2);
let port = 9001, playAt = -1, playFrom = 0, stopAt = -1, quitAt = -1, editAt = -1, liveChangeAt = -1;
let editTrack = "";
const names = [];
for (let i = 0; i < argv.length; ++i) {
      if (argv[i] === "--port") port = Number(argv[++i]);
      else if (argv[i] === "--play-at") playAt = Number(argv[++i]);
      else if (argv[i] === "--play-from") playFrom = Number(argv[++i]);
      else if (argv[i] === "--stop-at") stopAt = Number(argv[++i]);
      else if (argv[i] === "--quit-at") quitAt = Number(argv[++i]);
      else if (argv[i] === "--edit-clip") editTrack = argv[++i];
      else if (argv[i] === "--edit-at") editAt = Number(argv[++i]);
      else if (argv[i] === "--live-change-at") liveChangeAt = Number(argv[++i]);
      else names.push(argv[i]);
      }

const live = new FakeLive();
const t0 = Date.now();
const stamp = () => ((Date.now() % 100000) / 1000).toFixed(3) + " ";
const deviceIds = [];
for (const n of names) {
      const [name, route] = n.split("=");
      const props = {};
      if (route) {
            const [p, ch] = route.split("/");
            props.inputType = p;
            props.inputChannel = "Ch. " + ch;
            }
      const t = live.track(name, props);
      deviceIds.push(live.device(t, "MxDeviceMidiEffect", "MuseScore Link").id);
      live.device(t, "PluginDevice", "Kontakt 8");
      }
// the owner's clip to edit in MuseScore
let editClip = null;
if (editTrack) {
      let t = live.song.tracks.map((id) => live.objects[id]).find((x) => x.name === editTrack);
      if (!t) {
            t = live.track(editTrack);
            deviceIds.push(live.device(t, "MxDeviceMidiEffect", "MuseScore Link").id);
            }
      editClip = live.clip(t, "Idea", 16, 24);
      editClip.notes = [
            live.note({ pitch: 67, start_time: 0.013, duration: 0.95, velocity: 87.3, probability: 0.75, velocity_deviation: 3.5,
                        release_velocity: 40 }),
            live.note({ pitch: 69, start_time: 1.02, duration: 0.97, velocity: 80.6 }),
            live.note({ pitch: 71, start_time: 1.991, duration: 1.03, velocity: 91.2, probability: 0.5 }),
            live.note({ pitch: 72, start_time: 3.004, duration: 0.49, velocity: 70 }),
            live.note({ pitch: 74, start_time: 3.51, duration: 0.48, velocity: 75.9, mute: 1 }),
            live.note({ pitch: 76, start_time: 4.0, duration: 3.96, velocity: 99.4 }),
            ];
      live.view.detail_clip = editClip.id;
      }
// a copy of the device on each track, as in Live; the first loaded is the hub
const shared = {};
const copies = deviceIds.map((id) => loadDevice(live, shared, id));
copies.forEach((c) => c.bang());
const dev = copies[0];

// OSC 1.0
function oscString(s) {
      const b = Buffer.from(s + "\0", "utf8");
      const pad = (4 - (b.length % 4)) % 4;
      return Buffer.concat([b, Buffer.alloc(pad)]);
      }
function encode(address, args) {
      let tags = ",";
      const data = [];
      for (const a of args) {
            if (typeof a === "string") { tags += "s"; data.push(oscString(a)); }
            else if (Number.isInteger(a)) { tags += "i"; const b = Buffer.alloc(4); b.writeInt32BE(a); data.push(b); }
            else { tags += "f"; const b = Buffer.alloc(4); b.writeFloatBE(a); data.push(b); }
            }
      return Buffer.concat([oscString(address), oscString(tags)].concat(data));
      }
function decode(buf) {
      let pos = 0;
      const str = () => {
            const end = buf.indexOf(0, pos);
            const s = buf.toString("utf8", pos, end);
            pos = (end + 4) & ~3;
            return s;
            };
      const address = str();
      const args = [];
      if (pos >= buf.length)
            return { address, args };
      const tags = str();
      for (const t of tags.slice(1)) {
            if (t === "i") { args.push(buf.readInt32BE(pos)); pos += 4; }
            else if (t === "f") { args.push(buf.readFloatBE(pos)); pos += 4; }
            else if (t === "s") args.push(str());
            }
      return { address, args };
      }

const sock = dgram.createSocket("udp4");
let sentOut = 0;
function flush() {
      for (; sentOut < dev.out.length; ++sentOut) {
            const m = dev.out[sentOut];
            if (m[0] !== 0)
                  continue;
            sock.send(encode(m[1], m.slice(2)), port + 1, "127.0.0.1");
            if (m[1] !== "/live/transport" && m[1] !== "/live/hello" && m[1] !== "/live/clip/notes")
                  console.log(stamp() + "-> " + m[1] + " " + JSON.stringify(m.slice(2)));
            }
      }
const received = {};
sock.on("message", (buf) => {
      const { address, args } = decode(buf);
      received[address] = (received[address] || 0) + 1;
      if (address !== "/ms/notes" && address !== "/ms/cues")
            console.log(stamp() + "<- " + address + " " + JSON.stringify(args).slice(0, 200));
      dev.message(address, args);
      flush();
      });
sock.bind(port, "127.0.0.1");

setInterval(() => { dev.api.workStep(); flush(); }, 20);
let beatAt = 0;
setInterval(() => {
      const s = (Date.now() - t0) / 1000;
      if (live.song.is_playing)
            live.song.current_song_time += 0.04 * live.song.tempo / 60;
      if (playAt >= 0 && s >= playAt) {
            playAt = -1;
            live.song.current_song_time = playFrom;
            live.song.is_playing = 1;
            console.log(stamp() + "== Live plays from beat " + playFrom);
            }
      if (stopAt >= 0 && s >= stopAt) {
            stopAt = -1;
            live.song.is_playing = 0;
            console.log(stamp() + "== Live stops at beat " + live.song.current_song_time.toFixed(3));
            }
      if (editAt >= 0 && s >= editAt) {
            editAt = -1;
            console.log(stamp() + "== Edit in MuseScore pressed");
            dev.message("edit", []);
            }
      if (liveChangeAt >= 0 && s >= liveChangeAt && editClip) {
            liveChangeAt = -1;
            editClip.notes[0].velocity = 64;
            console.log(stamp() + "== the clip changed in Live (first note's velocity 64)");
            }
      dev.api.report();
      if (Date.now() - beatAt > 1000) {
            beatAt = Date.now();
            copies.forEach((c) => c.runTasks());
            }
      flush();
      if (quitAt >= 0 && s >= quitAt)
            quit();
      }, 40);

function quit() {
      const clips = [];
      for (const tid of live.song.tracks) {
            const t = live.objects[tid];
            for (const cid of t.clips) {
                  const c = live.objects[cid];
                  clips.push({ track: t.name, name: c.name, start: c.start_time, end: c.end_time, muted: c.muted,
                               monitor: t.current_monitoring_state, notes: c.notes });
                  }
            }
      console.log("CLIPS " + JSON.stringify({ tempo: live.song.tempo, is_playing: live.song.is_playing,
                                               cues: live.song.cues.map((id) => [live.objects[id].time, live.objects[id].name]),
                                               received: received, errors: live.errors, clips: clips }));
      process.exit(0);
      }
process.on("SIGINT", quit);
process.on("SIGTERM", quit);
