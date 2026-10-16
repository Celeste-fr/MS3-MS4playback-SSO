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
  format (`livesetwriter.h`), which 12.4 opens; move it to 12.4's only for a reason. The test VM runs a Live 12.4.6
  trial (from 2026-10-02, 30 days), the owner's version.
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
`mscore/playbackmode.h` (MS3 / MS4 / library, all parts) and `libmscore/partplayback.h` (per part, metaTag
`partPlayback`). `tempochange.h` (rit./accel. lines, metaTag `tempoChanges`). `slur.h` › Phrase marks (non-legato
slurs, Alt+S, metaTag `phraseMarks`). `articulation.h` › MarcatoLevel (every articulation sign's and technique staff text's level in dB,
Inspector › *Level*, ±35.5; a chord's add up, `MidiRenderer::levelOf`; metaTag `marcatoLevels`; library: velocity by the
measured curve or CC11 / CC1 per note, `libraryNoteLevels`; built-in: SoundFont 2's law).

**Notation.** Custom key signatures follow each staff's clef (`KeySigEvent::forClef`, `Staff::keySigEventForClef`,
*Tools › Adapt Key Signatures to Clefs*, `mscore/keyedit.cpp`). Deleting a restating time signature keeps the
measures (`Score::cmdRemoveTimeSig`). Numpad 1-7 enter scale degrees (`Score::scaleDegreeStep`; on Linux/xcb
`ScoreView::event` takes the keypad keys, `numpadDegreeAction`; `Shortcut::load` gives new actions their defaults
in an old shortcuts.xml). Continuous View has a bar ruler (`mscore/barruler.*`: every bar's number along the top,
as the measure-number style counts, view only, View › Bar Ruler).

**Tuning** (`libmscore/tuning.h`): temperament (metaTag `temperament`, the Tuning plugin's JSON) + accidental rules
(microtonal, HEJI, Johnston, stacked accidentals as hidden `Symbol`s) + the note's own tuning. `tuningtables.h`
from `tools/tuning/gen_tuning_tables.py`; `mscore/tuningdialog.*`; plugin API `playbackTuning`,
`microtonalTuning`. The Microtonal Tuner plugin is frozen: update or drop `tst_tuning::pluginParity` when needed.

**Playability** (PLAYABILITY.md): `libmscore/playabilityrules.*`, `playability.*`, `playabilitydiagram.*`,
`mscore/playabilitypanel.*`.

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
  listed shorts take velocity. **Adjustments: Recommended plays the measured ones the owner approved, Library default
  none** (the owner, 2026-10-06, first wanted none, timing and levels set in Live; from 2026-10-08 approved measured
  ones one at a time, by ear: below; 2026-10-12: this rule replaces "No automatic adjustments"). Every adjustment is a
  setting, off in Library default, on in Recommended only once the owner approved it. Legato transitions play on their
  beat, pedal changes one tick after their chord (2026-10-07: "remove all settings not currently being used"); the settings and code that could change that (legato early, fastFirsts, phrase
  gap, overlap, fast technique, level balance, pedal timing, the old calibrated short velocity, even steps, one
  instance, map track delays) were removed 2026-10-07 (docs/PLAYBACK_SETTINGS.md lists them; old ini keys and metaTags
  are ignored). The technique still comes from the notation. **Calibrated levels** (the owner, 2026-10-08, shorts too
  quiet; `[levels] calibrated`, on in Recommended, off in Library default): a technique on velocity plays as loud as
  the part's Long at the same dynamic (measured curves, by ear: perceived loudness where measured), plus MS4.7.5's offset for its articulations (marcato's too),
  plus the Inspector's levels (`libVelocity`, `SoundLib::calibratedVelocity`; docs/PLAYBACK_SETTINGS.md › Calibrated
  levels); techniques on CC1 keep the library's balance, except Performance legato notes (since 2026-10-10: by CC1, as
  loud as Long, `SoundLib::calibratedController`, `performanceLevel` in rendermidi.cpp; with **`[levels] context`** (the level rule, the owner 2026-10-10: the same written dynamic equally loud in every string part and technique, in the music; 1 in Recommended and by default, 0 in Library default) CC1 at the written dynamic instead and one gain per slur from the map's `<Articulation contextLevel>` (by dynamic, median IOI and pitch; `level_rule.py` from `sso_performance_context.json`), by CC11 and patch volumes: the score's largest gain G puts the Performance patches at G - S and every other patch of the library's parts at -S, S = max(0, G - 6) (`libmscore/contextlevel.h`, `TrackDelays::patchGain`; docs/PLAYBACK_SETTINGS.md › Level rule); with **`[levels] sectionBalance`** (**2 in Recommended and by default since 2026-10-14**, 0 in Library default) each section's patches also play a map level O louder: **2, the Clark & Luce balance** (the owner, 2026-10-14, levels from real data): `<Instrument sectionLevel2>`, the same method with the real side from Clark & Luce 1965's orchestral scales at mp as sound power (+31 - TU Berlin's DI in that direction, each marking its own since 2026-10-15: `tu_directivity.json`), `section_balance_cl.py` + `clark_luce_levels.json` + `clark_luce_table2.json` (Table II: the horn from its player 1, Fig. 2's horn being player 2's alone; each player counted once) + `sso_sound_dynamics.json` (one-pitch SSO side for the 15 patches without a full sweep: solo strings, Motif brass, piccolo, alto / bass flute, contrabass clarinet / trombone / tuba, cimbassi; ±2.3 dB) → `sso_cl_section_balance.json` → `SECTION_LEVEL2` (Violins 1 -0.13, Violins 2 +1.02, Violas -7.03, Celli +2.69, Basses +3.45, winds -15.52 (Contrabassoon) … +19.67 (Trumpets a6); docs/BALANCE_LOG.md: one entry per refinement; contrabassoon / bass trombones their sibling's level + TU Berlin's ratio between them at mp (since 2026-10-15, one source's ratio; the bass trombones' corroborated by Iowa MIS and ISO 23591), the bass clarinet its sibling's + the median of four independent sources' ratio (-0.35 dB, ±2: ISO 23591, O'Brien 2013, TU ff, Iowa MIS; Iteration 13; sectionLevel2 -4.85, was 0.38), the gap patches without their own instrument a family member's level (an assumption), Strings Ensemble from the sections it mixes at each key (+11.93, keys' SD 2.0; Iteration 9, `strings_ensemble.py`), the harp TU Berlin's sound power moved to this scale (-9.81, about ±4 dB; the percussion within Skålevik's range: no change; docs/BALANCE_LOG.md › Iterations 5-7), the timpani from the Spheres dataset's Romeo and Juliet at its written p / f on the strings' mf scale (-5.09, ±3 dB, realSteps 39.2: `spheres_timpani.py`; Iteration 10), the Grand Piano from Meyer 2009 by the median Meyer -> Clark & Luce offset (-1.11, ±3 dB; Iteration 11), celeste and mallets SSO's own (no source; Iowa's extremes not markings, Iteration 12); corroborated by the Spheres sections and Skålevik 2019; docs/PLAYBACK_SETTINGS.md › Section balance; `LibInstrument::sectionLevelFor`, a patch without it plays sectionLevel); **1, the TU Berlin balance** (the owner 2026-10-11: M; Recommended until 2026-10-14): `<Instrument sectionLevel>` (`gen_spitfire_sso.py` `SECTION_FADERS`: M, a real orchestra's balance at CC1 64 from Weinzierl et al. 2018's sound power and SSO's section sizes, less SSO's measured `sso_cc1_curves.json`, centred, mp where realRange's Meyer rule places it (the owner, 2026-10-14, "sure": one rule for everything): Violins 1 -2.60, Violins 2 -1.45, Violas -3.08, Celli +1.89, Basses +5.24 dB (until 2026-10-14, mp at 0.4 of pp -> ff, the owner's ABX-backed choice of 2026-10-11: -3.81 / -2.66 / -2.12 / +2.77 / +5.81); **since 2026-10-14 the 23 woodwind and brass patches too** (the owner: from the solo data "until we can find better non-solo data"): the same method on the strings' scale, -3.14 (Contrabassoon) … +20.07 dB (Trombones a6); all computed by `section_balance_winds.py` → `sso_winds_section_balance.json` → `SECTION_FADERS` / `SECTION_FADERS_WINDS`; docs/PLAYBACK_SETTINGS.md › Section balance), a phrase at gain + O, all lower by S' = max(0, loudest gain + O - 6) (docs/PLAYBACK_SETTINGS.md › Section balance); with **`[levels] realRange`** (stage A of the dynamics plan, the owner 2026-10-12; **2 in Recommended and by default, 0 in Library default**: the owner, 2026-10-13, heard on the ladder; 2026-10-14, 2) each note of the five string sections also plays realDb = (player's change from mp at the written CC1) - SSO Long's change from CC1 64 by CC11, following CC1 through hairpins; **Clark & Luce spans** (`realRange` 2; the owner, 2026-10-14: "let's use the section data wherever it's reliable", one source for every instrument): the player's change is steps(written CC1) x S / 5, S the instrument's written pp -> ff as orchestral players play it (map `<Instrument realSteps="S">`: Clark & Luce 1965, *J. Audio Eng. Soc.* 13(2), Table I, printed; `real_range.py` `SPANS`; the Spheres dataset's whole sections agree within ~2 dB where they recorded 3+ dynamics, a cross-check only, not in the map; bass clarinet / contrabassoon / bass trombones their family's, an assumption; docs/PLAYBACK_SETTINGS.md › Real range), ppp / fff one step more, a patch without realSteps the Meyer rule; **the Meyer rule** (`realRange` 1; the owner, 2026-10-14, replacing rule D: "this is clearly better"; Recommended until Clark & Luce spans the same day): (F(steps) - F(mp)) x span: TU Berlin's single-note pp / ff are ppp / fff (F 0 / 1), written pp / ff the levels of normal playing at the instrument's fractions a / b of the span (map `realPlay="a b"`: Meyer 2009 ch. 3, rapid scales against single-note extremes, `real_range.py` MEYER), p .. f in equal steps between; without `realPlay` rule D (7 equal steps, a = 1/7, b = 6/7): Cor Anglais, Contrabassoon, which Meyer gives no extremes for; Bass Clarinet and Bass Trombone(s) take their sibling's (clarinet's, tenor trombone's) fractions, an assumption ("plays like its family", the owner 2026-10-14; `real_range.py` SIBLING) (map `<Instrument realSpan longLevel realPlay>`: Weinzierl et al. 2018's ff - pp per pitch, smoothed, and `sso_cc1_curves.json`, by `real_range.py`, `--rule-d` for the comparison; a capped short's rest by CC11; since 2026-10-14 also the 23 main wind and brass patches the database has, solo / a2 / a6, with their sectionLevel, and since 2026-10-15 the 15 gap patches (`sso_gap_cc1_curves.json`; the eight without a database instrument realSteps only, their family's, an assumption; checked in Kontakt: every marking within 0.05 dB RMS of steps x S / 5, docs/BALANCE_LOG.md › Iteration 4): `sso_winds_cc1_curves.json`, `real_range.py` WINDS: realRange compresses the flutes, oboes and cor anglais (their real span is smaller than SSO's) and stretches the clarinets and trombones), its part's largest R (found by rendering the score once, `ContextLevel::realRangeTops`) as the patches' volume within the same S' (`libmscore/contextlevel.h` › Real range; docs/PLAYBACK_SETTINGS.md › Real range)). `[slurs] quick` (2 in Recommended, the default; 0 in Library default; in Recommended it acts only under staff text
"non performance", `[slurs] performance` taking the strings' slurs: Whence dump 2026-10-12, no Long (Rachm.)) plays short slurred notes on a quicker technique (Long (Rachm.)), early by its own onset, by `[heldNotes] byPitch` as every note (since 2026-10-09; `Choice::swapped`, `sso_rachm_onset_fit.json`) and, with `[slurs] quickLevel` (1 in Recommended, the default), as loud as the held one (map `quickLevel`: perceived, `sso_rachm_levels.json`; `quickDb`). Violins 1 / 2 only: Violas' and Celli's in-context entries in both files are measured but not used (`RACHM_UNUSED` in the generator; the owner, 2026-10-09: Whence bars 1-2 Celli ~8 dB louder and pulsing), their Rachm. as at 25a3dcbbeb. Exception: notes start
  early by their articulation's median measured onset (held notes, and since 2026-10-08 every measured technique, shorts too) (`[heldNotes] byPitch` 1: each by its pitch's)
  (`<Onset early>` 100 since 2026-10-07; the owner: plain Long for everything, lined up within Rasch's 30-50 ms between
  players; `[heldNotes] early` 0 plays them as written). **Performance patches: the strings' slurs in Recommended**
  (the owner, 2026-10-10, reversing 2026-10-06's "No Performance patches" for Recommended only): `[slurs] performance`
  (1 in Recommended, the default; 0 in Library default) plays slurred notes of the five string sections as under staff
  text "performance" (`Want::slurPerformance`, `choosePerformance`: a plain choice and a legato with `attacks`; staff
  text "non performance" opts out, `TextState::performanceOff`), instead of `[slurs] quick`'s swap; a staccato before a
  slur stays on All techniques, so the slur starts from silence. With **`[slurs] transitions`** (the owner, 2026-10-12; 1 in Recommended and by default, 0 in Library default) Auto plays a slurred note's transition fingered and a slur starting where the slur before ends (touching, no accent) as a bowed transition from it instead of a re-attack (`performanceNote`, `slurAt`; docs/PLAYBACK_SETTINGS.md); with **`[slurs] smoothStart`** (same values) a note after silence on its patch starts with the smooth attack, not spiccato, early by the smooth attack's own measured onset (map `attackOnset`, `sso_smooth_onset.json`, `Articulation::startOnset`; 2026-10-12); **`[slurs] transitionsEarly`** (the owner, 2026-10-14, to A/B by ear; **1 by default and in Recommended since 2026-10-15**, 0 in Library default: by ear on Whence's fast quarter tones, 1 measured best; "slow slurs are skipped" judges written intervals, `NoteInfo::written`) sends a Performance transition early: 1 by its modelled arrival (the audit's model), 2 by the measured leave + 23.5 ms (heard on the beat, Polfreman 2013), 3 the glide's middle; capped so the note before keeps keepMs / 15 ms (`TRANSITION_MIN_GAP_MS`), the join and bend following the moved note-on (docs/PLAYBACK_SETTINGS.md › Transitions early). Off (Library default), SSO slurs play the All techniques
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
  band ppp … fff); metaTag `performanceTechniques`, which also holds each note's **own technique** (Inspector › Note › Technique, the
  map's sounding articulations of its part's patches, `"<patch>|<articulation>"`, `Note::libraryTechnique`: replaces
  the notation's choice in `SoundLib::chooseNote`, so routes, lanes, Live's clips and plain set follow); map `attacks` `transitions` `reattack` `reattackGap` `overlap` `avoid` (a faulty
  round robin's velocities, steered to a clean one: `sso_bad_samples.json`, `[notes] avoidBadSamples`) `avoid` (a faulty round robin's velocities: `sso_bad_samples.json`, `[notes] avoidBadSamples`)
  `legatoGap`): its velocity,
  and the note before ending `overlap` ms after it or `reattackGap` ms before it (`performanceNote`, `libJoins` in
  `finishLibraryEvents`; not across 10-measure chunks or tuning lanes). **Held notes start early** by their
  measured onset (`<Articulation onset>` by pitch; metaTag `soundLibraryOnsetEarly`); the note before on its patch keeps
  `keepMs` of its length as played. A transition's measured delay (`<Articulation legatoDelay>` by interval, shortened
  after a short note: `libFastDelay`) times only a bent transition's glide. HANDOFF.md has the open timing items.
  Per-note levels (a marcato's, track levels) share one CC11 / CC1 schedule per route, `libraryNoteLevels`
  (`libLevels`; dB add up).
- **Even levels** (`libmscore/evenlevels.h`, `mscore/evenlevelsjob.h`; the owner, 2026-10-12, the blind test's Glide;
  **`[levels] evenLevels` 1 in Recommended and by default, 0 in Library default** (the owner, 2026-10-13)): SSO's Performance
  legato swells inside slurs by context (no table predicts it), so *Tools › Even Out Levels…* / `--even-levels <score>`
  renders the slurred passages through the hosted plug-ins (3 passes), measures the perceived envelope and fits a per-note
  CC11 glide flattening it to each slur's median (linear in dB between nodes halfway from each note-on to the next,
  `EvenLevels::nodeTime`, every 5 ms; the owner, 2026-10-14: S, `even_180_check.py`, before the nodes sat in the next
  note), / 1.28, then once more, the round whose envelope lies closest to its slurs' medians kept; each
  note keeps its value and a hash of its rendered context (`Note::evenLevel` / `evenContext`, metaTag `evenLevels`): a
  changed context is stale (Inspector › Note › Even level), not played. `MidiRenderer::libraryEvenLevels` glides on top of
  the CC11 schedule (the patch's notes lower by its largest value, never cut at 127; its volume adds what fits under +6 dB,
  the rest lowers every library patch alike: `TrackDelays::evenDb` / `evenAll`); Live's clips and the plain set carry it.
  **Slow slurs are skipped** (the owner, 2026-10-13, blind test: slow slurs sound the same uncorrected and corrected; HANDOFF.md
  > Slow slurs need no correction): a passage whose every interval between joined notes is >= `EvenLevels::SLOW_IOI_S`
  (350 ms since 2026-10-14, was 500: the owner heard 350 / 500 ms the same corrected or not; `slowPassage`) is neither
  measured by the job nor in the background: no render, no value, no wait.
  **Measured in the background near edits** (`libmscore/evenauto.h` the Session, `audio/vst3/evenmeasure.h` the shared
  render + measurement, `mscore/evenautomeasurer.h` the threads, triggers and Play's wait; the owner, 2026-10-13: a note
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
  don't play: SSO's script overrides them). By the patches' volumes as the section level (`Volumes::micTop`, within S'),
  automated mics each note's own C at its start by CC11 (`trackLevel`); Live's sets and clips carry both. Measured on
  the VM (`tools/soundlibraries/mic_levels.py` -> `sso_mic_levels.json` -> `gen_spitfire_sso.py`): every patch with a
  section level (44), the strings' Performance patches, the harp, the timpani (not its Close mic: empty offline) and the Grand Piano;
  other extras take their main's (an assumption); power-sum
  error <= 1.4 dB, Mic Mix Distance's fit 2.05 dB leave-one-out (docs/PLAYBACK_SETTINGS.md › Mic compensation).
- **Playback adjustments are configurable** (`libmscore/playbacksettings.*`; **docs/PLAYBACK_SETTINGS.md** is the
  inventory): built-in defaults → `<dataPath>/playback.ini` (written with every key when missing; Edit › Reload
  Playback Settings) → the score (metaTag `playbackSettings`, plus the older onset-early / lanes metaTags; removed
  keys are listed in `REMOVED` and ignored silently). Read a
  setting with `Playback::value(id, score, mapValue)`; a new adjustment gets a `DEFINITIONS` entry, a row in the
  inventory and a layers test. UI: Mixer › Advanced Options… › Playback adjustments (`mscore/playbacksettingswidget.*`). **Presets** (Recommended: every measured technique early, each articulation by its median onset, `heldNotes/byPitch` 0 (1: by pitch), calibrated levels, `slurs/quick` 2, `slurs/quickLevel` 1 / Library default: `Playback::presets()`, a text edit of playback.ini, the widget's Preset box; docs/PLAYBACK_SETTINGS.md › Presets).
- **Play from a tick** (`libmscore/playstart.h`, `Seq::setPos`): notes that start early (onset, negative track delay) have their note-on, switch and controllers before their beat, so the transport starts at the earliest note-on of a note written at or after the tick (`PlayStart::start()`, a pre-roll) and skips the previous notes' note-ons/offs there (`skips`); the cursor stays at the tick (the owner, 2026-10-10: the first note of a selected measure sometimes silent).
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
  automation. Unautomated: the map's default, else the patch's own (`PartControllers::unautomatedValue`: mic defaults
  from the map, else the setup's value recorded at its last load, `parameter values.json` in the setups folder), drawn
  as Live's **dashed line**. The old metaTag `partControllers` becomes flat lanes at load (`PartControllers::migrate`,
  read302.cpp) and is never written. Automation: `libmscore/automation.*`, metaTag
  `automation` (points step / linear, a ramp may carry Live's Bézier control points). Every lane is editable;
  `Lane::playedByLive` (liveHash / pointsHash in its extra) says whether Live's set plays it; an import merges
  (`Automation::merge`, newer edit wins, both changed: a dialog per lane). **Editor**: `mscore/automationlanes.*`
  (Continuous View, a header row under every library part, each folded / open at will, Alt-click all: `mscore/lanefolds.h`,
  per score, the session's, never saved; Live's gestures; header lists them), model
  `Automation::Edit`, one undo step per gesture (`undoWrite`; heard at once while playing: `Seq::renderAgainPlaying`);
  an empty lane draws the dashed line (`AutomationLanes::paintEnvelope`), a drag on it sets the value from the start; points
  on a bar's tick sit on the bar line (`Automation::pointX`; stems stay at `xAtTick`); the Velocity / Join lanes draw their
  rubber band and Ctrl+D copies the selected stems' values to the notes right after (`NoteLanes::duplicateSelected`); a Dynamics lane replaces the notation's CC1 from its
  first point. **Time selection** (Live's): the score's range selection on a part with a header (whole bars,
  a light band over its rows); Ctrl+D (score or lane focus; MS3 has no Ctrl+D) copies every lane of the part and each
  note's attack / transition / velocity / join to the bars after, one undo step, then selects them
  (`Automation::duplicateTimeSelection`, `Edit::duplicateSpan`, `PerformanceTechnique::duplicateNoteValues`).
- Percussion: `<Instrument keyShift="12">` on the Timpani (its keys sound an octave under their number: a written D2 goes on key 50; `KEY_SHIFT` in the generator, `libNote.key` in rendermidi.cpp), `<Instrument maxVelocity="126">` on the 13 metal patches whose Kickstart option "Roll on high vel." is ticked
  (127 would play a roll; `ROLL_ON_HIGH_VELOCITY` in the generator, `MidiRenderer::libraryMaxVelocity`; a correctness fix, not
  an adjustment: docs/PLAYBACK_SETTINGS.md). `<Drum key name>` lists every hit of SSO's kits, one-drum patches and ensembles
  (`tools/soundlibraries/sso_percussion_hits.json`, from the owner's window pictures); a technique off at the
  defaults is `<Drum name default="off"/>` (no key, never played). The nine `"<patch> (all on)"` `<Patch>`es set
  Kickstart's arrays whole (`%c2lsa`, `%4jwcn`, `%x4jsr`, `$nd5ia=0`); `KontaktSetup::unpurgeSwitchedOn` loads the
  sample groups they switch on (Kickstart's purge rule; test `kontaktKickstartUnpurge`). Script values may change
  length (`kontaktScriptValueLengths`). Mics on 3-mic patches are Close, Tree, Ambient.
- Microtones: Kontakt ignores VST 3 note tuning. **Tuning lanes** (patch copies per tuning, metaTag
  `soundLibraryLanes`) play by **varispeed** (`Vst3Plugin::setPitch`) or by **pitch bend** where the patch bends
  cleanly (`<Instrument bend>`; varispeed can't reach Live). A legato transition's bend glides with the
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
  wait in `_pending`), `mscore/vst3editor.*`, `mscore/soundlibraryhost.*` (`sync`, preload at score open,
  `SoundLibraryExport`, *View › Sound Library…*). Plug-in modules stay loaded until exit.
- **Setups are made by MuseScore** from each patch's `.nki` (`audio/vst3/kontaktsetup.*`), then replaced by
  Kontakt's own state after the first load (`resave`). Setups folder `<dataPath>/soundlibraries/<library>/`:
  `<patch>.vst3state`, `Kontakt empty.vst3state`, `made setups.json` (`MAKER_VERSION`), `checks.json` (raise
  `CHECK_VERSION` in soundlibrarycheck.cpp when old results go stale), `dynamics.json`, `load times.log` (qDebug
  doesn't show on Windows).
- Background jobs (own setups copy, lock, log; their `.bat` files in `bin` only with `-DMSCORE_INSTALL_MEASUREMENT_SCRIPTS=ON`, off since 2026-10-02): `--check-dynamics`, `--scan-keys`,
  `--measure-load-times`, `--verify-playback`, `--live-equivalence`, `--create-live-set`, `--update-live-set`,
  `--live-set-readback`.
  Each logs a **render preflight** (`libmscore/renderpreflight.h`: calibration files' path + SHA-256 and each
  patch's source, setups folder / copy, playback.ini + preset; in the verify report, VERIFY.md › Preflight); the
  copy is synced at start (`syncJobCopy`: verify mirrors the main calibration, measuring jobs keep their own and warn).
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

**Playback verification** (VERIFY.md; checks in `audio/vst3/playbackverify.h`): `--verify-playback`,
`--verify-audio`, `tools/playbackverify/read_verify_report.py`, faults via `MS_VERIFY_FAULT`.
**Playback audit** (VERIFY.md › Playback audit; `libmscore/playbackaudit.*`, `tst_playbackaudit`): the rendered library
events of a whole score checked against the map's own onsets (not the real sound) for overlaps on a route, unmeasured
swapped / early techniques or no onset, capped early starts (the cut), predicted arrival against neighbours (sent +
onset - written; Rasch 1979: > 50 ms fails) and level steps under a slur (values; proposed threshold 0.69 dB, Jesteadt,
Wier & Green 1977, owner to approve), DROPPED (fail: a legato transition sent < 15 ms after the legato note-on before
on its route at velocity <= 64: SSO doesn't play it) and LEAP (warn: a transition beyond an octave / 1200 cents: SSO attacks afresh). The renderer
reports each library note's choice through `MidiRenderer::setLibraryTrace`. **Run it on Whence (quarter tones,
`Whence.mscz`; Recommended) before any build goes to the owner.**

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
  `CCACHE_RECACHE=1`. A `build.rel` copied from another
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
- **Standard test score** (the owner, 2026-10-11): *Whence*, the quarter-tone original,
  `~/MuseScore/ms3fork/test-scores/Whence.mscz` on the dev VM, **never in the repo** (the owner's score). Use it for the
  playback audit, dumps (`dumpEvents`, with `MS_DUMP_LIVE` for the Live path), Create Live Set, VM runs and test
  instructions: its quarter tones exercise the tuning lanes, Kontakt Tune and bends. *Whence 12-TET*
  (`Whence 12-TET.mscz` / `.mscx`, same folder) only where a test needs no tuning lanes; numbers below marked
  "Whence 12-TET" were measured on it. Whence 12-TET was made 2026-10-06 by rounding each quarter tone down, as "Whence simple 2" played: the key's D / B quarter-flats → ♭, its
  G quarter-sharp dropped (key E♭ B♭ D♭ F♯), each E quarter-sharp → E♮; checked with the fork's dump (107 D / B
  notes a semitone lower, the other 347 and all times unchanged; 10 routes instead of 26: no tuning lanes).

## Tests and known state

- `tst_soundlibrary`: 95 passed, 3 skipped (counting initTestCase and cleanup; 2026-10-15: controllerSettings replaced liveControllers, liveMidiControllers removed (automation-only-controllers); keyShift, levelMicCompensation, playbackPresets with micCompensation; 2026-10-14: levelRealRange and playbackPresets with realRange 2, transitionsEarly, noLevelSpikes, startLead; 2026-10-13: evenAutoSlow (slow slurs skipped; evenAutoSession / evenAutoTestSynth now read even-auto.musicxml, legato-early at 240 bpm), evenAutoWindows (with the measured gaps, early stop, the order), evenAutoSession (the wait at opening: queued windows), evenAutoTestSynth (early stop), transportRestart, renderKitMaxVelocity, evenLevelsGlide; 2026-10-12: levelRealRange, smoothOnset; 2026-10-11: renderPreflightCalibration, renderPreflightJobCopy, dynamicsCalibrationUserFile, bendGlideSmoothed, bendGlideMeasured, levelSectionBalance, with the shipped map's M; 2026-10-10: levelContext, kontaktTune, tuningLanesLive, kontaktSlots, playStartEarly, slurPerformance; 2026-10-07: the removed settings' tests went, playbackPresets added; 2026-10-08: presets with `[levels] calibrated`; 2026-10-09: onsetLongerThanNote, dynamicsCalibrationPitches);
  the skips need inputs (`MS_ROUTES_SCORE`, `SSO_NKI` / `SSO_EMPTY`, `MS_EXTRACT_PLUGIN` + `MS_EXTRACT_OUT`: the
  owner's files; `SSO_KICKSTART_NKI` extends two Kontakt tests).
- `tst_playbackaudit`: 9 passed, 1 skipped (2026-10-14, transitionArrival, with transitionsEarly 1 / 2; 2026-10-13, droppedFindings, leapFindings; 2026-10-09, cappedArrival, swapByPitch; `auditScore` needs `MS_AUDIT_SCORE`: VERIFY.md ›
  Playback audit). Whence (quarter tones), Recommended (2026-10-11, main cb073b6917): the same counts as Whence 12-TET
  below (OVERLAP 0, UNMEASURED 10, CAPPED 289, ARRIVAL 19 fail / 136 warn, LEVEL STEP 0).
  Whence, Recommended, integrate-1012 (2026-10-12: transitions, smoothStart, smooth onset): OVERLAP 0, UNMEASURED 9,
  CAPPED 289, ARRIVAL 20 fail / 10 warn, LEVEL STEP 0. Branch `gaps-1012` (2026-10-14: the start lead, transitions by
  their modelled arrival, `LibTrace::glideArriveMs`): OVERLAP 0, UNMEASURED 10, CAPPED 288, ARRIVAL 33 fail / 6 warn
  (15 a slur's first transition 100-208 ms after its on-time attack, 3 + 6 neighbouring transitions' delays differing,
  15 the on-time attack after a slur's late last transition: HANDOFF.md › The audit's transitions), LEVEL STEP 0.
  Whence, Recommended with `[slurs] transitionsEarly` 1 (its default since 2026-10-15, integrate-1012): OVERLAP 0,
  UNMEASURED 10, CAPPED 1 (the Celli's 2nd note, bar 1: its transition 94.9 ms early, meant 128.2, the first note keeps
  keepMs), ARRIVAL 0 fail / 2 warn (that note +33.3 ms), LEVEL STEP 0, DROPPED 0, LEAP 0; 288 transitions early, median
  127.9 ms (94.9 - 207.4).
  Whence 12-TET, Recommended (2026-10-10, `[slurs] performance`): OVERLAP 0, UNMEASURED 10 info,
  CAPPED 289 info, ARRIVAL 19 fail / 136 warn, LEVEL STEP 0 (with the re-attack keep, 2026-10-10): the audit judges the Performance legato's transitions (on
  the beat) by the attack onset, having no model of a transition's arrival (HANDOFF.md); with `slurs/performance=0` as on
  2026-10-09 (UNMEASURED 9, CAPPED 2, ARRIVAL 1 fail: the Celli's 2nd note, bar 1, LEVEL STEP 12 info).
  On Whence 12-TET, Recommended (2026-10-09): no OVERLAP (12 with cc1c9dcd2b's fix reverted), TIMING fails in the
  violas (bars 8-16), violins and celli, UNMEASURED for every Long (Rachm.) / Spiccato / Long outside the fit files.
- `tst_liveequivalence` (links mscoreapp; uses tst_soundlibrary's test synth): 38 passed with init and cleanup (2026-10-15: liveMicCompensation; 2026-10-14: liveRealRange by rows, Meyer rule and Clark & Luce spans; plainWritten's "performance transitions early" row; 2026-10-13: liveEvenLevels; 2026-10-12: liveRealRange, `dumpEvents` writes headroom.txt; plainWritten's "performance smooth" row; updateLiveSet, Update Live Set on a set edited as in Live; plainOwnTechnique; 2026-10-11: verifyExportMix, the verification renders at the patch volumes; liveTimingOverride, timing set in Live; plainWritten, three rows: written clips + Track Delay + the device's delays play every note-on / note-off of MuseScore's render; `dumpEvents` with `MS_DUMP_TIMING=<file>` writes each plain-set note's written and played times; plainTuned checks the glided bend envelope; 2026-10-10: liveContextLevel, plainTuned, livePerformanceLevel; 2026-10-08: delayedTrackLevel; 2026-10-07: liveEquivalenceLegatoLevel removed, plainSetLinked added; 2026-10-08: liveMarcatoLevel's
  calibrated row), `dumpEvents` skipped (a
  tool: `MS_DUMP_SCORE`, `MS_DUMP_MAP`, `MS_DUMP_OUT`, `MS_DUMP_SETTINGS`, `MS_DUMP_CHUNKED` renders as
  playback does: 10-measure chunks, `MS_DUMP_LIVE` as Live's clips (plus kontakts.txt, routes.txt and multis.txt: the
  plain set's Kontakt instances and each slot's Tune); every dump writes routemap.txt (route, lane, cents, bend range)
  and tunings.txt (each note-on's varispeed), with notes.txt's cents to check that no microtone plays in 12-TET). `tst_liveintegration` 67 with init and cleanup (2026-10-15, laneDashedLine, lanePointsOnBarLines, noteLaneRubber, soundLibraryWindow, pictures with `MS_LANE_PNG=<folder>` / `MS_SOUND_LIBRARY_PNG=<file>`; 2026-10-12, laneFolds; 2026-10-11, clipsTimingFromLive; 2026-10-06, laneEvenBeats; 2026-10-05, clipEditDuplicateChunk, clipBandsEditing, MS_CLIP_TURNS_PNG=<file>; links mscoreapp; clipEditBands writes pictures with `MS_CLIPBANDS_PNG=<folder>`, clipTempoArrangement with `MS_CLIP_TEMPO_PNG=<file>`), `tst_keysig` 8, `tst_tuning` 18,
  `tst_phrasemark` 8, `tst_marcatolevel` 14 (`defaultUnchanged`, `controllerPath`: the level settings pinned off since 2026-10-15; `defaultUnchanged`: events regenerated 2026-10-15 for the start lead, 2026-10-06 no Performance patches; 2026-10-08:
  allArticulations, textLevel, calibrated), `tst_performancetechnique` 22 (2026-10-13: evenLevels; 2026-10-12: slurTransitions, onPerformancePatch, ownTechnique, duplicateNoteValues, duplicateAutomation (Ctrl+D over a time selection); 2026-10-11: avoidBadSamples; 2026-10-10: Auto techniques, overrides, joins on
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
