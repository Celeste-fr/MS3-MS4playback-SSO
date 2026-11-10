/* Score Script playground engine: the browser host of libmscore/script/scorescript.lua (MuseScore's Lua console).
   The page and its test/run.js share this file verbatim (tools/scripting/update_playground.py puts it and the Lua
   library into the page). The language itself, the rewrite included, is the Lua library's: this file only
   tokenizes for the page's editor (colours, hints), turns the page's score data into what M.setScore takes
   (hostData) and runs the library on Fengari (Lua 5.3). */
var ScoreScript = (function () {
  "use strict";
  var KEYWORDS = new Set(["and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if", "in",
    "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"]);
  var QUERY = new Set(["findall", "first", "last", "next", "count"]);
  var OPS = ["...", "..", "==", "~=", "<=", ">=", "//", "::", "<<", ">>"];

  function longBracketAt(src, i) { // returns level or -1 for [==[ at i
    if (src[i] !== "[") return -1;
    var j = i + 1, lvl = 0;
    while (src[j] === "=") { lvl++; j++; }
    return src[j] === "[" ? lvl : -1;
  }
  function findLongClose(src, i, lvl) {
    var close = "]" + "=".repeat(lvl) + "]";
    var k = src.indexOf(close, i);
    return k < 0 ? src.length : k + close.length;
  }

  /* Tokens {t, v, open?}: t in ws, comment, string, number, name, kw, op. Concatenating v gives the source back.
     open: true for an unterminated string / long comment (used by autocomplete). */
  function tokenize(src) {
    var toks = [], i = 0, n = src.length, m;
    while (i < n) {
      var c = src[i], start = i;
      if (/\s/.test(c)) {
        while (i < n && /\s/.test(src[i])) i++;
        toks.push({ t: "ws", v: src.slice(start, i) });
      } else if (c === "-" && src[i + 1] === "-") {
        var lvl = longBracketAt(src, i + 2), open = false;
        if (lvl >= 0) { i = findLongClose(src, i + 2, lvl); open = src.slice(start).indexOf("]" + "=".repeat(lvl) + "]") < 0; }
        else { while (i < n && src[i] !== "\n") i++; }
        toks.push({ t: "comment", v: src.slice(start, i), open: open });
      } else if (c === '"' || c === "'") {
        i++;
        var closed = false;
        while (i < n) {
          if (src[i] === "\\") { i += 2; continue; }
          if (src[i] === c) { i++; closed = true; break; }
          if (src[i] === "\n") break;
          i++;
        }
        toks.push({ t: "string", v: src.slice(start, Math.min(i, n)), open: !closed });
        i = Math.min(i, n);
      } else if (c === "[" && longBracketAt(src, i) >= 0) {
        var l2 = longBracketAt(src, i);
        i = findLongClose(src, i, l2);
        toks.push({ t: "string", v: src.slice(start, i), open: src.slice(start + 2).indexOf("]" + "=".repeat(l2) + "]") < 0 });
      } else if (/[0-9]/.test(c) || (c === "." && /[0-9]/.test(src[i + 1] || ""))) {
        m = /^(0[xX][0-9a-fA-F]*(\.[0-9a-fA-F]*)?([pP][+-]?[0-9]+)?|[0-9]*\.?[0-9]*([eE][+-]?[0-9]+)?)/.exec(src.slice(i));
        i += Math.max(1, m[0].length);
        toks.push({ t: "number", v: src.slice(start, i) });
      } else if (/[A-Za-z_]/.test(c)) {
        while (i < n && /[A-Za-z0-9_]/.test(src[i])) i++;
        var w = src.slice(start, i);
        toks.push({ t: KEYWORDS.has(w) ? "kw" : "name", v: w });
      } else {
        var op = null;
        for (var k = 0; k < OPS.length; k++) if (src.startsWith(OPS[k], i)) { op = OPS[k]; break; }
        if (!op) op = c;
        i += op.length;
        toks.push({ t: "op", v: op });
      }
    }
    return toks;
  }


  /* ---------- the page's score data -> the library's (MuseScore's instrument ids; tie continuations folded) ---------- */
  var INSTRUMENTS = { "Flute": ["flute", "flutes", "woodwinds"], "Oboe": ["oboe", "oboes", "woodwinds"],
    "Clarinet": ["bb-clarinet", "clarinets", "woodwinds"], "Bassoon": ["bassoon", "bassoons", "woodwinds"],
    "Horn": ["horn", "horns", "brass"], "Violin I": ["violin", "orchestral-strings", "strings"],
    "Violin II": ["violin", "orchestral-strings", "strings"], "Viola": ["viola", "orchestral-strings", "strings"],
    "Cello": ["violoncello", "orchestral-strings", "strings"], "Bass": ["contrabass", "orchestral-strings", "strings"] };
  function hostData(data) {
    if (data.parts.length && typeof data.parts[0] === "object") return data;   // already the library's form
    var parts = data.parts.map(function (p) {
      var i = INSTRUMENTS[p] || [];
      return { name: p, instrument: i[0], family: i[1], group: i[2] };
    });
    var div = data.division || 480;
    var sorted = data.notes.slice().sort(function (a, b) { return a.tick - b.tick; });
    var open = {}, dropped = {}, extra = {};
    sorted.forEach(function (n) {
      var key = n.part + "|" + n.voice + "|" + n.pitch, h = open[key];
      if (h && n.tied && Math.abs(h.end - n.tick) < 1) {
        dropped[n.id] = h.id; extra[h.id] = (extra[h.id] || 0) + n.duration; h.end = n.tick + n.duration * div;
      } else if (n.tied) open[key] = { id: n.id, end: n.tick + n.duration * div };
      else delete open[key];
    });
    var notes = data.notes.filter(function (n) { return !dropped[n.id]; }).map(function (n) {
      return { id: n.id, part: data.parts.indexOf(n.part) + 1, measure: n.measure, index: n.measure, beat: n.beat, tick: n.tick,
        duration: n.duration + (extra[n.id] || 0), pitch: n.pitch, sounding: n.pitch, name: n.name, degree: n.degree,
        octave: n.octave, voice: n.voice, staff: 1, tied: n.tied && !!extra[n.id], grace: false, chordId: n.chordId,
        chord: n.chord, top: n.top, bottom: n.bottom, slurs: n.slur ? [n.slur] : [], marks: n.marks, dynamic: n.dynamic || null };
    });
    return { parts: parts, notes: notes };
  }

  function luaLiteral(v) {
    if (v === null || v === undefined) return "nil";
    if (typeof v === "boolean") return v ? "true" : "false";
    if (typeof v === "number") return isFinite(v) ? String(v) : "0";
    if (typeof v === "string") return '"' + v.replace(/\\/g, "\\\\").replace(/"/g, '\\"').replace(/\n/g, "\\n").replace(/\r/g, "\\r") + '"';
    if (Array.isArray(v)) return "{" + v.map(luaLiteral).join(",") + "}";
    var parts = [];
    Object.keys(v).forEach(function (k) {
      if (v[k] === null || v[k] === undefined) return;
      var key = /^[A-Za-z_][A-Za-z0-9_]*$/.test(k) && !KEYWORDS.has(k) ? k : "[" + luaLiteral(/^\d+$/.test(k) ? Number(k) : k) + "]";
      parts.push(key + "=" + luaLiteral(v[k]));
    });
    return "{" + parts.join(",") + "}";
  }

  /* data: the page's score JSON (or the library's form); librarySrc: scorescript.lua. One Lua state, made on first use.
     run(src, {preview, marks: {id: [names]}, persist, echo, limit}) -> {error, errorLine, log, matched, ops, preview, code}
     rewrite(src) -> the Lua the library loads (throws its error); check(src) -> "" or the message */
  function makeEngine(fengari, data, librarySrc) {
    var lua = fengari.lua, lauxlib = fengari.lauxlib, lualib = fengari.lualib;
    function doString(L, s, name) {
      var st = lauxlib.luaL_loadbuffer(L, fengari.to_luastring(s), null, fengari.to_luastring(name));
      if (st === lua.LUA_OK) st = lua.lua_pcall(L, 0, 0, 0);
      if (st !== lua.LUA_OK) throw new Error(name + ": " + fengari.to_jsstring(lua.lua_tostring(L, -1)));
    }
    var L = null;
    function state() {
      if (L) return L;
      var S = lauxlib.luaL_newstate();
      lualib.luaL_openlibs(S);
      var st = lauxlib.luaL_loadbuffer(S, fengari.to_luastring(librarySrc), null, fengari.to_luastring("=scorescript"));
      if (st === lua.LUA_OK) st = lua.lua_pcall(S, 0, 1, 0);
      if (st !== lua.LUA_OK) throw new Error("scorescript: " + fengari.to_jsstring(lua.lua_tostring(S, -1)));
      lua.lua_setglobal(S, fengari.to_luastring("__M"));
      doString(S, "__M.setScore(" + luaLiteral(hostData(data)) + ")", "=data");
      doString(S, "function __call(name, src, opts) local r = __M[name](src, opts); if r == nil then return '' end return r end", "=host");
      L = S;
      return L;
    }
    function call(name, src, opts) {           // -> {ok, text}
      var S = state();
      var top = lua.lua_gettop(S);
      lua.lua_getglobal(S, fengari.to_luastring("__call"));
      lua.lua_pushstring(S, fengari.to_luastring(name));
      lua.lua_pushstring(S, fengari.to_luastring(src));
      var st = lauxlib.luaL_loadstring(S, fengari.to_luastring("return " + luaLiteral(opts || {})));
      if (st === lua.LUA_OK) st = lua.lua_pcall(S, 0, 1, 0);
      if (st === lua.LUA_OK) st = lua.lua_pcall(S, 3, 1, 0);
      var out = fengari.to_jsstring(lua.lua_tostring(S, -1));
      lua.lua_settop(S, top);
      return { ok: st === lua.LUA_OK, text: out };
    }
    function run(src, opts) {
      opts = opts || {};
      var r = call("runJSON", src, { preview: !!opts.preview, marks: opts.marks, persist: !!opts.persist, echo: !!opts.echo, limit: opts.limit });
      if (!r.ok) { L = null; return { error: "internal error: " + r.text, log: [], matched: [], ops: [] }; }
      return JSON.parse(r.text);
    }
    run.warm = state;
    function rewrite(src) { var r = call("rewrite", src); if (!r.ok) throw new Error(r.text); return r.text; }
    function check(src) { var r = call("check", src); return r.ok ? r.text : r.text; }
    function setScore(d) { data = d; if (L) doString(L, "__M.setScore(" + luaLiteral(hostData(d)) + ")", "=data"); }
    return { run: run, rewrite: rewrite, check: check, setScore: setScore };
  }

  return { tokenize: tokenize, makeEngine: makeEngine, hostData: hostData, luaLiteral: luaLiteral, QUERY: QUERY, KEYWORDS: KEYWORDS };
})();
if (typeof module !== "undefined") module.exports = ScoreScript;
