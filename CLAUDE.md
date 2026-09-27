# CLAUDE.md: notes for agents working on this repository

This is a private fork of the MuseScore 3.7 community fork (Jojo-Schmitz/MuseScore 3.x @
29e066cd, history squashed). It has two goals:

1. **MS4 playback.** MuseScore 3 plays scores the way MuseScore 4 does: the same FluidSynth
   setup, soundfont, reverb, note model and dynamics.
2. **External sound libraries.** Chosen parts play through a real sample library (the owner
   has **Spitfire Symphony Orchestra**, the Kontakt version, on **Windows**, in the free
   **Kontakt Player** (8), not the full Kontakt: the owner, 2026-09-27; everything here goes
   through VST 3, which the Player supports; NI's Creator Tools need the full Kontakt; SSO's
   patches are nevertheless readable: see Extract library files). MuseScore picks
   each note's articulation from the notation.

The owner talks to agents through claude.ai sessions and reads results on GitHub. Keep this
file current when you change architecture, build steps or known issues. Put the history of
what was done in commit messages.

**Picking up this work? Read `HANDOFF.md` first** (current state and next steps of the sound
library work; the tuning work is described under "Tuning" below).

## Branches

- `main`: the default branch and the only one. It was called `ms4-playback` until 2026-09-27,
  when the repository was also renamed from `musescore3-ms4-playback` to `MS3-MS4playback-SSO`.
  It has the MS4 playback work (essentially finished) and the sound-library work, which was
  developed on `claude/continue-previous-work-5u4n0j`, merged in on 2026-09-25 and then
  deleted. All work goes on `main` now. A cloud session that is given a `claude/…` branch of
  its own must merge it into `main` (and push) when its work is done: on 2026-09-27 two such
  branches (`claude/zen-heisenberg-kpqg2v`: SSO percussion, tuning, accidentals;
  `claude/gallant-johnson-czw783`: plug-in data extraction) were found unmerged a day later
  and merged then. Check `git log` and the latest commit messages first.
  They are detailed on purpose and describe what each step did and how it was measured.
- CI runs by hand only (`.github/workflows/build_all.yml`, workflow_dispatch). This keeps the
  private repo's Actions minutes.

## Rule: readings of music go on a review page

The owner (2026-09-27): **whenever work relies on an agent's reading of sheet music** (notation,
a legend of accidentals, a score's page, a screenshot of a plug-in's key or articulation list,
a contact sheet), publish a claude.ai artifact that shows each source picture (or a crop of it)
next to what was read from it and what that reading is used for, so the owner can check every
reading by eye before it changes code or the map. Draw musical symbols in Bravura (subset
`fonts/bravura/Bravura.otf` with `pyftsubset` and embed it) so the page shows the glyphs
themselves, and say which sources could not be read. Open the page with a short "What to check"
list: each reading to confirm, where to look for it, then the decision asked. Example: "Unpitched HEJI accidentals"
(https://claude.ai/artifact/CPfVu3qdHzMr5xPKiJFD2Y), the HEJI 2020 legend behind the 10
accidentals' pitches.

## Rule: files survive a round trip to MuseScore 3.6

The owner (2026-09-26): **any file this fork touches must survive a round trip to MuseScore 3.6
and back unchanged.** Test: `ab/roundtrip2.py [--mtest N] [--list file] [scores…]` (the fork
saves A, 3.6.2 opens A and saves B, the fork opens B and saves C; A and C compared as XML, as
the fork's PNG pages and as its MIDI; fresh settings per app). Where 3.7 differs from 3.6 in
what it reads or writes, the fork follows 3.6:
- the legacy style default files (`share/styles/legacy-style-defaults-v1/2/3.mss`) carry 3.6.2's
  header (3.02), so pre-3.6 scores get 3.6's defaults; five built-in style defaults are 3.6.2's
  again (footer `$:copyright:` and offset, 5 frets, square rehearsal-mark frame);
- `MStyle::isDefault` compares doubles within 1e-6 (an imported 10 mm vs the file's 0.393701);
- `MStyle::notInMuseScore36`: the nine styles 3.7 added stay at their defaults (reading, import,
  the Style dialog greys them out);
- a MuseScore 3 file's pedal line width reads as in 3.6 (not a styled property of its own);
- a tuplet's `<Number>` carries all its properties (fonts), as 3.6 reads them;
- a MuseScore 2 import gets its instrument id (`updateInstrumentId`), as 3.6 adds it on reading.
Some mtest references were updated for these (compat114/206, musicxml io, tuplet save-load).
Known leftovers: the XML of some MuseScore 1/2 imports still changes on the first round trip
(pages and MIDI identical); MIDI export's same-instant event order is not stable run to run.

Fixed from MuseScore 3.6 (not about files): deleting a time signature that restates the one in
force (same nominal meter, none local) no longer rebuilds the measures up to the next one, which
lost their breaks, spacers, stretch and volta offsets (musescore#21578; the owner used the
RemoveRedundantSig plugin for it). `Score::cmdRemoveTimeSig`, test `tst_timesig::removeRedundant`
(fails without the fix). `tst_timesig::timesig05` fails since 5f65a1d (fermatas' MS4 default
time stretch is written into the file: `<timeStretch>2</timeStretch>`), not from this.

## Layout of the fork-specific code

MS4 playback (see the header comment of each file):

- `libmscore/ms4playback.{h,cpp}`: MuseScore 4.7.5's note model. It covers articulations from
  context, their length and dynamic patterns, the dynamics map, presets per instrument,
  ornament rules and trill neighbours.
- `libmscore/ms4tables.h`: data tables generated by `tools/ms4playback/gen_tables.py` from
  MuseScore 4 sources. Do not edit by hand.
- `libmscore/rendermidi.cpp`: `MidiRenderer::collectMeasureEventsMs4` (notes, graces, tremolo,
  ornaments, arpeggio, swing, pitch curves) and `renderMs4Dynamics` (CC11, presets). This runs
  when the synthesizer's dynamics method is 3, `DynamicsRenderMethod::MS4`, which is the
  default.
- `thirdparty/fluidsynth`: FluidSynth 2.3.3, set up as MuseScore 4 sets it up (the old
  `audio/midi/fluid` is a wrapper). The default soundfont is `share/sound/MS Basic.sf3`.
- `effects/musereverb`: MuseScore 4's reverb, the default master effect.
- `libmscore/synthesizerstate.cpp`: upgrades MuseScore 3 synthesizer settings to the MS4
  defaults when they are read.
- Dynamics scope (`Ms4::Dynamics::apply`): as MS4 reads a MuseScore 3 file, a voice-1 dynamic
  or hairpin with the MS3 range "staff" (`<dynType>0</dynType>`) applies to its staff only
  (ALL_VOICE_IN_STAFF, `Read206::readDynamicRange`), otherwise to the whole instrument; voices
  2-4 to their own voice. (Found on the owner's "Ethanol": a piano ff on one staff.)
- **One deliberate difference from MS4:** a MuseScore 3 hairpin with a velocity change of its
  own plays as MuseScore 3.6 plays it (`Dynamics::addHairpin`, `ms3HairpinVelocity()`): CC11 from
  the value in force by the whole change, on MS3's curves (ChangeMap::interpolateRamp), clipped to
  0-127, down to silence (`expressionLevel` goes under ppp only then), even with a dynamic at its
  end (which takes over at its own tick). MS4 ignores the change and goes one step, so such
  hairpins are near inaudible there; the owner's scores rely on them (Ethanol, Prolongation:
  matched against 3.6.2's MIDI export). Preference `application/playback/ms3HairpinVelocityChange`
  (Advanced, on); `MS4_STRICT=1` in the environment turns it off, and `ab/trace/regress.sh` sets
  it (the demos Brassed_Up and Dawn change otherwise).
  When such a hairpin has faded to silence (CC11 0), `renderMs4Dynamics` sends all-sound-off
  just before the level returns, unless a note started during the silence, so what still rang
  (a release, the pedal) isn't heard again (Ethanol bar 14, confirmed by ear by the owner on
  Windows, 2026-09-26; built-in synthesizer only).

Playback mode (`mscore/playbackmode.h`): a "Playback, all parts" drop-down at the top of the Mixer
and of the Play Panel switches between MuseScore 3 (dynamics method 1 / CC2, Zita reverb,
MuseScore_General if found: SoundFonts folders or a MuseScore 3 install's `sound` folder,
`Fluid::sfFiles`), MuseScore 4 (method 3, MuseReverb, MS Basic) and the sound library (MS4 plus
`io/soundLibrary`; the last one is kept in `io/soundLibraryLast`). The mode is read back from the
synthesizer and the preference; a switch stops playback and saves synthesizer.xml. Actions
`playback-ms3/-ms4/-library` exist for shortcuts. The global synthesizer's dynamics method now
wins over one saved in a score (`renderChunk`; tests without a global state still use the score's).

Per part (`libmscore/partplayback.h`): the Mixer's details panel has "This part plays:" (as all parts, MuseScore 3, MuseScore 4, the sound library), saved in the score's metaTag `partPlayback`
(JSON: part index, name, mode; 3.6 keeps metaTags), undoable. Per part, not per staff: a part's
staves share its channels. `renderChunk` gives each staff its part's method (`ms4Active`: the
parts on the MS4 model; `renderMs4Dynamics`, pedals and vibrato follow it); `SoundLib::routes`
routes only parts that play the library (`playsLibrary`: their own mode, else the global one).
The library map stays loaded even when the global mode isn't the library (`updateSoundLibrary`),
so `SoundLib::active()` is true; MIDI out of the built-in parts is only held back when the global
mode is the library.

Gradual tempo changes (`libmscore/tempochange.h`), replacing the TempoChanges plugin's hidden
tempo markings: a text line whose begin text is rit., rall., accel., string. … (the Tempo palette
has rit. / rall. / accel. lines) changes the tempo over its length, set in the Inspector's
"Tempo Change" section like a hairpin: "Tempo at end" in % of the start (Auto = MuseScore 4's
default per term: rit./rall. 75, accel. 133, string. 150 …) and the change method (the hairpins'
curves; exponential is geometric). The end tempo stays; a tempo marking at the end takes over.
`rebuildTempoAndTimeSigMaps` adds tempo-map points every 32nd before the measure's tempo
markings and fermatas. `TextLine` has Pid::TEMPO_CHANGE_FACTOR / TEMPO_CHANGE_METHOD, not written
in the element: the metaTag `tempoChanges` (JSON tick, tick2, track, factor, method) is written on
save from the lines' positions and read after loading (`MasterScore::read`). Test
`tst_tempochange` (4/4). MuseScore 3.6 plays such a line at a steady tempo.

Tuning (`libmscore/tuning.h` explains the design), built in from two MuseScore 3.6 plugins:

- `libmscore/tuning.{h,cpp}`: a note's pitch in playback, in cents from equal temperament, is the
  score's temperament (billhails' Tuning plugin: 17 presets, root, pure tone, tweak, 12 final
  values; stored in the metaTag `temperament` as the plugin's JSON, which 3.6.2 keeps through a
  round trip), plus the Microtonal Tuner plugin's accidental rules (microtonal accidentals, the
  last one on the line in the bar, custom key signatures), plus the note's own tuning. Chain-of-
  fifths tunings (Pythagorean, meantones) keep C♯ and D♭ apart; the others have one value per key.
  Just intonation has both (owner, 2026-09-26: real JI tells enharmonics apart, the 12 keys stay for
  keyboards): the plugin's 12 keys (default; files unchanged) or by spelling, Ben Johnston's
  notation (`Temperament::justSpelled`, JSON `"just": "spelled"`, `johnstonCents`): the root's
  major scale 1/1 9/8 5/4 4/3 3/2 5/3 15/8, sharp / flat 25/24 (C♯ 25/24, D♭ 27/25; G♯ 25/16, A♭
  8/5, the diesis 128/125), a syntonic comma by the Sagittal 5-comma accidental; D-A 40/27; or
  Helmholtz-Ellis (`Just::HEJI`, JSON `"just": "heji"`): unmarked notes pure fifths from the root.
  The Tuning dialog's "Just intonation:" drop-down (12 keys / Johnston / HEJI) sets it.
  HEJI's 30 arrow accidentals (one to three syntonic-comma arrows on double flat … double sharp;
  MuseScore 3 plays them as naturals, 3.6.2's table gives them no value) now sound as written in
  every tuning: `ScoreTuning::hejiAccidental`, 100 × sharps + 21.506 × arrows from the written
  natural, the temperament taken from the sharp spelling (`Target::spelled`). Tests `justSpelling`,
  `heji` (heji.mscx).
- Accidental families (owner, 2026-09-26: one dominant convention → use it; competing ones →
  options, the most common the default). `conventionCents` (by symbol, before 3.6.2's table):
  Arel-Ezgi-Uzdilek and Turkish folk accidentals in Holdrian commas (1200/53; 3.6.2 had no value),
  Wyschnegradsky exact 72-EDO steps, Sagittal exact ratios over the Pythagorean note, quarter tones
  from the symbol (fixes 3.6.2's swapped FLAT2_ARROW_UP −250 / FLAT2_ARROW_DOWN −150). Per score
  (temperament metaTag, left out when default): `Temperament::quarter` (JSON "quarterTones":
  FIXED 50 default / "half" of the tuning's own sharp at that note, the 31-EDO semisharp / "33/32")
  and `Temperament::persian` ("persian": VAZIRI ±50 default / "practice" −60 +40 / "musescore36"
  −67 +33). The Tuning dialog has both. Test `families` (quarter.mscx); `microtonal` and
  `pluginParity` allow the plugin's rounding (Sagittal, Wyschnegradsky) and expect the AEU values
  the plugin didn't have. (The owner's listening page for these choices was deleted after the decision.)
- Stacked accidentals (`Accidental::isStackModifier`): HEJI's prime modifiers (7: 64/63 and two, 11:
  33/32, 13: 27/26, and the combining 17 … 53 ones) applied to a note with a ♭ ♮ ♯ 𝄪 𝄫, a HEJI arrow
  or another modifier go beside it (`stackModifier` in cmd.cpp: added, replacing one of the same
  prime, or taken away when applied again; a key signature's sharp is written out first; removing
  the accidental removes them). MuseScore 3 has one accidental per note, so each is a `Symbol` on
  the note: 3.6 reads, shows and keeps it. In the fork the accidental draws them to its left,
  highest prime outermost, and its width includes them (chord spacing); the Symbol is hidden
  (`Symbol::isStackedAccidental`) and `Note::write` saves its offset where the accidental draws it,
  so 3.6 shows it there. Tuning adds them (`stackedCents`) and carries them through the bar. Test
  `stacked` (stacked.mscx; save twice, read back).
- Tried by the owner on run 60 (2026-09-26) with `test-scores/Tuning test.mscx`: C♯/D♭ with 12 keys vs
  Johnston, the HEJI 4:5:6:7 with a stacked septimal comma (drawn left of the flat, sounds right),
  quarter tones in meantone, koron / sori, and the stacked accidental's round trip through MuseScore
  3.6.2: all as intended. `test-scores/SSO percussion test.mscx` (every kit sound with its expected
  SSO hit written under it) sounded right too, the guessed high/low orders included.
  Nothing is written to notes: `ScoreTuningScope` in `renderChunk`, `playbackTuning()` in
  `playNote`.
- `libmscore/tuningtables.h`: generated by `tools/tuning/gen_tuning_tables.py` from the plugins'
  `table.js` and `tuning.qml`. Do not edit by hand. 49 accidentals have no pitch in MuseScore
  3.6.2; HEJI's 30 arrows and its 9 prime modifiers (septimal, undecimal, tridecimal: stacked
  accidentals, below) now have theirs, and the last 10 too (the owner, 2026-09-27, after reviewing
  the HEJI 2020 legend on https://claude.ai/artifact/CPfVu3qdHzMr5xPKiJFD2Y; MuseScore 3.6 and 4.6
  play all 10 as a natural): HEJI's tempered ♭♭ ♭ ♮ ♯ 𝄪 and quarter ♭/♯ ("indicate the respective
  12-edo semitone") are −200 … +200 and ∓50 cents with no temperament, whatever the score's tuning
  (`temperedCents`); the enharmonic signs ~ = ≈ stack beside the accidental, outermost
  (`stackPrime` 1000, one at a time): the tilde moves an arrow accidental one schisma
  (32805/32768, 1.95 cents) to its Pythagorean respelling ("~♯↓ = ♭", "~♭↑ = ♯"), down with arrows
  down, up with arrows up; "=" and "≈" (not in the legend) add nothing; alone, all three are a
  natural. Test `temperedAndEnharmonic` (enharmonic.mscx).
  Now no accidental in MuseScore lacks a pitch (`ScoreTuning::accidentalCents` over every
  AccidentalType).
- Trade-off: a note's own tuning equal to a value the Microtonal Tuner writes (±50, 113.7 …)
  counts as a leftover of the plugin and plays at 0 (`looksLikeTunerValue`), as the 3.6 plugin does.
- `mscore/tuningdialog.*`: *Tools › Tuning…* (presets, final values, plugin file load/save, offers
  to clear plugin-written note values). The Inspector's note panel shows Temperament, Accidental,
  Tuning and Result.
- `mtest/libmscore/tuning` (`tst_tuning`, 13/13): the plugin's fixture and parity with the plugin
  run in 3.6.2 (51 notes, same values but 2 with a hand-set tuning, which the fork adds to and the
  plugin overwrites). The Microtonal Tuner plugin for 3.6 (reads the same metaTag; not in this
  repository) is frozen (owner, 2026-09-26): no parity to keep. Update or drop
  `pluginParity` when a deliberate change breaks it.

Sound libraries (`libmscore/soundlibrary.h` explains the design):

- `libmscore/soundlibrary.{h,cpp}`: `SoundLib`. It loads maps (`share/soundlibraries/*.xml`,
  format in `share/soundlibraries/README.md`), matches instruments by
  `Instrument::getId()` (the instruments.xml id; `instrumentId()` is the MusicXML sound id),
  computes the `Want` (techniques plus modifiers) from MS4 articulations and staff text, runs
  `choose()`, and computes `routes()` (one port and channel per matched part, in score order,
  plus one per extra patch the part uses).
- Extra patches (`with="<main>"` in the map): a part plays its main patch and the extras listed
  with it; `choose(patches, want)` picks across them (a tie between patches goes to the
  articulation that lists the base first). `usedPatches()` runs the notation once so only
  needed extras get a route (each is a Kontakt instance, ~0.7 GB). `setAvailable()` (set in
  `musescore.cpp` for plugin mode: has a setup) keeps an unset extra out; `routesGeneration()`
  makes the renderer rebuild when a setup is saved (`SoundLibraryHost::routesMayChange`).
- Kits (`kit="1"`): MuseScore's unpitched percussion; no patch of its own (the host loads
  nothing on its route). Its extras' `<Drum pitch key>` entries give each drum sound's patch and
  key (`SoundLib::drum()`); an unmapped sound stays on the built-in synthesizer (event patch
  tag -1: `finishLibraryEvents` leaves it unrouted, and its preset and CC11 go there too). A
  kit none of whose patches has a sound of the part is not routed at all.
- Key names from the plug-in (`Vst3Plugin::keyNames`): the program's pitch names (`IUnitInfo`, what
  Cubase shows as a drum map) and keyswitches (`IKeyswitchController`, "KS " prefix). The key scan
  asks for them first: results.json `keyNamesSource` and per key `pluginName`, the sheet labels,
  and a list in summary.txt. The test synth has both (Kick/Snare/Hi-Hat Closed, KS Legato/Staccato),
  tested in `tst_soundlibrary::vst3Plugin`. Kontakt 8 answers neither (tenth run, 2026-09-26:
  16 program lists with no pitch names, no IKeyswitchController), so the kits' keys came from
  the owner's screenshots of each drum's hit list in Kickstart (see Kits below).
- Key scan (`ArticulationCheckDialog::checkKeys`, for `keyScan="1"` patches): each key 0-127
  played; a picture while it sounds and one after release; a key that sounds (within 50 dB of
  the loudest) or a silent key whose release picture differs from the previous key's (beyond
  the window's own noise: a keyswitch) goes on the sheet and in results.json "keys"
  (`keyswitch`, `map`, `mapKeyswitch`). SSO's tuned percussion (Timpani … Desk Bells, Kickstart
  patches) has no UACC, only keyswitches from C-2: Timpani is mapped by key from the owner's
  screenshot (0 Timpani, 1 Muted, 2 Roll, 3 Roll Muted, 4 Swell mf, 5 Swell f); the others from
  their key scans (Tubular Bells 0 / 1, see the ninth run).
- Renderer: library parts play on the instrument's first channel. Each note and switch carries
  its patch (`NPlayEvent::libraryPatch`); `finishLibraryEvents` routes by channel and patch
  (`libRoutes`: channel -> port/channel per patch), drops redundant switches per patch, and
  copies the part's controllers (dynamics, pedal) to every patch. An articulation switch
  (`NPlayEvent::librarySwitch`) goes before each note. Events carry the route
  (`NPlayEvent::setExternal(port, channel)`); the old duplicate-controller pass compares routes,
  not channels. A legato articulation's note lasts DIVISION/16 into the next (Spitfire legato
  needs the overlap). A sampled trill or tremolo plays the note once (`SndConfig::ms4Once`).
  Dynamics go on the library's CC (CC1 for Spitfire).
- Controllers (the way extracted plug-in data reaches playback; README › Controllers):
  `SoundLib::Controller` (map `<Controller>`, library-wide or per `Instrument`, merged into
  `LibInstrument::allControllers` by id) is a MIDI CC or a plug-in parameter by title, 0-127,
  with a default and staff texts (`<Text match value>`, CC only). A part's values:
  `libmscore/partcontrollers.*`, metaTag `partControllers` (by controller id; parts found by
  `PartPlaybackModes::findPart`). The renderer (`renderMs4Dynamics`, `LibPart::controllers`)
  sends each CC's value at every chunk's start, ahead of the notes, and the staff text's changes;
  `finishLibraryEvents` copies them to the part's extra patches. Parameters are set on the hosted
  instances by `SoundLibraryHost::sync` (and the command-line export), `applyParameters`, via
  `Vst3Plugin::parameterId(title)`. UI: *View › Sound Library…* › *Controllers…* per part
  (undoable). From an extract: `tools/soundlibraries/controllers_from_extract.py <folder>` prints
  suggested `CONTROLLERS` / `PATCH_CONTROLLERS` lines for `gen_spitfire_sso.py`. Test:
  `tst_soundlibrary::controllers`. SSO's map has them since 2026-09-27 (the owner: "build the
  controls"), all as Kontakt parameters by title (`PATCH_CONTROLLERS` by family, from the second
  plug-in extract): strings Vibrato, Release, Tightness, Mic 1-5, Mic Mix Distance; woodwinds Vibrato,
  Release, Variation, Mic 1-4, Mic Mix Distance; brass Release, Tightness, Variation, Mic 1-4, Mic Mix
  Distance; Performance extras without Release, strings' with Mute; Grand Piano Pedal Vol, Pedal Dyn,
  Mic 1-4, Mic Mix Distance; the kits Releases, Variation, Mic 1-3. No defaults (the patch keeps its
  own until a part has a value). Not Dynamics, Expression (MuseScore's CC1 / CC11) nor Articulation
  Controller (UACC). The titles are guesses from the extract's summary ("Mic 1 level" …):
  `Vst3Plugin::parameterId` matches loosely (case, spacing, punctuation, a slot number in front), and
  the Controllers window lists the controls of all the part's patches and says "not in <patch>"
  for a loaded patch without that title. Tried by the owner on run 91 (2026-09-27): no "not in",
  Vibrato works; Mic 1-5 are Close, Tree, Ambient, Outrigger, Leader; Mic Mix Distance sets all
  five faders (0: Ambient only, 127: Close only), so it is applied first and a mic level ticked wins.
  **Values belong to the score; the library's default is SSO's own** (the owner): a parameter a
  score doesn't set is put back to the patch's value when another score set it on the same loaded
  instance (`Slot::patchValues`), and *Save setup* saves the patch's values, not the score's.
- Output: `Seq::putEvent` sends external events to the MIDI driver (`Driver::canOutputMidi`;
  PortMidi outputs A–D in `audiodrivers/pm.cpp`) or to the hosted plugin (see below). The
  preference is `io/soundLibrary`, set in Preferences › I/O › Sound library (`prefsdialog.*`).
- `share/soundlibraries/Spitfire Symphony Orchestra.xml`: generated by
  `tools/soundlibraries/gen_spitfire_sso.py` from the community Reaticulate bank, with
  corrections from Spitfire's own Cubase expression maps for SSS, SSB and SSW (the owner
  uploaded them; they are legacy downloads and stay out of the repository).
  `tools/soundlibraries/check_spitfire_expressionmaps.py <folder>` compares the two: 367 of
  399 numbers match Spitfire's. The rest can only be checked in Kontakt: 6 that no Spitfire
  map mentions, plus the solo strings and harp, which have no Spitfire map.

## Building (Linux container)

```sh
sudo apt-get install -y qtbase5-dev qtbase5-private-dev qttools5-dev qttools5-dev-tools \
  qtdeclarative5-dev qtscript5-dev libqt5svg5-dev libqt5xmlpatterns5-dev qtwebengine5-dev \
  libqt5opengl5-dev qtquickcontrols2-5-dev libqt5networkauth5-dev libsndfile1-dev \
  libasound2-dev portaudio19-dev libportmidi-dev libpulse-dev libmp3lame-dev \
  libfreetype6-dev zlib1g-dev libvorbis-dev libogg-dev libflac-dev
mkdir build.dir && cd build.dir
cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_JACK=OFF -DBUILD_TELEMETRY_MODULE=OFF \
  -DDOWNLOAD_SOUNDFONT=OFF -DBUILD_WEBENGINE=OFF /path/to/repo
ninja -j4 mscore                    # a full build takes about 40 minutes on 4 cores
```

- Keep `BUILD_PCH` ON (the default). Many files compile only with the precompiled header.
- mtests (`add_subdirectory(mtest EXCLUDE_FROM_ALL)`): build with `ninja tst_<name>`.
  `testutils` now links freetype for `ft2build.h`. The old `CPATH=…/freetype-2.14.1/include`
  workaround is no longer needed.
- Run tests with `QT_QPA_PLATFORM=offscreen ./tst_<name>` from
  `build.dir/mtest/libmscore/<name>`.
- A score imported from MusicXML in a test needs `score->rebuildMidiMapping()` before
  rendering. Until then its channels are -1, and rendering crashes.
- Linux defines both `USE_ALSA` and `USE_PORTMIDI`. PortMidi wins, so the PortMidi code
  (the owner's Windows path) compiles here too.

## Tests and known state

- `mtest/libmscore/soundlibrary` (`tst_soundlibrary`): text techniques, `choose`, the
  Spitfire map's instrument matching, and a rendered MusicXML score (the switch per note,
  routing, sampled ornaments). All tests pass.
- `mtest/libmscore/tuning` (`tst_tuning`): the built-in tuning (see "Tuning"). All 13 pass.
- `mtest/libmscore/midi` (`tst_midi`): **68 of 73 fail**, and they failed before the
  sound-library work too. The references predate the MS4 note model. Same-tick event order
  varies between runs because `ms4Parts` is keyed by pointer. Don't read these failures as
  regressions. Compare against a build of the previous commit instead.
- There is no way to hear Kontakt, SSO or Windows here. Anything about them is either untested
  or tested with a stand-in; say which when you report.

## Plugin hosting (VST3)

This mode applies when Preferences › I/O › Sound library › Play through is set to the
library's plug-in, which is the default when `BUILD_VST3` is on. The default is ON except on
macOS.

- `thirdparty/vst3sdk`: a subset of the VST 3 SDK v3.8.1 (MIT). Its own CMake builds
  `vst3sdk_hosting` and, for the tests, `vst3sdk_plugin`.
- `audio/vst3/vst3plugin.*`: one hosted instrument instance. MIDI arrives as note events and
  as `IMidiMapping` parameters (CC32 UACC and CC1 reach Kontakt this way). It also handles
  state, offline mode and the editor view.
- `audio/vst3/vst3synth.*`: `Vst3Synth`, registered in the live `synti` only
  (`musescore.cpp`). It has 64 slots, one per route (slot = port * 16 + channel). It is
  "dry", mixed in after the master effects (`MasterSynthesizer::process`). Export borrows it
  through `MasterSynthesizer::addGuest` and `beginExport` (offline mode, exporting thread
  only).
- `mscore/soundlibraryhost.*`: `SoundLibraryHost::sync(score)` runs at `Seq::start`, in
  exports and in the dialog. It loads an instance per route and restores the instrument's
  setup from `<dataPath>/soundlibraries/<library>/<instrument>.vst3state`.
  `SoundLibraryExport` handles audio export. Without a sequencer (command-line `-o`), it
  loads its own instances. `SoundLibraryDialog` is *View › Sound Library…*: parts, patches,
  Show (the plug-in's editor) and Save setup.
- Loading ahead (`SoundLibraryHost::preloadSoon`, from `MuseScore::setCurrentScoreView`): 2 s after
  a score is shown, its instances load one per event-loop turn (`syncSome(…, 1, &remaining)`, status
  bar "Loading … in the background"); a play before that's done loads the rest (`sync`). The owner,
  2026-09-27: a full orchestra (25+ Kontakt instances, ~0.7 GB each at Kontakt's default 60 kB
  preload) made the first play wait long. Tried here under Xvfb with the test synth: 5 parts, 5
  instances loaded one by one (`qDebug` "preloaded one instance, n to go"). Instances are reused
  across scores (`syncSome`): one the score can't use in its slot is set aside (`_spares`), a patch a
  spare plays moves to the slot that needs it ("<patch> kept (slot n)"), a spare of a patch no longer
  needed takes a new patch when it has a setup (one instance fewer), and the spares left are
  released once all is loaded. Tried with two scores sharing 3 of their 4-5 patches in another
  order: switching loads only the other 1-2. Status messages (`MuseScore::showMessage`) are in a
  label of their own and repainted at once: QStatusBar's own messages never showed in MuseScore 3
  (its stretching spacers leave them no room). **Crackle live, not in
  the export: memory.** With 40 GB at 89 %, the owner's playback crackled at start and stop;
  Kontakt's *Options › Memory › Override instrument's preload size* at 30 kB fixed it. Suggest that
  first when the owner reports crackles or a slow load.
- `mscore/vst3editor.*`: the plug-in's editor window (HWND, NSView or X11 plus IRunLoop).
- `Seq::putEvent`: in plugin mode, external events go to `Vst3Synth` with the slot as the
  channel.
- Plug-in modules (`vst3plugin.cpp`, `modules()`) stay loaded until exit, as in DAWs.
  Unloading sfizz and loading it again hung MuseScore (pango types registered in GLib twice).

Articulation check (*View › Sound Library…* › *Check articulations…*; this checks the map
against the plug-in itself):
- `audio/vst3/articulationcheck.*`: `ArticulationCheck::run(plugin, values, settings)`.
  Offline, on one instance, it tells whether each map value switches the plug-in. Each
  value's note is played twice, once after reference A and once after reference B. Both
  notes the same means the value switched; one like A and one like B means it was ignored.
  Ratio thresholds are 0.5 and 0.8. It also flags silent values, a patch that doesn't switch
  at all, and "sounds like": two values that sound the same. That catches a plug-in playing
  a default for values it lacks. The header has the details.
- `mscore/soundlibrarycheck.*`: `ArticulationCheckDialog`.
  - *Set up…* sets up a patch on an instance of its own, with no score needed.
    `SoundLibraryHost::setupChanged` reloads that setup into the live instances.
  - *Check* runs over each ticked, set-up patch. It grabs the plug-in's window after each
    switch (PrintWindow on Windows, else QScreen), crops to the region that changed and
    draws a contact sheet `<patch>.png` labelled with value, map name and sound verdict. If
    the window doesn't change on a switch alone, it retakes the pictures with the note
    playing. Then it runs the audio check.
  - Output goes to `Documents/MuseScore Sound Library Check/<library> <date>/`: sheets,
    `results.json` and `summary.txt`, plus a `.zip` of the folder next to it. The owner is
    asked to hand back that zip. **When they do**, run
    `tools/soundlibraries/read_check_names.py <folder>`. It OCRs each picture's articulation
    name, compares it with the map and lists mismatches and "None". Then look at the sheets
    for what it flags, and fix the map through `gen_spitfire_sso.py`. The OCR misreads
    (Long → "Large").
  - Expected verdicts: `<Articulation … expect="silent|ignored|unclear">` (EXPECT in
    gen_spitfire_sso.py: the eighth run's reviewed exceptions) passes a value with that verdict
    (ignored / unclear also with "switches"). When the dialog opens, a last check recorded as
    problems whose counts are exactly the expected ones, or a kit patch's check (from before the
    map had its keys) that heard every mapped key, becomes passed (`SoundLib::checkedAsExpected`,
    `acceptExpected`; same setup and CHECK_VERSION only). Test `checkedAsExpected` uses the
    owner's lines of 2026-09-26 08:43. Triangle 1 (open 103, closed 107) didn't sound in that
    run (Instrument Active off); in the 09:12 run it did (103-107, Triangle 2 108-112).
  - Memory: `<dataPath>/soundlibraries/<library>/checks.json` holds each patch's last check:
    setup SHA-1, map-entry SHA-1 (values and names), `CHECK_VERSION`, and a result of
    passed, problems or error. A patch needs checking only if something changed or its check
    errored. Raise `CHECK_VERSION` (soundlibrarycheck.cpp) when a change to the check makes
    old results stale.
  - Scan (checkbox, and always for patches added with *Add a patch…*, kept in
    `addedpatches.json`): pictures of CC 0–127. The picture most values share is "no
    articulation" (candidates 0, 127, 126, 99, 64). Two pictures count as the same when
    fewer pixels differ than max(30, 3 × the base-versus-again noise). Found values are
    listened to. `results.json` gets `found`, `notInMap`, `mapValuesShowingNone` and
    `sheet` (the sheet's order). Not tried with Kontakt yet.

Extract plug-in data (*Check articulations…* › *Extract plug-in data*; the owner, 2026-09-27: "extract all
data possible from the SSO plugin, I need way more control of the plugin"):
- `Vst3Plugin::describe()` (`audio/vst3/vst3plugin.cpp`): everything VST 3 lets a host ask. Module
  (moduleinfo.json, snapshots, factory, classes, the compatibility class's JSON), which of ~60 optional
  interfaces the component and controller have, buses and arrangements, latency, tail, process-context
  needs, every parameter (title, units, steps, flags, unit, value, default, the text of every step or of 17
  points), the MIDI mapping on every event bus and channel (0-129 plus program change, poly pressure) and
  IMidiMapping2, units, program lists (names, preset attributes, pitch names, program data), unit data,
  keyswitches and note expressions per channel, physical UI mapping, orchestral articulation info
  (VST 3.8.1), parameter functions, the XML representation, the editor (size, platforms), the component's
  and controller's state. `hostQueries()`: what plug-ins asked MuseScore for (host context, component
  handler, IPlugInterfaceSupport), logged since start.
- `PluginExtract` (`audio/vst3/pluginextract.*`), with *Try every controller* ticked: on each ticked patch,
  with its window open and a note held, every CC 0-119 (not the switch), channel pressure and pitch bend at
  0 then 127: window pixels and region, level and brightness, parameters changed, parameters the plug-in
  reported (processor output, performEdit). Then the value that looks (else sounds) like before, searched
  (the patch's own value, `patchValue`), is sent back; a CC with no effect goes back to its parameter's
  value. Parameters that change by themselves (meters) are learnt first and left out. Then the parameters
  no CC maps to (not families of more than 8 alike: Kontakt's placeholders), each at 0, 1 and back. Then
  which parameters each articulation value changes. Sheet `<patch> controllers.png`.
- Output: `Documents/MuseScore Sound Library Check/<library> extract <date>/` (`plugin.json`, `<patch>.json`,
  state `.bin`s, sheet, window, summary.txt) and a zip. **When the owner hands it back**, run
  `tools/soundlibraries/read_plugin_data.py <folder or zip> [--full]`: it prints (and writes report.txt)
  the plug-in, its parameters by family, the mapping, programs, what each patch changed against the empty
  plug-in, what each CC and parameter does, and the state blobs' zlib streams and strings (`_decoded/`).
- Tested with the test synth (`tst_soundlibrary::pluginDescribe`, `pluginExtract`: it now has a "Tone"
  parameter no CC maps to and twelve placeholder "Macro n"). In the GUI under Xvfb with sfizz (Solo Violin 1
  set to the UACC SFZ, *Try every controller*, 7 minutes): 543 parameters (512 "Controller n" and 16 "Level n"
  placeholders), 130 CCs mapped per channel; CC 1, 7 (its Volume knob), 10 (its Pan knob), 11, 64, 66 and
  pitch bend found, put back at 100/104, 100, 64, 127, 0, 0, 64; parameters Volume, Polyphony, Preload size,
  Tuning frequency; its meters ("Level 1/2") learnt as changing by themselves. Things learnt on the way:
  each value needs a new note (a decaying sample), a sound search needs notes averaged (round robins), and
  pan needs the stereo balance. Not run with Kontakt yet.
- Without the GUI, on any plug-in (offline, no window pictures): `MS_EXTRACT_PLUGIN=<.vst3>
  MS_EXTRACT_OUT=<file.json> [MS_EXTRACT_STATE=<.vst3state>] [MS_EXTRACT_TRY=1] ./tst_soundlibrary
  externalPlugin` (skipped when unset).
- First run on SSO (the owner, 2026-09-27 03:58, run 72, *Try every controller* on Violins 1 only; it
  takes very long per patch, and Strings Ensemble was stopped): Kontakt 8.9 exposes 4145 parameters, most
  placeholders ("##" x2033, "CC #n" per channel); Violins 1 - All techniques names 15 automation slots:
  0 Dynamics, 1 Vibrato, 3 Release, 4 Tightness, 6 Expression, 7-11 Mic 1-5 level, 13 Articulation
  Controller, 14 Mic Mix Distance (2, 5 "Unused CC", 12 "Unused mic"). Those can be driven as plug-in
  parameters by title (`<Controller param=…>`), no CC needed. 25 CCs change something (1, 7, 10, 11, 17,
  21-25, 103, 105-114, 116, 118, 119, pitch bend), but Kontakt reports no named parameter moving with them
  (the CC to knob link is in Spitfire's script) and the sheet's crops are too small to read which slider
  moved. Kontakt's state (`component.bin`, the `.vst3state` setup) is an NI container like an `.nki`: a
  Kontakt multi (BANK, SLOT_LIST) that embeds the whole patch (Violins 1: 1.9 MB, 26.8 MB of preset data
  unpacked, authorization N51), not a reference to it; `read_kontakt_preset` doesn't read its slot list
  yet ("read past the end"). So setups could in principle be generated from the `.nki` files (a multi with
  the patch in slot 1), keeping what the owner sets at setup (UACC & UI only) from a template; not tried.
- Second run (04:20, describe only, 12 patches): the named slots differ by family and so do their numbers
  (found by title, so that doesn't matter; a patch lacking one logs a warning and skips it). Orchestral
  (Flutes a2, Horn Solo, Horn Solo Short Staccatissimo, Violins 1 Long Sul G, Violins 1 Performance, and the
  Grand Piano): Dynamics 0, Expression 6, Mic 1-4 (strings also 5) level 7-11, Articulation Controller 13, Mic
  Mix Distance 14, and by patch Vibrato 1 (strings, woodwinds), Release 3 (not Performance nor the
  staccatissimo), Tightness 4 (Violins 1, horns), Variation 5 (Flutes a2, Horn Solo), Mute 2 (Violins 1
  Performance), Pedal Vol 2 / Pedal Dyn 3 (Grand Piano). Kits (Drums High / Low, Unpitched Metal / Wood,
  Toys): Dynamics 0, Releases 1, Variation 2, Expression 3, Mic 1-3 level 4-6. Not yet in the map
  (`PATCH_CONTROLLERS`): which mic "Mic n" is (the patches' NKS page lists Close, Tree, Ambient, Outrigger,
  Leader in that order, likely the same) is unconfirmed.

Setups made from the `.nki` files (`tools/soundlibraries/make_setups.py`, `MakeSetups.exe` from the same
workflow; the owner, 2026-09-27: set up every SSO patch without loading each by hand, then extract from all):
a setup (`.vst3state`: "MSV3", name, Kontakt's component state, controller state) holds an NI container
whose preset is a Kontakt multi: BANK (SAVE_SETTINGS, five multi-script slots, OUTPUT_CONFIGURATION,
SLOT_LIST, MULTI_CONFIGURATION, 0x49), 0xF02 (Kontakt 8's browser state) and the bank's FILENAME_LIST_EX
(version 3, absolute paths). SLOT_LIST = 8 bytes of used slots, then PROGRAM_CONTAINER (0x2B, SAVE_SETTINGS,
PROGRAM_LIST = count + the program). Compared on Violins 1 - All techniques (the owner's `.nki`, Kontakt
7.5.2, and its setup, Kontakt 8.9): the program is the `.nki`'s (1219 groups, 32470 zones identical, only
the zones' sample numbers shifted by the bank list's first entries), re-saved by 8.9 (a longer header,
parameter arrays; the main script's code newer, from the library's resources), and the script's saved
values (after its code, "<u32 length><$name value>") differ in exactly three: `$zdiqz` 0 → 1, `$iooxo` 0 → 3
(the owner's "UACC & UI only", most likely) and `$stgrp` 127 → 100. So a setup = a hand-made setup as the
template (the multi around the program, kept byte for byte), with the slot's program replaced by the
`.nki`'s, the values the owner changed in the template (same slot, name and length) set in it, and the
`.nki`'s sample list (version 2) with every path made absolute from the `.nki`'s folder. The script code
is copied as it is, never read out. Compressed with `fastlz_compress` (Violins 1: 26.3 MB → 2.5 MB in
1.7 s; Kontakt's 1.9 MB). Map patches get their map name (`MAP_ALIASES` for the `.nki` names that differ:
Tuned - …, Piccolo Flute, Solo Violin, Ensembles …), the others their `.nki` name and an entry in
`addedpatches.json`; `generated.json` lists what was made (never used as templates). Hand-made setups are
kept unless `--overwrite` (then `.bak`). Tests `test_make_setups.py` (7: built files; with `SSO_NKI=` /
`SSO_SETUP=` the owner's Violins 1: both files read and written back byte for byte, the three values
found, a generated setup reads back). The owner loaded a generated "Violins 1 - Core techniques" with run 91
(2026-09-27): "everything looks correct". Without a template (`--empty`, or found by itself: the newest
Extract plug-in data's `plugin component.bin`, Kontakt with nothing loaded): `make_component_from_empty` puts the
`.nki`'s program in slot 1 of the empty multi, copies its Authorization and BNI header fields (156-177, byte 36),
sets MULTI_CONFIGURATION byte 0 to 0 and the absolute file list; against the owner's hand-made Violins 1 only the
save time, patch and bank uuids differ (test `from_empty`, and `SSO_EMPTY=` in `real`). Plain defaults don't switch
by UACC, so each family's switching values must be applied: the settings learnt from the hand-made setups are
written to `learned_settings.json` (setups folder). The owner's, 2026-09-27 (all hand-made setups; the key is the
PAR_SCRIPT's child index in the program, 20 the main script): `$iooxo` 3 in every orchestral patch (strings, woodwinds,
brass, harp, piano, Performance and single-technique patches alike): the switching mode, "UACC & UI only", the one
setting MuseScore applies. `$zdiqz` 1 and `$rhlp3` 1/2/8 in some patches with no pattern by family (most likely the
last selected articulation: state, not a setting); `$m3gq2` 0 in the woodwind and brass Performance patches' script 19
(unknown; those take no switching); the kits' values are the techniques the owner had switched on (dropped, see Kits);
tuned percussion changed nothing. The owner: everything else at the library's defaults, hand-made setups discarded.
Owner, 2026-09-27: move this into MuseScore and drop manual set-up (not done yet).

Extract library files (`tools/soundlibraries/extract_library_files.py`; the owner, 2026-09-27: "completely
extract all data possible from SSO so we can fully integrate it into Muse in the future"): the files'
half, without Kontakt. Python 3.8+, standard library only; the owner runs `ExtractLibraryFiles.exe` (artifact
`ExtractLibraryFiles-win64` of the workflow "Tool: Extract library files", `tool_extract_library_files.yml`:
tests, a try on the runner, PyInstaller; it runs on pushes that change the extractor). With no argument it
finds the library (registry `Native Instruments\<library>` ContentDir, `--match "spitfire|symphon"`) or asks
for the folder. It reads:
- Kontakt 5+ `.nki .nkm .nkb .nksn` (NI container, "hsin" items, FastLZ sub-trees): application and
  version, library SNPIDs, Soundinfo (name, author, vendor, description, tags, attributes, properties), the
  container tree; when the preset data isn't encrypted, programs (key range, default keyswitch, library id
  …), groups (name, release trigger, voice group …), zones (keys, velocities, root, sample, rate, length,
  loops) and the sample file list;
- NI file containers (`.nicnt` with its ProductHints XML, monolith `.nki`, some `.nkr`): table of contents,
  instruments and data files inside; the older NKS archives (`.nkx` sample monoliths, `.nkr`, `.nkc`): every
  entry's name (sample names: articulation, dynamic, round robin, note) and the sizes stored plainly;
- NKS presets (`.nksf`, RIFF NIKS: NISI summary, NICA knob pages, PLID), loose WAVs (format, loops), `.nka`
  arrays, XML/JSON/text; on Windows also NI's registry keys, Native Access's Service Center XMLs, NI's
  databases (rows naming the library) and Spitfire's folders.
Formats as documented by ConvertWithMoss (LGPL) and PresetConverter, re-written, not copied. On purpose it
decrypts nothing (encrypted presets, samples and resources: only what their headers say in the clear) and
copies no script source (KSP in PAR_SCRIPT or a `scripts` folder: counted only); serials, licences, tokens,
e-mails and NI's HU/JDX key values are left out, the home folder is written `%USERPROFILE%`. Output:
`Documents/MuseScore Sound Library Check/<library> files <date>/` (`library.json`, `inventory.csv`,
`summary.txt`, `archives/<archive>.txt` with the names inside each, `extracted/`) and a zip. **When the owner
hands it back**, `extract_library_files.py --report <zip>` prints the summary; the rest is in `library.json`.
By default each program's zones are summed up per group (`compact_program`: its place in the group tree,
zone count, key and velocity ranges, roots, one sample's name); `--zones` keeps every zone and the container
trees (about 800 MB of JSON for SSO). The hand-back zip is LZMA and, over the chat's 30 MB limit, split into
"<folder> part n of N.zip" (a file too big alone in `.partNN` pieces; `--report` and `join_parts` put them
together). Tested with `test_extract_library_files.py` (14: files built in each layout, the group tree, the
split; with `KONTAKT_NKI=` a real Kontakt 6.7 `.nki`, ConvertWithMoss's template: 61 zones, 61 samples, 5
script slots).
First run on SSO (the owner, 2026-09-27 03:27, with the first build, library.json left out as 800 MB):
**all 700 `.nki` are readable, none encrypted** (SNPID N51, saved with Kontakt 7.5.2, library 1.4.0;
ProductHints: RAS3, NKS-enabled), although SSO plays in the free Player. So every patch's groups and zones
are there: Violins 1 - All techniques has 1219 groups and 32470 zones, keys 36-109; the group names spell
out the tree: mic header (`####### Close #######` …), articulation (`Long`, `Long CS`, `Long Harmonics` …),
variant (`Non Vib`, `Vib`, `Molto Vib`), then round robin and dynamic (`rr1 sus p`) and release triggers
(`sus p rt`). The 279 `.nkx` archives list 432,829 sample names (e.g.
`BML205_SoloBassTromb_dbltongue_ff_RR1_a_A#2.ncw`: articulation, dynamic, round robin, note; the samples
themselves encrypted). It also found BBC Symphony Orchestra's NKS presets (the `symphon` match; the owner has
it): one page Expression 0, Dynamics 1, Reverb 2, Global Tune 13, Pan 12, Gain 11. komplete.db3 has the 700
sound-info rows. The first build's slot-list reading gave an error on every patch (fixed: only for banks) and
wrote all of Kontakt 8's registry key (now only the values naming the library).

Tested here:
- `tst_soundlibrary` hosts `mstestsynth.vst3` (`mtest/libmscore/soundlibrary/testsynth`), a
  sine synth that maps CC32 and CC1 like Kontakt. The tests cover notes, CC mapping, state,
  offline mode and a rendered score.
- With sfizz, a real third-party VST 3 sampler (`tools/soundlibraries/sfizz-test`), a
  command-line export played 12/12 violin notes on the expected UACC articulation. The
  tremolo and trill samples each played once.
- In the GUI under Xvfb, *View › Sound Library…* › *Show* embedded sfizz's editor (X11
  plus IRunLoop), sized to fit. Clicks and redraws work, and closing and reopening work. The
  first-time flow works too: load the instrument in the plug-in's window, close it, and the
  setup is saved and marked "Ready".
- Articulation check:
  - `tst_soundlibrary::articulationCheck` uses the test synth, which gives each articulation
    its own timbre and has round robins. Values missing from its patch are ignored (90–127)
    or play a default (85–89). The check tells them apart from those that switch, plus a
    silent value and a patch switched on the wrong CC.
  - In the GUI with sfizz, *Set up…* then *Check* on Solo Violin 1 plus a bogus value 99:
    6 switch, 99 silent (the SFZ has no region for it), a sheet and a zip. *Stop* works.
    sfizz's window doesn't show CC32, so its pictures were retaken with the note playing.
  - `tst_soundlibrary::scanPictures`: 128 synthetic SSO-like pictures ("None / NO ACTIVE
    TECHNIQUE" or a name, a flickering meter, a memory display that grows during the scan).
    It finds exactly the 7 articulations. Without the after-scan picture of the start state,
    the growing display makes it fail.
  - Memory in the GUI with sfizz: a patch shows "Passed (date)" after its check and is
    unticked. That survives a restart. Changing its setup file shows "Its setup changed
    since the check of …" and ticks it; raising CHECK_VERSION shows "The check changed …".
  - Scan with sfizz is "inconclusive" (sfizz's window doesn't show the articulation). The
    map values are then only listened to. An added patch ("Trombones a5" with the Solo
    Violin 1 setup) loads (UACC 1 is tried when nothing is known) and is scanned; nothing
    was found, so it too is inconclusive.
  - Safety rules, because a wrong "missing" is worse than none: the scan is inconclusive
    when pictures needed a note, when nothing was found, or when more than half the map's
    values would show "None".

**Tried by the owner (Windows, Kontakt Player 8, SSO), 2026-09-25: first Check articulations run on
Violins 1 ("Violins 1 - All techniques", set to "UACC & UI only").**
- The MSVC build ran. *Set up…*, the Kontakt editor (HWND) and the pictures worked.
- Kontakt's window shows each switch. The patch prints the selected articulation's name,
  with "UACC CC# n" under it. The crop found that region.
- All 22 Violins 1 values match the map, by name and by Kontakt's own UACC number.
- The listening check was all silence (−200 dB): Kontakt played in real time but not after
  `setOffline(true)`. Cause: `Vst3PluginPrivate::setup()` switched off every bus when it ran
  a second time. Fixed; the test synth now honours bus activation like Kontakt, and the
  tests caught it. The same bug would have made audio export with Kontakt silent. The check
  now waits for an offline note to sound (up to a minute) before listening.

- Second run, with the bus fix: listening worked offline. 20 of 22 values "switch", with
  ratios 0 to 0.12. Short Harmonics (61) was silent and Long Harmonics (10) −73 dB at the
  test pitch (B4); the pictures show both selected correctly, so the harmonics most likely
  just have no sample there. The −73 dB note was normalised to the others' loudness, so
  noise was compared: "ignored" was wrong, and it was even picked as reference A. Fixed: a
  note 40 dB under the patch's loudest counts as having no sound at that pitch. It is never
  a reference, and it is re-tested at other pitches of the instrument's range (octave, fifth
  …), reported as "at pitch n". Test synth value 25 reproduces this.
- Third run (build 8): **Violins 1 22/22 switch**, confirmed by picture and by ear. Both
  harmonics were tested at pitch 83 and switch. Ratios 0–0.32, the highest being
  Pizzicato (round robins), so still well under 0.5. Nothing sounded like anything else.
  Patches still to check: the other 41.

- Fourth run, all 42 patches (10 minutes):
  - Every value's picture shows the map's articulation, except that Legato (20) in all 25
    woodwind and brass patches and the Trumpets a6 trills (70, 71) show SSO's "None, no
    active technique" and play nothing. Those were additions from Spitfire's legacy
    SSW/SSB maps and are now removed from the map. The six values no Spitfire map
    mentioned, and the solo strings and harp, all match.
  - Oboe Solo was left on "Normal keyswitching": its pictures say "KEYSWITCH C0". Solo
    Cello played nothing (not set up right). Both still need checking.
  - The "sounds like" hints were false alarms (trills a second apart, Tenuto and Marcato),
    so they are no longer listed as problems. Violins 2's Long Super Sul Tasto sits 42 dB
    under the patch's loudest, so the "no sound here" threshold is now 50 dB.
  - A few "ignored"/"unclear" verdicts on falls, rips and harmonics had pictures that
    confirm the right articulation: audio is the weaker evidence for those.
  - The owner's Kontakt lists patches the map lacks (Trombones a5, piano …) and
    articulations it lacks (Flute: Long Hollow, Multi Tongued): hence *Add a patch…* and
    the scan.

- Fifth run, all 42 patches with *Scan every value*:
  - Oboe Solo (now on UACC) and Solo Cello (set up again) pass; every mapped value of every
    patch switches and shows the map's name (the OCR's "Large"/"Terait" are Long/Tenuto).
  - Every articulation the scan found is in the Reaticulate bank with the same UACC number.
    The map had left them out because no notation asked for them. Now in the map, through
    new staff-text modifiers: Long Sul C (Violas), Bells up Long/Crotchet/Staccato (Horns a2),
    PDLT (Harp), Multi Tongued (winds and brass, "double/triple tongue" + tremolo).
  - Articulations no notation names are in the map too, with no techniques (never chosen,
    but listed and checked; the owner wants them kept for later): Long CS Blend, Long
    (Rachm.), Short 0.5, Short Brushed (CS), Trem (CS) MS 150/180 BPM (measured), Fx, Staccato
    Dig, Short Spicc-Pizz, Long Sul Pont (Dist), Long Sul String, Long Hollow, Long/Short
    Overblown, Marcato SFZ, Bells up Quaver, Long Mariachi, Fx Glissandi, Slid (harp).
  - Violins 1/2 Long Sul G and Celli Long Sul C: silent in the "All techniques" patches at
    every pitch tried (71, then 83, 59, 78, 64, 90, 52, 95, 76, 66: several inside Sul G's
    blue key range, G3 to about C5), although Kontakt shows them selected on UACC 112. The
    owner confirmed it by ear; the single "Violins 1 - Long Sul G" patch plays. So it is in
    the patch, not our switching. The owner played it in Kontakt: Kontakt's "Voices" stays
    at 0 (its load chip changed nothing), so that patch maps no samples to Long Sul G: a
    fault of Spitfire's patch. The same for Celli Long Sul C (Voices 0 in "Celli - All
    techniques", the single Long Sul C patch plays). Listed with no techniques (SILENT in
    gen_spitfire_sso.py); a part that needs them would have to use the single patches.
- The owner's SSO folder (`.nki` list, 2026-09-25): besides the mapped "All techniques"
  patches there are Motif Brass (Horns a4, Trombones a5, Trumpets a3 "All techniques"), the
  percussion (Timpani, Celeste, Glockenspiel, Xylophone, Marimba, Vibraphone, Crotales,
  Tubular Bells, Desk Bells, drums, unpitched), "Other - Grand Piano" and "Other - Harp
  glissandi", plus Performance / Core / Decorative / single-technique patches (574).
  Added to the map (not yet checked in Kontakt): Motif Horns a4 / Trumpets a3 / Trombones
  a5 (chosen for parts named a4 / a3 / a5; their six articulations from the single-technique
  patch names, numbered by the UACC standard: Long 1, Staccato 40, Staccatissimo 42, Tenuto
  50, Marcato 52, Multitongue 75), and one-sound entries (value 1, every technique) for Grand
  Piano, Timpani, Celeste, Glockenspiel, Xylophone, Marimba, Vibraphone, Crotales, Tubular
  Bells, Desk Bells. Not mapped (checked against all 700 .nki names; only Core / Decorative
  techniques are true subsets of All techniques):
  - the 22 Performance / Total Performance patches: Spitfire's legato (All techniques has
    none), Sul G / Sul C Performance, Oboe Principal (another player);
  - 13 single-technique patches whose articulation no All techniques patch has (the full
    scan agrees): Horn Solo / Horns a2 Legato and Staccatissimo, Horns a2 Bells up
    Staccatissimo, Fanfare (Horns a6, Trombones a6, Trumpets a2 / a6), Trumpet Solo Fall /
    Rip Muted, Cimbassi Long Alt, Violins 1 Long Sul Pont Distorted; plus the single Long
    Sul G / Sul C patches that do play;
  - drum, unpitched and toy percussion and the percussion ensembles (need a per-key drum
    map), Harp glissandi, the Curated Ensembles (blends, no MuseScore instrument).
  Groups 1 and 2 are now extra patches (57: every Performance patch as legato, UACC 20 a guess;
  Sul G / Sul C Performance and Long; the single techniques; reference-only ones). The unpitched
  percussion is a kit with Drums - High / Low, Unpitched - Metal / Wood, Other - Toys. Their keys
  (DRUMS in gen_spitfire_sso.py, 2026-09-26) come from the owner's Kickstart screenshots of
  each drum's hit list, C3 = 60 (checked against the key scans: every listed key sounds, every
  loud key is listed, except rings of the previous key). MuseScore's pitches are the GM ones of
  its drumsets; `ids` entries for instruments whose pitch means something else (tam-tam 52,
  temple blocks 58-62, ratchet 73 …). Tom 1, Conga 1 and Block 1 are the high ones
  (confirmed by ear by the owner, run 60). Triangles: off in the Metal kit by default (the owner's hand-made setup had them on 103-107 / 108-112), so
  they await Triangle 1 / 2's own patches; Ships Bell, Gankogui, Rivet Cymbal, Trash
  Gong Drum and Tom Ensemble have no MuseScore sound; Trash Metal Brake 1 plays Automobile Brake Drums.
  Rolls: `<Drum … technique="roll">` (`SoundLib::drumRoll`: a single-note tremolo or buzz roll plays
  the roll key once, held; without one, the tremolo's hits; test `renderKitRoll`). Swells are a
  roll under a hairpin (CC1), not SSO's fixed-length swell samples. SSO's roll keys are off in Kickstart by default
  (a technique switched on gets the next free key at the ends of the keyboard).
  **The owner, 2026-09-27: kits at the library's defaults; what they have off comes from the one-drum
  patches.** So the map has every key each kit patch plays at its defaults (`HITS` entries with on 1;
  keys with no MuseScore sound have no `pitch`) and a comment listing what is off (the keys the owner
  had switched on: Snare 1 x stick 115 / roll 119 / flam / swells, Snare 2 rim / x stick / flam / brush /
  roll 6, the triangles; and those with no key). Until the one-drum patches' keys are known, the snares'
  rolls play the tremolo's hits, the snare side stick the Field Drum's x stick, the triangle the built-in
  synthesizer. `default="off"` stays in the format but SSO's map no longer uses it. The rest come from
  SSO's one-drum patches ("Percussion - <kit> - <drum>", 42,
  the owner's folder of 2026-09-27; `SINGLES`): extras of the kit after the five kit patches, so a sound
  both have plays on the kit patch and only a technique the kit lacks loads the drum's own patch. Their
  keys (`SINGLE_HITS` / `SINGLE_DRUMS`, at their defaults) are not known yet. Kickstart shows a technique with no key on C-2 (0). None of it heard in
  Kontakt yet.
  - The scan also "found" many values that show "None": SSO leaves the RELEASE slider where
    the last short articulation put it, so their pictures differ from the first "None". They
    are silent at every pitch; the report now lists such values apart ("most likely none",
    `silentNotInMap` in results.json) instead of as articulations the map lacks.

- Sixth run (2026-09-25 18:06): Motif Brass's six values right; Grand Piano Direct (1) / Tape
  (2). Tuned percussion has no UACC (Kickstart keyswitches).
- Seventh run (2026-09-25 22:32, keys and Performance patches):
  - The Performance patches take no switching: "reacts to your playing without having to switch
    articulations". UACC 20 selected "None" and silenced the solo strings', several brass and
    the Sul G / Sul C Performance patches (the ensemble ones happened to accept 20); the
    single-technique patches likewise showed "None" for value 1. All extras (and the kit's
    patches) now have `<Switch type="none"/>` (SwitchType::NONE: no switch is ever sent; the
    check only listens to them, no scan). CHECK_VERSION 4.
  - Tuned percussion keyswitches, from the key scan's pictures (the technique arrow in the
    window's list) and the lists themselves: Timpani 0-5 confirmed; Celeste 0 Celeste / 1
    Espressivo / 2 Tight; Glockenspiel 0 Normal / 1 Muted / 2 Hard Sticks / 3 Roll; Marimba 0
    Normal / 1 Roll; Vibraphone 0 Normal / 1 Motor Sus. / 2 Roll; Tubular Bells 5 Normal / 6
    Muted (its keyswitches start at F-2); Xylophone, Crotales, Desk Bells have one sound (none).
    Silent keys at the ends of a range that "switch" are the previous note's meter decaying.
  - Unpitched kits (Drums - High / Low, Unpitched Metal / Wood, Other - Toys): Kickstart shows
    instrument pictures and colours key ranges, but names no hit while a key sounds, so the key
    scan can't say which key is which hit. Needs the owner (each instrument's hit list).

- Eighth run (2026-09-26 03:35, all 117 patches, build with extras and key scan):
  - Every patch had the right `.nki` loaded (`read_loaded_patches.py`; Drums High / Low "BAD" is
    the OCR reading Kontakt's suffix). Every mapped value's picture shows the map's name except
    Violins 2's Trem CS MS (150BPM) (84): "None" and silent (Violins 1 has it; Violins 2 takes
    Violins 1's bank). Dropped (SPITFIRE_DROP).
  - The remaining "ignored"/"unclear" verdicts (Long Harmonics on Solo Viola, Piccolo, Alto Flute;
    Flutes a2 Long Overblown; Contrabassoon Long; Motif Trumpets a3 Staccatissimo; Bass Trombone
    Fall; Contrabass Tuba Rip) have pictures showing the right articulation. Harp Fx (90) is
    silent at every pitch tried (no techniques, never chosen).
  - All 57 extras (Performance, Sul G / Sul C Performance, single techniques) sound with no
    switch sent: `<Switch type="none"/>` is right.
  - Tuned percussion: each patch's first keyswitch (Timpani 0, Celeste 0, Marimba 0, Vibraphone
    0, Tubular Bells 5) showed as "mapped but silent": it is selected when the patch loads, so
    playing it changed nothing. The key scan now selects the map's last keyswitch first.
    Silent "keyswitches" at the end of a range (Celeste 104, Marimba 100, Xylophone 99,
    Glockenspiel 64 and 100-101) are the previous note's meter decaying.
  - Unpitched kits: Kickstart's drum icons don't light up when a key plays, so the pictures
    show only the coloured key ranges. The owner is asked for each drum's hit list: clicking an
    icon shows its hits mapped on the keyboard.
- Ninth run (2026-09-26 07:00, run 42/44, four patches): Xylophone, Crotales, Desk Bells pass (one
  sound). Tubular Bells: its window's KEYSWITCHES field says C-2 and keys 0-1 are red, in this run's
  and the eighth run's pictures: the map's 5 / 6 (from the seventh run) were a misreading, now
  0 Normal / 1 Muted. The scan's 11-12 came from a black (failed) grab of key 11; failed grabs are
  now skipped, and the scan waits 4 s after all-notes-off (the load check's bells still rang on
  keys 0-1).
A value a SSO patch lacks: its window shows "None" and it plays nothing (no default).
`.github/workflows/test_soundlibrary_windows.yml` builds on Windows, runs the tests and
uploads the build. Start it with *Run workflow* (Actions › Test: Sound library on Windows), or
with a push to `main` (or a `claude/` branch) whose last commit message contains
`[windows-build]`. **Actions minutes: use them conservatively** (the owner, 2026-09-27, after
run 72 was started only to see the merged code compile on MSVC). Start a Windows build only
when the owner needs a new MuseScore to try something, or when a change touches code Linux
can't compile (`Q_OS_WIN`, MSVC-only paths) and the owner will need it soon. Not to confirm that
code which passes here also compiles on Windows: that check rides along with the next build the
owner needs. Batch changes into that one build, validate locally first, and don't retry blindly. A newer `[windows-build]` push doesn't cancel a build still running: cancel the
superseded one (Actions › the run › Cancel, or the API), as the owner asked why two ran (runs 46/47). Other pushes show up as skipped runs, which use no minutes. Runs so far
are in the commit log. Run 1 compiled everything with MSVC (about 15 minutes) and failed
only at the link (`Linux::IRunLoop` in vst3editor; now Linux-only). Run 2 built and uploaded
MuseScore (artifact `MuseScore-soundlibrary-win64`). The test build failed because on MSVC
`mtest/` is a separate solution (`project(mtest)`), so `cmake --build --target tst_…`
can't find the target. The workflow now builds the test's vcxproj with MSBuild (found
with vswhere). Upstream never built mtests on Windows, so more may break there. Run 3
got to compiling `testutils`, which failed on `ft2build.h` (the same problem that needed
CPATH on Linux). Fixed in mtest/CMakeLists.txt. Run 4 (run number 6 in the Actions list)
succeeded: MuseScore with the offline bus fix, and tst_soundlibrary built and exited 0. It
printed nothing, though, since QtTest's console output doesn't show on Windows, so the
per-test results weren't seen. The workflow now writes them to a file and prints it. The
next run (number 8, 44372f0, pitch fallback) showed **tst_soundlibrary 9/9 passed on
Windows**, including vst3Plugin, vst3Render and articulationCheck with the test synth. A
run takes about 15 minutes. Run 11 (6bc3a62: check memory, scan, added patches, map fixes):
**10/10 passed on Windows**, scanPictures included.
**The owner (2026-09-26): always name a build by its run number** (the Actions list's run
number, with its link) when asking them to try or download something. Run 32 (f5e721b) is the
first with the tuning, the Ethanol fix and the key scan fix.
When the owner reports problems, suspect these first: Kontakt's MIDI channel (we send
channel 1), its editor sizing, and sample loading in offline export.

The owner reviews the UACC numbers on a claude.ai artifact page ("SSO Articulation Map",
https://claude.ai/artifact/Y9dDEm5gpEjtpjjQmM9qBm). Marks are stored in its `reviews`
collection, one document per `<patch>__<articulation>` with status, value and note. Read
them with the ArtifactData tool, then fix the map through
`tools/soundlibraries/gen_spitfire_sso.py`, not by hand.

The earlier alternative, an Ableton set with every technique preconfigured, was not chosen.
Kontakt's patch loading can't be automated from outside (its state is opaque). Hosting means
the patch is set up once per instrument, and MuseScore reloads it by itself.
