// The MuseScore Link patcher's MIDI path (MuseScoreLink.maxpat, made by make_device.py), run on a small
// model of the Max objects it uses, as Cycling '74's reference describes them (midiparse, route, sel,
// -, prepend, iter, midiformat, midiout). It checks the wiring: a carrier note becomes its controller,
// its note-off is dropped, every other message passes unchanged. It can't show that Max behaves as
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
            case "expr":                        // (the one expression the patcher has)
                  assert.strictEqual(b.text, "expr $i1*($i1>1)");
                  emit(id, 0, list[0] * (list[0] > 1 ? 1 : 0));
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
                        emit(id, k, args[k] === "b" ? "bang" : list[0]);
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
            default:
                  return;           // (the script's side)
            }
      }

const midiin = patcher.boxes.find((b) => b.box.text === "midiin").box.id;
function play(bytes) {
      midiOut.length = 0;
      for (const x of bytes)
            emit(midiin, 0, x);
      return midiOut.slice();
      }

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

if (failures) {
      console.log(failures + " failed");
      process.exit(1);
      }
console.log("all passed");
