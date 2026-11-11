# CLAUDE.md: guide for agents working on this repository

A fork of the MuseScore 3.7 community fork (Jojo-Schmitz/MuseScore 3.x @ 29e066cd, history squashed). Goals:

1. **MS4 playback**: MuseScore 3 plays as MuseScore 4.7.5 does (FluidSynth setup, `MS Basic.sf3`, reverb, note
   model, dynamics). Essentially finished.
2. **External sound libraries**: chosen parts play through the owner's **Spitfire Symphony Orchestra (SSO)** in
   the free **Kontakt Player 8** on **Windows**, hosted as VST 3, sent over MIDI, or played by **Ableton Live 12**.
   Each note's articulation comes from the notation.

Around them: built-in tuning, the playability checker, gradual tempo changes, phrase marks, scale-degree input.

**The owner's principles** (interpretation is the composer's, rules read the marks, levels from multi-source data,
blind tests decide): **docs/PRINCIPLES.md**. Check every playback change against it.

Keep this file a **current-state guide**: update it when architecture, build steps, rules or known issues change.
What was done and measured goes in commit messages (detailed on purpose: read `git log` first). **Picking up the
work? Read `HANDOFF.md`.** Background (why a number is what it is, the owner's runs, measurement narratives,
CI history): `docs/HISTORY.md`, read only when you need the background of a topic.

## Rules

- **Files survive a round trip to MuseScore 3.6** (the owner): fork → 3.6.2 → fork unchanged. Test:
  `tools/roundtrip/roundtrip2.py --fork <mscore> [--mtest N] [--list file] [scores…]` (a build dir's mscore through
  `tools/roundtrip/portable_fork.sh`) (XML, PNG pages, MIDI). Where 3.7 reads or writes
  differently, follow 3.6 (legacy style defaults, `MStyle::isDefault` within 1e-6, `MStyle::notInMuseScore36`,
  tuplet `<Number>`, `updateInstrumentId`). New per-score state goes in **metaTags** (3.6 keeps them), left out
  when default. Known leftover: some MuseScore 1/2 imports' XML changes once (pages, MIDI identical). (MIDI
  export's same-instant order came from `ms4Parts`' pointer order, fixed 2026-10-04; round trip not re-run.)
- **Readings of music go on a review page**: when work relies on an agent's reading of sheet music, a legend, a
  screenshot of a plug-in's key/articulation list or a contact sheet, publish a claude.ai artifact with each
  source picture (or crop) next to the reading and its use, before it changes code or the map. Symbols in
  Bravura (subset `fonts/bravura/Bravura.otf` with `pyftsubset`, embedded); say what couldn't be read; open with
  a "What to check" list. Example: https://claude.ai/artifact/CPfVu3qdHzMr5xPKiJFD2Y.
- **Live 12.4 is the target** (the owner, 2026-10-02: first "make sure that we don't build any features that can't
  be run on 12.2", then "I updated my Ableton version across all devices to 12.4"): nothing may need a Live later
  than 12.4; 12.4's Live Object Model (e.g. `Envelope.create_event`) may be used. Create Live Set still writes 12.2's
  format (`livesetwriter.h`), which 12.4 opens; move it to 12.4's only for a reason. The test VM ran a Live 12.4.6
  trial (from 2026-10-02, 30 days), the owner's version; on 2026-10-22 Live updated itself there to 12.4.7 (its automatic
  update; OPEN_ITEMS 31a). Check against real Live with `tools/live/realcheck` (docs/TOOLS.md › 5; LIVE.md › Checked in
  real Live).
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
- **Audit what goes out, test the tests** (the owner, 2026-11-01, "learn from this and apply them"; method:
  `~/.claude/ai-eval-method.md`, scaled to playback work):
  - **Before research numbers reach the owner or anyone outside**, run one `stats-auditor` agent over the text:
    each number has to support its sentence (n, one source, the uncertainty, assumption / measured / computed).
  - **Before a comparison or demo goes out**, run one `research-critic` agent on its design, with the artifact
    only, not my conclusion. It checks that a reference (e.g. one recording) isn't used as ground truth.
  - **Blind A/B pages**: say on the page, before listening, which answer changes which default. Include catch trials
    (A and B the same clip: the owner, 2026-11-01, "if there are any surprising results, I would maybe doubt my hearing"),
    the reference recording where one exists, ~8 s clips; a surprising result is re-tested before it changes anything. Store the answers
    where I can read them back (ArtifactData), never in localStorage alone. Keep the letter → variant key off the
    page.
  - **A check is trusted only once it has caught a known fault**: positive controls (planted defects, a model
    wrong by a known amount) before its results decide anything (the audit's `plantedControls`, the detector
    regression; the heard check still has none: OPEN_ITEMS).
  - A rule from performance practice needs measured data from several performers. A treatise or one recording
    is a lead, or an editorial mark in the score, never a default.
- **Interpretation is the composer's** (the owner, 2026-11-01: "leaves choice to the composer ... the same thing with
  phrasing"): style-dependent choices (detaching unmarked notes, phrase shaping) are marks the composer puts in the
  score (staccato, phrase marks), played well; never an automatic default.
- **Balance and dynamic levels come from real data** (the owner, 2026-10-14: "I don't trust my ears, I prefer all the
  balance levels and the dynamic levels loudness to be based on real data"): the balance between sections / instruments
  and how loud each marking plays are set from measurements of real players and orchestras, chosen between sources by
  data quality (one consistent method across instruments, independent corroboration, player agreement), with the
  uncertainty stated; no listening test decides them (earlier ABX results, e.g. the basses of 2026-10-11, don't override
  data). A gap without data is marked as an assumption.
- **The owner decides what is heard** (everything else: timing, techniques, tone): measure, then offer (often "try both, decide by ear"). Kontakt patches keep
  the library's defaults: values change only through MuseScore (Controllers, automation), never saved from
  Kontakt's window; every patch MuseScore sets up gets 512 voices (approved).

## Branches and CI

- `main` is the default branch. **Work on a branch**; the coordinating session merges finished, tested work by
  itself and its subagents into `main` without asking (the owner, 2026-10-02); a subagent never merges into `main`
  itself. Anything incomplete or changing an owner-decided value waits for the owner. Check `git log` first.
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
- **Before pushing anything meant for a Windows build, run `tools/check_windows_names.sh`**: names windows.h defines as
  macros (`near`, `ABSOLUTE`, `ERROR`, `IN`, `OUT` …) compile on Linux and fail on MSVC (two failed runs, 2026-10-03).
- Also: `tool_extract_library_files.yml` (ExtractLibraryFiles.exe), `verify_playback_owner_pc.yml` (optional
  self-hosted runner, not set up).


## Architecture map

Read a file's header comment before changing it: it holds the design. Full detail of each item: docs/HISTORY.md
Part 1; the dated narratives and superseded values behind the items below: docs/HISTORY.md › "Moved from CLAUDE.md,
2026-11-11". Settings: **docs/PLAYBACK_SETTINGS.md** is the inventory (every default, preset value and history).

**MS4 playback.** `libmscore/ms4playback.*` (note model), `ms4tables.h` (generated by `tools/ms4playback/gen_tables.py`),
`rendermidi.cpp` (`collectMeasureEventsMs4`, `renderMs4Dynamics`; dynamics method 3 is the default),
`thirdparty/fluidsynth`, `effects/musereverb`, `synthesizerstate.cpp`. One deliberate difference: an MS3 hairpin with its
own velocity change plays as in 3.6 (`ms3HairpinVelocity()`, preference `application/playback/ms3HairpinVelocityChange`;
`MS4_STRICT=1` disables, for exact-MS4 A/B regressions).
`mscore/playbackmode.h` (MS3 / MS4 / each library engine by name, all parts) and `libmscore/partplayback.h` (per part,
metaTag `partPlayback`: "library:<engine>" a part's own library, an old "library" the global one).
- **Engine lane** (`libmscore/enginelane.h`; HANDOFF.md › Engine lane): an automation lane "engine" per part (MS3 / MS4 /
  a library, steps); each note plays on the engine at its tick, legato never across a switch, a lane's library loaded for
  the whole score (`playingModes` in soundlibrary.cpp), rendermidi.cpp › Engine lanes; fades stored, not yet played;
  ignored for Live / MIDI out.
- **Leading lane** (`libmscore/leadinglane.h`; the owner: which line leads is the composer's choice): an automation lane "Leading"
  per part (Off / On steps, metaTag `automation`); where On, the staves get a darker band (view only: `LeadingLane::bandRects`).
  `[levels] leading` (1 by default and in Recommended, 0 in Library default; the owner: "the simple 3.5 dB now, we'll refine as we
  go") plays a leading part's library notes louder: map `<Instrument leadingLevel>`, else `[levels] leadingDb` 3.5 dB (a lead,
  not yet multi-source), by its gain stage and CC11 on the others (`TrackDelays::Delays::leading`); built-in parts by velocity.
- `tempochange.h` (rit./accel. lines, metaTag `tempoChanges`).
- **Phrase marks** (`slur.h` › Phrase marks: non-legato slurs, Alt+S, metaTag `phraseMarks`; the owner: "phrasing is just 2
  variables, swell location and amount"): **Amount** (dB louder at the peak than at the ends, 0-12; a new mark starts at 3.3,
  `PhraseMark::DEFAULT_AMOUNT`, a rough lead from piano data; one saved before amounts loads at 0) and **Peak** (Auto: the
  highest note if unique and within 25-75 %, else the middle; or 0-100 %). Continuous as a hairpin, in every preset (no
  setting): MS4 and libraries in the part's dynamics map (`Ms4::Dynamics::addSwells`, `MidiRenderer::swellLaw`, so Live's clips
  too), MS3 by velocity (`phraseDb`); a peak grip (`SlurSegment::PEAK_GRIP`), Inspector › Slur (`Pid::PHRASE_AMOUNT` /
  `PHRASE_PEAK`); no Ending (a slowing is a rit. line). docs/PLAYBACK_SETTINGS.md › Phrase swell.
- `articulation.h` › MarcatoLevel: every articulation sign's and technique staff text's level in dB (Inspector › *Level*,
  ±35.5; a chord's add up, `MidiRenderer::levelOf`; metaTag `marcatoLevels`); library: velocity by the measured curve or
  CC11 / CC1 per note (`libraryNoteLevels`); built-in: SoundFont 2's law.

**Notation.** Custom key signatures follow each staff's clef (`KeySigEvent::forClef`, `Staff::keySigEventForClef`, *Tools ›
Adapt Key Signatures to Clefs*, `mscore/keyedit.cpp`). Deleting a restating time signature keeps the measures
(`Score::cmdRemoveTimeSig`). Numpad 1-7 enter scale degrees (`Score::scaleDegreeStep`; on Linux/xcb `ScoreView::event`
takes the keypad keys, `numpadDegreeAction`; `Shortcut::load` gives new actions their defaults in an old shortcuts.xml).
Continuous View has a bar ruler (`mscore/barruler.*`, View › Bar Ruler, view only). Switching the view mode keeps the
selection's measure (or the one at the view's centre) centred horizontally, its staff at the same height (`ScoreView::viewAnchor`
/ `showViewAnchor`, from `MuseScore::switchLayoutMode`).

**Score scripts in Lua** (HANDOFF.md › Lua console; merged into `main`; the owner has not yet reported trying the window): a
script queries the notes of a score and adds marks, `findall(strings, 1 <= measure <= 16, slur, slur.index == 1).add(marcato)`
(also `first` / `last` / `next(m, …)` / `count`).
- **`libmscore/script/scorescript.lua`** is the language (its header is the design): one Lua library for MuseScore, the
  browser playground and a plain `lua`, Lua 5.3 syntax; a source rewrite makes each condition argument `function(_ENV) return
  … end`; conditions are true / false (a mark = `has(mark)`); the unit is the note (tied notes one note, grace notes only with
  `grace`); marks per chord and idempotent; a whitelist sandbox, an instruction limit no `pcall` swallows; C-style `|` `||`
  `&` `&&` `!` `!=` accepted anywhere (bitwise given up); a preview on shadow marks.
- **`libmscore/script/scriptengine.*`** (`ScriptEngine`, in libmscore so that mtests and the command line need no GUI):
  `snapshot(score)` walks the master score into plain data (sections come from instrument id / family / group, never part
  names); `runOnScore` = snapshot + run + the edits as ONE undo step (rolled back when the script fails), only visible marks;
  the library embedded as bytes (`embed_lua.cmake` → `scorescript_lua.h`). Lua 5.4.9 is vendored in `thirdparty/lua` (MIT,
  `lua_standalone` for the tests).
- **`mscore/scriptconsole.*`**: View › Script Console (**Ctrl+Shift+P**, moved from Plugin Creator, which keeps its menu item;
  `Shortcut::load` hands the key over only in a user's file that still had the old default), a bottom dock in tabs with the
  Timeline: Console (REPL, variables persist) and Scripts (editor + saved `.lua` files in the Scripts folder of Preferences ›
  General, `PREF_APP_PATHS_MYSCRIPTS`, `Documents/<app>/Scripts`; no fallback folder); highlighting, completion, error
  underline, hover help, live preview (`ScoreView::setScriptPreview`, on a worker thread). Headless: `mscore --lua script.lua
  score [-o out]`.
- Tests: `lua_standalone libmscore/script/test/run.lua` (161 checks, no MuseScore), the playground's `test/run.js` (Fengari;
  `tools/scripting/update_playground.py <page>`), `tst_luascripting`.
**Tuning** (`libmscore/tuning.h`): temperament (metaTag `temperament`, the Tuning plugin's JSON) + accidental rules
(microtonal, HEJI, Johnston, stacked accidentals as hidden `Symbol`s) + the note's own tuning. `tuningtables.h` from
`tools/tuning/gen_tuning_tables.py`; `mscore/tuningdialog.*`; plugin API `playbackTuning`, `microtonalTuning`. The
Microtonal Tuner plugin is frozen: update or drop `tst_tuning::pluginParity` when needed.

**Playability** (PLAYABILITY.md): `libmscore/playabilityrules.*`, `playability.*`, `playabilitydiagram.*`,
`mscore/playabilitypanel.*`.

**Sound libraries** (`libmscore/soundlibrary.h`; map format `share/soundlibraries/README.md`)
- `SoundLib`: loads maps, matches instruments by `Instrument::getId()`, builds the `Want` from MS4 articulations and
  staff text, `choose()` (extras `with=`, kits `kit="1"`, `prefer`, shorts by `length`/`from`), `routes()`.
- **Several libraries in one score** (soundlibrary.h › engines, README › Several libraries; the owner: "natively integrate Vial
  Instrumental as another playback engine, like we have now for ms3, ms4 and sso"): every map is an engine by its name (a map
  "<engine> (<variant>)" a variant: Vial Fuse, Preferences › I/O, `io/soundLibraryVariants`). `SoundLib::setEngines` / `engine` /
  `libraryOf(part)` / `scoreLibraries` / `allRoutes` (each `Route` with its `library`; the current library's routes from slot 0,
  so a one-library score renders byte for byte as before). Plug-in output only (MIDI out, Live: the current library alone,
  other engines' parts MS4, `otherEngineParts`; LIVE.md › What still differs). Per library: the renderer's `LibPart::library` /
  `libAt(slot)`, calibration by `calibrationFor`, the host's instances, contextlevel's volumes, the master trim
  (`trimLibrary`). The global library's only: even levels' measurement, the audit, the verification, Live. An engine whose
  plug-in isn't found (`MS_VST3_PATH` adds folders) is grayed (`soundEngineAvailable`) and its parts play MS4;
  `PREF_IO_SOUNDLIBRARY_PLUGIN` applies to the global library or a library whose plug-in file it names.
- **The SSO map is generated, never hand-edited**: `python3 tools/soundlibraries/gen_spitfire_sso.py
  <reaticulate>/userbanks/Spitfire/Spitfire-Symphony_Orchestra.reabank "share/soundlibraries/Spitfire Symphony
  Orchestra.xml"` (needs a clone of github.com/jtackaberry/reaticulate). Inputs: the script's tables (`I`, `T`,
  `SPITFIRE_ADD/DROP/RENAME`, `DRUMS`, `KEYSWITCHED`, `LENGTHS`, `EXPECT`) and the measured `tools/soundlibraries/
  sso_*.json`. Diff the XML after: only intended entries may change.

**Renderer and playback settings** (`rendermidi.cpp`; `libmscore/playbacksettings.*`). Each library event carries patch,
switch and route; `finishLibraryEvents` routes, copies controllers to every patch ahead of the notes, applies early
starts, lanes and bends. Dynamics on CC1; listed shorts take velocity. Legato transitions play on their beat, pedal
changes one tick after their chord; the technique comes from the notation.
- **Rule: Recommended plays the measured adjustments the owner approved, Library default none** (the owner, 2026-10-12,
  "approved measured ones one at a time, by ear"). Every adjustment is a setting, off in Library default, on in
  Recommended only once the owner approved it. Settings that were removed (2026-10-07: "remove all settings not currently
  being used") are listed in docs/PLAYBACK_SETTINGS.md; old ini keys and metaTags are ignored (`REMOVED`).
- **Configuration**: built-in defaults → `<dataPath>/playback.ini` (written with every key when missing; Edit › Reload
  Playback Settings) → the score (metaTag `playbackSettings`, plus the older onset-early / lanes metaTags). Read a setting
  with `Playback::value(id, score, mapValue)`; a new adjustment gets a `DEFINITIONS` entry, a row in the inventory and a
  layers test. UI: Mixer › Advanced Options… › Playback adjustments (`mscore/playbacksettingswidget.*`). **Presets**
  (Recommended / Library default: `Playback::presets()`, a rewrite of the whole playback.ini: preset values + every other
  key at its default, the old file kept as .bak; the widget's Preset box; › Presets).
- **Levels** (default = Recommended; Library default 0 unless said; detail and sources in docs/PLAYBACK_SETTINGS.md):
  - `[levels] calibrated` 2 (a technique on velocity as loud as the part's Long at the same dynamic, plus D dB from real
    players, the map's `<Articulation heldOffset>`, `SoundLib::calibratedVelocity` / `techniqueOffsetDb` / `loudEndDb`, plus
    MS4.7.5's offset and the Inspector's levels); `[levels] context` 1 (the level rule; the owner: the same written dynamic
    equally loud in every string part and technique: one gain per slur from `<Articulation contextLevel>`, by CC11 and patch
    volumes; `libmscore/contextlevel.h`).
  - `[levels] sectionBalance` 2 (the Clark & Luce balance; the owner: levels from real data): `<Instrument sectionLevel2>` from
    Clark & Luce 1965 as sound power, TU Berlin's directivity and the SSO side (`section_balance_cl.py` →
    `sso_cl_section_balance.json` → `SECTION_LEVEL2`), plus `ANECHOIC_SHIFT` (flutes, oboes, horns, trumpets; flutes and oboes
    marginal, n = 2). Siblings, gap patches, harp, timpani, piano etc. are assumptions or single sources: › Section balance and
    docs/BALANCE_LOG.md. 1 = the TU Berlin balance (`<Instrument sectionLevel>`).
  - `[levels] realRange` 2 (the owner: "let's use the section data wherever it's reliable"): each note plays a real player's
    change from mp at the written CC1 less SSO's own, by CC11: steps x S / 5, S the map's `<Instrument realSteps>` (Clark &
    Luce Table I, equal steps an assumption); 1 = the Meyer rule (`realPlay`, `realSpan`, `longLevel`: `real_range.py`); the
    part's largest R by one render (`ContextLevel::realRangeTops`) goes on the gain stage. `evenKeys` 1 (a wind / brass Long's
    key more than 1.5 dB off its neighbours' trend plays the excess back: `<Articulation keyLevels>`, `even_keys.py`).
  - `transitionLevels` 2 (a fix: a bowed note-on's CC1 / CC11 step held to the leave and ramped to the arrival,
    `libraryTransitionTails`); `attackLevels` 2 (`attackSwitch`, `libAttackOnset`); `hallGuard` 0 in both presets until the
    owner decides (`libraryHallGuard`, the audit's HALL); `divisi` 1 (see Chords); `micCompensation`, `lowCut`, `evenLevels`:
    below.
- **Slurs and Performance patches** (the five string sections' Performance legato; detail › Plain Performance etc.):
  - `[slurs] quick` 2 (only under staff text "non performance"): short slurred notes on Long (Rachm.), early by its own onset,
    `[slurs] quickLevel` 1 as loud as the held one (`quickLevel`, `quickDb`; Violins 1 / 2 only, `RACHM_UNUSED`).
  - `[slurs] performance` 1 (the owner: Recommended only): slurred notes of the five string sections play as under staff text
    "performance" (`Want::slurPerformance`, `choosePerformance`; "non performance" opts out); `[slurs] transitions` 1 (fingered
    inside a slur, bowed for a slur touching the one before; `performanceNote`, `slurAt`); `[slurs] smoothStart` 1
    (`attackOnset`); `[slurs] transitionsEarly` 1 (by the modelled arrival; capped so the note before keeps keepMs / 15 ms).
  - `[slurs] plainPerformance` 1 (the owner, "C" by ear): every bowed note of such a part on its Performance legato
    (`choosePlain`), velocity 64, at its written start, the patch's events `<Instrument plainShift>` early as a track delay
    (`TrackDelays::Delays::plain`); `plainJoins` 0 everywhere; `plainAuto` 1 (smooth after a rest, bowed on each new bow,
    fingered inside a slur, spiccato for staccatos; 2 = new bows early by `bowedEarly`; 3 / 4 = re-attack variants for the
    owner's A/B only); `plainTiming` 0 (rejected by ear); `plainAccents` 2 (accented attack at real players' +5.0 dB,
    `PLAIN_ACCENT_DB`).
  - `speedFromWritten` 0 and `fixedTransitions` 0 in both presets (CC119 from the written spacing; Kontakt random sources at
    intensity 0). `microtoneBends` 3 and `runBends` 1 (the owner: "the fix is obv better"; `microtoneSpelling`, `midEarly`,
    `runBendPath`, `RUN_SLEW_C`); `fastTransitions` 1 (`libraryFastTransitions`); `[notes] transportReset` 1
    (`playbackScriptValues`). The Performance portamento plays at velocity 5.
  - Off (Library default), SSO slurs play the All techniques longs, each note its own attack; a legato transition needs an
    articulation with `legato` first (`Articulation::playsTransitions`), which the SSO map has only under modifier `performance`.
- **Performance technique** (`libmscore/performancetechnique.h`): attack smooth / spiccato / accented, transition portamento /
  fingered / bowed, Auto from the notation; Inspector › Note, every selected note at once, shown only when one plays on a
  Performance patch (`NoteLane::onPerformancePatch`); a choice resets the note's own values. Each note's own velocity and join
  (`Pid::LIBRARY_VELOCITY` / `LIBRARY_JOIN`; a join down to the map's `legatoGap` still a transition) is edited in the
  **Velocity and Join lanes** (`libmscore/notelane.h` the model, `mscore/notelanes.h` the lanes; Ctrl+C / Ctrl+V copy stems'
  values by note order into the same lane only); metaTag `performanceTechniques`, which also holds each note's **own
  technique** (Inspector › Note › Technique, `Note::libraryTechnique`: replaces the notation's choice in
  `SoundLib::chooseNote`). Map `attacks` `transitions` `reattack` `reattackGap` `overlap` `avoid` (a faulty round robin's
  velocities, steered to a clean one: `sso_bad_samples.json`, `[notes] avoidBadSamples`) `legatoGap`: the note's velocity, and
  the note before ending `overlap` ms after it or `reattackGap` ms before it (`performanceNote`, `libJoins` in
  `finishLibraryEvents`; not across tuning lanes).
- **Held notes start early** by their measured onset (`<Articulation onset>` by pitch, `<Onset early>` 100; metaTag
  `soundLibraryOnsetEarly`; every measured technique; the owner: plain Long for everything, lined up within Rasch's 30-50 ms
  between players; `[heldNotes] early` 0 plays them as written, `byPitch` 1 each by its pitch's); the note before on its patch
  keeps `keepMs` of its length. A transition's measured delay (`legatoDelay`, `libFastDelay`) times only a bent transition's
  glide. HANDOFF.md has the open timing items.
- Per-note levels (a marcato's, track levels) share one CC11 / CC1 schedule per route, `libraryNoteLevels` (`libLevels`; dB
  add up); a chunk's end undoes none of them, nor even levels' glide: the level in force on a route stays until the
  route's next note-on, in a later chunk too (`MidiRenderer::libCarry`; matters only at a repeat segment's boundary and at
  a playback window's start: › Chunks).
- **Staccato length** (`[shorts] staccatoLength` 1 in Recommended, 0 in Library default; the owner heard A/B clips): a library
  staccato (staccatissimo; not a portato) is held no longer than real players sound one (measured only for notes written
  0.15-0.37 s; a longer one staying this short is an assumption): the map's `<Instrument staccatoLength staccatissimoLength>`
  (`gen_spitfire_sso.py` STACCATO_RULES, RESEARCH.md 2.5a) caps the meant length (`SoundLib::staccatoKind`, `chooseBases`) and the
  note-off (`config.ms4Dur`, so Live's clips too); › Staccato length.
- **Even levels** (`libmscore/evenlevels.h`, `mscore/evenlevelsjob.h`; the owner, the blind test's Glide; `[levels]
  evenLevels` 1 in Recommended, 0 in Library default): SSO's Performance legato swells inside slurs by context, so *Tools ›
  Even Out Levels…* / `--even-levels <score>` renders the slurred passages through the hosted plug-ins, measures the perceived
  envelope and fits a per-note CC11 glide flattening it to each slur's median (`EvenLevels::nodeTime`, `chooseRound`,
  `roundTooBig`, `unseenNotes`). Each note keeps its value and a hash of its rendered context (`Note::evenLevel` /
  `evenContext`, metaTag `evenLevels`): a changed context is stale (Inspector › Note › Even level), not played.
  `MidiRenderer::libraryEvenLevels` glides on top of the CC11 schedule (the gain stage adds the largest value back,
  `TrackDelays::evenDb`); Live's clips and the plain set carry it.
  - **Slow slurs are skipped** (the owner, blind test; HANDOFF.md › Slow slurs need no correction): a passage whose every
    joined interval is >= `EvenLevels::SLOW_IOI_S` (350 ms, `slowPassage`) is neither measured nor corrected.
  - **Measured in the background near edits** (`libmscore/evenauto.h`, `audio/vst3/evenmeasure.h`, `mscore/evenautomeasurer.h`; the
    owner: a note plays its measured value or playback waits for it, never a predicted one): after an edit (`MuseScore::endCmd`)
    and at score open the score is rendered once and notes without a current value (user cache `<setups folder>/even cache.json`)
    are measured in windows on plug-in instances of MuseScore's own; `Seq::start` waits (status-bar countdown, Stop cancels) only
    when a note being measured is in what is about to play. Untried with Kontakt / SSO in MuseScore. Open proposals:
    docs/PLAYBACK_SETTINGS.md › Even levels.
- **Mic compensation** (`libmscore/contextlevel.h` › Mics; the owner: "mic changes then only affect tone and sense of space,
  never the balance"; `[levels] micCompensation` 1 in Recommended, 0 in Library default): every level is measured at the
  patch's default mix (SSO: the Tree alone at 127); a part whose mic Controllers or their lanes give another mix plays C =
  -(that mix's level) more (map `<Instrument micLevels micDefault>`, `<Mics fader mix>`), by the patches' volumes (`Volumes::micTop`)
  and, for automated mics, by CC11 per note (`trackLevel`); Live's sets and clips carry both. Measured on the VM
  (`mic_levels.py` → `sso_mic_levels.json`); other extras take their main's (an assumption); error <= 1.4 dB.
- **Gain stage and master trim** (`libmscore/contextlevel.h`; the owner: "yeah sure let's go with utility"): every patch's levels
  above play on its own gain stage, `TrackDelays::gainStageDb`: hosted, the slot's gain (once per instance with tuning slots);
  the plain set and route set, a stock Utility named "MuseScore gain" after the Kontakt (±35 dB; `LiveSetWriter::kontaktGains`;
  the fader stays the Mixer's, the owner's); MIDI out by CC7 (`TrackDelays::midiVolumes`). One **master trim** per score
  (`ContextLevel::masterTrim`) lowers every patch only as much as the loudest moment needs: power sum of gain + CC11 +
  calibrated level + the map's `<Dynamics crest>` (14.5 dB, `sso_crest.json`), trim = max(0, peak). HANDOFF.md › Gain stage.
- **Low cut on string sections** (`[levels] lowCut` 2 in Recommended, 1 the fixed cut, 0 in Library default): every patch of
  SSO's five string sections plays through a 4th-order Butterworth high-pass at the map's `<Instrument lowCut>` (Violins 150
  Hz, Violas 100, Celli 50, Basses 25; `gen_spitfire_sso.py` LOW_CUT): `Vst3Synth::setLowCut` / `setExportLowCut` (from
  `SoundLibraryHost::applyMixer`, `SoundLibraryExport::setExportMix`). **Live**: the plain set and route set put a stock EQ
  Eight "MuseScore low cut" (`LiveSetWriter::LOW_CUT_NAME`, `eqLowCutDevice`) after the Kontakt, before the Utility; none with
  lowCut 0; unconfirmed in real Live (`tools/live/realcheck/lowcut_response.py`); MIDI out none. **The following cut, lowCut
  2** (the owner approved it by ear; `libmscore/followcut.h`): the cutoff = max(the map's lowCut, 0.77 x the lowest fundamental
  sounding or released < 2 s ago on the part's routes), smoothed: `MidiRenderer::libraryLowCut` gives each route `ME_LOWCUT`
  events (Hz in `tuning()`; `Seq` sends them as controllers); `Vst3Synth` smooths and filters the slot (a slot without them
  plays lowCut 1 bit for bit). Live: the plain set's EQ band 1 / 2 Freq envelope (`Track::lowCutEnvelope`), unconfirmed; the
  route set the fixed cut. Tests' ini: lowCut 1 unless set. LIVE.md › The plain set › Low cut.
- **Chunks** (a fix, no setting; header comment of `MidiRenderer::renderChunk`): Seq plays a score rendered in chunks
  (`getChunkAt`, 10 measures at least), the export, Live's clips and the plain set in one chunk a repeat segment; the chunks
  play exactly the whole render's events. With library parts and `minChunkSize > 0` each chunk is rendered inside a **window**
  (`libWindow`, `WINDOW_BEFORE_S` 6 s / `WINDOW_AFTER_S` 4 s) and only the events from the chunk's start to its end are kept
  (`libCut`); what lasts longer than the window comes in from the window before (`libSnapshotAt` into `libCarry`,
  `LibCarry::evenRuns`, `libSpelled`). Proof: `tst_liveequivalence chunkCompare` + `tools/playbackverify/chunk_diff.py`,
  `tst_soundlibrary::chunkedEqualsWhole`; `MS_CHUNK_WINDOWS_OFF=1` renders each chunk alone, to compare.
- **Play from a tick** (`libmscore/playstart.h`, `Seq::setPos`): notes that start early have their note-on, switch and controllers
  before their beat, so the transport starts at the earliest note-on of a note written at or after the tick (`PlayStart::start()`,
  a pre-roll) and skips the previous notes' note-ons/offs there (`skips`); the cursor stays at the tick.
- **Clicked / entered notes** (`libmscore/notepreview.h`, `Seq::playNotes`): by the part's effective mode, MS3 3.6's click (velocity
  80), MS4 and library the note's own events from its playback chunk, sent at once; stopped only (› Clicked and entered notes).
- **Dynamics calibration** (*Check articulations* › Dynamics or `--check-dynamics`) → `dynamics.json`; every patch it lacks
  comes from the shipped `share/soundlibraries/<library>.dynamics.json` (SSO's from `sso_sound_dynamics.json` by
  `calibration_from_sound_dynamics.py`, the string sections' Long from `sso_string_dynamics.json`; `SoundLib::readCalibration`
  merges them). Read for calibrated levels and the Inspector levels. Curves may be per pitch (`DynamicsCurve::atPitch`, `raise`). The strings' Long against CC1
  (`sso_cc1_curves.json`, `sso_cc1_sweep.py`) feeds the section balance and, by `real_range.py`, the map's longLevel; not read
  by MuseScore.
- **Controllers**: map `<Controller>`, set **only by automation lanes** (the owner: "remove this entirely, we'll use
  automation instead … just make it like ableton"). A **flat lane** (`Lane::flatValue`) is the controller's setting
  (`libmscore/partcontrollers.*`): a CC ahead of the notes, a plug-in parameter set on the instance at each sync and live as
  the lane is edited (`audio/vst3/librarycontrollers.*`, `SoundLibraryHost::applyPartSettings`); any other lane as automation.
  Unautomated: the map's default, else the patch's own (`PartControllers::unautomatedValue`: the setup's value at its last
  load, `parameter values.json`, else the map's **`patchDefault`**, `sso_controller_defaults.json`), drawn as Live's **dashed
  line**. **Live** (the owner: "Option B"): Create / Update Live Set write a flat plug-in-parameter lane as the Kontakt panel
  parameter's plain value (no envelope; a changing lane as automation); the read-back takes a panel value turned in Live into
  a flat lane (`LiveTracks::knobs`; both changed: `Automation::merge`'s dialog). The old metaTag `partControllers` becomes flat
  lanes at load (`PartControllers::migrate`), never written. **Automation**: `libmscore/automation.*`, metaTag `automation`
  (points step / linear, a ramp may carry Live's Bézier points; two points at one tick are a jump, `Lane::events`). Every lane
  is editable; `Lane::playedByLive` says whether Live's set plays it; an import merges (`Automation::merge`, newer edit wins).
  **Editor**: `mscore/automationlanes.*` (Continuous View, a header row under every library part, folded / open at will:
  `mscore/lanefolds.h`; lane heights `mscore/laneheights.h`; Live's gestures); model `Automation::Edit`, one undo step per
  gesture (`undoWrite`; heard at once while playing: `Seq::renderAgainPlaying`); a Dynamics lane replaces the notation's CC1 from
  its first point. **Time selection** (Live's): Ctrl+D copies every lane of the part and each note's attack / transition /
  velocity / join to the bars after (`Automation::duplicateTimeSelection`, `NoteLanes::duplicateSelected`).
- **Read from SSO's scripts** (docs/SSO_SCRIPT_KNOWLEDGE.md): `<Instrument avoidCC="112 113">` on Trumpet / Tenor Trombone Solo -
  Total Performance (`MidiRenderer::libraryAvoidCC`, the audit's AVOIDED CC); SSO's machine-wide shared settings
  (`Share__Settings.nka`) checked by the map's `<PluginSettings>` as a patch loads (`SoundLibraryHost::checkPluginSettings`) and
  in every render's preflight (`RenderPreflight::setPluginSettings`). Tightness measured, not offered (› Tightness).
- **Percussion**: `<Instrument keyShift="12">` on the Timpani (`KEY_SHIFT`, `libNote.key`), `<Instrument maxVelocity="126">` on the
  13 metal patches whose Kickstart option "Roll on high vel." is ticked (`MidiRenderer::libraryMaxVelocity`; a correctness fix).
  `<Drum key name>` lists every hit of SSO's kits (`sso_percussion_hits.json`); a technique off at the defaults is `<Drum
  name default="off"/>`. The nine `"<patch> (all on)"` `<Patch>`es set Kickstart's arrays whole (`KontaktSetup::unpurgeSwitchedOn`,
  test `kontaktKickstartUnpurge`). Mics on 3-mic patches are Close, Tree, Ambient.
- **Vial Instrumental** (the owner approved; HANDOFF.md › Vial Instrumental): `share/soundlibraries/Vial Instrumental.xml`
  (hosted `Vial.vst3`) and `Vial Instrumental (Fuse).xml` (`Vial Fuse.vst3`), generated by `tools/soundlibraries/gen_vial.py`
  (ids from the SSO map): the owner's 13 "SSO imitations" Long presets, one articulation for every technique, CC1 dynamics,
  CC21 vibrato on ten instruments. A **preset library** (`<Files presets>`; README › Preset libraries): the patch's state is its
  preset file's bytes, read from Vial's data folder at every load (`MS_PRESET_FOLDER`); never the presets in the repo. Levels as SSO's: SSO's realSteps / realSpan / realPlay and a measured
  offset on longLevel / sectionLevel(2) (`vial_levels.py` → `gen_vial.py`; a Vial Long at SSO's level on loud1sDb, within 0.5
  dB; › Vial levels); no other SSO measurement. An engine as SSO is (› Several libraries). Live / MIDI out untried.
- **Ample guitars** (HANDOFF.md › Ample guitars; **dormant: the owner is not buying them**; the map is not shipped, it is
  `tools/soundlibraries/ample/Ample Guitar M.xml`, kept for the tests and a later purchase; to ship it again, move it back into
  `share/soundlibraries/`): generated, never hand-edited: `python3 tools/soundlibraries/gen_ample.py [--product <code>] <amphost
  dump> [out]` (the dump stays outside the repo); a new guitar: docs/AMPLE_PIPELINE.md. Hosted `AGM.vst3`, not Kontakt; state-only
  settings (`<Controller state values>`, `PartControllers::stateValues`) written into the part's state as it loads; CC7 plays as
  **M-Master Vol** (`MidiRenderer::libraryHostedParameters`). **Guitar notation**: `MidiRenderer::libraryGuitar`
  (`libmscore/guitarplayback.cpp`, its header is the design), over parts whose patch has the map's `<Guitar>`: strums, string keys,
  capo, harmonics, slides, HO / PO, FX noises; settings `[guitar] strums` / `softStrums` / `pinchHarmonics` (0; not in Recommended).
- **Microtones**: Kontakt ignores VST 3 note tuning. **Tuning lanes** (patch copies per tuning, metaTag `soundLibraryLanes`)
  play by **varispeed** (`Vst3Plugin::setPitch`) or by **pitch bend** where the patch bends cleanly (`<Instrument bend>`).
  **Every copy the music needs** (the owner: "2"): a maximum only where the map, `[tuning] maxLanes` or the score's
  `soundLibraryLanes` sets one; memory only warns (`SoundLib::copiesMemory`, `checkCopiesMemory`, `MS_FREE_MEMORY_MB`). A
  legato transition's bend glides with the transition's own pitch (map `bendGlide`, `bendSmoothing`, `libraryPitchBends`,
  `[tuning] bendAtArrival`; `tools/soundlibraries/legato_glide/`). **Live and MIDI out** (the owner: "I will not accept losing
  microtones"): `SoundLib::routes(…, forLive)` / `lanes(…, fixedTuning)` give a patch without `bend=` a fixed copy per tuning
  (`Lanes::fixed`, `Route::cents`), tuned by Kontakt's instrument Tune (`KontaktSetup::withTune`); the plain set gives a
  patch's copies one Kontakt multi (`PlainLiveSet::multis`, `KontaktSetup::withSlots`), a bending patch's copies their bends
  as the clip's pitch-bend envelope (LIVE.md › The plain set › Microtones). **Hosted, the same** (`[tuning] hostedTuning` 1
  slots, 0 varispeed; the owner: "Whence takes absolutely forever to load all the patches"): `SoundLib::hostedSlots` /
  `SoundLib::instances` put a patch's copies of one part in ONE instance, a slot per copy tuned by Tune on its own MIDI channel
  (`SoundLibraryHost::tunedState`, `Vst3Synth::setLink`); a setup that isn't Kontakt's falls back to varispeed
  (`untunable`). HANDOFF.md › Kontakt slots.
- **Chords on monophonic patches** (a fix, no setting; the owner: "of course it should play every note"): a patch marked
  `<Instrument mono="1">` (SSO's 42 Performance patches, one legato line per slot) spreads its notes over voices (from the
  lowest pitch, each the lowest voice free as written), each voice its own tuning lanes (`SoundLib::lanes`, `Lanes::voice`,
  `Route::voice`; without varispeed one untuned copy per voice), so every copy is a route as a tuning copy is: hosted
  slots, the renders' instances, the plain set's multi, route set, clips, MIDI out. On a bending patch a plain chord note
  touching its voice's note before stays on its copy (a transition; `otherLane` in rendermidi.cpp). Audit CHORD (fail). **A
  unison is one note** (same pitch and tuning starting together, `Lanes::unison`, `MidiRenderer::libUnison`).
  **Divisi level** (`[levels] divisi` 1 in Recommended, 0 in Library default; the owner approved): while n voices of a mono
  patch's part sound together (`Lanes::sounding`), each chord note plays 10 log10(n) dB softer by CC11 (`SoundLib::divisiDb`,
  `divisiLevel`), so the chord adds up to one section; **an assumption from player counts**, no measured divisi data.
- **Mixer on library parts**: `Vst3Synth::setMix` per slot (hosted), CC7/10/91/93 on the routes (MIDI out). Mixer and
  playback edits never leave playback stopped (the owner): track delays / levels render again while playing
  (`Seq::renderAgainPlaying`, swapped in by the realtime thread); changes that may load patches (part playback, lanes
  options, playback mode, Reload Playback Settings) stop and start again where they were (`GoOnPlaying`).
- **Track delays** (`libmscore/trackdelays.*`, metaTag `trackDelays`; Mixer › Track delay / Tracks…): ms per part plus per patch
  / technique (added up, as Live adds a group's and its tracks'); `MidiRenderer::libraryTrackDelays` moves the library events
  (not Live's clips); with a negative delay everything else plays later by the earliest one (`libraryDelayLead`); the plain set
  writes them as TrackDelay, `livetracks.*` reads them back. **Start lead**: a note whose early start the score's start would
  cut plays its whole onset early, everything else later by the most cut (`libStartLead`); Live's clips have no lead (the plain
  set: Track Delay, `EventMap::startEarlyMs`; the audit removes it, `startLeadMs`); *Live plays the score*'s route clips still
  start at 0. **Track levels** (same metaTag, `levels`): dB per patch / technique, added up, by CC11 per note (`libLevels`,
  `libraryNoteLevels`; Live's clips and plain set too); -42.08 .. +6 dB (Live's Volume top): a patch's loudest level is its
  headroom, played by its volume (`TrackDelays::patchGain`, `LiveSetWriter::kontaktVolume`), its notes' CC11 that much lower.
**Hosting (VST 3)** (`BUILD_VST3`, on except macOS; preference `io/soundLibrary`)
- `audio/vst3/vst3plugin.*` (one instance; `settle()` after setState, or SSO's script resets parameters; `restartTransport()`:
  every playback start, export and verification pass shows the plug-in the transport stopped for one block, then playing, as
  Live's Play does, because SSO's scripts reset their round robins on a transport start), `vst3synth.*` (64 slots =
  port*16+channel; **the slots' lock**, header of vst3synth.h: the audio / exporting thread waits for it, never skips a
  block; every other holder keeps it for a few instructions only, nothing allocated, freed or called on a plug-in under it;
  proof `tst_soundlibrary::slotLockStress`, `tools/playbackverify/slotlock_tsan/`), `mscore/vst3editor.*`,
  `mscore/soundlibraryhost.*` (`readyToPlay`, `sync`, preload at score open, `SoundLibraryExport`, *View › Sound Library…*).
  Plug-in modules stay loaded until the process ends, then `Vst3Plugin::unloadModules` unloads them with the SDK's ExitDll
  (left to the loader, Kontakt 8's teardown ended every MuseScore on Windows with 0xC000000D).
  **Background loads** (`audio/vst3/backgroundload.h`, map `<Plugin backgroundLoad="s">`, Ample 60): offline instances and
  hosted `sync` (`setStateWaiting`, one at a time) wait until the plug-in's load threads have ended and the memory stopped
  growing (`MS_BACKGROUND_LOAD_WAIT`); even levels' background measurer doesn't wait yet.
  **Loading never holds the window** (a fix; header of soundlibraryhost.h; the owner: "just have the playback button grayed
  out during it"): every hosted setState runs on a worker thread (`startSetState`, `parallelLoads()` = max(1,
  `io/soundLibraryLoadThreads`)); Play never loads: `readyToPlay` is true when every instance is loaded, else starts the
  background load and returns; `loading()` grays Play while stopped, status bar "Loading sounds: n of m…"; an edit
  (`scoreEdited`, 1 s debounce) loads new patches it needs; a change that stopped playback plays on once loaded
  (`whenLoaded`). Only `sync` still waits (audio export, tests). Test `tst_liveequivalence::asyncLoadHosted`.
- **Plug-ins in helper processes** (the owner approved, "seems good"; header of `audio/vst3/vst3remote.h`; HANDOFF.md ›
  Out-of-process host): `[soundlibrary] hostProcesses` (MS_VST3_HOST_PROCESSES wins) -1 by default = one
  `MuseScoreLibraryHost[.exe]` (`audio/vst3/vst3hostmain.cpp`) per physical core but one, at least 2 with 2+ cores; 0 = in
  MuseScore's process; K = up to K. `Vst3Plugin::load` makes a remote instance whose calls a local Vst3Plugin in the helper
  replays in order on the same kind of thread (Kontakt's setState work on the helper's main thread, so MuseScore's window
  stays free), bit-identical (`tst_soundlibrary::remoteNull`). Control: pipes; audio: a shared-memory segment per instance
  and two semaphores, no added latency. A helper that dies, or doesn't answer a block for 15 s (`HUNG_MS`), plays silence;
  `SoundLibraryHost::checkHostProcesses` says so, reloads the instances in a new helper and plays on; a patch whose helper
  ended `MAX_HELPER_ENDS` (3) times isn't reloaded. No helper can start: in-process, logged. Offline renders use helpers too;
  kthost always hosts in-process. Live and MIDI out unchanged. Open: a ~2.6 s audio-callback stall per helper start while
  stopped. Untried: Ample and the editor window in a helper.
- **Render ahead** (header of `mscore/seq.h`, engine `mscore/renderahead.h`; the owner approved; docs/PLAYBACK_SETTINGS.md ›
  Render ahead): `[soundlibrary] renderAhead` 500 ms (`MS_RENDER_AHEAD` wins; 0 = the callback renders). One producer thread
  (MMCSS Pro Audio / SCHED_FIFO, never a kheap LoadScope) runs `Seq::processDirect` for every block, so the audio is
  sample-identical; the callback (`Seq::process`) copies. Playing: up to renderAhead ms queued; no lead with MIDI out / sync /
  Live / JACK; stopped: no lead. Seek, Stop, device change drop what was rendered ahead; changes while playing heard
  renderAhead later. Underrun: silence. **Late audio**: three late callbacks / underruns within 1 s show "Playback couldn't
  keep up: audio was late %n time(s). …" once per play and a line in `load times.log`. Test tools: `-a null`,
  `MS_PLAY_CAPTURE`, `MS_AUDIO_STALL`, `tools/playbackverify/render_ahead_check.sh` / `render_ahead_null.py`.
- **mimalloc for setState only on Windows** (`audio/vst3/processallocator.*`, `tools/soundlibraries/kheap`): `main()` first
  starts `kheap.dll` (MinHook + mimalloc) **scoped**: a block comes from mimalloc only on a thread inside
  `ProcessAllocator::LoadScope` (`Vst3Plugin::setState`); every other thread keeps the system allocator. **Never mimalloc for
  the whole process again**: it made the owner's live playback crackle (`MS_KHEAP_OFF=1` cured it; HANDOFF.md › kheap and
  realtime audio). Load time only; without kheap.dll the system allocator; load times.log says which. `MS_AUDIO_TIMING=<file>`
  (`audiodrivers/pa.cpp`) logs each play's callbacks.
- **Setups are made by MuseScore** from each patch's `.nki` (`audio/vst3/kontaktsetup.*`), then replaced by Kontakt's own state
  after the first load (`resave`); the states loaded (voice limit 512, the copies' Tune slots) are **prepared once** and kept byte
  for byte (`prepared` in soundlibraryhost.cpp). Setups folder `<dataPath>/soundlibraries/<library>/`: `<patch>.vst3state`,
  `Kontakt empty.vst3state`, `made setups.json` (`MAKER_VERSION`), `checks.json` (raise `CHECK_VERSION` in soundlibrarycheck.cpp
  when old results go stale), `dynamics.json`, `load times.log` (qDebug doesn't show on Windows).
- Background jobs (own setups copy, lock, log; `.bat` files in `bin` only with `-DMSCORE_INSTALL_MEASUREMENT_SCRIPTS=ON`):
  `--check-dynamics`, `--scan-keys`, `--measure-load-times`, `--verify-playback`, `--live-equivalence`, `--create-live-set`,
  `--update-live-set`, `--live-set-readback`. `--even-levels`, `--verify-playback` and `--live-equivalence` load **one part's instances at a time**
  (`loadInstances(…, parts)`; every part's at once ran the 16 GB VM out of memory); a `std::bad_alloc` is logged as "out of
  memory". Each logs a **render preflight** (`libmscore/renderpreflight.h`: calibration files' path + SHA-256, each patch's
  source, setups folder / copy, playback.ini + preset; VERIFY.md › Preflight); the copy is synced at start (`syncJobCopy`).
- **Measuring a patch** (docs/MEASURING.md): `tools/soundlibraries/measure_patch.py plan|run|analyze|report` sweeps any patch on
  the VM through **kthost** (`tools/soundlibraries/kthost/`, an offline host in the Windows CI artifact's `bin/`); a
  transition's perceptual reference is f0 leave + 23.5 ms, not arrival (the owner). Tone: `tone_check.py` (its `avoid` proposal
  goes into `sso_bad_samples.json` only after a person checked it). Measurement runs (supervised, resumable): `--extract-library
  … --extract-controllers --extract-pitch-bend`, `--check-timing`, `--check-rest`, `--all-sounds`, `--window-pictures`; readers →
  `tools/soundlibraries/sso_*.json` (`timing_from_check.py`, `dynamics_from_check.py`, `rest_from_check.py`,
  `onset_from_check.py`, `nki_articulation_details.py`).
- Checks and extracts: `audio/vst3/articulationcheck.*` + `mscore/soundlibrarycheck.*` (*Check articulations…*);
  `audio/vst3/pluginextract.*`; `tools/soundlibraries/` readers and `extract_library_files.py` (decrypts nothing, copies no
  script). **A patch's script may be read locally** (its PAR_SCRIPT chunks: plain text in the owner's `.nki`, nothing
  decrypted; the owner: "script text is fucking useful"): dumps live only outside the repo, in
  `~/MuseScore/ms3fork/private/sso-scripts/`, never committed, published or quoted on a page; numbers and rules taken from a
  script are recorded with that source ("read from the patch's script") and confirmed by measurement where they set playback;
  still never decrypt or bypass any protection. HANDOFF.md › When a check zip arrives has the procedure.
- Owner reports a problem: suspect Kontakt's MIDI channel (we send 1), editor sizing, sample loading in offline export;
  crackles / slow loads: memory (Kontakt's preload override at 30 kB).

**Live** (LIVE.md; full architecture map in LIVE.md › Architecture map: read it before changing any of these): Create Live
Set writes the **plain set** (the owner: no automatic adjustments; a MuseScore Link copy on each technique track,
`PLAIN_LINKED`; `plainliveset.*`: section group › part group › a Kontakt track per patch + a MIDI track per technique
carrying its own switch; XML in `livesetxml.h`, `livesetclips.cpp`; `LiveSetKind`; LIVE.md › The plain set lists what Live
hasn't confirmed). **Its technique clips hold the written notes** (`MidiRenderer::setWrittenTiming`), each note keeping its
played times, which `libmscore/livetiming.*` puts back at playback (Track Delay, the Link device's `"Timing"` store; the
Controllers clip stays at played times; *Live plays the score*'s clips are still played). **Timing set in Live** stays (the
owner): a moved or resized note keeps it as its override (`libmscore/livetimingoverride.*`, metaTag `liveTimingOverrides`);
read back by `livetracks.*` (metaTag `liveTracks`). **Update Live Set** (Mixer › Advanced Options… › Ableton Live, `--update-live-set
<set> <score>`; `libmscore/livesetupdate.*`, `mscore/livesetexport.*`; LIVE.md › Update Live Set): a saved plain set (closed in
Live) edited in place as text, only MuseScore's parts replaced where they differ; the owner's devices, returns, sends, tracks
and mixer byte for byte; a `Backup/<name> [date time].als` first; an unchanged score writes nothing. Untried in real Live. Live
plays the score as clips (`liveclips.*`), the route set (`livesetwriter.*`, `livesetexport.*`), automation from a set
(`liveset.*`), clip tabs (`liveclipmodel.*`, `liveclipedit.*`, `cliptempo.*`), the Max for Live device and Control Surface
script (`tools/live/`), Live helpers (`livehelpers.*`).
**Playback verification** (VERIFY.md; checks in `audio/vst3/playbackverify.h`, Python replica
`tools/playbackverify/verify_detector.py`, regression set: VERIFY.md › Detector regression): `--verify-playback`,
`--verify-audio`, `tools/playbackverify/read_verify_report.py`, faults via `MS_VERIFY_FAULT`. **Playback audit** (VERIFY.md ›
Playback audit; `libmscore/playbackaudit.*`, `tst_playbackaudit`): the rendered library events of a whole score checked
against the map's own onsets (not the real sound) for overlaps on a route, unmeasured swapped / early techniques or no onset,
capped early starts, predicted arrival against neighbours (Rasch 1979: > 50 ms fails), level steps under a slur (proposed
threshold 0.69 dB, owner to approve), DROPPED (fail), LEAP (warn), HALL (fail) and AVOIDED CC (fail); the renderer reports each
note's choice through `MidiRenderer::setLibraryTrace`. **Run it on Whence (quarter tones, `Whence.mscz`; Recommended) before
any build goes to the owner, and the heard check too** (VERIFY.md › Heard check: `tools/playbackverify/heard_check.sh <score>
[preset]`: the real sound, Kontakt + SSO on the VM per route as MuseScore's own export renders it; each note's heard time
against its written one and the audit's prediction; ARRIVAL / RHYTHM (fail 0.10 / warn 0.05) / LEVEL; the audit alone can't
catch a wrong model).
### Edit lag

After an edit only what changed is rendered on the main thread (HANDOFF.md › Edit lag): `ContextLevel::volumes` / `masterTrim` keep
the last 8 results per score (`RecentResults`, recentresults.h), `NoteLane::onPerformancePatch` keeps its answers, and `EditWarmup`
(mscore/editwarmup.h) makes the edit's real-range and master-trim renders one chunk per event-loop pass while stopped. Rule (the
owner): sound-identical or dropped; prove changes here with `editLagIdentical` and before/after `dumpEvents`; `MS_EDIT_LAG_OFF=1`
turns the speedups off.

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
- All the fork's tests: `tools/run_fork_tests.sh <build dir>` (through the `ninja-<name>.sh`/`run-<name>.sh`
  wrappers on the dev VM).
- **Owner's dev VM**: disk is tight; delete throwaway worktrees. **One build dir per branch**: never point a build dir at
  another tree's sources (stale objects from the other tree crash tests: 2026-10-04); `ninja -j2` under `nice -n 15`.
  **Never reach a build dir through a symlink**: Qt's moc files include headers by relative path (`../../../../libmscore/
  score.h`), which resolve through the symlink's real location to the OTHER tree's headers (and ccache, keyed by real
  paths, keeps serving them): a mixed `Score` layout, every test crashing in `initTestCase` (2026-10-04). Bind-mount the
  build dir onto a real directory instead; after a mix-up, delete `*/mocs_compilation.cpp.o` and rebuild with
  `CCACHE_RECACHE=1`. **No sudo** (the owner, 2026-10-30, prompted by a
  build): never copy another worktree's `build.rel` and bind-mount it (the old `ninja-<name>.sh` / `run-<name>.sh` wrappers
  next to the worktrees use `sudo unshare` and prompt the owner); give a new worktree a fresh build dir configured for its
  own sources (a plain directory, on / when /data is tight; ccache makes it quick) and build with
  `flock ~/MuseScore/ms3fork/private/BUILD_LOCK nice -n 15 ninja -C <dir> -j2 <target>`.
- A MusicXML score in a test needs `score->rebuildMidiMapping()` before rendering (else channels -1 crash).
- Linux has both `USE_ALSA` and `USE_PORTMIDI`; PortMidi wins, so the owner's Windows path compiles here.
- Headless MuseScore: isolated `HOME`, `~/.vst3/mstestsynth.vst3` linked, `application/startup/firstStart=false`,
  splash / start center off in `MuseScore3Evo.ini`, `.mscz` scores (MusicXML asks about Edwin), no `session` file.
  GUI with sound: Xvfb + PulseAudio null sink (`pulseaudio -n --load=module-null-sink
  --load=module-native-protocol-unix`, own `XDG_RUNTIME_DIR`); under `QT_QPA_PLATFORM=offscreen` the Live link
  never binds its port. Env knobs: `MSTESTSYNTH_INIT_MS` / `_SETSTATE_MS` / `_STREAM_MS` / `_STREAM_MB`,
  `MS_KONTAKT_MAX_VOICES`, `MS_SOUNDLIBRARY_LOAD_THREADS`, `MS_PREPARED_OFF` (prepared states made at each load), `MS_KHEAP_OFF=1` (Windows: the system allocator, not kheap's mimalloc), `MS_AUDIO_TIMING=<file>` (PortAudio callbacks' times per play),
  `MS_CHUNK_WINDOWS_OFF=1` (playback's chunks rendered alone, as before chunk-equal), `MS_EDIT_LAG_OFF=1` (the edit-lag speedups off: the code as before them, to compare; `EditLag`, recentresults.h),
  `MS_LIVESET_OUT=<file>` (liveSetWrite's set), `MS_RENDER_AHEAD=<ms>` (render ahead, wins over the ini; 0 off),
  `MS_AUDIO_STALL=<s>:<ms>` (the renderer sleeps once there), `MS_PLAY_CAPTURE=<wav>` (one play recorded, then quit), `-a null`
  (no audio device, fixed blocks).
- Other tests: `tools/soundlibraries/test_make_setups.py`, `test_extract_library_files.py`; Node in
  `tools/live/test/`. Test data: `test-scores/`, `share/verifyplayback/`, each mtest's folder.
- **Standard test score** (the owner): *Whence*, the quarter-tone original, `~/MuseScore/ms3fork/test-scores/Whence.mscz` on the
  dev VM, **never in the repo** (the owner's score). Use it for the playback audit, dumps (`dumpEvents`, with `MS_DUMP_LIVE` for
  the Live path), Create Live Set, VM runs and test instructions: its quarter tones exercise the tuning lanes, Kontakt Tune and
  bends. *Whence 12-TET* (`Whence 12-TET.mscz` / `.mscx`, same folder: each quarter tone rounded down, 10 routes instead of 26,
  no tuning lanes) only where a test needs no tuning lanes.

## Tests and known state

Last recorded counts (with init and cleanup; the date of each is in the commit that changed it, older ones in docs/HISTORY.md ›
"Moved from CLAUDE.md, 2026-11-11"). Say which you ran; nothing here was re-run on the Linux dev machine for this file except
the Lua standalone check.

- `tst_soundlibrary`: 169 passed, 7 skipped with the default helpers (`vst3UnloadModules` skips with helpers), 170 passed,
  6 skipped in-process (`MS_VST3_HOST_PROCESSES=0`). The skips need the owner's files: `MS_ROUTES_SCORE`, `SSO_NKI` /
  `SSO_EMPTY`, `MS_EXTRACT_PLUGIN` + `MS_EXTRACT_OUT`; `SSO_KICKSTART_NKI` extends two Kontakt tests;
  `playbackVerifyRegression` needs `MS_VERIFY_REGRESSION` (VERIFY.md › Detector regression). The tests' ini pins settings
  `Playback::setIniValuesForTest` leaves at 0 (e.g. plainPerformance, lowCut 1), so older tests keep their expectations.
- `tst_playbackaudit`: 14 passed, 1 skipped (`auditScore` needs `MS_AUDIT_SCORE`: VERIFY.md › Playback audit). Baseline, Whence
  (quarter tones), Recommended, 2026-10-29: ARRIVAL 33 fail / 7 warn, LEVEL STEP 26 info, every other kind 0 (the heard check on
  the real sound, build 491: 35 fail / 19 warn).
- `tst_liveequivalence` (links mscoreapp; uses tst_soundlibrary's test synth): 93 passed, 5 skipped (tools, not tests:
  `dumpEvents`, `utilitySet`, `scriptValuesSet`, `chunkCompare`; `twoLibraries` without `MS_VIAL_VST3`). `dumpEvents` takes
  `MS_DUMP_SCORE`, `MS_DUMP_MAP`, `MS_DUMP_OUT`, `MS_DUMP_SETTINGS`, `MS_DUMP_CHUNKED` (renders as playback does), `MS_DUMP_LIVE`
  (as Live's clips, plus kontakts.txt / routes.txt / multis.txt), `MS_DUMP_TIMING`; every dump writes routemap.txt and
  tunings.txt. `tst_liveintegration` 71 (pictures with `MS_LEADING_PNG`, `MS_LANE_PNG`, `MS_SOUND_LIBRARY_PNG`,
  `MS_CLIPBANDS_PNG`, `MS_CLIP_TEMPO_PNG`).
- `tst_keysig` 8, `tst_tuning` 18, `tst_phrasemark` 19, `tst_marcatolevel` 14, `tst_performancetechnique` 26, `tst_tempochange`
  4, `tst_timesig` 14, `tst_playability` 60 passed, 2 skipped (`speed` needs `PLAYABILITY_BIG`, `dump` `PLAYABILITY_DUMP`),
  `tst_midi` 97 (its 3.x tests render with MuseScore 3.6's method, `ms3State`, against 3.6's references, changed only where the
  fork differs on purpose; `eventsMs4` against the fork's own `-ms4-ref.txt`: regenerate only for an intended change of the note
  model).
- `tst_luascripting` (mtest/libmscore/scripting; `tst_scripting` is the QML plug-in test): 13 passed; the library alone:
  `lua_standalone libmscore/script/test/run.lua`, 161 checks (target `lua_standalone`, no Qt).
- Node tests in `tools/live/test`, Python tests in `tools/soundlibraries` pass; `tools/playbackverify/test_heard_check.py`: 23
  run, 1 skipped (`KeptRun` without `HC_CONTROL_DUMP` / `HC_CONTROL_WAVS`; `KnownAnswer` = the heard check's positive controls).
- Untried with Kontakt / SSO unless docs say so; LIVE.md lists what only real Live can show.
- The owner's "SSO Articulation Map" review page (https://claude.ai/artifact/Y9dDEm5gpEjtpjjQmM9qBm, collection `reviews`): read
  with ArtifactData where available, else ask for the marks; fix through `gen_spitfire_sso.py`.
