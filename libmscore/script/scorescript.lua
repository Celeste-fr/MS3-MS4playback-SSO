-- Score Script: the query library of MuseScore's Lua console (libmscore/script/scriptengine.h is its host).
--
-- ONE implementation for three hosts: MuseScore (ScriptEngine embeds this file and loads it into a sandboxed
-- lua_State), the browser playground (Fengari, Lua 5.3) and a plain `lua` (test/run.lua). So: Lua 5.3 syntax only,
-- no dependency beyond the standard library, and `debug` used only when the host leaves it visible while this file
-- loads (line numbers, the instruction limit of hosts that have no C hook).
--
-- A script queries the notes of a score with conditions evaluated per note, ANDed left to right, short-circuit:
--     findall(strings, 1 <= measure <= 16, slur, slur.index == 1).add(marcato)
-- also first(...), last(...), next(m, ...), count(...). That isn't Lua: before loading, the source is REWRITTEN
-- (M.rewrite, a token pass; line numbers are kept):
--   * each top-level argument of those five calls becomes  function(_ENV) return <arg> end  (a function literal is
--     left alone; next's first argument, the bar, too), so inside a condition every name is looked up on the note:
--     its properties, the score's sections and parts, marks, durations, dynamics, then the script's own globals;
--     an unknown name is an error with a did-you-mean;
--   * a <= b <= c  ->  (a <= b and b <= c)  when b is a plain name or constant, else __chain(a, "<=", b, "<=", c):
--     the middle operand is evaluated once;
--   * beat == x, duration == x compare with a tolerance (__near: both are fractions, 1 + 1/3 and 4/3 may differ by
--     one bit); dynamic == x compares dynamics (never text: __dyneq); part == "name" is part("name") (__parteq);
--   * slur.x inside a condition reads __slur.x: an unslurred note's fields are false / nil instead of an error,
--     while `slur` alone stays nil (falsy) there. No error text is ever matched to make a condition false: a
--     script's own nil index is an error.
-- Declarations are checked first (checkNames): a local, loop variable or parameter named like a note property,
-- section, mark, duration or dynamic is an error ("local 'measure' hides the note property measure; rename it";
-- p and f are exempt: too common, and dynamic == "p" always works), and the five query names are reserved.
--
-- A condition must be true, false or nil. Exceptions with one meaning each: a mark (has(mark)), `slur` and `dynamic`
-- (the note has one), a selection (the note is in it), a function (called with the note). Anything else (a number, text, a table) is an error naming
-- the argument.
--
-- The unit is the note; a tied note is ONE note (its first notehead; `duration` is the whole tied length), grace
-- notes are left out unless the query mentions `grace`, rests never appear. Marks belong to chords: add / remove
-- are idempotent and act once per chord. The host supplies the notes as plain tables (M.setScore, the fields under
-- "notes" below) and applies changes through callbacks (M.setHost: add, remove, text); nothing here touches a
-- score.
--
-- Evaluation uses ONE shared environment with a current-note pointer (no table per note). A selection keeps the
-- score generation it was made in: used after the score's notes changed it raises a clear error (the REPL keeps
-- variables between lines).
--
-- Sandbox: a script sees only its own globals, the names above and a whitelist of the standard library, as copies
-- (string, table, math, utf8; no load, require, io, os, debug, coroutine, no metatable of a non-table). The
-- instruction limit raises KILL, which the sandbox's pcall and this file's own pcalls pass on, so a script can't
-- swallow it. A preview run (opts.preview) changes a shadow copy of the marks, calls no host callback and leaves
-- the persistent REPL table alone.
--
-- API: M.rewrite(src), M.setScore(data), M.setHost(host), M.run(src, opts), M.runJSON(src, opts), M.check(src),
-- M.words(), M.complete(text, cursor), M.help(word), M.KILL. Tests: test/run.lua (standalone), the playground's
-- test/run.js (Fengari), mtest/libmscore/scripting (MuseScore).

local M = {}

local rawnext, rawget, rawset, setmetatable, getmetatable = next, rawget, rawset, setmetatable, getmetatable
local type, tostring, tonumber, pcall, error, select, ipairs, pairs = type, tostring, tonumber, pcall, error, select, ipairs, pairs
local load, assert = load, assert
local sformat, concat, sort, tpack, tunpack = string.format, table.concat, table.sort, table.pack, table.unpack
local floor, abs, huge, mmin = math.floor, math.abs, math.huge, math.min
local debug = debug          -- nil when the host hides it
local STD = {string = string, table = table, math = math, utf8 = utf8}

local KILL = setmetatable({}, {__tostring = function() return "the script ran too long and was stopped (an endless loop?)" end})
M.KILL = KILL
local MEMORY = "not enough memory"

local function set(list) local t = {} for _, x in ipairs(list) do t[x] = true end return t end
local function fail(line, msg) error({line = line, msg = msg}, 0) end

------------------------------------------------------------------------------------------------- tokens
local KEYWORDS = set{"and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", "if", "in",
  "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"}
local QUERY_NAMES = {"findall", "first", "last", "next", "count"}
local QUERY = set(QUERY_NAMES)
local OPS3 = {"...", "..", "==", "~=", "<=", ">=", "//", "::", "<<", ">>"}
local REL = set{"<", "<=", ">", ">="}

local function longBracketAt(src, i)      -- the level of [==[ at i, or nil
  if src:sub(i, i) ~= "[" then return nil end
  local j = src:match("^=*()", i + 1)
  if src:sub(j, j) == "[" then return j - i - 1 end
  return nil
end
local function findLongClose(src, i, lvl) -- -> index after the close, closed?
  local close = "]" .. ("="):rep(lvl) .. "]"
  local a, b = src:find(close, i, true)
  if not a then return #src + 1, false end
  return b + 1, true
end

-- Tokens {t, v, line, pos, open}: t in ws, comment, string, number, name, kw, op. Concatenating v gives the
-- source back. open: an unterminated string / long comment (completion).
local function tokenize(src)
  local toks, i, n, line = {}, 1, #src, 1
  local function push(t, start, open)
    local v = src:sub(start, i - 1)
    toks[#toks + 1] = {t = t, v = v, line = line, pos = start, open = open}
    local _, nl = v:gsub("\n", "")
    line = line + nl
  end
  while i <= n do
    local c, start = src:sub(i, i), i
    if c:find("%s") then
      i = src:find("%S", i) or n + 1
      push("ws", start)
    elseif c == "-" and src:sub(i + 1, i + 1) == "-" then
      local lvl, open = longBracketAt(src, i + 2), false
      if lvl then
        local e, closed = findLongClose(src, i + 2, lvl)
        i, open = e, not closed
      else
        i = src:find("\n", i, true) or n + 1
      end
      push("comment", start, open)
    elseif c == '"' or c == "'" then
      i = i + 1
      local closed = false
      while i <= n do
        local ch = src:sub(i, i)
        if ch == "\\" then i = i + 2
        elseif ch == c then i = i + 1; closed = true; break
        elseif ch == "\n" then break
        else i = i + 1 end
      end
      if i > n + 1 then i = n + 1 end
      push("string", start, not closed)
    elseif c == "[" and longBracketAt(src, i) then
      local e, closed = findLongClose(src, i, longBracketAt(src, i))
      i = e
      push("string", start, not closed)
    elseif c:find("%d") or (c == "." and src:sub(i + 1, i + 1):find("%d")) then
      local j
      if src:find("^0[xX]", i) then
        j = src:match("^%x*()", i + 2)
        if src:sub(j, j) == "." then j = src:match("^%x*()", j + 1) end
        j = src:match("^[pP][+-]?%d+()", j) or j
      else
        j = src:match("^%d*()", i)
        if src:sub(j, j) == "." then j = src:match("^%d*()", j + 1) end
        j = src:match("^[eE][+-]?%d+()", j) or j
      end
      i = j > i and j or i + 1
      push("number", start)
    elseif c:find("[%a_]") then
      i = src:match("^[%w_]*()", i)
      push(KEYWORDS[src:sub(start, i - 1)] and "kw" or "name", start)
    else
      local op = c
      for _, o in ipairs(OPS3) do
        if src:sub(i, i + #o - 1) == o then op = o; break end
      end
      i = i + #op
      push("op", start)
    end
  end
  return toks
end
M.tokenize = tokenize

local function sig(t) return t.t ~= "ws" and t.t ~= "comment" end
local function isOp(t, v) return t ~= nil and t.t == "op" and t.v == v end
local function isKw(t, v) return t ~= nil and t.t == "kw" and t.v == v end
local function prevSig(toks, i) for k = i - 1, 1, -1 do if sig(toks[k]) then return toks[k], k end end return nil end
local function nextSigIndex(toks, i) for k = i, #toks do if sig(toks[k]) then return k end end return #toks + 1 end
local function raw(toks, a, b) local t = {} for k = a, b do t[#t + 1] = toks[k].v end return concat(t) end
local function slice(t, a, b) local r = {} for k = a, b do r[#r + 1] = t[k] end return r end
local function append(dst, src) for _, x in ipairs(src) do dst[#dst + 1] = x end end
local OPEN, CLOSE = set{"(", "[", "{"}, set{")", "]", "}"}

local function matchClose(toks, j)        -- toks[j] is an opening bracket
  local depth = 0
  for k = j, #toks do
    local t = toks[k]
    if t.t == "op" then
      if OPEN[t.v] then depth = depth + 1
      elseif CLOSE[t.v] then depth = depth - 1; if depth == 0 then return k end end
    end
  end
  return nil
end
local function blockDelta(t)
  if t.t ~= "kw" then return 0 end
  local v = t.v
  if v == "function" or v == "if" or v == "do" or v == "repeat" then return 1 end
  if v == "end" or v == "until" then return -1 end
  return 0
end
local function splitArgs(toks, a, b)      -- top-level commas in toks[a..b]
  local args, cur, depth, block = {}, {}, 0, 0
  for k = a, b do
    local t = toks[k]
    local comma = false
    if t.t == "op" then
      if OPEN[t.v] then depth = depth + 1
      elseif CLOSE[t.v] then depth = depth - 1
      elseif t.v == "," and depth == 0 and block == 0 then comma = true end
    end
    if comma then args[#args + 1] = cur; cur = {}
    else block = block + blockDelta(t); cur[#cur + 1] = t end
  end
  args[#args + 1] = cur
  return args
end

------------------------------------------------------------------------------------------------- rewrite
-- Items of a token list: {tok}, {group, open, inner, close} (a bracket pair), {opaque} (a function literal).
local function toItems(toks)
  local items, k, n = {}, 1, #toks
  while k <= n do
    local t = toks[k]
    if t.t == "op" and OPEN[t.v] then
      local c = matchClose(toks, k)
      if not c then items[#items + 1] = {tok = t}; k = k + 1
      else items[#items + 1] = {group = true, open = t, inner = slice(toks, k + 1, c - 1), close = toks[c]}; k = c + 1 end
    elseif isKw(t, "function") then
      local depth, e = 0, k
      while e <= n do
        depth = depth + blockDelta(toks[e])
        if depth == 0 and e > k then break end
        e = e + 1
      end
      items[#items + 1] = {opaque = slice(toks, k, mmin(e, n))}
      k = e + 1
    else items[#items + 1] = {tok = t}; k = k + 1 end
  end
  return items
end
local chainify
local function itemsToks(items)
  local out = {}
  for _, it in ipairs(items) do
    if it.group then out[#out + 1] = it.open; append(out, chainify(it.inner)); out[#out + 1] = it.close
    elseif it.opaque then append(out, it.opaque)
    else out[#out + 1] = it.tok end
  end
  return out
end
local function flatCopy(toks)             -- a one-line copy of an operand: comments dropped, newlines spaces
  local out = {}
  for _, t in ipairs(toks) do
    if t.t == "ws" then out[#out + 1] = {t = "ws", v = (t.v:gsub("\n", " ")), line = t.line}
    elseif t.t ~= "comment" then out[#out + 1] = t end
  end
  return out
end
local function trimmed(toks)              -- lead ws, the significant middle, trail ws
  local a, b = 1, #toks
  while a <= b and not sig(toks[a]) do a = a + 1 end
  while b >= a and not sig(toks[b]) do b = b - 1 end
  return slice(toks, 1, a - 1), slice(toks, a, b), slice(toks, b + 1, #toks)
end
local function newlinesOf(toks)           -- only what keeps the line count
  local out = {}
  for _, t in ipairs(toks) do if t.v:find("\n", 1, true) then out[#out + 1] = t end end
  return out
end
local function simpleOperand(toks)        -- a name, a.b.c, a number or a string: evaluating it twice is safe
  local n, expectName = 0, true
  for _, t in ipairs(toks) do
    if sig(t) then
      n = n + 1
      if expectName then
        if t.t == "name" then expectName = false
        elseif n == 1 and (t.t == "number" or t.t == "string") then expectName = false
        else return false end
      elseif isOp(t, ".") then expectName = true
      else return false end
    end
  end
  return n > 0 and not expectName
end
-- ==/~= on these names goes through a helper (the file's header says why)
local EQ_HELPER = {beat = "__near", duration = "__near", dynamic = "__dyneq", part = "__parteq"}
local function tk(t, v) return {t = t, v = v} end

function chainify(toks)
  local items = toItems(toks)
  local out, clause = {}, {}
  local function flush()
    local ops, eqs, eqAt = {}, 0, nil
    for idx, it in ipairs(clause) do
      local t = it.tok
      if t and t.t == "op" then
        if REL[t.v] then ops[#ops + 1] = idx
        elseif t.v == "==" or t.v == "~=" then eqs = eqs + 1; eqAt = idx end
      end
    end
    if #ops >= 2 and eqs == 0 then
      local parts, prev = {}, 1
      for _, o in ipairs(ops) do parts[#parts + 1] = itemsToks(slice(clause, prev, o - 1)); prev = o + 1 end
      parts[#parts + 1] = itemsToks(slice(clause, prev, #clause))
      local lead, first, firstTrail = trimmed(parts[1])
      parts[1] = first; append(parts[1], firstTrail)
      local lastLead, lastMid, trail = trimmed(parts[#parts])
      parts[#parts] = lastLead; append(parts[#parts], lastMid)
      append(out, lead)
      local simple = true
      for p = 2, #parts - 1 do if not simpleOperand(parts[p]) then simple = false end end
      if simple then
        out[#out + 1] = tk("op", "(")
        for p = 1, #ops do
          if p > 1 then
            local _, copy, copyTrail = trimmed(flatCopy(parts[p]))
            append(copy, copyTrail)
            if out[#out].t ~= "ws" then out[#out + 1] = tk("ws", " ") end
            out[#out + 1] = tk("kw", "and"); out[#out + 1] = tk("ws", " ")
            append(out, copy)
          else append(out, parts[1]) end
          out[#out + 1] = clause[ops[p]].tok
          append(out, parts[p + 1])
        end
        out[#out + 1] = tk("op", ")")
      else                                  -- __chain(a, "<", b, "<=", c): every operand once, in order
        out[#out + 1] = tk("name", "__chain"); out[#out + 1] = tk("op", "(")
        for p = 1, #parts do
          if p > 1 then
            out[#out + 1] = tk("op", ","); out[#out + 1] = tk("ws", " ")
            out[#out + 1] = tk("string", '"' .. clause[ops[p - 1]].tok.v .. '"'); out[#out + 1] = tk("op", ","); out[#out + 1] = tk("ws", " ")
          end
          local pl, pm, pt = trimmed(parts[p])
          append(out, newlinesOf(pl)); append(out, pm); append(out, newlinesOf(pt))
        end
        out[#out + 1] = tk("op", ")")
      end
      append(out, trail)
    elseif eqs == 1 and #ops == 0 then
      local lhs, rhs = itemsToks(slice(clause, 1, eqAt - 1)), itemsToks(slice(clause, eqAt + 1, #clause))
      local lead, l, lTrail = trimmed(lhs)
      local rLead, r, trail = trimmed(rhs)
      local helper
      for _, side in ipairs{l, r} do
        if #side == 1 and side[1].t == "name" and EQ_HELPER[side[1].v] then helper = helper or EQ_HELPER[side[1].v] end
      end
      -- (`not x == y` is (not x) == y in Lua: leave such a clause alone)
      if helper and #l > 0 and #r > 0 and not isKw(l[1], "not") then
        append(out, lead)
        if clause[eqAt].tok.v == "~=" then out[#out + 1] = tk("kw", "not"); out[#out + 1] = tk("ws", " ") end
        out[#out + 1] = tk("name", helper); out[#out + 1] = tk("op", "(")
        append(out, l); append(out, newlinesOf(lTrail))
        out[#out + 1] = tk("op", ","); out[#out + 1] = tk("ws", " "); append(out, newlinesOf(rLead))
        append(out, r)
        out[#out + 1] = tk("op", ")")
        append(out, trail)
      else append(out, itemsToks(clause)) end
    else append(out, itemsToks(clause)) end
    clause = {}
  end
  for _, it in ipairs(items) do
    local t = it.tok
    if t and ((t.t == "kw" and (t.v == "and" or t.v == "or")) or (t.t == "op" and (t.v == "," or t.v == ";"))) then
      flush(); out[#out + 1] = t
    else clause[#clause + 1] = it end
  end
  flush()
  return out
end

local function safeSlur(toks)             -- slur.x -> __slur.x (not a.slur.x, not slur alone)
  local out = {}
  for i, t in ipairs(toks) do
    if t.t == "name" and t.v == "slur" then
      local p = prevSig(toks, i)
      local nx = toks[nextSigIndex(toks, i + 1)]
      if isOp(nx, ".") and not (isOp(p, ".") or isOp(p, ":")) then t = {t = "name", v = "__slur", line = t.line} end
    end
    out[#out + 1] = t
  end
  return out
end
local function mentions(toks, name)       -- the bare name (not a field) appears
  for i, t in ipairs(toks) do
    if t.t == "name" and t.v == name then
      local p = prevSig(toks, i)
      if not (isOp(p, ".") or isOp(p, ":")) then return true end
    end
  end
  return false
end

local rewriteToks
-- calls: when given, collects {name, index, toks, line} of every condition argument (error diagnosis)
local function wrapArg(arg, plain, qname, index, calls)
  local lead, inner, trail = trimmed(arg)
  if #inner == 0 then return raw(arg, 1, #arg) end
  if isKw(inner[1], "function") then return raw(arg, 1, #arg) end
  if plain then return raw(lead, 1, #lead) .. rewriteToks(inner, calls) .. raw(trail, 1, #trail) end
  local depth, block = 0, 0
  for _, t in ipairs(inner) do
    if t.t == "op" then
      if OPEN[t.v] then depth = depth + 1 elseif CLOSE[t.v] then depth = depth - 1
      elseif t.v == "=" and depth == 0 and block == 0 then
        fail(t.line, sformat("argument %d of %s: use == to compare (a single = assigns)", index, qname))
      end
    end
    block = block + blockDelta(t)
  end
  if calls then calls[#calls + 1] = {name = qname, index = index, toks = inner, line = inner[1].line} end
  local body = rewriteToks(chainify(safeSlur(inner)), calls)
  local open, close = "function(_ENV) return ", " end"
  if mentions(inner, "grace") then open, close = "__grace(function(_ENV) return ", " end)" end
  return raw(lead, 1, #lead) .. open .. body .. close .. raw(trail, 1, #trail)
end
function rewriteToks(toks, calls)
  local out, i, n = {}, 1, #toks
  while i <= n do
    local t = toks[i]
    local done = false
    if t.t == "name" and QUERY[t.v] then
      local p = prevSig(toks, i)
      local isMember = isOp(p, ".") or isOp(p, ":")
      local isDef = isKw(p, "function")
      local j = nextSigIndex(toks, i + 1)
      if not isMember and not isDef and isOp(toks[j], "(") then
        local close = matchClose(toks, j)
        if close then
          local args = splitArgs(toks, j + 1, close - 1)
          out[#out + 1] = t.v .. raw(toks, i + 1, j)
          for k, arg in ipairs(args) do
            out[#out + 1] = (k > 1 and "," or "") .. wrapArg(arg, t.v == "next" and k == 1, t.v, k, calls)
          end
          out[#out + 1] = ")"
          i = close + 1
          done = true
        end
      end
    end
    if not done then out[#out + 1] = t.v; i = i + 1 end
  end
  return concat(out)
end
local function rewrite(src, calls) return rewriteToks(tokenize(src), calls) end
local function posError(e)                -- {line, msg} -> "line N: msg"
  if type(e) == "table" and e.msg then return (e.line and ("line " .. e.line .. ": ") or "") .. e.msg, e.line end
  return tostring(e), nil
end
function M.rewrite(src)
  local ok, r = pcall(rewrite, src)
  if not ok then error((posError(r)), 0) end
  return r
end

------------------------------------------------------------------------------------------------- vocabulary
local MARK_NAMES = {"staccato", "staccatissimo", "tenuto", "accent", "marcato", "portato", "fermata", "upbow", "downbow"}
local DURATIONS = {whole = 4, half = 2, quarter = 1, eighth = 0.5, sixteenth = 0.25, thirtysecond = 0.125}
local DUR_NAMES = {"whole", "half", "quarter", "eighth", "sixteenth", "thirtysecond"}
local DYN_NAMES = {"pppppp", "ppppp", "pppp", "ppp", "pp", "p", "mp", "mf", "f", "ff", "fff", "ffff", "fffff", "ffffff"}
local PROPS = {"part", "partname", "measure", "index", "beat", "tick", "duration", "pitch", "sounding", "name",
               "degree", "octave", "voice", "staff", "slur", "slurs", "tied", "grace", "dynamic", "chord", "top",
               "bottom", "has"}
local PROPSET = set(PROPS)
local FIELDS = set{"partname", "measure", "index", "beat", "tick", "duration", "pitch", "sounding", "name", "degree",
                   "octave", "voice", "staff", "chord", "top", "bottom"}
local BOOL_FIELDS = set{"tied", "grace", "chord", "top", "bottom"}
local SLUR_FIELDS = {"index", "length", "first", "last"}
local SEL_METHODS = {"add", "remove", "text", "count", "each"}

-- Sections come from MuseScore's instrument ids, families and groups (share/instruments/instruments.xml), never
-- from the part's name, which may be in any language.
local GROUP_SECTIONS = {
  woodwinds = {"winds", "woodwinds"}, brass = {"brass"}, strings = {"strings"},
  ["pitched-percussion"] = {"percussion"}, ["unpitched-percussion"] = {"percussion"},
  ["marching-percussion"] = {"percussion"}, ["body-percussion"] = {"percussion"},
  vocals = {"voices"}, keyboards = {"keyboards"}, ["plucked-strings"] = {"plucked"},
}
local INSTRUMENT_SECTIONS = {violin = "violins", viola = "violas", violoncello = "celli", contrabass = "basses"}
local INSTRUMENT_ALIAS = {violoncello = "cello", contrabass = "bass"}
local STATIC_SECTIONS = {"strings", "winds", "woodwinds", "brass", "percussion", "keyboards", "voices", "plucked",
                         "violins", "violas", "celli", "basses", "flutes", "oboes", "clarinets", "bassoons", "horns",
                         "trumpets", "trombones", "tubas", "timpani", "harps"}

local function levenshtein(a, b)
  local la, lb = #a, #b
  local prev = {}
  for j = 0, lb do prev[j] = j end
  for i = 1, la do
    local cur = {[0] = i}
    for j = 1, lb do
      local cost = a:sub(i, i) == b:sub(j, j) and 0 or 1
      cur[j] = mmin(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost)
    end
    prev = cur
  end
  return prev[lb]
end
local function closest(word, lists)
  local best, bestd = nil, huge
  local lw = word:lower()
  for _, list in ipairs(lists) do
    for _, w in ipairs(list) do
      if type(w) == "string" and w:sub(1, 2) ~= "__" then
        local d = levenshtein(lw, w:lower())
        if d < bestd then best, bestd = w, d end
      end
    end
  end
  if best and bestd <= mmin(2, #word // 3 + 1) and bestd < #word and best ~= "_G" then return best end
  return nil
end

local function fmtNum(x)
  if type(x) ~= "number" then return tostring(x) end
  if x == floor(x) and abs(x) < 1e15 then return sformat("%d", x) end
  return (sformat("%.4f", x):gsub("0+$", ""))
end

------------------------------------------------------------------------------------------------- marks, dynamics
local LIB = {}                            -- what every script sees besides the standard library (read-only)
local MARKS, MARK_OF = {}, setmetatable({}, {__mode = "k"})
local markMT = {__tostring = function(m) return MARK_OF[m] end, __metatable = false}
local function defineMarks(names)
  for _, n in ipairs(MARK_NAMES) do LIB[n] = nil end
  MARK_NAMES, MARKS = {}, {}
  for _, n in ipairs(names) do
    local m = setmetatable({}, markMT)
    MARK_OF[m] = n; MARKS[n] = m; LIB[n] = m; MARK_NAMES[#MARK_NAMES + 1] = n
  end
end
defineMarks(MARK_NAMES)
local function markName(m)
  if MARK_OF[m] then return MARK_OF[m] end
  if type(m) == "string" and MARKS[m] then return m end
  return nil
end
for k, v in pairs(DURATIONS) do LIB[k] = v end

local DYN, DYN_RANK = {}, setmetatable({}, {__mode = "k"})
local function toDyn(v, level)
  if DYN_RANK[v] then return v end
  if type(v) == "string" then
    if DYN[v] then return DYN[v] end
    local s = closest(v, {DYN_NAMES})
    error(sformat("'%s' isn't a dynamic%s", v, s and (": did you mean " .. s .. "?") or " (pp, p, mp, mf, f, ff …)"), level or 3)
  end
  if v == nil then error("this note has no dynamic in force: test `dynamic and …` first", level or 3) end
  error(sformat("a dynamic can only be compared with a dynamic such as mf, not with a %s", type(v)), level or 3)
end
local dynMT = {
  __tostring = function(d) return DYN_NAMES[DYN_RANK[d]] end,
  __lt = function(a, b) return DYN_RANK[toDyn(a)] < DYN_RANK[toDyn(b)] end,
  __le = function(a, b) return DYN_RANK[toDyn(a)] <= DYN_RANK[toDyn(b)] end,
  __metatable = false,
}
for i, n in ipairs(DYN_NAMES) do
  local d = setmetatable({}, dynMT)
  DYN[n] = d; DYN_RANK[d] = i; LIB[n] = d
end

------------------------------------------------------------------------------------------------- score state
local HOST = {}
local GEN = 0                             -- the score generation: selections of an older one are stale
local SIGNATURE
local PARTS, NOTES, NOTE_BY_ID, CHORDS = {}, {}, {}, {}
local SECTIONS, SECTIONSET = {}, {}       -- every section / part identifier of the vocabulary
local SHADOW                              -- a preview run's marks: chord -> set
local ENV, ENV_STD                        -- the running script's globals and its copy of the standard library
local CUR                                 -- the note a condition is evaluated on
local PREVIEW = false

local function ident(s)                   -- an instrument id as a Lua name: "bb-clarinet" -> clarinet
  s = s:lower():gsub("^[a-g]b?%-", ""):gsub("[^%a%d]", "")
  if s:find("^%a") then return s end
  return nil
end
local function addSection(part, name)
  if not name or part.sections[name] then return end
  part.sections[name] = true
  if not SECTIONSET[name] then SECTIONSET[name] = true; SECTIONS[#SECTIONS + 1] = name end
end
local HIDDEN = {}                      -- numbered names (violas1) of a section with one part: accepted, never offered
local function buildParts(list)
  PARTS, SECTIONS, SECTIONSET, HIDDEN = {}, {}, {}, {}
  for _, s in ipairs(STATIC_SECTIONS) do SECTIONSET[s] = true; SECTIONS[#SECTIONS + 1] = s end
  local byAlias = {}
  for i, p in ipairs(list or {}) do
    if type(p) == "string" then p = {name = p} end
    local part = {name = p.name or ("Part " .. i), order = i, instrument = p.instrument, family = p.family,
                  group = p.group, sections = {}}
    PARTS[i] = part
    for _, s in ipairs(GROUP_SECTIONS[p.group or ""] or {}) do addSection(part, s) end
    if p.family and p.family:find("^%a+$") then addSection(part, p.family) end
    if p.instrument then
      addSection(part, INSTRUMENT_SECTIONS[p.instrument])
      local alias = INSTRUMENT_ALIAS[p.instrument] or ident(p.instrument)
      if alias then
        byAlias[alias] = byAlias[alias] or {}
        local l = byAlias[alias]; l[#l + 1] = part
      end
    end
    for _, s in ipairs(p.sections or {}) do addSection(part, s) end
  end
  for alias, l in pairs(byAlias) do      -- violin1, violin2 …: parts of one instrument in score order
    for k, part in ipairs(l) do
      addSection(part, alias .. k)
      if #l < 2 then HIDDEN[alias .. k] = true end
      if not (PROPSET[alias] or QUERY[alias] or MARKS[alias] or DURATIONS[alias] or DYN[alias]) then addSection(part, alias) end
    end
  end
end

local slurMT
local EMPTY_SLUR
local IS_SLUR = setmetatable({}, {__mode = "k"})
local function buildNote(note, d)
  note.d = d
  note.part = PARTS[d.part] or note.part
  if type(d.part) == "string" then
    for _, p in ipairs(PARTS) do if p.name == d.part then note.part = p end end
  end
  local slurs = d.slurs or (d.slur and {d.slur}) or {}
  note.slurs = nil; note.slur = nil
  if #slurs > 0 then
    local list, anyFirst, anyLast = {}, false, false
    for _, s in ipairs(slurs) do
      if s.index == 1 then anyFirst = true end
      if s.index == s.length then anyLast = true end
    end
    for i, s in ipairs(slurs) do
      list[i] = setmetatable({index = s.index, length = s.length, first = s.index == 1, last = s.index == s.length}, slurMT)
    end
    -- `slur`: the last one given (the host lists the innermost / latest-starting last); first / last over all
    local top = slurs[#slurs]
    note.slur = setmetatable({index = top.index, length = top.length, first = anyFirst, last = anyLast}, slurMT)
    IS_SLUR[note.slur] = true
    note.slurs = list
  end
  local chord = CHORDS[d.chordId]
  if not chord then chord = {id = d.chordId, marks = {}}; CHORDS[d.chordId] = chord end
  note.chord = chord
  for _, m in ipairs(d.marks or {}) do chord.marks[m] = true end
end

-- data: {parts = {name | {name, instrument, family, group, sections}}, notes = {…}, marks = {names}, signature}.
-- A note: id, part (index into parts, or its name), measure (the printed number), index (the measure counted from
-- 1), beat, tick, duration (quarters; a tied note's whole length), pitch / name / octave / degree (written),
-- sounding, voice, staff, tied, grace, chordId, chord, top, bottom, slurs = {{index, length}…} (or slur),
-- marks = {names} (its chord's), dynamic (a name of DYN_NAMES or nil). Tie continuations and rests aren't listed.
-- The same signature as last time means "the same notes": marks and dynamics are refreshed, selections stay valid.
function M.setScore(data)
  if data.marks then defineMarks(data.marks) end
  local same = data.signature ~= nil and data.signature == SIGNATURE and #data.notes == #NOTES
  if same then
    for _, d in ipairs(data.notes) do if not NOTE_BY_ID[d.id] then same = false end end
  end
  SIGNATURE = data.signature
  if same then
    for _, c in pairs(CHORDS) do c.marks = {} end
    for _, d in ipairs(data.notes) do buildNote(NOTE_BY_ID[d.id], d) end
    return GEN
  end
  GEN = GEN + 1
  buildParts(data.parts)
  NOTES, NOTE_BY_ID, CHORDS = {}, {}, {}
  for i, d in ipairs(data.notes) do
    local note = {id = d.id, gen = GEN}
    buildNote(note, d)
    NOTES[i] = note; NOTE_BY_ID[d.id] = note
  end
  sort(NOTES, function(a, b)
    if a.d.tick ~= b.d.tick then return a.d.tick < b.d.tick end
    local pa, pb = a.part and a.part.order or 0, b.part and b.part.order or 0
    if pa ~= pb then return pa < pb end
    if a.d.pitch ~= b.d.pitch then return a.d.pitch > b.d.pitch end
    return a.id < b.id
  end)
  return GEN
end
function M.setHost(host) HOST = host or {} end
function M.generation() return GEN end

local function marksOf(chord) return (SHADOW and SHADOW[chord]) or chord.marks end
local function noteText(note)
  local d = note.d
  return sformat("%s %s, bar %s beat %s", note.part and note.part.name or "?", tostring(d.name), fmtNum(d.measure), fmtNum(d.beat))
end

------------------------------------------------------------------------------------------------- note properties
slurMT = {__index = function(_, k)
  local s = closest(tostring(k), {SLUR_FIELDS})
  error(sformat("slur has no field '%s'%s", tostring(k), s and (": did you mean slur." .. s .. "?") or " (it has index, length, first, last)"), 2)
end, __metatable = false}
EMPTY_SLUR = setmetatable({first = false, last = false}, {__index = function(_, k)
  if k == "index" or k == "length" then return nil end
  return slurMT.__index(_, k)
end, __newindex = function() error("a slur can't be changed", 2) end, __metatable = false})

local function globalNames()
  local t = {}
  for k in rawnext, LIB do if type(k) == "string" then t[#t + 1] = k end end
  if ENV then for k in rawnext, ENV do if type(k) == "string" then t[#t + 1] = k end end end
  if ENV_STD then for k in rawnext, ENV_STD do if type(k) == "string" then t[#t + 1] = k end end end
  return t
end
local function unknownInCondition(k)
  local s = closest(k, {PROPS, SECTIONS, MARK_NAMES, DUR_NAMES, DYN_NAMES, QUERY_NAMES, globalNames()})
  if s then return sformat("'%s' isn't a note property: did you mean %s?", k, s) end
  return sformat("'%s' isn't a note property, mark or variable", k)
end
local function checkNote(note)
  if note.gen ~= GEN then error("the score changed since this note was found: run the query again", 3) end
end

local GETTERS = {
  part = function(note)                   -- part("Violin I", …): the note's part has one of these names
    return function(...)
      local n = select("#", ...)
      if n == 0 then error('part() needs a part name, for example part("Violin I")', 2) end
      local found = false
      for i = 1, n do
        local name = select(i, ...)
        if type(name) ~= "string" then error('part() needs a part name, for example part("Violin I")', 2) end
        local known = false
        for _, p in ipairs(PARTS) do if p.name == name then known = true end end
        if not known then
          local names = {}
          for _, p in ipairs(PARTS) do names[#names + 1] = p.name end
          local s = closest(name, {names})
          error(sformat("the score has no part '%s'%s", name, s and (': did you mean "' .. s .. '"?') or ""), 2)
        end
        if note.part and note.part.name == name then found = true end
      end
      return found
    end
  end,
  partname = function(note) return note.part and note.part.name or "" end,
  has = function(note)
    return function(...)
      local n = select("#", ...)
      if n == 0 then error("has() needs a mark, for example has(staccato)", 2) end
      for i = 1, n do
        local name = markName((select(i, ...)))
        if not name then error("has() needs a mark, for example has(staccato)", 2) end
        if not marksOf(note.chord)[name] then return false end
      end
      return true
    end
  end,
  slur = function(note) return note.slur end,
  slurs = function(note) return note.slurs or {} end,
  __slur = function(note) return note.slur or EMPTY_SLUR end,
  dynamic = function(note) return note.d.dynamic and DYN[note.d.dynamic] or nil end,
}
local function envLookup(k)
  local v = ENV and rawget(ENV, k)
  if v ~= nil then return v end
  v = ENV_STD and ENV_STD[k]
  if v ~= nil then return v end
  return LIB[k]
end
local function noteProp(note, k)
  local g = GETTERS[k]
  if g then return g(note) end
  if FIELDS[k] then return note.d[k] end
  if BOOL_FIELDS[k] then return note.d[k] and true or false end
  if SECTIONSET[k] then return note.part ~= nil and note.part.sections[k] == true end
  local v = envLookup(k)
  if v ~= nil then return v end
  error(unknownInCondition(tostring(k)), 3)
end
-- the one environment of every rewritten condition
local COND_ENV = setmetatable({}, {
  __index = function(_, k) local v = noteProp(CUR, k); return v end,   -- (no tail call: the error level)
  __newindex = function(_, k) error(sformat("can't set '%s' inside a condition", tostring(k)), 2) end,
  __tostring = function() return CUR and noteText(CUR) or "note" end,
  __metatable = false,
})
-- a note a script may keep (each(), a function used as a condition)
local HANDLE_NOTE = setmetatable({}, {__mode = "k"})
local handleMT = {
  __index = function(h, k) local note = HANDLE_NOTE[h]; checkNote(note); local v = noteProp(note, k); return v end,
  __newindex = function(_, k) error(sformat("can't set '%s' on a note", tostring(k)), 2) end,
  __tostring = function(h) return noteText(HANDLE_NOTE[h]) end,
  __metatable = false,
}
local function handle(note)
  if not note.handle then
    note.handle = setmetatable({}, handleMT)
    HANDLE_NOTE[note.handle] = note
  end
  return note.handle
end

------------------------------------------------------------------------------------------------- results
local ARRAY = {}
local function arr(t) return setmetatable(t or {}, ARRAY) end
local LOG, MATCHED, MATCHED_SET, OPS = arr(), arr(), {}, arr()
local function log(kind, text, line)
  LOG[#LOG + 1] = {kind = kind, text = text, line = line}
  if HOST.log and not PREVIEW then HOST.log(kind, text, line) end
end
local function scriptLine(level)
  if not debug then return nil end
  for l = level, level + 8 do
    local info = debug.getinfo(l, "Sl")
    if not info then break end
    if info.source == "=script" then return info.currentline end
  end
  return nil
end

------------------------------------------------------------------------------------------------- selections
local SEL_NOTES = setmetatable({}, {__mode = "k"})
local SEL_SET = setmetatable({}, {__mode = "k"})
local function inSel(sel, note)
  local s = SEL_SET[sel]
  if not s then
    s = {}
    for _, m in ipairs(SEL_NOTES[sel]) do s[m] = true end
    SEL_SET[sel] = s
  end
  return s[note] == true
end
local function isSel(v) return SEL_NOTES[v] ~= nil end
local SelMT = {__metatable = false}

local function describe(list)
  local counts, order = {}, {}
  for _, n in ipairs(list) do
    local p = n.part
    if p then
      if not counts[p] then counts[p] = 0; order[#order + 1] = p end
      counts[p] = counts[p] + 1
    end
  end
  sort(order, function(a, b) return a.order < b.order end)
  local parts = {}
  for _, p in ipairs(order) do parts[#parts + 1] = p.name .. " " .. counts[p] end
  local s = #list .. (#list == 1 and " note" or " notes")
  if #parts > 0 then s = s .. " (" .. concat(parts, ", ") .. ")" end
  return s
end

-- marks belong to chords: one change per chord, none when the chord already is as asked
local function record(op, n, value)
  local chord = n.chord
  local marks = marksOf(chord)
  if op == "add" and marks[value] then return end
  if op == "remove" and not marks[value] then return end
  if op ~= "text" then
    if PREVIEW then
      if not SHADOW[chord] then
        local copy = {}
        for k, v in pairs(chord.marks) do copy[k] = v end
        SHADOW[chord] = copy
        marks = copy
      end
    end
    marks[value] = (op == "add") or nil
  end
  OPS[#OPS + 1] = {op = op, id = n.id, chord = chord.id, value = value}
  if not PREVIEW and HOST[op] then HOST[op](n.id, value) end
end

local function newSel(list)
  local sel, gen = {}, GEN
  local function args(...)
    if select("#", ...) > 0 and (...) == sel then return select(2, ...) end
    return ...
  end
  local function current()
    if gen ~= GEN then error("the score changed since this selection was made: run the query again", 3) end
  end
  local function marksOp(op, ...)
    current()
    local n = select("#", ...)
    local names = {}
    for i = 1, n do
      names[i] = markName((select(i, ...)))
      if not names[i] then error(sformat("%s() needs a mark, for example %s(staccato)", op, op), 3) end
    end
    if n == 0 then error(sformat("%s() needs a mark, for example %s(staccato)", op, op), 3) end
    for _, name in ipairs(names) do
      for _, note in ipairs(list) do record(op, note, name) end
    end
    return sel
  end
  sel.add = function(...) return marksOp("add", args(...)) end
  sel.remove = function(...) return marksOp("remove", args(...)) end
  sel.text = function(...)
    current()
    local s = args(...)
    if type(s) ~= "string" then error('text() needs a string, for example text("pizz.")', 2) end
    local seen = {}
    for _, n in ipairs(list) do
      if not seen[n.chord] then seen[n.chord] = true; record("text", n, s) end
    end
    return sel
  end
  sel.count = function() return #list end
  sel.each = function()
    current()
    local i = 0
    return function() i = i + 1; local n = list[i]; return n and handle(n) end
  end
  SEL_NOTES[sel] = list
  return setmetatable(sel, SelMT)
end
SelMT.__len = function(s) return #SEL_NOTES[s] end
SelMT.__tostring = function(s) return describe(SEL_NOTES[s]) end
SelMT.__index = function(_, k)
  local s = closest(tostring(k), {SEL_METHODS})
  error(sformat("a selection has no method '%s'%s", tostring(k), s and (": did you mean " .. s .. "?") or " (it has add, remove, text, count, each)"), 2)
end

------------------------------------------------------------------------------------------------- queries
local function stripPos(msg)
  msg = tostring(msg)
  local line, rest = msg:match("^script:(%d+): (.*)$")
  if line then return tonumber(line), rest end
  line, rest = msg:match("^line (%d+): (.*)$")
  if line then return tonumber(line), rest, true end
  return nil, msg
end
local function passOn(e)                  -- the errors no pcall may swallow
  if e == KILL or e == MEMORY or (type(e) == "string" and e:find(MEMORY, 1, true)) then error(e, 0) end
end

local GRACE_FN = setmetatable({}, {__mode = "k"})
LIB.__grace = function(f) GRACE_FN[f] = true; return f end
local WRAPPED = setmetatable({}, {__mode = "k"})  -- function -> is it a rewritten condition (parameter _ENV)?
local function isWrapped(f)
  local w = WRAPPED[f]
  if w == nil then
    w = true
    if debug and debug.getlocal then w = debug.getlocal(f, 1) == "_ENV" end
    WRAPPED[f] = w
  end
  return w
end

local function describeValue(v)
  local t = type(v)
  if t == "number" then return sformat("a number (%s): compare it, for example measure == %s", fmtNum(v), fmtNum(v)) end
  if t == "string" then return sformat('text ("%s"): compare it, for example partname == "%s"', v, v) end
  if t == "table" then return "a table" end
  return "a " .. t
end

local function evalOne(qname, idx, c, note)
  local v, ok = c, true
  for _ = 1, 2 do
    if type(v) ~= "function" then break end
    local f = v
    CUR = note
    ok, v = pcall(f, isWrapped(f) and COND_ENV or handle(note))
    CUR = note
    if not ok then
      passOn(v)
      local line, rest, nested = stripPos(v)
      if nested then error(v, 0) end
      if rest:find("attempt to compare", 1, true) and rest:find("nil", 1, true) then
        rest = rest .. sformat(" (%s has no such value: test it first, for example `slur, slur.index > 1` or `dynamic and dynamic < mf`)", noteText(note))
      end
      if not line and debug then
        local info = debug.getinfo(f, "S")
        line = info and info.linedefined > 0 and info.linedefined or nil
      end
      error(sformat("%sargument %d of %s: %s", line and ("line " .. line .. ": ") or "", idx, qname, rest), 0)
    end
  end
  if v == nil or v == false then return false end
  if v == true then return true end
  if isSel(v) then return inSel(v, note) end
  if MARK_OF[v] then return marksOf(note.chord)[MARK_OF[v]] == true end
  if IS_SLUR[v] then return true end      -- `slur`: the note is under one
  if DYN_RANK[v] then return true end     -- `dynamic`: one is in force
  local line
  if type(c) == "function" and debug then
    local info = debug.getinfo(c, "S")
    line = info and info.linedefined > 0 and info.linedefined or nil
  end
  error(sformat("%sargument %d of %s: a condition must be true or false, but this is %s", line and ("line " .. line .. ": ") or "",
                idx, qname, describeValue(v)), 0)
end

local function wantsGrace(conds)
  for i = 1, conds.n do if GRACE_FN[conds[i]] then return true end end
  return false
end
local function matches(qname, conds, offset, note)
  for i = 1, conds.n do
    if not evalOne(qname, i + offset, conds[i], note) then return false end
  end
  return true
end
local function collect(qname, conds, offset, fromMeasure, firstOnly, lastOnly)
  local out, grace = {}, wantsGrace(conds)
  local saved = CUR
  local a, b, step = 1, #NOTES, 1
  if lastOnly then a, b, step = #NOTES, 1, -1 end
  for i = a, b, step do
    local n = NOTES[i]
    if (grace or not n.d.grace) and (not fromMeasure or n.d.measure >= fromMeasure) and matches(qname, conds, offset, n) then
      out[#out + 1] = n
      if firstOnly or lastOnly then break end
    end
  end
  CUR = saved
  return out
end
local function noteResult(qname, list, extra)
  for _, n in ipairs(list) do
    if not MATCHED_SET[n.id] then MATCHED_SET[n.id] = true; MATCHED[#MATCHED + 1] = n.id end
  end
  log("query", qname .. " \u{2192} " .. (extra or describe(list)), scriptLine(3))
end
local function one(list) return #list > 0 and (describe(list) .. ": " .. noteText(list[1])) or nil end

function LIB.findall(...)
  local list = collect("findall", tpack(...), 0)
  noteResult("findall", list)
  return newSel(list)
end
function LIB.first(...)
  local list = collect("first", tpack(...), 0, nil, true)
  noteResult("first", list, one(list))
  return newSel(list)
end
function LIB.last(...)
  local list = collect("last", tpack(...), 0, nil, false, true)
  noteResult("last", list, one(list))
  return newSel(list)
end
LIB.next = function(m, ...)
  if type(m) == "table" and not isSel(m) and not HANDLE_NOTE[m] then  -- Lua's own next(t, k)
    local k = ...
    if type(k) == "function" and isWrapped(k) then k = k(ENV) end
    return rawnext(m, k)
  end
  if type(m) ~= "number" then error("next(m, ...) needs a bar number first, for example next(5, violin1)", 2) end
  local list = collect("next", tpack(...), 1, m, true)
  noteResult("next", list, one(list))
  return newSel(list)
end
function LIB.count(...)
  local list = collect("count", tpack(...), 0)
  noteResult("count", list, tostring(#list))
  return #list
end
function LIB.has() error("has() works inside a query, for example findall(violin1, has(staccato))", 2) end
function LIB.part() error('part() works inside a query, for example findall(part("Violin I"))', 2) end

local RELFN = {
  ["<"] = function(a, b) return a < b end, ["<="] = function(a, b) return a <= b end,
  [">"] = function(a, b) return a > b end, [">="] = function(a, b) return a >= b end,
}
function LIB.__chain(...)
  local n = select("#", ...)
  local a = ...
  for i = 2, n, 2 do
    local b = select(i + 1, ...)
    if not RELFN[select(i, ...)](a, b) then return false end
    a = b
  end
  return true
end
LIB.__pack = tpack
function LIB.__near(a, b)                 -- beat == 1 + 1/3
  if type(a) == "number" and type(b) == "number" then return abs(a - b) < 1e-6 end
  return a == b
end
function LIB.__dyneq(a, b)
  if a == nil or b == nil then return false end
  return toDyn(a) == toDyn(b)
end
function LIB.__parteq(a, b)
  if type(a) ~= "function" then a, b = b, a end
  if type(a) ~= "function" or type(b) ~= "string" then error('compare part with a name, for example part == "Violin I"', 2) end
  return a(b)
end

------------------------------------------------------------------------------------------------- JSON out
local function enc(v)
  local t = type(v)
  if t == "nil" then return "null" end
  if t == "boolean" then return v and "true" or "false" end
  if t == "number" then
    if v ~= v or v == huge or v == -huge then return "null" end
    if v == floor(v) and abs(v) < 1e15 then return sformat("%d", v) end
    return sformat("%.14g", v)
  end
  if t == "string" then
    return '"' .. v:gsub('[%c"\\]', function(c)
      local map = {['"'] = '\\"', ['\\'] = '\\\\', ['\n'] = '\\n', ['\r'] = '\\r', ['\t'] = '\\t'}
      return map[c] or sformat("\\u%04x", c:byte())
    end) .. '"'
  end
  if t == "table" then
    if getmetatable(v) == ARRAY then
      local parts = {}
      for i = 1, #v do parts[i] = enc(v[i]) end
      return "[" .. concat(parts, ",") .. "]"
    end
    local parts = {}
    for k, x in pairs(v) do parts[#parts + 1] = enc(tostring(k)) .. ":" .. enc(x) end
    return "{" .. concat(parts, ",") .. "}"
  end
  return enc(tostring(v))
end

------------------------------------------------------------------------------------------------- declarations
local function hideKind(name)
  if PROPSET[name] then return "note property" end
  if SECTIONSET[name] then return "section" end
  if MARKS[name] then return "mark" end
  if DURATIONS[name] then return "duration" end
  if DYN[name] and name ~= "p" and name ~= "f" then return "dynamic" end
  return nil
end
-- Locals, loop variables, parameters and assignments that would hide the vocabulary: errors {line, msg}
local function checkNames(toks)
  local S = {}
  for _, t in ipairs(toks) do if sig(t) then S[#S + 1] = t end end
  local function declared(t, what)
    if QUERY[t.v] then fail(t.line, sformat("'%s' is a query function: choose another name", t.v)) end
    local kind = hideKind(t.v)
    if kind then fail(t.line, sformat("%s '%s' hides the %s %s; rename it", what, t.v, kind, t.v)) end
  end
  local function nameList(i, what)        -- names separated by commas from S[i]; -> the index after
    while S[i] and S[i].t == "name" do
      declared(S[i], what)
      i = i + 1
      if isOp(S[i], "<") and S[i + 1] and isOp(S[i + 2], ">") then i = i + 3 end  -- <const>
      if isOp(S[i], ",") then i = i + 1 else break end
    end
    return i
  end
  local depth, handled = 0, {}
  local i = 1
  while i <= #S do
    local t = S[i]
    if t.t == "op" and OPEN[t.v] then depth = depth + 1
    elseif t.t == "op" and CLOSE[t.v] then depth = depth - 1
    elseif isKw(t, "local") then
      if isKw(S[i + 1], "function") then
        if S[i + 2] and S[i + 2].t == "name" then declared(S[i + 2], "local"); handled[i + 2] = true end
      else
        local e = nameList(i + 1, "local")
        for k = i + 1, e do handled[k] = true end
      end
    elseif isKw(t, "for") then
      local e = nameList(i + 1, "loop variable")
      for k = i + 1, e do handled[k] = true end
    elseif isKw(t, "function") then
      local j = i + 1
      if S[j] and S[j].t == "name" then
        local simple = not (isOp(S[j + 1], ".") or isOp(S[j + 1], ":"))
        if simple and not handled[j] then declared(S[j], "function") end
        handled[j] = true
        j = j + 1
        while (isOp(S[j], ".") or isOp(S[j], ":")) and S[j + 1] do j = j + 2 end
      end
      if isOp(S[j], "(") then
        j = j + 1
        while S[j] and not isOp(S[j], ")") do
          if S[j].t == "name" then declared(S[j], "parameter") end
          j = j + 1
        end
      end
    elseif t.t == "name" and depth == 0 and not handled[i] and isOp(S[i + 1], "=") then
      local p = S[i - 1]
      if not (isOp(p, ".") or isOp(p, ":")) then
        if QUERY[t.v] then fail(t.line, sformat("'%s' is a query function: choose another name", t.v)) end
        local kind = hideKind(t.v)
        if kind then fail(t.line, sformat("can't assign to '%s': it is a %s; use another name", t.v, kind)) end
      end
    end
    i = i + 1
  end
end

------------------------------------------------------------------------------------------------- run
local STEP_LIMIT = 20000000
local STD_FUNCTIONS = {"assert", "error", "ipairs", "pairs", "select", "tonumber", "tostring", "type", "setmetatable", "rawequal", "rawlen"}
local G = _G
local function copyLib(lib, without)
  local t = {}
  for k, v in pairs(lib or {}) do if not (without and without[k]) then t[k] = v end end
  return t
end
local function newStd()
  local s = {}
  for _, k in ipairs(STD_FUNCTIONS) do s[k] = G[k] end
  s._VERSION = G._VERSION
  s.string = copyLib(STD.string, {dump = true})
  s.table = copyLib(STD.table)
  s.math = copyLib(STD.math)
  s.utf8 = copyLib(STD.utf8)
  s.pcall = function(f, ...)             -- the limits pass through
    local r = tpack(pcall(f, ...))
    if not r[1] then passOn(r[2]) end
    return tunpack(r, 1, r.n)
  end
  s.getmetatable = function(v)            -- tables only: no way to the shared string metatable
    if type(v) ~= "table" then return nil end
    return getmetatable(v)
  end
  s.print = function(...)
    local t = {}
    for i = 1, select("#", ...) do
      local v = (select(i, ...))
      t[i] = type(v) == "number" and fmtNum(v) or tostring(v)
    end
    log("print", concat(t, "  "), scriptLine(2))
  end
  return s
end
local function isProp(k) return PROPSET[k] == true end
local ENV_MT = {__index = function(_, k)
  local v = ENV_STD[k]
  if v ~= nil then return v end
  v = LIB[k]
  if v ~= nil then return v end
  if type(k) == "string" and k:sub(1, 2) == "__" then return nil end
  if isProp(k) or SECTIONSET[k] then
    error(sformat("'%s' is a %s: use it inside findall(...), first(...), last(...), next(...) or count(...)", tostring(k),
                  isProp(k) and "note property" or "section"), 2)
  end
  local s = closest(tostring(k), {QUERY_NAMES, MARK_NAMES, DUR_NAMES, PROPS, SECTIONS, globalNames()})
  if s and s ~= k then error(sformat("'%s' isn't defined: did you mean %s?", tostring(k), s), 2) end
  error(sformat("'%s' isn't defined", tostring(k)), 2)
end, __metatable = false}
local function newEnv()
  local std = newStd()
  local env = setmetatable({}, ENV_MT)
  rawset(env, "_G", env)
  return env, std
end
local REPL_ENV, REPL_STD                  -- the console's persistent globals

local function cleanSyntax(msg)
  msg = msg:gsub("__grace%(function%(_ENV%) return ", ""):gsub("function%(_ENV%) return ", "")
  msg = msg:gsub("__slur", "slur"):gsub("__%a+%(", "(")
  return msg
end
-- A syntax error of the rewritten code, told on the script as written: each condition argument is tried alone
local function diagnose(calls, err)
  for _, c in ipairs(calls) do
    local text = raw(c.toks, 1, #c.toks)
    local fn, e = load("return " .. text, "=arg", "t", {})
    if not fn then
      local line, rest = tostring(e):match("^arg:(%d+): (.*)$")
      rest = rest or tostring(e)
      rest = rest:gsub("near <eof>", "at its end")
      return c.line + (tonumber(line) or 1) - 1, sformat("argument %d of %s: %s", c.index, c.name, rest)
    end
  end
  local line, rest = stripPos(err)
  return line, cleanSyntax(rest)
end

local function prepare(src)               -- -> code, calls | nil, message, line
  local calls = {}
  local ok, r = pcall(function()
    local toks = tokenize(src)
    checkNames(toks)
    return rewriteToks(toks, calls)
  end)
  if not ok then
    passOn(r)
    local msg, line = posError(r)
    return nil, msg, line
  end
  return r, calls
end

-- The script's syntax and declarations, without running it: nil, or message and line (the editor's underline)
function M.check(src)
  local code, calls, line = prepare(src)
  if not code then return calls, line end
  local fn, err = load(code, "=script", "t", {})
  if not fn then
    local l, rest = diagnose(calls, err)
    return (l and ("line " .. l .. ": ") or "") .. rest, l
  end
  return nil
end

-- opts: preview (shadow marks, no host callback, no persistent state), persist (the console: globals stay),
-- echo (the console: a line that is an expression prints its value), marks ({note id -> {names}}: the marks to
-- start from, the playground's), limit (instructions; 0: the host limits).
-- -> {log = {{kind, text, line}}, matched = {ids}, ops = {{op, id, chord, value}}, preview, error, errorLine, code}
function M.run(src, opts)
  opts = opts or {}
  PREVIEW = opts.preview and true or false
  SHADOW = PREVIEW and {} or nil
  LOG, MATCHED, MATCHED_SET, OPS = arr(), arr(), {}, arr()
  CUR = nil
  local result = {log = LOG, matched = MATCHED, ops = OPS, preview = PREVIEW, generation = GEN}
  if opts.marks then
    for _, c in pairs(CHORDS) do c.marks = {} end
    for _, n in ipairs(NOTES) do
      for _, x in ipairs(opts.marks[n.id] or opts.marks[tostring(n.id)] or {}) do n.chord.marks[x] = true end
    end
  end
  if opts.persist and not PREVIEW then
    if not REPL_ENV then REPL_ENV, REPL_STD = newEnv() end
    ENV, ENV_STD = REPL_ENV, REPL_STD
  elseif PREVIEW and opts.persist and REPL_ENV then  -- sees the console's variables, changes a copy
    ENV, ENV_STD = newEnv()
    for k, v in rawnext, REPL_ENV do if k ~= "_G" then rawset(ENV, k, v) end end
  else
    ENV, ENV_STD = newEnv()
  end
  local function finish(msg, line)
    result.error, result.errorLine = msg, line
    SHADOW = nil
    return result
  end
  local code, calls, line = prepare(src)
  if not code then return finish(calls, line) end
  result.code = code
  local fn, err
  -- (packed, so that the line's call isn't a tail call: its errors keep their line)
  if opts.echo then fn = load("return __pack(" .. code .. "\n)", "=script", "t", ENV) end
  local echo = fn ~= nil
  if not fn then fn, err = load(code, "=script", "t", ENV) end
  if not fn then
    local l, rest = diagnose(calls, err)
    return finish((l and ("line " .. l .. ": ") or "") .. rest, l)
  end
  local limit = opts.limit or STEP_LIMIT
  local hooked = debug and debug.sethook and limit > 0
  if hooked then debug.sethook(function() error(KILL, 0) end, "", limit) end
  local r = tpack(pcall(fn))
  if hooked then debug.sethook() end
  if not r[1] then
    local e = r[2]
    if e == KILL then return finish(tostring(KILL)) end
    if e == MEMORY or (type(e) == "string" and e:find(MEMORY, 1, true)) then
      return finish("the script used too much memory and was stopped")
    end
    if type(e) == "table" and e.msg then return finish(posError(e)) end
    local l, rest = stripPos(e)
    return finish((l and ("line " .. l .. ": ") or "") .. rest, l)
  end
  if echo then
    local queried = false
    for _, entry in ipairs(LOG) do if entry.kind == "query" then queried = true end end
    local t, v = {}, r[2]                 -- (a query has printed its own result; a selection is no news)
    for i = 1, v.n do
      if not isSel(v[i]) and not (queried and v.n == 1) then t[#t + 1] = type(v[i]) == "number" and fmtNum(v[i]) or tostring(v[i]) end
    end
    if #t > 0 then log("value", concat(t, "  "), nil) end
  end
  SHADOW = nil
  return result
end
function M.runJSON(src, opts) return enc(M.run(src, opts)) end
function M.resetConsole() REPL_ENV, REPL_STD = nil, nil end

------------------------------------------------------------------------------------------------- editor support
local DOC = {
  findall = "findall(condition, …): every note where all conditions hold",
  first = "first(condition, …): the earliest note that matches",
  last = "last(condition, …): the latest note that matches",
  next = "next(m, condition, …): the first match at or after bar m",
  count = "count(condition, …): how many notes match (a number)",
  part = 'part("Violin I"): the note is in the part with that name',
  partname = "the name of the note's part (text)",
  measure = "bar number as printed in the score",
  index = "the bar counted from 1 (pickups and unnumbered bars count)",
  beat = "beat in the bar, in beats of the time signature: 1, 1.5, 2 …",
  tick = "position from the start of the score (480 per quarter note)",
  duration = "length in quarter notes, ties included: duration == eighth",
  pitch = "written pitch as a MIDI number, 60 = middle C",
  sounding = "sounding pitch as a MIDI number",
  name = 'written note name, for example name == "Eb5"',
  degree = "scale degree in the key signature in force: 1 = tonic",
  octave = "octave number, C4 = middle C",
  voice = "voice 1-4",
  staff = "staff of the part, 1 = top",
  slur = "the slur over the note, or nil: slur.index, slur.length, slur.first, slur.last",
  slurs = "every slur over the note (a note may end one and start another)",
  tied = "true if the note is tied on (one note, its whole length)",
  grace = "true for a grace note (left out unless the query says grace)",
  dynamic = "dynamic in force, or nil: dynamic == p, dynamic >= mf",
  chord = "true if its chord has more than one note",
  top = "highest note of its chord",
  bottom = "lowest note of its chord",
  has = "has(mark): the note's chord has that mark",
  add = "add(mark, …): add the marks to every chord of the selection",
  remove = "remove(mark, …): remove the marks",
  text = 'text("pizz."): add staff text at every chord of the selection',
  each = "each(): loop over the notes: for n in sel.each() do … end",
  ["slur.index"] = "position under the slur, 1 = first note",
  ["slur.length"] = "number of notes under the slur",
  ["slur.first"] = "first note under a slur",
  ["slur.last"] = "last note under a slur",
}
DOC["sel.count"] = "count(): the number of notes in the selection"
local function sectionDoc(s)
  if s == "winds" then
    local w = {}
    for _, p in ipairs(PARTS) do if p.sections.winds then w[#w + 1] = p.name end end
    return "woodwinds only (brass is `brass`): " .. (#w > 0 and concat(w, ", ") or "no part of this score")
  end
  local names = {}
  for _, p in ipairs(PARTS) do if p.sections[s] then names[#names + 1] = p.name end end
  if #names == 0 then return "no part of this score" end
  return concat(names, ", ")
end
local function kindOf(w)
  if KEYWORDS[w] then return "keyword" end
  if QUERY[w] then return "query" end
  if PROPSET[w] then return "property" end
  if SECTIONSET[w] then return "part" end
  if MARKS[w] then return "mark" end
  if DURATIONS[w] then return "duration" end
  if DYN[w] then return "dynamic" end
  return nil
end
local LUA_DOC = {
  ["and"] = "Lua keyword: both conditions true", ["or"] = "Lua keyword: either condition true",
  ["not"] = "Lua keyword: the opposite of a condition", ["if"] = "Lua keyword: if cond then … end",
  ["then"] = "Lua keyword: starts the body of an if", ["else"] = "Lua keyword: the other branch of an if",
  ["elseif"] = "Lua keyword: another condition of an if", ["end"] = "Lua keyword: closes if, for, while, function, do",
  ["for"] = "Lua keyword: a loop, for i = 1, 10 do … end, or for n in sel.each() do … end",
  ["while"] = "Lua keyword: while cond do … end", ["repeat"] = "Lua keyword: repeat … until cond",
  ["until"] = "Lua keyword: ends a repeat loop", ["do"] = "Lua keyword: starts a block or a loop body",
  ["in"] = "Lua keyword: for x in … do", ["local"] = "Lua keyword: a variable only in this block",
  ["function"] = "Lua keyword: function name(args) … end", ["return"] = "Lua keyword: give a value back",
  ["break"] = "Lua keyword: leave the loop", ["goto"] = "Lua keyword: jump to a label",
  ["true"] = "Lua keyword: the value true", ["false"] = "Lua keyword: the value false", ["nil"] = "Lua keyword: no value",
  print = "print(…): write values to the console", tostring = "tostring(v): v as text", tonumber = "tonumber(s): text as a number, or nil",
  type = "type(v): \"number\", \"string\", \"table\" …", pairs = "pairs(t): loop over every key and value",
  ipairs = "ipairs(t): loop over a list in order", select = "select(n, …): the arguments from n on, or select(\"#\", …) their count",
  error = "error(message): stop the script with a message", assert = "assert(v, message): stop unless v is true",
  pcall = "pcall(f, …): call f, catching its errors", setmetatable = "setmetatable(t, mt): give a table its metatable",
  getmetatable = "getmetatable(t): a table's metatable", rawequal = "rawequal(a, b): equal without metamethods",
  rawlen = "rawlen(t): length without metamethods", math = "Lua library: math.floor, math.max, math.abs …",
  string = "Lua library: string.format, string.sub, string.rep …", table = "Lua library: table.insert, table.sort, table.concat …",
  utf8 = "Lua library: utf8.char, utf8.len …",
}
function M.help(word)
  local kind = kindOf(word)
  if kind == "part" then return sectionDoc(word), kind end
  if kind == "mark" then return "mark: add(" .. word .. "), has(" .. word .. ")", kind end
  if kind == "duration" then return sformat("%s note: duration == %s (%s quarter notes)", word, word, fmtNum(DURATIONS[word])), kind end
  if kind == "dynamic" then return sformat("dynamic: dynamic == %s, dynamic >= %s", word, word), kind end
  if DOC[word] then return DOC[word], kind or "method" end
  if kind == "keyword" then return LUA_DOC[word] or "Lua keyword", kind end
  if kind == nil and not word:find("[^%w_]") then
    local v = newStd()[word]
    if v ~= nil and (type(v) == "function" or type(v) == "table") then
      return LUA_DOC[word] or ((type(v) == "table" and "Lua library: " or "Lua function: ") .. word), "builtin"
    end
  end
  return nil, kind
end
-- word -> kind, for the highlighter: keyword, query, property, part, mark, duration, dynamic, method, builtin
function M.words()
  local t = {}
  for w in pairs(KEYWORDS) do t[w] = "keyword" end
  for _, w in ipairs(SEL_METHODS) do t[w] = "method" end
  for k, v in pairs(newStd()) do if type(k) == "string" and (type(v) == "function" or type(v) == "table") then t[k] = "builtin" end end
  for _, w in ipairs(DYN_NAMES) do t[w] = "dynamic" end
  for w in pairs(DURATIONS) do t[w] = "duration" end
  for _, w in ipairs(MARK_NAMES) do t[w] = "mark" end
  for _, w in ipairs(SECTIONS) do t[w] = "part" end
  for _, w in ipairs(PROPS) do t[w] = "property" end
  for _, w in ipairs(QUERY_NAMES) do t[w] = "query" end
  return t
end

-- Completion at `cursor` (the number of bytes before it): {from, to, items = {{word, kind, help, insert}}}.
-- By context: after slur. its fields; after a . or : the selection's methods (or a library's functions); inside
-- has / add / remove the marks; after part == or in part("…") the score's part names; after duration == the
-- durations, after dynamic and a comparison the dynamics; inside a query the sections of this score first, then
-- the properties; elsewhere the queries and Lua's keywords.
function M.complete(text, cursor)
  cursor = cursor or #text
  local before = text:sub(1, cursor)
  local toks = tokenize(before)
  local last = toks[#toks]
  local items, seen = {}, {}
  local function add(word, kind, help, insert)
    if seen[word] then return end
    seen[word] = true
    local h = help
    if not h then h = M.help(word) end
    items[#items + 1] = {word = word, kind = kind, help = h or "", insert = insert or word, rank = #items}
  end
  local function finish(from, prefix)
    local out, lp = {}, prefix:lower()
    for _, it in ipairs(items) do
      if it.word:lower():sub(1, #lp) == lp then out[#out + 1] = it end
    end
    if #out == 0 and #lp >= 2 then
      for _, it in ipairs(items) do
        if it.word:lower():find(lp, 1, true) then out[#out + 1] = it end
      end
    end
    return {from = from, to = cursor, items = out, prefix = prefix}
  end
  if last and last.t == "comment" then return {from = cursor, to = cursor, items = {}, prefix = ""} end
  -- the call around the cursor: its name, whether it is a member call, the innermost query around
  local function enclosing(upto)
    local depth, inner, query = 0, nil, nil
    for k = upto, 1, -1 do
      local t = toks[k]
      if t.t == "op" then
        if CLOSE[t.v] then depth = depth + 1
        elseif OPEN[t.v] then
          if depth > 0 then depth = depth - 1
          elseif t.v == "(" then
            local p, pi = prevSig(toks, k)
            if p and p.t == "name" then
              local pp = prevSig(toks, pi)
              local member = isOp(pp, ".") or isOp(pp, ":")
              if not inner then inner = {name = p.v, member = member} end
              if QUERY[p.v] and not member then query = p.v; break end
            elseif not inner then inner = {name = ""} end
          elseif not inner then inner = {name = ""} end
        end
      end
    end
    return inner, query
  end
  if last and last.t == "string" and last.open then   -- inside text: part names
    local inner = enclosing(#toks - 1)
    local p, pi = prevSig(toks, #toks)
    local pp = p and prevSig(toks, pi)
    local quote = last.v:sub(1, 1)
    local typed = last.v:sub(2)
    if (inner and inner.name == "part" and not inner.member) or ((isOp(p, "==") or isOp(p, "~=")) and pp and (pp.v == "part" or pp.v == "partname")) then
      for _, part in ipairs(PARTS) do add(part.name, "part", "part of this score", part.name .. quote) end
    elseif inner and inner.name == "text" then
      for _, s in ipairs{"pizz.", "arco", "div.", "unis.", "con sord.", "senza sord.", "sul pont.", "sul tasto", "ord."} do add(s, "text", "staff text", s .. quote) end
    end
    return finish(last.pos + 1, typed)
  end
  local word, from = "", cursor + 1
  if last and (last.t == "name" or last.t == "kw") then word, from = last.v, last.pos end
  local wi = (last and (last.t == "name" or last.t == "kw")) and #toks or #toks + 1
  local p, pi = prevSig(toks, wi)
  if isOp(p, ".") or isOp(p, ":") then
    local owner = prevSig(toks, pi)
    if owner and owner.t == "name" and (owner.v == "slur" or owner.v == "__slur") then
      for _, f in ipairs(SLUR_FIELDS) do add(f, "property", DOC["slur." .. f]) end
    elseif owner and owner.t == "name" and STD[owner.v] then
      local names = {}
      for k in pairs(STD[owner.v]) do names[#names + 1] = k end
      sort(names)
      for _, k in ipairs(names) do if k ~= "dump" then add(k, "builtin", owner.v .. "." .. k) end end
    elseif owner and (owner.t == "name" or isOp(owner, ")")) then
      for _, m in ipairs(SEL_METHODS) do add(m, "method", m == "count" and DOC["sel.count"] or DOC[m]) end
    end
    return finish(from, word)
  end
  local inner, query = enclosing(wi - 1)
  local function marks() for _, m in ipairs(MARK_NAMES) do add(m, "mark") end end
  if inner and (inner.name == "has" or ((inner.name == "add" or inner.name == "remove") and inner.member)) then
    marks()
    return finish(from, word)
  end
  local pp = p and prevSig(toks, pi)
  local comparing = p and p.t == "op" and (REL[p.v] or p.v == "==" or p.v == "~=")
  if comparing and pp and pp.v == "duration" then
    for _, d in ipairs(DUR_NAMES) do add(d, "duration") end
  elseif comparing and pp and pp.v == "dynamic" then
    for _, d in ipairs(DYN_NAMES) do add(d, "dynamic") end
  elseif comparing and pp and (pp.v == "part" or pp.v == "partname") then
    for _, part in ipairs(PARTS) do add('"' .. part.name .. '"', "part", "part of this score") end
  end
  if query then
    local present = {}
    for _, part in ipairs(PARTS) do for s in pairs(part.sections) do present[s] = true end end
    for _, s in ipairs(SECTIONS) do if present[s] and not HIDDEN[s] then add(s, "part") end end
    for _, w in ipairs(PROPS) do add(w, "property") end
    marks()
    for _, w in ipairs{"not", "and", "or", "true", "false", "nil"} do add(w, "keyword", "Lua keyword") end
    for _, d in ipairs(DUR_NAMES) do add(d, "duration") end
    for _, d in ipairs(DYN_NAMES) do add(d, "dynamic") end
    for _, s in ipairs(SECTIONS) do if not present[s] and not HIDDEN[s] then add(s, "part") end end
  else
    for _, q in ipairs(QUERY_NAMES) do add(q, "query") end
    local kws = {}
    for k in pairs(KEYWORDS) do kws[#kws + 1] = k end
    sort(kws)
    for _, k in ipairs(kws) do add(k, "keyword", "Lua keyword") end
    local names = {}
    for k, v in pairs(newStd()) do if type(v) == "function" or type(v) == "table" then names[#names + 1] = k end end
    sort(names)
    for _, k in ipairs(names) do add(k, "builtin", "Lua") end
    marks()
  end
  if REPL_ENV then
    for k in rawnext, REPL_ENV do if type(k) == "string" and k ~= "_G" then add(k, "variable", "your variable") end end
  end
  return finish(from, word)
end

return M
