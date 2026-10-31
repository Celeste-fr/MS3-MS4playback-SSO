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
Part 1.

**MS4 playback.** `libmscore/ms4playback.*` (note model), `ms4tables.h` (generated by
`tools/ms4playback/gen_tables.py`), `rendermidi.cpp` (`collectMeasureEventsMs4`, `renderMs4Dynamics`; dynamics
method 3 is the default), `thirdparty/fluidsynth`, `effects/musereverb`, `synthesizerstate.cpp`. One deliberate
difference: an MS3 hairpin with its own velocity change plays as in 3.6 (`ms3HairpinVelocity()`, preference
`application/playback/ms3HairpinVelocityChange`; `MS4_STRICT=1` disables, `ab/trace/regress.sh` sets it).
`mscore/playbackmode.h` (MS3 / MS4 / each library engine by name, all parts) and `libmscore/partplayback.h` (per part,
metaTag `partPlayback`: "library:<engine>" a part's own library, an old "library" the global one). **Engine lane** (`libmscore/enginelane.h`, branch `engine-lane`; HANDOFF.md › Engine lane): an automation lane "engine" per part (MS3 / MS4 / a library, steps), each note on the engine at its tick, legato never across a switch, a lane's library loaded for the whole score (`playingModes` in soundlibrary.cpp), rendermidi.cpp › Engine lanes; fades stored, not yet played; ignored for Live / MIDI out. `tempochange.h` (rit./accel. lines, metaTag `tempoChanges`). `slur.h` › Phrase marks (non-legato
slurs, Alt+S, metaTag `phraseMarks`). `articulation.h` › MarcatoLevel (every articulation sign's and technique staff text's level in dB,
Inspector › *Level*, ±35.5; a chord's add up, `MidiRenderer::levelOf`; metaTag `marcatoLevels`; library: velocity by the
measured curve or CC11 / CC1 per note, `libraryNoteLevels`; built-in: SoundFont 2's law).

**Notation.** Custom key signatures follow each staff's clef (`KeySigEvent::forClef`, `Staff::keySigEventForClef`,
*Tools › Adapt Key Signatures to Clefs*, `mscore/keyedit.cpp`). Deleting a restating time signature keeps the
measures (`Score::cmdRemoveTimeSig`). Numpad 1-7 enter scale degrees (`Score::scaleDegreeStep`; on Linux/xcb
`ScoreView::event` takes the keypad keys, `numpadDegreeAction`; `Shortcut::load` gives new actions their defaults
in an old shortcuts.xml). Continuous View has a bar ruler (`mscore/barruler.*`: every bar's number along the top,
as the measure-number style counts, view only, View › Bar Ruler). Switching the view mode keeps the selection's measure (if in view) or the one at the view's centre centred
horizontally, its staff at the same height (`ScoreView::viewAnchor` / `showViewAnchor`, from `MuseScore::switchLayoutMode`).

**Tuning** (`libmscore/tuning.h`): temperament (metaTag `temperament`, the Tuning plugin's JSON) + accidental rules
(microtonal, HEJI, Johnston, stacked accidentals as hidden `Symbol`s) + the note's own tuning. `tuningtables.h`
from `tools/tuning/gen_tuning_tables.py`; `mscore/tuningdialog.*`; plugin API `playbackTuning`,
`microtonalTuning`. The Microtonal Tuner plugin is frozen: update or drop `tst_tuning::pluginParity` when needed.

**Playability** (PLAYABILITY.md): `libmscore/playabilityrules.*`, `playability.*`, `playabilitydiagram.*`,
`mscore/playabilitypanel.*`.

**Sound libraries** (`libmscore/soundlibrary.h`; map format `share/soundlibraries/README.md`)
- `SoundLib`: loads maps, matches instruments by `Instrument::getId()`, builds the `Want` from MS4 articulations and
  staff text, `choose()` (extras `with=`, kits `kit="1"`, `prefer`, shorts by `length`/`from`), `routes()`.
- **Several libraries in one score** (branch `vial-engine`, 2026-10-26; the owner: "natively integrate Vial Instrumental
  as another playback engine, like we have now for ms3, ms4 and sso"; soundlibrary.h › engines, README › Several
  libraries): every map is an engine by its name (a map "<engine> (<variant>)" a variant: Vial Fuse, picked in
  Preferences › I/O, `io/soundLibraryVariants`); `SoundLib::setEngines` / `engine` / `libraryOf(part)` /
  `scoreLibraries` (the current library first, then the parts' other engines by name) / `allRoutes` (each `Route`
  with its `library`; the current library's routes from slot 0 as alone, so a one-library score renders byte for byte
  as before; `routes(score, lib)` numbers lib's after the ones before). Plug-in output only (MIDI out, Live: the
  current one alone, other engines' parts MS4, `otherEngineParts`; LIVE.md › What still differs). Per library: the
  renderer's `LibPart::library` / `libAt(slot)` (dynamics CC, expression, lanes, calibration by `calibrationFor`: the
  current library's, another's by the host's loader), the host's needs, plug-in paths, instances and per-slot varispeed
  (`Vst3Synth::setVarispeed(slot, …)`), the export's instances, contextlevel's volumes (`allRoutes`, mics by
  `LibInstrument::library`) and the master trim (`trimLibrary`: the first with a crest). The global library's only:
  even levels' measurement, the audit, the verification, Live. An engine whose plug-in isn't found (`MS_VST3_PATH` adds
  folders) is grayed in the boxes (`addEngineItems`, `soundEngineAvailable`) and its parts play MS4
  (`SoundLib::setEngineAvailable`); `PREF_IO_SOUNDLIBRARY_PLUGIN` applies to the global library or a library whose
  plug-in file it names.
- **The SSO map is generated, never hand-edited**: `python3 tools/soundlibraries/gen_spitfire_sso.py
  <reaticulate>/userbanks/Spitfire/Spitfire-Symphony_Orchestra.reabank "share/soundlibraries/Spitfire Symphony
  Orchestra.xml"` (needs a clone of github.com/jtackaberry/reaticulate). Inputs: the script's tables (`I`, `T`,
  `SPITFIRE_ADD/DROP/RENAME`, `DRUMS`, `KEYSWITCHED`, `LENGTHS`, `EXPECT`) and the measured `tools/soundlibraries/
  sso_*.json`. Diff the XML after: only intended entries may change.
- Renderer (`rendermidi.cpp`): each library event carries patch, switch and route; `finishLibraryEvents` routes,
  copies controllers to every patch ahead of the notes, applies early starts, lanes and bends. Dynamics on CC1;
  listed shorts take velocity. **Adjustments: Recommended plays the measured ones the owner approved, Library default
  none** (the owner, 2026-10-06, first wanted none, timing and levels set in Live; from 2026-10-08 approved measured
  ones one at a time, by ear: below; 2026-10-12: this rule replaces "No automatic adjustments"). Every adjustment is a
  setting, off in Library default, on in Recommended only once the owner approved it. Legato transitions play on their
  beat, pedal changes one tick after their chord (2026-10-07: "remove all settings not currently being used"); the settings and code that could change that (legato early, fastFirsts, phrase
  gap, overlap, fast technique, level balance, pedal timing, the old calibrated short velocity, even steps, one
  instance, map track delays) were removed 2026-10-07 (docs/PLAYBACK_SETTINGS.md lists them; old ini keys and metaTags
  are ignored). The technique still comes from the notation. **Calibrated levels** (`[levels] calibrated`; **2 in Recommended and by
  default since 2026-10-15**, 0 in Library default; the owner, 2026-10-08, shorts too quiet: 1): **1** a technique on
  velocity plays as loud as the part's Long at the same dynamic (measured curves, by ear: perceived loudness where
  measured), plus MS4.7.5's offset for its articulations (marcato's too), plus the Inspector's levels (`libVelocity`,
  `SoundLib::calibratedVelocity`; docs/PLAYBACK_SETTINGS.md › Calibrated levels); **2, matched + D from real players**
  (the owner, 2026-10-15, option B', replacing B the same day): 1's velocity, then D dB more along its own curve, D the
  map's `<Articulation heldOffset>` (`SoundLib::techniqueOffsetDb`; `technique_offsets.py` -> `technique_offsets.json` ->
  `gen_spitfire_sso.py`: string bowed shorts -3.4 dB, Spheres K. 550 pooled over Violins 1 / Celli / Basses with the owner's corrected readings (`staccato` › `use`); Short 1.0 +0.3; pizzicato Meyer's pp / ff;
  Basses' Bartók / col legno 0), else B's SSO loud-end difference (`SoundLib::loudEndDb`, an assumption; the rest past 127 by CC11 headroom), then
  MS4's offset and the Inspector's; the level rule's slur gains don't take D (both are measured against the held Long;
  the shift by the Spiccato's D went 2026-10-15: it would count D twice); realRange as for the Long; techniques on CC1 keep the library's balance, except Performance legato notes (since 2026-10-10: by CC1, as
  loud as Long, `SoundLib::calibratedController`, `performanceLevel` in rendermidi.cpp; with **`[levels] context`** (the level rule, the owner 2026-10-10: the same written dynamic equally loud in every string part and technique, in the music; 1 in Recommended and by default, 0 in Library default) CC1 at the written dynamic instead and one gain per slur from the map's `<Articulation contextLevel>` (by dynamic, median IOI and pitch; `level_rule.py` from `sso_performance_context.json`; re-measured 2026-10-15 with today's Recommended slurs, even levels off: gain = the section's held Long - its slurred figure at the written CC1, `grid_score.py` / `grid_ana.py`, MuseScore's own renders on the VM, IOI up to 1000 ms), by CC11 and patch volumes: the score's largest gain G on the Performance patches' own gain stage (below; before 2026-10-15 every other patch at -S, S = max(0, G - 6)) (`libmscore/contextlevel.h`, `TrackDelays::patchGain`; docs/PLAYBACK_SETTINGS.md › Level rule); with **`[levels] sectionBalance`** (**2 in Recommended and by default since 2026-10-14**, 0 in Library default) each section's patches also play a map level O louder: **2, the Clark & Luce balance** (the owner, 2026-10-14, levels from real data): `<Instrument sectionLevel2>`, the same method with the real side from Clark & Luce 1965's orchestral scales at mp as sound power (+31 - TU Berlin's DI in that direction, each marking its own since 2026-10-15: `tu_directivity.json`), `section_balance_cl.py` + `clark_luce_levels.json` + `clark_luce_table2.json` (Table II: the horn from its player 1, Fig. 2's horn being player 2's alone; each player counted once) + `sso_sound_dynamics.json` (one-pitch SSO side for the 15 patches without a full sweep: solo strings, Motif brass, piccolo, alto / bass flute, contrabass clarinet / trombone / tuba, cimbassi; ±2.3 dB) → `sso_cl_section_balance.json` → `SECTION_LEVEL2` (Violins 1 -0.03, Violins 2 +1.12, Violas -7.08, Celli +2.56, Basses +3.43, winds -15.04 (Bassoon Solo) … +15.89 (Trombones a6); **in context since 2026-10-24** (the owner: "apply the full measured balance", after checking the readings' spot-check page; Iteration 15): the flutes +3.96, oboes -4.17, horns -8.29, trumpets -4.59 dB on every patch of the instrument (piccolo / alto / bass flute and cor anglais their family's, an assumption), from two anechoic orchestra recordings against the strings at the same marking (docs/RESEARCH.md › 1.5a; `anechoic_balance.json` → `anechoic_shift.py` → `sso_anechoic_shift.json` → `ANECHOIC_SHIFT`; clarinets, bassoons, trombones, tuba, strings unchanged); docs/BALANCE_LOG.md: one entry per refinement; contrabassoon / bass trombones their sibling's level + TU Berlin's ratio between them at mp (since 2026-10-15, one source's ratio; the bass trombones' corroborated by Iowa MIS and ISO 23591), the bass clarinet its sibling's + the median of four independent sources' ratio (-0.35 dB, ±2: ISO 23591, O'Brien 2013, TU ff, Iowa MIS; Iteration 13; sectionLevel2 -5.14, was 0.38), the gap patches without their own instrument a family member's level (an assumption), Strings Ensemble from the sections it mixes at each key (+11.93, keys' SD 2.0; Iteration 9, `strings_ensemble.py`), the harp TU Berlin's sound power moved to this scale (-9.81, about ±4 dB; the percussion within Skålevik's range: no change; docs/BALANCE_LOG.md › Iterations 5-7), the timpani from the Spheres dataset's Romeo and Juliet at its written p / f on the strings' mf scale (-6.92, ±3 dB, realSteps 43.8: `spheres_timpani.py`; Iterations 10 and 14, the owner's corrected readings), the Grand Piano from Meyer 2009 by the median Meyer -> Clark & Luce offset (-1.11, ±3 dB; Iteration 11), celeste and mallets SSO's own (no source; Iowa's extremes not markings, Iteration 12); corroborated by the Spheres sections and Skålevik 2019; docs/PLAYBACK_SETTINGS.md › Section balance; `LibInstrument::sectionLevelFor`, a patch without it plays sectionLevel); **1, the TU Berlin balance** (the owner 2026-10-11: M; Recommended until 2026-10-14): `<Instrument sectionLevel>` (`gen_spitfire_sso.py` `SECTION_FADERS`: M, a real orchestra's balance at CC1 64 from Weinzierl et al. 2018's sound power and SSO's section sizes, less SSO's measured `sso_cc1_curves.json`, centred, mp where realRange's Meyer rule places it (the owner, 2026-10-14, "sure": one rule for everything): Violins 1 -2.60, Violins 2 -1.45, Violas -3.08, Celli +1.89, Basses +5.24 dB (until 2026-10-14, mp at 0.4 of pp -> ff, the owner's ABX-backed choice of 2026-10-11: -3.81 / -2.66 / -2.12 / +2.77 / +5.81); **since 2026-10-14 the 23 woodwind and brass patches too** (the owner: from the solo data "until we can find better non-solo data"): the same method on the strings' scale, -3.00 (Contrabassoon) … +19.96 dB (Trombones a6); all computed by `section_balance_winds.py` → `sso_winds_section_balance.json` → `SECTION_FADERS` / `SECTION_FADERS_WINDS`; docs/PLAYBACK_SETTINGS.md › Section balance), a phrase at gain + O on the gain stage (docs/PLAYBACK_SETTINGS.md › Section balance); with **`[levels] realRange`** (stage A of the dynamics plan, the owner 2026-10-12; **2 in Recommended and by default, 0 in Library default**: the owner, 2026-10-13, heard on the ladder; 2026-10-14, 2) each note of the five string sections also plays realDb = (player's change from mp at the written CC1) - SSO Long's change from CC1 64 by CC11, following CC1 through hairpins; **Clark & Luce spans** (`realRange` 2; the owner, 2026-10-14: "let's use the section data wherever it's reliable", one source for every instrument): the player's change is steps(written CC1) x S / 5, S the instrument's written pp -> ff as orchestral players play it (map `<Instrument realSteps="S">`: Clark & Luce 1965, *J. Audio Eng. Soc.* 13(2), Table I, printed; `real_range.py` `SPANS`; the Spheres dataset's whole sections agree within ~2 dB where they recorded 3+ dynamics, a cross-check only, not in the map; bass clarinet / contrabassoon / bass trombones their family's, an assumption; docs/PLAYBACK_SETTINGS.md › Real range), ppp / fff one step more, a patch without realSteps the Meyer rule; **the Meyer rule** (`realRange` 1; the owner, 2026-10-14, replacing rule D: "this is clearly better"; Recommended until Clark & Luce spans the same day): (F(steps) - F(mp)) x span: TU Berlin's single-note pp / ff are ppp / fff (F 0 / 1), written pp / ff the levels of normal playing at the instrument's fractions a / b of the span (map `realPlay="a b"`: Meyer 2009 ch. 3, rapid scales against single-note extremes, `real_range.py` MEYER), p .. f in equal steps between; without `realPlay` rule D (7 equal steps, a = 1/7, b = 6/7): Cor Anglais, Contrabassoon, which Meyer gives no extremes for; Bass Clarinet and Bass Trombone(s) take their sibling's (clarinet's, tenor trombone's) fractions, an assumption ("plays like its family", the owner 2026-10-14; `real_range.py` SIBLING) (map `<Instrument realSpan longLevel realPlay>`: Weinzierl et al. 2018's ff - pp per pitch, smoothed, and `sso_cc1_curves.json`, by `real_range.py`, `--rule-d` for the comparison; a capped short's rest by CC11; since 2026-10-14 also the 23 main wind and brass patches the database has, solo / a2 / a6, with their sectionLevel, and since 2026-10-15 the 15 gap patches (`sso_gap_cc1_curves.json`; the eight without a database instrument realSteps only, their family's, an assumption; checked in Kontakt: every marking within 0.05 dB RMS of steps x S / 5, docs/BALANCE_LOG.md › Iteration 4): `sso_winds_cc1_curves.json`, `real_range.py` WINDS: realRange compresses the flutes, oboes and cor anglais (their real span is smaller than SSO's) and stretches the clarinets and trombones), its part's largest R (found by rendering the score once, `ContextLevel::realRangeTops`) on the patches' gain stage (`libmscore/contextlevel.h` › Real range; docs/PLAYBACK_SETTINGS.md › Real range)); the winds' / gap patches' longLevel every semitone since 2026-10-28 (`sso_winds_cc1_semitone.json`, `sso_gap_cc1_semitone.json`, Solo Violin 2 still every 3); with **`[levels] evenKeys`** (2026-10-28, the owner: SSO's uneven keys an artefact; 1 in Recommended and by default, 0 in Library default; plays only with realRange) a wind / brass Long's key more than T = 1.5 dB (real players' key-to-key scatter at ff, TU Berlin, p80) off the Theil-Sen line through its 8 nearest keys at CC1 64 plays the excess back, in R (map `<Articulation keyLevels>`, `even_keys.py` -> `sso_even_keys.json`; 34 patches, Contrabass Clarinet up to 9 dB; Oboe Solo / Oboes a2 to the real oboe's key shape instead, TU's deviation at ff, the owner 2026-10-28; the Vial maps carry the same, their longLevel matched to SSO's own keys; docs/PLAYBACK_SETTINGS.md › Even keys). `[slurs] quick` (2 in Recommended, the default; 0 in Library default; in Recommended it acts only under staff text
"non performance", `[slurs] performance` taking the strings' slurs: Whence dump 2026-10-12, no Long (Rachm.)) plays short slurred notes on a quicker technique (Long (Rachm.)), early by its own onset, by `[heldNotes] byPitch` as every note (since 2026-10-09; `Choice::swapped`, `sso_rachm_onset_fit.json`) and, with `[slurs] quickLevel` (1 in Recommended, the default), as loud as the held one (map `quickLevel`: perceived, `sso_rachm_levels.json`; `quickDb`). Violins 1 / 2 only: Violas' and Celli's in-context entries in both files are measured but not used (`RACHM_UNUSED` in the generator; the owner, 2026-10-09: Whence bars 1-2 Celli ~8 dB louder and pulsing), their Rachm. as at 25a3dcbbeb. Exception: notes start
  early by their articulation's median measured onset (held notes, and since 2026-10-08 every measured technique, shorts too) (`[heldNotes] byPitch` 1: each by its pitch's)
  (`<Onset early>` 100 since 2026-10-07; the owner: plain Long for everything, lined up within Rasch's 30-50 ms between
  players; `[heldNotes] early` 0 plays them as written). **Performance patches: the strings' slurs in Recommended**
  (the owner, 2026-10-10, reversing 2026-10-06's "No Performance patches" for Recommended only): `[slurs] performance`
  (1 in Recommended, the default; 0 in Library default) plays slurred notes of the five string sections as under staff
  text "performance" (`Want::slurPerformance`, `choosePerformance`: a plain choice and a legato with `attacks`; staff
  text "non performance" opts out, `TextState::performanceOff`), instead of `[slurs] quick`'s swap; a staccato before a
  slur stays on All techniques, so the slur starts from silence. With **`[slurs] transitions`** (the owner, 2026-10-12; 1 in Recommended and by default, 0 in Library default) Auto plays a slurred note's transition fingered and a slur starting where the slur before ends (touching, no accent) as a bowed transition from it instead of a re-attack (`performanceNote`, `slurAt`; docs/PLAYBACK_SETTINGS.md); with **`[slurs] smoothStart`** (same values) a note after silence on its patch starts with the smooth attack, not spiccato, early by the smooth attack's own measured onset (map `attackOnset`, `sso_smooth_onset.json`, `Articulation::startOnset`; 2026-10-12); **`[slurs] transitionsEarly`** (the owner, 2026-10-14, to A/B by ear; **1 by default and in Recommended since 2026-10-15**, 0 in Library default: by ear on Whence's fast quarter tones, 1 measured best; "slow slurs are skipped" judges written intervals, `NoteInfo::written`) sends a Performance transition early: 1 by its modelled arrival (the audit's model), 2 by the measured leave + 23.5 ms (heard on the beat, Polfreman 2013), 3 the glide's middle; capped so the note before keeps keepMs / 15 ms (`TRANSITION_MIN_GAP_MS`), the join and bend following the moved note-on (docs/PLAYBACK_SETTINGS.md › Transitions early). **`[slurs] plainPerformance`** (the owner, 2026-10-17, "C" by ear; 1 by default and in Recommended, 0 in Library default since he heard the five sections, 2026-10-17): every bowed note of such a part on its Performance legato (`choosePlain`, `Choice::plain`), velocity 64, at its written start, staccatos spiccato attacks 45 ms clear of their neighbours, with `[slurs] plainJoins` 1 a touching note on the note before's copy also at another tuning (`SoundLib::lanes` `plainJoin`, slur-joins 2026-10-22; 0 everywhere: the owner preferred the attack, "violas A better for both"), no early start of its own; the patch's events the map's `<Instrument plainShift>` early as a track delay (`TrackDelays::Delays::plain`, never in the metaTag; Live: the Kontakt track's Track Delay; `sso_plain_performance_shift.json`, all five sections' all-notes medians) (docs/PLAYBACK_SETTINGS.md › Plain Performance); with **`[slurs] plainAuto`** (branch `celli-bowed`, 2026-10-26; 1 by default and in Recommended, 0 in Library default: the owner, "smooth after a rest, bowed on each new bow, fingered inside a slur, spiccato for staccatos"; first `plainBowed`, ignored) a plain note on Auto after silence on its patch plays the smooth attack (5), early by its smooth onset less the patch's plainShift (Violas 125 ms, Celli 92; capped as an attack; with plainTiming by its own onset), a note touching the note before outside a slur going on the bowed transition (93; Violas 100), inside a slur 64; staccatos, accents, a key struck again as plain plays them (`performanceNote`'s `plainAuto`, `PerformanceNote::smoothStart`; › Plain Performance › Plain Auto); **2** (branch `bowed-early`, 2026-10-27; for the owner to compare by ear, 1 stays the default and Recommended's) as 1, each new bow stroke (`PerformanceNote::plainBowed`) early by the map's `<Instrument bowedEarly>` (the bowed transition's measured heard time less plainShift: Violins 1 125, Violins 2 151, Violas 55, Celli 106, Basses 64 ms; `sso_legato_glide_bowed.json` `proposalNoOctavesMs` -> `gen_spitfire_sso.py` BOWED_EARLY) by midEarly's mechanism and caps; the audit models a bowed new bow as heard plainShift + bowedEarly after its note-on under 1 and 2 (`LibTrace::plainBowed`). **`[slurs] plainTiming`** (2026-10-21, branch `plain-timing`; 0 by default and in both presets: rejected by ear, the owner, 2026-10-27, on the A/B page (Whence Violins 1 bars 5-8, build 485): "I feel like A's timing is better in general ... I would prefer A for all the note timings" (the last note, a re-attack, sounded late in B though measured on the beat at -15 dB: his ear places these attacks later)): with plainPerformance no plainShift; each plain note early by its own model, an attack by the map's `<Instrument plainAttack>` (measured in context, 15 dB under the peak: Vos & Rasch 1981), a transition by its modelled arrival (transitionsEarly's mode, at least 1); the Basses' unsampled con sord. / espressivo stay on Performance (`Want::plainTiming`, `choosePlain`; docs/PLAYBACK_SETTINGS.md › Plain timing). **`[slurs] speedFromWritten`** (the owner approved 2026-10-17; 0 by default and in both presets until he has heard it after the null test): the five sections' Performance patches (map `speedCC="119" speedScript="$vfm12"`, `sso_performance_layers.json`) load with the script's own CC119 writing off (`SoundLib::playbackScriptValues`, `SoundLibraryHost::setupStateWith`; the setup file keeps the library's value) and each legato note-on gets CC119 from the WRITTEN spacing by the script's formula (`SoundLib::speedValue`, `MidiRenderer::librarySpeeds`), so early starts can't move a transition's speed band; Live: carrier key 113 (device protocol 11), the plain set's technique clip envelope (docs/PLAYBACK_SETTINGS.md › Speed from written). **`[slurs] fixedTransitions`** (2026-10-20, branch `sso-legato-fixes`; 0 by default and in both presets: the owner, 2026-10-28, Violas A/B, "keep A", SSO's own variation): the same patches (`speedScript`) load with their modulators from Kontakt's random generator (source type 10) at intensity 0 (`SoundLib::playbackScriptValues`' pseudo value `@randomSources`, `KontaktSetup::withRandomSources` in `setupStateWith`; resave puts the made ones back), so plays repeat (VM: Whence nulls at -71..-97 dB, an average take); Create Live Set embeds it (docs/PLAYBACK_SETTINGS.md › Fixed transitions). **`[slurs] microtoneBends`** (2026-10-21, branch `fast-microtones`; **3 by default and in Recommended since 2026-10-22**, 0 in Library default: the owner, Whence's Violas alone, "B is so much better it actually sounds right now" (1), then Celli and Violins 1 alone, "both B" (3)): on a bending patch, 1 a microtonal note joined by a legato transition goes on the key on the side of the note before, so key step and bend move the same way (`microtoneSpelling` in rendermidi.cpp, `libSpellKey` / `libSpellCents`), and a fingered transition's bend glides by the map's `<Articulation bendGlideFingered>` (`sso_legato_glide_fingered.json`, `legato_glide/gen.py --velocity=64`) uncut past the next note-on up to its arrival (`libraryPitchBends`' `uncut`); 3 (branch `microtone-next`, 2026-10-22; the default since the owner heard it the same day) as 1, and each plain fingered transition on such a patch early by its glide's middle less the patch's plainShift (`midEarly` in rendermidi.cpp; Violins 1 70 ms, Celli 61, Violas 0: the 12-TET notes between Whence's quarter tones were heard late, not detuned by the glide; docs/PLAYBACK_SETTINGS.md › Microtone bends); the audit's predicted.tsv gives a row's cents from the key it is sent on (before, a respelled note read a semitone off in the heard check); **`[slurs] runBends`** (2026-10-29, OPEN_ITEMS 5; 1 in Recommended and by default since 2026-10-31 (the owner, A/B page, off vs the fix: "the fix is obv better"), 0 in Library default): in a fast legato run (written IOI < 250 ms, touching transitions) on a patch with the map's `<Articulation runLag="fingered:D:tau [bowed:D:tau]">` (SSO's own pitch there: the keys delayed D, smoothed tau; `sso_run_lag.json`, fitted by `legato_glide/runlag.py` to Whence's export with the bends taken out; Violins 1 / Violas / Celli - Performance, the Celli 90:90 since 2026-10-30; no bowed lag since 2026-10-30 (branch `run-bends-fingered`): a bowed transition's own window gets no correction, its key from its heard start) the bend also corrects that lag, every 10 ms by a 30 ms least-squares horizon through bendSmoothing, within the range, no send more than 80 c from the one before (`RUN_SLEW_C`, 2026-10-30) (`runBendPath` in `libraryPitchBends`; only bends change); predicted offline (`runlag_sim.py`) quarter tones 24-40 -> 38-53 %, 12-TET 51-58 -> 65-71 % in tune (Whence, bars 1-14) (docs/PLAYBACK_SETTINGS.md › Run bends); **`[notes] transportReset`** (2026-10-21, branch `sso-script-fixes2`; **1 in Recommended and by default, 0 in Library default since 2026-10-28 (the owner: "I don't see a reason not to, since it literally changes nothing within one play")**): the patches with `<Instrument transportResetScript>` (SSO's five string Performance patches and Tuba Solo - Performance: "Reset on transport" saved off, read from the patch's script; they reset only after 1.5 s without a note-on) load with it at 1 (`playbackScriptValues`; resave puts back every `SoundLib::playbackScriptKeys` key), so passes repeat (VM kthost: with fixedTransitions -95 .. -100 dB, without the reset -3 .. -6); Create Live Set embeds it (docs/PLAYBACK_SETTINGS.md › Transport reset). **`[levels] hallGuard`** (0 by default and in both presets until the owner decides): a CC1 step down of `<Instrument hallDrop>` (10, SSO's Hall trigger on the five string Performance patches: an extra diminuendo release on every held key, VM: 14-25 dB under the note) or more while a key is held on the route is sent as steps of 9, 1 ms apart (`libraryHallGuard`; the audit's HALL; made by a subito dynamic in a slur, fp; › Hall guard). **`[slurs] fastTransitions`** (1 in Recommended and by default, 0 in Library default since 2026-10-28: the owner, A/B, "B is maybe ever so slightly better? the last 3 notes sound slightly more in time"): a legato note-on < 15 ms after the one before on its route at velocity 20-64, which SSO ignores (the audit's DROPPED), goes at 65, the same family (`libraryFastTransitions`; › Fast transitions). **`[slurs] plainAccents`** (2026-10-20, branch `plain-accents`; **2 by default and in Recommended since 2026-10-21**, 0 in Library default: the owner chose 2 "if accented is normally that loud", branch `accent-level`; heard at the real level, Whence bars 7-9 on real SSO, 2026-10-21: "fine"): a plain note with an accent / marcato / soft accent plays 1 MS4.7.5's offset for its articulations (40 log10 share, strings +5.3 dB; `marcatoLevel`'s `plainAccentDb`, by CC1 along the patch's curve, slurred notes too), 2 an accent or marcato that plays an attack the accented attack (122; a touching note re-attacked by `reattackGap`; slur transitions stay 64) by CC11 at real players' +5.0 dB over the spiccato attack (`PLAIN_ACCENT_DB`: Spheres K. 550's sf at f, 90 % [+3.8, +6.0], an assumption for ">"; D = 5.0 - the map's `accentedLevel`, SSO's 122 - 64 by pitch and played length, every semitone x 100-450 ms, `sso_accent_attack.json`; `performanceNote`'s `accentDb`; a positive D only up to CC11 127), 3 both (› Plain accents › Level of the accented attack). The CC102 "since note-on" ramp was measured inert (HANDOFF.md › SSO legato fixes). The Performance portamento plays at velocity 5 (1-9: PORT without FING; the Basses' 10). Off (Library default), SSO slurs play the All techniques
  longs, each note its own attack, early by its onset; a legato transition needs an articulation with `legato` first
  (`Articulation::playsTransitions`), which the SSO map has only under modifier `performance` (the Performance patches'
  Legato; Violas - Performance transitions at velocity 100, map `legatoVelocity`, approved with `[slurs] performance`;
  `sso_legato_*.json` kept). There the five string sections' Performance
  Legato play each note's **Performance technique** (`libmscore/performancetechnique.h`: attack smooth / spiccato /
  accented, transition portamento / fingered / bowed, Auto from the notation; Inspector › Note, every selected note at
  once, shown only when one of them plays on a Performance patch as rendered (`NoteLane::onPerformancePatch`; values
  kept), a choice resets the note's own values (`Note::undoChangeProperty`); each note's own velocity and join
  (`Pid::LIBRARY_VELOCITY` / `LIBRARY_JOIN`, in the same metaTag; a join down to the map's `legatoGap` still a transition),
  edited in the **Velocity and Join lanes** (`libmscore/notelane.h` the model, `mscore/notelanes.h` the lanes: a stem
  per note under the part's automation header, coloured by technique band or, on velocity techniques, by dynamic
  band ppp … fff; Ctrl+C / Ctrl+V copy and paste stems' values by note order into the same lane only, `NoteLane::copyClip` / `pasteClip`); metaTag `performanceTechniques`, which also holds each note's **own technique** (Inspector › Note › Technique, the
  map's sounding articulations of its part's patches, `"<patch>|<articulation>"`, `Note::libraryTechnique`: replaces
  the notation's choice in `SoundLib::chooseNote`, so routes, lanes, Live's clips and plain set follow); map `attacks` `transitions` `reattack` `reattackGap` `overlap` `avoid` (a faulty
  round robin's velocities, steered to a clean one: `sso_bad_samples.json`, `[notes] avoidBadSamples`) `avoid` (a faulty round robin's velocities: `sso_bad_samples.json`, `[notes] avoidBadSamples`)
  `legatoGap`): its velocity,
  and the note before ending `overlap` ms after it or `reattackGap` ms before it (`performanceNote`, `libJoins` in
  `finishLibraryEvents`; not across tuning lanes). **Held notes start early** by their
  measured onset (`<Articulation onset>` by pitch; metaTag `soundLibraryOnsetEarly`); the note before on its patch keeps
  `keepMs` of its length as played. A transition's measured delay (`<Articulation legatoDelay>` by interval, shortened
  after a short note: `libFastDelay`) times only a bent transition's glide. HANDOFF.md has the open timing items.
  Per-note levels (a marcato's, track levels) share one CC11 / CC1 schedule per route, `libraryNoteLevels`
  (`libLevels`; dB add up); a chunk's end undoes none of them, nor even levels' glide: the level in force on a route stays
  until the route's next note-on, in a later chunk too (`MidiRenderer::libCarry`, by the chunk's end tick, cleared with
  the renderer's state; branch `chunk-end-levels`, 2026-10-25: before, Seq's 10-bar chunks put CC11 back to 127 under the
  last notes' releases, the owner's Whence with empty bars after bar 16 +7 .. +24 dB at bar 17; since `chunk-equal` the
  carry matters only at a repeat segment's boundary and at a playback window's start: › Chunks); with **`[levels] transitionLevels`** (slur-joins 2026-10-22; **2 in Recommended and by
  default since 2026-10-28**, 0 in Library default, 1 never heard) 1 a legato transition's CC11 level moves over its glide
  (leave -> arrival, linear in dB) instead of with its note-on; 2 (bowed-join, a fix: the owner, hall.musicxml, "the tail of
  the last note suddenly get louder as the next note enters": CC1 48 -> 96 with a bowed note-on, ~+12 dB on the p note's
  end before the f is heard) every step the transition's note-on brings on its route, the dynamics CC and CC11 from whatever
  sets it, held to the leave and ramped to the arrival, the rest of the schedule as rendered (`libraryTransitionTails`)
  (docs/PLAYBACK_SETTINGS.md › Transition levels); with **`[levels] attackLevels`**
  (attack-levels 2026-10-22; **2 in Recommended and by default since 2026-10-23**, 0 in Library default: the owner, "B
  better", Whence's Violas bars 11-13 with even levels) an attack whose route still rings the note before (within that
  note's `release`) takes its CC11 level, even levels' glide start too, not with the note-on but 1 at its note-on + its
  measured onset (`plainAttack`, else the articulation's), 2 at its written time (the early start undone, plain
  Performance's `plainShift` too: `libAttackShiftMs`; rises only, a drop with the note-on: check471, an accent's level on the next attack) (`attackSwitch`, `libAttackOnset`, `libAttackLevelAt`;
  docs/PLAYBACK_SETTINGS.md › Attack levels).
- **Staccato length** (branch `staccato-length`, 2026-10-24; **`[shorts] staccatoLength` 1 by default and in Recommended, 0 in
  Library default**: the owner heard the A/B clips, Violins 1 "B" (slow) / "same" (fast), Celli "B", Oboe and Bassoon "same"; readings reviewed 2026-10-24, https://claude.ai/artifact/GNwFDt1PcNHJ1UWoWugvDc, only confirmed
  ones used): 1 = a library
  staccato (staccatissimo; not a portato) is meant to sound, and is held, no longer than real players sound one: the map's
  `<Instrument staccatoLength staccatissimoLength>` (s; `gen_spitfire_sso.py` STACCATO_RULES from `staccato_lengths.json`,
  RESEARCH.md 2.5a: winds 0.095-0.16, strings 0.19 / low strings 0.22, staccatissimo 0.16), capping the meant length where
  `from` chooses the short (`SoundLib::staccatoKind`, `chooseBases`) and the note-off (`config.ms4Dur`, both renders, so Live's
  clips too); docs/PLAYBACK_SETTINGS.md › Staccato length.
- **Even levels** (`libmscore/evenlevels.h`, `mscore/evenlevelsjob.h`; the owner, 2026-10-12, the blind test's Glide;
  **`[levels] evenLevels` 1 in Recommended and by default, 0 in Library default** (the owner, 2026-10-13)): SSO's Performance
  legato swells inside slurs by context (no table predicts it), so *Tools › Even Out Levels…* / `--even-levels <score>`
  renders the slurred passages through the hosted plug-ins (3 passes), measures the perceived envelope and fits a per-note
  CC11 glide flattening it to each slur's median (linear in dB between nodes halfway from each note-on to the next,
  `EvenLevels::nodeTime`, every 5 ms; the owner, 2026-10-14: S, `even_180_check.py`, before the nodes sat in the next
  note), / 1.28, then once more, the round whose envelope lies closest to its slurs' medians kept; each
  note keeps its value and a hash of its rendered context (`Note::evenLevel` / `evenContext`, metaTag `evenLevels`): a
  changed context is stale (Inspector › Note › Even level), not played. `MidiRenderer::libraryEvenLevels` glides on top of
  the CC11 schedule (the patch's notes lower by its largest value, never cut at 127; its gain stage adds it back,
  `TrackDelays::evenDb`); Live's clips and the plain set carry it.
  **Slow slurs are skipped** (the owner, 2026-10-13, blind test: slow slurs sound the same uncorrected and corrected; HANDOFF.md
  > Slow slurs need no correction): a passage whose every interval between joined notes is >= `EvenLevels::SLOW_IOI_S`
  (350 ms since 2026-10-14, was 500: the owner heard 350 / 500 ms the same corrected or not; `slowPassage`) is neither
  measured by the job nor in the background: no render, no value, no wait.
  **Measured in the background near edits** (`libmscore/evenauto.h` the Session, `audio/vst3/evenmeasure.h` the shared
  render + measurement, `mscore/evenautomeasurer.h` the threads, triggers and Play's wait, its instances' setState on a
  worker thread: `SoundLibraryExport::beginInstance`; the owner, 2026-10-13: a note
  plays its measured value or playback waits for it, never a predicted one): whenever evenLevels plays and the library is
  hosted, after an edit (debounced, `MuseScore::endCmd`) and at score open the score is rendered once; notes without a
  current value take the user's cache first (`<setups folder>/even cache.json`, keyed by the contexts of notes j-3 … j+3:
  undo / redo restore without rendering), the rest are measured in windows (note +-3, run-in 2 / Basses 4, run-out 2, 0.5 s
  lead; rounds 1 / 2 = fit / 1.28, always both; each pass a transport restart; 2 passes, a 3rd only
  when they differ by more than the patch's `earlyStopFor`; a gap of 1 s / Basses 1.5 s after the last note-off,
  `gapFor`, 3 s and always 3 passes for patches not measured: `sso_even_gap_check.json`; blocks of 4096) on plug-in
  instances of MuseScore's own (one per part and patch, a worker thread each, offline, paused while playing; each renders
  its queue by `EvenAuto::priority`: from the play position / Play's start forward first); values written without an undo
  step (`MasterScore::setEvenLevelsWritten` marks the score modified), 0 dB when none is needed. `Seq::start` waits
  (status-bar countdown, Stop cancels; the owner, 2026-10-13: also at score open, "Wait for the measurement") only when a
  note being measured, queued or rendering, is in what is about to play (`PlayStart` to the loop's / score's end). VM:
  ~1.5-2.5 s of rendering per edited part. Untried in MuseScore with Kontakt / SSO (headless with the test synth: waits,
  then plays).
- **Mic compensation** (`libmscore/contextlevel.h` › Mics; the owner, 2026-10-15: "mic changes then only affect tone and
  sense of space, never the balance"; **`[levels] micCompensation` 1 in Recommended and by default, 0 in Library
  default**): every level is measured at the patch's default mix (SSO: the Tree alone at 127). A part whose mic
  Controllers (Mic 1-5 level, Mic Mix Distance) or their lanes give another mix plays C = -(that mix's level) more:
  the mics' powers added, each mic alone at full against the default (map `<Instrument micLevels micDefault>`), the
  faders' law and Mic Mix Distance's (map `<Mics fader mix>`; with Mic Mix Distance set the mic values sent with it
  don't play: SSO's script overrides them). By the patches' volumes as the section level (`Volumes::micTop`, on the gain stage),
  automated mics each note's own C at its start by CC11 (`trackLevel`); Live's sets and clips carry both. Measured on
  the VM (`tools/soundlibraries/mic_levels.py` -> `sso_mic_levels.json` -> `gen_spitfire_sso.py`): every patch with a
  section level (44), the strings' Performance patches, the harp, the timpani (not its Close mic: empty offline) and the Grand Piano;
  other extras take their main's (an assumption); power-sum
  error <= 1.4 dB, Mic Mix Distance's fit 2.05 dB leave-one-out (docs/PLAYBACK_SETTINGS.md › Mic compensation).
- **Gain stage and master trim** (`libmscore/contextlevel.h` › Gain stage and master trim; the owner, 2026-10-15: "yeah sure
  let's go with utility"; docs/PLAYBACK_SETTINGS.md › Gain stage and master trim): every patch's levels above (level rule,
  section balance, realRange tops, mic compensation, even levels) play on its own gain stage, `TrackDelays::gainStageDb`:
  hosted, the slot's gain (float, no cap; with hosted tuning slots the instance's slot's, once per instance: the copies'
  slots get the same value, unused, `tst_liveequivalence::hostedSlotsGain`); the plain set and route set, a stock Utility
  named "MuseScore gain" after the Kontakt (one per multi; ±35 dB, a rest on the fader; `LiveSetWriter::kontaktGains`, `utilityDevice`; read back by name, Update Live Set
  edits or inserts it; the fader stays the Mixer's, the owner's); MIDI out by CC7 (`TrackDelays::midiVolumes`). Nothing
  lowers the other patches any more (no S / S'); one **master trim** per score (`ContextLevel::masterTrim`, cached) lowers
  every patch only as much as the loudest moment needs: power sum of the sounding library notes' gain + CC11 + calibrated
  level + the map's `<Dynamics crest>` (14.5 dB, the max of 392 SSO string notes' loudest 50 ms over their calibration,
  `crest_sweep.py` -> `sso_crest.json`), trim = max(0, peak); without calibration or crest the old S'. Checked in Live 12.4.6
  (VM): the Utility's gain plays to 0.001 dB, Live re-saves the same XML; Whence: trim 0, HANDOFF.md › Gain stage.
- **Low cut on string sections** (branch `string-lowcut`, 2026-10-30; **`[levels] lowCut` 1 in Recommended and by default, 0
  in Library default**: the owner asked why the strings' new-bow kick wasn't fixed): every patch of SSO's five string sections
  plays through a 4th-order Butterworth high-pass at the map's `<Instrument lowCut>` (just under the section's lowest note:
  Violins 150 Hz, Violas 100, Celli 50, Basses 25, a compromise; `gen_spitfire_sso.py` LOW_CUT), taking out the new-bow
  samples' low bow / body noise; `Vst3Synth::setLowCut` / `setExportLowCut` on the slot's output in MuseScore's process
  (helpers too), from `SoundLibraryHost::applyMixer` and `SoundLibraryExport::setExportMix`; Live and MIDI out not yet
  (LIVE.md › What still differs; docs/PLAYBACK_SETTINGS.md › Low cut).
- **Playback adjustments are configurable** (`libmscore/playbacksettings.*`; **docs/PLAYBACK_SETTINGS.md** is the
  inventory): built-in defaults → `<dataPath>/playback.ini` (written with every key when missing; Edit › Reload
  Playback Settings) → the score (metaTag `playbackSettings`, plus the older onset-early / lanes metaTags; removed
  keys are listed in `REMOVED` and ignored silently). Read a
  setting with `Playback::value(id, score, mapValue)`; a new adjustment gets a `DEFINITIONS` entry, a row in the
  inventory and a layers test. UI: Mixer › Advanced Options… › Playback adjustments (`mscore/playbacksettingswidget.*`). **Presets** (Recommended: every measured technique early, each articulation by its median onset, `heldNotes/byPitch` 0 (1: by pitch), calibrated levels, `slurs/quick` 2, `slurs/quickLevel` 1 / Library default: `Playback::presets()`, a rewrite of the whole playback.ini (preset values + every other key at its default, the old file kept as .bak), the widget's Preset box; docs/PLAYBACK_SETTINGS.md › Presets).
- **Chunks** (branch `chunk-equal`, 2026-10-25; a fix, no setting; header comment of `MidiRenderer::renderChunk`): Seq
  plays a score rendered in chunks (`getChunkAt`, 10 measures at least), the export, Live's clips and the plain set in one
  chunk a repeat segment; the chunks play exactly the whole render's events. With library parts and `minChunkSize > 0`
  (Seq, the audit, even levels, real-range / master-trim / note-lane renders, clicked notes) each chunk is rendered inside
  a **window** (`libWindow`: its repeat segment's measures from `WINDOW_BEFORE_S` 6 s before it, and the start of every note
  still sounding there, to `WINDOW_AFTER_S` 4 s after it, both plus the track-delay / start lead) and only the events
  from the chunk's start to its end are kept (`libCut`: a boundary belongs to the chunk rendered first next to it; a chunk
  rendered before the one before it, a seek, keeps its own notes' early events before its start, a pre-roll). What
  lasts longer than the window comes in from the window before: the levels and even levels' glide in force at the next
  window's start (`libSnapshotAt`, into `libCarry`), even levels' step grid of a run going on (`LibCarry::evenRuns`),
  each note's microtone spelling as first made (`libSpelled`). Trace and even-levels information: the chunk's own notes
  only; `setKeepWindow` (clicked notes) gives the whole window, its state in force. Before, each chunk rendered alone: a
  note on a chunk's first tick couldn't start early (up to 740 ms late at Seq's size, Strings.musicxml), joins / keepMs /
  bends / switches / pedal / levels broke at boundaries. Untouched where `minChunkSize` is 0 (whole render byte-identical).
  Proof tool: `tst_liveequivalence chunkCompare` + `tools/playbackverify/chunk_diff.py` (docs/TOOLS.md › 1); test
  `tst_soundlibrary::chunkedEqualsWhole`; `MS_CHUNK_WINDOWS_OFF=1` renders each chunk alone, to compare. Costs: a
  10-bar chunk ~the same (W, 48 bars: 40 / 47 ms), a 1-bar chunk ~2x.
- **Play from a tick** (`libmscore/playstart.h`, `Seq::setPos`): notes that start early (onset, negative track delay) have their note-on, switch and controllers before their beat, so the transport starts at the earliest note-on of a note written at or after the tick (`PlayStart::start()`, a pre-roll) and skips the previous notes' note-ons/offs there (`skips`); the cursor stays at the tick (the owner, 2026-10-10: the first note of a selected measure sometimes silent).
- **Clicked / entered notes** (`libmscore/notepreview.h`, `Seq::playNotes`; the owner, 2026-10-15): by the part's effective mode, MS3 3.6's click (velocity 80), MS4 and library the note's own events from its playback chunk (rendered on the click's own renderer, cached) with the switch / controllers / bend in force on its route, sent at once; stopped only (playing, *Live plays the score*, a clip tab's Live track: as before); docs/PLAYBACK_SETTINGS.md › Clicked and entered notes.
- Dynamics calibration (*Check articulations* › Dynamics or `--check-dynamics`) → `dynamics.json` (the patches checked so far);
  every patch it lacks from the shipped `share/soundlibraries/<library>.dynamics.json` (SSO's from `sso_sound_dynamics.json`
  by `tools/soundlibraries/calibration_from_sound_dynamics.py`, the five string sections' Long from `sso_string_dynamics.json`
  (`string_dynamics_sweep.py`, 2026-10-12: each point 4 times; the check's single notes caught alternating round robins,
  Celli's Long came out flat and not monotonic: docs/PLAYBACK_SETTINGS.md › Calibrated levels); `SoundLib::readCalibration`, since 2026-10-11: before, a
  user's file replaced the shipped one whole, and the Windows VM's, Solo Violin 1 only, left the strings uncalibrated). Read for calibrated
  levels and the Inspector levels (the curve's inverse). A curve may carry per-pitch curves (`"pitches"`; SSO's
  strings' Long (Rachm.), `sso_rachm_register_curves.json`, `-r` of the script): `DynamicsCurve::atPitch` (linear in dB
  between the nearest measured pitches), `raise` (the lowest CC1 reaching the level: crosses dips); quickLevel from
  `sso_rachm_context_match.json` by `rachm_quick_levels.py` (docs/PLAYBACK_SETTINGS.md › Calibrated levels). The strings' All techniques
  Long against CC1 16-127, every 3 semitones, Celli and Basses every semitone (VM, 2026-10-11 / 10-14, with each round
  robin's takes): `sso_cc1_curves.json` by `sso_cc1_sweep.py` (the section balance's SSO side and, by `real_range.py`,
  the map's longLevel; not read by MuseScore).
- Controllers: map `<Controller>`, set **only by automation lanes** (the owner, 2026-10-15: "remove this entirely, we'll
  use automation instead … just make it like ableton"; the Sound Library window's *Controllers…* and its window are gone).
  A **flat lane** (`Lane::flatValue`: one value from tick 0) is the controller's setting (`libmscore/partcontrollers.*`):
  played as the old per-part value was (a CC ahead of the notes with its staff texts, a plug-in parameter set on the
  instance at each sync and live as the lane is edited, `audio/vst3/librarycontrollers.*`,
  `SoundLibraryHost::applyPartSettings`; in Create Live Set's Kontakt state; mics by the patch volume); any other lane as
  automation. Unautomated: the map's default, else the patch's own (`PartControllers::unautomatedValue`: the setup's
  value recorded at its last load, `parameter values.json` in the setups folder, else the map's **`patchDefault`**, Kontakt's
  own value measured on the VM, `sso_controller_defaults.json` by `controller_defaults_from_extract.py` (2026-10-15, all
  767 SSO controllers; a loaded setup differing logs a line in `load times.log`), else the map's mic defaults), drawn
  as Live's **dashed line**. **Live (the owner, 2026-10-15, "Option B" and "yea sure")**: Create / Update Live Set write a
  flat plug-in-parameter lane as the Kontakt panel parameter's plain value (Configure slot, Manual, no envelope; a
  changing lane as automation; a flat CC lane stays a one-value envelope: no Live parameter); the read-back takes a panel
  value turned in Live without automation into a flat lane (`LiveTracks::knobs`, `knob:` liveHash, `Written::params`;
  both changed: `Automation::merge`'s dialog, undecided ones left as Live has them); Kontakt's own window isn't read. The old metaTag `partControllers` becomes flat lanes at load (`PartControllers::migrate`,
  read302.cpp) and is never written. Automation: `libmscore/automation.*`, metaTag
  `automation` (points step / linear, a ramp may carry Live's Bézier control points; two points at one tick are a jump,
  as Live writes and reads one: the later one holds from that tick everywhere, `Lane::events` sending one value a tick,
  branch `lane-steps` 2026-10-20). Every lane is editable;
  `Lane::playedByLive` (liveHash / pointsHash in its extra) says whether Live's set plays it; an import merges
  (`Automation::merge`, newer edit wins, both changed: a dialog per lane). **Editor**: `mscore/automationlanes.*`
  (Continuous View, a header row under every library part, each folded / open at will, Alt-click all: `mscore/lanefolds.h`,
  per score, the session's, never saved; Live's gestures; header lists them); **lane heights** (`mscore/laneheights.h`, the owner 2026-10-28): drag a lane header's lower edge (Alt: every lane; Alt + / Alt - the focused lane: Live's), double-click the edge / the menu's Reset Lane Height / Reset All Lane Heights (ours: Live has none); spatium, 2.5-30, default 5; per score, the session's, never saved, as the folds, model
  `Automation::Edit`, one undo step per gesture (`undoWrite`; heard at once while playing: `Seq::renderAgainPlaying`);
  an empty lane draws the dashed line (`AutomationLanes::paintEnvelope`), a drag on it sets the value from the start; points
  on a bar's tick sit on the bar line (`Automation::pointX`; stems stay at `xAtTick`); the Velocity / Join lanes draw their
  rubber band and Ctrl+D copies the selected stems' values to the notes right after (`NoteLanes::duplicateSelected`); a Dynamics lane replaces the notation's CC1 from its
  first point. **Time selection** (Live's): the score's range selection on a part with a header (whole bars,
  a light band over its rows); Ctrl+D (score or lane focus; MS3 has no Ctrl+D) copies every lane of the part and each
  note's attack / transition / velocity / join to the bars after, one undo step, then selects them
  (`Automation::duplicateTimeSelection`, `Edit::duplicateSpan`, `PerformanceTechnique::duplicateNoteValues`).
- **Read from SSO's scripts, corrections** (2026-10-21, branch `sso-script-fixes2`, docs/SSO_SCRIPT_KNOWLEDGE.md): `<Instrument
  avoidCC="112 113">` on Trumpet / Tenor Trombone Solo - Total Performance (a helper script takes the note velocity from
  CC112 / CC113; `MidiRenderer::libraryAvoidCC` drops them, the audit's AVOIDED CC); SSO's machine-wide shared settings
  (`%LOCALAPPDATA%\Spitfire Symphony Orchestra\Data\Share__Settings.nka`: a shared articulation mode, Reset on transport,
  transpose … override every patch) checked by the map's `<PluginSettings folder="local" format="nka" good=…>` as a patch
  loads (`SoundLibraryHost::checkPluginSettings`, status bar) and in every render's preflight
  (`RenderPreflight::setPluginSettings`). Tightness measured, not offered (docs/PLAYBACK_SETTINGS.md › Tightness).
- Percussion: `<Instrument keyShift="12">` on the Timpani (its keys sound an octave under their number: a written D2 goes on key 50; `KEY_SHIFT` in the generator, `libNote.key` in rendermidi.cpp), `<Instrument maxVelocity="126">` on the 13 metal patches whose Kickstart option "Roll on high vel." is ticked
  (127 would play a roll; `ROLL_ON_HIGH_VELOCITY` in the generator, `MidiRenderer::libraryMaxVelocity`; a correctness fix, not
  an adjustment: docs/PLAYBACK_SETTINGS.md). `<Drum key name>` lists every hit of SSO's kits, one-drum patches and ensembles
  (`tools/soundlibraries/sso_percussion_hits.json`, from the owner's window pictures); a technique off at the
  defaults is `<Drum name default="off"/>` (no key, never played). The nine `"<patch> (all on)"` `<Patch>`es set
  Kickstart's arrays whole (`%c2lsa`, `%4jwcn`, `%x4jsr`, `$nd5ia=0`); `KontaktSetup::unpurgeSwitchedOn` loads the
  sample groups they switch on (Kickstart's purge rule; test `kontaktKickstartUnpurge`). Script values may change
  length (`kontaktScriptValueLengths`). Mics on 3-mic patches are Close, Tree, Ambient.
- **Vial Instrumental** (branch `vial-map`, 2026-10-25; the owner approved; HANDOFF.md › Vial Instrumental): `share/
  soundlibraries/Vial Instrumental.xml` (hosted `Vial.vst3`) and `Vial Instrumental (Fuse).xml` (`Vial Fuse.vst3`, the
  .vialfuse mic mixers) are generated by `tools/soundlibraries/gen_vial.py` (ids from the SSO map): the owner's 13 "SSO
  imitations" Long presets, one articulation for every technique, `<Switch type="none">`, CC1 dynamics, CC11 127, CC21
  vibrato (64, senza / molto vib.) on Violins 1/2, Violas, Celli, flute, oboe, clarinet, bassoon, trumpet, tenor trombone (the last two since 2026-10-26). A **preset library**
  (`<Files presets>`, `<Instrument preset>`; README › Preset libraries): the patch's state is its preset file's bytes,
  read from Vial's data folder at every load (`SoundLib::presetFolder`: Library folder…, `MS_PRESET_FOLDER`, Vial.config's
  `data_directory`, Vital's, `Documents/Vial`); never the presets in the repo. Levels as SSO's (branch `vial-levels`,
  2026-10-27): the SSO instrument's realSteps / realSpan / realPlay, its own longLevel and SSO's sectionLevel(2) moved by a
  measured offset (`vial_levels.py` -> `vial_levels.json` -> `gen_vial.py`), so realRange / sectionBalance play a Vial
  Long at SSO's level on loud1sDb (the loudest 50 ms of a note's first second, the owner, 2026-10-29; test within 0.5 dB; on loudDb, the 3 s peak, check <= 0.53 dB until then; other measures: winds +2 .. +7 phon louder; docs/PLAYBACK_SETTINGS.md ›
  Vial levels); no other SSO measurement (calibration, onsets, mics). An engine as SSO is
  (the global box or a part's *Part playback*, together with SSO parts: › Several libraries; Fuse its variant). Live / MIDI out untried (LIVE.md › What still differs).
- **Ample guitars** (branch `ample-map`, 2026-10-18; HANDOFF.md › Ample guitars; **dormant since 2026-10-29**: the owner is
  not buying them, so the map is no longer shipped: it is `tools/soundlibraries/ample/Ample Guitar M.xml`, kept for the tests
  and a later purchase; to ship it again, move it back into `share/soundlibraries/`): `Ample Guitar M.xml` is generated, never hand-edited: `python3 tools/soundlibraries/gen_ample.py [--product <code>] <amphost dump>
  [out]` (the dump stays outside the repo: `~/MuseScore/ms3fork/private/ample-map/agm-info.txt`). **A new Ample guitar**
  (AGSJ, AG12, AGL, AGT, after purchase): **docs/AMPLE_PIPELINE.md** (branch `ample-pipeline`, 2026-10-21): amphost
  (`tools/soundlibraries/amphost/`) -> `tools/ample/measure_ample.py` (VM) -> `ample_<code>.json` -> `gen_ample.py
  --product` (`ample_products.py`: per-product facts, TODOs for what is unmeasured) -> `tools/ample/check.py` (VM, PASS /
  FAIL). Hosted `AGM.vst3`, not Kontakt (no `<Files>`:
  no setups made, `makesSetups` false); the steel-string guitar ids; every plug-in parameter (124) a `param` controller by
  title with `steps` / `display` (the lane editor's text: `SoundLib::controllerText`), CC7 / CC1 / CC64, keyswitches 24-30
  (`<Articulation keyShift>`: a natural harmonic's key = sounding - 12), dynamics by velocity (`<Dynamics cc="-1">`),
  `maxVelocity` 126 (127 is Pop). **State settings** (`<Controller state values>`, `PartControllers::stateValues`,
  `audio/vst3/pluginstate.h`): the sample set (`DBName`) written into the part's state as it loads
  (`SoundLibraryHost::setupStateWith` for a non-Kontakt library; base: the setup file, else `defaultState`, a new
  instance's), in the slot's `values` key (`PartControllers::valuesKey`: many state lanes changed before a sync = one
  load; an edit leaving every written text as it was = none), so a change loads the part again. Every state-only setting
  is one (ample-fixes2, 2026-10-20, measured: `~/MuseScore/ms3fork/private/ample-state/RESULT.md`; `gen_ample.py`
  state_settings): 204 `state="Parameter:…"` (menus `values`, numbers `range` / `decimals`, written as Ample shows
  them, FX toggles 1 / -1; ranges not seen in the GUI assumed) and three velocity layers `Layer[ID=n]`; Master Tune left
  out (setState ignores it). The map's `<State>` writes Ample's MIDI-learn defaults into every instance's state (CC7 =
  M-Master Vol, for MIDI out; `Library::fixedState`, `loadStateValues`); hosted and in Live the CC7 (volume lane,
  strum levels) plays as **M-Master Vol** (map `hosted="m-master-vol"`, `Controller::hosted`,
  `MidiRenderer::libraryHostedParameters`: same tick, CC / 127, measured bit-identical, no lead; MV.md in the private
  ample-state folder), Live's plain set as the Ample track's automation, the route set by the Link device; `<PluginSettings>` warns when the global `AGM_info.xml` has midiGuitarMode on
  (`SoundLibraryHost::checkPluginSettings`). **Guitar notation** (phase 2, branch
  `ample-phase2`, 2026-10-20; HANDOFF.md › Ample guitars › Phase 2): one renderer pass, `MidiRenderer::libraryGuitar`
  (`libmscore/guitarplayback.cpp`, its header comment is the design), run last in `finishLibraryEvents`, over parts whose
  patch has the map's `<Guitar>` (`SoundLib::Guitar`, `readGuitar`; generated: `gen_ample.py` GUITAR / STRUM_LEVELS /
  GUITAR_FX): strums (their set-up, strum mode / Detect / chord keys, the map's `strumLead` 50 ms before the strum key:
  AGM's strummer reads it only from an earlier block, else dark, partial strums, quiet every 7.5 s: not the demo, measured
  2026-10-21; a strum at the score's start: its set-up at 0, the score later by the start lead; on a later chunk's first
  tick as anywhere since `chunk-equal`), string keys, capo (M-Capo, capoLead ms before its note: Ample applies it from its next block), artificial / pinch harmonics, slides, HO / PO, FX noises, keyswitches
  re-sent (they latch); staff text "pick" / "strum" / "finger" → the map's extra patches Ample Guitar M (Pick) / (Strum)
  (an instance each, its own DBName state). Settings `[guitar] strums` / `softStrums` / `pinchHarmonics` (0; Library
  default 0; not in Recommended); `softStrums` 1 rolls a soft strum's notes, 2 (ample-fixes2, 2026-10-20) rolls the
  strummer's single-string keys at the map's `softRoll` velocity / spacing (meas3's level pairs v20 10 ms / v40 15 ms), no
  CC7. Live: the keys reach the clips and plain set, a strum's level as M-Master Vol (CC7 has no carrier);
  Create Live Set's Ample states carry each part's state lanes (their start values, as hosted playback loads them), and so
  do the command-line renders' instances (`SoundLibraryExport::loadInstances` → `loadInstanceSetup(…, part)`: verify,
  live equivalence, even levels, audio export; ample-fixes3). A strum's level and the volume lane under it multiply
  (ample-fixes3: the lane's CC7 until the restore x level / 64, the exact product in the event's tuning, which
  `libraryHostedParameters` uses; the restore is the lane's value then), hosted and in Live alike. Read back (Import / Update Live Set), the plain set's M-Master Vol and M-Capo automation is
  MuseScore's own render, not a lane (`livetracks.h` › Derived parameters, metaTag `liveTracks` › written › `derived`;
  branch `liveset-hosted-readback`, 2026-10-21): edited in Live it comes back as the volume lane only when it was the
  lane's plain values (no strum levels in it), else it is reported and Update Live Set keeps Live's until the score
  changes it (LIVE.md › The plain set › Read back).
- Microtones: Kontakt ignores VST 3 note tuning. **Tuning lanes** (patch copies per tuning, metaTag
  `soundLibraryLanes`) play by **varispeed** (`Vst3Plugin::setPitch`) or by **pitch bend** where the patch bends
  cleanly (`<Instrument bend>`; varispeed can't reach Live). **Every copy the music needs** (the owner, 2026-10-21,
  "2", replacing his 2026-10-03 rule that the free memory caps them; branch `all-copies`): a maximum only where the map,
  `[tuning] maxLanes` or the score's `soundLibraryLanes` sets one; the memory decides only a warning (`SoundLib::copiesMemory`:
  the copies, `routeCopies`, at 245 MB against `freeMemoryBytes`, `MS_FREE_MEMORY_MB` pins it): hosted a non-modal box
  and load times.log, once per score open and per change of the copies (`checkCopiesMemory` in soundlibraryhost.cpp);
  the command-line renders a WARNING on stderr / load times.log and the render preflight's `copies`
  (`RenderPreflight::setCopies`); never fewer copies, never a wait. A legato transition's bend glides with the
  transition's own pitch, straight, a value a tick (map `bendGlide`: when SSO's f0 leaves the note before / reaches the
  new one, by the note before's length; measured for the five sections' Performance patches), then holds; with the map's
  `bendSmoothing` (Kontakt's one-pole smoothing of a bend, measured) it is sent so that the bend as heard reaches the
  tuning at the arrival (one value from leave to arrival, then the tuning; branch `legato-bend-settle`); unmeasured
  patches: from the arrival (note-on + legato delay), a cent a tick (`libraryPitchBends`, `[tuning] bendAtArrival`;
  `tools/soundlibraries/legato_glide/`). The plain set writes envelope steps as hold pairs (Live ramps between points).
  **Live and MIDI out** (no varispeed; the owner, 2026-10-10: "I will not accept losing microtones"):
  `SoundLib::routes(…, forLive)` / `lanes(…, fixedTuning)` give a patch without `bend=` a fixed copy per tuning
  (`Lanes::fixed`, `Route::cents`; lane 0 nearest 0 cents; no max), which the export tunes by Kontakt's instrument
  Tune (`KontaktSetup::withTune`, measured within 2 cents in kthost and Live 12.4.6); the plain set gives a patch's
  copies one Kontakt multi (`PlainLiveSet::multis`, `KontaktSetup::withSlots`: a slot per copy, slot i on MIDI
  channel i; each other copy a MIDI track with its Controllers clip, MIDI To `DeviceIn.0.<i−1>`; measured identical
  to a Kontakt per copy, ~85-90 MB less each, Whence 20 Kontakts → 8), a bending patch's copies their bends as the
  clip's pitch-bend envelope (−8192 … 8191, measured). LIVE.md › The plain set › Microtones.
  **Hosted, the same since 2026-10-15** (`[tuning] hostedTuning` 1 slots, the default; 0 varispeed, as before; slots kept by the owner 2026-10-16, "yeah seems good", as Live plays them;
  the owner: "Whence takes absolutely forever to load all the patches because the microtonal stuff"):
  `SoundLib::hostedSlots` (a hosted Kontakt library with `<Tuning method="varispeed">`) makes routes() give Live's fixed
  copies, and `SoundLib::instances` puts a patch's copies of one part in ONE instance, a slot per copy tuned by Tune on
  its own MIDI channel (`SoundLibraryHost::tunedState` = `withSlots` / `withTune` on the setup; a made setup goes in
  plain first so that it is resaved, then tuned); `Vst3Synth::setLink` plays a route's slot on its instance's channel
  (`tuned`: no varispeed, no note tuning sent), the Mixer is the instance's (one output: one part, one patch volume),
  and so is the gain stage (`TrackDelays::patchGain` per part and patch, the host slot's mix: applied once, every copy
  the same; the plain set's multi one Utility; `tst_liveequivalence::hostedSlotsGain`, +12 dB on three copies).
  A plug-in parameter the part sets (Vibrato, mics) is set on the instance and reaches every slot (measured: the
  load-times run's phase 6, SSO, 2026-10-15); a setup that isn't Kontakt's falls back to an instance per copy by varispeed (`SoundLibraryHost::untunable`). The
  even-levels background measurer keeps one plain instance per part and patch (varispeed). HANDOFF.md › Kontakt slots.
- **Chords on monophonic patches** (branch `chord-voices`, 2026-10-20; the owner: "of course it should play every note";
  a fix, no setting): a patch marked `<Instrument mono="1">` (the generator's `MONO`: SSO's 42 Performance patches, one
  legato line per slot) spreads its notes over voices (from the lowest pitch, each the lowest voice free as written; a
  chord after a chord continues voice by voice), each voice its own tuning lanes ("voice lanes": `SoundLib::lanes`,
  `Lanes::voice`, `Route::voice`; without varispeed one untuned copy per voice), so every copy is a route as a tuning copy
  is: hosted slots, the renders' instances, the plain set's multi ("(voice n)" tracks), route set, clips, MIDI out. On a
  bending patch a plain chord note touching its voice's note before stays on its copy (a transition); a note goes on
  legato only from the note before on its own copy where a chord is involved (`otherLane` in rendermidi.cpp). Audit
  CHORD (fail). **A unison is one note** (2026-10-28: same pitch and tuning starting together, `Lanes::unison`, `MidiRenderer::libUnison`; no divisi count; `sameKeyEndsFirst` keys by copy). A part without overlapping notes: lanes as before. Whence: only Celli - Performance bars 9-16 change
  (docs/PLAYBACK_SETTINGS.md › Chords on monophonic patches).
  **Divisi level** (branch `divisi-level`, 2026-10-20; the owner approved; **`[levels] divisi` 1 in Recommended and by
  default, 0 in Library default**): while n voices of a mono patch's part sound together (as written, at the note's
  start: `Lanes::sounding`), each chord note, the line's too, plays 10 log10(n) dB softer by CC11 (2: -3.01, 3: -4.77 dB;
  `SoundLib::divisiDb`, `divisiLevel` in rendermidi.cpp, through `libLevels` / `libraryNoteLevels`), so the chord adds up to
  one section; a note held under a chord change keeps its start's level. **An assumption from player counts** (n equal
  incoherent groups as powers), no measured divisi data. Reaches clips / plain set / MIDI out as every note level; the
  master trim reads the rendered CC11 (docs/PLAYBACK_SETTINGS.md › Divisi level).
- Mixer on library parts: `Vst3Synth::setMix` per slot (hosted), CC7/10/91/93 on the routes (MIDI out).
  Mixer and playback edits never leave playback stopped (the owner, 2026-10-07): track delays / levels render again
  while playing (`Seq::renderAgainPlaying`, swapped in by the realtime thread); changes that may load patches (part
  playback, lanes options, playback mode, Reload Playback Settings) stop and start again where they were (`GoOnPlaying`).
- Track delays (`libmscore/trackdelays.*`, metaTag `trackDelays`; Mixer › Track delay / Tracks…): ms per part plus
  per patch / technique (added up, as Live adds a group's and its tracks'); `MidiRenderer::libraryTrackDelays` moves
  the library events (not for Live's clips; a note's CC11 / dynamic at its tick with the note, in the plain set's lanes too); with a negative delay everything else plays later by the earliest one
  (`libraryDelayLead`), so nothing is clamped at the start; the plain set writes them as TrackDelay, `livetracks.*` reads them back.
  **Start lead** (2026-10-14): a note whose early start (onset, smooth attackOnset) the score's start would cut plays its
  whole onset early, everything else later by the most cut (`libStartLead`, added to the track lead; the first chunk
  rendered once more when a later one comes first); Live's clips have no lead: the plain set plays that note before its
  written place by Track Delay (`EventMap::startEarlyMs`, `LiveTiming::Span::earlyMs`); the audit removes it
  (`EventMap::startLeadMs`). *Live plays the score*'s route clips still start it at 0 (LIVE.md › What still differs).
  Track levels (same metaTag, `levels`; same dialog): dB per patch / technique, added up, by CC11 per note
  (`libLevels`, `libraryNoteLevels`), so in Live's clips and the plain set's CC11 lane too; -42.08 .. +6 dB (Live's
  Volume top): a patch's loudest level is its headroom, played by its volume (slot / Kontakt track: `TrackDelays::patchGain`,
  `LiveSetWriter::kontaktVolume`), its notes' CC11 that much lower (`noteDb`).

**Hosting (VST 3)** (`BUILD_VST3`, on except macOS; preference `io/soundLibrary`)
- `audio/vst3/vst3plugin.*` (one instance; `settle()` after setState, or SSO's script resets parameters;
  `restartTransport()`: every playback start, export and verification pass shows the plug-in the transport stopped for one
  block, then playing, as Live's Play does, because SSO's scripts reset their round robins on a transport start;
  `Vst3Synth::playbackStarted` from `Seq::process`),
  `vst3synth.*` (64 slots = port*16+channel; `idle` must not hold the slots' mutex while sending: missed events
  wait in `_pending`), `mscore/vst3editor.*`, `mscore/soundlibraryhost.*` (`readyToPlay`, `sync`, preload at score open,
  `SoundLibraryExport`, *View › Sound Library…*). Plug-in modules stay loaded until the process ends, then
  `Vst3Plugin::unloadModules` (main, after the jobs / `exec`; the GUI releases its instances in `closeEvent`) unloads
  them with the SDK's ExitDll: left to the loader, Kontakt 8's teardown ended every MuseScore on Windows with 0xC000000D
  (ntdll `TppRaiseInvalidParameter`; 2026-10-16, kthost on the VM: with ExitDll first it exits 0).
  **Background loads** (`audio/vst3/backgroundload.h`, map `<Plugin backgroundLoad="s">`, Ample 60; branch `lane-steps`
  2026-10-20): Ample loads a state's sample set on its own thread after setState; an offline render's instances
  (`SoundLibraryExport::loadInstances` / `loadInstance`) each wait until the threads the instance started have ended or
  gone quiet and the memory stopped growing (load times.log; `MS_BACKGROUND_LOAD_WAIT`). Hosted playback
  (`SoundLibraryHost::sync`) waits the same since 2026-10-21 (branch `ample-pipeline`: `setStateWaiting`, on the load's
  worker thread or the GUI thread, one background-loading instance at a time; load times.log "(hosted)"; test
  `tst_liveequivalence::backgroundLoadHosted`; measured need: Pick / Strum 1.0 s, Finger 0.25 s). Even levels'
  background measurer doesn't wait yet (SSO only; HANDOFF.md › Lane jumps and Ample's background load).
  **Loading never holds the window** (branch `async-load`, 2026-10-24; the owner: "load without locking up the rest of
  MuseScore … just have the playback button grayed out during it"; a fix, no setting; header of soundlibraryhost.h):
  every hosted setState runs on a worker thread (`startSetState`, one at a time: `parallelLoads()` = max(1,
  `io/soundLibraryLoadThreads`); a made setup's copies' tunings too, Pending stage 1), the instance made, its setup read,
  settled, resaved and put in its slot on the GUI thread as before (made on a worker it hung Kontakt: load-small), the
  event loop running normally meanwhile. Play never loads: `readyToPlay` (Seq::start, the play action's handler,
  GoOnPlaying's `startAfterChange`) is true when every instance is loaded (then set up as `sync` did), else starts the
  background load and returns at once; `loading()` grays Play while stopped (`SoundLibraryHost::updatePlayAction`, from
  `MuseScore::changeState`, `loadingChanged`, `seqStopped`), the status bar "Loading sounds: n of m…"; an edit
  (`scoreEdited` from `MuseScore::endCmd`, 1 s debounce, stopped) loads new patches it needs, keeping unneeded instances
  as spares until the next Play (an undo); a change that stopped playback plays on once loaded (`whenLoaded`); `release()`
  (closing MuseScore, MIDI output) waits only for the setState in flight, pumping events without user input. Only `sync`
  still waits (audio export, tests). Linux, test synth at 1 s a setState, Whence (lag probe phases o / l / r): score open's
  main-thread stalls 20 x ~1.0 s -> none (max lateness 1117 -> 60 ms), Play during loading 17.4 s held -> 0.1 s (grayed),
  Play after a part's change 5.2 s -> 0.05 s, window-system input held up to 17.4 s -> 0.5 s (`tools/lagprobe/linux/
  input.py`, `run-input.sh`); a change while playing plays on once loaded (phase g); quitting during a load exits 0
  after the setState in flight (`close-test.sh`); loaded states and instances byte-identical
  (`tst_liveequivalence::asyncLoadHosted`). **With Kontakt on Windows the window still froze in process** (build #480, VM: Kontakt runs its setState work on the GUI thread even when called from the worker; HANDOFF.md › Build for the owner: #480): the helper processes below move that work out of MuseScore's process.
- **Plug-ins in helper processes** (branch `oop-host`, 2026-10-25; the owner approved, "seems good"; header of
  `audio/vst3/vst3remote.h`; HANDOFF.md › Out-of-process host): `[soundlibrary] hostProcesses` (MS_VST3_HOST_PROCESSES
  wins) -1 by default = one `MuseScoreLibraryHost[.exe]` (next to MuseScore3Evo.exe; `audio/vst3/vst3hostmain.cpp`) per
  physical core but one, at least 2 with 2+ cores / 4+ logical processors; 0 = in MuseScore's process as before; K = up to K.
  `Vst3Plugin::load` makes a remote instance whose calls a local Vst3Plugin in the helper replays in the same order on the
  same kind of thread (GUI calls on its main thread, where Kontakt then does its setState work, so MuseScore's window stays
  free; audio on its audio thread, MMCSS Pro Audio / SCHED_FIFO), so it sounds bit-identical (`tst_soundlibrary::remoteNull`).
  Control: pipes, sequence-numbered QDataStream messages; a busy helper (a long call in flight) never holds the GUI thread
  (parameter reads from the cache `prefetchParameters` fills on the load's worker; idle / setParameter not waited for).
  Audio: same block, no added latency: a shared-memory segment per instance (commands, block length, MXCSR, output) and two
  semaphores; `Vst3Synth` posts every slot (`beginProcess`) then collects in slot order; setParameter's processor side by the
  audio channel (CMD_EDIT, the next block as in-process). Loads in parallel helpers, biggest setup first, a patch to the helper
  holding it (Kontakt shares samples per process). A helper that dies, or doesn't answer a block for 15 s (`HUNG_MS`), wakes
  its waiters, its instances play silence; `SoundLibraryHost::checkHostProcesses` (idle timer) says so (status bar, load
  times.log), stops playback with GoOnPlaying, reloads the instances in a new helper and plays on; a patch whose helper ended
  `MAX_HELPER_ENDS` (3) times isn't reloaded; the even levels' measurer drops a dead instance's result and reloads it. No
  helper can start: in-process, logged. Closing MuseScore ends the helpers at once (`shutdownHosts(true)`). Offline renders
  use helpers too. kheap in a helper: scoped (#480's), never its audio thread. MS_AUDIO_TIMING's line carries the round
  trips (`Vst3Remote::timingSummary`). Live and MIDI out unchanged. kthost compiles vst3remote.cpp but always hosts
  in-process (VST3PLUGIN_NO_SETTINGS). VM, build 483 (Kontakt + SSO, Whence): open 15 s against 29-32 s in-process, event
  loop max 0.19 s against 6.6 s, no "Not responding"; playback callbacks over budget as in-process; heard-check export
  identical to build 480's; a killed helper reloaded in 7.6 s while MuseScore stayed up. Open: a ~2.6 s audio-callback
  stall per helper start while stopped. Untried: Ample and the editor window in a helper (HANDOFF.md › Out-of-process host).
- **mimalloc for setState only on Windows** (branch `sso-load`, 2026-10-23; scoped by `kheap-realtime`, 2026-10-24):
  `main()` first calls `ProcessAllocator::start()` (`audio/vst3/processallocator.*`), which loads `kheap.dll` from
  MuseScore's folder (`tools/soundlibraries/kheap`, MinHook + mimalloc in `thirdparty/`, built and installed into bin/
  with MSVC), pins it and starts it **scoped** (`kheap_start("mi-scoped")`): a new block comes from mimalloc only on a
  thread inside `ProcessAllocator::LoadScope` (`Vst3Plugin::setState`: Kontakt's setState makes millions of small
  allocations, 18.9 million for Violins 1 - Performance); every other thread (the audio callback, Kontakt's own
  threads, the GUI) keeps the system allocator, a mimalloc block grown there moves to the CRT, and no thread outside a
  load reclaims a segment a load thread left (`abandoned_reclaim_on_free` off). **Never mimalloc for the whole process
  again**: builds #474-#477 did, and the owner's live playback crackled on every audio device while renders nulled
  (`MS_KHEAP_OFF=1` cured it, 2026-10-24; not reproduced on the VM: HANDOFF.md › kheap and realtime audio). A block
  mimalloc doesn't own always goes back to the CRT, HeapFree / HeapSize / HeapReAlloc of a mimalloc block to mimalloc
  (MuseScore's own /MT code and the DLLs on ucrtbase still free each other's blocks); all hooks or none; nothing undone
  at exit. Load time only (renders null at the floor); `MS_KHEAP_OFF=1` the system allocator, to compare; without
  kheap.dll the system allocator. load times.log says which ("allocator: …", once per process). Linux / macOS: nothing.
  `MS_AUDIO_TIMING=<file>` (`audiodrivers/pa.cpp`): each play's audio callbacks against their budget, one line per play
  (the crackle test: callbacks over budget).
- **Setups are made by MuseScore** from each patch's `.nki` (`audio/vst3/kontaktsetup.*`), then replaced by
  Kontakt's own state after the first load (`resave`). The states loaded (voice limit 512, the copies' Tune slots) are **prepared once**
  and kept, byte for byte, in memory and `<setups folder>/prepared` (`prepared` in soundlibraryhost.cpp; 2026-10-16, the
  load profile: 0.5-1.8 s an instance before; Whence 5-11 s -> 0.2 s on the VM, renders null at the floor of two passes, HANDOFF.md › Load profile). Setups folder `<dataPath>/soundlibraries/<library>/`:
  `<patch>.vst3state`, `Kontakt empty.vst3state`, `made setups.json` (`MAKER_VERSION`), `checks.json` (raise
  `CHECK_VERSION` in soundlibrarycheck.cpp when old results go stale), `dynamics.json`, `load times.log` (qDebug
  doesn't show on Windows).
- Background jobs (own setups copy, lock, log; their `.bat` files in `bin` only with `-DMSCORE_INSTALL_MEASUREMENT_SCRIPTS=ON`, off since 2026-10-02): `--check-dynamics`, `--scan-keys`,
  `--measure-load-times`, `--verify-playback`, `--live-equivalence`, `--create-live-set`, `--update-live-set`,
  `--live-set-readback`. `--even-levels`, `--verify-playback` and `--live-equivalence` (MuseScore's side; `Options::wholeScore` is the old way, the null test's reference) load **one part's instances at a time**
  (`loadInstances(…, parts)`; the verification's mix = the parts' renders added up, `mixOf`): every part's at once ran
  the 16 GB VM out of memory on the passages' score (2026-10-16, 18 GB committed, growing as Kontakt played); a
  `std::bad_alloc` is logged as "out of memory" instead of ending the process. The verification's WAVs (`WavWriter`,
  `audio/vst3/wavwriter.h`) are valid from the start, their header brought up to date every second.
  Each logs a **render preflight** (`libmscore/renderpreflight.h`: calibration files' path + SHA-256 and each
  patch's source, setups folder / copy, playback.ini + preset; in the verify report, VERIFY.md › Preflight); the
  copy is synced at start (`syncJobCopy`: verify mirrors the main calibration, measuring jobs keep their own and warn).
- **Measuring a patch** (docs/MEASURING.md): `tools/soundlibraries/measure_patch.py plan|run|analyze|report` sweeps any
  patch on the VM (VM locks, resumable, ABBA takes, CC119 speed played / written) through **kthost**
  (`tools/soundlibraries/kthost/`: an offline host built from `vst3plugin.cpp` / `kontaktsetup.cpp`, `VST3PLUGIN_NO_SETTINGS`;
  in the Windows CI artifact's `bin/`); a transition's perceptual reference is f0 leave + 23.5 ms, not arrival (the owner,
  2026-10-21). Tone: `tools/soundlibraries/tone_check.py` (faulty round robins, clicks, noise against the patch's own
  median; `measure_patch.py --what …,tone --repeats N`; its `avoid` proposal goes into `sso_bad_samples.json` only after a
  person checked it; docs/MEASURING.md › Tone).
- Measurement runs (supervised: `superviseExtract`, a hang or crash retried then left out; resumable: a start
  skips what earlier runs of the same version did): `--extract-library … --extract-controllers
  --extract-pitch-bend`, `--check-timing`, `--check-rest [--rest-parts range,repeats,controls,legato,onset,shorts,
  legatolengths,legatopitches]`, `--all-sounds` (every sound, not only notated ones), `--window-pictures`
  (percussion windows). Readers → `tools/soundlibraries/sso_*.json`: `timing_from_check.py`,
  `dynamics_from_check.py`, `rest_from_check.py`, `onset_from_check.py`, `nki_articulation_details.py`.
- Checks and extracts: `audio/vst3/articulationcheck.*` + `mscore/soundlibrarycheck.*` (*Check articulations…*);
  `audio/vst3/pluginextract.*`; `tools/soundlibraries/` readers (`read_check_names.py`, `read_loaded_patches.py`,
  `*_from_check.py`, `read_plugin_data.py`) and `extract_library_files.py` (decrypts nothing, copies no script). **A patch's script may be read locally** (its PAR_SCRIPT chunks: plain text in the owner's `.nki`, nothing decrypted; the owner, 2026-10-16: "script text is fucking useful"): dumps live only outside the repo, in `~/MuseScore/ms3fork/private/sso-scripts/`, never committed, published or quoted on a page; numbers and rules taken from a script are recorded with that source ("read from the patch's script") and confirmed by measurement where they set playback; still never decrypt or bypass any protection.
  HANDOFF.md › When a check zip arrives has the procedure.
- Owner reports a problem: suspect Kontakt's MIDI channel (we send 1), editor sizing, sample loading in offline
  export; crackles / slow loads: memory (Kontakt's preload override at 30 kB).

**Live** (LIVE.md): Create Live Set writes the **plain set** (the owner, 2026-10-06: no automatic adjustments; since
2026-10-07 a MuseScore Link copy on each technique track, `PLAIN_LINKED`, for clip editing; `plainliveset.*`: section group › part group › a Kontakt track per patch + a MIDI track per technique carrying
its own switch, MIDI To the Kontakt; XML in `livesetxml.h`, `livesetclips.cpp`; `LiveSetKind`; LIVE.md › The plain
set lists what Live hasn't confirmed; **its technique clips hold the written notes** (the owner, 2026-10-11): a second
render without the timing in seconds (`MidiRenderer::setWrittenTiming`: no onset early, keepMs, cut, joins), each note
keeping its played times, which `libmscore/livetiming.*` puts back at playback: the technique track's Track Delay its
lookahead earlier, its MuseScore Link copy delaying each note (the `"Timing"` store, MuseScoreLink.js › Written clips;
LIVE.md › The plain set › Written clips); the Controllers clip stays at played times; *Live plays the score*'s clips
are still played (no Track Delay through Live's API: open), but **timing set in Live** stays (the owner, 2026-10-11): a
note moved or resized in such a clip keeps that timing as its override (`libmscore/livetimingoverride.*`: ms after its
written start / end, metaTag `liveTimingOverrides`, Inspector › Note › Start / End from Live; the device reports an edited
clip and doesn't write over it, `/ms/track` `base`, protocol 10; played by `MidiRenderer::libraryTimingOverrides`, so
MuseScore plays it too; LIVE.md › Live plays the score › Timing you set in Live); read back by `livetracks.*` (track keys in the Info text; metaTag `liveTracks`: the
written hashes, other tracks' mixer and its automation). **Update Live Set** (2026-10-12; Mixer › Advanced Options… ›
Ableton Live, `--update-live-set <set> <score>`; `libmscore/livesetupdate.*`, `mscore/livesetexport.*` `updateLiveSet`;
LIVE.md › Update Live Set): a saved plain set (closed in Live) edited in place as text: read back first (as the import,
plus a note moved in a technique clip as its timing override, paired through `liveTracks` › written › `notes`), then
only MuseScore's parts replaced where they differ (clips, Track Delay, the device's `"Timing"`, Kontakt parameter
automation, a Kontakt's state only when its slots' tunings changed), new techniques / patches / parts inserted, unused
techniques cleared, removed parts reported; the owner's devices, returns, sends, tracks and mixer byte for byte; a
`Backup/<name> [date time].als` first; an unchanged score writes nothing. Untried in real Live. Live plays the score as clips (`liveclips.*`), the route set (`livesetwriter.*`,
`livesetexport.*`), automation from a set (`liveset.*`), clip tabs that edit Live clips (`liveclipmodel.*`,
`liveclipedit.*`, `cliptempo.*`), the Max for Live device and Control Surface script (`tools/live/`), Live helpers
(`livehelpers.*`). **The full Live architecture map is in LIVE.md › Architecture map**: read it before changing any
of these.

**Playback verification** (VERIFY.md; checks in `audio/vst3/playbackverify.h`; since 2026-10-23 the offset by the audit's expected onsets, quarter tones on their own bins, the Python replica `tools/playbackverify/verify_detector.py` and a regression set: VERIFY.md › Detector regression): `--verify-playback`,
`--verify-audio`, `tools/playbackverify/read_verify_report.py`, faults via `MS_VERIFY_FAULT`.
**Playback audit** (VERIFY.md › Playback audit; `libmscore/playbackaudit.*`, `tst_playbackaudit`): the rendered library
events of a whole score checked against the map's own onsets (not the real sound) for overlaps on a route, unmeasured
swapped / early techniques or no onset, capped early starts (the cut), predicted arrival against neighbours (sent +
onset - written; Rasch 1979: > 50 ms fails) and level steps under a slur (values; proposed threshold 0.69 dB, Jesteadt,
Wier & Green 1977, owner to approve), DROPPED (fail: a legato transition sent < 15 ms after the legato note-on before
on its route at velocity <= 64: SSO doesn't play it) and LEAP (warn: a transition beyond an octave / 1200 cents: SSO attacks afresh); HALL (fail: a CC1 step down of `hallDrop`
while a key is held) and AVOIDED CC (fail: an `avoidCC` controller sent), 2026-10-21. The renderer
reports each library note's choice through `MidiRenderer::setLibraryTrace`. **Run it on Whence (quarter tones,
`Whence.mscz`; Recommended) before any build goes to the owner, and the heard check too** (VERIFY.md › Heard check:
`tools/playbackverify/heard_check.sh <score> [preset]`: the real sound, Kontakt + SSO on the VM per route, by default as MuseScore's own export renders it (`HC_RENDER=export`: `--verify-playback --verify-wav` with `MS_VERIFY_ROUTES=1`, one run per part; `kthost` the bare patches), each note's
heard time against its written one and the audit's prediction; ARRIVAL / RHYTHM (fail 0.10 / warn 0.05, approved 2026-10-16) / LEVEL, model drift per
patch x situation; on Whence and `share/verifyplayback/Heard passages.musicxml`; the audit alone can't catch a wrong
model).

### Edit lag (branch `edit-lag`, 2026-10-17)

After an edit only what changed is rendered on the main thread (HANDOFF.md › Edit lag): `ContextLevel::volumes` /
`masterTrim` keep the last 8 results per score (`RecentResults`, recentresults.h: the newest by key as before, older
ones while `untrackedInputs` holds; even levels written outside the undo stack count in `EvenLevels::generation`),
`NoteLane::onPerformancePatch` keeps its answers, and `EditWarmup` (mscore/editwarmup.h) makes the edit's real-range and
master-trim renders one chunk per event-loop pass while stopped (`ContextLevel::warmStep`; a caller asking earlier
finishes the same job; the host's mixer timer and the Inspector's Performance rows wait for it). Rule (the owner,
2026-10-16): sound-identical or dropped; prove changes here with `editLagIdentical` and before/after `dumpEvents`.

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
  `MS_LIVESET_OUT=<file>` (liveSetWrite's set).
- Other tests: `tools/soundlibraries/test_make_setups.py`, `test_extract_library_files.py`; Node in
  `tools/live/test/`. Test data: `test-scores/`, `share/verifyplayback/`, each mtest's folder.
- **Standard test score** (the owner, 2026-10-11): *Whence*, the quarter-tone original,
  `~/MuseScore/ms3fork/test-scores/Whence.mscz` on the dev VM, **never in the repo** (the owner's score). Use it for the
  playback audit, dumps (`dumpEvents`, with `MS_DUMP_LIVE` for the Live path), Create Live Set, VM runs and test
  instructions: its quarter tones exercise the tuning lanes, Kontakt Tune and bends. *Whence 12-TET*
  (`Whence 12-TET.mscz` / `.mscx`, same folder) only where a test needs no tuning lanes; numbers below marked
  "Whence 12-TET" were measured on it. Whence 12-TET was made 2026-10-06 by rounding each quarter tone down, as "Whence simple 2" played: the key's D / B quarter-flats → ♭, its
  G quarter-sharp dropped (key E♭ B♭ D♭ F♯), each E quarter-sharp → E♮; checked with the fork's dump (107 D / B
  notes a semitone lower, the other 347 and all times unchanged; 10 routes instead of 26: no tuning lanes).

## Tests and known state

- `tst_soundlibrary`: 164 passed, 6 skipped in-process (`MS_VST3_HOST_PROCESSES=0`; 2026-10-30, lowCut (string-lowcut; playbackPresets with `[levels] lowCut` 1 in Recommended, 0 in Library default); 2026-10-29, remoteKilledWrites (a write to a killed helper before its pipe's end is seen: EPIPE, no SIGPIPE); 2026-10-29, runBends; 2026-10-28, bowed-join; monoUnison; evenKeys), 154 / 6 with the default helpers on 2026-10-25 (vst3UnloadModules skips: MuseScore holds no module) (counting initTestCase and cleanup; 2026-10-28: transitionTails (bowed-join: `[levels] transitionLevels` 2, tail-levels.musicxml; transitionLevels / playbackPresets with 2 in Recommended); 2026-10-25: chunkedEqualsWhole (chunk-equal; chunkEndLevels' count of compared values >= 14: the chunks restate nothing the whole render doesn't); 2026-10-25: chunkEndLevels (chunk-end-levels; `chunk-end.musicxml`); 2026-10-25: remoteNull, remoteTiming, remoteParallelLoad, remoteKilled, remoteHung (oop-host; the last two skip on Windows); 2026-10-24: staccatoLength (staccato-length; playbackPresets with `[shorts] staccatoLength` 1 in Recommended, 0 in Library default); 2026-10-24: playbackVerifyLevelGroups (verify-detector2); 2026-10-23: playbackVerifyOffset, playbackVerifyShortLegato, playbackVerifyQuarterTones, playbackVerifyRegression (skipped without `MS_VERIFY_REGRESSION`: VERIFY.md › Detector regression; verify-detector); 2026-10-22/23: attackLevels 0 / 1 / 2 (attack-levels; playbackPresets with `[levels] attackLevels` 2 / 0); 2026-10-22: plainJoinLanes, transitionLevels (slur-joins; playbackPresets with `[levels] transitionLevels` 0); 2026-10-21: microtoneBends (fast-microtones; playbackPresets with `[slurs] microtoneBends` 0); 2026-10-21: ampleStrumLead (ample-pipeline; the other guitar tests at strumLead 0); 2026-10-21: ssoScriptFindings, avoidCCDropped, playbackPresets with `[notes] transportReset` / `[levels] hallGuard` (sso-script-fixes2); 2026-10-21: voiceLanesMemory (all-copies: the same routes at any memory figure, the warning raised when short, `MS_FREE_MEMORY_MB` its figure, an explicit maxLanes still a cap; voice-determinism: `SoundLib::memoryForCopies` counts a VM's installed memory not yet visible); 2026-10-21: plainTiming (plain-timing); 2026-10-21: plainAccents extended with accentedLevel, playbackPresets with `[slurs] plainAccents` 2 (accent-level); 2026-10-20: automationJumps, backgroundLoad (lane-steps); 2026-10-20: divisiLevel (divisi-level; playbackPresets with `[levels] divisi`); plainAccents (plain-accents; playbackPresets with `[slurs] plainAccents` 0); chordVoices (chord-voices); kontaktRandomSources, speedFromWritten with fixedTransitions' layers (sso-legato-fixes); 2026-10-20: ampleMasterVol extended (ample-fixes3: a strum's level x the volume lane under it); ampleMasterVol (ample-fixes2, third run: CC7 as M-Master Vol); ampleSoftStrums / guitarSettings extended for `softStrums` 2 (ample-fixes2, second run); ampleStateControllers, ampleStateLoads, ampleStateWritten, ampleSettingsWarning, pluginStateValues' keyed names, ampleMap's state counts (ample-fixes2, state controllers); ampleOpenMuted, ampleCapo's capo lead (ample-fixes2); ampleFxMute (ample-winfix); ampleStrums, ampleSoftStrums, ampleStrings, ampleCapo, ampleHarmonics, ampleSlides, ampleHammer, ampleFx, guitarSettings, ampleSampleSets (ample-phase2; ampleMap counts the extra patches, playbackPresets Library default's [guitar]); 2026-10-18: ampleMap, ampleRender, controllerDisplay, pluginStateValues, ampleParameters (ample-map); 2026-10-17: levelContextPlain (plain-levels); speedFromWritten (cc119-control); plainPerformance (plain-performance); plainPerformance on by default and in Recommended (same count: `Playback::setIniValuesForTest` leaves it 0 unless a test sets it, so the older tests keep their expectations); 2026-10-16: vst3UnloadModules, wavWriterHeader (crash-fixes); 2026-10-16 load-profile, unchanged; 2026-10-15: vst3SynthLinks (tuning-slots: hosted slots); gainStage (utility-gain: a patch at +29.6 leaves the others at 0 dB, the trim, the fallback, MIDI CC7); controllerPatchDefaults (lanes-b-readback), clickedNote (clicked-notes-library); controllerSettings replaced liveControllers, liveMidiControllers removed (automation-only-controllers); levelTechniqueBalance (calibrated 2, option B': heldOffset, the shipped map's D; B's D against sso_sound_dynamics.json), levelContext with calibrated 2 (the slur gains unchanged, the staccatos D softer; the heldOffset shift removed), playbackPresets with calibrated 2, keyShift, levelMicCompensation, playbackPresets with micCompensation; 2026-10-14: levelRealRange and playbackPresets with realRange 2, transitionsEarly, noLevelSpikes, startLead; 2026-10-13: evenAutoSlow (slow slurs skipped; evenAutoSession / evenAutoTestSynth now read even-auto.musicxml, legato-early at 240 bpm), evenAutoWindows (with the measured gaps, early stop, the order), evenAutoSession (the wait at opening: queued windows), evenAutoTestSynth (early stop), transportRestart, renderKitMaxVelocity, evenLevelsGlide; 2026-10-12: levelRealRange, smoothOnset; 2026-10-11: renderPreflightCalibration, renderPreflightJobCopy, dynamicsCalibrationUserFile, bendGlideSmoothed, bendGlideMeasured, levelSectionBalance, with the shipped map's M; 2026-10-10: levelContext, kontaktTune, tuningLanesLive, kontaktSlots, playStartEarly, slurPerformance; 2026-10-07: the removed settings' tests went, playbackPresets added; 2026-10-08: presets with `[levels] calibrated`; 2026-10-09: onsetLongerThanNote, dynamicsCalibrationPitches);
  the skips need inputs (`MS_ROUTES_SCORE`, `SSO_NKI` / `SSO_EMPTY`, `MS_EXTRACT_PLUGIN` + `MS_EXTRACT_OUT`: the
  owner's files; `SSO_KICKSTART_NKI` extends two Kontakt tests).
- `tst_playbackaudit`: 14 passed, 1 skipped (2026-10-30, plantedControls: ARRIVAL / LEVEL STEP on planted defects; VERIFY.md › Positive controls; 2026-10-29, plainAutoArrival (plain-auto.musicxml: smooth, fingered, bowed, re-attack each by its own model, plainAuto 1 and 2); 2026-10-28, hallFindings pinned to transitionLevels 0, plus a row with 2: only m4's fp; 2026-10-21, hallFindings, avoidedCCFindings (sso-script-fixes2: HALL, AVOIDED CC; `hall.musicxml`); 2026-10-20, chordFindings (chord-voices: CHORD); droppedFindings with `[slurs] fastTransitions`; 2026-10-14, transitionArrival, with transitionsEarly 1 / 2; 2026-10-13, droppedFindings, leapFindings; 2026-10-09, cappedArrival, swapByPitch; `auditScore` needs `MS_AUDIT_SCORE`: VERIFY.md ›
  Playback audit). **Current** (2026-10-29, bowed-join, today's Recommended with plainPerformance 1 and plainAuto 1, after the plain-note model fix: a plain transition by its glide's arrival, any other plain attack by plainAttack, smooth by attackOnset; VERIFY.md › Playback audit › Plain notes): Whence ARRIVAL 33 fail / 7 warn (Violas 17, Violins 1 9, Celli 4, Basses 3; the heard check on the real sound, build 491: 35 fail / 19 warn), LEVEL STEP 26 info, every other kind 0; plainAuto 0: 34 fail / 3 warn. Before the fix (b0de2e5f3e): 147 fail (every plain note modelled at plainShift, a new bow + bowedEarly; 100-250 ms off the heard transitions), plainAuto 0: 12, both off: 2. The counts below are older states.
  Whence (quarter tones), Recommended (2026-10-11, main cb073b6917): the same counts as Whence 12-TET
  below (OVERLAP 0, UNMEASURED 10, CAPPED 289, ARRIVAL 19 fail / 136 warn, LEVEL STEP 0).
  Whence, Recommended, integrate-1012 (2026-10-12: transitions, smoothStart, smooth onset): OVERLAP 0, UNMEASURED 9,
  CAPPED 289, ARRIVAL 20 fail / 10 warn, LEVEL STEP 0. Branch `gaps-1012` (2026-10-14: the start lead, transitions by
  their modelled arrival, `LibTrace::glideArriveMs`): OVERLAP 0, UNMEASURED 10, CAPPED 288, ARRIVAL 33 fail / 6 warn
  (15 a slur's first transition 100-208 ms after its on-time attack, 3 + 6 neighbouring transitions' delays differing,
  15 the on-time attack after a slur's late last transition: HANDOFF.md › The audit's transitions), LEVEL STEP 0.
  Whence, Recommended with `[slurs] transitionsEarly` 1 (its default since 2026-10-15, integrate-1012; the same on integrate-stage, 2026-10-15: timpani, context re-measure, automation-only controllers, clicked notes): OVERLAP 0,
  UNMEASURED 10, CAPPED 1 (the Celli's 2nd note, bar 1: its transition 94.9 ms early, meant 128.2, the first note keeps
  keepMs), ARRIVAL 0 fail / 2 warn (that note +33.3 ms), LEVEL STEP 0, DROPPED 0, LEAP 0; 288 transitions early, median
  127.9 ms (94.9 - 207.4).
  Whence 12-TET, Recommended (2026-10-10, `[slurs] performance`): OVERLAP 0, UNMEASURED 10 info,
  CAPPED 289 info, ARRIVAL 19 fail / 136 warn, LEVEL STEP 0 (with the re-attack keep, 2026-10-10): the audit judges the Performance legato's transitions (on
  the beat) by the attack onset, having no model of a transition's arrival (HANDOFF.md); with `slurs/performance=0` as on
  2026-10-09 (UNMEASURED 9, CAPPED 2, ARRIVAL 1 fail: the Celli's 2nd note, bar 1, LEVEL STEP 12 info).
  On Whence 12-TET, Recommended (2026-10-09): no OVERLAP (12 with cc1c9dcd2b's fix reverted), TIMING fails in the
  violas (bars 8-16), violins and celli, UNMEASURED for every Long (Rachm.) / Spiccato / Long outside the fit files.
- `tst_liveequivalence` (links mscoreapp; uses tst_soundlibrary's test synth): 88 passed with init and cleanup (2026-10-30), `dumpEvents`, `utilitySet`, `scriptValuesSet`, `chunkCompare` and `twoLibraries` (without `MS_VIAL_VST3`) skipped (2026-10-30, string-lowcut: lowCutHosted; 2026-10-28, bowed-join: transitionTailsLive; 2026-10-27, vial-levels: vialLevels (a Vial Long as loud as SSO's at every marking and measured pitch on the shipped maps and curves, within 0.5 dB; rendered, a flute on each in one score); 2026-10-26, vial-engine: twoLibraries (an SSO-like part on the test synth, slot 0, and a Vial part, slot 1: routes, renders, MIDI out / Live, an engine not found; with `MS_VIAL_VST3` + `MS_PRESET_FOLDER` the real Vial hosted and sounding, else skipped after the unit checks); 2026-10-25, chunk-equal: chunkCompare (a tool: MS_CHUNK_SCORE, docs/TOOLS.md › 1); 2026-10-25, oop-host: hostCrashRecovery (a killed helper noticed, its instance reloaded, sounding sample for sample as before; the third end not reloaded), asyncLoadHosted with helpers (the two setStates at once); 2026-10-24, async-load: asyncLoadHosted (loads in the background, the event loop free, the same bytes and sound as the blocking sync, an edit queued, release and a second score during a load); 2026-10-24, staccato-length: staccatoLengthLive; 2026-10-22, attack-levels: attackLevelsLive; 2026-10-22, live-celli: speedSentLive (speedFromWritten's CC119 sent to every voice copy's slot: a technique track's first value always a step, Live sending an envelope's value only where it changes); 2026-10-22, live-realcheck: retriggerOrder (a re-struck key's note-off RETRIGGER_MARGIN_MS before its note-on in the plain set, real Live), emptySlotKnob (Live's filled empty panel slots are no knobs); 2026-10-22, slur-joins: plainJoinsLive, transitionLevelsLive; 2026-10-21, sso-script-fixes2: transportResetState, sharedSettingsWarning; 2026-10-21, plain-timing: plainTimingLive; 2026-10-21, accent-level: plainAccentsLive's three "real level" rows; 2026-10-21, liveset-hosted-readback: hostedReadBack (the plain set's M-Master Vol read back as MuseScore's own, no lane of its own; unchanged updates write nothing and leave the score unmodified; Live's edits: as the volume lane when invertible, else kept until the score changes it; a set made again carries the lanes Live had marked); 2026-10-20, lane-steps: laneJumpsLive, backgroundLoadInstances; 2026-10-20, divisi-level: divisiLive; 2026-10-20, plain-accents: plainAccentsLive, four rows; 2026-10-20, chord-voices: chordVoicesLive, chordVoicesHosted; 2026-10-20, sso-legato-fixes: randomSetupState; 2026-10-20, ample-fixes3: liveStateSettings (the command-line renders' instances load each part's state), liveMasterVol (the plain set's automation under a strum: lane x level); ample-fixes2: liveMasterVol (third run); liveGuitarSoftRoll, liveStateSettings; 2026-10-18: verifyRoutesByPart (verify-routes: `--verify-playback --verify-wav` with `MS_VERIFY_ROUTES=1` on two library parts renders every route, one part's instances at a time; no route rendered: an error, run() fails); 2026-10-17: editLagIdentical (edit-lag: an edit sequence's gains, trim, volumes, Inspector answers, previews and chunked render with EditLag off and on, the warm-up cut off at 0-2 steps or run out; `MS_EDITLAG_SCORE` / `_SETTINGS` / `_TIMES`); speedFromWrittenLive, speedSetupState, scriptValuesSet (a tool: MS_SCRIPT_VALUES_IN / OUT / MS_SCRIPT_VALUES) (cc119-control); plainPerformanceLive (plain-performance: the Performance Kontakt track's Track Delay -plainShift, every note-on / note-off and CC1 as MuseScore plays them); 2026-10-16: liveEquivalenceByPart (liveeq-parts: one part's instances at a time, null against all at once, max difference 0); 2026-10-16: exportInstancesOfParts (crash-fixes); 2026-10-16: preparedStates (load-profile: the kept states are the same bytes, from memory, from the file, a damaged file made again); 2026-10-15: hostedSlotsGain (integrate-ts: three tuning copies on one instance's slots at +12 dB: each copy and all at once +12 in an export and at play, the linked slots' Mixer unused, the plain set's multi one Utility); hostedSlots (tuning-slots: a patch's tuning copies as one instance's slots: routes, SoundLibraryHost::sync, loadInstances, the multi state, links, the varispeed and parameter cases); liveGainStage (the Utility written, read back, updated in place and inserted), utilitySet (a tool: MS_UTILITY_IN / OUT / DB), `dumpEvents` writes gains.txt; updateLiveSetKnobs (option B constants, knobs read back, Update keeps them), controllerPatchDefault; liveMarcatoLevel's "technique balance" row, `dumpEvents` trace.txt with articulation and velocity; liveMicCompensation; 2026-10-14: liveRealRange by rows, Meyer rule and Clark & Luce spans; plainWritten's "performance transitions early" row; 2026-10-13: liveEvenLevels; 2026-10-12: liveRealRange, `dumpEvents` writes headroom.txt; plainWritten's "performance smooth" row; updateLiveSet, Update Live Set on a set edited as in Live; plainOwnTechnique; 2026-10-11: verifyExportMix, the verification renders at the patch volumes; liveTimingOverride, timing set in Live; plainWritten, three rows: written clips + Track Delay + the device's delays play every note-on / note-off of MuseScore's render; `dumpEvents` with `MS_DUMP_TIMING=<file>` writes each plain-set note's written and played times; plainTuned checks the glided bend envelope; 2026-10-10: liveContextLevel, plainTuned, livePerformanceLevel; 2026-10-08: delayedTrackLevel; 2026-10-07: liveEquivalenceLegatoLevel removed, plainSetLinked added; 2026-10-08: liveMarcatoLevel's
  calibrated row), `dumpEvents` skipped (a
  tool: `MS_DUMP_SCORE`, `MS_DUMP_MAP`, `MS_DUMP_OUT`, `MS_DUMP_SETTINGS`, `MS_DUMP_CHUNKED` renders as
  playback does: 10-measure chunks, `MS_DUMP_LIVE` as Live's clips (plus kontakts.txt, routes.txt and multis.txt: the
  plain set's Kontakt instances and each slot's Tune); every dump writes routemap.txt (route, lane, cents, bend range)
  and tunings.txt (each note-on's varispeed), with notes.txt's cents to check that no microtone plays in 12-TET). `tst_liveintegration` 70 with init and cleanup (2026-10-28, laneHeights; 2026-10-16, inspectorNoteRows (no two Note Inspector widgets share a grid cell); 2026-10-15, liveSetKnobs, laneDashedLine, lanePointsOnBarLines, noteLaneRubber, soundLibraryWindow, pictures with `MS_LANE_PNG=<folder>` / `MS_SOUND_LIBRARY_PNG=<file>`; 2026-10-12, laneFolds; 2026-10-11, clipsTimingFromLive; 2026-10-06, laneEvenBeats; 2026-10-05, clipEditDuplicateChunk, clipBandsEditing, MS_CLIP_TURNS_PNG=<file>; links mscoreapp; clipEditBands writes pictures with `MS_CLIPBANDS_PNG=<folder>`, clipTempoArrangement with `MS_CLIP_TEMPO_PNG=<file>`), `tst_keysig` 8, `tst_tuning` 18,
  `tst_phrasemark` 8, `tst_marcatolevel` 14 (2026-10-15: calibrated and velocityPath with calibrated 2, option B; `defaultUnchanged`, `controllerPath`: the level settings pinned off since 2026-10-15; `defaultUnchanged`: events regenerated 2026-10-15 for the start lead, 2026-10-06 no Performance patches; 2026-10-08:
  allArticulations, textLevel, calibrated), `tst_performancetechnique` 24 (2026-10-17: evenLevelsPlain (plain-levels; its ini keeps plainPerformance 0); 2026-10-15: noteLaneCopyPaste; 2026-10-13: evenLevels; 2026-10-12: slurTransitions, onPerformancePatch, ownTechnique, duplicateNoteValues, duplicateAutomation (Ctrl+D over a time selection); 2026-10-11: avoidBadSamples; 2026-10-10: Auto techniques, overrides, joins on
  rendered events, file round trip, undo; own velocity / join, the trace, the lanes' model `lanes`), `tst_tempochange` 4, `tst_playability` 20 passed (2026-10-04, microThreshold; `speed` skipped without
  `PLAYABILITY_BIG`), 2026-10-02. `tst_timesig` 14 passed (2026-10-04). Node tests in
  `tools/live/test` and the Python tests in `tools/soundlibraries` pass.
- `tst_midi`: 97 passed (2026-10-04). Its 3.x tests render with MuseScore 3.6's method (`ms3State`: the MS3 mode)
  against 3.6's references, changed only where the fork differs on purpose (bends for a 24-semitone wheel, the
  export's RPN says 24; a fermata without a stretch stretches by 2); `eventsMs4` renders the same scores as MS4
  against the fork's own `-ms4-ref.txt` (regenerate those only for an intended change of the note model).
- Untried with Kontakt / SSO unless docs say so; LIVE.md lists what only real Live can show.
- The owner's "SSO Articulation Map" review page (https://claude.ai/artifact/Y9dDEm5gpEjtpjjQmM9qBm, collection
  `reviews`): read with ArtifactData where available, else ask for the marks; fix through `gen_spitfire_sso.py`.
