// The MuseScore Link patcher's MIDI path (MuseScoreLink.maxpat, made by make_device.py), run on a small
// model of the Max objects it uses, as Cycling '74's reference describes them (midiparse, route, sel,
// -, prepend, iter, midiformat, midiout). It checks the wiring: a carrier note becomes its controller,
// its note-off is dropped, every other message passes unchanged; on a track without a MuseScore clip (the carriers' gate) every note passes. It can't show that Max behaves as
// modelled (LIVE.md › What to check). Also: the .amxd container's layout.
// Run: node tools/live/test/test_patch.js

"use strict";
const assert = require("assert");
const fs = require("fs");
const path = require("path");

const dir = path.join(__dirname, "..");
const patcher = JSON.parse(fs.readFileSync(path.join(dir, "MuseScoreLink.maxpat"), "utf8")).patcher;
const CARRIER_CCS = [32, 1, 11, 64, 2, 4, 21, 5, 65, 66, 67, 68];

const boxes = {};
for (const b of patcher.boxes)
      boxes[b.box.id] = b.box;
const wires = {};
for (const l of patcher.lines) {
      const [src, so] = l.patchline.source;
      const [dst, di] = l.patchline.destination;
      assert.ok(boxes[src] && boxes[dst], "a line to a missing box");
      (wires[src + ":" + so] = wires[src + ":" + so] || []).push([dst, di]);
      }

const midiOut = [];
const state = {};
const forwardTarget = {};
const receiveNames = {};
const toScript = [];

function emit(id, outlet, value) {
      for (const [dst, inlet] of wires[id + ":" + outlet] || [])
            receive(dst, inlet, value);
      }

function receive(id, inlet, v) {
      const b = boxes[id];
      const words = (b.text || "").split(" ");
      const cls = b.maxclass === "newobj" ? words[0] : b.maxclass;
      const args = words.slice(1).map((w) => (isNaN(Number(w)) ? w : Number(w)));
      const list = Array.isArray(v) ? v : [v];
      const s = (state[id] = state[id] || {});
      switch (cls) {
            case "midiparse": {
                  s.bytes = s.bytes || [];
                  if (v >= 0x80)
                        s.bytes = [v];
                  else
                        s.bytes.push(v);
                  const st = s.bytes[0] & 0xf0, ch = (s.bytes[0] & 0x0f) + 1;
                  const need = st === 0xc0 || st === 0xd0 ? 2 : 3;
                  if (s.bytes.length < need)
                        return;
                  const [, a, c] = s.bytes;
                  s.bytes = [s.bytes[0]];          // running status
                  emit(id, 6, ch);                 // (right to left: the channel first)
                  if (st === 0x90 || st === 0x80)
                        emit(id, 0, [a, st === 0x80 ? 0 : c]);
                  else if (st === 0xa0)
                        emit(id, 1, [a, c]);
                  else if (st === 0xb0)
                        emit(id, 2, [a, c]);
                  else if (st === 0xc0)
                        emit(id, 3, a);
                  else if (st === 0xd0)
                        emit(id, 4, a);
                  else if (st === 0xe0)
                        emit(id, 5, c);
                  return;
                  }
            case "route": {
                  const k = args.indexOf(list[0]);
                  if (k >= 0)
                        emit(id, k, list.length === 2 ? list[1] : list.slice(1));
                  else
                        emit(id, args.length, list);
                  return;
                  }
            case "sel":
                  if (list[0] === args[0])
                        emit(id, 0, "bang");
                  else
                        emit(id, 1, list[0]);
                  return;
            case "expr": {                      // ($i1 / $f2 … from the inlets; the left one hot; int() truncates)
                  s.in = s.in || [];
                  s.in[inlet] = list[0];
                  if (inlet !== 0)
                        return;
                  const src = b.text.slice(5).replace(/\$[if](\d)/g, (m, k) => "(" + (s.in[Number(k) - 1] || 0) + ")")
                        .replace(/\bint\(/g, "Math.trunc(").replace(/\bfloor\(/g, "Math.floor(");
                  emit(id, 0, Number(Function("return " + src)()));
                  return;
                  }
            case "snapshot~":                   // (the song position's phase: the model's)
                  if (inlet === 0 && list[0] === "bang")
                        emit(id, 0, songPhase);
                  return;
            case "peek~":                       // (the ring the script fills: the model's)
                  if (inlet === 0)
                        emit(id, 0, ring[Math.round(list[0])] || 0);
                  return;
            case "unpack":
                  for (let k = Math.min(list.length, args.length) - 1; k >= 0; --k)
                        emit(id, k, list[k]);
                  return;
            case "gate":                        // (gate n [initial]: inlet 0 picks the outlet, 1 … n; 0 closed)
                  if (s.open === undefined)
                        s.open = args.length > 1 ? args[1] : 0;
                  if (inlet === 0)
                        s.open = list[0];
                  else if (s.open)
                        emit(id, s.open - 1, v);
                  return;
            case "loadmess":
                  emit(id, 0, args.length === 1 ? args[0] : args);
                  return;
            case "-":
                  if (inlet === 0)
                        emit(id, 0, list[0] - args[0]);
                  return;
            case "prepend":
                  emit(id, 0, args.concat(list));
                  return;
            case "t": {                        // (trigger: right to left)
                  for (let k = args.length - 1; k >= 0; --k)
                        emit(id, k, args[k] === "b" ? "bang" : args[k] === "l" ? list : typeof args[k] === "number" ? args[k] : list[0]);
                  return;
                  }
            case "pack": {
                  s.v = s.v || args.slice();
                  if (inlet > 0) {
                        s.v[inlet] = list[0];
                        return;
                        }
                  if (list[0] !== "bang")
                        s.v[0] = list[0];
                  emit(id, 0, s.v.slice());
                  return;
                  }
            case "iter":
                  for (const x of list)
                        emit(id, 0, x);
                  return;
            case "midiformat": {
                  s.ch = s.ch || 1;
                  const status = (base) => base | (s.ch - 1);
                  if (inlet === 6)
                        s.ch = list[0];
                  else if (inlet === 0)
                        [status(0x90), list[0], list[1]].forEach((x) => emit(id, 0, x));
                  else if (inlet === 1)
                        [status(0xa0), list[1], list[0]].forEach((x) => emit(id, 0, x));
                  else if (inlet === 2)
                        [status(0xb0), list[0], list[1]].forEach((x) => emit(id, 0, x));
                  else if (inlet === 3)
                        [status(0xc0), list[0]].forEach((x) => emit(id, 0, x));
                  else if (inlet === 4)
                        [status(0xd0), list[0]].forEach((x) => emit(id, 0, x));
                  else if (inlet === 5)
                        [status(0xe0), 0, list[0]].forEach((x) => emit(id, 0, x));
                  return;
                  }
            case "midiout":
                  midiOut.push(list[0]);
                  return;
            case "zl.slice": {                 // (right outlet first: the rest, then the first n)
                  const n = args[0];
                  emit(id, 1, list.slice(n));
                  emit(id, 0, n === 1 ? list[0] : list.slice(0, n));
                  return;
                  }
            case "sprintf":                    // (the one format the patcher has: %ld)
                  emit(id, 0, String(args[0]).replace("%ld", String(Math.trunc(list[0]))));
                  return;
            case "forward":                    // "send name" sets the target; the rest goes to [receive name]
                  if (list[0] === "send") {
                        forwardTarget[id] = list[1];
                        return;
                        }
                  for (const r of Object.keys(boxes))
                        if (boxes[r].text === "receive" && receiveNames[r] === forwardTarget[id])
                              emit(r, 0, list.length === 1 ? list[0] : list);
                  return;
            case "receive":                    // (unnamed: "set name" names it)
                  if (list[0] === "set")
                        receiveNames[id] = list[1];
                  return;
            case "deferlow":
                  emit(id, 0, v);
                  return;
            case "v8":
                  toScript.push(list);
                  return;
            default:
                  return;           // (the script's side)
            }
      }

let songPhase = 0;
const ring = {};
const midiin = patcher.boxes.find((b) => b.box.text === "midiin").box.id;
for (const b of patcher.boxes)                  // (the patcher loaded: a [loadmess] with a number says it)
      if (b.box.maxclass === "newobj" && /^loadmess \d+$/.test(b.box.text || ""))
            receive(b.box.id, 0, "bang");
function play(bytes) {
      midiOut.length = 0;
      for (const x of bytes)
            emit(midiin, 0, x);
      return midiOut.slice();
      }

// the carriers' gate (MuseScoreLink.js › Carriers): the script's outlet 9 "gate 1" (a MuseScore clip on the track) or 2
const v8box = patcher.boxes.find((b) => b.box.text === "v8").box.id;
const carrierGate = patcher.boxes.find((b) => b.box.text === "gate 2 2").box.id;
function carriers(on) { emit(v8box, 9, ["gate", on ? 1 : 2]); }

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

test("on a track without a MuseScore clip (the gate as loaded, \"gate 2 2\", or the script's \"gate 2\") every note passes, keys 114-127 too", () => {
      assert.ok((wires[carrierGate + ":0"] || []).some(([d]) => /^route 127 /.test(boxes[d].text)));
      const pass = () => {
            for (let key = 114; key <= 127; ++key) {
                  assert.deepStrictEqual(play([0x90, key, 65]), [0x90, key, 65]);
                  assert.deepStrictEqual(play([0x90, key, 1]), [0x90, key, 1]);
                  assert.deepStrictEqual(play([0x90, key, 127]), [0x90, key, 127]);
                  assert.deepStrictEqual(play([0x80, key, 64]), [0x90, key, 0]);       // (its note-off kept, as note-on 0)
                  assert.deepStrictEqual(play([0x93, key, 90]), [0x93, key, 90]);     // (on its channel)
                  }
            assert.deepStrictEqual(play([0x90, 60, 90]), [0x90, 60, 90]);
            assert.deepStrictEqual(play([0xb0, 7, 100]), [0xb0, 7, 100]);
            assert.deepStrictEqual(play([0xe0, 0, 80]), [0xe0, 0, 80]);
            };
      pass();                                    // (as loaded: the model's gate takes its initial outlet from "gate 2 2")
      carriers(true);                            // a MuseScore clip on the track: converted
      assert.deepStrictEqual(play([0x90, 126, 65]), [0xb0, 1, 65]);
      assert.deepStrictEqual(play([0x80, 126, 0]), []);
      carriers(false);                           // gone: through again
      pass();
      carriers(true);                            // (the tests below: a track with MuseScore's clips)
      });

test("notes pass unchanged", () => {
      assert.deepStrictEqual(play([0x90, 60, 90]), [0x90, 60, 90]);
      assert.deepStrictEqual(play([0x80, 60, 0]), [0x90, 60, 0]);            // (a note-off as note-on 0)
      assert.deepStrictEqual(play([0x90, 113, 20]), [0x90, 113, 20]);        // just below the carriers
      });

test("each carrier key's note-on becomes its controller (UACC: velocity - 1; the others: the velocity, 1 as 0); its note-off goes", () => {
      CARRIER_CCS.forEach((cc, i) => {
            const key = 127 - i;
            const uacc = cc === 32;
            assert.deepStrictEqual(play([0x90, key, 1]), [0xb0, cc, 0]);
            assert.deepStrictEqual(play([0x90, key, 127]), [0xb0, cc, uacc ? 126 : 127]);
            assert.deepStrictEqual(play([0x90, key, 65]), [0xb0, cc, uacc ? 64 : 65]);
            assert.deepStrictEqual(play([0x90, key, 2]), [0xb0, cc, uacc ? 1 : 2]);
            assert.deepStrictEqual(play([0x80, key, 64]), []);
            assert.deepStrictEqual(play([0x90, key, 0]), []);
            });
      });

test("the pitch bend carriers (115 upper, 114 lower 7 bits; the velocity, 1 as 0) become one pitch bend", () => {
      // the first: the centre until then (pack 224 0 64); upper 64 then lower 0 (velocity 1)
      assert.deepStrictEqual(play([0x90, 115, 64]), [0xe0, 0, 64]);               // 8192
      assert.deepStrictEqual(play([0x90, 114, 1]), [0xe0, 0, 64]);
      // +50 cents of ±100: 8192 + 4096 = 12288 = upper 96, lower 0: only the upper half written
      assert.deepStrictEqual(play([0x90, 115, 96]), [0xe0, 0, 96]);
      assert.deepStrictEqual(play([0x80, 115, 0]), []);                           // its note-off dropped
      // 12345 = upper 96, lower 57: the lower half alone, then a pair in the other order (a chase)
      assert.deepStrictEqual(play([0x90, 114, 57]), [0xe0, 57, 96]);
      assert.deepStrictEqual(play([0x90, 115, 32]), [0xe0, 57, 32]);              // (upper 32 with the lower kept)
      assert.deepStrictEqual(play([0x90, 114, 1]), [0xe0, 0, 32]);                // 4096: -50 cents
      assert.deepStrictEqual(play([0x80, 114, 0]), []);
      assert.deepStrictEqual(play([0x90, 114, 0]), []);                           // (a note-on 0 is a note-off)
      // the extremes: 16383 (127 / 127) and 0 (1 / 1)
      assert.deepStrictEqual(play([0x90, 115, 127]).concat(play([0x90, 114, 127])), [0xe0, 57 - 57, 127, 0xe0, 127, 127]);
      assert.deepStrictEqual(play([0x90, 115, 1]).concat(play([0x90, 114, 1])), [0xe0, 127, 0, 0xe0, 0, 0]);
      });

test("MuseScore's pitch bend carriers: the same keys (libmscore/liveclips.h)", () => {
      const h = fs.readFileSync(path.join(dir, "..", "..", "libmscore", "liveclips.h"), "utf8");
      assert.ok(/BEND_MSB\s*= 115;/.test(h));
      assert.ok(/BEND_LSB\s*= 114;/.test(h));
      assert.ok(/CARRIER_LOW\s*= 114;/.test(h));
      assert.ok(patcher.boxes.some((b) => b.box.text === "route 127 126 125 124 123 122 121 120 119 118 117 116 115 114"));
      });

test("other messages pass: controllers, program, pressure, bend, on their channel", () => {
      assert.deepStrictEqual(play([0xb0, 7, 100]), [0xb0, 7, 100]);
      assert.deepStrictEqual(play([0xc0, 5]), [0xc0, 5]);
      assert.deepStrictEqual(play([0xd0, 33]), [0xd0, 33]);
      assert.deepStrictEqual(play([0xe0, 0, 80]), [0xe0, 0, 80]);
      assert.deepStrictEqual(play([0x91, 60, 90]), [0x91, 60, 90]);
      });

test("the script's box holds MuseScoreLink.js; the patch opens in presentation", () => {
      const v8 = patcher.boxes.find((b) => b.box.text === "v8").box;
      assert.strictEqual(v8.textfile.text, fs.readFileSync(path.join(dir, "MuseScoreLink.js"), "utf8"));
      assert.strictEqual(v8.textfile.embed, 1);
      assert.strictEqual(patcher.openinpresentation, 1);
      assert.ok(patcher.boxes.some((b) => b.box.text === "udpsend 127.0.0.1 9002"));
      });

test("the .amxd: ampf mmmm, meta, ptch with mx@c and the same JSON, its directory", () => {
      const a = fs.readFileSync(path.join(dir, "MuseScore Link.amxd"));
      assert.strictEqual(a.toString("ascii", 0, 4), "ampf");
      assert.strictEqual(a.readUInt32LE(4), 4);
      assert.strictEqual(a.toString("ascii", 8, 12), "mmmm");
      assert.strictEqual(a.toString("ascii", 12, 16), "meta");
      assert.strictEqual(a.readUInt32LE(16), 4);
      assert.strictEqual(a.readUInt32LE(20), 7);
      assert.strictEqual(a.toString("ascii", 24, 28), "ptch");
      assert.strictEqual(a.readUInt32LE(28), a.length - 32);
      assert.strictEqual(a.toString("ascii", 32, 36), "mx@c");
      assert.strictEqual(a.readUInt32BE(36), 16);
      const jsonEnd = 32 + a.readUInt32BE(44);
      const json = a.toString("utf8", 48, jsonEnd - 2);
      assert.deepStrictEqual(JSON.parse(json), { patcher: patcher });
      assert.strictEqual(a.toString("ascii", jsonEnd - 2, jsonEnd), "\n\0");
      assert.strictEqual(a.toString("ascii", jsonEnd, jsonEnd + 4), "dlst");
      assert.strictEqual(a.readUInt32BE(jsonEnd + 4), a.length - jsonEnd);
      assert.ok(a.toString("latin1", jsonEnd).includes("MuseScore Link.amxd"));
      });

test("Live parameters can hold their range: an Int one has 256 steps, so the port is a Float shown whole, initial 9001", () => {
      for (const b of patcher.boxes) {
            const v = b.box.saved_attribute_attributes && b.box.saved_attribute_attributes.valueof;
            if (!v || v.parameter_type !== 1)
                  continue;
            const lo = v.parameter_mmin || 0, hi = v.parameter_mmax === undefined ? 127 : v.parameter_mmax;
            assert.ok(hi - lo <= 255, v.parameter_longname + ": an Int parameter spans " + (hi - lo + 1) + " values (Live: 256)");
            }
      const port = patcher.boxes.find((b) => b.box.varname === "Port").box.saved_attribute_attributes.valueof;
      assert.strictEqual(port.parameter_type, 0);
      assert.strictEqual(port.parameter_unitstyle, 0);
      assert.deepStrictEqual(port.parameter_initial, [9001]);
      assert.ok(port.parameter_mmin <= 9001 && 9001 <= port.parameter_mmax);
      });

test("the carrier table is MuseScore's (libmscore/liveclips.cpp)", () => {
      const cpp = fs.readFileSync(path.join(dir, "..", "..", "libmscore", "liveclips.cpp"), "utf8");
      const m = /CARRIER_CCS\[CARRIER_COUNT\] = \{([^}]*)\}/.exec(cpp);
      assert.ok(m);
      assert.deepStrictEqual(m[1].split(",").map((x) => Number(x.trim())), CARRIER_CCS);
      const pre = patcher.boxes.filter((b) => /^prepend 176 /.test(b.box.text || "")).map((b) => Number(b.box.text.split(" ")[2]));
      assert.deepStrictEqual(pre, CARRIER_CCS);
      });

test("parameter lanes: the song position in ms (phasor~ locked to Live's transport, *~ from the script), 16 slots", () => {
      const byText = (t) => patcher.boxes.filter((b) => b.box.text === t).map((b) => b.box.id);
      const has = (a, ao, b, bi) => (wires[a + ":" + ao] || []).some(([d, i]) => d === b && i === bi);
      const v8 = byText("v8")[0];
      assert.strictEqual(boxes[v8].numoutlets, 10);
      const [phasor] = byText("phasor~ @frequency 7864320 ticks @lock 1");
      assert.strictEqual(7864320, 16384 * 480);
      const [ms] = byText("*~ 1.");
      assert.ok(phasor && ms);
      assert.ok(has(phasor, 0, ms, 0));
      assert.ok(has(v8, 4, ms, 1));                          // the factor
      const snap = byText("snapshot~").find((x) => has(ms, 0, x, 0));      // (the other one reads the phase: the velocity shaper)
      const [pos] = byText("prepend posvalue");
      assert.ok(has(ms, 0, snap, 0) && has(v8, 5, snap, 0) && has(snap, 0, pos, 0) && has(pos, 0, v8, 0));
      const [route] = byText("route " + Array.from({ length: 16 }, (_, k) => k).join(" "));
      assert.ok(route && has(v8, 3, route, 0));
      const remotes = byText("live.remote~");
      assert.strictEqual(remotes.length, 16);
      for (let k = 0; k < 16; ++k) {
            assert.strictEqual(byText("buffer~ ---mslp" + k).length, 1);
            const [idx] = byText("index~ ---mslp" + k);
            assert.ok(idx && has(ms, 0, idx, 0), "index~ " + k);
            const remote = remotes.find((r) => has(idx, 0, r, 0));
            assert.ok(remote, "index~ " + k + " -> a live.remote~");
            assert.ok(has(route, k, remote, 1), "route " + k + " -> live.remote~'s id inlet");
            assert.ok(!remotes.some((r) => r !== remote && has(route, k, r, 1)));
            }
      // the "---" prefix and the hub's word, through deferlow
      const [prefix] = byText("loadmess prefix ---mslp");
      assert.ok(prefix && has(prefix, 0, v8, 0));
      const [rcv] = byText("receive msl_params");
      const dl = byText("deferlow").find((d) => has(rcv, 0, d, 0));
      const [pre] = byText("prepend msl_params");
      assert.ok(has(rcv, 0, dl, 0) && has(dl, 0, pre, 0) && has(pre, 0, v8, 0));
      // the script's own outlets kept: OSC and udpsend's settings, the status
      const [send] = byText("udpsend 127.0.0.1 9002");
      assert.ok(has(v8, 0, send, 0) && has(v8, 1, send, 0));
      assert.ok(patcher.boxes.some((b) => b.box.maxclass === "comment" && has(v8, 2, b.box.id, 0)));
      });

test("a clip tab's notes: /ms/midi from the hub's udpreceive reach the copy on that track's midiout, at once; the rest goes to the script", () => {
      const into = patcher.boxes.find((b) => b.box.varname === "msl_in").box;
      assert.strictEqual(into.text, "route /ms/midi");
      const v8 = patcher.boxes.find((b) => b.box.text === "v8").box.id;
      emit(v8, 7, ["set", "msl_m12"]);                  // (the script names its [receive] after its track, id 12)
      const osc = (l) => { midiOut.length = 0; toScript.length = 0; receive(into.id, 0, l); return midiOut.slice(); };
      assert.deepStrictEqual(osc(["/ms/midi", 12, 0x90, 60, 100]), [0x90, 60, 100]);
      assert.deepStrictEqual(osc(["/ms/midi", 12, 0x80, 60, 0]), [0x80, 60, 0]);
      assert.deepStrictEqual(osc(["/ms/midi", 12, 0xb0, 64, 127]), [0xb0, 64, 127]);
      assert.deepStrictEqual(osc(["/ms/midi", 12, 0xe0, 0, 80]), [0xe0, 0, 80]);
      assert.deepStrictEqual(toScript, []);             // (not through the script)
      assert.deepStrictEqual(osc(["/ms/midi", 13, 0x90, 60, 100]), []);   // another track's copy
      assert.deepStrictEqual(osc(["/ms/song", 1, 120]), []);
      assert.deepStrictEqual(toScript, [["/ms/song", 1, 120]]);           // everything else: deferlow -> the script
      // the carriers still work alongside
      assert.deepStrictEqual(play([0x90, 126, 65]), [0xb0, 1, 65]);
      });

test("the velocity shaper: a note-on's velocity by the ring's code at the song position (MuseScoreLink.js velShape)", () => {
      const js = fs.readFileSync(path.join(dir, "MuseScoreLink.js"), "utf8");
      const ringSize = Number(/loadmess (\d+)/.exec(patcher.boxes.map((b) => b.box.text || "").join("\n"))[1]);
      assert.strictEqual(ringSize, 2 * Math.ceil(2 * 999 / 60 * 480));
      const v8 = patcher.boxes.find((b) => b.box.text === "v8").box.id;
      // the bias: one signal vector (64 samples at 44.1 kHz) at 120 bpm, in ticks
      const bias = 64 / 44100 * 120 / 60 * 480;
      emit(v8, 9, ["bias", bias]);
      const at = (beat) => { songPhase = (beat * 480 - bias) / (16384 * 480); };     // (snapshot~: a vector early)
      const cell = (beat) => Math.round(beat * 480) % ringSize;
      ring[cell(10)] = 1.5;                       // scale 50 %
      ring[cell(11)] = -64;                       // absolute 64
      ring[cell(12)] = 0;                         // as it is
      ring[cell(13)] = 3;                         // 200 %
      at(10);
      assert.deepStrictEqual(play([0x90, 60, 100]), [0x90, 60, 50]);
      assert.deepStrictEqual(play([0x80, 60, 0]), [0x90, 60, 0]);              // a note-off as it is
      at(11);
      assert.deepStrictEqual(play([0x90, 61, 10]), [0x90, 61, 64]);
      at(12);
      assert.deepStrictEqual(play([0x90, 62, 77]), [0x90, 62, 77]);
      at(13);
      assert.deepStrictEqual(play([0x90, 63, 100]), [0x90, 63, 127]);           // 200 -> 127
      assert.deepStrictEqual(play([0x91, 63, 1]), [0x91, 63, 2]);               // its channel kept
      ring[cell(13)] = 1.0;                       // 0 % -> 1, never a note-off
      assert.deepStrictEqual(play([0x90, 63, 100]), [0x90, 63, 1]);
      // the code is reset for each note: an empty cell after a shaped one plays the note as it is
      at(14);
      assert.deepStrictEqual(play([0x90, 64, 99]), [0x90, 64, 99]);
      // the carriers aren't shaped
      at(10);
      assert.deepStrictEqual(play([0x90, 126, 65]), [0xb0, 1, 65]);
      // the log: closed until the script opens it, then each note to the script
      toScript.length = 0;
      play([0x90, 60, 100]);
      assert.deepStrictEqual(toScript, []);
      emit(v8, 9, ["log", 1]);
      play([0x90, 60, 100]);
      assert.strictEqual(toScript.length, 1);
      assert.deepStrictEqual([toScript[0][0], toScript[0][1], toScript[0][3], toScript[0][4]], ["vlog", 60, 100, 50]);
      emit(v8, 9, ["log", 0]);
      at(0);
      for (const k of Object.keys(ring))
            delete ring[k];
      assert.ok(/function velShape\(v, c\)/.test(js));
      });

test("the script's constants agree with the patcher's", () => {
      const js = fs.readFileSync(path.join(dir, "MuseScoreLink.js"), "utf8");
      assert.ok(/var SLOTS = 16;/.test(js));
      assert.ok(/var PERIOD_QUARTERS = 16384;/.test(js));
      assert.ok(/outlets = 10;/.test(js));
      });

if (failures) {
      console.log(failures + " failed");
      process.exit(1);
      }
console.log("all passed");
