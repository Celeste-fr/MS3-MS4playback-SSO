#!/usr/bin/env python3
"""Puts the repo's Lua library and browser host into the playground page, so that the playground, its tests and
MuseScore run one implementation:
  <script type="text/x-lua" id="lua-runtime">  <- libmscore/script/scorescript.lua
  <script id="engine-js">                      <- tools/scripting/playground_engine.js
Usage: update_playground.py <playground index.html>     (then: node <playground>/test/run.js)"""
import os, re, sys
here = os.path.dirname(os.path.abspath(__file__))
page = sys.argv[1]
lua = open(os.path.join(here, "..", "..", "libmscore", "script", "scorescript.lua"), encoding="utf-8").read()
js = open(os.path.join(here, "playground_engine.js"), encoding="utf-8").read()
assert "</script" not in lua and "</script" not in js
html = open(page, encoding="utf-8").read()
def put(html, opening, body):
    m = re.search(re.escape(opening) + r".*?</script>", html, re.S)
    assert m, opening
    return html[:m.start()] + opening + body + "</script>" + html[m.end():]
html = put(html, '<script type="text/x-lua" id="lua-runtime">', lua)
html = put(html, '<script id="engine-js">', js)
open(page, "w", encoding="utf-8").write(html)
print("updated", page)
