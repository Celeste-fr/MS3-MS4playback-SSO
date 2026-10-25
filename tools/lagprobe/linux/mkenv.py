#!/usr/bin/env python3
# Linux lag-probe environment: isolated HOME with the test synth as the library's plug-in.
# usage: mkenv.py <repo> <home> <build.rel>
import os, re, struct, sys
repo, home, build = sys.argv[1:4]
src = os.path.join(repo, "share/soundlibraries/Spitfire Symphony Orchestra.xml")
name = "Lag Test SSO"
xml = open(src, encoding="utf-8").read()
xml = xml.replace('<SoundLibrary name="Spitfire Symphony Orchestra">', f'<SoundLibrary name="{name}">')
xml = re.sub(r'<Plugin files="[^"]*"/>', '<Plugin files="mstestsynth.vst3"/>', xml)
xml = re.sub(r'\s*<Files [^>]*/>', '', xml)
libdir = os.path.join(home, "maps")
os.makedirs(libdir, exist_ok=True)
mp = os.path.join(libdir, name + ".xml")
open(mp, "w", encoding="utf-8").write(xml)
dyn = src.replace(".xml", ".dynamics.json")
if os.path.exists(dyn):
    open(mp.replace(".xml", ".dynamics.json"), "w").write(open(dyn).read())
setups = os.path.join(home, ".local/share/MuseScore/MuseScore3Evo/soundlibraries", name)
os.makedirs(setups, exist_ok=True)
for inst in set(re.findall(r'<(?:Instrument|Patch) name="([^"]+)"', xml)):
    fn = re.sub(r'[\\/:*?"<>|]', "_", inst)
    # a state the test synth takes (MuseScore's container: "MSV3", version 1, the plug-in's name, the component's
    # state: articulation and level as doubles; no controller state), so that every patch loads as a setup does
    # (MSTESTSYNTH_SETSTATE_MS then times its setState; a text here was refused at once: "could not be loaded")
    name16 = "MS Test Synth".encode("utf-16-be")
    component = struct.pack("<dd", 0.0, 1.0)
    state = b"MSV3" + struct.pack(">I", 1) + struct.pack(">I", len(name16)) + name16 \
            + struct.pack(">I", len(component)) + component + struct.pack(">I", 0)
    with open(os.path.join(setups, fn + ".vst3state"), "wb") as f:
        f.write(state)
vst = os.path.join(home, ".vst3")
os.makedirs(vst, exist_ok=True)
link = os.path.join(vst, "mstestsynth.vst3")
if not os.path.lexists(link):
    os.symlink(os.path.join(build, "mtest/libmscore/soundlibrary/mstestsynth.vst3"), link)
cfg = os.path.join(home, ".config/MuseScore")
os.makedirs(cfg, exist_ok=True)
open(os.path.join(cfg, "MuseScore3Evo.ini"), "w").write(f"""[application]
startup\\firstStart=false
startup\\sessionStart=empty

[ui]
application\\startup\\showSplashScreen=false
application\\startup\\showStartCenter=false

[io]
soundLibrary={mp}
soundLibraryOutput=plugin
""")
print(mp)
