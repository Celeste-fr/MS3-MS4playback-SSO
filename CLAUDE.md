# CLAUDE.md: guide for agents working on this repository

A fork of the MuseScore 3.7 community fork (Jojo-Schmitz/MuseScore 3.x @ 29e066cd, history squashed). Goals:

1. **MS4 playback**: MuseScore 3 plays as MuseScore 4.7.5 does (FluidSynth setup, `MS Basic.sf3`, reverb, note
   model, dynamics). Essentially finished.
2. **External sound libraries**: chosen parts play through the owner's **Spitfire Symphony Orchestra (SSO)** in
   the free **Kontakt Player 8** on **Windows**, hosted as VST 3, sent over MIDI, or played by **Ableton Live 12**.
   Each note's articulation comes from the notation.

Around them: built-in tuning, the playability checker, gradual tempo changes, phrase marks, scale-degree input.

Keep this file a **current-state guide**: update it when architecture, build steps, rules or known issues change.
What was done and measured goes in commit messages (detailed on purpose: read `git log` first). **Picking up the
work? Read `HANDOFF.md`.** Background (why a number is what it is, the owner's runs, measurement narratives,
CI history): `docs/HISTORY.md`, read only when you need the background of a topic.

## Rules

- **Files survive a round trip to MuseScore 3.6** (the owner): fork → 3.6.2 → fork unchanged. Test:
  `ab/roundtrip2.py [--mtest N] [--list file] [scores…]` (XML, PNG pages, MIDI). Where 3.7 reads or writes
  differently, follow 3.6 (legacy style defaults, `MStyle::isDefault` within 1e-6, `MStyle::notInMuseScore36`,
  tuplet `<Number>`, `updateInstrumentId`). New per-score state goes in **metaTags** (3.6 keeps them), left out
  when default. Known leftovers: some MuseScore 1/2 imports' XML changes once (pages, MIDI identical); MIDI
  export's same-instant event order isn't stable.
- **Readings of music go on a review page**: when work relies on an agent's reading of sheet music, a legend, a
  screenshot of a plug-in's key/articulation list or a contact sheet, publish a claude.ai artifact with each
  source picture (or crop) next to the reading and its use, before it changes code or the map. Symbols in
  Bravura (subset `fonts/bravura/Bravura.otf` with `pyftsubset`, embedded); say what couldn't be read; open with
  a "What to check" list. Example: https://claude.ai/artifact/CPfVu3qdHzMr5xPKiJFD2Y.
- **Live and MuseScore sound alike** (the owner, 2026-09-30): any change to SSO playback (renderer, hosting,
  Mixer, Controllers, tuning) must reach the Live path: clips (`libmscore/liveclips.*`), the set Create Live Set
  writes (`mscore/livesetexport.*`, `libmscore/livesetwriter.*`) or the MuseScore Link device (`tools/live/`).
  Otherwise list it in LIVE.md › Live against MuseScore › What still differs. Check with `tst_liveequivalence`
  and, for SSO, `MuseScore3Evo.exe --live-equivalence <folder> <score>` on the Windows VM; say which you ran.
- **Public repository**: never commit Spitfire's or NI's files (samples, `.nki`, presets, scripts, expression
  maps), the owner's scores, extracts or Live sets, logs with their paths, keys or e-mail addresses. Derived
  names, titles, key ranges and measured numbers only.
- **Say what was tested with what**: the Linux dev machine can't hear Kontakt, SSO or Windows. Results come from
  a stand-in (test synth `mstestsynth.vst3`, sfizz), the Windows VM (VERIFY.md › With the real library) or the
  owner. Say which.
- **The owner decides what is heard**: measure, then offer (often "try both, decide by ear"). Kontakt patches keep
  the library's defaults: values change only through MuseScore (Controllers, automation), never saved from
  Kontakt's window; every patch MuseScore sets up gets 512 voices (approved).

## Branches and CI

- `main` is the default branch. **A session works on its own branch only**: no merging into or pushing to `main`
  (the owner decides). Check `git log` of `main` and your branch first.
- Since 2026-10-02 `main` contains every feature branch: the chain `live-set-export` (live-integration,
  live-clip-edit, piano-v37-fixes, legato-timing, the SSO timing of `claude/intelligent-volta-gx7gmw`) →
  `automation-editor` (drawn automation lanes, `automation-live-research`), and the side branches
  `claude/intelligent-cray-6pd4o1` (SSO measurements, VM measurement tools) and `playability-checker`
  (PLAYABILITY.md). The branches are kept; new work branches from `main`.
- CI runs by hand only. Windows: `test_soundlibrary_windows.yml` (*Run workflow*, or `[windows-build]` in the last
  commit message of a push to `main` / a `claude/` branch): MSVC build (cached, ~15 min), `tst_soundlibrary`,
  artifact `MuseScore-soundlibrary-win64`. On MSVC `mtest/` is its own solution (built by vcxproj).
- **Windows build etiquette**: build only when the owner needs a new MuseScore to try soon, or for code Linux
  can't compile that the owner will need; not to check that passing code compiles on Windows. Validate locally,
  batch, don't retry blindly, **don't cancel a running build**. Name a build by its **run number with its full
  GitHub link**. A map-only change also needs a build (or the owner copies the XML into the installed
  `share/soundlibraries/`).
- Also: `tool_extract_library_files.yml` (ExtractLibraryFiles.exe), `verify_playback_owner_pc.yml` (optional
  self-hosted runner, not set up).

## Architecture map

Read a file's header comment before changing it: it holds the design. Full detail of each item: docs/HISTORY.md
Part 1.

**MS4 playback.** `libmscore/ms4playback.*` (note model), `ms4tables.h` (generated by
`tools/ms4playback/gen_tables.py`), `rendermidi.cpp` (`collectMeasureEventsMs4`, `renderMs4Dynamics`; dynamics
method 3 is the default), `thirdparty/fluidsynth`, `effects/musereverb`, `synthesizerstate.cpp`. One deliberate
difference: an MS3 hairpin with its own velocity change plays as in 3.6 (`ms3HairpinVelocity()`, preference
`application/playback/ms3HairpinVelocityChange`; `MS4_STRICT=1` disables, `ab/trace/regress.sh` sets it).
`mscore/playbackmode.h` (MS3 / MS4 / library, all parts) and `libmscore/partplayback.h` (per part, metaTag
`partPlayback`). `tempochange.h` (rit./accel. lines, metaTag `tempoChanges`). `slur.h` › Phrase marks (non-legato
slurs, Alt+S, metaTag `phraseMarks`).

**Notation.** Custom key signatures follow each staff's clef (`KeySigEvent::forClef`, `Staff::keySigEventForClef`,
*Tools › Adapt Key Signatures to Clefs*, `mscore/keyedit.cpp`). Deleting a restating time signature keeps the
measures (`Score::cmdRemoveTimeSig`). Numpad 1-7 enter scale degrees (`Score::scaleDegreeStep`; on Linux/xcb
`ScoreView::event` takes the keypad keys, `numpadDegreeAction`; `Shortcut::load` gives new actions their defaults
in an old shortcuts.xml).

**Tuning** (`libmscore/tuning.h`): temperament (metaTag `temperament`, the Tuning plugin's JSON) + accidental rules
(microtonal, HEJI, Johnston, stacked accidentals as hidden `Symbol`s) + the note's own tuning. `tuningtables.h`
from `tools/tuning/gen_tuning_tables.py`; `mscore/tuningdialog.*`; plugin API `playbackTuning`,
`microtonalTuning`. The Microtonal Tuner plugin is frozen: update or drop `tst_tuning::pluginParity` when needed.

**Playability** (PLAYABILITY.md): `libmscore/playabilityrules.*`, `playability.*`, `playabilitydiagram.*`,
`mscore/playabilitypanel.*`. Later work is on `playability-checker`.

**Sound libraries** (`libmscore/soundlibrary.h`; map format `share/soundlibraries/README.md`)
- `SoundLib`: loads maps, matches instruments by `Instrument::getId()`, builds the `Want` from MS4 articulations and
  staff text, `choose()` (extras `with=`, kits `kit="1"`, `prefer`, shorts by `length`/`from`), `routes()`.
- **The SSO map is generated, never hand-edited**: `python3 tools/soundlibraries/gen_spitfire_sso.py
  <reaticulate>/userbanks/Spitfire/Spitfire-Symphony_Orchestra.reabank "share/soundlibraries/Spitfire Symphony
  Orchestra.xml"` (needs a clone of github.com/jtackaberry/reaticulate). Inputs: the script's tables (`I`, `T`,
  `SPITFIRE_ADD/DROP/RENAME`, `DRUMS`, `KEYSWITCHED`, `LENGTHS`, `EXPECT`) and the measured `tools/soundlibraries/
  sso_*.json`. Diff the XML after: only intended entries may change.
- Renderer (`rendermidi.cpp`): each library event carries patch, switch and route; `finishLibraryEvents` routes,
  copies controllers to every patch ahead of the notes, applies early starts, lanes and bends. Dynamics on CC1;
  listed shorts take velocity (calibrated from `dynamics.json`). Slurred notes overlap; **legato transitions** and
  **held notes start early** by measured delays (`<Articulation legatoDelay>` by interval, `<Articulation onset>`
  by pitch; metaTags `soundLibraryLegatoEarly`, `soundLibraryOnsetEarly`), capped by the note before (none up to
  125 ms, half from 250 ms). HANDOFF.md has the open timing items.
- **Playback adjustments are configurable** (`libmscore/playbacksettings.*`; **docs/PLAYBACK_SETTINGS.md** is the
  inventory): built-in defaults → `<dataPath>/playback.ini` (written with every key when missing; Edit › Reload
  Playback Settings) → the score (metaTag `playbackSettings`, plus the older early / lanes metaTags). Read a
  setting with `Playback::value(id, score, mapValue)`; a new adjustment gets a `DEFINITIONS` entry, a row in the
  inventory and a layers test. UI: Mixer › Advanced Options… › Playback adjustments (`mscore/playbacksettingswidget.*`).
- Dynamics calibration (*Check articulations* › Dynamics or `--check-dynamics`) → `dynamics.json`; per-family
  balance, Recommended, even steps (metaTag `soundLibraryEvenSteps`). Only shorts are calibrated (the owner).
- Controllers: map `<Controller>`, metaTag `partControllers` (`partcontrollers.*`), live while playing
  (`ControllersWindow`, `audio/vst3/librarycontrollers.*`). Automation: `libmscore/automation.*`, metaTag
  `automation` (points step / linear, a ramp may carry Live's Bézier control points). Every lane is editable;
  `Lane::playedByLive` (liveHash / pointsHash in its extra) says whether Live's set plays it; an import merges
  (`Automation::merge`, newer edit wins, both changed: a dialog per lane). **Editor**: `mscore/automationlanes.*`
  (Continuous View, lanes under a selected library part, Live's gestures; header lists them), model
  `Automation::Edit`, one undo step per gesture (`undoWrite`); a Dynamics lane replaces the notation's CC1 from its
  first point.
- Percussion: `<Drum key name>` lists every hit of SSO's kits, one-drum patches and ensembles
  (`tools/soundlibraries/sso_percussion_hits.json`, from the owner's window pictures); a technique off at the
  defaults is `<Drum name default="off"/>` (no key, never played). The nine `"<patch> (all on)"` `<Patch>`es set
  Kickstart's arrays whole (`%c2lsa`, `%4jwcn`, `%x4jsr`, `$nd5ia=0`); `KontaktSetup::unpurgeSwitchedOn` loads the
  sample groups they switch on (Kickstart's purge rule; test `kontaktKickstartUnpurge`). Script values may change
  length (`kontaktScriptValueLengths`). Mics on 3-mic patches are Close, Tree, Ambient.
- Microtones: Kontakt ignores VST 3 note tuning. **Tuning lanes** (patch copies per tuning, metaTag
  `soundLibraryLanes`) play by **varispeed** (`Vst3Plugin::setPitch`) or by **pitch bend** where the patch bends
  cleanly (`<Instrument bend>`; varispeed can't reach Live).
- Mixer on library parts: `Vst3Synth::setMix` per slot (hosted), CC7/10/91/93 on the routes (MIDI out).

**Hosting (VST 3)** (`BUILD_VST3`, on except macOS; preference `io/soundLibrary`)
- `audio/vst3/vst3plugin.*` (one instance; `settle()` after setState, or SSO's script resets parameters),
  `vst3synth.*` (64 slots = port*16+channel; `idle` must not hold the slots' mutex while sending: missed events
  wait in `_pending`), `mscore/vst3editor.*`, `mscore/soundlibraryhost.*` (`sync`, preload at score open,
  `SoundLibraryExport`, *View › Sound Library…*). Plug-in modules stay loaded until exit.
- **Setups are made by MuseScore** from each patch's `.nki` (`audio/vst3/kontaktsetup.*`), then replaced by
  Kontakt's own state after the first load (`resave`). Setups folder `<dataPath>/soundlibraries/<library>/`:
  `<patch>.vst3state`, `Kontakt empty.vst3state`, `made setups.json` (`MAKER_VERSION`), `checks.json` (raise
  `CHECK_VERSION` in soundlibrarycheck.cpp when old results go stale), `dynamics.json`, `load times.log` (qDebug
  doesn't show on Windows).
- Background jobs (own setups copy, lock, log; `.bat` files in `bin`): `--check-dynamics`, `--scan-keys`,
  `--measure-load-times`, `--verify-playback`, `--live-equivalence`, `--create-live-set`, `--live-set-readback`.
- Measurement runs (supervised: `superviseExtract`, a hang or crash retried then left out; resumable: a start
  skips what earlier runs of the same version did): `--extract-library … --extract-controllers
  --extract-pitch-bend`, `--check-timing`, `--check-rest [--rest-parts range,repeats,controls,legato,onset,shorts,
  legatolengths,legatopitches]`, `--all-sounds` (every sound, not only notated ones), `--window-pictures`
  (percussion windows). Readers → `tools/soundlibraries/sso_*.json`: `timing_from_check.py`,
  `dynamics_from_check.py`, `rest_from_check.py`, `onset_from_check.py`, `nki_articulation_details.py`.
- Checks and extracts: `audio/vst3/articulationcheck.*` + `mscore/soundlibrarycheck.*` (*Check articulations…*);
  `audio/vst3/pluginextract.*`; `tools/soundlibraries/` readers (`read_check_names.py`, `read_loaded_patches.py`,
  `*_from_check.py`, `read_plugin_data.py`) and `extract_library_files.py` (decrypts nothing, copies no script).
  HANDOFF.md › When a check zip arrives has the procedure.
- Owner reports a problem: suspect Kontakt's MIDI channel (we send 1), editor sizing, sample loading in offline
  export; crackles / slow loads: memory (Kontakt's preload override at 30 kB).

**Live** (LIVE.md): `libmscore/midisync.h`, `liveset.*` (automation from a set), `liveclips.*` + `mscore/liveclips.*`
(Live plays the score; carrier notes 114-127), `mscore/liveclipmodel.*` + `liveclipedit.*` (edit Live clips),
`libmscore/livesetwriter.*` + `mscore/livesetexport.*` (Create Live Set; compare format changes with
`tools/live/test/compare_als_skeleton.py`), `mscore/liveequivalence.*`, `tools/live/` (Max for Live device, Node
tests, `fake_live_server.js`). Plug-in parameter lanes MuseScore plays reach Live through the device (`/ms/params`,
`/ms/pvals`; tables played by `live.remote~` at Live's song position; Kontakt's slots "#001" matched by
`SoundLibraryHost::knownParameterId`). The device keeps each track's lanes, packed (`packLane`, same in the device
and `livesetwriter.cpp`), in `[pattr Lanes]` … stores in the set, so a set plays them without MuseScore; Create Live
Set writes them; stored 2 s after edits pause. Live's own Arrangement automation can't be written in 12.2 except by
a Control Surface script (`tools/live/research/`, not used).

**Playback verification** (VERIFY.md; checks in `audio/vst3/playbackverify.h`): `--verify-playback`,
`--verify-audio`, `tools/playbackverify/read_verify_report.py`, faults via `MS_VERIFY_FAULT`.

## Building and testing

```sh
sudo apt-get install -y qtbase5-dev qtbase5-private-dev qttools5-dev qttools5-dev-tools \
  qtdeclarative5-dev qtscript5-dev libqt5svg5-dev libqt5xmlpatterns5-dev qtwebengine5-dev \
  libqt5opengl5-dev qtquickcontrols2-5-dev libqt5networkauth5-dev libsndfile1-dev \
  libasound2-dev portaudio19-dev libportmidi-dev libpulse-dev libmp3lame-dev \
  libfreetype6-dev zlib1g-dev libvorbis-dev libogg-dev libflac-dev
mkdir build.dir && cd build.dir
cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_JACK=OFF -DBUILD_TELEMETRY_MODULE=OFF \
  -DDOWNLOAD_SOUNDFONT=OFF -DBUILD_WEBENGINE=OFF /path/to/repo
ninja -j4 mscore                    # about 40 minutes on 4 cores
```

- Keep `BUILD_PCH` ON. mtests: `ninja tst_<name>`, run `QT_QPA_PLATFORM=offscreen ./tst_<name>` in
  `build.dir/mtest/libmscore/<name>`.
- **Owner's dev VM**: disk is tight; reuse build dirs, delete throwaway worktrees. A `build.rel` copied from another
  worktree keeps that worktree's absolute paths, so build and test **through the `ninja-<name>.sh` /
  `run-<name>.sh` wrappers** next to the worktrees (`~/MuseScore/ms3fork/`), which bind-mount the worktree over
  the original path, e.g. `./ninja-liveset.sh tst_soundlibrary` and `./run-liveset.sh 'cd
  mtest/libmscore/soundlibrary && QT_QPA_PLATFORM=offscreen ./tst_soundlibrary'`. Outside the mount a test reads
  the other worktree's files.
- A MusicXML score in a test needs `score->rebuildMidiMapping()` before rendering (else channels -1 crash).
- Linux has both `USE_ALSA` and `USE_PORTMIDI`; PortMidi wins, so the owner's Windows path compiles here.
- Headless MuseScore: isolated `HOME`, `~/.vst3/mstestsynth.vst3` linked, `application/startup/firstStart=false`,
  splash / start center off in `MuseScore3Evo.ini`, `.mscz` scores (MusicXML asks about Edwin), no `session` file.
  GUI with sound: Xvfb + PulseAudio null sink (`pulseaudio -n --load=module-null-sink
  --load=module-native-protocol-unix`, own `XDG_RUNTIME_DIR`); under `QT_QPA_PLATFORM=offscreen` the Live link
  never binds its port. Env knobs: `MSTESTSYNTH_INIT_MS` / `_SETSTATE_MS` / `_STREAM_MS` / `_STREAM_MB`,
  `MS_KONTAKT_MAX_VOICES`, `MS_SOUNDLIBRARY_LOAD_THREADS`, `MS_LIVESET_OUT=<file>` (liveSetWrite's set).
- Other tests: `tools/soundlibraries/test_make_setups.py`, `test_extract_library_files.py`; Node in
  `tools/live/test/`. Test data: `test-scores/`, `share/verifyplayback/`, each mtest's folder.

## Tests and known state

- `tst_soundlibrary`: 58 passed, 3 skipped (counting initTestCase and cleanup, 2026-10-01); the skips need inputs
  (`MS_ROUTES_SCORE`, `SSO_NKI` / `SSO_EMPTY`, `MS_EXTRACT_PLUGIN` + `MS_EXTRACT_OUT`: the owner's files).
- `tst_liveequivalence` (links mscoreapp; uses tst_soundlibrary's test synth): 9 passed, `dumpEvents` skipped (a
  tool: `MS_DUMP_SCORE`, `MS_DUMP_MAP`, `MS_DUMP_OUT`). `tst_liveintegration` 31 passed, `tst_keysig` 8 passed
  (2026-10-01). `tst_tuning` (13), `tst_tempochange`, `tst_phrasemark`, `tst_playability`,
  `tst_timesig::removeRedundant`: passed when last run on their branches.
- `tst_midi`: **68 of 73 fail, and did before** the sound-library work (references predate the MS4 note model;
  same-tick order varies, `ms4Parts` keyed by pointer). Compare with the previous commit's build instead.
  `tst_timesig::timesig05` fails since 5f65a1d (fermata `<timeStretch>`).
- Untried with Kontakt / SSO unless docs say so; LIVE.md lists what only real Live can show.
- The owner's "SSO Articulation Map" review page (https://claude.ai/artifact/Y9dDEm5gpEjtpjjQmM9qBm, collection
  `reviews`): read with ArtifactData where available, else ask for the marks; fix through `gen_spitfire_sso.py`.
