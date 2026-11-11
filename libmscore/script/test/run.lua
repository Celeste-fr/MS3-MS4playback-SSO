-- Tests of scorescript.lua without MuseScore: the rewrite, the runtime on K. 550's notes (k550.lua) and on a small
-- made-up score (ties, grace notes, two staves, a part named in Greek, dynamics), the sandbox, the limits, the
-- editor support. The playground's test/run.js runs the same library on Fengari (Lua 5.3).
-- Run: lua_standalone libmscore/script/test/run.lua     (thirdparty/lua/CMakeLists.txt: target lua_standalone)
local dir = arg[0]:match("^(.*)/[^/]*$") or "."
local M = dofile(dir .. "/../scorescript.lua")
local DATA = dofile(dir .. "/k550.lua")

local pass, failed = 0, 0
local function check(name, cond, detail)
  if cond then pass = pass + 1; print("  ok   " .. name)
  else failed = failed + 1; print("  FAIL " .. name .. (detail ~= nil and ("\n         " .. tostring(detail)) or "")) end
end
local byId = {}
for _, n in ipairs(DATA.notes) do byId[n.id] = n end
local PARTNAME = {}
for i, p in ipairs(DATA.parts) do PARTNAME[i] = p.name end
local STR = {["Violin I"] = true, ["Violin II"] = true, Viola = true, Cello = true, Bass = true}
local function pn(n) return PARTNAME[n.part] end
local function run(src, opts) return M.run(src, opts) end
local function ids(res) local t = {} for i, v in ipairs(res.matched) do t[i] = v end table.sort(t) return t end
local function where(f) local t = {} for _, n in ipairs(DATA.notes) do if f(n) then t[#t + 1] = n.id end end table.sort(t) return t end
local function same(a, b) if #a ~= #b then return false end for i = 1, #a do if a[i] ~= b[i] then return false end end return true end
local function slurOf(n) return n.slurs[#n.slurs] end
local function chordsOf(list) local s, c = {}, 0 for _, id in ipairs(list) do local k = byId[id].chordId if not s[k] then s[k] = true; c = c + 1 end end return c end
local function has(n, m) for _, x in ipairs(n.marks) do if x == m then return true end end return false end
local function fresh() M.setScore(DATA) end
fresh()

print("Rewrite")
local W, E = "function(_ENV) return ", " end"
local rw = M.rewrite("findall(strings, 1 <= measure <= 16, slur, slur.index == 1).add(marcato)")
print("    " .. rw)
check("owner's line rewritten as expected", rw ==
  "findall(" .. W .. "strings" .. E .. ", " .. W .. "(1 <= measure and measure <= 16)" .. E .. ", " .. W .. "slur" .. E .. ", " .. W .. "__slur.index == 1" .. E .. ").add(marcato)")
check("function literal argument untouched",
  M.rewrite("findall(function(n) return n.pitch > 70 end, top)") == "findall(function(n) return n.pitch > 70 end, " .. W .. "top" .. E .. ")")
check("method calls :count() / .first not rewritten", M.rewrite("s:count() x.first(a)") == "s:count() x.first(a)")
check("strings and comments with commas/parens pass through",
  M.rewrite('count(name == "a, (b", --[[ ) ]] top)') == 'count(' .. W .. 'name == "a, (b"' .. E .. ', --[[ ) ]] ' .. W .. 'top' .. E .. ')')
check("next's bar argument not wrapped", M.rewrite("next(5, violin1)") == "next(5, " .. W .. "violin1" .. E .. ")")
local multi = "findall(violin1,\n  60 < pitch\n  <= 80,\n  top)"
local function lines(s) local _, n = s:gsub("\n", "") return n end
check("line count preserved", lines(M.rewrite(multi)) == lines(multi), M.rewrite(multi))
check("chain with a call in the middle binds it once: __chain", M.rewrite("count(a < f(x) <= c)") == 'count(' .. W .. '__chain(a, "<", f(x), "<=", c)' .. E .. ')', M.rewrite("count(a < f(x) <= c)"))
check("beat == x compares with a tolerance", M.rewrite("count(beat == 1)") == "count(" .. W .. "__near(beat, 1)" .. E .. ")")
check("dynamic ~= x compares dynamics", M.rewrite('count(dynamic ~= "p")') == "count(" .. W .. 'not __dyneq(dynamic, "p")' .. E .. ")")
check('part == "x" is part("x")', M.rewrite('count(part == "Viola")') == "count(" .. W .. '__parteq(part, "Viola")' .. E .. ")")
check("a condition naming grace is marked", M.rewrite("count(grace)") == "count(__grace(" .. W .. "grace" .. E .. "))")
check("nested query inside a condition is rewritten too", M.rewrite("count(count(top) > 3)") == "count(" .. W .. "count(" .. W .. "top" .. E .. ") > 3" .. E .. ")")

print("Runtime")
local r = run("findall(strings, 1 <= measure <= 16, slur, slur.index == 1).add(marcato)")
local exp = where(function(n) return STR[pn(n)] and n.measure >= 1 and n.measure <= 16 and slurOf(n) and slurOf(n).index == 1 end)
check("owner's line: " .. #exp .. " notes, as computed from the data", not r.error and same(ids(r), exp), r.error)
check("owner's line adds marcato once per chord (" .. chordsOf(exp) .. ")", #r.ops == chordsOf(exp) and r.ops[1].op == "add" and r.ops[1].value == "marcato", #r.ops)
local found = false
for _, i in ipairs(exp) do local n = byId[i]; if pn(n) == "Violin I" and n.measure == 1 and n.beat == 4 and n.name == "Eb5" then found = true end end
check("Violin I Eb5 bar 1 beat 4 (slur start) is in it", found)
check("query result printed as 'N notes (Part n, ...)'", r.log[1].text:find("^findall → %d+ notes %(Violin I %d+") ~= nil, r.log[1] and r.log[1].text)
r = run("print(count(strings, has(marcato)))")
check("a run's marks stay for the next run (has)", r.log[2] and r.log[2].text == tostring(#exp), r.log[2] and r.log[2].text)
r = run("findall(strings, 1 <= measure <= 16, slur, slur.index == 1).add(marcato)")
check("add is idempotent: a second run changes nothing", not r.error and #r.ops == 0, #r.ops)
fresh()

r = run("findall(violin1, 3 <= measure < 5)")
check("chained 3 <= measure < 5", same(ids(r), where(function(n) return pn(n) == "Violin I" and n.measure >= 3 and n.measure < 5 end)), r.error)
r = run("findall(violas or celli, beat == 1)")
check("OR: violas or celli on beat 1", same(ids(r), where(function(n) return (pn(n) == "Viola" or pn(n) == "Cello") and n.beat == 1 end)), r.error)
r = run("findall(violins, duration == eighth, not slur)")
check("not slur + duration == eighth", same(ids(r), where(function(n) return (pn(n) == "Violin I" or pn(n) == "Violin II") and n.duration == 0.5 and not slurOf(n) end)), r.error)
r = run("findall(has(staccato))")
check("has(staccato) finds the score's staccatos", #r.matched > 0 and same(ids(r), where(function(n) return has(n, "staccato") end)), r.error)
r = run("findall(staccato)")
check("a bare mark means has(mark)", not r.error and same(ids(r), where(function(n) return has(n, "staccato") end)), r.error)
r = run("findall(has(staccato))", {marks = {[1] = {"staccato"}}})
check("has() sees marks passed by the host", same(ids(r), where(function(n) return n.chordId == byId[1].chordId end)), #r.matched)
fresh()

r = run("local a = first(celli, measure >= 10)\nlocal b = last(violin1)\nlocal c = next(5, violin1, has(staccato))\nprint(count(strings, top))")
local function best(f, less) local b for _, n in ipairs(DATA.notes) do if f(n) and (not b or less(n, b)) then b = n end end return b end
local cel = best(function(n) return pn(n) == "Cello" and n.measure >= 10 end, function(a, b) return a.tick < b.tick or (a.tick == b.tick and a.pitch > b.pitch) end)
local lastV = best(function(n) return pn(n) == "Violin I" end, function(a, b) return a.tick > b.tick or (a.tick == b.tick and a.pitch < b.pitch) end)
local nxt = best(function(n) return pn(n) == "Violin I" and n.measure >= 5 and has(n, "staccato") end, function(a, b) return a.tick < b.tick end)
local topCount = #where(function(n) return STR[pn(n)] and n.top end)
check("first(celli, measure >= 10)", not r.error and r.log[1].text:find(cel.name, 1, true) and r.log[1].text:find("bar " .. cel.measure, 1, true), r.error or r.log[1].text)
check("last(violin1)", r.log[2] and r.log[2].text:find(lastV.name, 1, true) and r.log[2].text:find("bar " .. lastV.measure, 1, true), r.log[2] and r.log[2].text)
check("next(5, violin1, has(staccato))", r.log[3] and (nxt and r.log[3].text:find("bar " .. nxt.measure, 1, true) or r.log[3].text:find("0 notes")), r.log[3] and r.log[3].text)
check("count(strings, top) = " .. topCount .. " (a number, printed)", r.log[5] and r.log[5].kind == "print" and r.log[5].text == tostring(topCount), r.log[5] and r.log[5].text)

r = run("findall(strings, mesure == 3)")
print("    " .. tostring(r.error))
check("typo: error with did-you-mean and the argument number", r.error == "line 1: argument 2 of findall: 'mesure' isn't a note property: did you mean measure?", r.error)
r = run("findall(violin1, slur.index == 1)")
check("slur.index on unslurred notes = false, not an error", not r.error and same(ids(r), where(function(n) return pn(n) == "Violin I" and slurOf(n) and slurOf(n).index == 1 end)), r.error)
r = run("findall(violin1, not slur.first)")
check("not slur.first is true on unslurred notes", not r.error and same(ids(r), where(function(n) return pn(n) == "Violin I" and not (slurOf(n) and slurOf(n).index == 1) end)), r.error)
local a, b = run("findall(violin1, slur.first or beat == 1)"), run("findall(violin1, beat == 1 or slur.first)")
check("or is commutative around slur.first", not a.error and not b.error and same(ids(a), ids(b)) and #a.matched > 0, a.error or b.error)
r = run("findall(violin1, slur, slur.indx == 1)")
check("slur.indx: did you mean slur.index", r.error and r.error:find("did you mean slur.index?", 1, true), r.error)
r = run("findall(violin1, slur.index > 1)")
check("slur.index > 1 on an unslurred note: an error that says what to test first", r.error and r.error:find("argument 2 of findall", 1, true) and r.error:find("test it first", 1, true), r.error)
r = run("local limits = {}\nfindall(violin1, pitch > limits.violin.low)")
check("a script's own nil index is an error, not false", r.error and r.error:find("line 2: argument 2 of findall", 1, true) and r.error:find("index a nil value", 1, true), r.error)

print("Conditions are true or false")
r = run("findall(strings, measure)")
check("a number as a condition is an error", r.error and r.error:find("argument 2 of findall: a condition must be true or false, but this is a number", 1, true), r.error)
r = run('findall("Violin I")')
check("text as a condition is an error", r.error and r.error:find("argument 1 of findall", 1, true) and r.error:find("text", 1, true), r.error)
r = run("findall(violin1, {})")
check("a table as a condition is an error", r.error and r.error:find("a table", 1, true), r.error)
r = run("findall(violin1, measure = 3)")
check("measure = 3: use == to compare", r.error == "line 1: argument 2 of findall: use == to compare (a single = assigns)", r.error)
r = run("findall(violin1, measure == )")
check("a syntax error names the argument, never rewritten code", r.error and r.error:find("^line 1: argument 2 of findall: ") and not r.error:find("_ENV", 1, true) and not r.error:find("'end' expected", 1, true), r.error)
r = run("local s = findall(violin1, measure == 2)\nfindall(s, top)")
check("a selection as a condition: the note is in it", not r.error and same(ids(r), where(function(n) return pn(n) == "Violin I" and n.measure == 2 end)), r.error)

print("Selections")
r = run("findall(celli, measure == 2).add(staccato)\nfindall(celli, measure == 3):add(tenuto)")
local c2, c3 = chordsOf(where(function(n) return pn(n) == "Cello" and n.measure == 2 end)), chordsOf(where(function(n) return pn(n) == "Cello" and n.measure == 3 end))
local function opsWith(res, v) local c = 0 for _, o in ipairs(res.ops) do if o.value == v then c = c + 1 end end return c end
check(".add and :add both work", not r.error and opsWith(r, "staccato") == c2 and opsWith(r, "tenuto") == c3, r.error)
fresh()
r = run("findall(celli, measure == 2):add(staccato, tenuto)")
check(":add(a, b) adds both", not r.error and opsWith(r, "staccato") == c2 and opsWith(r, "tenuto") == c2, r.error)
r = run("findall(celli, measure == 2).remove(tenuto).remove(tenuto)")
check("remove once per chord, a second remove does nothing", not r.error and #r.ops == c2, #r.ops)
fresh()
r = run("findall(violas, measure == 1).add(accent)")
local v1 = where(function(n) return pn(n) == "Viola" and n.measure == 1 end)
check("marks belong to chords: viola dyads get one accent per chord", #v1 == 16 and #r.ops == 8, #v1 .. " notes, " .. #r.ops .. " ops")
fresh()
r = run("local s = findall(violin1, measure == 2)\nprint(#s, s:count(), s.count())\nfor n in s:each() do print(n.name) end")
local t = {}
if r.log[2] then for w in r.log[2].text:gmatch("%S+") do t[#t + 1] = w end end
check("#sel, :count(), .count(), each()", not r.error and t[1] == t[2] and t[2] == t[3] and #r.log == 2 + tonumber(t[1]), r.error)
r = run("local keep = {}\nfor n in findall(violin1, measure == 2).each() do keep[#keep + 1] = n end\nprint(keep[1].name, keep[#keep].name, keep[1] ~= keep[#keep])")
check("notes from each() are separate handles", not r.error and r.log[2].text:find("true$") ~= nil, r.error or r.log[2].text)
r = run("local function high(n) return n.pitch >= 79 end\nfindall(violin1, high)")
check("local helper as a condition", not r.error and same(ids(r), where(function(n) return pn(n) == "Violin I" and n.pitch >= 79 end)), r.error)
r = run("local lo = 70\nfindall(violin1, pitch < lo)")
check("locals visible inside conditions", not r.error and same(ids(r), where(function(n) return pn(n) == "Violin I" and n.pitch < 70 end)), r.error)
r = run("findall(function(n) return n.pitch > 80 end)")
check("function literal argument works", not r.error and same(ids(r), where(function(n) return n.pitch > 80 end)), r.error)
r = run("local x = 1\n\nfindall(violin1,\n  beat == 1,\n  mesure == 3)")
check("error line preserved across a multi-line call (line 5)", r.error and r.error:find("^line 5: argument 3 of findall") ~= nil, r.error)
r = run("findal(strings)")
check("unknown global: did you mean findall", r.error and r.error:find("did you mean findall?", 1, true), r.error)
r = run("findall(violin1, measure == 2).text(\"pizz.\")")
check("text() once per chord", #r.ops == chordsOf(where(function(n) return pn(n) == "Violin I" and n.measure == 2 end)), #r.ops)
r = run("findall(violas, measure == 1):text(\"div.\")")
check("text() on viola dyads once per chord", #r.ops == 8, #r.ops)
r = run("findall(violin1).ad(staccato)")
check("unknown selection method: did you mean add", r.error and r.error:find("did you mean add?", 1, true), r.error)

print("Preview")
r = run("findall(celli).add(staccato)\nprint(count(celli, has(staccato)))", {preview = true})
local cc = #where(function(n) return pn(n) == "Cello" end)
check("preview flag reported, ops listed", r.preview == true and #r.ops > 0 and not r.error, r.error)
check("inside a preview has() sees the previewed marks", r.log[3] and r.log[3].text == tostring(cc), r.log[3] and r.log[3].text)
r = run("print(count(celli, has(staccato)))")
check("a preview leaves the marks alone", r.log[2] and r.log[2].text == tostring(#where(function(n) return pn(n) == "Cello" and has(n, "staccato") end)), r.log[2] and r.log[2].text)
local calls = {}
M.setHost{add = function(id, m) calls[#calls + 1] = {id, m} end, text = function(id, s) calls[#calls + 1] = {id, s} end}
run("findall(celli, measure == 2).add(tenuto).text('x')", {preview = true})
check("a preview calls no host callback", #calls == 0, #calls)
r = run("findall(celli, measure == 2).add(tenuto).text('x')")
check("a run calls the host once per chord and op", #calls == #r.ops and #calls == 2 * c2, #calls)
M.setHost(nil)
fresh()

print("Names")
r = run("local measure = 3\nfindall(violin1)")
check("a local named like a property is an error", r.error == "line 1: local 'measure' hides the note property measure; rename it", r.error)
r = run("for beat = 1, 4 do end")
check("a loop variable named like a property is an error", r.error == "line 1: loop variable 'beat' hides the note property beat; rename it", r.error)
r = run("local function f(a, strings) end")
check("a parameter named like a section is an error", r.error == "line 1: parameter 'strings' hides the section strings; rename it", r.error)
r = run("\nlocal staccato = 1")
check("a local named like a mark is an error (line 2)", r.error == "line 2: local 'staccato' hides the mark staccato; rename it", r.error)
r = run("local mf = 1")
check("a local named like a dynamic is an error", r.error == "line 1: local 'mf' hides the dynamic mf; rename it", r.error)
r = run("local f, p = 1, 2\nprint(f + p)")
check("locals f and p are allowed", not r.error and r.log[1].text == "3", r.error)
r = run("local count = 0")
check("query names are reserved: local count", r.error == "line 1: 'count' is a query function: choose another name", r.error)
r = run("function first() end")
check("query names are reserved: function first", r.error == "line 1: 'first' is a query function: choose another name", r.error)
r = run("findall = 1")
check("query names are reserved: assignment", r.error == "line 1: 'findall' is a query function: choose another name", r.error)
r = run("measure = 3")
check("assigning to a property name is an error", r.error and r.error:find("can't assign to 'measure'", 1, true), r.error)
r = run("local t = {measure = 1, count = 2}\nt.beat = 3\nprint(t.measure + t.count + t.beat)")
check("table fields with those names are fine", not r.error and r.log[1].text == "6", r.error)
r = run("print(measure)")
check("a property outside a query says where to use it", r.error and r.error:find("'measure' is a note property: use it inside findall", 1, true), r.error)

print("Sandbox and limits")
r = run("while true do end")
check("endless loop stopped", r.error and r.error:find("ran too long", 1, true), r.error)
r = run("while true do pcall(function() while true do end end) end", {limit = 200000})
check("pcall can't swallow the limit", r.error and r.error:find("ran too long", 1, true), r.error)
r = run("findall(violin1, (function() while true do end end)())", {limit = 200000})
check("the limit inside a condition", r.error and r.error:find("ran too long", 1, true), r.error)
r = run("x = 5"); r = run("print(x)")
check("globals don't leak between runs", r.error == "line 1: 'x' isn't defined", r.error)
for _, name in ipairs{"os", "io", "require", "debug", "dofile", "loadfile", "load", "loadstring", "package", "coroutine", "collectgarbage", "rawset", "rawget"} do
  r = run("return " .. name .. " ~= nil")
  check("no " .. name, r.error and r.error:find("isn't defined", 1, true), r.error)
end
r = run("print(getmetatable(''))\nstring.rep = nil\nprint(('ab'):rep(2), math.floor(2.5), #table.pack(1, 2), utf8.char(233))")
check("no string metatable; the string table is a copy", not r.error and r.log[1].text == "nil" and r.log[2].text == "abab  2  2  é", r.error or r.log[2].text)
r = run("print(string.rep('a', 3))")
check("a run's changes to its library copies don't reach the next run", not r.error and r.log[1].text == "aaa", r.error)
r = run("local s = findall(violin1, measure == 1)\nprint(getmetatable(s), pcall(setmetatable, s, {}))")
check("a selection's metatable is protected", not r.error and r.log[2].text:find("^false  false") ~= nil, r.error or r.log[2].text)
r = run("for k, v in pairs({a=1}) do print(k, v) end\nprint(next({b=2}))")
check("Lua's pairs / next(t) still work", not r.error and r.log[1].text == "a  1" and r.log[2].text == "b  2", r.error)
r = run("print(_G.os, _G.print ~= nil)")
check("_G is the script's own table", r.error and r.error:find("'os' isn't defined", 1, true), r.error)

print("Console")
M.resetConsole()
r = run("x = 5", {persist = true}); r = run("x + 1", {persist = true, echo = true})
check("console variables persist, an expression prints its value", not r.error and r.log[1].kind == "value" and r.log[1].text == "6", r.error)
r = run("s = findall(violin1, measure == 2)", {persist = true})
r = run("s.add(tenuto)", {persist = true, echo = true})
check("a selection kept in the console works on the next line", not r.error and #r.ops > 0 and #r.log == 0, r.error or #r.log)
r = run("x = 99\nprint(x)", {persist = true, preview = true})
local r2 = run("x", {persist = true, echo = true})
check("a preview sees the console's variables and changes none", r.log[1].text == "99" and r2.log[1].text == "5", r2.log[1] and r2.log[1].text)
r = run("print(x)")
check("a script run doesn't see the console's variables", r.error == "line 1: 'x' isn't defined", r.error)
r = run("count(violin1)", {persist = true, echo = true})
check("a query line prints its result once", #r.log == 1 and r.log[1].kind == "query", #r.log)
local d2 = dofile(dir .. "/k550.lua"); d2.signature = "a"; M.setScore(d2)
r = run("s = findall(violin1, measure == 2)", {persist = true})
d2 = dofile(dir .. "/k550.lua"); d2.signature = "a"; M.setScore(d2)
r = run("s.add(accent)", {persist = true})
check("the same notes again (same signature): the selection is still valid", not r.error and #r.ops > 0, r.error)
d2 = dofile(dir .. "/k550.lua"); d2.signature = "b"; table.remove(d2.notes); M.setScore(d2)
r = run("s.add(marcato)", {persist = true})
check("after the score's notes changed: a clear error", r.error == "line 1: the score changed since this selection was made: run the query again", r.error)
r = run("s.add(marcato)", {persist = true, echo = true})
check("… with its line in the console's echo mode too", r.error == "line 1: the score changed since this selection was made: run the query again", r.error)
M.resetConsole()
fresh()

print("Made-up score: ties, grace notes, staves, names, dynamics")
local function note(t) t.sounding = t.sounding or t.pitch; t.index = t.index or t.measure; t.voice = t.voice or 1; t.staff = t.staff or 1
  t.slurs = t.slurs or {}; t.marks = t.marks or {}; t.top = true; t.bottom = true; t.chord = false; t.octave = 4; t.degree = 1; return t end
local SMALL = {
  parts = {{name = "Βιολί", instrument = "violin", family = "orchestral-strings", group = "strings"},
           {name = "Clarinet in B♭", instrument = "bb-clarinet", family = "clarinets", group = "woodwinds"},
           {name = "Piano", instrument = "piano", family = "keyboards", group = "keyboards"},
           {name = "Horn", instrument = "horn", family = "horns", group = "brass"}},
  notes = {
    note{id = 1, part = 1, measure = 0, index = 1, beat = 4, tick = 0, duration = 1, pitch = 67, name = "G4", chordId = 1, dynamic = "p"},
    note{id = 2, part = 1, measure = 1, index = 2, beat = 1, tick = 480, duration = 3, pitch = 72, name = "C5", chordId = 2, tied = true, dynamic = "p"},
    note{id = 3, part = 1, measure = 1, index = 2, beat = 4, tick = 1900, duration = 0.25, pitch = 74, name = "D5", chordId = 3, grace = true, dynamic = "p"},
    note{id = 4, part = 1, measure = 1, index = 2, beat = 4, tick = 1920, duration = 1 / 3, pitch = 76, name = "E5", chordId = 4, dynamic = "mf",
         slurs = {{index = 3, length = 3}, {index = 1, length = 2}}},
    note{id = 5, part = 1, measure = 1, index = 2, beat = 4 + 1 / 3, tick = 2080, duration = 1 / 3, pitch = 77, name = "F5", chordId = 5, dynamic = "mf", slurs = {{index = 2, length = 2}}},
    note{id = 6, part = 2, measure = 1, index = 2, beat = 1, tick = 480, duration = 4, pitch = 62, sounding = 60, name = "D4", chordId = 6},
    note{id = 7, part = 3, measure = 1, index = 2, beat = 1, tick = 480, duration = 2, pitch = 48, name = "C3", chordId = 7, staff = 2, voice = 1, dynamic = "ff"},
    note{id = 8, part = 3, measure = 1, index = 2, beat = 1, tick = 480, duration = 2, pitch = 60, name = "C4", chordId = 8, staff = 2, voice = 2, dynamic = "ff"},
    note{id = 9, part = 4, measure = 1, index = 2, beat = 1, tick = 480, duration = 4, pitch = 60, sounding = 53, name = "C4", chordId = 9, dynamic = "pp"},
  }}
M.setScore(SMALL)
local function got(src, opts) local res = run(src, opts); return res.error or table.concat(ids(res), " ") end
check("a tied note is one note with its whole length", got("findall(tied)") == "2" and got("findall(violin, duration == 3)") == "2", got("findall(tied)"))
check("grace notes are left out", got("findall(violin1)") == "1 2 4 5", got("findall(violin1)"))
do local c = M.complete("findall(vio"); local seen = {}
  for _, it in ipairs(c.items) do seen[it.word] = true end
  check("completion: violin1 not offered for a section of one part (still accepted when typed)", seen.violin and seen.violins and not seen.violin1 and got("findall(violin1)") == "1 2 4 5", tostring(seen.violin1)) end
check("… unless the query says grace", got("findall(violin1, grace)") == "3" and got("findall(violin1, grace or beat == 1)") == "2 3", got("findall(violin1, grace or beat == 1)"))
check("not grace is the same as no word about it", got("findall(violin1, not grace)") == "1 2 4 5", got("findall(violin1, not grace)"))
check("measure is the printed number (pickup 0), index counts", got("findall(measure == 0)") == "1" and got("findall(index == 1)") == "1" and got("findall(violin, index == 2)") == "2 4 5", got("findall(index == 1)"))
check("beat == 4 + 1/3 matches a triplet (tolerance)", got("findall(beat == 13/3)") == "5" and got("findall(beat == 4 + 1/3)") == "5", got("findall(beat == 13/3)"))
check("duration == quarter/3", got("findall(duration == quarter/3)") == "4 5", got("findall(duration == quarter/3)"))
check("voice and staff", got("findall(keyboards, staff == 2, voice == 2)") == "8" and got("findall(staff == 2)") == "7 8", got("findall(staff == 2)"))
check("written pitch, and sounding", got("findall(pitch == 60)") == "8 9" and got("findall(sounding == 60)") == "6 8" and got("findall(sounding == 53)") == "9", got("findall(sounding == 60)"))
check("sections from instrument ids: strings, winds, woodwinds, brass, clarinets, horns", got("findall(strings)") == "1 2 4 5" and got("findall(winds)") == "6"
  and got("findall(woodwinds)") == "6" and got("findall(brass)") == "9" and got("findall(clarinets)") == "6" and got("findall(horns)") == "9" and got("findall(clarinet)") == "6", got("findall(winds)"))
check('part("Βιολί"): a part by its exact name, not ASCII', got('findall(part("Βιολί"))') == "1 2 4 5" and got('findall(part == "Βιολί")') == "1 2 4 5" and got('findall(part ~= "Βιολί", part("Piano", "Horn"))') == "7 8 9", got('findall(part("Βιολί"))'))
check("partname is the name as text", got('findall(partname == "Clarinet in B♭")') == "6", got('findall(partname == "Clarinet in B♭")'))
r = run('findall(part("Violin"))')
check("an unknown part name is an error", r.error and r.error:find("the score has no part 'Violin'", 1, true), r.error)
check("dynamics are ordered: dynamic >= mf", got("findall(dynamic, dynamic >= mf)") == "4 5 7 8" and got("findall(dynamic, pp < dynamic < ff)") == "1 2 4 5", got("findall(dynamic, dynamic >= mf)"))
check('dynamic == p and dynamic == "p" are the same', got("findall(dynamic == p)") == "1 2" and got('findall(dynamic == "p")') == "1 2" and got("findall(dynamic ~= p)") == "4 5 6 7 8 9", got("findall(dynamic ~= p)"))
check("no dynamic in force: dynamic is nil", got("findall(not dynamic)") == "6", got("findall(not dynamic)"))
r = run("findall(dynamic < mf)")
check("comparing a missing dynamic: an error that says what to test first", r.error and r.error:find("no dynamic in force", 1, true), r.error)
r = run('findall(dynamic == "loud")')
check("an unknown dynamic name is an error", r.error and r.error:find("'loud' isn't a dynamic", 1, true), r.error)
r = run("findall(dynamic == 3)")
check("a dynamic against a number is an error", r.error and r.error:find("can only be compared with a dynamic", 1, true), r.error)
check("a note ending one slur and starting another is slur.last and slur.first", got("findall(slur.last)") == "4 5" and got("findall(slur.first)") == "4" and got("findall(slur, slur.index == 1)") == "4", got("findall(slur.last)"))
r = run("for n in findall(slur.first).each() do print(#n.slurs, n.slurs[1].index, n.slurs[2].length) end")
check("slurs lists both", not r.error and r.log[2].text == "2  3  2", r.error or r.log[2].text)
r = run("first(violin1)")
check("a note prints with its part's name", r.log[1].text == "first → 1 note (Βιολί 1): Βιολί G4, bar 0 beat 4", r.log[1].text)
fresh()

print("Editor support")
check("check: nothing for a good script", M.check("findall(strings, 1 <= measure <= 16).add(marcato)") == nil)
local msg, line = M.check("local a = 1\nfindall(violin1, measure = 3)")
check("check: message and line without running", msg == "line 2: argument 2 of findall: use == to compare (a single = assigns)" and line == 2, msg)
msg, line = M.check("x = = 1")
check("check: a plain syntax error", msg and msg:find("^line 1: ") and line == 1, msg)
local function words(c) local t = {} for _, it in ipairs(c.items) do t[#t + 1] = it.word end return t end
local function hasWord(c, w) for _, it in ipairs(c.items) do if it.word == w then return it end end return nil end
local c = M.complete("findall(vio")
do local miss = {}
  for w in pairs(M.words()) do local h = M.help(w); if not h or h == "" then miss[#miss + 1] = w end end
  table.sort(miss)
  check("hover help for every word of the vocabulary (keywords and Lua functions too)", #miss == 0, table.concat(miss, " ")) end
check("complete in a query: this score's sections, with their kind", hasWord(c, "violin1") and hasWord(c, "violins") and hasWord(c, "violin1").kind == "part" and c.from == 9, table.concat(words(c), " "))
c = M.complete("findall(violin1, slur.")
check("complete after slur.: its fields", #c.items == 4 and hasWord(c, "index"), table.concat(words(c), " "))
c = M.complete("findall(violin1).a")
check("complete after a selection: its methods", #c.items == 1 and c.items[1].word == "add" and c.items[1].kind == "method", table.concat(words(c), " "))
c = M.complete("findall(violin1).add(st")
check("complete in add(: marks", #c.items == 2 and c.items[1].kind == "mark", table.concat(words(c), " "))
c = M.complete("findall(duration == ")
check("complete after duration ==: durations first", c.items[1] and c.items[1].kind == "duration", table.concat(words(c), " "))
c = M.complete('findall(part("Vi')
check("complete in part(\": part names", #c.items == 3 and hasWord(c, "Violin I") and hasWord(c, "Violin I").insert == 'Violin I"', table.concat(words(c), " "))
c = M.complete("fi")
check("complete at top level: queries", c.items[1] and c.items[1].word == "findall" and c.items[1].kind == "query" and not hasWord(c, "measure"), table.concat(words(c), " "))
c = M.complete("findall(me")
check("complete: a property with its help", hasWord(c, "measure") and hasWord(c, "measure").help:find("bar number", 1, true), table.concat(words(c), " "))
local w = M.words()
check("words: a kind per word", w.findall == "query" and w.measure == "property" and w.strings == "part" and w.marcato == "mark" and w.eighth == "duration" and w.mf == "dynamic" and w["while"] == "keyword" and w.add == "method" and w.print == "builtin")
check("help: a section lists its parts", M.help("violins") == "Violin I, Violin II", M.help("violins"))
local json = M.runJSON("print(count(violin1, measure == 1))")
check("runJSON: the result as JSON", json:find('"kind":"print"', 1, true) and json:find('"matched":[', 1, true), json:sub(1, 200))

-- C-style logic: || | && & != ! (the owner, 2026-11-10)
do
  local function rw(a, b) local r = M.rewrite(a); check("C logic: " .. a:gsub("\n", " "), r:gsub("%s+", " ") == b, r) end
  rw("x = a || b", "x = a or b")
  rw("x = a | b", "x = a or b")
  rw("x = a && b", "x = a and b")
  rw("x = a & b", "x = a and b")
  rw("x = a != b", "x = a ~= b")
  rw("x = !a", "x = not a")
  rw("x = !a == b", "x = not a == b")
  rw("x = a | b == c", "x = a or b == c")
  rw("x = a ~= b // 2 .. 'x' ~ 1", "x = a ~= b // 2 .. 'x' ~ 1")
  rw('x = "a || b != c && !d | e"', 'x = "a || b != c && !d | e"')
  rw("x = [[a || b]] -- c && d", "x = [[a || b]] -- c && d")
  rw("x = 1 --[[ | ]] + 2", "x = 1 --[[ | ]] + 2")
  check("C logic keeps the line count", lines(M.rewrite("a ||\n b &&\n !c")) == 2)
  check("C logic: a chain around && still chains", M.rewrite("count(1 <= measure <= 4 && !grace)"):find("and", 1, true) ~= nil)
  local function cnt(src) fresh(); local r = run("print(" .. src .. ")"); return r.error and ("ERR " .. r.error) or r.log[1].text end
  local function eq(c, l) local a, b = cnt(c), cnt(l); check("C logic runtime: " .. c, a == b and not a:find("^ERR"), a .. " / " .. b) end
  eq("count(winds | brass)", "count(winds or brass)")
  eq("count(strings && !basses)", "count(strings and not basses)")
  eq("count(violas || celli, beat != 1)", "count(violas or celli, beat ~= 1)")
  eq("count(violin1 & measure == 1)", "count(violin1 and measure == 1)")
  eq("count(1 <= measure <= 2 && violin1)", "count(1 <= measure <= 2 and violin1)")
  fresh(); local r = run("x = 3\nif x != 2 then print(1) end\nif !(x == 3) || x != 3 then print(2) end")
  check("plain Lua line with != and ||", not r.error and #r.log == 1 and r.log[1].text == "1", r.error)
  check("hover help for | || & && ! != ~=", M.help("|"):find("or", 1, true) and M.help("&&"):find("and", 1, true) and M.help("!"):find("not", 1, true)
        and M.help("!="):find("~=", 1, true) and M.help("or"):find("||", 1, true) and M.help("and"):find("&&", 1, true)
        and M.help("not"):find("!", 1, true) and M.help("~="):find("!=", 1, true))
end

local t0 = os.clock()
for _ = 1, 5 do run("findall(strings, 1 <= measure <= 16, slur, slur.index == 1)") end
print(string.format("    timing: %.1f ms per run", (os.clock() - t0) / 5 * 1000))
print(string.format("\n%d passed, %d failed", pass, failed))
os.exit(failed == 0 and 0 or 1)
