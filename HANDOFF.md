# Handoff: start here

Current state of the work and what is open. Read `CLAUDE.md` first (rules, architecture map, build and test).
The dated work logs that used to fill this file (2026-09-25 … 2026-10-01: tuning, extracts, setups, dynamics,
Mixer, load times, Live integration, piano fixes, playback verification) are in `docs/HISTORY.md` Part 2; read
a section there when you need the background of that topic. Commit messages describe each step in detail.

## Paused 2026-10-25 (the owner: "pause project")

State: integrate-1017 has everything through chunk-equal (live playback's chunks = the whole render); last build for the
owner is #484 (https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/40230637035: helper processes, crackle fix,
bar-17 fix; chunk-equal is NOT in it). Waiting on the owner's report of #484 (loading, crackles, bar 17, Kontakt editor,
closing during a load). In progress when paused: branch `vial-map` (WIP d0ac45e4fe, not merged: `Vial Instrumental.xml` and `(Fuse).xml` by
`gen_vial.py`, presets loaded from Vial's data folder, Preferences › I/O › Library; real Linux Vial loads and plays them;
left: re-run vialHosted on all 13 when the CC11 / CC21 pack ships, the final CC21 list, Windows, Live / MIDI out; 13 Long
presets, CC1 / CC11 / CC21 on channel 1; Linux Vial bundles in ~/MuseScore/ms3fork/private/vial-vst3; the presets answer CC11 only
after vial sso's preset update, not shipped: paused too, its state in ~/vial-sso/HANDOFF.md; only CC1 works now). Owner limits: Max nearly out, then Pro (1/20 the usage): one subagent at a time, small
targeted fixes, the owner picks what he hears.

## The knowledge base (2026-10-21): read these before starting new playback work

The owner's goal for October 2026: everything a later AI needs to make playback eventually sound perfect.
- `docs/OPEN_ITEMS.md`: every known imperfection, ranked by audibility, with its data, cause, tool and fix; its end lists
  the missing tools.
- `docs/TOOLS.md`: every measurement / verification / generation tool, and how to run it.
- `docs/RESEARCH.md`: published data for every playback question, and the ranked research tasks.
- `docs/SSO_SCRIPT_KNOWLEDGE.md`: what SSO's 700 patches' scripts and group data do (legato, CC119, round robins,
  releases, Tightness), marked used / contradicts / not used.
- **The owner's ears beat the heard check on timing.** On 2026-10-21 he listened to each section alone with a click
  and preferred plainTiming 0, which the heard check (transition = f0 arrival) calls 130-200 ms late. A legato
  transition is heard near where the pitch leaves the note before. Judge transitions by leave time (OPEN_ITEMS.md
  item 2). Private record: `~/MuseScore/ms3fork/private/plain-timing/OWNER_EARS.txt`.
- **How he judges timing:** one section alone with a metronome click, long rests cut out, about 20 s a clip, one
  question per page.

## Vial levels (branch `vial-levels`, 2026-10-27, from integrate-1017 4e6d877914)

A Vial part as loud as the SSO part at the same written dynamic (the owner: measured, not by ear): docs/PLAYBACK_SETTINGS.md ›
Vial levels; `tools/soundlibraries/vial_levels.py` sweep / fit / check (Linux kthost, Vial c29c49e, the owner's presets read
in place). Matched on loudDb; the check (real plug-in, MuseScore's CC11 and gain stage under Recommended) within 0.53 dB.
Open: the winds measure +2 .. +7 phon louder on Vial by the other loudness measures (envelopes differ): if the owner hears
Vial's winds too loud against SSO, matching on phon is the alternative (`fit` takes the measure from `offsetBy`). Untried
on Windows Vial (same code; the Windows build reads the same presets).

## Several libraries: Vial Instrumental as an engine (branch `vial-engine`, 2026-10-26, from integrate-1017 0f4abe48ea; not merged)

The owner (2026-10-26): "natively integrate Vial Instrumental as another playback engine, like we have now for ms3, ms4
and sso"; MuseScore uses his installed Vial (Vial.vst3 where its installer puts it, the VST 3 folders); not found: the
engine grayed, its tooltip saying so; Vial stays a hosted VST 3 (GPL v3: never compiled or linked into MuseScore).
- What the user sees: the Mixer's / Play Panel's *Global playback* box and each part's *Part playback* list MuseScore 3,
  MuseScore 4 and every library by name (Spitfire Symphony Orchestra, Vial Instrumental, Ample Guitar M); an engine that
  can't play here (plug-in not found; over MIDI out every one but the global) is grayed with the reason as tooltip.
  Preferences › I/O › Sound library lists the engines once each, with a row *Vial Instrumental plays: Vial / Fuse* (the
  variant, preference `io/soundLibraryVariants`; a map "<engine> (<variant>)" is a variant). A part set to Vial plays
  through Vial.vst3 next to the SSO parts' Kontakt, each part its own instance.
- File: metaTag `partPlayback` `"mode": "library:<engine>"` (3.6 keeps metaTags); `"library"` stays the global one.
  Default unchanged: nothing plays otherwise until a part names another engine.
- Code: soundlibrary.h › engines (`setEngines`, `engine`, `engineOf`, `libraryOf`, `scoreLibraries`, `allRoutes`,
  `Route::library`, `LibInstrument::library`, `calibrationFor` + `setCalibrationLoader`, `otherEngineParts`);
  partplayback.h (`libraries`, `libraryOf`, `write` with names); rendermidi (`LibPart::library`, `libAt`, `plib`);
  soundlibraryhost (`syncSome` library by library, `_slotLibraries`, `Pending::library`, `pluginPath`'s override only for
  its library, `MS_VST3_PATH`; the export's `loadInstances` / `beginInstance` / `finishInstance` by the route's
  library); contextlevel (`allRoutes`, mics by the patch's map, `trimLibrary`); `Vst3Synth::setVarispeed(slot, on)`;
  playbackmode.h (`soundEngines`, variants, `soundEngineAvailable`, `addEngineItems`, `updateSoundEngines`); the Mixer,
  Preferences, Create / Update Live Set's warning.
- Per place: per library = routes, renderer, hosting, export instances, Mixer gains, volumes (section balance, level
  rule, real range, mics), master trim (measured on the first library with a crest, applied to all); the global
  library's only = even levels' measurement, the audit, the verification, Live and MIDI out (the other engines' parts
  MS4 there, said before a Live set is written: LIVE.md › What still differs), the Sound Library window,
  `PartControllers::setSetupValues` (a part's unautomated value read from the global library's setups).
- Tested (Linux, 2026-10-27): `tst_liveequivalence::twoLibraries` (test synth as the current library standing in for SSO,
  the real Linux Vial.vst3 7a75da1 with the private preset pack for the Flute, in the helper processes and in-process);
  Whence `dumpEvents` before / after (Recommended, chunked: 53 files identical; `MS_DUMP_LIVE`: 56 identical);
  tst_liveequivalence 85 / 4 skipped, tst_soundlibrary 155 / 7 skipped (`vialHosted` skips in the whole run: `vialMap`
  unsets MS_PRESET_FOLDER before it, as on integrate-1017; run alone with the real Vial: passes), tst_liveintegration 69.
  Untried: Windows, the GUI boxes by hand, SSO + Vial with Kontakt.
- Finding: Vial hosted in a helper process (Linux) is silent for ~200-600 ms of the GUI event loop after
  `SoundLibraryHost::sync` (a sleep doesn't help; in-process it sounds at once): the test waits on the event loop. On
  playback that is under Play's own start; check on Windows whether a first note right after a load is cut.
- Left: Live and MIDI out for a second engine (its parts MS4 there), even levels / audit / verification for the global
  library only, `setSetupValues` from the global library, Vial Fuse chosen per engine only (not per part).

## Vial Instrumental (branch `vial-map`, 2026-10-25, from integrate-1017 b163700144; not merged)

The owner's "SSO imitations" Vial presets as a sound library (approved 2026-10-25, relayed by the vial sessions).
- Maps (generated: `tools/soundlibraries/gen_vial.py`, ids from the SSO map): `Vial Instrumental.xml` (hosted Vial.vst3,
  `<data>/Vial Instrumental/Presets/SSO imitations/<name>.vital`) and `Vial Instrumental (Fuse).xml` (Vial Fuse.vst3,
  `<data>/Fuse/Vial Instrumental/SSO imitations/<name>.vialfuse`, `presetHeader="fuse_preset"`). The owner picks one in
  Preferences › I/O › Library (one library at a time, OPEN_ITEMS 14); the data folder as Vial finds it (Vial.config's
  `data_directory`, else Vital's, else Documents\Vial), or *View › Sound Library… › Library folder…*.
- Code: `SoundLib::presetFolder` / `presetFile` / `presetComponent` (soundlibrary.cpp), `SoundLibraryHost::presetPath`,
  `savedSetupState`'s preset branch (the file's bytes are the component state, read at every load), the Sound Library
  window's folder box for preset libraries.
- What plays: one Long preset per part for every technique, CC1 dynamics as MS4 writes them, CC11 127, CC21 64 (vibrato;
  senza / non vib. 0, molto vib. 127, ord. 64) on the ten with vibrato (trumpet and tenor trombone added 2026-10-26, the owner via the vial session; their presets get the CC21 rows later, until then CC21 does nothing there). None of SSO's measurements (they're SSO's).
- Tested on Linux (`tst_soundlibrary::vialMap`; `vialHosted` with `MS_VIAL_VST3` + `MS_PRESET_FOLDER`, skipped without;
  `tst_liveequivalence::vialSetupState`): the real Linux Vial.vst3 / VialFuse.vst3 load the presets of the private pack
  (`~/vial-sso/work/cc/zip0`, unzipped outside the repo) through setState: every preset value comes back (Vial adds its
  defaults for 342 newer settings and "tuning"; JUCE appends its private data), in process and in a helper; it sounds,
  CC1 40 is 4.2-5.2 dB under 64. **CC11 / CC21 do nothing with that pack and the private Linux bundle** (the presets have no
  CC rows yet, the vial sessions' preset update pending): the CC11 check runs only where the preset has a CC11 row; with
  the gap-expression build of Vial and a preset given the row by `cc_apply.py`, CC11 64 = -5.95 dB exactly.
- Open: Windows untried (build only when the owner needs it); Live / MIDI out untried (LIVE.md › What still differs);
  `vialHosted` re-run 2026-10-27 on the CC11 pack (md5 cd7b74fb…) with Vial main 7a75da1's Linux bundles (`~/vial-bin/linux-vst3-7a75da1/`): all 26 (13 .vital, 13 .vialfuse) pass, every setting kept, CC11 64 -5.95 dB, CC1 40 -3.7 .. -5.2 dB, CC21 0 0.00 dB (no depth rows yet); each .vialfuse renders the .vital's rms to 5 digits (asked the vial session whether that is meant); re-run when the CC21 rows ship; the final CC21 list may
  change the ten (gen_vial.py PRESETS).

## Out-of-process host (branch `oop-host`, 2026-10-25, from integrate-1017 6dd53a9e97 + kontakt-procs; not merged)

The owner approved ("seems good") hosting the library's plug-ins in helper processes: build 480 showed Kontakt doing its
setState work on its process's GUI thread even when called from a worker, so MuseScore's window froze 8-18 s per big patch;
oop-load showed setState is one CPU-bound thread per process (VM: 37 s in 1 process, 21 s in 2, 19 s in 3). Design in
`audio/vst3/vst3remote.h`'s header; CLAUDE.md › Hosting › Plug-ins in helper processes.
- **What it is:** `MuseScoreLibraryHost[.exe]` next to MuseScore3Evo.exe; `[soundlibrary] hostProcesses` -1 (auto, new
  default: physical cores - 1, at least 2 on 2 cores / 4 threads), 0 in-process as before, `MS_VST3_HOST_PROCESSES` wins.
  The same Vst3Plugin calls replayed in the helper on the same kind of thread; same-block audio over shared memory.
- **Sound-identical (Linux, test synth):** `tst_soundlibrary::remoteNull`: 900 random steps (tuned notes on 3 slots incl. a
  linked one, varispeed glides, blocks of 1-4096 frames, CC1, parameters, Mixer, transport restarts, all notes off, settle)
  bit-identical in-process / 1 helper / 2 helpers, states identical. The full suites pass both ways
  (`MS_VST3_HOST_PROCESSES=0` and the default): tst_soundlibrary, tst_liveequivalence (verify, live equivalence, even
  levels, hosted slots, Ample state, background loads all run through helpers by default).
- **Round trip per block (remoteTiming, 4 instances in 2 helpers, paced like a callback, dev PC):** overhead (round trip less
  the helpers' own process time) median 41-53 us at 64 frames (p99 67-172), 54-65 at 128, 84-99 at 256, 144-163 at 512 (p99
  237-357); a 64-frame block lasts 1333 us. No added latency (same block).
- **Linux GUI, Whence with the test synth at 1 s a setState (22 instances by varispeed), lag probe o,l,r,g:** score open
  22.3-22.5 s in-process -> 5.1-5.3 s with 5 helpers (3 runs); an edit's / Play's 5 instances 5.1 -> 1.0 s; event loop max
  lateness at opening 211 -> 28-86 ms; audio callback mean 0.5-0.7 -> 0.6-1.0 ms of 21.6, overruns the same. (With the
  first rule, a patch's copies all to the helper holding the patch, the open took 10.1-10.3 s: the 4-5 copies of a patch
  waited for each other; now that helper is used only when it isn't loading.)
- **Crash / hang:** a helper killed: its instances silent at once, nothing waits; `SoundLibraryHost::checkHostProcesses`
  reloads them in a new helper and the part sounds sample for sample as before (`tst_liveequivalence::hostCrashRecovery`);
  a patch whose helper ended 3 times isn't reloaded. A helper that doesn't answer a block for 15 s is ended (`remoteHung`,
  SIGSTOP). The even levels' measurer drops a dead instance's result and reloads it.
- **Fixed on the way:** setParameter's processor side now goes with the next block (it raced the helper's main thread:
  vst3Settle); the GUI thread's parameter reads of a busy helper come from the cache (kept across a setState; 1.15 s -> 0);
  `EvenAutoMeasurer::loadStep` returned before finishing a load in steps when no other job waited (the measurer then stayed
  busy for ever: the probe stuck after phase l in 2 of 5 runs; a latent bug in-process too); kthost links vst3remote.cpp
  but hosts in-process.
- **Offline renders** use helpers too (they go through load()); nulled on Linux by the suites; on the VM: the heard check.
- **Windows builds:** build 482 failed (std::max under windows.h in the helper, no PCH:
  https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/40223936264), build 483 at 9a8edc7e0a good
  (https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/40224978823; the CI runner: tst_soundlibrary 150 passed / 8
  skipped, remoteNull bit-identical, remoteTiming 38.5 us median at 64 frames .. 95 us at 512, p99 77-382 us).
- **VM, build 483, Kontakt 8 + SSO, Whence (quarter tones), Recommended** (`C:\claude\b483\ms`, scripts in private/checkoop):
  - Probe o (score open), helpers / in-process (same build, `MS_VST3_HOST_PROCESSES=0`): every instance loaded in 15.0 /
    15.3 s against 29.2 / 32.1 s (first cold helper run 30.8 s, in-process 29.7); event loop max lateness 182-189 ms against
    6.6 s; Windows' "Not responding" 0 against 1-2 times. The window stays free with Kontakt now (it didn't with build 480's
    async load).
  - Probe l,r,g with helpers against build 480: an edit's reload 15 ms (58.9 s), pitch-up during loading 0.42-0.50 s
    (5.6-9.7 s), r's play press 0.23 s (5.3 s); g plays on after the change; no "Not responding".
  - MS_AUDIO_TIMING, 60 s of Whence playing, ABBA, budget 23.22 ms, ~2300 callbacks: helpers 3 / 4 over budget (p99 8.2 / 7.6
    ms, max 167 / 93), in-process 5 / 1 (p99 6.4 / 6.5, max 389 / 382). Round trip overhead on the VM median 380-440 us, p99
    ~2.5 ms. No crackle measure worse than in-process; nobody listened.
  - Open: while loading (stopped) the audio callback stalled once per helper start, ~2.6 s (mean 2.2 ms; in-process max 0.9).
    Not in the helpers' round-trip maxima (<= 166 ms); cause unknown; nothing plays then.
  - Kill test: the helper holding 8.2 GB killed while playing: noticed the same second, MuseScore stayed up, 5 instances
    reloaded in 7.6 s (one in a new helper), playback went on; ~7 s silent in the recording.
  - Heard check (export, per route) with helpers against check480's t1: 0 differing events in all five parts, detector 0
    findings; report ARRIVAL 36 fail / 16 warn, UNHEARD 194; in-process control on build 483 (t1i): 35 / 16, 190; check480:
    32 / 18, 190; RHYTHM, LEVEL the same: take spread.
- **Untried:** Ample in a helper (the AGM trial ends 2026-10-25); the editor window in a helper (a top-level window of the
  helper's, `AllowSetForegroundWindow`); closing MuseScore during a load with helpers on Windows; MMCSS in the helper; the
  owner's PC (6 cores: more helpers), a cold local disk.
- Files outside the repo: `~/MuseScore/ms3fork/ovl-oophost/` (overlay build, lp/ probe logs), `private/checkoop/` (VM).

## Paused 2026-10-18: where to pick up

The owner paused the project. This branch (`integrate-1017`) holds everything merged; all fork tests pass on Linux.
It has no Windows build yet (the last one is 98572aa). Merged since then: `trim-render` (playback settings reads
cached: the Even levels master-trim render 200 -> 20 ms, byte-identical) and `stac-levels` (the plain grid measured
in loudness metric P0: calib staccato RMS error 1.36 -> 0.72 dB, Whence 1.6 -> 0.8 dB; open: slurs and single notes
0.5-1 dB under the held note at mp, unexplained).

Not merged, each with its own "Paused 2026-10-18" note on its branch:
- `own-legato`: our own legato instrument from SSO layers recorded with zones muted (recording-only). Slow slurs
  passed a blind test; run joins at 150 ms pass on held-out phrases except the first (attacked) note into the second.
  Next: that join, then an sfizz build and a new blind round. 89 ms runs not attempted.
- `load-small`: NOT merging (measured on the VM 2026-10-18, private/load-small-work/RESULT.md). Fix 1 (Kontakt's module
  loaded on a worker thread) hangs Kontakt in every run: the first setState never returns (GUI, 150 s), the headless
  verify waits with no CPU; it saves only ~1 s on the first instance. Fix 2 (no mixer / even-levels work between loads)
  is worth at most 0.3-0.6 s, inside the 24-31 s spread of a Whence open; re-measure it alone only if load time matters.
- `kontakt-procs`: Kontakt instances in parallel host processes, `[soundlibrary] hostProcesses` (0 = in-process,
  the default). Linux only; with 2 processes forced, tst_soundlibrary vst3Settle and transportRestart fail (bugs),
  vst3LoadTimes / vst3UnloadModules / pluginDescribe need a decision. Next: those, a Windows build, null and
  dropout tests, load times.
- `instance-reuse`: a pool of unused instances; null-tested, but no measured load-time gain. Not merging.

Load-time facts: Kontakt's setState is the cost (the instrument script is compiled once per tuning slot, +1.3-1.7 s
per slot); MuseScore's own work is about 4 % of a score open.

## Chunks equal the whole render (branch `chunk-equal`, 2026-10-25, from integrate-1017 0cb99e84be; not merged)

Live playback (Seq: 10-bar chunks) now sends exactly the events the whole-score render sends (export, Live's clips, the
plain set). A fix, no setting; design: CLAUDE.md › Sound libraries › Chunks, header of `MidiRenderer::renderChunk`.
- **Found** (`tst_liveequivalence chunkCompare` + `tools/playbackverify/chunk_diff.py`, chunk sizes 10 / 1 / 2 / 3 against
  one chunk, each chunk in its own map; Whence, the owner's 48-bar copy, share/verifyplayback, the 33 soundlibrary mtest scores (SSO map),
  Recommended / Library default, Ample with strums; integrate-1017 0cb99e84be): note-ons moved by 1-740 ms (median 69; at
  Seq's own size in Strings.musicxml: a note at the chunk's first tick 195 ms late, its note before cut late), note-offs
  -24 .. +734 ms (joins, keepMs), pitch bend in force wrong by up to 74 cents (100 with the 0-everywhere settings) for up
  to 191 (427) ms (glides lost), CC11 up to 7.8 dB (24 dB) for up to 4 s (a level held from a note > 6 s back,
  attackLevels), CC1 up to 63 for 10.7 s (chunks of 1-3), switches CC32 moved up to 740 ms, the pedal (CC64) 41 ms late
  in Piano pedal chords (both presets), CC119 (speedFromWritten) up to 109 for 291 ms, Ample's M-Master Vol 1 tick wrong
  (0.50 / 0.43) and a keyswitch restated at a chunk's start. Most need a forced break (no clean bar line for 20 bars).
- **Fix**: each chunk rendered inside a window around it, its own events kept; levels / even levels' glides / even
  levels' step grid / microtone spellings that last longer than the window come from the window before (renderChunk).
- **Proved**: every chunked render (sizes 1, 2, 3, 10; own maps and one map; Recommended, Library default, and with
  plainTiming, transitionLevels, transportReset, hallGuard, fastTransitions, speedFromWritten, fixedTransitions,
  plainJoins, attackLevels 1 on) byte-identical to the whole render on all the scores above; the whole render and Live's
  clips byte-identical to before (the plain set is built from those renders: minChunkSize 0, untouched); the audit on
  Whence and the owner's copy identical. A seek (the middle chunk first): notes identical, CC11 differs only on silent
  routes (a level from before the first window). Test `tst_soundlibrary::chunkedEqualsWhole` (fails with
  `MS_CHUNK_WINDOWS_OFF=1`). clickedNote needed `setKeepWindow` (its state in force came from the chunk's restatements).
- **Untried**: on the VM / with real SSO or Ample (needs a Windows build); the GUI's Seq itself (the harness renders as
  Seq's background and first fill do). Rendering cost at 10 bars about the same; at the window's 6 s / 4 s margins a
  note or level lasting longer and not carried would differ (none found). `libSlurAcross` / `libNoteAfter` (chunk
  breaks avoided at slurs / bar-line notes) are no longer needed but kept (fewer windows).

## Chunk-end levels (branch `chunk-end-levels`, 2026-10-25, from integrate-1017 6dd53a9e97; not merged)

The owner (live playback, SSO hosted, his Whence with empty bars appended after bar 16 to 48): "after bar 16 at the start of
bar 17, it suddenly gets really loud before stopping". A fix, no setting; CLAUDE.md › Sound libraries (libraryNoteLevels).
- **Cause** (chunked dump, as Seq plays: 10-bar chunks): the per-note levels lived inside one chunk. At a chunk's end
  `libraryNoteLevels` put the written values back (a real range's CC11 kept), and the next chunk's start restated the
  written controllers (CC11 = the map's expression 127, the dynamic's CC1) unmapped, with no level in force. Every SSO
  route jumped at bar 17 (the boundary) while the last notes' releases rang: Violins 1 - Performance slots +11.7 .. +17.0
  dB, Violins 2 +12.0 .. +12.5, Violas +6.7 .. +16.1, Celli +14.8 .. +24.0, Basses +12.8 / +13.4 (CC11 8-59 -> 127;
  tails 0-2.3 s after their note-offs). Even levels' glide was put back at the chunk's end the same way.
- **Fix**: `MidiRenderer::libCarry` (by the chunk's end tick): each route's level in force (LibLevel, the written values)
  and even levels' last glide go to the next chunk, which maps its controllers by them (its start's restatements among
  them) until the route's first note-on, where the level switches as inside a chunk. Nothing at a chunk's or the score's
  end. A chunk rendered without the one before (a seek, the first after an edit) starts as before (nothing rings there).
- **Proved** (dumps through tst_liveequivalence dumpEvents, defaults and Recommended, chunked / renderMidi / Live's
  clips; pre = integrate-1017's build, identical to the owner's dump): the owner's copy changes only at the chunk
  starts of bars 17, 27, 37, 47 (chunked: CC11 now held at the level, 34 route files) and at the score's end (bar 48:
  same-value or 70 s-late restores gone); Whence (16 bars, one chunk) only at its end, every removed event the value
  already in force: no sound change. Note-ons / offs, bends, trace, gains unchanged; the audit on both identical;
  editLagIdentical on both passes. Test `tst_soundlibrary::chunkEndLevels` (fails before the fix at bar 2's chunk start
  and at the score's end).
- **Untried**: on the VM / with the real SSO (needs a Windows build). Not covered: Ample strums' CC7 level
  (`guitarplayback.cpp`, restored at the chunk's end, and the volume lane's restatement at the next chunk's start): the
  same kind of jump on a strum ringing over a chunk boundary.

## Async load (branch `async-load`, 2026-10-24, from integrate-1017 426a759d88; not merged)

The owner: "is it possible for SSO to load without locking up the rest of MuseScore? just have the playback button grayed
out during it". Done as a fix (no setting); design in soundlibraryhost.h's header, CLAUDE.md › Hosting.
- **Where it blocked** (measured, Linux, test synth standing in for Kontakt with `MSTESTSYNTH_SETSTATE_MS=1000`, Whence
  = 22 instances by varispeed; lag probe, new phases `l` (every instance released and loaded again, then scroll / select
  / pitch-up / undo / Play while loading) and `r` (Violins 1 to MS4 and back, Play right after); window-system input
  from `tools/lagprobe/linux/input.py`, xdotool wheel steps every ~200 ms): score open's preload, one `preloadStep` per
  instance holding the main thread for its whole setState (20 x ~1.0 s, 20.2 s of 25 s; event loop max 1117 ms late);
  Play while loading: `Seq::start` -> `sync` waited for every remaining instance, 17.1 s, user input held back
  (`harvest`'s `ExcludeUserInputEvents`; wheel input stalled 17.4 s); Play after a part's change 5.0 s the same way;
  the Sound Library window's Show (`sync`). On the VM with Kontakt + SSO the same stalls were measured before (lag
  probe build 442: "at opening the preload's setState 7-9.6 s stalls"; load profile: setState 1.9-19.5 s per instance).
  Stays on the GUI thread (VST 3 / Kontakt): a new instance (0.2-0.6 s on the VM; on a worker it hung Kontakt,
  load-small), `settle` (3-80 ms), resave of a setup made from its .nki (first load only), the parameter reads.
- **After** (same runs): score open no main-thread stall from loading (max lateness 60 ms; preloadStep p50 0.0 / max
  17 ms, finishLoad max 1.4 ms); while loading, Play is grayed ("play enabled 0" in the probe's notes; pressing it 0.1 s,
  the rewind's render), edits respond as when nothing loads (select 150, pitch-up 230 ms: the edit's own renders), input
  stalls at most 0.5 s (an edit); Play after the part's change 46 ms (grayed: the edit check had started the load);
  the edit check 2-11 ms on Whence (routes cached by the undo state; a 21-part score's routes cost 0.3-0.5 s, once a
  second after the last edit). Total load time unchanged (25-26 s for 22 x 1 s). Phase `g` (Violins 1 to MS4 and back
  while playing): playback stops, its 5 instances load in 5.1 s ("the window free meanwhile" in load times.log), then
  it plays on; input never held > 0.43 s. Quit (Ctrl+Q) during loading, setState 5 s (`tools/lagprobe/linux/
  close-test.sh`, even levels off so that the score isn't modified): exit 0 after 1.9 / 4.0 / 5.1 s (the setState in
  flight); with 2 s, Discard at the save prompt: exit 0 after 2.0 s. A screenshot showed Play grayed and "Loading
  sounds: 4 of 22…" while the save prompt was up.
- **Sound-identical**: the same calls in the same order on the same bytes; only setState's thread changed (Kontakt took
  setState on worker threads in --measure-load-times 2026-10-03 / 10-15 and in the even levels' measurer since
  2026-10-17). `tst_liveequivalence::asyncLoadHosted`: the states loaded, their order and each instance's getState
  byte-identical to the blocking sync, the rendered note sample-identical; the event loop never > 150 ms late with 300 ms
  setStates; an edit queued during loading; whenLoaded once; release() during a setState; a second score meanwhile.
- **Untried on Windows with Kontakt / SSO** (needs a Windows build; the VM's quiet.lock was held by another session):
  check on the next build: open Whence, scroll / edit while it loads, Play grayed with "Loading sounds: n of m", Play
  once loaded, close MuseScore during loading (waits for the one setState in flight, then exits), a load times.log with
  "set on a worker thread" lines; the lag probe's phases `l,r` (`MS_LAG_PROBE_PHASES=l,r`, tools/lagprobe/vm/go.cmd).
  Open: whether Kontakt needs anything from the GUI thread during a worker setState while the user works (the event
  loop runs, so a posted / sent message is served; not seen in the measurer's loads).
- Not changed: an audio export still waits (`SoundLibraryExport` -> `sync`, modal); the GUI-thread steps above; Play
  doesn't start by itself once loaded, except after a change that stopped playback (GoOnPlaying) as before.

## Staccato length (branch `staccato-length`, 2026-10-24, from integrate-1017 e951ee4a69; not merged)

`[shorts] staccatoLength` (docs/PLAYBACK_SETTINGS.md › Staccato length): a library staccato (staccatissimo) is meant to
sound, and is held, no longer than real players sound one (the map's `<Instrument staccatoLength staccatissimoLength>`,
generated from `tools/soundlibraries/staccato_lengths.json`, RESEARCH.md 2.5a, the owner's reviewed readings only:
https://claude.ai/artifact/GNwFDt1PcNHJ1UWoWugvDc). **1 by default and in Recommended, 0 in Library default**: the owner
heard the A/B clips (https://claude.ai/artifact/DvMdsMtTigjhLzztAjnTSz; Windows build #476,
https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/40126286353, MuseScore's export of real SSO on the VM,
`share/verifyplayback/Staccato lengths.musicxml`): Violins 1 "B for the first set, same for the next set", Celli "B",
Oboe "same", Bassoon "same" (B = 1). So it is heard to matter on slow string staccatos; the winds' SSO staccato samples
are already short (their clips differed 22-30 dB under the signal). Whence (Recommended, 1 against 0): 7 notes change,
all bar 8's staccato eighths (Violins 1 beat 3, Violins 2 beats 1-3, Violas beat 3), held 0.20-0.21 -> 0.19 s; the audit
identical (OVERLAP 0, UNMEASURED 0, CAPPED 0, ARRIVAL 12 fail, LEVEL STEP 12 info, the rest 0). Open:
- No Windows build with the new default yet (#476 has it at 0; a score or playback.ini with `[shorts] staccatoLength=1`
  plays it there).
- The heard length stays well above the players' because of SSO's hall on the Tree mic; RESEARCH.md 2.5a proposal c
  (measure SSO's shorts on the Close mic) and b (shorter notes before a rest) are not done.

## Balance in context (branch `anechoic-balance`, 2026-10-24, from integrate-1017 329e1fa54a; not merged)

The owner checked the score readings' spot-check page (40 of 40 right) and chose "apply the full measured balance"
(docs/RESEARCH.md › 1.5a). `sectionLevel2` (`[levels] sectionBalance` 2, Recommended and the default): flutes +3.96,
oboes -4.17, horns -8.29, trumpets -4.59 dB on every patch of the instrument, piccolo / alto / bass flute and cor anglais
by their family (an assumption); new step `tools/soundlibraries/anechoic_shift.py` -> `sso_anechoic_shift.json`, read by
`gen_spitfire_sso.py` (`ANECHOIC_SHIFT`); the map's diff is those 16 attributes. Per-patch table, the master trim and the
tests: docs/BALANCE_LOG.md › Iteration 15. Open:
- The owner hasn't heard it; no Windows build yet (a map-only change needs one, or the XML copied into the installed
  `share/soundlibraries/`).
- The harp's offset to the Clark & Luce scale comes from the solo winds' M2 (Iteration 6); with the shifted winds it would
  move -3.4 dB. Left as it was: an owner question.
- Trombones and tuba: Aalto only, no conclusion; clarinets and bassoons inside their uncertainty. A third dataset with
  several players per instrument would settle them (RESEARCH.md › 1.5a › Still unknown).

## SSO load: the allocator (branch `sso-load`, 2026-10-23, from integrate-1017 7a6e1e9f0c; not merged)

The owner, 2026-10-23: "is there any way for sso to load faster, taking the ableton optimization repo as example".
Then (the coordinator, after the owner's PC run): ship it, always on. Details and raw data:
`~/MuseScore/ms3fork/private/sso-load/RESULT.md` (outside the repo).
- The ableton-optimization approach (cache repeated file / registry / shell lookups) has nothing to cache here: on the
  owner's PC setState's file and registry calls are 0.04 s of 1.3-2.6 s.
- Where setState's CPU goes (sampled, VM and PC alike): Kontakt's code ~65 % (KSP compile about half), **the C
  runtime's heap 16-21 %** (PC 20.7 %), memset / memcpy 3-4 %. Violins 1 - Performance's setState makes 18.9 million
  malloc calls from Kontakt (2.2 GB).
- mimalloc in its place (`tools/soundlibraries/kheap`): time inside the allocator 2.73 -> 1.31 s for that setState.
  **Owner's PC** (Ryzen 5 5600X, 48 GB, Kontakt 8.13.1, his 15 last-used prepared states, 5 + 5 alternating, the kit
  private/sso-load/pc-kit): setState in all plain 54.3 / 50.5 / 56.7 / 73.1 / 126.9 s (median 56.7), mimalloc 42.2 /
  42.7 / 48.2 / 49.3 / 91.4 s (median 48.2): **-15 %**; CPU medians 59.9 -> 49.5 s; memory 14.5 -> 16.9 GB (+17 %).
  VM (paired runs): -12 %. Segment heap: less memory, no clear speed change.
- Null test of the injected DLL (VM, build 472, Whence `--verify-playback --verify-wav`, plain / mi / mi / plain): every
  part at the floor of two identical passes (Violins 2 -85.6 / -85.1 dB against -86.1 / -85.3; Basses -24.1 / -22.7
  against -22.8 / -24.2; the Performance parts at their floors).
- **Shipped (this branch):** `main()` calls `ProcessAllocator::start()` first (`audio/vst3/processallocator.*`):
  kheap.dll from MuseScore's folder, pinned, `kheap_start("mi")`. All 26 hooks are made, then enabled at once, or
  none (system allocator). A block mimalloc doesn't own always goes back to the CRT (also the reallocs: moved into
  mimalloc or left the CRT's); the whole `_aligned` family is answered (the CRT's returns pointers inside malloc
  blocks that mi_free can't take); ntdll's HeapFree / HeapSize / HeapReAlloc give mimalloc blocks to mimalloc;
  nothing is undone at exit (mimalloc's destroy_on_exit off, `MI_SKIP_COLLECT_ON_EXIT`), so Qt's and the CRT's
  teardown and `Vst3Plugin::unloadModules`' ExitDll free safely. `MS_KHEAP_OFF=1`: the system allocator; no kheap.dll:
  the system allocator. load times.log: "allocator: mimalloc (kheap.dll: mimalloc 2.1.7, 23 hooks)" / "system (…)".
  Stress test (VM and wine): blocks across the switch, every function, 8 threads: 0 failures. Linux: all fork tests pass.
- **Windows build #473** (https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/40031224805, sso-load @
  8b4f87c82f; kheap.dll in the artifact's bin/, the workflow checks it). Checked on the VM (`C:\claude\b473\ms`, private
  `sso-load/vm473/`):
  - load times.log: "allocator: mimalloc (kheap.dll: mimalloc 2.1.7, 23 hooks)" in every run, "system (MS_KHEAP_OFF=1)"
    with the variable.
  - Null test, MuseScore's own allocator start against `MS_KHEAP_OFF=1`, Whence `--verify-playback --verify-wav`,
    order off / on / on / off (`compare_renders.py`, residual dB):

    | part | floor off-off | floor on-on | off-on (n0-n1) | off-on (n3-n2) |
    |---|---|---|---|---|
    | Violins 2 | -85.5 | -85.5 | -85.2 | -85.6 |
    | Contrabasses | -23.9 | -22.0 | -23.6 | -19.6 |
    | Violins 1 | -7.0 | -6.3 | -7.8 | -8.4 |
    | Violas | -9.9 | -9.9 | -10.0 | -10.1 |
    | Violoncellos | -7.5 | -7.4 | -7.1 | -6.5 |

    Violins 2 (the part that renders deterministically) nulls at -85 dB every way, all per-note differences 0. The four
    Performance parts differ between identical passes (round robins), and the cross pairs sit in the floors' range. The
    Contrabasses' n3-n2 is 2.4 dB above both floors, so the other cross pairs were checked too: n0-n2 -20.1, n3-n1
    -20.4. The pairs in time order (n0-n1 -23.6, n1-n2 -22.0, n2-n3 -19.6) and the notes over 0.5 dB apart (floors 17 / 22,
    cross 14 / 23 / 22 / 25 of 444) show drift between passes, not a side: the same as the injected test on build 472.
  - `--measure-load-times` Whence, off / on / on / off: score open (phase 2, 10 instances one after the other) 44.3 / 39.1 /
    38.4 / 43.3 s (**-11 %**), setState in it 38.0 / 37.2 / 36.6 / 41.5 s; each patch alone (phase 1), setState summed 15.5 /
    14.5 / 16.4 / 20.4 s; memory after the score open +15.5 / +18.8 / +17.6 / +15.3 GB (the samples over SMB, still
    growing at 5 minutes). Whole runs 843 / 646 / 674 / 831 s.
  - `--create-live-set` (Whence.als, 30 s) and `--even-levels` (303 s, round 2 envelope off its slurs' medians
    0.55 dB) run as before with kheap.

## kheap and realtime audio (branch `kheap-realtime`, 2026-10-24, from integrate-1017 426a759d88; not merged)

The owner, 2026-10-24: since #474 live playback (hosted Kontakt + SSO through PortAudio) crackles a lot on every audio
device, exports null; `MS_KHEAP_OFF=1` cures it. Rule: an optimization never changes the sound. Raw data and scripts:
`~/MuseScore/ms3fork/private/kheap-realtime/` (outside the repo).
- **Fix (this branch):** kheap is scoped. `kheap_start("mi-scoped")`: new blocks come from mimalloc only on a thread
  inside `ProcessAllocator::LoadScope` (`Vst3Plugin::setState`); every other thread, the audio callback and Kontakt's own
  threads included, gets the CRT's blocks as with `MS_KHEAP_OFF=1`; a mimalloc block grown outside a scope (realloc,
  _recalloc, aligned reallocs, HeapReAlloc) moves to the CRT; mimalloc's `abandoned_reclaim_on_free` off, so no thread
  outside a load takes over a segment a finished load thread left. Frees of mimalloc blocks stay mimalloc's (from
  another thread: an atomic push). So during playback no realtime thread ever runs mimalloc's allocation, purge or
  reclaim paths. The whole-process mode stays in kheap for the tools only (`KHEAP=mi`).
- **Cause: not reproduced on the VM** (Windows 11, 4 vCPU, Kontakt 8.13.1, SSO over SMB), so not measured; the owner's
  PC is the judge. What was tried:
  - ktload `--play` (new: MMCSS "Pro Audio" thread, every instance one block at the real-time pace, dense legato lines
    + CC1, 48 kHz, 60 s at 512 and 60 s at 256 frames; V1 + V1P): cycles over the block's period, 512 / 256:
    system 42 / 155, 4 / 122; whole-process mimalloc 294 / 1767, 4 / 92; scoped 235 / 923, 0 / 18; mimalloc with
    `MIMALLOC_PURGE_DELAY=-1` 24 / 269; with `ABANDONED_RECLAIM_ON_FREE=0` 97 / 1048. The spread is the VM's (two runs
    of one variant differ more than the variants); page faults during play the same (0.36-0.64 M per minute every way).
    With each setState on a thread that then ends (`--thread-loads`, mimalloc's abandoned segments): system 24 / 260,
    whole-process 12 / 148, scoped 14 / 59. The output WAVs: no digital-silence gaps in any.
  - Ten states (Whence's strings, 4 Performance) don't keep real time on the VM in any mode (median cycle 13-16 ms of
    10.67).
  - During play (count mode, system allocator) Kontakt makes ~1,100 malloc + 1,100 free calls a second for two
    instances, ~66 k a minute (thread not identified): these are what mimalloc answered on Kontakt's threads before.
  - MuseScore #477 itself in the VM's desktop session playing Whence 12-TET into VB-Cable (recorded by a WASAPI
    capture, `wasrec`), digital-silence gaps of 1-100 ms in 64-68 s: kheap on 23, `MS_KHEAP_OFF=1` 20 (another off
    run 168: the VM's noise). No difference the VM can show.
  Hypotheses left (none confirmed): mimalloc's purge (decommit after 10 ms, recommit + zero-page faults) or its
  segment reclaim (`abandoned_reclaim_on_free`, `max_segment_reclaim`: a thread taking over a 32 MiB segment a dead
  load thread left, e.g. the even-levels measurer's std::async setStates) running on Kontakt's audio / voice threads;
  the owner's 12 hardware threads and real devices make OS-level costs (TLB shootdowns of decommits) larger than the
  VM's 4. The scoped mode removes all of these from every thread but a loading one.
- **Load time kept** (VM, ktload, Whence's 10 states, setState sum, 3 alternating runs each): system 46.1 / 26.3 / 22.2 s
  (median 26.3), whole-process 21.6 / 35.5 / 19.4 (21.6), scoped 25.9 / 23.0 / 23.6 (23.6); private memory 9.75 / 11.43 /
  10.79 GB. Scoped keeps about half of the VM's gain by these medians (the VM's noise is ±30 %); the owner's PC decides.
- **`MS_AUDIO_TIMING=<file>`** (new, `audiodrivers/pa.cpp`): every PortAudio callback while playing is timed against its
  budget (frames / rate); at each stop one line: callbacks, median / p99 / p99.9 / max ms, over budget, over half,
  underflow flags, and whether `MS_KHEAP_OFF=1` was set. Nothing without the variable.
- **ktload** (`tools/soundlibraries/ktload`): `--play s [--block n]` (repeatable), `--rate`, `--note-ms`, `--churn`,
  `--times <file>` (cycle times + the output WAV), `--thread-loads`; kheap's `kheap_scope` around each setState.
- **Windows build #479** (https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/40161792540, kheap-realtime @
  c4d7403594), checked on the VM (`C:\claude\b479\ms`):
  - load times.log: "allocator: mimalloc (kheap.dll: mimalloc 2.1.7 inside loads, 23 hooks)"; "system (MS_KHEAP_OFF=1)"
    with the variable.
  - kheap's scoped mode under stress (`private/kheap-realtime/stress/scoped.cpp`, the build's kheap.dll: 8 threads,
    1.6 million mallocs / reallocs / frees of every family across threads and scopes, blocks from before the switch):
    0 bad; outside a scope 1000 of 1000 new blocks the process heap's, inside 1000 of 1000 mimalloc's.
  - Null test, Whence `--verify-playback --verify-wav`, off / on / on / off (`compare_renders.py`, residual dB; floors
    off-off, on-on, cross n0-n1, n3-n2): Violins 2 -85.8 / -85.1 / -85.4 / -86.3; Contrabasses -23.6 / -22.7 / -25.5 /
    -22.0; Violins 1 -8.1 / -6.8 / -6.8 / -7.8; Violas -10.0 / -9.9 / -10.8 / -10.3; Violoncellos -7.1 / -7.5 / -7.1 / -6.7:
    every cross pair in its floors' range (Violins 2, the deterministic part, -85 dB every way). Its setState sums (one
    part at a time): off 25.1 / 26.8 s, on 23.9 / 32.1 s (the VM's noise; no gain shown here).
  - Live playback in the desktop session (Whence 12-TET into VB-Cable, 60 s, `MS_AUDIO_TIMING`, 13.06 ms callbacks),
    on / off / on / off: callbacks over budget 14 / 43 / 183 / 392, PortAudio underflow flags 0 / 1 / 6 / 57, recorded
    silence gaps 22 / 21 / 40 / 217. The VM itself underruns (4 vCPU, ten instances); scoped is no worse than the
    system allocator in either pair. `MS_AUDIO_TIMING`'s over-budget count follows the recorded gaps.
- **For the owner** (his PC decides): play Whence (or what crackled) with #479 as it starts, then again with
  `MS_KHEAP_OFF=1`, both with `MS_AUDIO_TIMING=<file>` set (the same file is fine: each stop adds a line saying which).
  Fixed if the default sounds like `MS_KHEAP_OFF=1` and its lines show no more callbacks over budget. If it still
  crackles: the default goes to the system allocator (kheap not loaded; `ProcessAllocator::start` without kheap.dll).
  Score-open time against #477 from load times.log ("At score open: … setState …"): the gain the scoped mode keeps.
- The Ableton kit's kheap (`~/ableton-optimization` branch `kheap`, LiveCached starts `KHEAP=mi`) is the whole-process
  mode, the same exposure for Live's and Kontakt's audio threads; not installed (install-kheap.patch not applied), only
  the test kit runs it. Live has no setState hook to scope: don't install it; if wanted, a mode with mimalloc only on
  Live's main thread would need its own realtime test in Live first.

## Build for the owner: #485 (2026-10-27)

Windows build #485 (https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/40454299721, integrate-1017 @ 377a25afca):
#484 plus chunk-equal (live playback's chunks = the whole render), the Vial map, docs. Its one test failure, vialMap, was a
test-only Windows path issue, fixed in 0f6ece92de. Checked on the VM (private `check485/RESULT.md`, real Kontakt + SSO,
nobody listened; the VM is now at 172.28.59.4):
- Whence's export is the same as build 483's except the CC11 restates at the score's last tick, which are gone (chunk-equal).
  Detector 0 findings. The heard-check counts are within take spread. The audit matches check480's (ARRIVAL 12, LEVEL STEP
  12 info, the rest 0).
- Opening with helpers matches build 483 interleaved under the same load (35-39 s each today: the VM was slow, every
  setState ~2.5x 2026-10-25's). "Not responding" 0. In-process (`MS_VST3_HOST_PROCESSES=0`): 80.6 s and 5 "Not
  responding".
- Play works. Callbacks over budget are equal for builds 485 and 483 today (the VM's load).
- **Closing during a load now works**: the save prompt appears within 2.4-4.5 s, MuseScore exits 0 within ~5 s, and no
  helper is left. Build 480 waited for the whole load. One cosmetic log line: a helper ended at shutdown is reported as
  "loading them again", but nothing reloads.
- Since 3e31313ce9 (`[slurs] plainBowed`, 1 in Recommended), dumps of HEAD no longer match build 485's Recommended. Use
  plainBowed=0 to compare against 485.

## Build for the owner: #484 (2026-10-25)

Windows build #484 (https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/40230637035, integrate-1017): #481 plus
oop-host (sound-library plug-ins in helper processes; checked on the VM as build 483: HANDOFF.md › Out-of-process host).
The merged tree itself not run on the VM (483 = oop-host without chunk-end-levels, 481 = the rest). For the owner to try:
loading while editing, crackles (MS_KHEAP_OFF removed), his Whence at bar 17, a Kontakt editor, closing during a load;
`MS_VST3_HOST_PROCESSES=0` for in-process.

## Build for the owner: #481 (2026-10-25)

Windows build #481 (https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/40191534206, integrate-1017): #480 plus
chunk-end-levels (a note's level and even-levels glide carried across chunk boundaries: the owner's Whence copy jumped
+7..+24 dB at bar 17 in live playback). Linux dumps only (the export and Live's clips render one chunk: unchanged but
for the score's end); not run on the VM: the fix acts in live playback's chunks, which the VM's offline checks don't
render. The owner to check his copy at bar 17.

## Build for the owner: #480 (2026-10-25)

Windows build #480 (https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/40165471973, integrate-1017 @ 796eb9b0ae):
#477 plus pa-device-name, pa-buffer (Preferences › I/O › Audio buffer), async-load, kheap-realtime (mimalloc only inside
setState, per thread). Why: the owner's live playback crackled on #477 on every device and stopped with MS_KHEAP_OFF=1
(2026-10-24); he asked for loading without locking up the window. Checked on the VM (private `check480/RESULT.md`, real
Kontakt + SSO, nobody listened): Whence's export identical to #477's (5 parts, 0 events differ); no crash or hang; Play
grayed while loading, then plays; the device found by name; Audio buffer sets the callback budget; live callbacks over
budget scoped vs off within noise (the VM can't reproduce the owner's crackling). **Two findings:**
- **Async load doesn't help with Kontakt on Windows**: Kontakt runs its setState work on the GUI thread even when called
  from the worker (GUI thread 64 % CPU, workers 0.6 %), so the window still freezes 8-18 s per big patch ("Not
  responding"); a close / Ctrl+Q sent during that freeze takes effect after the load or is lost. Only hosting Kontakt in
  another process would free the window (not started; the owner to decide).
- **Scoped kheap loses the load speed-up**: Kontakt's load allocations are on the GUI thread, outside the worker's scope
  (memory and load time as with MS_KHEAP_OFF=1). A scope on the GUI thread while a load is in flight would bring it back;
  untried, and only after the owner confirms #480 doesn't crackle.

## Build for the owner: #477 (2026-10-24)

Windows build #477 (https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/40132296643, integrate-1017 @ b38f02ee4a):
#475 plus `[shorts] staccatoLength` 1 by default and in Recommended (the owner's A/B: Violins 1 and Celli "B", Oboe and
Bassoon "same"). Checked on the VM (private `check477/RESULT.md`): Whence's export changes only the 7 bar-8 staccato
eighths' note-offs (0.202-0.211 -> 0.190 s) and the Violins 1 beat 3 accented attack's CC11 (68 -> 66: its level is looked
up by played length); Celli and Basses byte-identical to #475; the staccato score held min(old length, map value) to the
tick, tenuto-staccato unchanged, string tails 3-12 dB lower where capped, winds -0.8 to -1.5 dB; detector 0 findings;
mimalloc in every run. research-reverb (docs only) merged after the build.

## Build for the owner: #475 (2026-10-24)

Windows build #475 (https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/40096814080, integrate-1017 @ d001c96cc3):
#474 plus the anechoic wind / brass balance (BALANCE_LOG Iteration 15, the owner's "Full", 2026-10-24), verify-detector2,
the staccato research. Checked on the VM (private `check475/RESULT.md`): Whence's events byte-identical to #474's; a winds
test score's routes moved by exactly the shifts (Flute +3.96, Oboe -4.17, Horn -8.29, Trumpet -4.59, the rest 0.000 dB);
the detector 0 findings on Whence; mimalloc in every run.

## Build for the owner: #474 (2026-10-23)

Windows build #474 (https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/40057511007, integrate-1017 @ 24913b6ad4):
#472 plus kheap (mimalloc, SSO load), the verify-detector fixes, tone_check.py, the anechoic balance research. Checked on
the VM (private `check474/RESULT.md`): MuseScore's export of Whence byte-identical to #472's (events, CC11 included),
levels within take spread; score open 21 % faster (memory +25 %); Ample identical; repeat check pass; tone_check re-finds
sso_bad_samples' fault on real SSO and leads at keys 45, 46, 50 (not heard, not in the map: an avoid entry changes the
sound, the owner's call); verify detector: one false positive left (Celli bar 13's quarter-flat Ab3 on a voice copy,
branch verify-detector2). One 0xC0000005 in an Ample load with MS_KHEAP_OFF=1 (the system allocator; AGM's known load
crash, OPEN_ITEMS 12), none in 5 with mimalloc.

## Build for the owner: #472 (2026-10-23)

Windows build #472 (https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/39962541862, integrate-1017 @ 4055da9f06):
#470 (microtoneBends 3, plainJoins 0, live-realcheck, live-celli) plus `[levels] attackLevels` 2, rises only. Checked on
the VM (private `check472/RESULT.md`): every export event but CC11 identical to #470; CC11 differs only at the 19 moved
rises; Violas bar 12's rise before the slur's beat +3.4 -> +1.1 dB (even levels +6.6 -> +1.1); #471's two regressions
(drops moved) back to #470's within 0.1 dB; Ample identical; repeat check pass. Installed on the VM at `C:\claude\b472\ms`.
(#471 moved drops too: skipped.) The owner, on #472: "hell yeah whence finally sounds good now" (2026-10-23): the
Recommended preset at 4055da9f06 is his reference for Whence; a later sound change on it needs his ear again.

## Attack levels (branch `attack-levels`, 2026-10-22/23, from integrate-1017 8a827c2026; not merged)

The owner, Whence's Violas alone in #470: "when the slur starts. the volume just like jumps in a disjointed way" (bar 12
most). Cause (private `violas-slurstart/RESULT.md`): the slur's first note, an attack after a staccato on the same key,
sent its level-rule CC11 step with its note-on, 111 ms before the beat (plainShift).

- `[levels] attackLevels` (docs/PLAYBACK_SETTINGS.md › Attack levels): an attack whose route still rings the note before
  (within the map's `release`) takes its CC11 level 1 at note-on + its measured onset (plainAttack, 37 ms), 2 at its
  written time (plainShift / its early start undone). **2 by default and in Recommended, 0 in Library default**: the
  owner, 2026-10-23, "B better" (Whence's Violas bars 11-13 alone with a click, even levels on, kthost renders of #470's
  kthost + Kontakt 8 + SSO; A = 0, B = 2 as a map copy with plainAttack 111).
- Measured (kthost, private `attack-levels/RESULT.md`): 1 changes almost nothing (bar 12's rise before the beat +3.7 ->
  +3.6 dB): SSO's attack itself sounds from 74 ms before the beat; 2 moves the step after the beat (+1.2 before / +1.7
  after; even levels +1.3 / +4.4).
- Whence, Recommended, 0 -> 2: 35 attacks' levels move (Violas 18, Violins 2 8, Violins 1 5, Celli 2, Basses 2; each by its
  plainShift). Audit: LEVEL STEP 0 -> 12 info (the audit reads a note's level at its note-on: each Violas / Violins 1 slur's
  second note against the first's, still the note before's), everything else the same.
- Build #471 (VM, private `check471/RESULT.md`): bar 12's rise before the beat +3.4 -> +1.1 dB (even levels +6.6 -> +1.1),
  timing / tuning identical, but two drops after an accent (Violas bar 8 beat 4.5, Celli bar 9 beat 1) +4.7 / +5.9 dB louder
  before the beat. So 2 moves **rises only** since 2026-10-23 (a drop with the note-on, as #470, which the owner called
  perfect there). Untried in real Live (the plain set's CC11 lane carries it: `attackLevelsLive`).

## Slur joins (branch `slur-joins`, 2026-10-22, from integrate-1017 6d0eaddb57; not merged)

The owner, Whence's Violas alone in build #464, Recommended: "viola bar 12 and 16 slur first note sounds disjointed from the
note before". Bars 12 / 16: G4+50 staccato eighths (the key's quarter-sharp G), a slur of sixteenths G4+50 F#4 Eb4 F#4,
then an unslurred G4+50 half note touching the slur's last F#4. The slur's first note after the staccato is played as in
bars 9-11 / 13-15 (an attack after the staccato's gap, CC11 86 -> 108-110; envelopes the same within 1 unit). The join
that only bars 12 / 16 have is **the slur's last F#4 into the G4+50 half note**: under plainPerformance a transition, but
`SoundLib::lanes` put the half on the copy already at +50 (bar 11's G4+50), so it was attacked there 95 ms before its beat
(pitch arrival; leave 162 ms before) while the F#4 kept sounding on its own copy for ~200 ms (two pitches a 3/4 tone
apart). VM, #464's export, Violas alone; private `~/MuseScore/ms3fork/private/slur-joins/RESULT.md`.

- **The owner's verdict, 2026-10-22** (the clips of #464 against #466, Violas alone with a click): "violas A better for
  both": the attack, as before. The half note is unslurred, so a new bow stroke; #466's transition also lands its G late
  and fades the F#. So the fix became **`[slurs] plainJoins`, 0 everywhere** (not in either preset); Whence plays as #464.
- **Fix, now `[slurs] plainJoins` 1** (first made a bug fix on by default): such a note stays on the note
  before's copy (docs/PLAYBACK_SETTINGS.md › Plain Performance › Joins across tunings). Whence: 7 split joins -> 0 (also
  Violins 1 bar 16, Basses bars 10, 11, 14, 15); every note's time / key / velocity unchanged. Windows build #466
  (https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/39774787367), installed on the VM in `C:\claude\b466\ms`:
  one sound through the join, the half's pitch +85 ms after its beat (a fingered transition under plainShift, as every
  transition there: OPEN_ITEMS 2).
- **`[levels] transitionLevels`** (0 everywhere until the owner has heard it; proposed 1 in Recommended): with the join on
  one copy, the half's lower level-rule CC11 (a single note's 47 against the slur's 110), sent with its note-on, turned
  the F#4 down 6-7 units over its last 100 ms (#466). 1 moves a transition's CC11 step over its glide (leave -> arrival).
  kthost + SSO on the VM, Violas, Recommended with 0 / 1: the F#4 at 39.4-40.2 to 90 ms after the half's note-on with 1,
  falling to 31-32 by then with 0 (docs/PLAYBACK_SETTINGS.md › Transition levels). Not in #466: the owner hears it in the
  next build.
- Quarter tones: both joins involve the key's quarter-sharp G (the half is reached by a bend glide from 0 to +50 on the one
  copy now), so branch `microtone-next`'s glide changes act on them; this branch changes only the copy choice and the
  level timing, not the glide.
- Clips for the owner (not published): `~/MuseScore/ms3fork/ms37/.claude/worktrees/integrate-1017/.mixedtech/joins/clips/`
  (A #464, B #466; bars 11-13 and 15-17; `clips.json` › `joins`: each join's clip second).

## Microtone bends (branch `fast-microtones`, 2026-10-21, from integrate-1017 0d30c7e614; not merged)

The owner, Whence's Violas alone (build #458, Recommended): "the 3rd measure pitch bending sounds weird" (bar 7: A3, B3 -50,
A3, G3 +50 sixteenths at 110 bpm on Violas - Performance, velocity 64: the fingered transition). Found on the VM (kthost +
SSO, MuseScore's own route events, pYIN): the bend followed `bendGlide`, the bowed transition's timing (from the note-on,
arriving at 130 ms), while the fingered one leaves 25-100 ms after its note-on and arrives at 130-200 ms; and B3 -50 after
A3 went on B3 bent down (key up, bend down), so the pitch first went a quarter tone the wrong way. Built:
`[slurs] microtoneBends` (0 everywhere until the owner has heard it): 1 the key on the side of the note before
(`microtoneSpelling`) + `bendGlideFingered`, uncut past the next note-on; 2 the glide alone (docs/PLAYBACK_SETTINGS.md ›
Microtone bends; OPEN_ITEMS.md 5). Tests: tst_soundlibrary::microtoneBends, tst_liveequivalence::microtoneBendsLive.
Private: `~/MuseScore/ms3fork/private/fast-microtones/` (RESULT.md, the variant renders' tools gen3.py / ana.py).
Follow-up (branch `microtone-next`, 2026-10-22, build #465): `microtoneBends` 3 = 1 + plain fingered transitions early by
their glide's middle less plainShift (Violins 1 70 ms, Celli 61, Violas 0), for the owner to compare by ear against 1;
and the audit's predicted.tsv cents from the key sent (the heard check had read respelled notes a semitone off).
Private: `~/MuseScore/ms3fork/private/microtone-next/RESULT.md` (gen4.py / ana4.py: bend variants on kthost).

- **Measured scans** (two takes each, quarter tones in tune within 30 c of their heard time): Violas bars 5-8 8-10 % as now,
  35-40 % with 1; the best leave scanned (90 ms, cut at the next note-on) 43-44 %; bowed (velocity 100) 30-35 %; detached
  tuned attacks 25 %; RUNS (CC119 120) and a bend at the note-on worse. Violins 1 10 -> 31 %, Celli 6 -> 19-22 %.
- **Export check** (run 462 on the VM against #458, heard check renders, two takes): Violas bars 5-14 quarter tones in
  tune 15-16 -> 27 %, bar 7's wrong-way hook 40-44 % -> 0 % of the first 136 ms (2 alone: 49 %); Violins 1 11-13 -> 28-29 %,
  Celli 5-6 -> 14 %, but their 12-TET notes after a quarter tone 39-40 -> 29-30 % / 28-29 -> 19-21 % (the quarter tone's
  glide runs on into them; 2 the same): a next step would be the glide back to 12-TET cut at the next note-on.
- **Left**: SSO's fingered transition outlasts a 136 ms note, so the pitch still glides through each sixteenth; nothing in
  MuseScore's bend makes it settle within tens of ms. The owner decides by ear (violas_bend_A / _B clips); the next
  options are a technique choice (owner's), not a bend.
- The fingered glide table was measured isolated (`legato_glide/gen.py --velocity=64`, `glide.py --summarise --moving`:
  medians of the glides that move, a leave or arrival at the window's start is a clamp, not a time).

## Ample pipeline (branch `ample-pipeline`, 2026-10-21, from integrate-1017 95335ed024; not merged)

For the four Ample guitars the owner buys later (AGSJ, AG12, AGL, AGT: no trials): **docs/AMPLE_PIPELINE.md** is the
step-by-step and checklist; AGM 4.1.0 (the VM's trial, ending ~2026-10-25) was the regression case. Private results:
`~/MuseScore/ms3fork/private/ample-pipeline/RESULT.md`.

- **Tools**: `tools/soundlibraries/amphost/` (the VST 3 measuring host, msvc-wine `build.sh`), `ample_products.py`
  (per-product facts; every unmeasured number a `TODO(<experiment>)`), `ample_AGM.json` (AGM's measured numbers, each
  with its source), `gen_ample.py --product <code> [--state] [--allow-todo]` (AGM's map regenerated byte-identical before
  the strumLead change), `tools/ample/measure_ample.py` (VM experiments + `merge`), `tools/ample/check.py` (MuseScore's own
  renders through the real plug-in: determinism, routes = sets, capo, strum brightness, state lanes, soft strums, volume
  lane, Create + 2 x Update Live Set, AGM_info.xml hash; PASS / FAIL report), `check_scores.py`, `ample_state.py`,
  `amplevm.py` (VM lock protocol, .ps1 by scp: PowerShell's stdin mode drops multi-line blocks).
- **Strums were dark because of timing, not the demo**: the strummer reads its set-up (strum mode, Detect, the chord
  keys) only from an earlier process block than the strum key; in the same block the strum is dark and partial, and
  quiet at exact multiples of 7.5 s of render time. #458's own render shows it (check.py: 639 Hz). Fix: map `strumLead`
  50 (`Guitar::strumLeadMs`, guitarplayback.cpp `setupTick`: the set-up 50 ms before the strum key, not before the route's
  previous note-on; a previous strum's release moves to the set-up; at the score's start the set-up stays at 0
  (`setLibraryDelayed`) and `guitarStartLeadMs` plays the rest later, as for the capo). Test `ampleStrumLead`; the older
  guitar tests run with strumLead 0 (GUITAR_SETUP). **Known limit**: a strum on a later chunk's first tick (every 10
  bars) keeps its set-up in the strum key's tick (the chunk before may release a strum there); Live's clips have no start
  lead. StrumLevel re-measured with the lead for all three sets (Pick measured, no longer Finger's): the numbers moved
  (Finger 120: 50 -> 76, Strum 120: 69 -> 67); `[guitar] strums` is 0 everywhere, so nothing the owner hears by default
  changed.
- **Hosted background load**: `SoundLibraryHost` waits for Ample's own loader after setState (`setStateWaiting`, one load
  at a time per `backgroundLoad` map; log line "(hosted)"), test `tst_liveequivalence::backgroundLoadHosted`. Even levels'
  background measurer still doesn't wait (SSO only).
- **Measured, not changed**: CC11 reaches Ample only through a MIDI-learn entry and then overwrites CC7 (fold per-note
  CC11 levels into M-Master Vol: OPEN_ITEMS 10); state ranges (`stateclamp`: two out-of-range values crash AGM);
  load crashes 0 / 60 in amphost (network on / blocked); in MuseScore 1 / 14 (#463) and 2 / 15 (#458) with the network,
  9 / 22 with MuseScore's outbound traffic blocked: network-dependent, the licence / demo check likely (OPEN_ITEMS 12).
- **Windows build #463** (https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/39716570373), installed on the VM
  in `C:\claude\b463\ms`: `check.py` 25 PASS / 0 FAIL, strum bright 1335 Hz (#458: 639), the renders without strums
  bit-identical to #458's. **Next**: the owner hears the strums (`[guitar] strums` 1); the bought guitars by
  docs/AMPLE_PIPELINE.md.

## SSO script fixes 2 (branch `sso-script-fixes2`, 2026-10-21, from integrate-1017 0a9319a9b7; not merged)

From reading SSO's patch scripts (docs/SSO_SCRIPT_KNOWLEDGE.md). Measured on the Windows test VM with kthost (Kontakt 8 +
SSO, copies of MuseScore's setups; private `~/MuseScore/ms3fork/private/sso-script-fixes2/RESULT.md`), not in MuseScore:
- **`[notes] transportReset`** (0 everywhere, proposed 1 in Recommended): "Reset on transport" on in the state of the six
  map patches that save it off. Three passes 0.5 s apart: -3 .. -6 dB as shipped or with fixedTransitions alone; with both
  -95 .. -100 dB (pass 1 differs in its first 3 s only: the previous pass's tail). Passes 2.5 s apart reset by themselves.
- **`[levels] hallGuard`** (0 everywhere, proposed 1): the Hall trigger measured (a one-step CC1 drop of 14 on a held
  note adds a release 14-25 dB under it; split 1 ms apart: none; two values at one sample: as one step). The audit's HALL
  finds 3 in `hall.musicxml`, none in Whence, the passages, Mixed techniques, Strings.
- **avoidCC** on the two Total Performance brass patches (CC112 / CC113 never sent; only a `cc112` lane could); the
  audit's AVOIDED CC.
- **Shared settings** check (`Share__Settings.nka`, preflight + status bar). VM: nothing shared. Owner's PC: unknown.
- **Tightness** measured: 1 = 10-20 ms earlier on spiccato / pizzicato / col legno / Violins 1 Short CS (the script
  reading had the direction reversed), nothing built.
- `MS_VERIFY_ROUTE_PASSES` (a verify-routes aid) untried on the VM. Open: hear transportReset / hallGuard in MuseScore;
  Update Live Set and *Live plays the score* don't carry transportReset (as fixedTransitions).

## Copies by the free memory, deterministic (branch `voice-determinism`, 2026-10-21, from integrate-1017 0ab003951f; not merged)

Symptom (private/solo-clicks, build 458): the heard check's export of one Whence copy, byte-identical input, put Violins 1
- Performance's 92 notes on 1, 2 or 4 routes between runs (events.tsv: 92 | 79/13 | 63/18/10/1, the Linux dump
63/18/10/1). Not the voices (Violins 1 has no chords there: its four copies are tuning copies of the line) and not the
renderer's order: the copies' maximum, `SoundLib::memoryMaxLanes` = 1 + free memory / (245 MB x the parts needing copies,
5 in Whence), read once a run, and the Windows VM runs on **Hyper-V dynamic memory**: 16 GB installed
(GetPhysicallyInstalledSystemMemory), 4.9 GB visible and 1.9 GB free when idle (2026-10-21) -> max 2 copies; a first run
after an idle spell 1 (w0, 92 on one route), later ones, after Kontakt had made Windows grow, 4. The Linux dev machine
is on dynamic memory too (the once-in-13 Violas dump of VERIFY.md › Heard check). Fix: `SoundLib::memoryForCopies` counts
the installed memory not yet visible beyond a 2 GB allowance for hardware reservations (Windows; Linux has no installed
size for an unprivileged process: MemAvailable as before), so the VM counts ~11 GB, as the owner's rule meant ("free
memory ÷ the measured cost per copy": what the copies can have); `MS_FREE_MEMORY_MB` pins it, and the heard check pins
both its dump and its export at 16384 (`HC_FREE_MEMORY_MB`). Test `tst_soundlibrary::voiceLanesMemory` (the VM idle
against plenty free: the same routes and channels; fails before: 1 route and every note on it). On a machine truly short of
memory the copies still dropped (the rule then); since `all-copies` (above) they never do. Linux only (test synth);
Windows needs a build.

## Every copy the music needs (branch `all-copies`, 2026-10-21, from integrate-1017 c3ef2da874; not merged)

The owner's decision (2026-10-21, "2": always load every copy the music needs and warn when memory is low) replaces his
2026-10-03 rule that the free memory caps the tuning copies. `SoundLib::lanes` has no maximum unless `maxLanes` is set
(map `<Tuning maxLanes>`, `[tuning] maxLanes`, the score's `soundLibraryLanes` `max=`); `memoryMaxLanes` and
`partsNeedingCopies` are gone. The memory only decides a warning: `SoundLib::copiesMemory(routeCopies(routes), loaded)`,
245 MB a copy (`LANE_COPY_BYTES`; the fork keeps no per-patch load size, OPEN_ITEMS 28) against `freeMemoryBytes()`
(now read at each check; `memoryForCopies` on Windows, MemAvailable on Linux, `MS_FREE_MEMORY_MB` pins it). Hosted
(`SoundLibraryHost::syncSome`): a non-modal message box plus a line in load times.log, once per score open and per change
of its copies (an edit adding copies: the added ones against what is free then). Command-line renders and audio export
(`SoundLibraryExport::loadInstances`): the render preflight's `copies` entry per score (lines, JSON, a WARNING when short:
the verify report's preflight) and a WARNING line on stderr and in load times.log. Never fewer copies, never a wait.
Mixer › Playback adjustments shows an unset `tuning/maxLanes` as "as the music needs" (before, the computed 0 showed as 1).
Live / MIDI out unchanged: their fixed copies never took the cap; a bending patch's lanes there took it, and are the same
on a machine with plenty free. The heard check's `HC_FREE_MEMORY_MB` pin is unnecessary for builds with this (kept for
older builds). Linux only (test synth); Windows needs a build; the GUI box untried with Kontakt / SSO.

## Plain timing (branch `plain-timing`, 2026-10-21, from integrate-1017 15820b7b46; not merged)

`[slurs] plainTiming` (0 by default and in both presets; the owner decides by ear): with plainPerformance each plain note
starts early by its own model instead of the patch's one plainShift: an attack by the map's new `plainAttack` (Mixed
techniques' measured plain attacks, 19.5-38.7 ms), a transition by its modelled arrival (transitionsEarly's mode, at least 1);
the Basses' unsampled con sord. / espressivo stay on the Performance patch. Onset rule kept at -15 dB under the peak (Vos &
Rasch 1981; Gordon 1987 and Villing 2010 point lower; -6 dB unsupported at normal levels): docs/PLAYBACK_SETTINGS.md › Plain
timing. Windows build #458 (https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/39682778298), installed in
`C:\claude\bpt\ms` on the VM.

- **Heard check** (VM, build 458, export, one take each; heard - written medians): Mixed techniques: attacks -74 .. -21 ->
  -7 .. +3 ms, transitions +86 .. +203 -> -42 .. +42 (Violins 1 +1, Violins 2 -17, Violas +4, Celli +42, Basses -42), quick
  All techniques notes unchanged (-16 .. +2). Whence: attacks -62 .. -9 -> -4 .. +27, transitions -3 .. +182 -> -52 .. +18;
  ARRIVAL 35 / 26 -> 36 / 19, RHYTHM slurs 1 / 0 -> 3 / 1, runs 9 / 6 -> 7 / 3. Audit (Recommended + plainTiming 1): Whence
  CAPPED 1 info, ARRIVAL 0 fail / 2 warn (the Celli's 2nd note, as with plainPerformance 0); Mixed as before.
- **Open**: (1) the transition model (bendGlide's arrival) misses Celli and Basses by 40-50 ms with opposite signs in the
  two scores, and the Violas' slurs after a held first note in Whence bars 9 / 13 arrive ~100 ms sooner than modelled: a
  model of transitions after an attack, by note length, needs more data (Heard passages); (2) the accented attack's own onset
  is unmeasured (plainAttack used); (3) **sul tasto / flautando levels**: 16-54 dB under the Performance notes at the same
  mf (Mixed techniques), no real-player data found for the offset to the Long, so nothing changed; (4) Live: the plain set's
  Controllers clip starts at 0, so the score's first early note's CC1 comes up to its start lead late in Live (LIVE.md).
- Clips for the owner (not published): `~/MuseScore/ms3fork/ms37/.claude/worktrees/integrate-1017/.mixedtech/clips/`
  `*_plainTiming1.mp3` (A, B: same cut and gain as the existing pairs) and `whence_9-31s_plainTiming0/1.mp3` (bars 5-14);
  times in `clips_plainTiming_notes.json`. Private report: `~/MuseScore/ms3fork/private/plain-timing/RESULT.md`.

## Mixed techniques (branch `mixed-techniques`, 2026-10-21, from integrate-1017 3d783a6ac7; not merged)

The owner's question (2026-10-21): with `[slurs] plainPerformance` a part switching between its Performance patch and
another technique on All techniques, do they play in time and at one level? Test score `share/verifyplayback/Mixed
techniques.musicxml` (generator `tools/playbackverify/make_mixed_techniques.py`, CC0). Heard check on the VM (build 457,
export, Recommended, two takes: attacks identical, transitions within ~10 ms) and the audit. Nothing changed in the code.

- **Routing** (audit dump): every technique reaches its articulation; the Basses' "con sord." and "espress." have no
  sample (no CS, no Rachm.), fall back to All techniques Long / Spiccato and so leave the Performance patch
  (`choosePlain` refuses any modifier, even one the choice dropped). Audit: OVERLAP 0, ARRIVAL 2 fail (Violins 2 bar 38:
  Super Sul Tasto's 767 ms early start CAPPED by 211 ms after a 417 ms note), CAPPED 3, UNMEASURED 69 info (every All
  techniques onset is an isolated-note measurement).
- **Timing** (heard - written, medians, -15 dB rule): quick All techniques notes -16 … +4 ms; Performance fresh attacks
  Violins 1 -46, Violins 2 -37, Violas -74, Celli -41, Basses -21; Performance transitions +88 … +202 (unslurred touching
  notes +110 … +223, slurred +49 … +188; 60 bpm +150 … +208, 144 bpm +100 … +131). So a pizz. / col legno / tremolo on a beat
  sounds ~30 ms after another section's Performance attack and ~150 ms before its transition. Cause: one constant
  `plainShift` per patch (65.8 / 66.0 / 111.0 / 80.2 / 59.1 ms, the all-notes median measured on "Heard passages" in
  kthost) can't place attacks and transitions both; All techniques notes start early each by their own onset. At -6 dB the
  attack gap is gone (both +23 … +61), so the attack part depends on the onset rule (open, owner); the transitions' isn't.
- **Slow techniques**: Long Sul Tasto / Super Sul Tasto, Flautando, Long Harmonics start 380-770 ms early and are 16-54 dB
  softer than the Performance notes at the same mf (dynamics.json: Super Sul Tasto ~50 dB under Long at CC1 80): heard as
  a faint swell starting a beat or two early. Levels of the others against the Performance attacks: pizz. -8 … -13 dB,
  col legno -8 … -19 (Basses +3.5), Bartók +1 … +12, tremolo -2 … -7, trem. sul pont. -6 … -15, con sord. -4 … -6, espressivo
  -2 … -6.
- **Proposals** (none built; each a setting, off in Library default, for the owner to hear): (1) plain transitions early
  by their modelled arrival less plainShift (the `transitionsEarly` model; ~100-150 ms here); (2) an attacks-only shift
  for fresh Performance attacks (20 / 29 / 37 / 39 / 38 ms by these export readings at -15 dB, 0 by -6 dB); (3) level
  calibration of CC1 techniques (sul tasto, flautando, harmonics) to the Long, less a real-data offset (none found yet:
  an assumption); (4) Basses' unsampled modifiers stay plain Performance. Private report and clips:
  `~/MuseScore/ms3fork/private/mixed-techniques/RESULT.md`.

## Update Live Set on Ample parts (branch `liveset-hosted-readback`, 2026-10-21, from integrate-1017 56bbe38c6e; not merged)

From the Ample check of build 456 (private: `~/MuseScore/ms3fork/private/ample-check456/RESULT.md` › Findings 2; report
`~/MuseScore/ms3fork/private/liveset-readback/RESULT.md`).

- **A second lane on read-back**: the plain set plays the volume lane (CC7) as Ample's M-Master Vol automation, strum
  levels multiplied in; the read-back took it as a lane `m-master-vol` next to `volume`. Now such "derived" envelopes
  (a `hosted=` controller's parameter, the guitar pass's capo) are recorded as written (`liveTracks` › written ›
  `derived`) and an unchanged one comes back as nothing. Edited in Live: an envelope that was the lane's plain values
  (no strums) is an edit of the volume lane (merge rules; `basePoints` in `Automation::merge`'s classify), else reported
  and left in the set by Update Live Set until the score changes it. LIVE.md › The plain set › Read back › Derived
  parameters.
- **Every update modified the score**: Update Live Set stripped the lanes' Live marks as an undo step before planning,
  recordWritten put them back: tags equal, undo stack dirty, the command line saved. Now `planLiveSet` sets the marks
  aside in place for plain sets (`LiveMarksOff`), which also fixes Create Live Set made again leaving lanes Live had
  marked out of the new set.
- **Open**: the first update after Create Live Set still records the linked set (metaTag `liveSet`), so it modifies the
  score once; not run in real Live or with AGM (test synth as AGM, `tst_liveequivalence::hostedReadBack`).

## Lane jumps and Ample's background load (branch `lane-steps`, 2026-10-20, from integrate-1017 71da2b227b; not merged)

From the Ample check of build 455 (private: `~/MuseScore/ms3fork/private/ample-check455/RESULT.md`, FIX4.md there).

- **Lane jumps** (every library): two points at one tick (a jump; Live writes and reads one so, `LiveSet::lanes`) played
  the first value: `Lane::events` skipped the second point, while `valueAt` (and so a chunk starting after it) gave the
  second. Now `events` keeps one value a tick, the later point's (the renderer inserts each at `lower_bound`, so two
  would have played the earlier one); a ramp into the jump ends at its first value. `flatValue` (a jump at tick 0) and
  `PartControllers::stateValue` (a jump at the lane's first point) take the later value too. Everything else (hosted
  CC / parameters, Live's clips, the plain set, the route set, Update Live Set) follows from the rendered events; the
  editor and `valueAt` already treated the later point as the one that holds. Tests `tst_soundlibrary::automationJumps`,
  `tst_liveequivalence::laneJumpsLive` (written as two breakpoints at one time, read back, played the same). Whence has
  no automation lanes: its dumps are unchanged.
- **Ample's background load**: Ample loads a state's sample set (Pick / Strum) on its own thread after setState
  (~0.6 s of one core, warm disk), and notes meanwhile play whatever is loaded. The verification started rendering at
  once, so the three-instance score rendered differently each run (VM, #455: the Finger route's hash differed in 4 of 4
  runs, kept or fresh `parameter values.json`; Pick and Strum the same in those 4). `parameter values.json` changes no
  hosted render (the events and the Pick / Strum WAVs identical, fresh or kept). Reproduced in amphost5 (three
  instances in one process): without a wait the Finger slot differed in 3 of 3 runs; with the wait 8 of 8 identical;
  one run without waiting between instances crashed AGM.vst3 (access violation) while a second instance was set up
  during the first's load. Fix: map `<Plugin backgroundLoad="60">` (gen_ample.py), `audio/vst3/backgroundload.*`,
  `SoundLibraryExport::loadInstances` / `loadInstance` wait after each instance (load times.log: "its background load
  done in … s"). **To confirm on the next Windows build**: agm2-on verified 3 times (`MS_VERIFY_ROUTES=1`): bit-identical
  route WAVs, load times.log with three "background load done" lines (Pick / Strum ~0.6 s, Finger ~0.2 s).
- **Open**: hosted playback (`SoundLibraryHost::sync`, parallel loads on worker threads) doesn't wait: Play right after a
  load may sound wrong for about a second, and parallel Ample loads might crash as amphost5 did once (not seen in
  MuseScore). The even-levels background measurer (SSO only) doesn't wait either.

## Chords on monophonic patches (branch `chord-voices`, 2026-10-20, from integrate-1017 09f6847772; not merged)

The owner: "of course it should play every note". Whence's Celli divisi (bars 9-16) lost a note in each of its
three-note chords: SSO's Performance patches play one legato line per slot, and two chord notes shared a slot at the same
instant (private/bars9-12/SILENT.md). Now a patch marked `<Instrument mono="1">` (the generator's `MONO`: the 42
Performance patches) spreads its notes over voices, each voice its own copies of the patch ("voice lanes", riding the
tuning-lane machinery: `SoundLib::lanes` › Voice lanes, `Lanes::voice`, `Route::voice`), so every chord note has a slot
of its own: hosted (slots of the part's one instance), command-line renders, Live's plain set (a "(voice n)" MIDI track
per copy in the multi), route set, clips, MIDI out. Voice 0 is the line; a chord after a chord continues voice by voice
in pitch order; a note goes on legato only from the note before on its own copy (rendermidi.cpp `otherLane`). The audit
has a new fail, CHORD. Details: docs/PLAYBACK_SETTINGS.md › Chords on monophonic patches.

Checked on Linux only (test synth, rendered events): Whence before / after (Recommended, `dumpEvents`, lanes pinned
max=8): only Celli - Performance from 19.67 s on changes, plus one copy (4 instead of 3); every other route identical;
audit all 0, CHORD 0. Untried with Kontakt / SSO and in Live: a Windows build should check that the four chords now
sound all three notes (the verify run of SILENT.md, Celli m15 / m16), and that Celli - Performance loads one slot more
(~1.5 s, ~220 MB).

Divisi level: done on branch `divisi-level` (below).

## Divisi level (branch `divisi-level`, 2026-10-20, from integrate-1017 e263421d2c; not merged)

The owner, 2026-10-20, "yes": a chord on a mono patch should add up to one section, as a real section divided into n
groups does. `[levels] divisi` (1 by default and in Recommended, 0 in Library default): while n voices sound together
(the note's part, its patch, as written at the note's start, the note itself included: `SoundLib::Lanes::sounding`)
every chord note plays 10 log10(n) dB softer by CC11 (2: -3.01, 3: -4.77 dB), voice 0 too; a note after the chord at
full level again; a note held while voices enter keeps the level of its own start (no CC11 move inside a note: at
worst +1.8 dB over one section while a voice enters under a held one). An assumption from player counts (equal,
incoherent groups), no measured divisi data. Code: `divisiDb`, `Lanes::sounding` (soundlibrary.cpp `lanes`),
`MidiRenderer::libDivisi` / `divisiLevel` (on `libLevels`' volumeDb, so `libraryNoteLevels` schedules it and it reaches
Live's clips, the plain set's CC11 lanes, MIDI out). The master trim reads the rendered CC11, so it sees it.
Details: docs/PLAYBACK_SETTINGS.md › Divisi level; report private/bars9-12/DIVISI.md.

Checked on Linux only (test synth, rendered events). Whence (Recommended, `dumpEvents` chunked and `MS_DUMP_LIVE`, lanes
pinned max=8, divisi 0 vs 1, same build): only Celli - Performance copies 1-3 change, only CC11, exactly the 20 chord
notes (four two-note chords -2.90 .. -3.02 dB, four three-note -4.72 .. -4.83, 7-bit rounding); everything else identical,
trim 0 dB both, audit all 0; divisi 0 = the chord-voices dump. Tests: tst_soundlibrary 132 / 0 / 4 (divisiLevel),
tst_liveequivalence 63 / 0 / 3 (divisiLive), playbackaudit 10 / 0 / 1, midi 97, the other fork tests pass.

Open: a fast slurred line (IOI < 350 ms) entering a chord would have its divisi step flattened by even levels' fit (it
measures the envelope with the step in it); none in Whence (its divisi are a bar per chord). Not heard; untried with
Kontakt / SSO and in Live.

## SSO legato fixes (branch `sso-legato-fixes`, 2026-10-20, from integrate-1017 e8a330c429; not merged)

Four tests of SSO's Performance legato on the Windows test VM (kthost, Kontakt 8 + SSO, `--wait 2`, two kthost side by
side: renders equal `--wait 60` / serial ones at -93 dB, the fresh-process floor). Violas and Violins 1 - Performance
(Celli, Basses for test 2): Whence lane 0 as plainPerformance plays it (~37 s) and a grid of 16 ten-note legato phrases
(IOI 120 / 200 / 350 / 600 ms; velocity 5 attack, 93 / 69 transitions). Measurement copies of MuseScore's setups only.
1. **CC102 "since note-on" ramp: no effect, nothing to fix.** The script's switch for it (a saved script value, read from
   the patch's script) turned off, CC102 held at 0, 40, 90 or 126 from the start or from 0.5 s, and the stock ramp all null
   against each other at -93 dB (Violas) / -70 (Violins 1), Whence and the 350 / 600 ms phrases. The ramp rebuilt from
   the note-ons and sent as MIDI changed the sound (-17 dB, +0.2 dB per note), but the same stream on an unused CC (20),
   or CC102 held at 64 re-sent every 5 ms, changed it alike: dense MIDI events, not CC102 (probably Kontakt's blocks split at each
   event; even levels' CC11 glides send one every 5 ms, so they may carry a +0.1..0.2 dB effect of their own: open).
   The swell inside slurs is not the CC102 ramp's.
2. **Random modulators: off makes plays repeatable (built: `[slurs] fixedTransitions`).** No script setting does it (its
   random-detune switch acts on modulators these patches don't have and is read back from the groups at load). In the
   state: the 100-130 external modulators from Kontakt's random generator (source type 10) at intensity 0. Two plays of
   Whence null at -73.5 dB (Violas), -70.4 (Violins 1), -96.6 (Celli), Basses -85.8 (its 90 transition-level modulators
   only), against -9.0 / -8.9 / -7.2 / -21.4 shipped. Per note against the mean of two shipped takes: median +0.00 / -0.06
   / +0.14 dB, SD 0.30 / 0.22 / 0.50 (the takes differ by SD 0.32 / 0.25 / 0.52); spectra within 0.07-0.30 dB on average;
   swell unchanged. Still random: bowed (85+) transitions at 120-200 ms (Across Strings sustain groups, no random
   modulator). Listening clips (private): Whence stock take 1 / 2 / fixed per section.
3. **Dropped transitions: confirmed (built: `[slurs] fastTransitions`).** A whole step 10 or 14 ms after a note at velocity
   64 is silent, 16 ms plays; at 65 / 70 / 84 10 ms after it plays as the 64 sent 20 ms after (spectrum within 0.3-1.0 dB,
   arrival the same), at 85 another family (DETACHED). Sent at 65 by the setting (velocities 20-64 only).
4. **The 500 ms transition release: confirmed.** In a release-family-only copy, Violas' old-key release voice is silent at
   holds 450 / 480 ms (-101..-129 dB) and sounds at 520 / 550 ms (-58.6 dB); Violins 1 has an old-key release at every
   hold (-59 dB, probably its CC119 release group) and +3.3 dB more above 500 ms. Nothing built for it.

Both settings: 0 by default and in both presets until the owner has heard them; docs/PLAYBACK_SETTINGS.md › Fixed
transitions / Fast transitions; LIVE.md (Create Live Set embeds the fixed state; the velocity is in the clips). Tests:
`tst_soundlibrary::kontaktRandomSources` (+ speedFromWritten's layers), `tst_liveequivalence::randomSetupState`,
`tst_playbackaudit::droppedFindings`. Untried in MuseScore with Kontakt (the state is the one measured by kthost).

## Ample guitars (branch `ample-map`, 2026-10-18, from integrate-1017 c0d2f7325c; not merged)

The owner, 2026-10-18: "I want to be able to control all the parameters directly in MuseScore. ALL of them." Phase 1
is Ample Guitar M (`AGM.vst3`, hosted directly, not Kontakt); the dump it is generated from stays outside the repo
(`~/MuseScore/ms3fork/private/ample-map/agm-info.txt`, amphost).

- **Generator**: `python3 tools/soundlibraries/gen_ample.py <dump> [out]` writes `share/soundlibraries/Ample Guitar M.xml`
  (never hand-edited); test `tools/soundlibraries/test_gen_ample.py` (in run_fork_tests.sh).
- **The map**: all 124 real plug-in parameters as `param` controllers by title (Bypass included; MIDI CC proxies left
  out), each with its patchDefault, and steps / display where the dump gives texts (62): a linear table thinned to its
  two ends, stepped when the texts are repeated integers. CC7 Volume, CC1 Vibrato, CC64 Hold Pedal. The sample set
  (`DBName` AGMFg / AGMPk / AGMSt) is a state controller. Keyswitches Sustain 24, Natural Harmonic 25 (keyShift -12),
  Palm Mute 26, and Slide In / Out, Legato Slide, Hammer-On / Pull-Off, Slide Guitar 27-30 (reference only: no notation
  picks them yet). Dynamics by velocity only, maxVelocity 126 (127 plays Pop). Ids guitar-steel,
  guitar-steel-treble-clef, guitar-steel-tablature. Ample's other keys are a comment in the map. Pitch bend is the
  M-Pitch Bend parameter lane, not MIDI pitch bend (that would collide with the tuning bends).
- **Map format** (README, parser, tests): `<Controller steps display>` (the lane editor shows the plug-in's text,
  `SoundLib::controllerText`), `<Controller state values>` (a load-time setting kept only in the plug-in's state),
  `<Articulation keyShift>`.
- **State settings**: `audio/vst3/pluginstate.*` sets `<Name Value>` / `<Parameter Name Value>` in the state's XML and
  resizes the JUCE VstW / CcnK / FBCh / VC2! chunk (layout checked against AGM's real state sizes). SoundLibraryHost
  carries the values per part in the slot's values (a changed sample set loads the part again) and through
  setupState / loadSetup (export, Even levels, Create Live Set). Without a setup file the base is the plug-in's
  `defaultState`. A state-setting lane's value at the score's start is what counts. Test synth: `MSTESTSYNTH_PARAMS=<file>`
  adds parameters by title.
- 208 parameters exist only in AGM's state (Master Tune, reverb ...): settable by the mechanism, ranges unknown, so no
  lanes yet. **Done on ample-fixes2 (2026-10-20)**, below.

**State controllers** (ample-fixes2, 2026-10-20; measured in `~/MuseScore/ms3fork/private/ample-state/RESULT.md`, report
`CTL.md` there): every setting AGM keeps only in its saved state is a lane.
- `gen_ample.py` `state_settings()`: 204 `<Controller state="Parameter:…">` (all 208 state-only `<Parameter>`s but
  Master Tune: setState stores it, the sound ignores it until moved in the GUI; Num Host Params; the transport toggles
  S-SEQ Play Tog / T-Play Toggle). Menus as `values` + display texts (Cycle Separate / Round Robin / Random, FX toggles
  -1 / 1, chord roots C-B, …), numbers as `range` (+ `decimals`: Reverb Send -60..0 by 0.1). Measured: Vel Sensitivity,
  Max Voices, Auto Off Velocity, Cycle Mode / X3, Fret Sound, Capo Logic, Reverb IR / Size / Toggle / Send, SEQ slot,
  chord slot root, Tab loop / playback; the other ranges and menus are assumptions in the generator (a Windows check
  must confirm). The generator refuses a state setting that is a VST 3 parameter.
- Velocity layers: `Layer[ID=1..3]` (pluginstate.h keyed names `Name[Key=k]`, `Name[Key=k]@Attr`, the latter added when
  missing), 1-127, defaults 96 / 64 / 32.
- CC map: `<State name="Control[ID=n]@Parameter">` writes Ample's seven MIDI-learn defaults into every instance's state
  (`Library::fixedState`, `PartControllers::loadStateValues`, through `SoundLibraryHost::stateValuesFor`: hosted, export,
  Create Live Set, live equivalence); MIDI out still needs the CC7 entry. (2026-10-20: Live's clips do NOT carry CC7.
  Resolved 2026-10-20, third run: hosted and in Live the CC7 plays as M-Master Vol, below.)
- **Volume by M-Master Vol** (third run, 2026-10-20; report `~/MuseScore/ms3fork/private/ample-state/MV.md`): map
  `<Controller id="volume" cc="7" hosted="m-master-vol">` (`Controller::hosted`, `SoundLib::hostedParameter`,
  gen_ample.py); `MidiRenderer::libraryHostedParameters` (after the duplicate-controller pass, plug-in output or Live's
  clips) turns every CC7 on an Ample route (volume lane, flat volume, softStrums 0's levels and restores) into an
  M-Master Vol ME_PARAMETER at the same tick, value CC / 127, dataB 7 (a MIDI file writes CC7 again). Measured (VM,
  amphost, blocks 512 / 64): gain = 2 x normalized, linear (display = gain, 0.01 steps), CC7 and the parameter
  bit-identical; either applies from the start of the host block holding it (never late) with ~4.4 ms one-pole
  smoothing (95 % in ~13 ms, block-independent), to everything sounding: no lead. Hosted sound unchanged (both go
  through `Vst3Plugin::addParam` with CC / 127). Seq::updateSynthesizerState now replays ME_PARAMETER events with
  controllers on a seek (the volume, and the capo FIX2 listed as open). Live: route set by the clips' lane (Link
  device), plain set as Ample track automation; M-Master Vol and M-Capo always in Live's Configure panel. Tests:
  ampleMasterVol, `tst_liveequivalence::liveMasterVol`, liveGuitarSoftRoll updated.
- **ample-fixes3** (2026-10-20, from integrate-1017 37e02f842f; findings `~/MuseScore/ms3fork/private/ample-check453/RESULT.md`,
  report FIX3.md there; Linux only, not yet on Windows):
  - The command-line renders (`--verify-playback`, `--live-equivalence`, `--even-levels`, the audio export without a
    sequencer: `SoundLibraryExport::loadInstances`) made each instance's state without its part, so state lanes (Vel
    Sensitivity, Max Voices, velocity layers, the sample set) never played there; `loadInstanceSetup` now takes the route's
    part on to `loadSetup` / `setupState`, as hosted playback and Create Live Set did. Also the live-equivalence fault
    `no-controllers`' state. The background measurer's `loadInstance` / `beginInstance` passed it already. Test:
    `tst_liveequivalence::liveStateSettings` (`SoundLibraryHost::loadedStateObserver`: the very states loaded, all parts,
    one part at a time, `loadInstance`).
  - A volume-lane point under a softStrums-0 strum replaced the strum's level (a ppp strum on a ramp played at full
    volume). The guitar pass now multiplies: every CC7 of the part's own on the route from the strum to its restore plays
    lane x level / 64 (Ample's gain is linear in the value), the exact value in the event's tuning (the CC rounded for
    MIDI out; `libraryHostedParameters` takes the exact one when it rounds to the CC; the duplicate-controller pass keeps
    two CCs of one value with different exact values); the restore is the lane's value by then (before: the value at the
    strum's start, which also undid a lane step between). The strum's own level is exact too (it was rounded to a CC:
    up to 0.5 CC unit different now). Also the CC7 at the strum's own tick now counts as the one in force. Tests:
    ampleMasterVol (the product per lane tick under both bar-2 strums, nothing above the ppp level, 80 in force at bar 3),
    `tst_liveequivalence::liveMasterVol` (the plain set's automation under the ppp strum). Both new checks fail on the
    old code (run).
- Loads: the slot's values key (`PartControllers::valuesKey`) is compared once per slot at the sync: many changed lanes =
  one load, an unchanged written text = none (ampleStateLoads).
- `<PluginSettings>`: as an Ample state is made, `Documents/Ample Sound/AGM/Configs/AGM_info.xml` is read (read only;
  `MS_PLUGIN_SETTINGS_DIR` for tests); midiGuitarMode true/1 logs to load times.log + qWarning and shows a status-bar
  message once per session.
- Live (second run, 2026-10-20): Create Live Set's route and plain sets now embed each part's state values (the start's
  value of each lane, as hosted playback loads them: `setupState` with `t.partRef`, a state per patch and values key;
  `tst_liveequivalence::liveStateSettings`); before, none reached Live and the (Pick) / (Strum) patches loaded Finger
  there. A state lane can't be automated in Live (state), nor does hosted playback follow a change while playing: both
  play the lane's first value (LIVE.md › What still differs). Update Live Set doesn't rewrite the state (only on a
  tuning change): create the set again after a state lane changes.
- `[guitar] softStrums` 2 (second run, 2026-10-20; the owner compares 0 / 1 / 2 by ear): a soft strum (velocity <= 30) as
  a roll of the six single-string keys D3-B3 (map `rollKeys`) in strum mode with Detect and the chord keys held, at the
  chord's velocity held within 20-40 and spaced 10 ms at v20 .. 15 ms at v40 (map `softRoll="20:10 40:15"`: meas3's level
  pairs, the roll as loud as the CC7-lowered recorded strum; linear between = assumption), no CC7 (a lowered one restored
  before it); a muted strum stays the strummer. Tests: ampleSoftStrums (value 2; value 1 unchanged and without CC7),
  guitarSettings (map parse, mapping), `tst_liveequivalence::liveGuitarSoftRoll` (the clip's notes = MuseScore's, no CC7;
  value 0's levels: since the third run M-Master Vol in both). Report: `~/MuseScore/ms3fork/private/ample-state/SS.md` Linux: tst_soundlibrary 127 / 0 / 4, tst_liveequivalence 54 / 0 / 3, tst_midi
  97, run_fork_tests.sh all passed, check_windows_names none.
- Tests (Linux, overlay ovl-fixes2): tst_soundlibrary 127 / 0 / 4 (+ ampleStateControllers, ampleStateLoads,
  ampleStateWritten, ampleSettingsWarning; pluginStateValues' keyed names; ampleMap's counts); run_fork_tests.sh all
  passed (liveequivalence 52 / 0 / 3, midi 97); test_gen_ample 4; check_windows_names none. Not tried: the real AGM.

**Tests** (Linux, test synth, overlay build `~/MuseScore/ms3fork/ovl-amplemap/am.sh`; 2026-10-18): all fork tests pass;
tst_soundlibrary 111 passed / 4 skipped (integrate-1017's 106 / 4 + ampleMap, ampleRender, controllerDisplay,
pluginStateValues, ampleParameters), tst_liveequivalence 52 / 3, the rest unchanged. One fix: ampleMap compared
S-Strum Time's text at 0.5, a rounding tie (500.5; the plug-in shows 500, `controllerText` 501); it now checks the
dump's own texts at the default (29) and 1/22 (46). Not tried: the real AGM (Linux has none), Windows, Live, by ear.
Untested assumptions: a diamond notehead in MusicXML import gives "harmonics"; fff renders at velocity 126.

**Phase 2** (branch `ample-phase2`, 2026-10-20, from integrate-1017 c16934d256; not merged): guitar notation plays on
Ample by one renderer pass, `MidiRenderer::libraryGuitar` (`libmscore/guitarplayback.cpp`, its header comment is the
design), run last in `finishLibraryEvents`; the library facts are the map's `<Guitar>` (`SoundLib::Guitar`, generated by
`gen_ample.py` GUITAR / STRUM_LEVELS / GUITAR_FX from the measurements). Notation → events:
- **Strums** (`[guitar] strums`, 0 by default / Library default, not in Recommended): a straight arpeggio (strum line,
  stretch <= 1) → CC7 (as loud as its notes at their velocity: meas3's roll-equal CC7 per sample set; restored before the
  next non-strum note), D5 127, C#6 100, chord keys (lowest pitch class in C1-B1, the rest above), C4 / E4 (G4 / B4
  muted: every note a cross notehead) at 110; at its end the keys and C#6 v1. Arrow up (low to high) = Ample's down
  strum. Wavy / stretched arpeggios stay MuseScore's spread notes. `[guitar] softStrums`: velocity <= 30 → the notes
  rolled 15 ms apart.
- **Strings**: a tablature note's string (or a string number 1-6) → its key G0-C1 at 127 right before the note.
- **Capo** (staff text's capo): the M-Capo parameter (plug-in output; not when the part has an M-Capo lane), keys
  sounding - capo. AGM applies a change from its next process block only (ample-fixes2, measured 2026-10-20): a change
  goes `capoLead` (50 ms) before its first note, not before the route's previous note-on; at the chunk start whenever
  the staff has a capo text; a capo at the score's start plays the score 50 ms later (start lead), the parameter at 0.
  Still open: playing from a later tick sends no parameter in force (Seq sends only controllers), so the instance keeps
  the capo it last had; Live's clips get the parameter at 0 together with the first note (no start lead there).
- **Harmonics**: natural as phase 1; artificial = a fretted note + a diamond note k frets above → only the harmonic, at
  stopped + partial (12/7/5/4/3 frets → +12/+19/+24/+28/+31) as a natural harmonic; pinch (`[guitar] pinchHarmonics`,
  off) = text "P.H.": C#0 + harmonic (fretted + 19, or the diamond), 2 ms later D0 + fretted v60 for 45 ms.
- **Slides**: scoop / plop → D#0 before; fall / doit → D#0 200 ms before the end; glissando line → the first note alone
  (MuseScore's steps dropped) held 20 ms into the second, E0 before it. **HO / PO**: legato slur over two single notes of
  different pitch, or text H / P → the first held 20 ms into the second, F0 (velocity 1) before it.
- **Palm mute text after "open"** (ample-fixes2): MS4 keeps a staff text "open" in force as Art::Open (`Ms4::ArtRef::technique`),
  which ended every later "muted"; `SoundLib::want()` now lets the text state decide for a technique-text "open" (newest
  of the two wins); only an "open" articulation on the note ends a mute there. MS4's own rendering unchanged.
- **FX**: staff text scratch / slap / muting / strum mute / down|up noise (1|2) / hit top (open|mute) / hit rim → its key.
- Each renderer note whose keyswitch an added one replaced gets it again (keyswitches latch, meas3).
- **Sample sets**: staff text "pick" / "strum" / "finger" (the whole text; `TextTechniques`: modifiers pick / strum)
  chooses the map's extra patch Ample Guitar M (Pick) / (Strum) (`with=`, its articulations carry the modifier, its own
  state controller `sample-set-pick` / `-strum` with `default` 64 / 127 picks DBName): a route and instance of its own,
  loaded only where used (SoundLib::routes' used patches); a strum's level takes its route's set.
- Left: Auto Legato's three values (which is Off) unmeasured, left at the plug-in's default; Pop (no measured
  rule); slide-speed by destination velocity (no measured curve: velocity stays the dynamic's); the state-only settings;
  Live with a non-Kontakt plug-in (LIVE.md › What still differs); CC11 does nothing on Ample; the other four guitars need
  their own dumps; a Windows build, then the owner listens and decides the three settings.
- Build note: the overlay `~/MuseScore/ms3fork/ovl-amplep2/am.sh` (lowers ovl-integrate1017/upper, build-integrate);
  ovl-amplemap/upper was deleted (disk). A fresh worktree checkout makes every source newer than the lower's objects:
  `git ls-files -z | xargs -0 touch -h -d 2026-10-01` and touch only the changed files, else ninja rebuilds everything.

## Speed from written (branch `cc119-control`, 2026-10-17)

The owner approved taking over the Performance patches' CC119 (2026-10-17). `[slurs] speedFromWritten` (0 by default
and in both presets; the owner flips Recommended after listening): the five string sections' Performance patches load
with the script's own CC119 writing off ($vfm12 = 0, applied by the host as a prepared state; the setup file keeps the
library's value) and MuseScore sends CC119 = 126 - floor(ms*127/1000) just before each transition note-on, ms = the
WRITTEN time from the first note of the previous note's tie chain. First notes and attacks get none (the script writes
none there either: the value before stays). Map fix: the Performance portamento velocity 10 also fell in FING's range
(10-...) on Violins 1/2, Violas, Celli; it is now 5 (Basses 10, FING from 20), from `sso_performance_layers.json`
(group data and saved script values). Live: carrier key 113 (device protocol 11), the plain set's technique clip
envelope at its notes' Track Delay.

**Null test** (Windows test VM, kthost, Kontakt 8 + SSO; Whence, plainPerformance 1, lane 0 of each section; ~37 s
renders, 2 passes each): A = library state ($vfm12 119, no MuseScore CC119), B = speedFromWritten 1, C = control
($vfm12 0, no CC119). Waveform nulls are dominated by pass-to-pass variation (A1-A2 whole file: Basses -25.1 dB,
Violins 1 -9.3, Violas -9.0, Celli -7.4; A vs B in the same range). Third-octave bands, 46 ms frames, mean / p95 |dB|:

| Section | A1-A2 (floor) | B1-B2 | A vs B (4 pairs) | A vs C |
|---|---|---|---|---|
| Basses | 0.12 / 0.71 | 0.18 / 1.15 | 0.10-0.17 / 0.58-0.94 | 0.34-0.35 / 2.1-2.2 |
| Violins 1 | 0.53 / 2.09 | 0.53 / 2.04 | 0.51-0.55 / 1.99-2.16 | 3.04-3.05 / 8.5 |
| Violas | 0.61 / 2.56 | 0.62 / 2.62 | 0.61-0.63 / 2.56-2.80 | 3.06-3.07 / 10.8-10.9 |
| Celli | 0.80 / 3.46 | 0.81 / 3.55 | 0.80-0.87 / 3.43-3.76 | 4.41-4.43 / 12.3 |

So where written = played, MuseScore's CC119 sounds as the script's own (within the floor), and $vfm12 = 0 is in
effect (the control differs by 3-4 dB mean on the upper strings). Violins 2 has no transitions in Whence.

Open: Live paths untried in real Live; the host's resave restore and reload when the setting changes untried with real
Kontakt; lanes other than 0 not rendered; a slur over a repeat jump sends none; other Performance patches (Sul, solo,
winds) not covered; Live's carrier value 1 plays as 0.
## Edit lag (branch `edit-lag`, 2026-10-17, from plain-performance 44114ac529 + lag-probe; merged into integrate-1017)

Goal: edits respond fast with SSO, sound unchanged (the owner's rule: speedups sound-identical or dropped).
- **Redundant**: the Inspector asked `NoteLane::onPerformancePatch` twice per command (selectionChanged, then endCmd:
  same undo state, same notes); an undo / redo back to a state seen seconds earlier rendered realRangeTops and the master
  trim again (one cached result per score). **Needed but not now**: the first Inspector question after an edit paid
  both whole-score renders (~200 ms of each VM edit), only needed for the next sound. **Main-thread stalls**: the
  measurer's instance loads (setState 1.8-3.2 s on the VM).
- **Changed** (CLAUDE.md › Edit lag): RecentResults (8 per score), onPerformancePatch kept by inputs, EditWarmup
  (the edit's renders one chunk per event-loop pass while stopped; the mixer timer and the Inspector's Performance
  rows wait; preview / Play / export finish the same job at once), measurer setState on a worker
  (`SoundLibraryExport::beginInstance`). `MS_EDIT_LAG_OFF=1` = the code before; probe phases `b-` / `a-` = off.
- **Proof** (Linux): `editLagIdentical` (21 steps, off vs on, Whence 44314 lines per step) identical; before/after
  `dumpEvents` (6 scores x Recommended / plainPerformance 1 x default / chunked / live) + audit dumps: 1213 files
  byte-identical; run_fork_tests all passed. Not proven with Kontakt: the measurer's threaded setState (the same bytes;
  Kontakt took threaded setState in the 2026-10-03 load-times run) and the mixer gains set after the warm-up (stopped).
- **Linux probe** (test synth, Whence, 5 rounds, interleaved off/on blocks b-,b,b-,b,a-,a,a-,a; host load 10-12 of 12
  cores from other builds): all-edit response p50 384 -> 34, 495 -> 40 (b), 418 -> 99, 668 -> 76 ms (a); worst
  event-loop lateness in an edit's window p50 409 -> 138, 538 -> 162, 448 -> 294, 702 -> 253 ms (a warm-up step is one
  10-measure chunk, ~80-200 ms here). Pitch edits and select still render at once (the click's preview needs the
  edit's renders): pitch-up 562 -> 366, 781 -> 413 ms.
- **Windows VM** (Kontakt 8 + SSO, Whence, Inspector open; combined build integrate-1017 @ 16d3dbdcf8, CI run
  https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/39034459356; probe `el1`, phases o,b-,b,b-,b,a-,a x 5
  rounds, stopped; quiet.lock held, no kthost, Debian 1-min load 0.02-0.29): all-edit **sync** p50 295 -> 19, 271 -> 19
  (even levels on), 244 -> 18 ms (off); **worst event-loop lateness** in an edit's window p50 313 -> 91, 277 -> 84,
  248 -> 72 ms; response to idle p50 341 -> 244, 293 -> 242, 266 -> 224 ms (Windows' dispatcher runs the warm-up's
  0 ms steps before it idles: range step p50 ~63, trim step ~52 ms). Per action (sync / worst lateness p50, b- -> b,
  2nd pair): undo 270/272 -> 17/55, inspector open 118/116 -> 16/15, staccato add 272/280 -> 17/83, lane point add
  219/214 -> 19/88, select 174/173 -> 118/113, pitch-up 383/388 -> 224/228 (the click still needs the edit's renders).
  Measurer instance loads: main-thread parts 61-101 ms (begin) and 44 ms (finish), against 1.8-3.2 s before (build 442).
  Left on the main thread with even levels: each EvenAutoMeasurer::onResult re-renders the master trim (p50 125 ms at
  opening). Exit 0xC000000D at the probe's _Exit (the known headless-Kontakt exit), no crash. Logs local only.
- **Settings reads (branch `trim-render`, 2026-10-17)**: the 125 ms master-trim re-render per Even-levels result was not
  the trim's own arithmetic (the event walk: 0.4-3 ms) but the whole-score render under it (and under every Even next-round
  render and `SoundLib::routes`): ~77 % of that time was `Playback::value` -> `scoreValues`, which split and parsed the
  score's whole `playbackSettings` tag, and looked the id up in ~100 definitions with a QString made of it, at every read
  (thousands per render). Now `definition` is an index (first duplicate wins, as the loop), `scoreValues` keeps the last
  tag's parse per thread (a pure function of the tag text), `hasOwnMetaTag` compares C strings. Same values by
  construction; `settingsReadIdentical` (old algorithm vs new, tags edited between reads) and `evenTrimTrace` (MS_TRIM_*:
  Whence + fakeMeasure, every job gain, trim, patch gain, even level and event at full precision) are byte-identical to
  integrate-1017 with Even levels on and off; `dumpEvents` Whence chunked (Recommended, evenLevels 1 and 0): ev/, gains.txt,
  instances.txt, trace.txt identical. Linux, interleaved base/new x4, 48 results: Session::result sum 6.8-9.5 s -> 0.9-1.1 s,
  the gains (master trim) the mixer timer asks after each result 2.4-3.6 s -> 0.23-0.29 s (one trim render ~200 -> ~20 ms).
- **Open**: the 0xC0000005 crash
  (rerun1 phase e): not ours on the evidence (below). Fixed (coordinator, 2026-10-17): the note preview's chunk cache
  ignored even levels written without an undo step (a click after such a write replayed the chunk rendered before it);
  `NotePreview::stateKey` now holds `EvenLevels::generation` (editLagIdentical checks every step's click against a fresh
  render; without the fix it fails at "even level written").
- Crash (rerun1, phase e playing, logs only): the "VirtualUnlock failed" lines (19108, errno 158 ERROR_NOT_LOCKED) came
  while the 8 playback instances were released at the switch to MS4 (01:22:32-57), 3 min 15 s before the exit; the
  process was then shrinking (ws 5006 -> 1739 MB, physical free >= 5432 MB throughout; commit not logged); no stdout in
  the last 3 min; no code path found that touches a released instance (setPlugin under the mutex, the audio thread's
  try_lock, the measurer joins its workers first). Most consistent with Kontakt's own fault after its purge; next time:
  WER LocalDumps on the VM, commit headroom in monitor.ps1, phase e alone. (The measurer's instances stay loaded in MS4
  mode: EvenAuto::active follows the current library; releasing them there would save memory, not proven related.)

## Plain Performance (branch `plain-performance`, 2026-10-17)

The owner chose "C" by ear (Whence Violas bars 8-13): plain SSO Performance, the whole part early by one constant.
2026-10-17, after the five-section listening round of Whence: he chose plain Performance on and the all-notes constants.
`[slurs] plainPerformance` is 1 by default and in Recommended, 0 in Library default (docs/PLAYBACK_SETTINGS.md › Plain
Performance). Shifts in `sso_plain_performance_shift.json` (all-notes medians, PLAINb, 2 passes, "Heard passages.musicxml"):
Violins 1 65.8, Violins 2 66.0, Violas 111.0, Celli 80.2, Basses 59.1 ms; no UNMEASURED Performance patch of the five
remains. Tested on Linux only (test synth, dumps); not in Kontakt or real Live.

**Plain levels** (branch `plain-levels` 86263cdd5a, merged into integrate-1017): `[levels] context` with plain Performance
takes per-kind grids measured on the Windows VM (map `contextLevelPlain` / `contextLevelStaccato` / `contextLevelSingle`,
`sso_performance_context.json` › `plain`; docs/PLAYBACK_SETTINGS.md › Level rule). **The owner chose the "after" levels
(2026-10-17).** The staccatos' ±3 dB per section in the calibration score: found (branch stac-levels, not merged): the
grid's metric P grows 1.1-1.4 dB per dB in SSO's quiet notes (its threshold term), so the quietest figures (staccatos)
got too much gain; the plain grid is now measured in P0 (no threshold, level-independent), calib staccato RMS error
1.36 -> 0.72 dB (P), Whence staccato - slurred error 1.6 -> 0.8 dB (docs/PLAYBACK_SETTINGS.md › Metric P0).

## Paused 2026-10-17: where to pick up

The owner paused the project to start a new session. `integrate-1012` (pushed) holds everything finished: crash fixes
(Kontakt unloaded at exit, jobs load one part's instances at a time, `--live-equivalence` too, WAV header always
valid; the GUI exit verified on the VM with build 444: 5 of 5 closes exit 0), the heard check, prepared states,
tuning slots, the Inspector rows fix. Not yet a Windows build for the owner; before one: the Whence audit and the
heard check (Before a build goes to the owner).

1. **First: Violas, Whence bars 9-12.** The owner, 2026-10-16: plain SSO Performance (every note on the Performance
   patch, no parameters tweaked) sounds better than the latest build; "plain SSO performance had the timing perfect
   relatively between the staccatos and the slurs. though it's consistent late". The likely cause: our staccatos go to
   All techniques and each technique is sent early by its own onset, so staccatos and slurs drift apart. Candidate:
   keep staccatos on the Performance patch (spiccato attack) and send the whole patch early by one constant (its
   median heard latency; two constants only if attacks and transitions differ by > 20 ms). The ablation (PLAIN,
   PLAIN-SHIFT, CURRENT, CURRENT-ON-PERF, one adjustment off at a time) is prepared on branch `qt-bends` (67f90e1a06:
   `abl.py`, `ablana.py`), NOT rendered yet: event files and tuned setups are on the VM in `C:\claude\qtbends\abl`
   (8 kthost processes), state in `~/MuseScore/ms3fork/private/violas-ablation-state.md`. PLAIN-SHIFT(-2) need PLAIN's
   measured latency first. From the dumps (bars 8-13): CC1 64 throughout; the slurs' first notes go out 236 ms early,
   the staccatos 19.9 ms (the drift the owner hears, likely); Performance plays 16.3 dB louder than All techniques
   (gain stages -3.64 / -19.96 dB). Then a listening page for the owner (the owner decides what is heard).
2. **Quarter tones in fast slurs** (`qt-bends`): Violas' bends work in isolation, something in Whence's context
   loses them (14 one-change variants ready, `whence_variants.py`); Celli: SSO's own Performance transitions don't
   finish below ~300 ms per note, in 12-TET too (no fix on our side; another way of playing fast celli slurs would
   be the owner's decision).
3. **Lag with SSO loaded** (branch `lag-probe` d13748d3e1, build 442): phases o, b, c, d and a-stopped measured (20
   rounds, undisturbed: the ablation never rendered): every edit's response > 100 ms, scrolling ~6 ms; a (evenLevels 0)
   slower than b, unexplained: re-run. Resume: `C:\claude\lagprobe\go.cmd <tag> a+e 20 stopped+playing` on a quiet VM;
   state in `~/MuseScore/ms3fork/private/lag-probe-state.md`. Even levels: whether to skip short slurs waits on those numbers.
4. **Own legato: paused** (the owner's ear: plain SSO already beats our build in bars 9-12, so what we add comes
   first). 12-TET only when it resumes. Diagnosis: six bugs fixable in sfizz (CC1 reviving faded layers, same-key
   note-off cutting the new note, leaps > ±7 unrecorded, ~0.9 s loops, releases after long holds only, sfizz 1.2.3
   hangs a `delay=` region on a 256-frame boundary; fast/runs sets unused; attacks start late); the splice (old note
   switched 80 ms before the shift, SSO 150-300 ms) needs SSO-like latency or a custom sampler (~40 % to reach SSO's
   joins; a 1-hour null test of two identical SSO passes would decide, ~65 % if it nulls). SSO's splice rules,
   read from the patch's script: `~/MuseScore/ms3fork/private/sso-splice-notes.md` (local only). Plan:
   `private/own-legato-plan.md`; branch `own-legato`.
## Lag probe (2026-10-16/17, branch `lag-probe`, from integrate-1012 1133362748; not merged)

`MS_LAG_PROBE=<log>` (off by default; libmscore/lagprobe.h, mscore/lagprobesession.h, tools/lagprobe/). Windows VM,
Kontakt 8 + SSO, build 442, Whence, Inspector open; logs local only (worktree `.lagprobe/res/`; state and details:
`~/MuseScore/ms3fork/private/lag-probe-state.md`).
- Every edit's response > 100 ms with SSO (stopped, 20 rounds, b: select ~210-270, pitch-up ~470-580, undo ~310-440,
  Inspector open ~150-180, lane point ~250-320 ms p50); scrolling 6-8 ms. MS4 mode (e): 45-116 ms. Causes: renders on
  the main thread per edit (Inspector's NoteLane::onPerformancePatch, masterTrim, realRangeTops, the click preview).
- **evenLevels 0 (a) is not slower than 1 (b)**: full1/rerun1 showed a slower because the host's CPU load moved
  (the Debian VM's builds: 1-min load ~3-5 during b, ~7-10 during a in rerun1); a does the same work minus the
  measurement (identical scope counts), and every scope, paint and layout included, was uniformly slower. Interleaved
  blocks (b,a,b,a,b,a x 5 rounds; `tools/lagprobe/compare_blocks.py`): all-edit medians b -> a 485 -> 452,
  383 -> 308, 682 -> 524 ms. Even levels' own cost: +7..+30 % per edit (its threads compete for 4 vCPUs), detect
  82-141 ms after each edit's debounce, measurer instance loads 1.8-3.2 s main-thread stalls.
- **Short slurs: don't skip them for lag.** Whence's Performance notes are all in 3-4-note slurs joined into long
  passages; skipping 2-4-note slurs would drop even levels from Whence, where it measurably helps (even_need
  a69ebef56a); and the lag without even levels is still 308-524 ms per edit. Fix the renders instead (cache by undo
  state, off the main thread; measurer instances loaded on a worker).
- Open: rerun1's phase e playing crashed (0xC0000005, after a Kontakt VirtualUnlock error); a crashed probe leaves the
  playback mode at MS4 in `C:\claude\lagprobe\cfg` (check `soundLibrary=` before a run).

## Load profile (2026-10-16, branch `load-profile`, from integrate-1012 41cdbbb311; not merged)

The owner, 2026-10-16: "is there any way at all we can make SSO load even faster"; rule the same day: "no optimizations
should ever change the sound" (an optimization is sound-identical, proven by a render null test against the floor of two
identical passes, or dropped). Measured on the Windows VM (Kontakt 8 + SSO) with build 439 (2ba8f9f7ac, run
https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/38850314270, `MS_SLOTS_PARAMETERS=1`) and ktload
(`tools/soundlibraries/ktload`: the very states MuseScore loads, one after the other, timed). **Every VM run today was
contended** (another session's Live 12.4.6 with 8 GB, one or two kthost renders): numbers 3-7x the 2026-10-15 warm run.

| Part of Whence's warm load (8 instances, 20 slots) | Measured | Notes |
|---|---|---|
| new instance (Kontakt's component) | 0.2-0.6 s each | Kontakt's |
| MuseScore's state: voice limit 512 + the copies' Tune slots | 0.5-1.7 s each, 8.9 s in all (Linux CPU); "setups read" 5.3 / 10.9 s for the voice limit alone (VM) | **now prepared once** (below) |
| setState (Kontakt's instrument load + script) | 1.9-19.5 s each (VM, contended); ktload CPU time 2.4-7.4 s each, the 3-slot multis ~2x a single patch | Kontakt's, single-threaded |
| settle (47 ms of audio) | 3-80 ms each | needed (parameters) |
| every parameter's info (4145) | 2-10 ms | negligible |
| samples after setState | ~0.5-1.1 GB allocated during each setState, then filled by Kontakt's own thread, processed or not | VM: over SMB from the owner's PC (C:\NI\…\Samples are symlinks to \\VOLENS\VMShare): ~190 reads/s of ~40 KB, 3-17 MB/s, still growing after 5 min; not the owner's local disk |

- **Implemented, on (load time only)**: `prepared` (soundlibraryhost.cpp): the states setupState (voice limit) and
  tunedState (the copies' slots) make are kept, keyed by the input's SHA-1 + what was done + PREPARED_VERSION, in memory
  and in `<setups folder>/prepared` (checked by SHA-1 when read). The plug-in gets the same bytes by construction
  (`tst_liveequivalence::preparedStates`); `MS_PREPARED_OFF=1` makes them at each load, to compare. The even levels'
  measurer loads its instances only after the playback's preload (it took turns with it on the main thread). Timing
  lines in `load times.log` ("<patch>: steps | …" per instance).
- **Proven sound-identical** (build 441 of `load-profile`, https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/38942606898,
  Windows VM, Kontakt 8 + SSO, 2026-10-16): Whence per part (`part_variants.py`, hostedTuning 1), `--verify-playback
  --verify-wav` with prepared states on / off (`MS_PREPARED_OFF=1`), each twice (on a, off a, on b, off b; on a wrote the
  cache, on b read it: the job copy's `prepared` folder), compared with `compare_renders.py` (`-`: no dump needed).
  Whole-part null, floors (on a - on b, off a - off b) | on - off (a, b):
  Violins 2 -144.6 / -144.6 | -144.6 / -144.6 dB; Basses -158.0 / -97.8 | -97.8 / -158.2 dB (the -97.8 is off a's own
  pass, its floor too); Violins 1 -7.5 / -7.3 | -7.0 / -6.9; Violas -8.3 / -8.0 | -8.5 / -8.7; Celli -7.2 / -6.9 | -6.5 /
  -7.7 dB (Performance patches: not deterministic). Per note, every part: level 0.00 dB / f0 0.00 cents / onset 0 ms /
  spectrum 0.00 dB on Violins 2 and Basses; on the Performance parts on - off within the floors' own maxima (level
  <= 3.9 dB against floors to 5.0, spectrum <= 6.0 against 6.2, f0 <= 10.8 cents against 14.1), and their notes bit-exact
  in both floors (14 / 28 / 18) null on - off at the floors' level (worst -62.7 / -100.0 / -140.0 dB against -62.6 /
  -101.7 / -140.0). Kept, on.
- **Load times** (same build, `--measure-load-times`, phase 2: all 8 instances one after the other; another session's
  kthost rendered during most runs, total CPU median 16-31 %, no Live): off 56.6 s (the build's first run) / 33.3 /
  22.8 s, setups read 11.6 / 7.4 / 5.3 s; on 21.4 s (the run that wrote the cache, setups read 3.6 s) / 17.9 / 17.7 /
  19.0 s, setups read 0.17 / 0.14 / 0.21 s. setState 17.0-18.2 s both ways once warm (off 3 / on 2-4): the gain is the
  setups read, 5-11 s -> 0.2 s. The owner's PC needs its own numbers.
- **Dropped** (could change the sound, or no gain): purging articulations the score doesn't play (no guarantee that a
  later edit, a play from anywhere, an export and the measurer find their samples loaded: Kontakt loads in the
  background with no "done" signal); a smaller preload (realtime dropouts possible; the owner's Kontakt overrides
  every instrument's preload with 30 kB anyway, the VM doesn't: `overrideInstDFD` 0); a Kontakt-resaved multi (loads
  no faster: ktload 7.6-23.7 s against 7.8-25.2 s, 4 + 4 alternating); worker threads (no gain, 2026-10-15); playing
  before late parts load (their notes would be lost; MuseScore can't see when Kontakt's samples are in). Note: Play
  doesn't wait for Kontakt's background sample loading today either (found, not changed).

**Tuning slots against varispeed** (`[tuning] hostedTuning` 1 / 0, the coordinator's check; build 439, Whence per part:
the other parts on MS4's sounds, each render twice, `compare_renders.py`; floors = two identical passes):
- Floors: the All techniques parts (Violins 2, Basses) render **bit for bit** (whole part -145 / -158 dB); a part with
  a Performance patch doesn't (Violins 1 -6.9, Violas -8.4, Celli -7.1 dB; per note up to ±2-6 dB level): SSO's
  Performance patches vary between runs after the transport restart.
- Slots vs varispeed: Violins 2 -11.5 dB, Basses -25.2 dB (whole part, both pairs alike); per note on the deterministic
  notes: pitch within 1.2 cents (Violins 2) / 1.7 (Basses) / 2.9 (Violas), level within 0.08 / 0.03 / 0.82 dB, 1/3-octave
  spectrum within 0.17 / 0.25 / 1.45 dB, onset within 2.7 / 0 / 10 ms (Violas' tuned notes). Basses' untuned notes
  identical (-148 dB), its tuned notes -23 dB (Kontakt's Tune and MuseScore's resampler differ in the waveform). Violins
  2's and Celli's untuned notes differ too (null +2 / -7 dB, level / spectrum the same): the copies' notes fall on other
  instances, so other round robins play. Parts with Performance patches: slots - varispeed at the floor (Violins 1 -4.6
  against -7.2; Violas -6.7 against -8.3; Celli -5.8 against -7.1).
- Pitch against the written tuning (YIN, median over the note): slots and varispeed alike, quarter-tone notes
  median -2.2 / -2.0 cents (Violins 2), -2.1 / -2.1 (Violins 1), -0.9 / -0.5 (Violas).
- The plain Live set plays Kontakt's Tune (KontaktSetup::withSlots / withTune: the same states as hosted slots;
  2026-10-10, kthost: a multi = a Kontakt per copy, null -111 dB), so slots are what Live plays by construction; no Live
  render was made today (Live held by another session; the VM's memory).

## Kontakt slots for tuning copies (2026-10-15, branch `tuning-slots`, from integrate-1012 41e1c46d46; merged with integrate-1012 in `integrate-ts`)

The owner, 2026-10-15: "is there any way at all we can make SSO load even faster" … "Whence takes absolutely forever to
load all the patches because the microtonal stuff". Hosted playback now tunes as the plain Live set does
(`[tuning] hostedTuning` 1, slots, the default; 0 varispeed as before; slots kept by the owner 2026-10-16; CLAUDE.md › Microtones,
docs/PLAYBACK_SETTINGS.md inventory, LIVE.md › What still differs): Live's routes (a fixed copy per tuning) and a
patch's copies of one part in one Kontakt, a slot per tuning by Tune on its own MIDI channel.

- Whence: 20 routes (Live's) against 19 varispeed copies; 8 instances against 19 (one per part and patch, as the plain
  set's multis). Phase 6 (new in `--measure-load-times`): Vibrato set on a 3-slot Violins 2 multi changed the same
  script values in all 3 slots, so a part's plug-in parameters share the multi (Violins 1 sets Vibrato; build 439,
  before that was known, kept its 5 copies apart: 11 instances).
- Before (VM, build 436 = 10c6641, varispeed; `--measure-load-times … Whence.mscz --measure-threads 2,4
  --measure-no-probe`, an extract job of another session running alongside): phase 2, one after the other, 19
  instances loaded in 42.9 s (setState 32.1 s), memory settled 178 s after the start, +7.7 GB freed at the end (the
  run's +12.2 GB counts the VM's paging); each patch alone 425-1017 MB, not the 245 of `LANE_COPY_BYTES`. Worker
  threads: Kontakt takes them (no failure) but slower: 2 threads 63.0 s, 4 threads 100.2 s, memory still growing at
  5 minutes. Keep `io/soundLibraryLoadThreads` 0.
- After (VM, build 439 = 2ba8f9f7ac, run https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/38850314270;
  same command). The first runs of each build differ by the disk cache (436 ran cold), so both were run again
  back to back, warm (436, then 439 sharing parameters, `MS_SLOTS_PARAMETERS=1` then, the default now):
  | | instances | phase 2 loaded | memory settled after start | freed at the end | 2 threads | 4 threads |
  |---|---|---|---|---|---|---|
  | 436 varispeed, cold | 19 | 42.9 s | 178 s | 7.70 GB | 63.0 s / >300 s | 100.2 s / >300 s |
  | 439 slots, 11 instances, warmer | 11 | 33.6 s | 121 s | 7.41 GB | 37.7 / 133 s | 35.4 / 130 s |
  | 436 varispeed, warm | 19 | 26.5 s | 90 s | 7.72 GB | 30.8 / 89 s | 25.0 / 73 s |
  | 439 slots, shared, warm | 8 | 21.5 s | 71 s | 7.27 GB | 22.9 / 69 s | 22.8 / 69 s |
  Warm: everything sounds ~20 s sooner (90 → 71 s), 0.45 GB less; the samples (one set per patch either way) dominate
  the memory. Worker threads gain nothing with slots (8 instances); keep `io/soundLibraryLoadThreads` 0.
- Tuning (VM, `--verify-playback --verify-wav` on two quarter-tone test scores of mine, pYIN): All techniques Violins by
  slots and Tune -2 … -4.5 cents from the written tuning (mean -3.4, the 0-cent notes the same -3), by varispeed (436)
  -1 … -5 (mean -3.2), note by note within 3 cents of each other; Violas Performance (bends) identical in both builds
  (max 8 cents); 0 verify findings in both. Slots kept by the owner 2026-10-16 ("yeah seems good"): they match the plain Live set.
- **With the gain stage** (merged with utility-gain in `integrate-ts`): a multi plays at its instance slot's Mixer, which
  carries the patch's gain stage (`TrackDelays::patchGain`, per part and patch, so every copy's slot gets the same value;
  the linked slots have no instance of their own and their values go unused): the gain applies once per instance, never
  per MIDI channel. The plain set's multi: one Kontakt track, one Utility; the copies' MIDI tracks none.
  `tst_liveequivalence::hostedSlotsGain` (test synth): three copies at +12 dB, each and all together +12.00 dB against
  unity, export and play, with the linked slots' Mixer muted / changed. Untried with Kontakt / SSO.
  Whence (quarter tones), Recommended, `integrate-ts` (`dumpEvents` chunked with the shipped calibration, instances.txt /
  gains.txt): 20 routes, 8 instances; trim 0.00 dB, loudest -2.71 dBFS at 15.45 s; slot gains per instance as on
  integrate-1012: Violins 1 (3 slots) -0.03, Violins 1 - Performance (2, bending) +16.29, Violins 2 (3) +1.12, Violas
  (3) -7.08, Violas - Performance (3, bending) +9.24, Celli (3) +2.56, Celli - Performance (1) +18.88, Basses (2) +3.43.
- Open: the even-levels background measurer keeps one plain instance per part and patch, by varispeed (the cheapest
  load; it measures envelopes);
  `LANE_COPY_BYTES` (245 MB, memoryMaxLanes) is from the owner's 2026-10-04 measurement and only bounds varispeed copies.

## Gain stage and master trim (2026-10-15, branch `utility-gain`, merged with integrate-1012 in `integrate-ug`)

The owner, 2026-10-15 ("sure", then "yeah sure let's go with utility"): no more lowering every patch by S = max(0, G - 6)
when one quiet patch needs more than Live's fader +6 dB. Each patch's levels (level rule, section balance, realRange
tops, mic compensation, even levels) now play on its own gain stage: MuseScore's slot gain (float, no cap) and, in the
plain set, a stock **Utility named "MuseScore gain"** after the Kontakt (±35 dB; a rest above +35 goes on the fader);
the fader stays at 0 dB (the Mixer's), the owner's. One **master trim** per score lowers everything only as much as the
loudest moment needs (power sum of the sounding notes' calibrated level + the map's `crest`, 14.5 dB, measured in SSO:
`sso_crest.json`); without calibration or crest it falls back to the old S'. Design and numbers:
docs/PLAYBACK_SETTINGS.md › Gain stage and master trim; Live: LIVE.md › The plain set › MuseScore gain.
No extra safety margin under 0 dBFS (the owner, 2026-10-16: "keep it as it is, we have other ways to make extra room
anyways"): Whence's loudest moment stays at about −2.71 dBFS.

- **Whence (quarter tones), Recommended, `integrate-ug`** (with integrate-1012's B′; `dumpEvents` chunked, gains.txt):
  trim 0.00 dB (loudest moment -2.71 dBFS at 15.45 s, 7 notes; the old S' was 12.88 dB), so every patch plays 12.88 dB
  louder than before: Violins 1 Utility -0.03 dB (fader was -12.91), Violins 1 - Performance +16.29 (+3.41), Violins 2
  +1.12 (-11.76), Violas -7.08 (-19.96), Violas - Performance +9.24 (-3.64), Celli +2.56 (-10.32), Celli - Performance
  +18.88 (+6.00), Basses +3.43 (-9.45); every fader 0.00. utility-gain alone (before B′): trim 0, loudest -2.65 dBFS, S'
  12.98, the Celli +2.66 / +18.98, the rest the same. Audit (OVERLAP 0, UNMEASURED 10, CAPPED 1, ARRIVAL 0 / 2 warn,
  LEVEL STEP 0, DROPPED 0, LEAP 0) unchanged.
- **Checked in real Live 12.4.6 on the VM** (build 433's Create Live Set + a Utility added by this branch's code on
  Linux, `tst_liveequivalence utilitySet`): the set opens with the Utility after Kontakt; its +6 dB plays +6.000 dB
  (export against the same set without it, median sample ratio); Live re-saves the Utility with the same XML (ids aside);
  the in-place Gain edit (Update Live Set's code) of Live's saved set to -10 dB opens and plays -10.000 dB.
  **Untried**: Update Live Set end to end and the hosted slot gain with Kontakt / SSO (need a Windows build).
- **For the owner**: playback is louder by the old S' wherever the trim is smaller (by design: the trim lowers only what
  the peak needs); the crest is the strings' (woodwinds, brass, percussion assumed alike); single soft odd takes peak up
  to 20.2 dB over their calibration level (median 9.1), above the 14.5 crest; the trim costs one more render of the score
  after an edit; MIDI out plays the gain by CC7 within 127 (receiver's CC7 law unmeasured, LIVE.md › What still
  differs); *Live plays the score*'s clips into the owner's own set carry no Utility (LIVE.md › What still differs).

## Ctrl+C / Ctrl+V in the Velocity and Join lanes (2026-10-15, branch `notelane-copypaste`, merged into integrate-1012)

The owner: "Ctrl C+V still doesn't work in the velocity and join automation lanes". Cause: `AutomationLanes::wantsKey`
refused C / X / V (and D, until the stems' Ctrl+D) when the focused lane was a note lane, so the ShortcutOverride was
not accepted and the score's own Ctrl+C / Ctrl+V took the key (copying the score's selection, if any).
The automation lanes' rules: Ctrl+C / X copy the selected points (with their times), Ctrl+V pastes at the mouse's time in
the lane under it (any parameter), else after the copied ones; the lane has the focus after a click, Esc drops it.
Note lanes (values per note): Ctrl+C keeps the selected stems' own values (Auto as Auto: velocity 0, join JOIN_AUTO) in note
order with their ticks relative to the first (`NoteLane::Clip`); Ctrl+V writes them in order onto the lane's stems
(Join: the joinable ones) from the first note at or after the mouse's time when the mouse is over that lane (any part),
else the first selected stem, else the stem after the copied ones; the paste is by order, the relative ticks are kept for
later. Fewer stems left: the rest dropped (status bar says "n of m pasted"). A Velocity clip over the Join lane (or the
other way): refused with a message. One undo step, the pasted notes selected, values in the metaTag `performanceTechniques`
as any edit. Ctrl+X stays the automation lanes' (the score's in a note lane). The score's own Ctrl+C / V act whenever no
lane has the focus. Tested: `tst_performancetechnique::noteLaneCopyPaste` (the model). The key routing and the lane
widget need a ScoreView: tried by hand only (see the commit message).

## Flat lanes in Live, knobs read back, shipped controller defaults (2026-10-15, branch `lanes-b-readback`, merged into integrate-1012)

The owner's three decisions on https://claude.ai/artifact/4hxBZ3x3yzQUxC94EkGJYD (2026-10-15):
- **Option B**: Create Live Set and Update Live Set write a flat plug-in-parameter lane as the Kontakt panel parameter's
  plain value (a Configure slot with its Manual value, no envelope); a changing lane stays automation; Update removes an
  envelope whose lane became flat and also writes the constants into the set's own Kontakt state (its other settings
  kept), since it is unknown whether Live applies a Manual value over the state at load. A flat MIDI-CC lane has no Live
  parameter: still a one-value envelope in the Controllers clip.
- **Knobs read back** ("yea sure"): a panel parameter without automation whose saved value differs from what MuseScore
  wrote (or, never written, from the unautomated value) becomes a flat lane (one undo step, report line); both changed: the
  merge dialog per lane; a conflict not chosen (no GUI, `--update-live-set`) leaves Live's value and asks again next time;
  Update never writes over a turned knob. Kontakt's own window (opaque state) is not read. Main Kontakt only: an extra
  patch's turned knob is reported.
- **Controller defaults shipped**: `patchDefault` on every SSO `<Controller param>` (767), measured on the Windows VM
  (background extract `--extract-patches mapped`, build 2496da4 = run 433, Kontakt 8 + SSO, MuseScore's setups) ->
  `tools/soundlibraries/sso_controller_defaults.json` (`controller_defaults_from_extract.py`) -> `gen_spitfire_sso.py`.
  Never-loaded patches' dashed lines show it; a loaded setup differing is logged in `load times.log`. Note: SSO reports
  every Mic N level at 1.0 (Celeste / Crotales / Marimba 0.9998) while `micDefault` says the Tree alone plays: the mics'
  on / off lie elsewhere in SSO's script; mic compensation still reads `micDefault`.
- Tested on Linux with the test synth (`tst_liveequivalence` updateLiveSetKnobs / controllerPatchDefault,
  `tst_liveintegration` liveSetKnobs, `tst_soundlibrary` controllerPatchDefaults). **Untried in real Live**: whether
  Live applies the Manual value, fills a free Configure slot as written, and keeps it on save.

## Controllers only by automation (2026-10-15, branch `automation-only-controllers`, merged into integrate-1012)

The owner, 2026-10-15, showing the Sound Library window and a Live lane's red dashed line: "remove this entirely, we'll
use automation instead. if something has no automated value yet, I want it to show a dashed line like Ableton does …
just make it like ableton"; and: move the library's folder box from Mixer › Advanced Options… into the Sound Library
window, titled with the library's name alone.

- Gone: the Sound Library window's Controllers column and *Controllers…* window (`ControllersWindow`), its live MIDI
  corrections (`PartControllers::LiveOverrides`, `Seq::libraryControllersChanged`), the metaTag `partControllers` as a
  source (`libmscore/partcontrollers.h` header: what is left).
- A **flat lane** (one value from tick 0) is a controller's setting and plays exactly as a Controllers value did (CC with
  its staff texts, plug-in parameter on the instance and in Create Live Set's Kontakt state, mics by the patch volume);
  other lanes as automation. Old files: `PartControllers::migrate` at load (read302.cpp), the metaTag dropped on save.
- Lanes without points: Live's dashed line at the unautomated value (map default, the map's mic defaults, else the
  setup's own value recorded at its last load here, `parameter values.json`); value on hover and in the header; a click
  on it adds a point at that value, a drag sets the value from the start; the last point deleted: dashed again.
- A lane edit is heard at once while playing (parameter settings on the instances, the rest by `renderAgainPlaying`,
  which ends sounding notes as a seek does).
- Tested on Linux only (renderer, test synth, `tst_liveequivalence` with migrated scores, pictures from
  `tst_liveintegration` `MS_LANE_PNG` / `MS_SOUND_LIBRARY_PNG`); Whence's own partControllers (Violin I vibrato 0)
  migrates to a flat lane with identical dumps and audit. **Untried with Kontakt / SSO and in Live**: the dashed value
  of a parameter before its patch ever loaded on that computer is unknown (no line, header "–"); Update Live Set keeps
  Kontakt's state, so a flat lane's new value doesn't reach an updated set (as Controllers values didn't); a Kontakt
  parameter turned in Live without automation isn't read back. (All three addressed by `lanes-b-readback` above.)

## Technique balance, `[levels] calibrated` 2 (2026-10-15, branch `bprime-final`: technique-balance + context-remeasure, merged into integrate-1012)

Option B' (the owner, 2026-10-15: "if the data clearly point to b', then b' it is"), replacing B the same day: a velocity
technique is matched to the part's Long at its marking (as 1), then plays D more along its own curve, then MS4's offset and
the Inspector's. D from real players where measured, the map's `heldOffset` (`tools/soundlibraries/technique_offsets.py`
-> `technique_offsets.json` -> `gen_spitfire_sso.py`): the string sections' Spiccato and Short 0.5 **-3.4 dB** (Spheres
K. 550, staccato against held at f, perceived, with the owner's corrected readings, review page
https://claude.ai/artifact/8FoTWaNAb3Ekf2zFEDqWg8; pooled over Violins 1 -0.4, Celli -6.7, Basses -3.1; 90 % [-5.7,
-0.9]; Violins 2 has no single held note at f, double stops, and Violas take the pooled value: assumptions), Short 1.0
+0.3 (K. 550's quarters against wholes at p, [-2.2, +2.3]; an assumption that it plays as those), pizzicato Meyer 2009's
pp / ff by the written CC (sound power: several dB uncertain), the Basses' Bartók and col legno 0 (SSO's two metrics
disagree by ~10 dB). Everything else (Bartók, col legno of the upper four, Short CS, harmonics) keeps B's SSO loud-end D:
an assumption, no real data. The same D at every marking: untested at p. **The level rule is not shifted**: its grid was
re-measured the same day against each section's held Long (gain = held - slurred, context-remeasure), the reference D
is measured against too, so the earlier shift of every slur gain by the Spiccato's D (`ContextLevel::staccatoShiftDb`)
was removed (it would count D twice). Why B went: its D had the wrong sign for bowed shorts and was too high for
pizzicato (docs/PLAYBACK_SETTINGS.md › Calibrated levels).
Tests (Linux, renderer and test synth): tst_soundlibrary 97 + 3 skipped, tst_liveequivalence 39 + 1, tst_marcatolevel 14,
tst_performancetechnique 22, tst_playbackaudit 9 + 1. Whence against integrate-1012: the 109 Spiccato notes 5-15
velocities softer (medians Violins 1 107 -> 95, Violins 2 105 -> 93, Violas 111 -> 106), G 15.29 -> 16.32 dB, S 9.29 ->
10.32; audit OVERLAP 0, CAPPED 1, ARRIVAL 0 / 2 warn. Not merged (waits for the coordinating session). **Untried in
Kontakt / SSO and not heard.**

## Mic compensation (2026-10-15, branch `mic-compensation`, merged into integrate-1012)

The owner, 2026-10-15 ("sure"): measure each patch through each mic on the VM and compensate, so that changing a part's
mics changes tone and space, never the balance. `[levels] micCompensation` (1 in Recommended and by default, 0 in Library
default; docs/PLAYBACK_SETTINGS.md › Mic compensation): measured with kthost on the VM (Kontakt 8 + SSO), 33 patches (the
28 balance patches, the strings' Performance patches), the fader law and Mic Mix Distance's (`mic_levels.py` ->
`sso_mic_levels.json`). Found: SSO's default mix is the Tree alone; mic values sent with Mic Mix Distance don't play.
Since balance-refine's mic levels (657fab36a9) every patch with a section level has its own (the timpani's Close mic
rendered silent; the percussion patches have no mic controls in the map). Open: a mic changed alone later while Mic Mix Distance is set (live, or a lane point) is untested in Kontakt. Not
heard; untried in MuseScore / Live with the real library (tests: the test synth and the renderer).

## Balance refinement (2026-10-15, branch `balance-refine`, merged into integrate-1012 at 657fab36a9; the owner approved each step)

The owner, 2026-10-15: "recursively try to improve our current level balance until you think it's impossible to
improve anymore". `sectionBalance` 2 refined from data in three iterations, each in docs/BALANCE_LOG.md: the French horn
from Clark & Luce's player 1 (Fig. 2's horn was player 2's alone; Trumpets a2 over Horns a2 at mp 17.9 -> 4.5 dB), a
`sectionLevel2` for the 15 patches that had none (solo strings, Motif brass, piccolo, alto / bass flute, contrabass
clarinet / trombone / tuba, cimbassi; SSO side from the one-pitch dynamics check, ±2.3 dB; 8 by a family member's level,
an assumption), each Clark & Luce player counted once (tuba +1.6 dB). Iterations 4-8 (same day, VM): the 15 patches
swept against CC1 (`sso_gap_cc1_curves.json`; 7 with their own real range, 8 their family's span, marked); every marking
checked as MuseScore sends it (43 patches, median error 0.05 dB, 42 in order; Cor Anglais' round robins spread 4.5 dB
against its 1.04 dB step); bass clarinet / contrabassoon / bass trombones by TU Berlin's ratio to their sibling (bass
clarinet ±5 dB); the harp by TU's sound power for decaying tones (fast-weighted energy, `tu_transient.py`; -9.81 dB,
±4 dB); timpani not applied from TU (its D2 has unlabelled dynamics), since iteration 10 (branch perc-keys-levels) from the Spheres dataset's Romeo and Juliet at written p / f (-6.92 dB, ±3, realSteps 43.8; markings review page https://claude.ai/artifact/R2bYpawLeH8MU2585HRoXj, checked by the owner, four readings corrected: iteration 14, branch timpani-readings), the Grand Piano from Meyer 2009 (-1.11, ±3; iteration 11), celeste and mallets no source (iteration 12); the percussion group inside Skålevik 2019's range (no
change); each marking's own directivity from TU's pp recordings (`tu_directivity.json`; horns +3.5 dB); mic levels of
the 15 patches and the harp (`sso_mic_levels.json`). **Timpani octave, fixed** (341ac3dd84): SSO's timpani keys sound an
octave under their MIDI number (key 50 = D2), so a written D2 (MIDI 38) hit a silent key and every timpani note played
an octave low; map `<Instrument keyShift="12">` (`KEY_SHIFT` in the generator), checked in Kontakt on the VM (written D2
73.2 Hz, A2 109.2 Hz). Open: the owner to check the
Table II readings (https://claude.ai/artifact/5W2xDamjK9vLRcJ4S6cFC2) and Skålevik's
(https://claude.ai/artifact/JKYSTkAFHEv4hjZviRhCEn); summary https://claude.ai/artifact/X4jyPthyHEgqYiGyhpPTvP. Not
heard (data decide).

## SSO script fixes (2026-10-13, branch `sso-script-fixes`, from reading SSO's Kontakt scripts; untried with the real library)

- **Round robins reset on every playback start**: SSO's All-techniques and single-technique patches reset their per-key
  round-robin counters at a transport start; the host reported "playing" in every block, so Kontakt saw one start per session.
  `Vst3Synth::playbackStarted` (called by `Seq::process` when the transport changes to play) / `restartTransport`: each
  plug-in gets one block (`Vst3Plugin::TRANSPORT_STOP_FRAMES`) with the playing flag off, no events, output dropped, then
  playing. Also at `beginExport` (exports, `--live-equivalence`, verify renders, even levels' library instance) and at the
  start of each `PlaybackVerifier::renderPasses` pass (the even-levels measurement passes and any repeated pass: each is a
  playback, so each starts from the same round robins). To check on the Windows VM when a build is made: play twice, the
  same repeated note should sound the same sample sequence both times (Kontakt untested here; the test synth logs the
  host's flag, `MSTESTSYNTH_TRANSPORT_LOG`, test `transportRestart`).
- **Kickstart "Roll on high vel."**: ticked in 13 one-drum metal patches (Anvil, Cymbal Hi / Lo / Med, Mark Tree, Mini Anvil,
  Rain Sheet, Rivet Cymbal, Tam Tam, Trash Metals, Triangle 1 / 2, Wind Gong), where velocity 127 plays a roll; elsewhere
  Kickstart plays 127 as 126. Map attribute `<Instrument maxVelocity="126">` (generator `ROLL_ON_HIGH_VELOCITY`; README),
  applied to every library note-on last (`MidiRenderer::libraryMaxVelocity`), so Live's clips and the plain set follow.
  A correctness fix, not an adjustment (no setting, no preset). A roll the owner wants is the patch's Roll hit.

## Remind the owner (raise these at the start of the next conversation)

- **Stage A verdict** (2026-10-12): the listening page https://claude.ai/artifact/SYgoXqPuHmJpicoCRrTRpc (the owner's new account, 2026-10-13) (db collection
  `realrange1`) A/Bs `[levels] realRange` on a ppp -> fff ladder per section; read the verdicts (ArtifactData) and put
  realRange into Recommended only for what the owner approves. A score with written dynamics is the next test (Whence has
  almost none).
- **After the next Windows build** (the owner, 2026-10-18): ask how the winds and brass sit against the strings and how
  the wider dynamics sound (the questions of builds 433 / 434 below, never answered).
- **Build 434 for the owner** (2026-10-15, https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/38692403474,
  integrate-1012 44cfae4a5e, artifact MuseScore-soundlibrary-win64-44cfae4): Clark & Luce spans (`realRange` 2) and
  balance (`sectionBalance` 2). Since then on integrate-1012 (341ac3dd84, no Windows build yet): balance refinement
  iterations 1-8, mic compensation, the timpani octave. Whence audit on 341ac3dd84 unchanged (OVERLAP 0, UNMEASURED 10,
  CAPPED 288, ARRIVAL 33 fail / 6 warn, LEVEL STEP 0, DROPPED 0, LEAP 0).
- **Build 433 for the owner** (2026-10-14, https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/38667271676,
  integrate-1012 2496da477c, artifact MuseScore-soundlibrary-win64-2496da4; build 432 crashed in a test's use-after-free,
  fixed): the Meyer rule for realRange, section balance for woodwinds and brass (Meyer's mp), `[slurs] transitionsEarly`
  0-3 to A/B by hand (off in both presets), and everything of build 431 below. Ask how the winds and brass sit against
  the strings, and which transitionsEarly sounds most like players. Merge integrate-1012 into main once the owner confirms.
- **Clark & Luce spans, not yet in a Windows build** (2026-10-14, branch `section-spans`, merged into integrate-1012;
  the owner: "let's use the section data wherever it's reliable", then one source for every instrument): `[levels]
  realRange` 2 (Recommended and the default; 1 the Meyer rule by hand): each written marking one step of S / 5 from mp,
  S Clark & Luce 1965's Table I pp -> ff per instrument (map `realSteps`; docs/PLAYBACK_SETTINGS.md › Real range ›
  Clark & Luce spans). The Spheres dataset (whole sections, arXiv 2511.21247) agrees within ~2 dB where a section
  recorded 3+ dynamics: a cross-check, not used. Its section balance is not used either (one chamber orchestra, players
  per stem partly unknown, basses against three other sources; a 5.3 dB question between its two sessions, being checked
  with the sessions' hand claps). Ask after the next build how the dynamics sound, mostly ppp (now one step under pp,
  not the single-note extreme) and the horns' / trombones' wider range.
- **Accents at real players' level** (2026-10-21, branch `accent-level`): `[slurs] plainAccents` 2 is now the default and
  Recommended's: the accented attack (velocity 122) by CC11 at +5.0 dB over the spiccato attack (Spheres K. 550's sf at f;
  docs/PLAYBACK_SETTINGS.md › Plain accents › Level of the accented attack). Ask whether Whence's bar 8 accents now sound
  natural (clips accent_2b_ensemble / accent_2b_v2 at the old clips' gain). Open: a ">" may play softer than an sf (no
  ">" data; Romeo and Juliet has them in the strings); option 1's CC1 raise still overshoots on short notes (+10 dB on
  Violins 1 / Violas against +5.3: the held curve, no short-note curve measured); a positive D only up to CC11 127 (e.g.
  Violas Eb4 at 211 ms from CC11 86: +3.4 of +4.7 dB; Whence's, from CC11 35, fits); the accented attack's own onset is unmeasured (the plainShift is velocity 64's).
- **Try in the next build:** the automation headers for every part (fold at will; `( + | All )`, round Draw Mode button,
  Grid menu), the bar ruler (View > Bar Ruler), Update Live Set (Mixer > Advanced Options > Ableton Live), the MuseScore
  Link device fix (copy the new device from the build into the User Library; Live no longer stalls with 28 copies, phrase-
  first notes on time), smooth starts on time, a repeated key under a slur spiccato.

## Dynamics like a real player (the owner's decisions, 2026-10-12)

A written dynamic is one gesture (bow speed, pressure, contact point): tone and loudness change together. In SSO, CC1
(and a short's velocity) picks the recording, mostly tone; CC11 / patch volume is loudness only. Plan:
- **Stage A, loudness:** CC11 (with patch-volume headroom) makes each note as loud as the real player at its written
  dynamic: Weinzierl et al. 2018 (TU Berlin, 3_Audio_Features.csv: SoundPower per pitch at pp and ff) plus section size
  and section balance M, against SSO's measured loudness at that CC1 (sso_cc1_curves.json, after the Long calibration
  fix, branch long-calibration). CC1 and CC11 move together (hairpins ramp both); the Performance gain follows hairpins;
  shorts' velocity no longer capped silently (CC11 covers the rest). **Target: the real player's full range** (the
  owner). **ppp and fff: rule D** (the owner, 2026-10-12, after the data page
  https://claude.ai/artifact/Lcohqt9KsWsHanfD1jxxmw): TU Berlin's pp / ff ("as softly / loudly as possible") are the
  player's ppp / fff, the markings between them in 7 equal dB steps ((ff - pp) / 7 at each pitch; e.g. Celli A3 72.3 ->
  91.4 dB, 2.7 dB a step), relative to mp, where M was set. Rejected: A (beyond the measured extremes), C (ppp = pp,
  fff = ff: two pairs of markings alike), E (Edinburgh's 30 dB exceeds the cellists' measured 19.1). No source measures
  ff against fff (only pp / ff exist for strings), so the steps' sizes come from the rule; stage B adds the tone.
- **The Meyer rule replaces rule D** (the owner, 2026-10-14: "replace rule D with this, this is clearly better"; branch
  `meyer-rule`, for the strings and the woodwinds / brass): TU Berlin's single-note pp / ff stay ppp / fff, but written
  pp / ff are the levels of normal playing. Source: J. Meyer, Acoustics and the Performance of Music (Springer 2009),
  chapter 3, sound power of rapid two-octave scales (each note sounded) against single-note extremes, per instrument
  (figures and sections in docs/PLAYBACK_SETTINGS.md › Real range; the book's text stays out of the repo, copyrighted).
  Why: under rule D pp and ff were one step from the extremes, so a written ff asked a violin for nearly its loudest
  single note; Meyer measures players' ff in music 5 dB under it and their pp 16 dB over the softest note. Per pitch:
  pp / ff at Meyer's fractions a / b of TU Berlin's span, p .. f equal steps between (Meyer's own method), ppp -> pp and
  ff -> fff one step each. Effect at mid pitch: the violins' pp -> ff 22.0 → 15.0 dB, ppp -> fff unchanged (30.8); realDb fff
  +13.1 → +8.3 (Violins 1), so R and with it the mix's drop at mp shrink by ~3-5 dB on string scores. Rule D stays (no
  Meyer extremes): Cor Anglais, Contrabassoon (Bass Clarinet and Bass Trombone Solo / a2 take their sibling's
  fractions since 2026-10-14: an owner-approved assumption, Section balance › Winds). Cross-check: the Beethoven 8
  orchestra check (branch `orchestra-markings` 5c57d9687a) finds the clarinet within ~2 dB of the oboe at p / pp; the
  Meyer rule moves one player's levels that way without closing the gap (clarinet p minus oboe p, TU Berlin unsmoothed,
  F4 / A4 / C5 / E5 / G5: rule D median -12.9 dB, Meyer -8.6); the B8 score readings await the owner's review
  (https://claude.ai/artifact/1Eyi6i8tTt8wkzXaEtUZE9) and are not used as numbers. Not yet heard: a ladder or a score
  with written dynamics, both rules, for the owner (`real_range.py --rule-d` regenerates rule D for the comparison).
- **Stage B blind test** (2026-10-14, db `stageb1`, branch `stage-b-brightness` bb13144e0a has the measurements, HANDOFF
  there › Stage B measured): the owner picked SSO's CC1 127 brighter in 10 / 10 (Violins 1 4, Violas 4, Celli 13 / 14);
  Celli 15 / 16 (127 measured darker) and Basses 21 / 22 / 24 (centroid within 1 Hz) "sound exactly the same"; the 8
  real-player clips (TU Berlin, anechoic, one mic, 0.65-2 s) "don't sound like strings at all": unanswered. The owner:
  "maybe it IS only 2-10%, but side by side it's obv": audible is not large; the measured share stands. Test flaws: half
  the 24 clips repeat (05=06, 09=11, 13=14, 15=16, 17=18, 19=23, 20=21, 22=24). Open: a reference the ear can compare
  (a section in a hall at pp / ff), then CC1 by tone for the upper strings plus another control (tilt EQ?) for the rest.
  Option 1 (CC1 by tone) is today's mapping already (16 … 127). **Option 2, tilt EQ, dropped** (branch `stage-b-tilt`
  fa2e4a931b, `tilt_eq.py`, `sso_tilt_eq.json`: per band real pp -> ff minus SSO CC1 16 -> 127, applied offline at 0 /
  0.5 / 1 to a ppp-fff-ppp scale on real SSO; db `tilt1`): the owner, "all the same" in all four sections. The owner's
  "fff quieter than ff" was **a timing error of mine, not playback**: tilt-key.json's `start` 1.2 s is what was cut from
  the render, the first note-on is at 0.245 s; the page's jumps and my per-note measurement ran 0.955 s late (fixed). At
  the true starts fff is loudest and down matches up within ~2 dB (Violins 1 ff +13.1, fff +17.9, ff +12.9 dB vs mp);
  the Recommended events are as intended. Left: uneven steps by pitch (Celli p +1.2 over mp, Violas mf ~ f, Basses f
  -2.0 under mf: SSO's per-pitch dips, Stage A open item (5)). The same-tick CC11 X, 127, Y at every library note-on
  (e.g. 14, 127, 15) is **fixed** (branch `gaps-1012`): `libraryNoteLevels` put the value in force back before the next
  note's own value, and realRange moved the note before's CC11 with a dynamic at the next note; both now left out, every
  value after each tick unchanged (they reached MIDI out, Live's clips and the plain set's CC11 lane; hosted, zero
  duration). Test `noLevelSpikes`.
- **Stage B, tone:** after A ("important for realism"): CC1 / short velocity chosen so the tone matches the dynamic
  (brightness change pp -> ff of the real instrument, TU Berlin spectral centroids, against SSO's measured per CC1 /
  velocity), loudness then entirely CC11.
- Other sections than strings (woodwind / brass Performance transitions): later. **Woodwinds and brass started
  2026-10-14** (the owner: "research and measure winds and brass"; research: TU Berlin covers every standard modern wind
  and brass instrument at pp / ff, spans flute ~6 … clarinet ~31 dB; SSO has one measured CC1 pitch per wind patch).
- **Phrasing and vibrato: not automatic** (the owner, 2026-10-14: "they are something I should manually edit in the
  automation curves anyway"; research done: KTH phrase arch ~1 dB, near threshold; vibrato data only for register).
  Don't propose automatic phrasing or vibrato; the automation lanes are the tool.

### Stage A built: `[levels] realRange` (branch `real-range`, 2026-10-12; 0 everywhere until the owner has heard it)

Design: contextlevel.h › Real range, docs/PLAYBACK_SETTINGS.md › Real range. Each string note plays by CC11
realDb = steps(written CC1) × span / 7 - SSO Long's change from CC1 64 (span: Weinzierl's ff - pp at the pitch,
smoothed, k = 5 by leave-one-out; SSO: `sso_cc1_curves.json` rmsDb), following CC1 through hairpins; a capped short's
rest by CC11; each part's largest R as its patches' volume within the level rule's / section balance's S'. Windows
build of 8ce52a7: https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/38294617925 (on the VM:
`C:\claude\realrange\ms`).

Measured with Kontakt 8 + SSO on the Windows VM (`--verify-playback --verify-wav`, VM playback.ini preset Recommended,
realRange by the score's metaTag), a dynamics ladder (`tools/playbackverify/make_dynamics_ladder.py`: each section ppp
… fff held, then staccatos, at its check pitch), each section alone, the held note's sustain K-weighted, ppp → fff
(pp → ff) in dB, off / on / rule D: Violins 1 10.3 (7.7) / 31.6 (23.2) / 31.8 (22.7); Violins 2 8.7 (6.4) / 31.8
(23.3) / 31.8 (22.7); Violas 10.8 (6.7) / 27.9 (19.2) / 26.8 (19.2); Celli 8.6 (2.3) / 22.8 (12.8) / 21.3 (15.2);
Basses 15.7 (9.3) / 20.7 (12.0) / 19.7 (14.1). Every violin / viola marking within 1.0 dB of rule D. Celli and Basses
miss by up to 3.7 dB at single markings (Celli p -5.9 / mf -0.1 against -3.0 / +3.0; Basses p -6.5, mf -0.9): the
same dips are in the off render (Celli mf 2.0 dB under mp, Basses mf 2.8 under), one note per marking, so round robins
(sso_cc1_curves.json: up to ~4 dB apart at soft CC1) or the pitch, not the CC11. Staccatos follow the held notes (within
~1.5 dB of them, both ways). Mp: everything 5.6 dB quieter (S': Violins 2's R 14.2 + M -2.66 puts it at +6).
Whence (quarter tones; no written range: only mp in bar 9 Celli / Basses, every note at CC1 64): bars 5-12, on minus
off, -0.08 … -0.14 dB per section (median), the mix -0.12 (a capped Celli short's +0.10 dB of headroom); dump the same.
Audio for the owner: no-measure-bats/nmb-lvlpage/audio/realrange/ (`<section>_off|on.mp3`, mix, times.json; one gain per
pair).

Open for the owner: (1) listen to the ladder pairs and decide whether realRange goes into Recommended; (2) Whence has
no dynamics to hear it in: a score with written dynamics is needed to judge it in music; (3) with realRange the whole
mix plays ~5-6 dB quieter at mp (the violins' fff needs +18 dB over mp; Live's Volume top is +6) — fine for Kontakt,
but louder playback means the Mixer's master; (4) each edit re-renders the score once to find R while realRange is on
(a big score may lag after an edit); (5) Celli / Basses: the SSO side is measured every 3 semitones with ~4 dB round-robin
spread at soft CC1: a denser or repeated measurement at the parts' common pitches would tighten them; (6) stage B (tone).
**(5) measured** (2026-10-14, branch `cb-curves` 2a084db5bb, merged): Celli / Basses Long at every semitone, 4 takes (each
of SSO's two round robins twice; offline takes of one round robin are identical), map `longLevel` regenerated (nothing
else changed). Between the old 3-semitone points the old curves were off by up to 2.2 dB (Celli) / 7.3 dB (Basses). The
ladder (check pitches already on the old grid) stays up to 1.4 / 3.9 dB off rule D: the two round robins differ by a
median 0.6-1.3 dB (Celli) / 1.1-2.5 dB (Basses, max 5.5), and every other note plays the other one. Closing that needs
the renderer to predict each note's round robin (deterministic since the transport restart; the per-take data is in the
file): a C++ step, proposed to the owner. Whence unchanged (all its Celli / Basses notes at mp).
**Not wanted** (the owner, 2026-10-14, blind A/B, https://claude.ai/artifact/7RrZnLvhhFmUo9drMrqCpC, real SSO on the VM,
branch `rr-test` `rr_blind_test.py`): 8 repeated quarters + a line, Basses E1 / A1 / D2, Celli C4 / G4, as recorded
against a best-case per-note CC11 evening (the clip's own per-take levels; repeats' spread 3-8 dB → 0.1-2.3): the owner
preferred as recorded on all 5 (sure on the three Basses, guessing on the Celli). So no round-robin correction. Found on
the way: SSO's CC11 is 20 log10 (as `rendermidi.h`), and in quarter notes the Basses' two takes differ more than in held
notes (up to 7.7 dB against 5.0) and rise differently (one fast, one slow swell).

## Before a build goes to the owner (2026-10-09)

Run the playback audit on *Whence* (the quarter-tone original, `Whence.mscz`, the standard score since 2026-10-11) with Recommended (VERIFY.md › Playback audit: `tst_playbackaudit auditScore`
with `MS_AUDIT_SCORE`) and read its report: no OVERLAP may remain, and every ARRIVAL fail and CAPPED note left must be
named in the hand-over with its reason (the owner, 2026-10-09: "why do I have to point out all of these issues?").
LEVEL STEP: values, marked against a proposed threshold (the difference limen, 0.69 dB at 60 dB SL, Jesteadt, Wier &
Green 1977) the owner hasn't approved yet.
DROPPED (fail: a legato note sent < 15 ms after the one before at velocity <= 64, which SSO drops) and LEAP (warn: a transition beyond an octave, which SSO attacks afresh) were added 2026-10-13: name any left in the hand-over.

## Timing set in Live (2026-10-11)

Branch `live-timing-overrides` (from `live-written-clips`). The owner: *Live plays the score* keeps the played timing in
its clips, "but if I manually adjust the timings in Live, it stays overridden like that". A note moved or resized in a
"MuseScore: …" clip becomes that note's override (ms after its written start / end), kept in the score (metaTag
`liveTimingOverrides`), played by MuseScore too, never moved back by the next push (the device doesn't write over a
clip edited since: `base`, protocol 10). LIVE.md › Live plays the score › Timing you set in Live has the design, rules and
what only real Live can show. Not tried in real Live yet; the route set and the plain set's clips get no edits back
from Live this way (the plain set plays an override through its played times when MuseScore writes the set again).

## Written clips in the plain set (2026-10-11)

Branch `live-written-clips` (from `claude/whence-listen-2`). The owner (2026-10-11): Live clips hold the WRITTEN notes;
the timing is applied at playback, per technique by Track Delay, the rest by the MuseScore Link device. LIVE.md › The
plain set › Written clips has the design; `libmscore/livetiming.h`, MuseScoreLink.js › Written clips.

Where played timing reached Live before: the plain set's technique clips (onset early, keepMs cap, the note before cut,
Performance joins, the same-key note-off after them, so also the switch runs' clip starts): now written. The Kontakt
track's Controllers clip: still at played times (controllers, not notes; meets the notes as before). *Live plays the
score*'s route clips, the route set and `--live-equivalence`: still played (open, below). Clip tabs: show the clip, so
the written notes now.

Step 2, measured on the Linux dev machine (`tst_liveequivalence dumpEvents` with `MS_DUMP_TIMING`, the shipped SSO map,
no Kontakt), 454 notes, Whence 12-TET, Recommended (`heldNotes/byPitch` 0), MuseScore's played times against Live:
- Track Delay alone (each technique track minus its most common early start; the median gives the same): note-ons
  equal for 377, off for 77 (all > 30 ms, 9 > 50, max 87.5 ms: capped or cut notes); note-offs off for 439 (median 26 ms,
  78 > 30 ms, max 170.5 ms: the note-off stays where written while the track moves it early, and the note before an early
  start is cut there).
- With `byPitch` 1: note-ons off for 95 (84 > 30 ms, max 95.5), note-offs 439 (max 179.5).
- Whence (quarter tones, 28 technique tracks): as Whence 12-TET by the median.
- Track Delay (the lookahead: the earliest note-on or note-off before its written place, 59.1-170.5 ms by track, most
  94.4) plus the device's delays: every note-on and note-off at MuseScore's time (0.00 ms, all three runs). Before the
  lookahead covered note-offs too, 42 note-offs (Performance re-attacks: the note before ends its gap before the next
  one's early start) came up to 52 ms late.

Open, for the owner:
- *Live plays the score* (and the route set it uses): written clips there need each route track's Track Delay, which
  Live's API can't set and those tracks are often made by hand; left played. Decide whether to change it (the route set
  could write the Track Delay; hand-made tracks would need it typed in).
- The device's side in real Live 12.4.6 (branch `link-device-fix`, 2026-10-12, the VM; LIVE.md › Create Live Set › What
  is tested): the Track Delay moves a MIDI track's MIDI To output; the patcher sees the note's own song position
  (`TIM_SEES_TRACK_DELAY` false; true had made every phrase-first Performance note late by its track's default); the
  `"Timing"` store comes back from the set (all 28 copies' records); the timing record now goes into the buffer once
  (the 1 s ring was outrun by the export). Export: phrase-first notes within 3.3 ms of MuseScore's time. Still open:
  Bank / Sub through the device, `[pipe]`'s precision in playback (only the export was recorded). The device no longer
  stalls Live with 28 copies (LIVE.md › Live plays the score › The device's load on Live).

## Clark & Luce balance, `[levels] sectionBalance` 2 (2026-10-14, branch `cl-balance`)

The owner, 2026-10-14: "honestly I don't trust my ears, I prefer all the balance levels and the dynamic levels loudness
to be based on real data". Recommended and the default now play sectionBalance 2: the map's `sectionLevel2`, Clark &
Luce 1965's orchestral scales (pp / mf / ff, mp = pp + 2/3 (mf - pp)) as sound power (+31 - TU Berlin's DI_max) against
SSO's Long at CC1 64, centred on the strings as before (`section_balance_cl.py`; table and sources:
docs/PLAYBACK_SETTINGS.md › Section balance). Three independent sources agree on the woodwind / brass levels against
the strings (C&L, the Spheres sections, Skålevik 2019); the TU single-note balance (sectionLevel, 1) was the outlier.
1 stays selectable ("Custom"). Assumption: bass clarinet, contrabassoon, bass trombones by their family member's change.
Open: the owner to review the Skålevik readings (https://claude.ai/artifact/JKYSTkAFHEv4hjZviRhCEn) and the C&L
readings (https://claude.ai/artifact/67i6bLfUGkfrkLVLWQdT2e). Not heard by the owner (by their rule, data decides).
Checked (dumpEvents on the Linux dev machine, no Kontakt; Recommended): every one of the 28 sections at the default
dynamic (CC1 64, CC11 127) plays SSO's measured Long level at CC1 64 + its volume = the real section's sound power at mp
- 135.6 dB, within 0.05 dB (Trumpets a6 at +6, the loudest; Horn Solo -29.97 the lowest volume). Headroom: Whence
(quarter tones) Celli - Performance still the top (+6), Violins 1 / 2 +1.65, Violas -4.33, Basses -2.27 dB against
sectionBalance 1; a full orchestra ppp -> fff (a2 woodwinds, a6 brass, tuba, strings): Trumpets a6 set the top (+6),
note levels -25.93 .. +6 dB, CC11 20 .. 127; nothing over +6 dB or CC11 127, nothing near the -42.08 floor. Whence audit
unchanged (OVERLAP 0, UNMEASURED 10, CAPPED 288, ARRIVAL 33 / 6, LEVEL STEP 0, DROPPED 0).

## Section balance, `[levels] sectionBalance` (2026-10-11)

Branch `section-balance-m` (from main; section-balance's mechanism + `sso-cc1-curves`'s measurements): the owner chose
**M** (2026-10-11), on in Recommended and by default, 0 in Library default. Each string section's patches play its map
`sectionLevel` O louder against the rule's flat level: Violins 1 -3.81, Violins 2 -2.66, Violas -2.12, Celli +2.77,
Basses +5.81 dB (a real orchestra's balance at CC1 64 from Weinzierl et al. 2018's sound power and SSO's section sizes,
less SSO's measured All techniques Long at CC1 64, `sso_cc1_curves.json`, centred; `gen_spitfire_sso.py`
`SECTION_FADERS`, docs/PLAYBACK_SETTINGS.md › Section balance). A phrase plays gain + O, everything lower together by
S' = max(0, loudest gain + O - 6). Listening: the owner's blind ABX heard the basses' 1.6-3.6 dB differences soloed
(14/14), not in the full mix (5/8); M was chosen for its model. The earlier balance A′ (R's faders + the rule's S) is
replaced; branch `section-balance` keeps it.

Whence (quarter tones), Recommended, `dumpEvents` on the Linux dev machine (no Kontakt): S' 12.063 (the Celli -
Performance's slurs, G 15.293 + 2.77, set the top: Celli - Performance +6.000). Volumes on / off: Violins 1 -15.873 /
-9.293, its Performance -0.580 / +6.000; Violins 2 -14.723 / -9.293; Violas -14.183 / -9.293, Performance +1.110 /
+6.000; Celli -9.293 / -9.293, Performance +6.000 / +6.000; Basses -6.253 / -9.293. Every note's level (CC11 + volume),
on minus off, is O - 2.77 exactly, hosted (454 notes, chunked) and in the plain set (Live's clips: the same CC11 at all
454 note-ons on and off; each Kontakt track's volume O - 2.77 dB apart, tuning copies too). So the sections are M apart
and the whole mix is 2.77 dB quieter than without the balance (only the Celli keep their level). Nothing above +6, no
CC11 over 127, nothing near the -42.08 floor. Playback audit identical on and off. Not yet heard by the owner as M.

**Since 2026-10-14 the strings' M is -2.60 / -1.45 / -3.08 / +1.89 / +5.24** (the owner, "sure": one rule for every
section, mp where realRange's Meyer rule plays it instead of 0.4 of pp -> ff; today's curves, Celli / Basses every
semitone; computed by `section_balance_winds.py`, read by `gen_spitfire_sso.py`). The figures above are the 2026-10-11 M's.
Whence now: Celli - Performance still the top (+6), the mix 1.89 dB quieter than without the balance; against the
2026-10-11 M Violins 1 / 2 +2.09, Violas -0.08, Celli 0, Basses +0.31 dB (dumpEvents). Not heard.

### Winds (2026-10-14, branch `winds-balance`)

The owner, 2026-10-14: woodwinds and brass get an M from the solo data (TU Berlin sound power with Meyer's levels)
"until we can find better non-solo data". 23 patches, `section_balance_winds.py` -> `sso_winds_section_balance.json`
-> `gen_spitfire_sso.py` (`SECTION_FADERS_WINDS`) -> map `sectionLevel` (table: docs/PLAYBACK_SETTINGS.md › Section
balance). The C++ already applied `sectionLevel` to any patch (`ContextLevel::volumes`); test `levelSectionBalance`
now also plays a flute and Trumpets a6 on the shipped map.

- **One rule for strings and winds** (the owner, 2026-10-14, "sure"): first the winds went on the 2026-10-11 strings'
  scale (mp 0.4), which put them 4.39 dB higher than Meyer's own mp would; then the strings moved to Meyer's mp too
  (above), the winds with them (-3.14 Contrabassoon … +20.07 Trombones a6).
- **Headroom**: S' = max(0, loudest O + R + gain - 6) lowers every patch alike. A full orchestra (a2 woodwinds, a6
  brass, tuba, five string sections; bar 1 fff, strings slurred, bar 2 mp; `dumpEvents`, Recommended, against the map
  without winds' M and with the 2026-10-11 strings): Trombones a6 (O 20.07 + R 5.39) now sets the top, everything **11.47
  dB lower** plus its own M change: the strings 10.26-12.43 dB lower (Celli - Performance +6.00 -> -6.35, Violins 1
  -3.03 -> -13.29), the brass up (Trombones a6 -2.60 -> +6.00). A score with a6 brass is that much quieter; CC11 lowers
  further below the volumes (the -42.08 floor not reached here, not checked on long scores). Whence: above.
- **Open: better non-solo data.** The only ensemble measurement is the Beethoven 8 orchestra check (branch
  `orchestra-markings` 5c57d9687a; clarinet within ~2 dB of the oboe at p / pp), its score readings unreviewed
  (https://claude.ai/artifact/1Eyi6i8tTt8wkzXaEtUZE9). With M the clarinet minus the oboe moves +3.80 dB at every
  marking (before -6.8 / -5.2 / -1.9 at p / mp / f near pitch 73, now about -3.0 / -1.4 / +1.9).
- **Bass clarinet, bass trombones: sibling's fractions** (the owner, 2026-10-14, "sure": an assumption, "plays like its
  family", not a measurement; `real_range.py` `SIBLING`): Bass Clarinet the clarinet's, Bass Trombone Solo / a2 the
  tenor trombone's. Cor Anglais and Contrabassoon stay on rule D (the owner not asked).

## The level rule, `[levels] context` (2026-10-10)

The owner (2026-10-10): the same written dynamic sounds equally loud in every string part and technique, judged in the
music. Branch `performance-level-rule`: SSO's five string sections' Performance Legato slurs play CC1 at the written
dynamic and one gain per slur, by volume only (CC11 and patch volumes; never CC1): gain = st_fit - pfd + 1.3 dB
(since 2026-10-15, branch `context-remeasure`: held Long - slurred figure, re-measured with today's slurs; below › Open)
(`level_rule.py`, map `contextLevel`; docs/PLAYBACK_SETTINGS.md › Level rule). Headroom: the score's largest gain G puts
the Performance patches at G - S and every other patch of the library's parts at -S, S = max(0, G - 6) (Whence: G 15.3,
Performance +6.0, the rest -9.3 dB). On in Recommended and by default, 0 in Library default.

Checked with the real Kontakt + SSO on the Windows VM (kthost renders of `dumpEvents`' routes, 2026-10-10) against the
owner's lowered R set (render + faders), loudest 50 ms per note, median, after one common offset (-1.6 dB):

| Part | Bars | Slurs (Performance) | Staccatos (All techniques) | Without the rule (all notes) |
|---|---|---|---|---|
| Violins 1 | 1-12 | +0.3 | +3.1 (2 notes) | -6.3 |
| Violins 1 | 13-16 | +6.7 | +3.1 | +3.8 |
| Violins 2 | 1-12 / 13-16 | - | +2.5 / +2.5 | +3.3 / +3.3 |
| Violas | 1-12 / 13-16 | +0.3 / -0.7 | -0.4 / -0.4 | -3.3 / -1.0 |
| Celli | 1-12 / 13-16 | -0.0 / - | -2.2 / -1.3 | -6.9 / -0.4 |
| Basses | 1-12 / 13-16 | - | -0.4 / -0.4 | +0.4 / +0.4 |

Approved by the owner on A′ (2026-10-11; HANDOFF › No Performance patches' Whence decisions). Open:
- ~~Violins 1 bars 13-16 slurs +6.7 dB against R~~: the owner approved the rule's level (A′, 2026-10-11, below).
- ~~The All techniques balance between sections~~: `[levels] sectionBalance` (above, Section balance) plays M.
  Before (Violins 1 / 2 +3.1 / +2.5, Celli -2.2 / -1.3): `[levels] calibrated`, not this rule. The measured staccato offsets (fit `a`: Violins 1 -1.5, Violins 2 -1.1, Violas +2.1, Celli +0.6,
  Basses -0.1 dB) point the other way from R's faders (violins lower, violas higher), so equalising measured level
  would move further from R. Not applied.
- Held out (the fit without a block of cells, against each cell's own measured gain; cells' take-to-take noise 0.9-1.3
  dB): RMS 1.2 / 1.9 / 1.9 / 3.2 / 3.5 dB (Violins 1 / 2, Violas, Celli, Basses) leaving out an IOI, 1.1 / 1.9 / 1.1 /
  3.6 / 4.5 leaving out the middle register; Celli and Basses pp cells break the rule themselves (4.9 / -4.8 dB).
- Live: clips, the plain set's CC11 lanes and Kontakt volumes carry the same values (`tst_liveequivalence::
  liveContextLevel`, Linux, test synth). The VM `--live-equivalence` run needs a Windows build of this branch: not run.
- A/B (dry strings, bars 1-16, one common gain): `~/MuseScore/ms3fork/uploads/levelrule/` A (rule), B (R lowered), C
  (before).

## Even levels without measuring by hand (2026-10-13, design pending two checks)

Predicting the swell from the notes failed (offline, `predict/levels`: best model r 0.66, removes 20-30 % of the slur
range against 65-80 % measured in place). The perturbation scan (`sso_perturbation_scan.json`, VM, real SSO) found a
note's level depends on ~2 notes before and 1 after (Basses ~4 before), nothing further. The owner's direction
(2026-10-13): **combine** a table of common contexts measured once (shipped) with measuring the rest locally in the
background (only the edited note +-3), every measurement saved to the user's own table for reuse. **If play is pressed
while a measurement runs: wait only when the passage being measured is in what is about to play** (a short countdown,
then play; otherwise play at once; the owner: "the third"). Checked (2026-10-13): the same exact local context (2 notes
before, 1-2 after, their pitches and velocities) gives the same level in any phrase to ~0.2 dB (`sso_context_reuse.json`,
VM, real SSO; the earlier 1.3 dB had unmatched velocities), but not transposed (+1 semitone already moves the shape
0.74 / 1.77 dB p50 / p90): pitch stays in the key. Real scores barely share contexts (offline count, Whence + Glazunov
op. 97 + Dvorak op. 22 ii: ~2,200 contexts for 90 % of 2,958 notes, 83-96 % heard once, Glazunov <-> Dvorak <= 1 %
shared). So a shipped table would almost never be hit: the design is **background measuring near edits**, its results
kept per exact context for reuse (edits and re-measures of the same score), no shipped table. Untested: whether note
length / dynamic change the shape (the scans used 136 ms at CC1 64). **No predicted stand-in** (the owner, 2026-10-13:
"you would be hearing a 'fake' version for that moment. that's not acceptable, I'd rather just wait"): a note plays its
measured correction or waits for it, never a model's guess. To shorten the wait (estimate from the VM's render speeds,
~1 s per edit): one pass instead of three, one round with CC11's measured strength (~25 % over the law) built in, only
the edited note +-3 plus a run-in, parts in parallel; both "one pass" (MuseScore's renders vary ~0.6 dB per note, the
scans 0.1-0.25) and "one round within ~1 dB" to check on the VM first. Checked (`sso_even_fast_checks.json`, VM, kthost +
SSO, MuseScore's events): a window of the edited note +-3 with 2 run-in / run-out notes matches the whole passage; one pass
or one round leaves the worst notes 0.3-0.7 dB further off; ~0.5 s of rendering per part per pass-set (4.4x real time).
**The owner (2026-10-13) chose the full method** (3 passes, 2 rounds, CC11's measured strength 1.28 built in): ~3 s per
edit, parts in parallel. Branch `even-auto` builds it.

**Built** (branch `even-auto`, 2026-10-13; not merged; no Windows build yet): libmscore/evenauto.h (the Session: render,
cache, windows, rounds), audio/vst3/evenmeasure.h (render + measurement, shared with the job), mscore/evenautomeasurer.h
(own plug-in instances on worker threads, triggers, Play's wait), docs/PLAYBACK_SETTINGS.md › Even levels. Tested on Linux
only (tst_soundlibrary evenAutoWindows / evenAutoSession with a stand-in plug-in that answers CC11 by 1.28 / evenAutoTestSynth
end to end on the test synth). **Untried with Kontakt and SSO**: the time per edit (the estimate ~3 s assumed ~2.1 s of audio
per pass; the windows render the job's 3 s gap after the last note-off too, so ~5 s per pass at Whence's tempo, ~10-15 s of
rendering per edit at the VM's 4.4x), memory (one more Kontakt per part and patch measured: ~85-90 MB each plus samples),
Kontakt rendering offline on a worker thread while the main instances play nothing, the countdown's estimate, a full
score's first measurement at open (every passage). On the VM: open Whence, edit a slurred note, press Play at once (the wait,
then the corrected note), undo (no wait), compare a window's values with `--even-levels` on the same bars. Open choices for
the owner: whether a first measurement at score open should wait for Play at all (a whole score at once), whether the 3 s
gap may shrink (a measured release time per patch would be the source).

**The owner's decisions (2026-10-13)**, built on `even-auto`: (1) opening a score without values, **Play waits for the
measurement** (countdown, Stop cancels) rather than playing uncorrected: the wait covers queued windows too (at opening
every passage is queued; `Session::mustWait`), and each instance renders **from the play position forward first**
(`EvenAuto::priority` / `next`: the windows reaching the focus or after it, earliest first, then those before it, nearest
first; the focus is the play position at each detect, Play's start while it waits; instances load in the same order); the
rest goes on in the background; running playback is never stopped. (2) **The gap may shrink** to what the VM shows
doesn't change results: measured (`even_gap_check.py`, `sso_even_gap_check.json`; VM, kthost + Kontakt 8 + SSO, the
measurer's windows of Whence 12-TET / Whence Celli, Violins 1, Violas, Basses = Whence Celli an octave lower; gaps 0.25-3 s,
2-6 kthost processes x 4 renders x 3 passes each; the process is the unit of noise: a process shifts every note a little):
from **1 s** (Violins 1, Violas, Celli) and **1.5 s** (Basses) every longer gap's per-note levels and fitted glides match
3 s (mean z^2 under the chi-square 95 % bound, mean shift within t x SE; e.g. Celli 1 s: shift -0.003 +- 0.013 dB, z^2
0.93 / 1.33; fit -0.016 +- 0.011, 1.04), 0.25 / 0.5 s do not (Celli 0.25: z^2 1.78, fit 1.91; effect ~0.1-0.2 dB rms).
The release does not fall to -60 dB in 3 s (the -40 dB point is 2.4-3 s after the last note-off, Tree mic + Leader as
shipped) but the next window's measured notes begin >= 0.5 s + 2 run-in notes later. Patches not measured keep 3 s.
Also built: **early stop** (the owner's addition: 2 passes, a 3rd only above the 95th percentile of SSO's |pass 1 - pass
2|: 0.67 / 0.74 / 1.26 / 1.49 dB; ~70 % stop at 2, 23-25 % of the passes saved; fits within always-3's 95 % interval,
RMS error +0.01-0.02 dB), **4096-frame blocks** (12-16 % faster, levels the same), a transport restart per pass (as
renderPasses). **Render speed** (the owner asked why 4.4x vs 17x): kthost's time goes with the seconds of *notes* (~0.2 s
per second for Violas / Celli, 0.1 Basses, 0.3 Violins 1 in the earlier, colder runs), silence is nearly free; the VM
streams SSO's samples over the network from the owner's PC, so one file took 31-65 s on different runs: 17x was a
mostly silent scan, 4.4x dense passes with cold caches. 120 s idle after loading changed nothing; two processes at once ran
each nearly as fast as alone. **Time per edit** (VM): ~0.4 s per pass (2 s of notes), ~2.3 passes, 2-3 rounds: ~1.5-2.5 s
of rendering per edited part (before ~2.4-3.6 s), plus the plug-in's load at the first edit; the countdown starts from the
VM's 8.9x audio (`RENDER_SPEED`) until a render here is timed. The round-2 rule is the job's (residual above 1 dB).
Smoke test (Linux, headless MuseScore, Xvfb + PulseAudio null sink, the test synth as the library's plug-in, slow to load):
Play pressed at once after opening waited 11.3 s with "Even levels: measuring … loading the plug-in, then ~n s", then
played (status bar: Play, the position moving); Stop pressed during a 30 s wait: no start in the 4 s until it quit. Still untried in MuseScore with Kontakt / SSO (no Windows build).

## Slow slurs need no correction (the owner, 2026-10-13)

`swell_vs_length.py` measured slow slurs (500 ms-1 s notes) as uneven as fast ones by the job's short window, so a blind
test (`slow_slur_test.py`, listening page db `slowslur1`: Celli and Violins 1 at 500 ms, Violas at 1 s; uncorrected /
the short-window correction / a whole-note correction, loudness-matched): the owner marked every version Fine and every
passage "All the same". So the background measurer and the job skip slurred passages whose every inter-onset interval
is >= 500 ms (heard at 500 ms and 1 s; 136-500 ms untested by ear: still measured); a faster note inside a slow slur
keeps its window measured.
**Lowered to 350 ms** (the owner, 2026-10-14, "sure"): the tempo-line test heard 350 and 500 ms the same corrected or not,
neither wrong, figure and line; the owner had held the limit until the correction worked at 250 ms (the mid-note fix:
fine 4 / 4). `EvenLevels::SLOW_IOI_S` 0.35; `even-slow.musicxml` bars at 350 / 348.8 / 250 ms.

## Where the correction works (the owner's blind test, 2026-10-13, db `tempoline1`)

`tempo_line_test.py` (Celli, mf; U uncorrected vs C as Even levels corrects, loudness-matched): 136 ms repeating
figure U wrong; 136 ms line **C wrong**; 180 ms figure both wrong (C: notes 1 and 3 swell, U: note 3); 180 ms line
neither, U preferred; 250 ms figure "same", **both wrong**; 250 ms line U wrong (C preferred); 350 and 500 ms (both
kinds): no difference, neither wrong. The owner: keep the skip at 500 ms until the correction is reliable at 136-250 ms
("if we can't even consistently do corrections, seems like we need to figure that part out first"). Suspect: each
note's window starts at its arrival (note-on + legatoDelay 170-440 ms), i.e. while the next notes sound. Branch
`even-window` investigates.
**Found** (`even_window_check.py`, `sso_even_window_check.json`; earlier renders + VM, kthost + Kontakt 8 + SSO): the
CC11 acts as modelled (r 0.88-0.98, lag 10-15 ms); the measure is wrong. SSO's level peak of a transition comes before
the pitch arrives, so the arrival windows straddle peaks and dips and come out flat while each note keeps a 60-120 ms
dip-then-rise; the owner hears that rise ("swell": the largest rise of the perceived envelope within [note-on, next
note-on)): it ranks 180R as the owner did (U note 3; C notes 3 and 1) and matches 7 of 8 checks on the mp3s heard (the
job's measure 4). New correction N: flatten the envelope to each slur's median with glide nodes every 40 ms (not one
per note), 2 rounds: swell p75 136R 2.4 -> 1.4, 180R 3.3 -> 1.7, 250R 4.4 -> 1.7, L 1.8-2.9 -> 1.3-1.8 (C left them
at 1.6-4.0). Blind set U / C / N at 136 / 180 / 250 ms × R / L ready for the owner; no renderer change until he has
heard it (C++ would store glide nodes, not per-note values).
**The owner's blind test of U / C / N** (2026-10-13, db `evenwindow1`; "new correction is horrible, it has a really obv
wobbly sound", picked out every time): N wrong in 6 of 6, **dropped**. C fine in 5 of 6 (136R, 136L, 180L, 250R, 250L;
250L "dwelt on this a lot"), wrong only at 180R; U wrong 4 of 6 (fine 136L, 180L). Across both tests C failed at 180R
twice, 136L and 250R once each (fresh renders each test: SSO's per-play randomness may account for the single failures).
The swell measure ranked N best: it does not hear N's 40 ms wobble, so it can't stand in for the owner's ear. Open: the
180 ms repeating figure, where C still swells (notes 1 and 3).
**180 ms figure** (branch `even-180`, `even_180_check.py`, `sso_even_180_check.json`; VM, kthost + Kontakt 8 + SSO, 6
takes): consistent, not randomness (C's largest swells notes 1 and 3 in 6 of 6). Note 3: SSO's own E3->Eb3 transition
(dip ~66 ms, rise 4-4.5 dB by ~125 ms, in U too), beyond one value per note. Note 1: C's doing: each node sits 270-310
ms after its note-on, inside the next note, so C ramps up through note 1. Candidate S: each node at note-on + IOI/2;
swell note 1 2.67 -> 1.61, note 3 4.20 -> 3.89, note 4 1.17 -> 1.71 dB; 250R p75 4.01 -> 3.29, 250L 2.48 -> 2.77
(worse). Blind test C vs S (db `evenfig1`): 3 pairs 180R (one a hidden repeat with letters swapped), 2 pairs 250R.
The owner: 180R both wrong and "all the same" in every pair (the repeat agrees), 250R S fine once, wrong once: S not
heard at 180 ms. The owner then asked whether the "swell" is the bowed transition itself (each slur's note 1 in this
figure is a bowed transition, velocity 93, from the touching slur before): the page now marks the bowed notes.
With the marks (re-answered): the swell is **note 3** (E3 -> Eb3, fingered), not the bowed note 1 ("why do the 3s all
swell"). 250R: S fine 2 / 2, C wrong 2 / 2 ("A swells at 3"); 180R: both swell at 3, the same, every pair. Open: is
note 3's dip-and-rise that transition sample or the fast overlap (measurement on `even-180`).
**Answer: the sample** (`note3_check.py`, `sso_note3_check.json`, bf091c1b9c on `even-180`; VM, kthost + Kontakt 8 + SSO,
U, 14 passages × 3 takes): E3 -> Eb3 swells wherever it sits (rotated cycle, same overlaps: as note 1 / 2 / 3 / 4 3.60 /
3.76 / 4.49 / 3.70 dB; in a line 4.21; the cycle's other transitions 0.3-2.8), also with 600 ms notes (7.2 first, then
3.2-3.3), less into other pitches (F3 3.80, Ab3 2.44, C3 2.01). By IOI: 136 3.21, 180 4.49 (rise in ~55 ms), 250 7.34
(~200 ms), 350 6.39 (~265 ms): why S works at 250 and can't at 180. No fix within one value per note at 180 ms; left:
another transition kind or velocity for such notes (not measured). The owner: no changing the transition; asked for an
elegant levels-only solution.
**Rule R** (branch `even-rule`, c6a56a0ed4, `even_rule_check.py`, `sso_even_rule_check.json`; VM kthost + Kontakt 8 + SSO
renders, MOSQITO 1.2.1 on Linux): N wobbled because flattening the low bands' swell with a broadband CC11 moved the
upper bands (12-24 Bark), whose fluctuation strength (Zwicker & Fastl, ISO 532-1) rose 11-30% above U and C in 6 / 6
passages (180R U .207 C .221 N .288): ranks N worst as the owner did; not noise (repeatable up to 64 Hz), not timing
(CC11 = gain 1.048, 40 ms one-pole, 4 ms lag, r 0.994). R: CC11 cancels the repeatable part of each slur's loudness swell
at the strength (0.25-1 sweep) minimising fluctuation strength summed over bands; a continuous curve (5 ms), not one value
per note (storage would change). Real renders, swell p75 / upper-band FS: 180R S 2.45 / .227, R 1.65 / .210 (note 3
3.07 -> 1.84); 250R S 2.54 / .177, R 2.08 / .179; 250L S 2.09 / .194, R 1.13 / .209. Blind S vs R (db `evenrule1`):
180R, 250L, 250R, 180R repeated with letters swapped.
The owner (2026-10-14): 180R both wrong, "both swell at 3" (the repeat agrees): R's 40% smaller loudness rise is not
heard; 250L both fine, R preferred; 250R both fine, S preferred. So R isn't worth its storage change; S is fine at 250
ms in 4 of 4 pairs. Note 3's swell may be timbre (the sample brightening), which no CC11 curve removes: not measured.
**Measured: both, and what's left is tone** (`note3_tone_check.py`, `sso_note3_tone_check.json`, cb81baf1ea on
`note3-tone`; the VM's note-3 renders + 6 heard mp3s, MOSQITO 1.2.1, Bark bands, Boersma HNR). U note 3 dip -> peak (75 ->
134 ms): +5.0 phon, lowest band (< ~400 Hz) +12.1 dB, middle +2.0-4.6, top +0.3; centroid -45% (darker), HNR +7.1 dB
(bow noise -> pitched), f0 still sliding; notes 1 / 2 tone spread 1.96 / 2.24 dB against 4.17. Heard clips, band-weighted
level change S / C / R 6.52 / 5.77 / 1.91 dB, tone spread 4.40 / 4.20 / 3.67, centroid -46 / -45 / -44%: R removed the level
part; the tone part (lowest band +8 dB against the rest, noise turning to tone) is what the owner hears and CC11 (a broadband
gain) can't reach. It follows the sample (same step as note 1 or in a line alike; into F3 less, C3 hardly). **Closed on the
levels side**: the 180 ms note 3 is SSO's sample.
Demo page (2026-10-14, not blind: 4 slurs of the R / S clips looping, -16 LUFS, R level-flattened, below / above 400 Hz,
`nmb-tonedemo.py` untracked): the owner, "both R and S sound fine here". So the swell is borderline: heard when hunting
faults over the whole 8 s passage, not in a short known excerpt. Left as SSO plays it; judge in Whence with the next build.
**S built** (the owner, 2026-10-14: "do 1"; branch `even-midnote`): the job and the background measurer fit as
`even_180_check.py`'s S (nodes halfway from each note-on to the next, `EvenLevels::nodeTime`, the renderer's glide too;
target each slur's median of the envelope over its span; basis lagged 12 ms; round 1 / 1.28, round 2 always, the job's
by the envelope's measured gain, the background's / 1.28; round kept by envelope rms). Old values stale (contextOf's
version). Linux tests only (test synth): untried with SSO in MuseScore; the background now renders 3 rounds per window
(was 2 where the residual stayed under 1 dB).

## Even levels (2026-10-13, branch `even-levels`; `[levels] evenLevels` on in Recommended since 02614c9eb3)

Built from the listening test below (libmscore/evenlevels.h, mscore/evenlevelsjob.h, docs/PLAYBACK_SETTINGS.md › Even
levels): *Tools › Even Out Levels…* / `--even-levels <score>` renders each slurred Performance-legato passage through the
hosted plug-ins (3 passes), measures each note, fits the glide, renders again (once more above the 1 dB floor) and keeps
each note's value + a hash of its context on the note (metaTag `evenLevels`; a changed context is stale, shown in
Inspector › Note › Even level, not played). The renderer glides CC11 (Live's clips and the plain set too); each patch's
notes play its largest value lower by CC11 (never cut at 127), its volume adds it back up to +6 dB, the rest lowers every
library patch alike (`Delays::evenAll`).
Windows VM, Kontakt 8 + SSO, Recommended (realRange on), build 38387460368: the job on Whence 12-TET Celli bars 1-8 (the
passage), median slur range 2.11 -> 0.84 dB (round 1 kept); Whence (quarter tones) bars 1-8, Violins 1 / Violas / Celli
2.54 / 1.96 / 1.54 -> 0.75 / 0.65 / 0.74. **MuseScore's own playback** (`--verify-playback --verify-wav` of the corrected
score, Celli alone, `tools/soundlibraries/even_levels_check.py`): Whence 12-TET bars 2 / 4 slur range 3.6 dB uncorrected
-> 0.60 (median of 3 renders; the listening test's G 0.7); a single render 0.9-1.6: SSO's pass-to-pass noise is ~0.6 dB
per note (SD), so one hearing still varies. The first build (38382322224) cut the boosts at CC11 127 (Celli - Performance
at +6 dB by the level rule): fixed. Open: the owner's ear (then Recommended?); the whole library plays quieter by the rest
(Whence 12-TET Celli: 3.7 dB); the GUI path (Tools menu) is untried with Kontakt; correct whole scores, not just Whence.

## Transition levels: the slur swell (2026-10-12, branch `transition-levels`, measurement only)

The owner hears a swell in Whence 12-TET bar 2, Celli (`sso_slur_swell.json`) and wants it evened out eventually.
`sso_transition_levels.json` (`transition_level_scan.py`, `transition_levels_report.py`; Windows VM, Kontakt 8 + SSO,
kthost): every destination x interval (+-1..5, 7, 12) x fingered 69 / bowed 93 of the five string Performance patches
in isolation (136 ms at CC1 64 / 32 / 112, 600 ms at 64), plus 30 slurred validation phrases per section as MuseScore
plays Whence. Isolated, the interval moves a note's level by p50 0.4-0.8, p90 1.3-2.2 dB, repeatably; but in runs the
table predicts the (very repeatable, 0.2-0.3 dB pass to pass) note-to-note pattern only at r 0.45-0.63: a per-note CC11
from it doesn't narrow a slur's spread; half of it removes 10-23 % of the SD. Whence bar 2 reproduced in the runs
(sso_slur_swell's window 34.1 37.1 37.2 34.1). Open, for the owner: a correction would need the passage's own context
(render and measure in place, CC11 ramped across each transition, not stepped: the 2026-10-10 stepped try "sounds
horrible"), or accept the half-table rule as a weak option (`[slurs] evenTransitions`, off) to try by ear.
In-place listening test (branch `even-levels-test`, `even_levels_test.py`, `sso_even_levels_test.json`; VM, kthost):
Whence 12-TET bars 1-4 Celli, each note measured in context and corrected by CC11 moving as a step (S), a ramp over
each transition (R) or a glide between notes (G): bars 2 / 4 slur range 3.7 -> 1.1 / 0.8 / 0.7 dB (floor ~1.0). The
heard window lags the note-on by more than a sixteenth, so per-note values are solved against the envelope, not c.
Waiting for the owner's ear; no renderer change.

## Performance legato for the strings' slurs in Recommended (2026-10-10)

The owner (2026-10-10, after hand-building *Whence*'s Live set R): Recommended plays the five string sections' slurs on
their Performance patches. `[slurs] performance` (1 in Recommended and by default, 0 in Library default) plays a slurred
note as under staff text "performance" (staff text "non performance" opts out); it replaces `[slurs] quick`'s Long
(Rachm.) swap for those notes; the solo strings and the winds keep `[slurs] quick`. A staccato before a slur stays on All
techniques, so the slur starts from silence (an attack). With `[levels] calibrated` the Performance notes' CC1 is mapped
so they sound as loud as Long (`calibratedController`; docs/PLAYBACK_SETTINGS.md › Performance legato in Recommended).
This reverses the section below for Recommended only, and with it approves what branch `legato-pair-delays` left
waiting for the owner (the Violas - Performance transitions at velocity 100, map `legatoVelocity`; already merged).
No track delay was added. Measured on *Whence* 12-TET (Recommended, `dumpEvents`): the renderer already starts each
slur's first note (an attack, velocity 112 / 106) early by the Legato's onset, Violins 1 50.0 ms, Violas 87.5, Celli
70.5 (the map's onset at that velocity), and plays the transitions on the beat; Live's clips carry the same times. R's
-50 ms TrackDelay on a set made from these clips would count the attacks' early start twice (100 / 137.5 / 120.5 ms) and
move the transitions 50 ms early, which MuseScore doesn't. R's own clips sit on the grid, so there the -50 ms is the
only early start.

Open:
- **Level rule's grid re-measured** (2026-10-15, branch `context-remeasure`, in `bprime-final`; docs/PLAYBACK_SETTINGS.md ›
  Level rule): the VM check of build 435 had held notes balanced as the model says but slurs not (Va - V1 -6.6, Vc - Va
  +11.7). New grid from MuseScore's own renders (build 435 `--verify-playback --verify-wav`, Kontakt 8 + SSO, today's
  Recommended slurs incl. transitionsEarly 1, even levels off, contextLevel zeroed): gain = held Long - slurred figure,
  IOI 100 … 1000 ms. Calibration score after: slurred - held within ±1.5 dB in every section at mp and f (before up to
  +5.0); Whence's Performance notes Va - V1 -1.7, Vc - Va +7.4 (before -8.1 / +13.3). Open:
  - the held notes' own balance in the calibration score is Va - V1 -5.5 / Vc - Va +7.3 at mp (model -3.9 / +6.6): the
    section balance's job, not the rule's (the check's -3.5 used the owner's Whence's mixer and a wider window);
  - the largest gain is now 29.6 dB (Celli pp, 136 ms; was 21): a score with fast Celli pp slurs plays every other
    patch S = G - 6 = ~24 dB lower (headroom by volume, as designed); the owner may want to hear it;
  - Whence's Violas slurs against its staccatos: with B' final (`bprime-final`) the staccatos play 3.4 dB under the held
    Long and the slurs as loud as it, so the slurs sit ~3.4 dB over the staccatos, by the data (no 1.3 dB rule any more).
- **Technique levels**: B' final on branch `bprime-final` (above), not merged.
- **Transitions early: decided 2026-10-15**, 1 by default and in Recommended (the owner by ear on Whence's fast quarter
  tones, "1 and 3 are both fine"; 1 measured best, https://claude.ai/artifact/HwdLDf8SuzNbGzLK1PSEKv). Fast quarter-tone
  slurs (136 ms notes) still sit 60-90 cents off: SSO's legato is slower than the notes and the bend retunes the note
  still sounding. Below, the A/B as it was:
- **Transitions early, to A/B** (branch `transitions-early`, 2026-10-14; the owner: "sure let's hear them started
  early"; 0 until 2026-10-15): `[slurs] transitionsEarly` sends a
  Performance legato transition early: 1 by its modelled arrival (new pitch arrives on the beat), 2 by the measured
  bendGlide leave + 23.5 ms (the change heard on the beat: Polfreman 2013, ISMIR, a bowed legato note perceived 23.5 ms,
  SD 22.8, after its pitch change starts), 3 the glide's middle on the beat (docs/PLAYBACK_SETTINGS.md › Transitions
  early). Capped so the note before keeps keepMs (40) and never under 15 ms (DROPPED); the overlap, switch, controllers
  and bend follow the moved note-on; Live's plain set plays it by LiveTiming (tested), *Live plays the score*'s clips
  carry it. *Whence* (quarter tones, Recommended + the mode; `MS_AUDIT_SETTINGS=slurs/transitionsEarly=<n>`): 288
  transitions; mode 1 median 127.9 ms early (94.9-207.4), 1 capped (Celli bar 1, the 2nd note after the score's first
  smooth note: 94.9 of 128.2); mode 2 median 15.4 (14.2-80.2: the measured leave is about -8 ms for most), 0 capped;
  mode 3 median 59.7 (46.1-132.4), 0 capped; no patch fell back (every Performance patch in Whence has a bendGlide).
  Audit counts (OVERLAP / UNMEASURED / CAPPED / ARRIVAL fail-warn / LEVEL STEP / DROPPED): 0: 0 / 10 / 288 / 33-6 / 0 / 0
  (as today); 1: 0 / 10 / 1 / 0-2 / 0 / 0 (the 2 warns: the capped Celli note and the one after it, 33 ms); 2: 0 / 10 / 0
  / 30-3 / 0 / 0; 3: 0 / 10 / 0 / 30-0 / 0 / 0. ARRIVAL still compares the modelled pitch arrival, so modes 2 / 3 show
  their transitions arrive - shift late (Violas 115 / 69 ms) by design, not a fault. Mode 0's CAPPED 288 are the
  transitions judged by the attack onset (the old quirk, unchanged). Even levels' measured values are tied to the
  rendered context, so a mode change makes them stale (re-measured in the background). **How to A/B in the next
  build:** Mixer › Advanced Options… › Playback adjustments › `[slurs] transitionsEarly` 0 / 1 / 2 / 3 (playback goes on,
  re-rendered), or `transitionsEarly=<n>` under `[slurs]` in `<dataPath>/playback.ini` then Edit › Reload Playback
  Settings; the Preset box shows "(Custom)" while it isn't 0. Untried with Kontakt / SSO and in Live (headless dumps and
  the test synth only).
- **The audit's transitions** (branch `gaps-1012`, 2026-10-14): a legato transition now arrives by the renderer's own
  model (`LibTrace::glideArriveMs`: measured bendGlide, else `legatoDelay` + `libFastDelay`), not the attack onset.
  *Whence* (quarter tones, Recommended): ARRIVAL 33 fail / 6 warn (was 20 / 10), all three kinds from transitions
  played on the beat (the owner, 2026-10-07): (1) 15 fails, a slur's first transition 100-208 ms after the attack
  before it, which starts early by its onset and lands on time (Violas 130 / 173 ms, Violins 1 100 / 208, Celli 128;
  Violas bars 3, 9-16, Violins 1 5, 13-16, Celli 1); (2) 3 fails + 6 warns, neighbouring transitions' delays differing
  (Violins 1 bars 13-15 208 then 100, Violas 9-11 / 13-15 173 then 130); (3) 15 fails, the on-time attack after a
  slur's late last transition (follows from 1). Open, for the owner: transitions early by their modelled arrival (as
  attacks by their onset) would clear 1-3 by the model, but it compares a pitch arrival (f0 nine tenths of the way)
  with a loudness onset (15 dB under the peak); untested by ear.
- **The score's first note starts early** (`gaps-1012`, 2026-10-14): the start lead (`libraryDelayLead`) plays
  everything later by the most the score's start cut a note's early start (Whence: the Celli's smooth 172 ms), that
  note at 0. The plain set plays it before its written place by Track Delay; *Live plays the score*'s clips can't.
  Untried with Kontakt / SSO and in Live. Left at 0 s on its route: CC11 127 then the note's level at the same instant
  (the score-start value before the first note's, nothing sounding).
- **A re-attack keeps the note before** (fixed 2026-10-10, integrate-2026-10-10): a slur's last sixteenth (136 ms at
  *Whence*'s tempo) followed by a new attack on the same Performance patch sounded 1.1 ms (Violas: the attack 87.5 ms
  early, the map's reattackGap 54 ms), 11.4 ms (Celli) and 31.8 ms (Violins 1): 63 notes. `onsetEarliest` now keeps
  `[legato] keepMs` (40) after the re-attack gap: those notes sound 39.8 ms and their attacks start 42.0 ms early
  instead of 87.5 / 70.5 / 50 (predicted arrival 45.5 / 28.5 / 8 ms late; Rasch's fail is > 50). Owner to hear.
- **Quarter tones on the Performance patches** (branch `legato-bend-timing`, 2026-10-11, owner to hear): a legato
  transition's bend now glides straight from the note before's tuning to its own while SSO's own transition moves its
  pitch (map `bendGlide`: when the f0 leaves the note before / reaches the new one, by the note before's length,
  pitch-tracked from Kontakt 8 + SSO renders, `sso_legato_glide.json`), then holds; the plain set writes the bend
  envelope with hold pairs. Real renders on the VM (Kontakt 8 + SSO, kthost): slurred runs at 300 / 600 ms a note in
  tune (±10 c) 0.81-0.91 / 0.91-0.95 of each note's steady time (before: 0.14-0.15 / 0.83-0.85), the note before intact
  0.98-1.00. *Whence* (quarter tones, Recommended): the 40 quarter-tone notes the patch settles on in the real
  render were in tune 0.15 of that time before, 0.54 after (Violins 1 0.06 → 0.66, Celli 0.00 → 0.29: Kontakt smooths
  a bend over 29-80 ms, `sso_bend_smoothing.json`, measured but not compensated: an inverse overshot ~15 c in
  context). 69 of the 147 quarter-tone notes on bending copies have no steady time at all (in 136 ms sixteenths the
  patch's own f0 never settles). With `slurs/performance=0` every quarter-tone note is on a tuned copy. Owner to
  decide by ear (mp3 pairs in the dev VM's `uploads/legato-bends/`).
- **Quarter tones settle in sixteenths** (branch `legato-bend-settle`, 2026-10-11, untested with SSO): Kontakt smooths a
  bend as one pole (71-80 ms), so the glide sent as the straight path (arrive 100-130 ms at IOI 136) was heard 35-43 % of
  the step short at the next note-on. Now (map `bendSmoothing`) one value from the leave makes the heard bend reach the
  tuning at the arrival (or an earlier note-on on the lane: slurs ending on a quarter tone before a re-attack, 94 ms);
  Whence dump with the pole modelled: within 5 cents before the next note-on 139 of 140 transitions (before 5), median 80
  ms after the written time, then held (median 33 ms). Needs a real render (cents per note) and the owner's ear.
- **Levels against R**: the calibration predicts Performance over All +2.7 dB (Violins 1), +3.8 (Violas), +7.8 (Celli)
  at *Whence*'s CC1 64 (perceived curves of `Spitfire Symphony Orchestra.dynamics.json`, Long minus Performance
  Legato); the owner's R faders say +13.1, +8.6, +14.0 (Violins 1 bars 13-16: +6.6). Measured on held notes along
  velocity = CC1, one pitch; R's slurs are 136 ms sixteenths (tempo 110), and the All techniques faders also balance the
  spiccatos. The owner decides by ear; nothing was tuned.
- **Performance level in context, measured, not applied** (branch `performance-context-level`, 2026-10-10;
  `tools/soundlibraries/sso_performance_context.json`, scripts in `performance_context/` of commit c10734850d; the JSON re-measured 2026-10-15): real Kontakt + SSO renders of
  Whence-like figures (staccatos around slurs) for the five sections × dynamic 32-112 × IOI 100-500 ms × three
  registers. The gain the slurs need over their calibrated CC1 for staccatos - slurs = -1.3 dB is mostly +2 … +9 dB
  (Violins 2 up to +13, Basses about 0), rising with register and falling with IOI; CC1 127 can't give it in most cells.
  The owner chose the rule (2026-10-10): applied as `[levels] context`, above.

## No Performance patches (2026-10-06; superseded for Recommended 2026-10-10)

Superseded: since 2026-10-10 Recommended plays the strings' slurs on the Performance patches (`[slurs] performance`,
section "Performance legato for the strings' slurs in Recommended" above); Library default still plays as below.

The owner: "NO using performance patches. with all technique patches we have control of release, tightness, CC
mapped vel. … and sync to tempo". The SSO map has no Performance extras; slurs play the All techniques longs, each
note its own attack, early by its onset (`Articulation::playsTransitions`). The legato-timing items below are about
Performance patches and wait unless a legato patch comes back. Release, Tightness, *CC mapped vel.* and *Sync to
tempo* stay at the library's defaults (the owner, 2026-10-06): adjusted in Live (the plain set's Kontakt track), not
by MuseScore. Unmeasured: how the All techniques longs sound slurred at speed. A slurred
"espr." note now plays Long (Rachm.) as a held one does (`long legato`, as Long CS plays muted slurs; it played the
Performance legato): offer to the owner, revert in the generator's table if they prefer plain Long.

**Performance legato tried again, 2026-10-10: not worth it** (the owner: "if we can't get the 4 separate slurs to work on
performance patch, then it's maybe just not worth it"). *Whence* 12-TET bars 1-4, Violas / Celli on Violas / Celli -
Performance (Windows VM, real SSO, kthost offline; hand-made events: each four-sixteenth slur's first note early by the
map's Legato onset, the others transitions on their beat, velocity 64, CC1 64, CC11 127). The owner heard the line pulse
on every beat: each slur's first note is a new bow stroke, louder than the transitions after it. Per-16th level SD
(RMS, 60 ms after the written time): Celli 2.6-2.9 dB (three takes) against plain Long's 0.7, first note +1.8..+2.1 dB;
Violas 1.7-2.3 against 1.6. The group-start velocity is no lever (30 / 45 / 64 / 90 / 120: Celli first note +1.0..+2.1,
Violas worse at 90-120; takes alone differ by about ±0.5 dB of SD). One slur per bar measured flat (Celli 1.9, Violas 1.1:
the per-beat pattern gone), which is why the first Performance try (until df273c3250, 2026-09-30, a slur's last note
overlapped the next) didn't pulse. A measured CC11 dip on each slur's first note was tried too
(2026-10-10; allowed: the same kind of measured level matching as calibrated levels and `[slurs] quickLevel`): first notes
lowered by how much louder they measured than the transitions against C (Celli 2.7 dB = CC11 93, Violas 0.9 dB = 115, the
fork's CC11 law, confirmed in the render), from the written time (from the early send it lowers the note before by ~2 dB).
Celli four-note bump 2.9 → ~2.0 dB, but the note-to-note spread stays 2.5 dB (C 0.7): most of it isn't the group pattern.
The owner: "still terrible". Then every note evened by a CC11 value per note, fitted by rendering (each note against
C's, two rounds): per-16th spread Celli 2.6 → 0.8 dB, Violas 2.0 → ~1.7, but CC11 steps of up to ~14 dB between
sixteenths, landing on transitions that still carry the note before: the owner, "sounds horrible, the volume seems to jump
all over the place" (a per-16th RMS spread doesn't see jumps inside a note). Performance stays dropped: the transitions'
own 2-6 dB differences can't be evened by level without audible steps. **How the Performance patches pick samples** (Spitfire Symphonic Strings
manual p. 10, the same recordings as SSO's strings; the SSO manual has no such section): a phrase's first note, velocity
1-9 smooth attack, 10-127 spiccato/staccato attack (3-4 layers, 6 round robins); transitions need overlap; slow playing
1-19 portamento, 20-84 fingered, 85-127 bowed; faster and "runs" (very fast) sets chosen by playing speed (how measured: not
stated), each 1-84 / 85-127. Measured 2026-10-10 (Windows VM): single notes, only velocity 7 of 7..127 has a slow attack;
transitions change type between 79 and 87. So P's slur starts at 64 were spiccato attacks: the pulse. Slur starts at
velocity 7 (smooth): Celli spread 2.6 → 1.5 dB, no four-note pattern; Violas 2.0 → 2.35 (first note low: the smooth
attack is slower, still sent early by the spiccato onset). Waiting for the owner's ear (listening page, version S). Velocity picks the transition (no other control on these patches: `sso_patch_controls.json`), but it
barely matters. The restored kthost (MD5 ea7e692e…, after C:\claude was lost 2026-10-10) renders the Long patches
bit-identically to the old one (050b4970…, -137 dB null); Performance renders differ take to take.

**How the Performance patches work (measured 2026-10-10)**: Violins 1 - Performance on the Windows VM (real Kontakt 8 +
SSO, kthost2 in real time with the editor open, the window's Attack & Interval / Vibrato icons read from screen shots;
A4 area, CC1 64, CC11 127; review page with the crops: https://claude.ai/artifact/NNRZK4h1VPsTBGM54mqvUw). Other
sections' Performance patches not measured.
- First note of a phrase: velocity 1-9 smooth (no bite, -6 dB at ~165 ms), 10-116 spiccato (bite at 50-80 ms, then a
  swell, -6 dB at ~260 ms), 117-127 an accented attack (-20 dB at 30-40 ms; icon and sound change at 117).
- Slow transition (IOI 700): 1-19 portamento, 20-84 fingered (new pitch at ~234-266 ms), 85-127 bowed (~185 ms up,
  ~136 down). The window shows a third interval icon from 118; 85-127 sound the same.
- Legato = the next note-on before the note-off, **or a gap up to 39 ms**; 40 ms or more re-attacks (both passes).
  CC64 changes nothing. The same pitch again (20 ms gap) re-attacks. Violas - Performance (2026-10-10, E4 → E♭4, two
  passes): 39 ms still legato, 39.98 ms re-attacks, so the owner's 39.98 ms clip gaps re-attack. Same pitch (C4 112 → C4
  106, gaps 30-60 ms): the spiccato attack icon lights, but the old note still rings within 6 dB of the new one's peak, so
  the attack barely shows (level step +0.9..+1.9 dB).
- **A staccato before a slur stays off the Performance patch** (the owner, 2026-10-10, *Whence* bars 9-12 Violas, each
  slur after a staccato eighth on its pitch): moving the eighth onto Violas - Performance (velocity 112) so the slur
  start re-attacks as bars 1-8's do was rendered (real SSO) with the eighth ending 39.98 ms before (T), shortened to the
  figure notes' 96.38 ms (V), and 4.1 dB quieter by CC1 48 (W). V's attack measured clearest (2-8 kHz step +6.4 dB against
  T's +0.5), but the owner: "all of these are worse than not doing anything". The eighth stays on the All techniques
  patch and the slur starts from silence.
- **Slurs after staccatos need the Performance patch louder** (the owner, 2026-10-10, chose B): in *Whence* bars 9-12
  (renders as above; loudest 50 ms per note, median) MuseScore's own playback has the Violas slurs 2.5 dB under the
  staccatos around them (-36.6 / -34.1 dB), the set R (Violas - Performance fader +2.0, Violas -1.6, levelled over bars
  5-8) 5.0 dB (-37.1 / -32.2). The Performance patch's short slur notes are no quieter than bars 3-8's (-42 against
  -40 … -44 dB raw); the staccatos (Spiccato 110) are what's louder. Offered +2.5 / +5.0 dB on Violas - Performance; the
  owner chose +5.0 (fader +7.0: slurs as loud as the staccatos). Raises bars 1-8's Violas slurs too.
- **Performance levels move together, by lowering** (meaning, confirmed 2026-10-12: one shared gain for every
  Performance patch, the rest lowered so nothing exceeds Live's +6 dB; section balance M, 2026-10-11, adds each
  section's own offset on top, as intended) (the owner, 2026-10-10: raising only Violas - Performance "makes the
  volume unbalanced between the parts"; then chose the lowered version as "perfect on bars 1-12"): every string
  Performance track 5 dB over R against the All techniques tracks, done by lowering since Live's fader stops at +6 dB:
  Performance R - 0.7 (Violins 1 +2.3, Violas +1.3, Celli +6.0), All techniques R - 5.7 (Violins 1 -10.8, Violins 2
  -10.2, Violas -7.3, Celli -8.0, Basses -7.3). R's Celli - Performance (+6.7) and B's Violas (+7.0) were past +6.
  Measured on the page's stems (K-weighted, BS.1770): Violins 1 at most +0.6 LU over the loudest other part (bars 5, 8).
  Bars 13-16: the model "slurs about as loud as the staccatos around them" (the Violas' approved relation, median -1.3
  dB) predicted Violins 1 - Performance -1.7 dB from R; the owner heard -1.5 as "about right" (2026-10-10). In the
  lowered set that is Violins 1 - Performance -4.2 in bars 13-16 (volume automation, step in bar 11 where the track is
  silent): an exception to moving together, which the owner heard; the rule-keeping alternative (Violins 1 All
  techniques +6.5 in bars 13-16) is unheard.
- **The level rule approved on A′** (the owner, 2026-10-11): A′ = the owner's R set (*Whence 1-16 UACC lowered*, Violins
  1 13-16) with only the three Performance tracks' levels set by the rule (each note's gain 20·log10(CC11/127) + G from
  the rule's dump, plus the section's All techniques fader). Against R (B): Violins 1 -3.0 dB bars 1-12, +3.0 bars 13-16;
  Violas +0.1 / -0.9; Celli -0.1. Violins 1 solo, bars 13-16: "I think A' sounds good", "yeah I think A' sounds good":
  Violins 1 bars 13-16 keep the rule's level, no per-score override. The All techniques balance between sections: done,
  `[levels] sectionBalance` (Section balance, above: M, 2026-10-11). Still open: R's grid + -50 ms timing
  against MuseScore's per-note onsets. Fixed separately: Celli - Performance C3's bad round robin at high velocity.
- **A hand-built set must switch techniques by clip Bank / Sub**, never a CC32 clip envelope (LIVE.md: Live 12.4.6 drops
  it at load): sets R … W did, so in Live every All techniques track played Long throughout (the owner's Kontakt window,
  2026-10-10) while the renders, which send CC32 directly, played Spiccato.
- Intervals ±1..±12 transition; over an octave (±13..±24) no transition: the new note plays its own attack by its
  velocity (v5 slow, v127 fast) and the old one releases.
- Speed (pair 69 v5 → 71, three passes per IOI; passes at slow IOIs agree within 0.1-1 dB, so round robins don't blur):
  v100 (bowed) ≥ 260 ms slow form (arrival 160-185 ms), 145-230 a second form (early burst at 50-110 ms, a dip; 145-200
  differ pass to pass), ≤ 130 an early form (40-55 ms). v64 (fingered): arrival shortens smoothly 700 → 130 ms
  (130 → 60 ms arrival), ≤ 108 a later form (125-200). History doesn't matter: four notes at 70 or 500 ms before the
  same last transition give the same arrival. **The limits are fixed in ms**: host tempo 60 / 120 / 180 (kthost
  `--tempo`, `ProcessContext::tempo`; no song position sent) differ no more than passes at one tempo (≤ 3.7 dB against
  ≤ 4.2). Transitions at 30 / 50 / 64 / 84 play the same fingered sample; arrival shortens smoothly 130 ms (IOI 700) →
  75-80 (300-200), one consistent 10-15 ms step earlier between IOI 200 and 180 (same curve shifted, not a new form);
  the only distinct fast form is the later one at ≤ 108.
- CC21 = vibrato: Non-vib 0-41, Vibrato 42-84, Molto-vib 85-127 (window zones; the level follows continuously).
- CC16 = Mute (an on/off switch: window MUTE OFF at 0-64, ON at 72-127, edge 65-71 not measured): a filter on the same
  sample, 1 kHz -2, 2 kHz -6, 4 kHz -4 dB, level -1 to -3 dB (higher notes more); transition timing unchanged.
- CC20 changes nothing measurable on a freshly loaded patch (held notes, first notes, pairs at 500 / 150 ms; the window
  labels it "UACC CC#20").
- **CC32 (UACC) switches the patch to "None / NO ACTIVE TECHNIQUE"**: silent until reloaded. Never send CC32 to a
  Performance patch.
- The owner's own set (2026-10-10, recorded on the Launchkey): first notes at 63-99 (spiccato), transitions 85-100
  (bowed), a lower neighbour at 60-75 (fingered), 2-5 ms gaps (still legato), a 54 ms gap (re-attack). Its clips play
  Whence's own figures. Rendered from it (*Whence* 12-TET bars 1-4, Violas / Celli - Performance, kthost offline, real
  SSO): velocities per figure and place from clip "Kontakt 8" (neighbour below 94 / 69 / 93 / 94, above 97 / 102 / 92 /
  88; first 112), overlap 24 ms (its median). **O** all legato; **R** as the owner's other clips, each slur re-attacked
  (the note before ends 54 ms early, velocity 106: medians of the set's 9 re-attacks), so the window lights the spiccato
  attack per slur and the transition on notes 2-4. Note-to-note level SD: Violas C 1.6, O 1.2, R 1.2 dB; Celli C 0.7,
  O 1.9, R 1.5 dB; no place in the group over 1 dB (P: 2.5). As rendered quieter than C (O 4.9 / 10.9, R 4.0 / 8.7 dB);
  listening page (level-matched): https://claude.ai/artifact/HiaXmvp2m4v5GJSCLhhus8, waiting for the owner's ear.

**Performance technique per note (2026-10-10, branch `performance-techniques`)**: under staff text "performance" the
five string sections' Performance patch plays each note by technique (`libmscore/performancetechnique.h`, the map's
`attacks` / `transitions` / `reattack` / `reattackGap` / `overlap` from the owner's set above). Auto plays the
notation: a note after silence spiccato 112, a slur's first note right after another a re-attack (106, the note before
ending 54 ms earlier), notes under a slur bowed 93 (Violas 100, `legatoVelocity`) overlapping by 24 ms, an accent
accented 122 (the sections' Performance Legato also lists `longmarcato` so an accented held note stays on it; short
accented notes keep the All techniques Marcato), a portamento glissando into a note portamento 10. The Inspector
(Note › Performance attack / transition) overrides it for every selected note at once; metaTag `performanceTechniques`.
`tst_performancetechnique` (rendered events only, Violins section from the template; not heard with SSO yet). Not
covered: a chromatic / diatonic glissando's steps and ornament sub-notes (`renderAt`) keep their plain velocity; joins
across 10-measure chunks and tuning lanes.
**Velocity and Join lanes** (2026-10-10, same branch; `libmscore/notelane.h`, `mscore/notelanes.h`): in Continuous
View, under a library part's automation header, a stem per note. Velocity 1-127 over coloured bands: a Performance
note's attacks (smooth 1-9, spiccato 10-116, accented 117-127) or, joined by transition, its transitions (portamento
1-19, fingered 20-84, bowed 85-127); a note on a velocity technique (Spiccato, Pizzicato …) the dynamics' bands, each
edge the velocity a plain note gets at ppp … fff; CC1 techniques gray. Join (ms, Performance notes after one of their
patch): + the note before overlaps, − a gap; the dashed line at the map's `legatoGap` 39 (−39 still a transition, −40
re-attacks). A dragged stem is the note's own value (filled); double-click / Delete / Reset to Auto give it back; an
Inspector technique resets them. Not tried in the GUI by the owner yet; Live gets the values through the rendered
events (not checked with `tst_liveequivalence` on a Performance score).

**Plain Long for every section, lined up (the owner, 2026-10-07: "just using plain long for everything"; goal: pairs of sections within Rasch 1979's 30-50 ms)**, branch `legato-pair-delays`. Every slurred note plays the All techniques Long, early by its onset (`<Onset early="100"/>`). Measured on the Windows VM (real SSO, kthost offline, *Whence* violas bars 3-7 moved into each of 12 patches' range, odd and even notes on two instances so no attack is masked): with the map's old onsets the slow run (durations x4) had 40 of 66 section pairs within 50 ms (loudness detector; 58 by the new pitch's harmonics), the fast run 14 of 15. The residual followed pitch (Oboe r 0.94, Tuba 0.90), so `sso_long_onset_fit.json` (`onset_fit_from_split.py`) gives Violins 1, Violas, Flute, Oboe, Clarinet and Tuba a per-semitone Long onset (isolated notes, each the median of those within a semitone, + the section's offset) and moves Basses' (-84) and Tenor Trombone's (-36); `gen_spitfire_sso.py` reads it. Rendered again (batch 7): slow 56 of 66 pairs within 50 ms (loudness, median SD 29 ms), 62 of 66 (harmonics, 28), fast 16 of 16 (34; the fast loudness detector reads too few notes). The replay predicted each to within one pair. Left over 50 ms, not fixable by a per-pitch early start: Basses on loudness (round-robin variants; the detector misses one below pitch 38, and pitch 40 alternates 150 / 320 ms; harmonics puts their pairs at 25-42) and Oboe on harmonics (pitch 76 has two attacks ~60 ms apart, at 79 the detectors differ by ~105 ms). Next only by ear: the owner listens, against `[heldNotes] early 0`. `[heldNotes] early 0` plays notes as written, to compare.

**Performance with every note shifted to a consistent arrival (the owner's "B", 2026-10-07): not reachable by shifting**
(branch `legato-pair-delays`; Windows VM, real SSO, kthost offline, Violas - Performance at mf, *Whence* 12-TET violas
bars 3-7 three times). Heard arrival after note-on in the run, unshifted: median 93 ms, 10-90 % 58..183 (the same note
in the three passes within 10 ms: SSO is repeatable). (1) The pair scan (`legato_pair_scan.py`, every start x interval
±1..12 in three contexts, `sso_legato_pairs.json`) doesn't predict the run: shifting each note by its pair's value
leaves the 10-90 % width at 115-130 ms (was 125). (2) Closed loop on the passage itself (shift by each note's own heard
lateness, render, measure again) diverges: width 125 → 165 → 328 ms; even notes whose predecessor kept ≥ 60 ms got no
tighter (130 → 144). Moving a note-on changes SSO's transition about as much as the shift. Left: a uniform shift (the
old `legatoDelay` by interval, `Legato early`), which centres the run but keeps its spread.

**Performance as an opt-in, its spread in the players' range (the owner, 2026-10-07: "let's use the performance patch,
but tune the note spread so that they are within the established range for professional players")**, branch
`legato-pair-delays`, waiting for the owner (changes the 2026-10-06 decision where a score asks for it). Staff text
"performance" (until "ord." / "non performance") plays slurred and held notes on the part's Performance patch (modifier
`performance`, 42 Legato articulations); Violas - Performance plays its transitions at velocity 100, the others at the note's own (map `legatoVelocity`; the playback.ini
override `[legato] velocity` was removed 2026-10-07: to hear the note's own, a map without it). Target: Rasch 1979, between-player asynchrony SD 30-50 ms typical (string
quartets 24-28 ms at fast tempi). Measured (same passage and VM): arrival - written SD 49 ms at velocity 64, **41 ms at
100** (on the notes heard in every render 45-46 → 37-38); 159 of 180 transitions detected against 133; median arrival
68 ms against 88; median note peak 1.5 dB lower. Per-note nudges toward the run's median (gain 0.5, ±60 ms) made it no
tighter (49 → 46 at 64; 41 → 51 at 100). Inside 30-50, not down to 24-28. To hear: velocity 100 is Spitfire's "fast
slurred with accent" (slow transitions 85-127: bowed), so offer both by ear (a map without `legatoVelocity` vs the default). Same passage on every family (moved by octaves near each patch's test note), SD at 64 / at 100 (ms): Violas 54 / 39, Violins 1 53 / 65, Celli 53 / 70, Basses 39 / 65, Flute 35 / 65, Trumpet 26 / 35; Oboe 29, Clarinet 31, Bassoon 98, Horn 29, Tenor Trombone 31, Tuba 55 render the same at both. So only Violas - Performance gets 100; the others keep the note's own. A sweep at 30 / 50 / 84 / 120 (Violins 1 53 / 50 / 53 / 58, Celli 44 / 35 / 47 / 64) looked like 50 for both, but rendering again gave Celli 49 at 50 and 42 at 64, Violins 1 55 / 53: renders differ by about ±10 ms of SD, so 50 was taken back. Bassoon (98) and Tuba (55) don't react to velocity; a per-note nudge helps Bassoon (98 → 62) and hurts Tuba (55 → 87). Lateness grows in slower music (every duration x4: Violas median 98 → 189, Horn 98 → 249 ms), so one early start per patch fits one tempo (map `trackDelay` was tried and reverted 2026-10-07: commit "Map track delays", c82c63d5c3, has every family's numbers, and the All techniques Long patches'). Transitions play on their beat (`Legato early` removed 2026-10-07; notes as written: the run's median arrives 68 ms late; an early start at
velocity 100 is unmeasured, and any start shift changes SSO's transition, above).

## Where things are (2026-10-02)

- **Standard test score** (2026-10-11): the owner's *Whence*, the quarter-tone original,
  `~/MuseScore/ms3fork/test-scores/Whence.mscz` on the dev VM (not in the repo; CLAUDE.md › Building and testing);
  *Whence 12-TET* (`Whence 12-TET.mscz`, 2026-10-06's standard) only where a test needs no tuning lanes.
- `main` has everything: `live-set-export` / `claude/intelligent-volta-gx7gmw`, `automation-editor`, the
  measurement branch `claude/intelligent-cray-6pd4o1` and `playability-checker` were merged on 2026-10-02 (the
  branches are kept). Work on a branch; finished work by Claude and its subagents is merged into `main` without asking (the owner, 2026-10-02), anything incomplete or changing an owner-decided value waits for the owner.
- Windows test builds come from a branch on request (CLAUDE.md › Branches and CI). The Windows VM with Kontakt 8
  and SSO is reachable for agents (VERIFY.md › With the real library; access in the agents' own notes). VM
  measurement jobs (task `claude-measure`, `C:\claude\measure\job*.cmd`) and regression sweeps (task
  `claude-sweep`; `tools/playbackverify/make_sweep_scores.py`, `fix_sweep_ids.py`, `analyze_sweep.py`,
  `compare_sweeps.py`): docs/HISTORY.md Part 3 › Measurements for the legato-timing fixes. Keep VM outputs under
  ~2 GB; delete renders once analysed.

## Measurements: open items (from the measurement branch; background in docs/HISTORY.md Part 3)

- Not measured: parameters only in 5 steps (0-1) and at the test pitch only; the rest check's legato grid from the
  test pitch only; links for Vibraphone and Curated Tutti - Low Wood String Stab (none found).
- Octaves: the template times (`tLeaveMs` / `tMidMs` / `tArriveMs` in `sso_legato_grid_pitches.json`, f0bef96) are
  the reliable ±12; the harmonic times are not (reverb, the lower note's harmonics).
- Velocity barely changes SSO's legato speed (20 / 64 / 110 alike on most patches): the renderer's reason for
  keeping MS4's legato velocity doesn't hold for SSO; unused so far.
- Sounds that play nothing where the map might send notes: Long Sul G / Sul C in Violins 1 / 2 and Celli All
  techniques and Core (`noSamples`), Rain Sheet Swell mp; Low Ensemble's Toms 3-5 and Field Drum Rim / X Stick share
  keys (the owner: a real conflict; set them in Kontakt when a part needs them).
- Kit techniques off at the defaults: the owner's plan is to switch one on in the kit patch (and give it a key)
  when a score needs it; the "(all on)" patches and `unpurgeSwitchedOn` are the measurement-only start of that.
- Controller links (`sso_patch_measurements.json` `links`: CC 16 Mute, 17 Release, 18 Variation, 21 / 104 Vibrato,
  22-25 mics, 40-46 Harp pedals): the map drives these as parameters by title; CCs would also work.
- Tam Tam FX Scrape and Wind Gong FX Bow (slow swells) missed the rest check's 10 dB "sounds" test though they sound.

## Legato and onset timing (state 2026-10-02, after the VM sweeps)

Measured on SSO (sweeps of builds e6f44e6, c27da62, a289780, 47b872f; tools in tools/playbackverify, history in
docs/HISTORY.md Part 3). Settings and their sources: docs/PLAYBACK_SETTINGS.md (owner's page:
https://claude.ai/artifact/XYLfhVe44uPJemn4uexhAM).
- Done: the 5-pitch legato grid covers all 43 Performance patches; octaves (±12) come from the template fit (the
  harmonic method was wrong by 75-320 ms); per-family medians now: strings +5 / −10 ms (+12 / −12), woodwinds +5 / 0,
  brass −45 / +20; other intervals 0…+10. Sul tasto / flautando / harmonics: per-semitone onset at −12 dB clamped to the
  neighbours, median +58 ms, none early. Oboe +60 and Violins 2 +25 ms corrections.
- Octaves by start pitch (branch `octave-per-pitch`, owner's option C, 2026-10-02): the 16 patches measured at
  ~16-30 starts (branch octave-measure, `tools/soundlibraries/octave_measure/`) time each ±12 slur by its start
  pitch's own value (map `octaveUp` / `octaveDown`; rule in docs/PLAYBACK_SETTINGS.md); the others keep one ±12 value.
  Not used: Violins 2 - Sul G (4 starts, inconclusive), Oboe Principal (plays no notated legato).
  Sweep (make_octave_sweep_scores.py, every start, 15 patches) 49b00a0 → 5698181: median |offset| 40 → 20 ms,
  within 50 ms 63 → 76 %, over 100 ms 129 → 60 of 690 slurs (Horn +12 170 → 10, Basses +12 155 → 20, Horns a2 +12
  120 → 15, Tuba +12 70 → 15, Piccolo −12 80 → 5); controls unchanged. Oboe Solo +12 was −60: its +60
  sweep correction no longer applies to its +12 octave values (owner 2026-10-02; −12 keeps it; re-sweep to confirm). Violins 2 ±12 were −30 (both directions): `OCTAVE_SWEEP_CORRECTION` −30 / −30 (branch legato-level-balance; re-sweep to confirm).
  Violins 1 flautando low register still +256 ms.
- **Fast slurs on time (the owner, 2026-10-02: "I want fast slurs to not sound late"; replaces the earlier acceptance of
  slurred sixteenths arriving late, and the fast-note ramp; branch `fast-slurs-on-time`). History: the early
  transitions, `fastFirsts`, `overlapTicks` and `fastTechnique` below were removed 2026-10-07 (the owner: "remove all
  settings not currently being used"); `fastShare` / `fastFullMs` now time only a bent transition's glide.** SSO's Performance patches
  sound a slurred sixteenth's pitch 100-170 ms after its note-on (strings; woodwinds / brass 60-130) at 100-200 bpm,
  whatever the note's own length; the ramp gave a 136 ms note 6 ms, so they were heard 90-125 ms late (median), and a
  slur's first note inside a run (its own attack right after the note before) 100-160 ms late. Now: the note before
  keeps `keepMs` (40) of its length as played (not a share of the written one), so every note of a run starts early
  by about the same and keeps its length; after a short note a transition takes 65 % of its measured delay rising to
  all of it at 800 ms (refit 2026-10-03, numbers-measured: 50 % / 380 ms, docs/PLAYBACK_SETTINGS.md › Measured by sweeps) (`fastShare`, `fastFullMs`: fitted to 2666 measured transitions, each patch's median within 11
  ms); a slur's first note in a fast run starts as early as a transition would (`fastFirsts`); a transition's note
  before ends `overlapTicks` after the new start as played. Measured (Windows VM, kthost re-timing the renderer's events
  offline through Kontakt; 13 Performance patches; `tools/playbackverify/make_fastrun_scores.py`,
  `analyze_fastruns.py`): transitions' median arrival +4 / +11 / +19 ms (strings / woodwinds / brass; before +125 /
  +89 / +109), slur firsts +34 / +32 / +16 (before +160 / +104 / +114), level spread over slur positions 1.5 / 1.8 /
  1.9 dB (before 1.3 / 1.9 / 2.0); Whence cellos bars 1-4 +3 ms, firsts +28, 0.7 dB (before +138, +163, 1.2). Per note
  the spread stays wide (10-90 %: -60 … +100 ms; SSO's transition time differs by interval). The fast technique
  (`fastTechnique=1`: own attacks on the same patch, as early) measured about the same (strings +10, Whence cellos +28,
  2.5 dB); with only the onset as lead it was 40-85 ms late. Owner to judge by ear: default vs `fastTechnique=1`.
  Confirmed with the real build (Windows run 37027355685, 2bc46bc; MuseScore --verify-playback of Whence on the VM,
  heard pitch arrival of slurred notes, median / 10-90 %): violas bars 9-12 +93 (+2…+253, 2 swallowed) before → +8
  (-42…+76, none) now; violins 1 bars 3-7 +108 → -32; cellos +143 → -17; violas bars 3-7 +83 → -32 (now slightly
  early; `fastShare=57` brought the cellos to +3 but not the violins: kept 65). With `fastTechnique=1` the violas
  bars 9-12 +12 but 2 swallowed, cellos +33 with a wider spread. Levels by slur position, violas bars 3-6 (heard
  windows): bars 4 / 6 within 0.8 / 1.3 dB; bars 3 / 5 still 3.9 / 4.4 dB (the third note, Eb3 reached again from D3,
  is 3-4 dB down in every variant, own attacks too: SSO's sample, not the timing). Open: the legato level balance
  below didn't fix it.
- **Legato level balance (branch `legato-level-balance`, 2026-10-02; `[legato] levelBalance`, off; removed 2026-10-07
  with map `legatoLevel` / `legatoLevelLong`, the measurement kept in `sso_legato_levels.json`).** Measured on the
  Windows VM with kthost (offline Kontakt 8 + SSO, the Performance patches' own setups): CC11 is a plain volume on every
  Performance patch, 20 log10(v/127) (101 / 80 / 64: -2.0 / -4.0 / -6.0 dB, spectrum unchanged within 0.1 dB; Tuba
  Solo - Performance ignores it), heard 10-240 ms after a step (the hall). Every start x interval +-1 2 3 5 7 12 of the 43
  Performance patches at mf (`tools/soundlibraries/legato_level_scan.py`, `legato_levels_from_scan.py` →
  `sso_legato_levels.json` → map `legatoLevel` / `legatoLevelLong`): transitions repeat exactly; against the median of
  the other transitions into the same pitch they differ by a median 0.1-1.8 dB per patch (over 3 dB: 0-31 %, Cor
  Anglais most), the same at p and f (correlation 0.7-0.99 on 4 patches); attacks against them -3.8 … +5.3 dB in a
  run, -2.4 … +5.8 settled (most woodwinds and solo strings: their legato notes settle 3-6 dB under their attacks).
  But in a run the notes around a transition move its level 1.4-4.9 dB (Violas, the same transition after other
  notes), and the isolated tables predict a run's per-note levels poorly (correlation 0.1-0.3 for sixteenths and
  eighths, 0.1-0.7 for quarters). Renderer runs (`make_levelrun_scores.py`, `analyze_levelruns.py`; 16 parts,
  sixteenths / eighths / quarters / halves; tst_liveequivalence dumpEvents through kthost): unevenness of the slurred
  notes (sd around their pitch neighbours) off 1.77 / 1.96 / 2.19 dB, on with 3 dB headroom 1.91 / 1.62 / 1.92, on
  without headroom 1.74 / 1.71 / 2.00; Whence violas bars 3-6 position spreads off 3.8 / 1.3 / 4.7 / 0.5 dB, on (3 dB
  headroom) 3.1 / 3.8 / 3.3 / 3.9, settled tables only 3.5 / 1.4 / 3.8 / 1.5, no headroom 4.0 / 1.2 / 4.4 / 0.7. So
  it stays off; the tables and the CC11 path (`libraryNoteLevels`, shared with the marcato level) are there for a
  model that knows the run's context. A raise needs headroom (`levelHeadroomDb`: the part rests that much down).

## The owner's decisions of 2026-10-04 (from the pause list of numbers-measured)

1. Phrase gap: done, `[legato] phraseGapMs` 60 (c4c8860); off 2026-10-06, removed 2026-10-07.
2. Pedal 40 / 90 ms: off 2026-10-06, `[pedal]` removed 2026-10-07 (a change one tick after its chord).
3. Thresholds: 10 dB short length and 50 % legato arrival approved as the owner's rules (ISO 3382-1's early decay time
   as an analogy for the first); T30 for the tuning tail approved (docs/PLAYBACK_SETTINGS.md › 3C). The standard's text is not
   needed (the owner, 2026-10-04): both are the owner's rules, the standard only an analogy.
4. Velocity lane: 0-200 % and "shape" default confirmed (LIVE.md).
5. Tuning copies: the free memory split between the parts that need copies only (8a53711).
6. Checker fast basses: 94 ms / 1.25 s (19fedbe), re-read against the books (review page
   https://claude.ai/artifact/KzdKk7UrH1HyJ2aFoYFR9h): they don't support a limit, so S12 is now an advisory
   ("playable, may sound unclear", still yellow) with the same numbers as the owner's trigger; no register
   condition (PLAYABILITY.md).
7. Smallest microtone: by frequency from Wier, Jesteadt & Green 1977, Table IV at 40 dB SL, held at the 200 Hz value
   below 200 Hz (option A; `microMinCents`, playabilityrules.h): 3.4-5.9 cents, was a fixed 5.

## Waiting for the owner (built, not yet confirmed on Windows / by ear)

Each has its background in docs/HISTORY.md Part 2 under the same name.
- The plain Live set (Create Live Set, 2026-10-06): the owner's first try, with Kontakt 8.13 or later on the PC
  (setups saved by the VM's 8.13.1 don't load in an older Kontakt).
- The Mixer on library parts (`mixer-sso`) and Controllers / Mixer live while playing (`live-controls`): the
  owner's Windows check.
- SSO load times (`sso-load-times`): the owner's `--measure-load-times` run; worker-thread loading stays off until
  it shows Kontakt takes it.
- SSO's controls: which controls say "not in <patch>" on the owner's patches.
- Playback verification with Kontakt: the owner's runs on the test scores and the piano score (VERIFY.md).
- Live: everything LIVE.md lists under "what only Live can show" and "Open questions for the owner".
- Clip-tab automation lanes as the clip's envelopes (branch `clip-envelopes`): the owner installs the Control
  Surface script `tools/live/MuseScoreEnvelopes` once (LIVE.md › Automation lanes in a clip tab) and tries a Windows
  build (the new device, protocol 5, from that build's `tools/live`); arrangement clips get no lanes (open question).
- Calibrated levels (branch `calibrated-levels`, from `legato-pair-delays`, 2026-10-08; the owner: shorts too
  quiet, "use MuseScore 4.7.5's articulation profiles for everything incl. marcato"): `[levels] calibrated` (on,
  Recommended) plays SSO's velocity techniques as loud as the part's Long at the same dynamic, plus MS4's
  articulation offset, plus the Inspector's *Level* (every articulation sign and technique staff text, ±35.5 dB;
  metaTag `marcatoLevels`); calibration shipped as `share/soundlibraries/Spitfire Symphony Orchestra.dynamics.json`.
  The owner listens: Recommended against Library default (shorts, marcatos, pizzicato against Long), and decides
  whether it merges (with `legato-pair-delays` under it). docs/PLAYBACK_SETTINGS.md › Calibrated levels.
  Measured 2026-10-08 (Windows VM, kthost offline, real SSO, All techniques Violins 1 / Flute Solo / Horn Solo, events
  from 1bceb0b4da; each note's loudest 50 ms RMS, median of 4, minus Long at the same dynamic, p / mf / f):
  Violins Short 0.5 off -3.6 / +1.6 / -5.2, on -4.1 / +2.5 / -1.2; Pizzicato off -13.2 / -4.2 / -5.5, on +1.4 / +2.4 /
  -1.2; Flute staccato off -13.8 / -8.1 / -0.8, on -5.1 / -2.1 / -1.6; Flute Marcato off -15.4 / -6.6 / +0.4, on -0.6 /
  +4.2 / +3.8 (target about +4.5); Horn staccato off -2.0 / -3.2 / -4.6, on -1.3 / -3.6 / +0.6; Horn Marcato off +4.1 /
  +3.6 / -2.7, on +4.7 / +2.1 / +1.5. Open: at p the shipped curves are bumpy (Flute's 48 quieter than its 32), so
  straight-line reading misses by up to 5 dB; Horn at mf picks a lower velocity than off (staccato alternates samples,
  5 dB note to note). On a K-weighted peak, Flute Marcato on reads +4 .. +8.6 dB (may sound louder than the RMS says).
  Since then (d8e7aa2001, 29aca978b8): calibration by ear (perceived curves), every held Long measured at 8 CC1 points.
  Measured again 2026-10-08 (same VM and patches, kthost, 3 pitches each: Violins A3 / E5 / D6, Flute E4 / F5 / E6,
  Horn F3 / B♭3 / F4 sounding; perceived peak, technique minus Long, 54 rows): off 6 .. 35 dB away (low pizz -34.7),
  on median |2.95| dB, 41 of 54 within 5 dB. The rest is pitch: one velocity per part and dynamic for every pitch, the
  curves measured at one test pitch. Worst on: Horn F4 staccato mf -10.4, Flute E4 staccato -9.9 / -9.5 / -7.9,
  Horn F4 Marcato p +8.2, Violins D6 pizz +6.9 / +6.0, Violins A3 pizz f -7.4. A fix needs per-pitch level curves
  (a new measurement: the check at several pitches); offered to the owner, not started.
  Also on this branch (12b6b30593, 2026-10-08): every measured technique starts early by its median onset, shorts
  and marcato too (map onset= on 343 more articulations); untried by ear with SSO. Most shorts move 3-8 ms, marcato /
  tenuto ~33, Short 0.5 61, Short 1.0 113 ms (docs/PLAYBACK_SETTINGS.md). Listen: slurred shorts against Longs.
- Tuning: the playback regression (`ab/trace/regress.sh`, not in the repository) not run since edb5eec;
  *Tools › Tuning…* left untested by the owner's choice.

No longer waiting (moot since "no automatic adjustments", 2026-10-06; the settings and their code removed
2026-10-07, docs/PLAYBACK_SETTINGS.md lists them): even dynamic steps and attack salience (calibrated short
velocities), `[tuning] oneInstance`, the timing A/B renders, the Track Delay value, per-patch octave corrections,
pedal 40 / 90 ms, audit #4 (event timing within a block).

## When a check zip arrives (*Check articulations* hand-back)

1. Unzip it. Read `summary.txt` and `results.json`: per patch, `passed`, the verdicts, and for
   scans `found`, `notInMap` and `mapValuesShowingNone`. A scan marked "inconclusive" doesn't
   mean anything is missing; use its pictures only.
2. Run `python3 tools/soundlibraries/read_loaded_patches.py "<folder>"`. It reads the loaded
   Kontakt instrument's name from each `(window).png` and says whether each patch was set up
   with the right `.nki` (numbers must match: Violins 1 vs 2, Horns a2 vs a4). Tell the owner
   about any "BAD" line before trusting that patch's results.
3. Run `python3 tools/soundlibraries/read_check_names.py "<folder>"` (it needs tesseract and
   ImageMagick `convert`). It OCRs the articulation name in each picture and lists values
   whose name differs from the map's, or that show "None". The OCR misreads some names
   (Long becomes "Large"), so look at the `<patch>.png` contact sheets for anything it flags
   before changing the map. For added patches, the "map" column reads "(not in map)": the OCR
   name is what the patch calls that value.
4. Fix or extend the map only through `tools/soundlibraries/gen_spitfire_sso.py`:
   - A new patch is a row in `I` (bank, name shown, MuseScore instrument ids, partName regexp).
     Its values come from the Reaticulate bank. When the bank lacks the patch, add its
     articulations from the scan the same way `SPITFIRE_ADD` does.
   - New articulation names need an entry in `T` (techniques, modifiers). Otherwise they are
     left out.
   - `SPITFIRE_ADD`, `SPITFIRE_DROP` and `SPITFIRE_RENAME` hold per-patch corrections.
   - Regenerate the map (the command is in the script's header; it needs a clone of
     github.com/jtackaberry/reaticulate) and diff `share/soundlibraries/Spitfire Symphony
     Orchestra.xml`. Only the intended entries should change.
5. Record what the run showed in the commit message (long narratives: a dated section in `docs/HISTORY.md`).
6. Build and run `tst_soundlibrary` locally if you can (`CLAUDE.md` › Building and testing). The
   `spitfireMap` test checks instrument matching.
7. Push. If code changed (not just the map) and the owner needs a new Windows build, put
   `[windows-build]` in the last commit message. The workflow then builds, tests and uploads
   in about 15 minutes. Follow `CLAUDE.md` › Windows build etiquette.

   A map-only change also needs a new build to reach the owner, because the XML is installed
   with MuseScore. Alternatively the owner can copy the regenerated XML over
   `share/soundlibraries/` in their installed folder.

## Things an agent here can't do

- Hear Kontakt, SSO or Windows (see CLAUDE.md › Rules: say what was tested with what).
- Read the owner's review page "SSO Articulation Map" without claude.ai's ArtifactData tool: ask the owner to
  paste their marks if they're needed.
