// VLStore: research device (clip-velocity-lane, 2026-10-03): which storage persists with the set without an undo step.
// OSC in on UDP 9011 (udpreceive in the patcher), answers /t/r id text to 127.0.0.1:9002.
//   /t/set kind id text   kind A..G: write text into that store
//   /t/get id             every store's value now, and what each gave back at load
//   /t/undo id            song.can_undo, can_redo
//   /t/call id fn         song.<fn>() (undo / redo)
autowatch = 0;
inlets = 1;
outlets = 3;      // 0: udpsend, 1: "kind value" to the stores' route, 2: unused
var restored = {};
var last = {};
var log = [];
function now() { return new Date().getTime(); }
function str(v) { return Array.isArray(v) ? v.join(" ") : String(v); }
function reply(id, text) { outlet(0, "/t/r", id, String(text).substring(0, 6000)); }
function anything() {
      var a = arrayfromargs(arguments);
      var m = messagename;
      if (m === "back") {                       // a store's output: back <kind> value…
            var k = String(a[0]);
            var v = a.slice(1).join(" ");
            last[k] = v;
            if (!restored[k]) restored[k] = v;
            log.push(now() + " " + k + " " + v.substring(0, 40));
            return;
            }
      if (m === "/t/set") {
            var kind = String(a[0]), id = a[1], text = a.slice(2).join(" ");
            if (kind === "C" || kind === "D") {
                  var d = new Dict(kind === "C" ? "vlC" : "vlD");
                  d.set("v", text);
                  outlet(1, kind, "bang");              // (a dict: notify the box)
                  }
            else if (kind === "E")
                  outlet(1, "E", "store", 1, text);
            else if (kind === "F" || kind === "G")
                  outlet(1, kind, parseFloat(text));
            else
                  outlet(1, kind, text);
            return reply(id, "set " + kind);
            }
      if (m === "/t/get") {
            var o = { restored: restored, last: last, log: log.slice(-20) };
            try { o.C = new Dict("vlC").get("v"); } catch (e) { o.C = "err " + e; }
            try { o.D = new Dict("vlD").get("v"); } catch (e) { o.D = "err " + e; }
            outlet(1, "E", 1);                          // (coll: its line 1 comes back as "back E …")
            outlet(1, "A", "bang"); outlet(1, "B", "bang"); outlet(1, "F", "bang"); outlet(1, "G", "bang");
            o.lastAfterBang = last;
            return reply(a[0], JSON.stringify(o));
            }
      if (m === "/t/undo") {
            var s = new LiveAPI("live_set");
            return reply(a[0], "can_undo " + s.get("can_undo") + " can_redo " + s.get("can_redo"));
            }
      if (m === "/t/call") {
            var s2 = new LiveAPI("live_set");
            s2.call(String(a[1]));
            return reply(a[0], "called " + a[1]);
            }
      }
