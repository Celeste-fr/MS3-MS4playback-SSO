// A stand-in for Live's Object Model and Max's JS globals, to run MuseScoreLink.js under Node.
// It models only what the device uses, as Cycling '74's LOM reference describes it
// (https://docs.cycling74.com/apiref/lom/): values come back from get() as arrays, a list of
// children as ["id", n, "id", m], a dictionary property as a JSON string; arrangement clips can't
// overlap on a track (making one over another is recorded as an error here, so a test fails).
// Notes (Live 11+): each has a note_id and every field get_all_notes_extended returns; Song.View has
// detail_clip (the clip in the Detail View) and highlighted_clip_slot (session slots: clipSlot()).
// Parameter lanes: a device's parameters (DeviceParameter: name, value, min, max), Max's Buffer (a stub keeping
// its size and values, by name, shared by every copy as buffer~ names are global), messnamed (to every copy's
// [receive msl_params] -> [prepend msl_params] -> the script), Task.schedule (run by settle()).

"use strict";
const fs = require("fs");
const path = require("path");
const vm = require("vm");

class FakeLive {
      constructor() {
            this.nextId = 1;
            this.objects = {};
            this.errors = [];
            this.calls = [];
            this.nextNoteId = 1;
            this.loaded = [];             // the copies of the device (loadDevice)
            this.buffers = {};            // buffer~ name -> { size, data }
            this.song = this.add({ kind: "song", tempo: 120, is_playing: 0, current_song_time: 0, cues: [], tracks: [],
                                   signature_numerator: 4, signature_denominator: 4 });
            this.view = this.add({ kind: "view", detail_clip: 0, highlighted_clip_slot: 0 });
            }
      // a note as Live keeps it (the defaults add_new_notes gives)
      note(n) {
            return Object.assign({ note_id: this.nextNoteId++, velocity: 100, mute: 0, probability: 1, velocity_deviation: 0,
                                   release_velocity: 64 }, n);
            }
      // a session clip in a slot of the track
      clipSlot(track) {
            const slot = this.add({ kind: "clipslot", track: track.id, clip: 0 });
            (track.slots = track.slots || []).push(slot.id);
            return slot;
            }
      add(o) {
            o.id = this.nextId++;
            this.objects[o.id] = o;
            return o;
            }
      track(name, props) {
            const t = this.add(Object.assign({ kind: "track", name: name, has_midi_input: 1, clips: [], devices: [],
                                              inputType: "All Ins", inputChannel: "All Channels",
                                              current_monitoring_state: 1 }, props || {}));
            this.song.tracks.push(t.id);
            return t;
            }
      device(track, className, name) {
            const d = this.add({ kind: "device", class_name: className, name: name, track: track.id });
            track.devices.push(d.id);
            return d;
            }
      // a DeviceParameter of a device (Live lists only the plug-in's Configured ones)
      param(device, name, value, min, max) {
            const p = this.add({ kind: "param", name: name, value: value, min: min === undefined ? 0 : min,
                                 max: max === undefined ? 1 : max, device: device.id, automation_state: 0 });
            (device.parameters = device.parameters || []).push(p.id);
            return p;
            }
      // the track's mixer (MixerDevice: volume, panning), made when first asked for
      mixer(track) {
            if (!track.mixer) {
                  const m = this.add({ kind: "mixer", track: track.id });
                  m.volume = this.add({ kind: "param", name: "Track Volume", value: 0.85, min: 0, max: 1, automation_state: 0 }).id;
                  m.panning = this.add({ kind: "param", name: "Track Panning", value: 0, min: -1, max: 1, automation_state: 0 }).id;
                  track.mixer = m.id;
                  }
            return this.objects[track.mixer];
            }
      // every copy's scheduled work and the hubs' work steps, until nothing is left
      settle() {
            for (let n = 0; n < 50; ++n) {
                  let busy = false;
                  for (const d of this.loaded) {
                        if (d.runScheduled(true))
                              busy = true;
                        if (d.api.state().isHub && d.api.state().work.length) {
                              d.api.workStep();
                              busy = true;
                              }
                        }
                  for (const d of this.loaded)
                        if (d.api.state().isHub)
                              d.api.pollParams();
                  if (!busy)
                        return;
                  }
            }
      clip(track, name, start, end) {
            const c = this.add({ kind: "clip", name: name, start_time: start, end_time: end, muted: 0, notes: [], track: track.id,
                                 parent: track.id, is_midi_clip: 1, signature_numerator: 4, signature_denominator: 4,
                                 loop_start: 0, loop_end: end - start, start_marker: 0, end_marker: end - start, looping: 0 });
            track.clips.push(c.id);
            return c;
            }
      resolve(p) {
            p = String(p).trim();
            let m;
            if (p === "live_set")
                  return this.song;
            if (p === "live_set view")
                  return this.view;
            if (p === "live_set view detail_clip")
                  return this.objects[this.view.detail_clip];
            if (p === "live_set view highlighted_clip_slot")
                  return this.objects[this.view.highlighted_clip_slot];
            if ((m = /^live_set tracks (\d+)$/.exec(p)))
                  return this.objects[this.song.tracks[Number(m[1])]];
            if ((m = /^id (\d+)$/.exec(p)))
                  return this.objects[Number(m[1])];
            if (p === "this_device")
                  return this.objects[this.thisDevice];
            if (p === "this_device canonical_parent")
                  return this.objects[this.objects[this.thisDevice].track];
            return null;
            }
      idList(l) {
            const out = [];
            for (const id of l)
                  out.push("id", id);
            return out;
            }
      }

// the LiveAPI class for one device (thisDevice: its device id)
function liveApiFor(live, deviceId) {
      return function LiveAPI(a, b) {
            const p = typeof a === "function" ? b : a;
            live.thisDevice = deviceId;
            const o = live.resolve(p);
            this.id = o ? o.id : 0;
            this.path = p;
            this.type = o ? { song: "Song", track: "Track", clip: "Clip", clipslot: "ClipSlot", device: "Device", cue: "CuePoint",
                              view: "Song.View", param: "DeviceParameter", mixer: "MixerDevice" }[o.kind] : "";
            this.info = o ? "id " + o.id + "\ntype " + this.type + "\nproperty name str\ndone" : "No object";
            const self = this;
            this.getcount = function(what) {
                  if (o.kind === "song" && what === "tracks")
                        return o.tracks.length;
                  throw new Error("getcount " + what);
                  };
            this.get = function(prop) {
                  live.calls.push(["get", o.kind, prop]);
                  if (o.kind === "song") {
                        if (prop === "cue_points")
                              return live.idList(o.cues);
                        if (prop === "tracks")
                              return live.idList(o.tracks);
                        return [o[prop]];
                        }
                  if (o.kind === "track") {
                        if (prop === "arrangement_clips")
                              return live.idList(o.clips);
                        if (prop === "devices")
                              return live.idList(o.devices);
                        if (prop === "clip_slots")
                              return live.idList(o.slots || []);
                        if (prop === "mixer_device")
                              return ["id", live.mixer(o).id];
                        if (prop === "input_routing_type")
                              return [JSON.stringify({ input_routing_type: { display_name: o.inputType, identifier: 7 } })];
                        if (prop === "input_routing_channel")
                              return [JSON.stringify({ input_routing_channel: { display_name: o.inputChannel, identifier: 3 } })];
                        return [o[prop]];
                        }
                  if (o.kind === "device" && prop === "parameters")
                        return live.idList(o.parameters || []);
                  if (o.kind === "mixer" && (prop === "volume" || prop === "panning"))
                        return ["id", o[prop]];
                  if (o.kind === "clip" && prop === "canonical_parent")
                        return ["id", o.parent];
                  if (o.kind === "clipslot" && prop === "canonical_parent")
                        return ["id", o.track];
                  if (o.kind === "clipslot" && prop === "clip")
                        return ["id", o.clip];
                  if (o.kind === "clipslot" && prop === "has_clip")
                        return [o.clip ? 1 : 0];
                  return [o[prop]];
                  };
            this.set = function(prop, v) {
                  live.calls.push(["set", o.kind, prop, v]);
                  if (o.kind === "song" && prop === "current_song_time")
                        o.current_song_time = Number(v);
                  else
                        o[prop] = v;
                  };
            this.call = function(fn) {
                  const args = Array.prototype.slice.call(arguments, 1);
                  live.calls.push(["call", o.kind, fn].concat(args));
                  if (o.kind === "track" && fn === "create_midi_clip") {
                        const start = Number(args[0]), len = Number(args[1]);
                        for (const cid of o.clips) {
                              const c = live.objects[cid];
                              if (c.start_time < start + len && c.end_time > start)
                                    live.errors.push("a clip made over " + c.name);
                              }
                        live.clip(o, "", start, start + len);
                        return;
                        }
                  if (o.kind === "track" && fn === "delete_clip") {
                        const id = Number(args[0] === "id" ? args[1] : String(args[0]).replace("id ", ""));
                        o.clips = o.clips.filter((x) => x !== id);
                        delete live.objects[id];
                        return;
                        }
                  if (o.kind === "clip" && fn === "remove_notes_extended") {
                        const [p0, span, t0, tspan] = args.map(Number);
                        o.notes = o.notes.filter((n) => !(n.pitch >= p0 && n.pitch < p0 + span && n.start_time >= t0 && n.start_time < t0 + tspan));
                        return;
                        }
                  if (o.kind === "clip" && fn === "add_new_notes") {
                        const d = args[0];
                        if (!d || !Array.isArray(d.notes))
                              live.errors.push("add_new_notes without a notes list");
                        const added = [];
                        for (const n of d.notes) {
                              const x = live.note(Object.assign({}, n));
                              o.notes.push(x);
                              added.push(x.note_id);
                              }
                        return live.addReturnsIds === false ? undefined : added;
                        }
                  if (o.kind === "clip" && fn === "get_all_notes_extended")
                        return JSON.stringify({ notes: o.notes });           // (Max hands a dictionary to JS as JSON)
                  if (o.kind === "clip" && fn === "remove_notes_by_id") {
                        const ids = args.map(Number);
                        o.notes = o.notes.filter((n) => ids.indexOf(n.note_id) < 0);
                        return;
                        }
                  if (o.kind === "clip" && fn === "apply_note_modifications") {
                        const d = args[0];
                        if (!d || !Array.isArray(d.notes))
                              live.errors.push("apply_note_modifications without a notes list");
                        for (const m of d.notes) {
                              const i = o.notes.findIndex((n) => n.note_id === m.note_id);
                              if (i < 0)
                                    live.errors.push("apply_note_modifications: no note " + m.note_id);
                              else
                                    o.notes[i] = Object.assign({}, o.notes[i], m);
                              }
                        return;
                        }
                  if (o.kind === "song" && fn === "set_or_delete_cue") {
                        const t = o.current_song_time;
                        const at = o.cues.find((cid) => Math.abs(live.objects[cid].time - t) < 1e-9);
                        if (at)
                              o.cues = o.cues.filter((x) => x !== at);
                        else
                              o.cues.push(live.add({ kind: "cue", time: t, name: "" }).id);
                        return;
                        }
                  if (o.kind === "song" && fn === "continue_playing") {
                        o.is_playing = 1;
                        return;
                        }
                  if (o.kind === "song" && fn === "stop_playing") {
                        o.is_playing = 0;
                        return;
                        }
                  throw new Error("call " + fn + " on " + o.kind);
                  };
            };
      }

// one copy of the device: the script in a context of its own, as each [js] / [v8] box has
function loadDevice(live, shared, deviceId) {
      const src = fs.readFileSync(path.join(__dirname, "..", "MuseScoreLink.js"), "utf8");
      const out = [];
      const tasks = [];
      const scheduled = [];
      const patcher = { made: [], lines: [], removed: [],
            newdefault(x, y, cls, arg) { const o = { cls: cls, arg: arg }; this.made.push(o); return o; },
            connect(a, ai, b, bi) { this.lines.push([a, ai, b, bi]); },
            remove(o) { this.removed.push(o); } };
      const ctx = {
            post() {},
            outlet() { out.push(Array.prototype.slice.call(arguments)); },
            arrayfromargs(a) { return Array.prototype.slice.call(a); },
            Global: function(name) {
                  if (!shared[name])
                        shared[name] = {};
                  return shared[name];
                  },
            LiveAPI: liveApiFor(live, deviceId),
            Task: function(fn, that) {
                  this.fn = fn;
                  this.interval = 0;
                  this.repeat = function() { tasks.push(this); };
                  this.cancel = function() { this.cancelled = true; };
                  this.schedule = function(ms) {
                        this.delay = Number(ms) || 0;
                        if (scheduled.indexOf(this) < 0)
                              scheduled.push(this);
                        };
                  },
            Buffer: function(name) {
                  const b = live.buffers[name] = live.buffers[name] || { size: 0, data: [] };
                  this.send = function(msg, n) {
                        if (msg !== "sizeinsamps")
                              throw new Error("buffer~ " + msg);
                        b.size = Number(n);
                        b.data = new Array(b.size).fill(0);
                        };
                  this.poke = function(ch, frame, values) {
                        if (ch !== 1)
                              throw new Error("poke channel " + ch);
                        const v = Array.isArray(values) ? values : [values];
                        if (frame < 0 || frame + v.length > b.size)
                              throw new Error("poke past the end of " + name);
                        for (let i = 0; i < v.length; ++i)
                              b.data[frame + i] = v[i];
                        };
                  this.framecount = function() { return b.size; };
                  },
            messnamed(name) {
                  const args = Array.prototype.slice.call(arguments, 1);
                  live.messages = (live.messages || []).concat([[name].concat(args)]);
                  if (name === "msl_params")
                        for (const d of live.loaded)
                              d.message("msl_params", args);
                  },
            module: { exports: {} },
            messagename: "",
            patcher: patcher,
            box: { name: "js box" },
            Math: Math, Date: Date, JSON: JSON,
            };
      ctx.module = { exports: {} };
      vm.createContext(ctx);
      vm.runInContext(src, ctx, { filename: "MuseScoreLink.js" });
      const api = ctx.module.exports;
      const dev = {
            ctx: ctx, out: out, tasks: tasks, patcher: patcher, api: api,
            bang() { vm.runInContext("bang()", ctx); },
            message(name, args) {
                  ctx.messagename = name;
                  ctx.__args = args;
                  vm.runInContext("anything.apply(this, __args)", ctx);
                  },
            call(fn) { return vm.runInContext(fn, ctx); },
            runTasks() { for (const t of tasks) if (!t.cancelled) t.fn(); },
            // the scheduled Tasks (a delayed one only with all: settle() lets the time pass)
            runScheduled(all) {
                  const l = [];
                  for (let i = scheduled.length - 1; i >= 0; --i)
                        if (all || !scheduled[i].delay)
                              l.unshift(scheduled.splice(i, 1)[0]);
                  for (const t of l)
                        if (!t.cancelled)
                              t.fn();
                  return l.length > 0;
                  },
            // what went to slot k's live.remote~ ("id n")
            slotIds() { return out.filter((m) => m[0] === 3).map((m) => [m[1], m[3]]); },
            work() { let n = 0; while (api.state().work.length && n++ < 1000) api.workStep(); },
            sent(address) { return out.filter((m) => m[0] === 0 && m[1] === address).map((m) => m.slice(2)); },
            };
      live.loaded.push(dev);
      return dev;
      }

module.exports = { FakeLive, loadDevice };
