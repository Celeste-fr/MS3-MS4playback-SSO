// A long plug-in parameter ramp with adaptive steps (Automation::Lane::events at PARAM_RESOLUTION 1e-4: a value at
// each tick where it moves by a step) through the device (numbers-measured, 2026-10-03; batch A's item 3): how many
// /ms/pvals datagrams, how long the device's JavaScript takes for them (Node's V8 here; Max runs the same script in
// its V8), whether the table plays the ramp (each ms within a step), and whether the lanes still fit the set's stores
// (packLane). Against the stand-in Live (fakelive.js). Run: node tools/live/test/measure_param_ramp.js
"use strict";
const { FakeLive, loadDevice } = require("./fakelive");
const UNITS = 3840, PAIRS = 100, DIVISION = 480;

function ramp(seconds, bpm, from, to, step) {
      // a tick where the value moves by a step (as Lane::events: lround(v / step) changes), beats
      const ticks = Math.round(seconds * bpm / 60 * DIVISION), ev = [[0, from]];
      let last = from;
      for (let t = 1; t <= ticks; ++t) {
            const v = from + (to - from) * t / ticks;
            if (Math.round(v / step) !== Math.round(last / step)) {
                  ev.push([t / DIVISION, v]);
                  last = v;
                  }
            }
      return ev;
      }

function run(seconds, label) {
      const live = new FakeLive();
      const vln = live.track("Violins 1", { inputType: "MuseScore A", inputChannel: "Ch. 1" });
      const d1 = live.device(vln, "MxDeviceMidiEffect", "MuseScore Link");
      const k1 = live.device(vln, "PluginDevice", "Kontakt 8");
      live.param(k1, "Mic 1 level", 0.25, 0, 1);
      const hub = loadDevice(live, {}, d1.id);
      hub.bang();
      hub.message("prefix", ["001mslp"]);
      const beats = seconds * 2 + 8;
      hub.message("/ms/song", [1, 120, Math.round(beats * UNITS), 0, 0, 1]);
      hub.message("/ms/track", [1, "0:1", "MuseScore A", 1, "Violins 1", "MuseScore: Violins 1", 1, Math.round(beats * UNITS), 1, 1, 7]);
      hub.message("/ms/notes", [1, "0:1", 0, 60, 0, UNITS, 80, 0]);
      live.settle();
      const ev = ramp(seconds, 120, 0, 1, 1e-4);
      const chunks = Math.ceil(ev.length / PAIRS);
      const t0 = process.hrtime.bigint();
      hub.message("/ms/params", [1, "0:1", 1, 42]);
      let worst = 0;
      for (let c = 0; c < chunks; ++c) {
            const a = [1, "0:1", 0, "Mic 1 level", -1, c, chunks];
            for (const [b, v] of ev.slice(c * PAIRS, (c + 1) * PAIRS))
                  a.push(Math.round(b * UNITS), v);
            const s = process.hrtime.bigint();
            hub.message("/ms/pvals", a);
            worst = Math.max(worst, Number(process.hrtime.bigint() - s) / 1e6);
            }
      const s2 = process.hrtime.bigint();
      live.settle();
      const apply = Number(process.hrtime.bigint() - s2) / 1e6, total = Number(process.hrtime.bigint() - t0) / 1e6;
      const buf = live.buffers["001mslp0"];
      let maxErr = 0;                   // the table (one value a ms) against the ramp itself
      for (let ms = 0; ms < seconds * 1000; ++ms)
            maxErr = Math.max(maxErr, Math.abs(buf.data[ms] - ms / (seconds * 1000)));
      const status = (hub.sent("/live/papplied")[0] || [])[2];
      hub.runScheduled(true);                 // (the stores: STORE_DELAY_MS after the edits)
      const kept = hub.call("keptStatus");
      const atoms = hub.call("encodeSaved(saved).length");
      const store = hub.call("STORES * (STORE_ATOMS - 5)");
      console.log(`${label}: ${ev.length} events, ${chunks} datagrams; per datagram at most ${worst.toFixed(2)} ms, ` +
                  `applying ${apply.toFixed(1)} ms, all ${total.toFixed(1)} ms; table vs ramp max ${maxErr.toFixed(5)}; ` +
                  `papplied ${status}; set stores ${atoms} of ${store} atoms ${kept ? "(" + kept + ")" : "(kept)"}`);
      }

for (const s of [1, 10, 60, 300])
      run(s, `ramp 0 -> 1 over ${s} s`);
