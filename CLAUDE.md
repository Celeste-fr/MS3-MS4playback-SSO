# CLAUDE.md: notes for agents working on this repository

This is a fork (public since 2026-09-28; private before) of the MuseScore 3.7 community fork (Jojo-Schmitz/MuseScore 3.x @
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

- `main`: the default branch. It was called `ms4-playback` until 2026-09-27, when the
  repository was also renamed from `musescore3-ms4-playback` to `MS3-MS4playback-SSO`. It has
  the MS4 playback work (essentially finished) and the sound-library work.
- **A session works on its own branch only** (the owner, 2026-09-27: "just work on your
  branch"). A cloud session that is given a `claude/…` branch commits and pushes there and
  does not merge into `main` or push to it; the owner decides what goes to `main`. (Several
  sessions merging into `main` at once had crossed: one reverted another's work there.)
  Windows test builds for the owner come from the session's branch (`[windows-build]` in its
  last commit message, or *Run workflow* on that branch). Check `git log` of `main` and of
  your branch, and the latest commit messages, first. They are detailed on purpose and
  describe what each step did and how it was measured.
- CI runs by hand only (`.github/workflows/build_all.yml`, workflow_dispatch). This kept the
  private repo's Actions minutes; the repository is public since 2026-09-28 (the owner ran out of
  minutes), and public repositories run Actions on GitHub's standard runners for free.

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

Custom key signatures (the owner, 2026-09-27): dropped from the palette, a custom key signature
(made on the editor's treble staff) is adapted to each staff's clef (`KeySigEvent::forClef`: each
accidental where that clef's standard key signature puts the same note, an octave apart as drawn;
`KeySig::drop`, `Measure::drop`, `ChordRest::drop`); the positions are stored per staff as before, so
3.6 shows the same. Also adapted (the owner, 2026-09-27: a viola added after the signature kept the treble
positions): a staff added in the Instruments dialog (`adjustKeySigs` with the source staff's clefs, a new
staff of a part), a split staff, a clef change at the signature's tick (`undoChangeClef`). A later clef
change under a custom key: `Staff::keySigEventForClef` reads the signature for the clef in force (accidental
states in layout, measure, cmd, tuning; the signature repeated at a system start and the courtesy one drawn
for it); the file keeps the signature as placed. Paste and drag (2026-09-28): `KeySig::mimeData` writes a
custom signature taken from a staff for the treble clef (as the palette's), so every drop places it for its
own clef. Scores saved before: *Tools › Adapt Key Signatures to Clefs* (`Score::cmdAdaptKeySigsToClefs`):
a custom signature on a staff whose clef places keys unlike the treble clef, with the same symbols in the
same places as a treble-like staff's (G, G8va, G15mb …) at that tick, was copied as placed: it is placed for
its clef. One already adapted differs from the treble one and is left (running it twice changes nothing); a
score with no treble-like staff at that tick is left too.
Tests `tst_tuning::customKeyDrop` (keysig-clefs.musicxml: viola and treble-15 cello), `customKeyPasteAndAdapt`. Alt+Shift+Up/Down
(`Score::upDown`, DIATONIC) steps to the signature's accidental under a custom key signature (it used
the key, C, and wrote a natural against the signature). The key signature editor (*Master Palette ›
Key Signatures › Create Key Signature*, `mscore/keyedit.cpp`): staff twice the palettes' size, a
minimum height of 11 spaces, accidentals snapped to a line or space only (not to a whole-space grid
across). Tests `tst_tuning::diatonicCustomKey`, `customKeyForClef`.

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
- Plugin API (`mscore/plugin/api/elements.h`, Note, read only): `playbackTuning` (the total cents,
  as played) and `microtonalTuning` (accidental + own tuning, without the temperament: a quarter-sharp
  F is +50 in any temperament). Each builds a `ScoreTuning` for the call. For plugins that judge
  pitches, not playback: the owner's Playability Checker (`~/MuseScore/orchestration-checker`, not in
  this repository) uses `microtonalTuning` in its string multiple-stop check (a quarter-sharp G3 is not
  the open G string, a quarter-flat G3 is below it). MuseScore 3.6 has neither (undefined in QML).
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
  `musescore.cpp` for plugin mode: has a setup, or its `.nki` is there to make one) keeps an unset extra out; `routesGeneration()`
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
  Shorts (the owner, 2026-09-28: at pp the staccatos stood out; their velocity was MS4's soundfont one, 56 at
  pp and 65 at mf, while CC1 went 32 → 80, and Spitfire's shorts take their dynamics from velocity only):
  a base listed in `<Dynamics velocity="short staccatissimo spiccato marcato tenuto pizzicato bartok collegno">`
  (`Library::velocityDynamics`) gets `NoteResult::levelVelocity`: the dynamic level on CC1's scale
  (`expressionLevel`), times MS4's velocity over a plain note's (an accent: pp 32 → 48, mf 80 → 108; the
  curve's peak would make an accented pp short 113). Longs and legato keep MS4's velocity (Spitfire's legato
  speed is on velocity). The library's dynamics CC now goes ahead of the notes at its tick (a long starting on
  a new dynamic started at the old one); MS4's CC11 for the built-in sounds keeps MS4's order.
  Test `shortsFollowDynamics` (shorts-dynamics.musicxml).
  **The owner's Dynamics check, Violas (2026-09-27 21:57 local):** Long and every long / tremolo / trill is on
  CC1 only (velocity 32 vs 127: no change), so `long` is not listed (18492e8 reverted). CC1 moves a Long 8-16
  dB (Long: -35.2 dB at 32, -27.1 at 80, -26.7 at 127: little above mf); velocity moves a short far more
  (Spiccato -58.8 at 32 -> -19.4 at 127, Bartok 28 dB, Col legno 21 dB). So at pp the listed shorts are
  25 dB under the longs, at ff level with them: their velocity scale may need narrowing (open; the owner's ear).
  **Dynamics calibration** (the owner, 2026-09-28: with section strings the staccatos were quiet again;
  "me manually hearing for every single technique volume is not gonna cut it"): *Check articulations* ›
  *Dynamics* measures each articulation along velocity = CC1 = 16, 32 … 112, 127
  (`ArticulationCheck::CURVE_POINTS`, loudest 50 ms) plus velocity-only / CC1-only ends (`drivenBy`), and
  merges the curves into `<setups folder>/dynamics.json` (`SoundLib::DynamicsCalibration`, loaded by
  `SoundLibraryHost::loadCalibration` at startup / preferences / after a check). Playback
  (`libVelocity` in rendermidi): a note whose articulation is measured on velocity plays
  `calibratedVelocity`: the inverse of its curve at the held note's loudness (the articulation
  `choose(patches, {long})` gives: the Performance legato) at the dynamic's CC, plus `balanceDb`
  (*View › Sound Library…* "Short notes against held notes"); an accent keeps its share. No curve for
  either: the `<Dynamics velocity>` rule. On the controller (tremolo, trills …): not adjustable (the CC is
  the part's). summary.txt "# Dynamics balance": every measured articulation against the held note at
  pp / mf / ff, now and as before, "!" past 3 dB (the automatic test). The other branch's data (control
  titles, articulation names, key ranges per .nki) has no velocity layers or volumes; the library-files
  extract's library.json would show velocity-split vs crossfaded layers but not loudness. Tests
  `dynamicsCalibration`, `dynamicsCheck`.
  **Faster, in the background** (the owner, 2026-09-28: "have it run in the background and use separate folders
  … make the test faster without compromising the data"): *Dynamics only* (check box; `dynamicsPatch`) skips
  the articulation check and the window pictures; `measureDynamics` measures only articulations a notation
  chooses (techniques not empty); `ArticulationCheck::dynamics` classifies with 3 notes (velocity 32 / CC 32,
  CC 127, velocity 127; other pitches where silent: +12, -12, +7, -5, +24) and gives the full 8-point curve only
  to one on velocity (0.5 s note, 0.2 s tail) or asked for in full (the part's held note: `full`), one on the
  controller 32 / 80 / 112 / 127; notes go soft to loud and each waits for the last one's tail 50 dB under it
  (`Player::relativeSettle`), not -70 dBFS. The owner's Violas run of 2026-09-27 (old way, ~430 notes) took about
  a minute: Kontakt offline is ~10x real time. `MuseScore --extract-library <lib> --check-dynamics
  [--extract-patches mapped|file]` (musescore.cpp `extractInBackground`, `runHeadless(…, dynamics)`): its own
  setups copy (`background dynamics check setups`), lock and log (`background dynamics check.log`), the curves
  merged into the working `dynamics.json` at the end (balance kept). `Measure SSO dynamics in background.bat`
  (bin): copies the install to `%LOCALAPPDATA%\MuseScore background dynamics check` (the owner installs other
  builds meanwhile), refuses a second run (PowerShell: a process from that folder), starts it; a patch list file
  dropped on it. Tried here headless with the test synth (a map DynTest.xml, the synth's state as setups):
  2 patches in 4 s, the unused value skipped, harmonics measured an octave up, the balance report in the summary.
  **Section strings' shorts and Long (Rachm.)** (the owner, 2026-09-28): staccato plays Short 0.5,
  staccatissimo Spiccato, tenuto / portato Short 1.0 (Violins 1/2, Violas, Celli, Basses, Strings
  Ensemble); staff text "espr." / "espressivo" / "molto vib." / "con vibrato" sets the modifier
  `espressivo` (until "non vib." / "senza vib." / "ord."): a held note plays Long (Rachm.) (Rachmaninoff:
  Spitfire's romantic long), a slurred one keeps the Performance legato (legato is tried before long).
  Not the default held sound (the owner). Short Brushed (CS) and Fx stay unmapped.
  **Held notes on the Performance patch** (the owner, 2026-09-28: lone held notes quiet and "the pan is
  broken"; bar 12's lone pickup eighths barely audible): a part's slurred notes played "X - Performance"
  and its lone held / unmarked notes the All techniques patch's Long, another recording with its own
  level and place (Solo Viola Long -30.9 dB peak at 100/100) and slow to speak. `<Articulation prefer>`
  (`Articulation::prefer`, `choose`: of equal fits in different patches, one that prefers the base wins):
  the Performance legato is `techniques="legato long" prefer="long"`, so held notes play it too (not
  overlapping, a note plays with its own attack). Shorts, pizzicato, con sord., sul G … stay on their
  patches (their modifiers or techniques). The Performance patch now gets lanes for other tunings.
  Tests `heldOnPerformance`, `spitfireMap`.
  **Check of it with the library** (the owner, 2026-09-28: "verify that dynamics is consistent across all
  techniques"; not knowable here: which of SSO's articulations are on velocity is Spitfire's, the list above a
  guess for tenuto and marcato): *Check articulations* › *Dynamics* (`ArticulationCheck::dynamics`): each
  articulation that switches plays, offline, at pp / mf / ff as MuseScore sends them (CC1 32 / 80 / 112; the
  velocity: the same for a listed technique, else MS4's plain one, 56 / 65 / …) and with velocity 32 / 127 at
  CC1 100 and CC1 32 / 127 at velocity 100; loudness = the loudest 50 ms (RMS). summary.txt, per articulation:
  dB at pp / mf / ff, driven by velocity / controller / both / neither (3 dB and more from 32 to 127), and a
  flag: on velocity but not listed (add it), on the controller only but listed (not needed), neither, or a
  pp -> ff span more than 6 dB off the patch's controller articulations' median. results.json `dynamics`.
  Round robins move a note ±1-2 dB. Test `dynamicsCheck` (test synth: velocity * CC1).
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
  controls"), all as Kontakt parameters by title. **Each patch's own list since 2026-09-28**: the owner's
  extract of all 700 patches (2026-09-27 18:34, run 151, 54 min, every patch with named controls) is kept as
  `tools/soundlibraries/sso_patch_controls.json` (the titles of each patch's named automation slots, in slot
  order, Kontakt's placeholders left out), and `gen_spitfire_sso.py` (`measuredControllers`) gives every map
  patch exactly its own; it stops if a map patch is missing from it. 38 different sets: e.g. the solo strings
  Vibrato and 3 mics (the family guess had Release, Tightness and 5), Flute Solo Vibrato, Release, Variation,
  some woodwinds Tightness, Performance strings Mute, 4 patches "Bow Emph.", the Curated Ensembles Reverb, some
  Speed, the Harp Releases and Harp Pedal 1-7, the Grand Piano Pedal Vol / Pedal Dyn, the kits Releases,
  Variation, 3 mics. Not Dynamics, Expression (MuseScore's CC1 / CC11) nor Articulation Controller (UACC). Mics
  named Close … Leader where a patch has 4 or 5; where it has 3 (148 patches: solo strings, Curated, percussion …) Close, Tree, Ambient (2026-09-28: each such `.nki` has samples under exactly those three mic headers; the Curated Ensembles' Outrigger header has no zones). No defaults
  (the patch keeps its own until a part has a value). `Vst3Plugin::parameterId` matches titles loosely (case,
  spacing, punctuation, a slot number in front), and the Controllers window lists the controls of all the
  part's patches and says "not in <patch>" for a loaded patch without that title. Tried by the owner on run 91
  (2026-09-27, with the family lists): Vibrato works; Mic 1-5 are Close, Tree, Ambient, Outrigger, Leader; Mic
  Mix Distance sets all five faders (0: Ambient only, 127: Close only), so it is applied first and a mic level
  ticked wins.
  **Values belong to the score; the library's default is SSO's own** (the owner): a parameter a
  score doesn't set is put back to the patch's value when another score set it on the same loaded
  instance (`Slot::patchValues`).
- Microtones (the owner, 2026-09-27): each library note's tuning (cents, `tuning.h`) goes into VST 3's
  `NoteOnEvent::tuning` (`Vst3Plugin::midi`, from `Vst3Synth::play`); the test synth honours it
  (`vst3Plugin` test: A4 +50 cents sounds at 452.9 Hz). **Kontakt / SSO ignores it** (the owner, run 115,
  2026-09-27: "tuning doesn't affect the note pitch at all"). No note expression either: Kontakt 8.9 has
  `INoteExpressionController` but lists 0 note expression types on every channel, with nothing loaded and
  with Violins 1/2, Violas, Celli loaded (the owner's extract of 2026-09-27 10:58). So Kontakt has no
  per-note tuning a host can reach; what is left shifts a whole instance (pitch bend, or an effect).
  The owner chose pitch bend if SSO's bends the pitch cleanly. *Extract plug-in data* now measures it on
  every patch (`PluginExtract::pitchBend`: the test note at bends 0 … 16383 against the unbent note,
  `centsShift`: the log-frequency spectra's best alignment, ±26 semitones, 5-cent bins refined; JSON
  `pitchBend`, summary "pitch bend range"). Test `pitchShift` (the test synth now bends ±2 semitones,
  "Pitch Bend" parameter on MIDI pitch bend: measured within 6 cents; shifts of 50 … 1300 cents by
  note-on tuning; a vibrato tone a minor third up).
  **SSO's pitch bend doesn't bend the pitch** (the owner's extract of 2026-09-27 14:13, run 127/130: Violins 1,
  Flutes a2, Horn Solo, every bend 0 … 16383 within the round robins' own spread: 0 / −14 cents on Violins 1
  alternating with the round robin, not with the bend). So the owner's plan below is what is left.
  Kontakt does receive the bend (its "Pitchbend" parameter moves; run 134's extract), the patches' scripts ignore it. (Timpani, a Kickstart patch, does bend: ±2 semitones, linear, -196 … +195 cents; the orchestral patches don't.)
  **Built instead (the owner chose "option 1" and asked for memory savings, 2026-09-27): tuning lanes
  with varispeed.** An effect on one instance's output would shift the tails of earlier notes with the
  new one (the owner: "this shouldn't happen"), and a second copy of a patch costs about 245 MB even in the
  same Kontakt (the owner measured 1031 → 1276 MB: Kontakt shares no samples between copies). So:
  - `Vst3Plugin::setPitch(cents, glide)`: varispeed. The plug-in renders into a buffer read back at
    2^(cents/1200) through a windowed-sinc resampler (Lanczos, 8 taps each side): exact pitch, no
    pitch-shifter artifacts, 8 samples of latency once engaged; the plug-in's own time runs as much faster
    (3 % for a quarter tone: vibrato and attacks). Glides for legato.
  - `SoundLib::lanes` (map `<Tuning method="varispeed" tolerance="0.5" tail="1.5" maxLanes="4"/>`, SSO's
    since 2026-09-27): a part's notes over copies ("lanes") of their patch. In order of start: a slurred
    note stays on its previous note's lane (the legato transition needs one instrument; it glides), else a
    lane at its tuning (within the tolerance, cents; the note then plays at the lane's tuning, `Lanes::cents`,
    `libLaneCents`, so nothing sounding on it moves; 0.5 merges rounding only, not HEJI's 1.95-cent schisma), else a lane silent by then (its notes' end plus the
    tail, seconds), retuned, else a new lane; past maxLanes (memory) the lane quiet longest is retuned.
    Tied notes follow their first note, grace notes their chord. `routes()` gives each patch one route
    per lane (`Route::lane`), so each lane is an instance with the same setup; the renderer
    (`libLanes`, `finishLibraryEvents`) sends a note's events to its lane and the part's switches and
    controllers to all lanes. `Vst3Synth::setVarispeed` (from the map, in sync and export): a note-on
    sets its slot's speed from the note's tuning, at once when the slot is silent, else gliding 30 ms (80 ms until 2026-09-28: the owner's
    slurred 16th E quarter-sharp after a D quarter-flat, 110 bpm, glided a semitone over most of its 136 ms);
    the note goes to the plug-in with no tuning. 12-tone equal scores need no lane (every tuning 0); a
    temperament (meantone, JI) can need several per part, hence maxLanes. The Sound Library dialog lists
    lanes as "~ <part> (other tuning n)".
  - Test `tuningLanes` (quartertones.musicxml: 8 notes' lanes, their routing and tuning, CC1 and switches
    on both lanes, maxLanes 1, and Vst3Synth playing ±50 cents on the test synth by speed); `pitchShift`
    (setPitch +50, −100, +700, a glide to +200). Not heard with Kontakt yet.
  - The score's own tolerance, ring time (tail) and maximum copies (2026-09-28): *View › Sound Library…*
    row "Copies for other tunings", metaTag `soundLibraryLanes` ("tolerance=… tail=… max=…", only what
    differs from the map; `SoundLib::laneSettings`, undoable; kept by MuseScore 3.6 as a metaTag). A change
    renders again; the copies load at the next play. Memory: the dialog's *Memory* column is what the
    process grew by as each patch loaded, and 3 s later if no other load started (Kontakt goes on loading);
    Windows: private bytes (`SoundLibraryHost::processMemory`, soundlibrarymemory.cpp, kept apart from the
    Windows headers' macros), Linux: resident. The part's first row adds its extras and copies; the info
    line the total and MuseScore's own. Not done: lighter single-technique patches for the copies (needs
    which lighter patches SSO has per instrument: the library-files extract).
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
  Show (the plug-in's editor, to look at) and *Library folder…*.
- **Setups made by MuseScore** (the owner, 2026-09-27: "the set up script should be integrated into
  MuseScore and you should no longer be able to manually set up things", "the manually set up patch
  configs should be discarded in favor of setting up everything automatically to their default
  settings"). A map with `<Files registry>` (SSO) gives each patch its `.nki` (`nki=`, relative to the
  library's folder: NI's registry `ContentDir`, or the one chosen with *Library folder…*, QSettings
  `soundLibrary/<name>/folder`) and script values (`setup="$iooxo=3"`, UACC). `SoundLibraryHost::setupState`
  makes the setup on demand with `KontaktSetup::fromEmpty` (`audio/vst3/kontaktsetup.*`, the C++ port of
  make_setups.py's from-empty path; 1 s for Violins 1) from Kontakt's state with nothing loaded (a fresh
  instance's, kept as `Kontakt empty.vst3state`), writes `<patch>.vst3state` and records what it was made
  from in `made setups.json` (the `.nki`'s size and time, the values, `MAKER_VERSION`, the empty state's
  SHA-1): made again when one changes. Kontakt's controller state is always empty (the extracts), so none
  is written. The first time, every other `.vst3state` there (hand-made, or make_setups.py's) is moved to
  `old setups (not used)`. No manual set-up: *Set up…*, *Save setup*, saving on closing the editor and
  *Add a patch…* (for SSO) are gone. `hasSetup` = the `.nki` is there. Check articulations' "setup"
  memory is `setupId` (what it's made from), so it is known before the setup is made. A library
  without `<Files>` still uses a `.vst3state` put there (the tests' plug-ins). The map lists SSO's other
  541 patches as `<Patch>` (from `tools/soundlibraries/sso_nki_files.txt`, the owner's 700 `.nki`):
  Check articulations lists all 700 (*Tick all*, then *Untick all*) and scans the others.
  Tried here in the GUI under Xvfb (PulseAudio null sink, the test synth as the plug-in, a library folder
  with the owner's Violins 1 `.nki` under the Solo Violin and Grand Piano paths, Kontakt 8.9's empty state
  seeded as `Kontakt empty.vst3state` since the test synth's isn't Kontakt's): opening a violin and piano
  score moved three hand-made files to `old setups (not used)` and made both setups (1.1 s each: the
  program, `$iooxo` 3, 32,472 absolute sample paths), reused on the next start; the dialogs show "Loaded" /
  "Made by MuseScore", *Library folder…*, no *Save setup*, *Set up…* or *Add a patch…*. Tests
  `kontaktSetup`, `kontaktSetupReal`. **Run 107 (the owner, 2026-09-27): Kontakt refused every made setup**
  ("The project could not be recalled for unknown reasons", Solo Violin 1 / 2, Viola, Cello). The last 4 bytes
  after the Preset Chunk Item's data are a marker, not a checksum: a7636734 in an `.nki` and a multi with a
  program, 8565620d in Kontakt with nothing loaded; the setups kept the empty one. They take the `.nki`'s now
  (`Preset::set(data, tail)`; make_setups.py's from-empty path too), `MAKER_VERSION` 2 remakes them; tests
  compare the marker (the fixtures' `.nki` now carries a7636734). Against the owner's hand-made Violins 1
  every chunk is now the same but ids, the save time, the browser state, the program and the paths.
  **Run 112: they load, but about 100 times slower than the hand-made ones** (the owner). Most likely the sample
  list: a made setup has the `.nki`'s (version 2: absolute paths, the `.nki`'s dates and numbers), Kontakt 8
  writes version 3 (each entry: u32 kind, u32 library id 0x8515 for "from the library", u32 segments, the
  segments, then u32 0, u64 the file's date, u32 a number, u32 0; the preset's folder, Kontakt's user folder, the
  `.nkr`, the `.nki`, then the samples), and likely re-checks every sample when the dates don't match. Not
  written by us: after a made setup's first load, `resave` (soundlibraryhost.cpp) replaces it with Kontakt's own
  state (getState), kept only when it has the same program, the program marker and the script values
  (`"resaved": true` in `made setups.json`; the record's other fields still say when to make it again). So a
  patch's first load stays slow, later ones should be like the hand-made setups': run 119's `load times.log` (the owner, 4 solo strings): made 92-106 ms, first load 0.3-2.4 s (from the .nki), resaved 60-88 ms (386 → 298 KB), next load 92-109 ms (Kontakt's own state), about 20 times faster. setState's time only: Kontakt may still stream samples after it returns.
  `load times.log` (setups folder) records making, loading ("made from the .nki" / "Kontakt's own state") and
  resaving per patch, since qDebug doesn't show on Windows. Every part gets its instance, with or without notes
  (the owner, 2026-09-27: "just load everything at score open"; loading only parts with notes, and a part once it
  got notes, was tried and removed).
- Loading at score open (`SoundLibraryHost::preloadSoon`, from `MuseScore::setCurrentScoreView`): as soon as
  a score is opened or shown, all its instances load one per event-loop turn (`syncSome(…, 1, &remaining)`, status
  bar "Loading … in the background"), each after a pause of `INPUT_PAUSE_MS` without a key, click or wheel (0 since the owner asked, 2026-09-27: no wait; it was 1.5 s)
  (`eventFilter`, `INPUT_PAUSE_MS`: an instance blocks the window while it loads, and VST 3 wants that
  on the UI thread); a play before that's done loads the rest (`sync`). The owner's full orchestra
  (2026-09-27, `load times.log`): first loads from the made setups 2-43 s per patch (Trumpet Solo 43 s,
  the string sections 15-31 s), after the resave 0.15-0.8 s (25 instances in 11 s); `load times.log`
  now also has each new instance's creation time. The owner,
  2026-09-27: a full orchestra (25+ Kontakt instances, ~0.7 GB each at Kontakt's default 60 kB
  preload) made the first play wait long. Tried here under Xvfb with the test synth: 5 parts, 5
  instances loaded one by one (`qDebug` "preloaded one instance, n to go"). Instances are reused
  across scores (`syncSome`): one the score can't use in its slot is set aside (`_spares`), a patch a
  spare plays moves to the slot that needs it ("<patch> kept (slot n)"), a spare of a patch no longer
  needed takes a new patch when it has a setup (one instance fewer), and the spares left are
  released once all is loaded. Tried with two scores sharing 3 of their 4-5 patches in another
  order: switching loads only the other 1-2. Status messages (`MuseScore::showMessage`) are in a
  label of their own and repainted at once: QStatusBar's own messages never showed in MuseScore 3
  (its stretching spacers leave them no room). `load times.log` also says what each sync has to load, "At play" or
  "At score open", and a setup that failed to load (the owner, 2026-09-28: the Performance patches loaded at every
  play; cause not found by reading the code). A slot whose setup failed is not loaded again at each play
  (`Slot::setupFailed`). **Audio thread** (the owner, 2026-09-28: sound stopped at random): `Vst3Synth::idle`
  (GUI, every 50 ms) held the slots' mutex while it passed every CC played to each instance's controller, and
  `play`/`process` only try that mutex: events were dropped (a note, its note-off, a switch) and blocks skipped.
  Now idle holds it only to list the instances (and while a MIDI mapping changes), an event that misses the lock
  waits in `_pending` for the next event or block, and a missed all-notes-off is done then. It also ended the owner's "note stays bent after deleting its accidental" (run 142; not reproduced by `tuningLanes`): with the old `play`, a dropped note-on skipped its slot's `setPitch` (the slot stayed at the accidental's speed) and a dropped note-off left the bent note sounding and counted, so later notes glided; the owner can't reproduce it on the build of 9fa4ce2 (2026-09-28). `Vst3Plugin::parameterId`
  keeps an index of the loose titles (it went through Kontakt's 4145 parameters, two regexes each, for every
  controller of every instance at every play). **Crackle live, not in
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
  - The setups are MuseScore's (above); *Set up…* is gone (2026-09-27).
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
  **Quick** (on by default under *Try every controller*; the owner, 2026-09-27: "a more efficient way"): no
  value search; the patch's state, taken after it loaded, is set again after each CC that changed something
  (`Settings::restore`: setState, 1.5 s, the articulation and dynamics again), so every CC is back at the
  patch's own value but `patchValue` is unknown (`"restored": true`). Then `controllersToControls`: each CC's
  window region against the named parameters' (IoU over 0.3), or a parameter the CC's own try changed, gives
  which named control it moves ("?" when none: the CCs that do something no named control does). An
  estimate of about 5 minutes a patch instead of 15-20; not timed with Kontakt yet. Tried here in the GUI
  with the test synth (one patch in a minute) and in `tst_soundlibrary::pluginExtract`.
- Output: `Documents/MuseScore Sound Library Check/<library> extract <date>/` (`plugin.json` and its state
  `.bin`s, `<patch>.json`, sheet, window, summary.txt) and a zip. A patch's `describe` holds only what
  differs from `plugin.json` (`describeAgainst`: `sameAsPlugin`, `parametersChanged`, `parameterCount`) and
  no state is written per patch (MuseScore makes them from the `.nki`): the owner's run of 5 patches
  (2026-09-27 11:15, run 124) was 7 MB a patch, nearly all Kontakt's 4145 parameters as with nothing
  loaded; the same data diffed is 12 KB a patch (so 700 patches about 8 MB before zipping). The summary
  lists each patch's named controls. For long runs (the owner, 2026-09-27: all 700 patches, estimated 10-12
  hours): one Kontakt instance for every patch (the one described with nothing loaded; each patch's setup set
  on it; a new one only after a patch that failed), `describe(&empty)` takes the texts of parameters unchanged
  since the empty plug-in from it (`parameterTextsFromBase`), pitch bend only with *Measure pitch bend* ticked
  (off by default: about 25 s a patch; one patch per family), and each patch's times (`timesMs`, summary
  "times": new instance, setup, until it sounds, describe, pitch bend, controllers), the run's total and a
  minutes-left estimate on the progress bar. Tried here in the GUI with the test synth: 3 patches with pitch
  bend in 1.5 min (setup 0-1.8 s, until it sounds 3 s, pitch bend 27 s each), one instance. With Kontakt (the
  owner, run 134, 4 patches loaded before): setup 0.2-0.7 s, until it sounds 3-5 s, describe 0.7-0.9 s, pitch
  bend 27 s; so without pitch bend about 5 s a patch, plus a patch's first load (2-43 s) when never loaded.
  **In the background** (the owner, 2026-09-27: all 700 patches "without me having to configure or check any
  boxes", "without interfering my ability to work on musescore projects"): `Extract SSO in background.bat`
  next to the Windows executable (`main/Extract SSO in background.bat.in`, installed to bin) starts
  `MuseScore3Evo.exe --extract-library "Spitfire Symphony Orchestra"` (`[--extract-patches all|mapped|<file>]
  [--extract-pitch-bend]`; `extractInBackground` in musescore.cpp, `ArticulationCheckDialog::runHeadless`):
  no window (noGui), no audio or MIDI device, not handed to the running MuseScore (single-instance check
  skipped), below-normal priority (the first run's PROCESS_MODE_BACKGROUND_BEGIN plus `start /low` starved
  Kontakt: setup 46-54 s, describe 25-35 s against 0.2-0.7 and 0.7-0.9 at normal priority; the owner's
  summary, 2026-09-27), one run at a time (a lock file). Describing without pitch bend or controllers no
  longer waits for the test note to sound (1.5 s for the patch's script to start; "sounds" left out).
  **The owner's run of 2026-09-27 15:27 (run 139):** 700 patches in about an hour, but a warning of Kontakt's
  on patch 59 (Celli - Performance: "something could not be loaded, open Kontakt"; the exact text unknown)
  held it 21 minutes, and from that patch on Kontakt ran no patch's script: 545 patches with no named controls
  (their states full size). Now a patch with none named, once earlier patches of the run had some, is done
  again on a new Kontakt instance; five in a row still without stop the run with a log line. The owner's next run
  (16:43, 28.8 min for 700) had the same warning on Celli - Performance (after Violas - Performance on the same
  instance) and, from it on, 642 patches whose only "control" was Kontakt's own `NIKT0018` (parameter 2048), so the
  retry didn't fire: `namedControls` no longer counts `NIKT<n>` titles. The run after (17:20) retried Celli - Performance on a new
  instance and that failed too: after the warning Kontakt runs no patch script in the whole process. So a headless run
  now stops at a patch still without named controls after the retry (`_broken`, `_left`), and `extractInBackground`
  starts a new MuseScore (`--extract-round n`, at most `MAX_EXTRACT_ROUNDS` 20) on `background extract round n.txt`
  (the patches left, without that one); each process writes its own extract and zip (a folder of the same minute gets
  " (2)"). Tried here with the test synth and `MS_EXTRACT_TEST_BROKEN=<patch>` (a test switch: from that patch on the
  process counts as broken): 5 patches, broken on the 3rd, the other 4 in two zips. Looked into Celli - Performance
  (the owner's `.nki` and made setup): its 27,528 sample references all resolve inside their `.nkx` (the library files
  extract's archive lists), its header and SNPID (N51) are as Violins 1's, and the made setup's program is the
  `.nki`'s byte for byte but `$iooxo` (1 in this `.nki`, 0 in Violins 1's; set to 3 as in the owner's hand-made
  Performance setups). **Found (2026-09-28)**: the warning came with Celli - Performance
  alone in a fresh MuseScore (the owner), and its `.nki`'s "other files" have a convolution reverb from Kontakt's own
  content, `<6>presets/Effects/Convolution/K4IR.nkx/K4 IR Samples/L224 Orchestral 1.1s.wav` (segment type 6: Kontakt's
  content folder); making paths absolute put it under the `.nki`'s folder, which has no `presets`. A path starting with
  a 6 is now kept as it is (`KontaktSetup` `absolute`, make_setups.py `absolute`; test `test_kontakt_content_kept`;
  `kontaktSetupReal` with the owner's `.nki` keeps it). Other patches with Kontakt's convolution would have had the same
  fault. **Confirmed by the owner on run 151 (2026-09-28): with its setup made again, Celli - Performance
  loads and plays.** Without raising `MAKER_VERSION` (every patch's first load slow again): a setup
  Kontakt gives back unchanged loses its `made setups.json` record, so it is made again at its next load. On Windows a
  watchdog thread (`DialogWatch`, musescore.cpp, background run only: the process has no window of its own)
  logs the title and texts of any visible window of the process and closes it (WM_CLOSE) after 30 s. The
  warning (the owner's screenshot): "One or more Kontakt instances cannot be recalled correctly, perhaps due to
  missing content. Please open any Kontakt instance in your host, Komplete Kontrol or Maschine to resolve the
  issue." Celli - Performance loads and plays in Kontakt standalone, and its made setup's 27,530 sample paths
  all point where the `.nki`'s do (the Samples folder's Strings_Celli_*, Legacy_2-4, Strings_Violins1_8
  archives), so the cause is not found yet. `load times.log` showed that from then on Kontakt gave every setup
  back unchanged (636 "resaved … (n KB, was n KB)"): `resave` now refuses a state identical to the one given
  ("the plug-in gave the setup back unchanged: it did not load it"). (The test synth gives Kontakt states back
  unchanged, so with it a made setup is never resaved now.) Its setups
  are a copy of the working MuseScore's in `Documents/MuseScore Sound Library Check/background extract setups`
  (`SoundLibraryHost::setDataFolder`; copied file by file where missing), and it writes nothing in MuseScore's
  data or settings folders (not even `workspaces/global/menubar.xml`, which every other start writes).
  Progress in `Documents/MuseScore Sound Library Check/background extract.log` (`logBackground`), the folder
  opens when done. Tried here headless with the test synth, alone and beside a running MuseScore: 3 patches in
  10 s, only the copy, the log and the extract written; a second start while one runs stops at once.
  **Every controller and pitch bend on all 700, in the background** (the owner, 2026-09-28: "let's do that", after
  *Try every controller* had run on Violins 1 only and pitch bend on four patches): `Measure SSO controllers in
  background.bat` starts `--extract-library … --extract-patches all --extract-controllers --extract-pitch-bend`
  (`extractControllers`, passed on to a new process when Kontakt breaks; `runHeadless(…, controllers)`). In a
  background run with controllers or pitch bend, `extractPatch` plays the patch offline (`setOffline(true)`, back
  to real time after the patch) and `Pump::fast` renders without waiting for the clock; no window is opened, so
  each controller's effect is its sound (level, brightness, balance) and Kontakt's parameters, with Quick's reload
  after each one that did something; which named control it moves ("controllersToControls") needs the window's
  pictures and stays "?". In real time with the window this would be about 7 minutes a patch (80 hours); offline
  it's estimated at 1-2 minutes a patch with Kontakt (not timed yet; Kontakt maps all 128 controllers on every
  channel). Tried here with the test synth: 2 patches in 9 s, pitch bend ±200 cents exactly, CC 1 and the "Tone"
  parameter found.
  **When the owner hands it back**, run
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
Now in MuseScore itself (Plugin hosting › Setups made by MuseScore); the Python tool stays for tries outside it.

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
  keys (`SINGLE_HITS` / `SINGLE_DRUMS`, at their defaults): Snare 1 / 2 and Triangle 1 / 2 since 2026-09-28 (the
  owner's screenshots, reviewed on https://claude.ai/artifact/CNocqS6mHvhGiJB6A4R1qw: snare Swell mf 41, Swell f 43,
  Hit 48, Flam 49, Edge 52, Rim 55, X Stick 59, Roll 61, Snare 2 also Brush 56, Brush Roll 63; triangle Open Hit
  1-4 48 / 53 / 59 / 64, Closed Hit 49), so a snare roll plays Snare 1 / 2's Roll, a snare's side stick Snare 1's X
  Stick, the triangle (open / muted) Triangle 1 (test `spitfireMap`); the other 38 not yet. Kickstart shows a technique with no key on C-2 (0). None of it heard in
  Kontakt yet.
  - **Articulations from the files** (2026-09-28; the owner: scan faster without missing anything): each
    `.nki`'s top-level sample-group names under the first mic are its articulations (variants, round robins,
    dynamic layers, release groups left out; for Violins 1 exactly what Check articulations saw in Kontakt),
    read from the owner's library-files extract into `tools/soundlibraries/sso_nki_articulations.json` (700
    patches). Of the 541 patches the map doesn't use, 518 have one sound (single techniques, Performance): no
    switch to scan. The 23 with several are marked in the map, `<Patch … scan="values">` (the 4 Curated
    Ensembles, the 12 Core / Decorative techniques) or `scan="keys"` (the 6 percussion ensembles, Harp
    glissandi; `keyScan`); *Check articulations* › *Tick the patches to scan* ticks them. The files give the
    names only: which switch value plays which is in Spitfire's script, so Kontakt still scans those 23.
    The owner's scan of 2026-09-27 20:15 (run 165) gave the 4 Curated Ensembles' values: their names in
    alphabetical order, 1 … n (reviewed on https://claude.ai/artifact/RLjhTuv28WtqGNcsF1kVMR; Tutti 5 = Long
    confirmed by the owner in Kontakt). They are in the map as `<Articulation>` children of their `<Patch>`
    (`SCANNED` in gen_spitfire_sso.py; no techniques, no `scan`), so 12 values and 7 keys patches are left.
    Test `spitfireMap`. Scan blind spot: the articulation selected at load (Curated: value 1, Beast Long)
    looks like the values the patch lacks, which show it too, so neither picture nor sound tells its value.
    The picture most values show now goes on the sheet as its last cell (`noneOnSheet` in results.json);
    `read_check_names.py` flags it "AT LOAD" with the UACC number it reads when it isn't SSO's "None".
    The owner's scan of the 12 Core / Decorative patches (2026-09-27 21:29, run 167): 8 scanned well; Celli,
    Violas, Violins 1 and 2 - Core techniques "found" 64-67 values and played nothing offline. Their "None"
    pictures looked two ways (the RELEASE slider left where the last articulation put it), so the ones before
    it moved counted as articulations, the first of them (0, silent) was the offline wait's note, and a minute
    of silence left no listening. Now a picture 4 or more values share is "no articulation" too
    (`scanPictures`; on the owner's sheets: Celli Core 65 → 14, Violins 1 Core 67 → 17, the files list 17 / 18),
    and the offline wait plays the value heard while loading. Test `scanPictures` (a slider like SSO's).
    The re-scan of those 4 (2026-09-27 23:32, run 170) worked (14-15 each, every value and name the same as
    the map's All techniques patch of that instrument), but it and the 21:29 scan missed articulations the
    files list: Pizzicato (Core), Pizzicato / Bartok / Col Legno (Basses Core), Short Brushed / Spiccato CS
    (Ensembles Core), Long CS Sul Pont and three trills (Violins 1 Decorative). A patch not in the map starts
    as it loaded (Long) and the scan can't go back there, so its after-scan picture showed "None", and against
    the start that marked the name and its button as "changing by itself"; articulations that differed from
    "None" only there were lost. Now that picture is paired with the last value's during the scan
    (`scanPictures` `samePairs`). Test `scanPictures` (the old comparison misses, the new finds all).
    With it (2026-09-28 00:09, run 177) 10 of the 12 came out complete, Long included; Ensembles and Violins
    2 - Decorative went wrong that time (cause unknown) and come from the 21:29 scan. All 12 are in the map
    now (`SCANNED`, (value, name) pairs; names as the `.nki` and Kontakt's window give them; review page
    https://claude.ai/artifact/D7VXjMSZfd1xRy3x5TzzBZ, OCR confirmed by the owner): every value is the
    instrument's All techniques value; Violins 2 - Decorative's Trill (Major 2nd), in neither scan, is 71
    (confirmed by the owner in Kontakt); Long Sul G / C in Violins 1 / 2 and Celli - Core play nothing, as in
    All techniques (`expect="silent"`). No `scan="values"` patch is left; the 7 key patches have no key names.
    The one-drum patches' keys are not in the files: their zones sit on keys 0-31 (round robins, dynamic
    layers) and Spitfire's script lays the techniques on the keyboard when the patch loads. The kits the owner
    screenshotted lay each drum's default techniques on consecutive white keys in the `.nki`'s group order
    (Bongos: Hand flam 48, Hand bass 50, Hand tone 52, Finger flam 53, bass 55, slap 57, Hit 59; Snare 1: Hit
    36, Edge 38, Rim 40). So a key scan of the 42 (which keys sound) plus the group order should name them, to
    be reviewed by the owner. *Tick the patches to scan* now ticks a kit's own drum patches whose keys the map
    lacks (extra, keyScan, no `<Drum>`: the 42), not the 7 scanned on 2026-09-27. About 5-6 minutes a patch.
    **In the background** (the owner, 2026-09-28: "exactly like" the extract): `Scan SSO drum keys in
    background.bat` (`main/…bat.in`, installed to bin) starts `MuseScore3Evo.exe --scan-keys "Spitfire Symphony
    Orchestra"` (`[--extract-patches <file>]`; `scanKeysMode`, which also sets `extractMode`: the same process
    rules, setups copy, lock, log `background extract.log` and `DialogWatch`; no relaunch).
    `ArticulationCheckDialog::runHeadlessKeyScan` ticks `toScanNow` (the same as the button) and runs `check()`,
    which says its steps in the log (patch n of m, minutes left, each result) instead of message boxes; the key
    scan opens no plug-in window then (a window of a windowless process would pop up on the owner's screen), so
    it listens only: which keys sound, no sheet. Tried here with the test synth (a map with two drum patches
    without keys and one with): the two scanned, 2.6 min each, log, summary and zip as the extract's; so about
    2 hours for the 42.
    **The owner's run (2026-09-28 01:46, run 182, 112 min, all 42):** every patch sounds; the keys that sound and
    their peaks are kept in `tools/soundlibraries/sso_drum_keys_sounding.json` (keys around -45 … -60 dB next to
    loud ones are most likely the previous key still ringing). The one-drum patches do **not** follow the kits'
    white-key layout: Snare 1 sounds on 41-53 chromatic (41-47 rising 27 → 10 dB, a ramp), then 55, 57, 59-61;
    Triangle 1 on 48-51, 53, 55, 59-60, 64-65. So the keys can't be named from the file's order: which key plays
    which hit needs the patch's hit list (the owner's Kickstart screenshots, as for the kits). What playback
    needs from them: Snare 1 / 2 roll and x stick, Triangle 1 / 2 (the rest the kits already play). Now in the map (above).
    **Pictures of the percussion windows, in the background** (the owner, 2026-09-28: "let's close off these
    gaps"): Kickstart shows a drum's hits and keys in its window only (a one-drum patch as it loads, a patch with
    several drums once its icon is clicked; the six ensembles' rows are 4-8 drums, Contemporary's 8th off the row's
    left end). `Take SSO percussion pictures in background.bat` starts `MuseScore3Evo.exe --window-pictures
    "Spitfire Symphony Orchestra"` (`[--extract-patches <file>]`; `picturesMode`, extractMode's process rules, log
    `background extract.log`): `ArticulationCheckDialog::runHeadlessPictures` takes every keyScan patch named
    "Percussion - …" or "Ensembles - …" (`isPicturePatch`, 48), loads it on one instance, waits until it sounds, opens
    its window off the screen (-20000, not activated, a Tool window; `DialogWatch` leaves it: `isPictureWindow`),
    grabs it (PrintWindow; if that is blank, a moment in the screen's corner), then clicks each drum icon
    (`ArticulationCheck::drumIcons`: a name under each slot of a 100-pixel grid from the right, Kontakt's whole window
    only; `pluginMouse`: WM_LBUTTONDOWN/UP posted to the plug-in's window under the point, Windows only), each picture
    once the window has settled (`settled`: the same twice, the drum row drawn; up to 30 s). **The owner's first run
    (2026-09-28 04:41, run 194, 6 min):** the clicks work off the screen (Traditional Orchestra's Toms: Tom 1-5 on C3
    E3 G3 B3 D4), but the pictures were taken 3 s after opening, while Kontakt still drew its window at 1010 x 647, so
    46 of 48 had no row and no icon; hence `settled`. Every ensemble's row shows all its drums (4, 5, 6, 8, 8, 9: as
    many as each file has), so the wheel step was dropped. `<library> windows <date>/`: `<patch>.png`, `<patch> - n.png` (icon n from the right), pictures.json,
    summary.txt, zip. Tried here only with the test synth (no editor: "the plug-in has no window"); the window,
    the off-screen grab and the clicks are untried with Kontakt. Test `drumIcons`.
    **The owner's second run (2026-09-28 05:02, run 198, 8.8 min):** all 48 patches, every drum clicked (4-9 icons per
    ensemble). 82 hit lists, 519 hits, read by OCR and checked by eye (8 keys corrected), reviewed on
    https://claude.ai/artifact/CKcFbayqAj3iPi9h3irUFK (the owner: correct), kept in
    `tools/soundlibraries/sso_percussion_hits.json` (per patch and drum: hit names and keys, null = off) with its
    notes: every technique of a one-drum patch is on at its defaults with a key (rolls, swells, FX), 395 of their 397
    keys sounded in the 01:46 scan; Rain Sheet Swell mp plays nothing (Voices 0; its zones are like Swell mf's, so
    it's Spitfire's script; it plays in the Unpitched - Metal kit once switched on there, the owner), Cymbal Hi Choked Hit sounds though the scan heard nothing; in the ensembles many
    techniques are off, with no key at all, and Low Ensemble's Toms 3-5 share E2 and its Field Drum Rim / X Stick A2
    (the owner: a real conflict; set them in Kontakt when a part needs them). The owner's decisions (2026-09-28): (a) every one-drum
    patch's list is in the map (`SINGLE_HITS` from the JSON, `<Drum key name>` for reference; "save all the data we can
    so future work will be easier"; the screenshots of Snare 1 / 2 and Triangle 1 / 2 checked against it); (b) not now:
    the rolls (and other techniques) the kit patches have off are not played from the drum's own patch; **a future
    system is to switch an off technique on in the kit patch and give it a key when a score needs it**; (c) the six
    ensembles' lists are in the map too, as `<Drum key name>` children of their `<Patch>` (the owner: "make sure the
    map (and reference files) is 100% complete whether used or not"). Every technique off at a patch's defaults, which
    has no key, is an entry too: `<Drum name default="off"/>` with no key and no pitch (never played; the check and
    the key scan skip it), in the one-drum patches, the ensembles and the kits (the kits' others, named only as the
    kit shows them, in a comment). Test `spitfireMap`.
    Harp glissandi's keyswitches from the 20:15 pictures (review page https://claude.ai/artifact/DDuuuj2uqZhxb1CjgumQtD,
    the owner: "OCR correct"): 0 Whole (selected at load, label "KEYSWITCH C0"; from the order), 1 Minor H., 2 Minor
    M., 3 Major, 4 Pentatonic, 5 Diminished; 102-103 no change (a ring). In the map (`SCANNED_KEYS`; a `<Patch>` now
    takes a `<Switch>` child).
    The owner's scan of the 23 (2026-09-27 20:15, run 165, with Win+D: the pictures came out, PrintWindow draws
    windows hidden that way) did the 4 Curated Ensembles (values in their names' alphabetical order: Brass 9,
    Strings 16, Tutti 13, Woodwinds 9; review page https://claude.ai/artifact/RLjhTuv28WtqGNcsF1kVMR), the 6
    percussion ensembles and Harp glissandi (keys that sound and keyswitches; Kickstart names no key), then hung on
    Basses - Core techniques: the test note was 60, above its samples (24-78), and the check waits for a sound.
    `<Patch pitch=>` now gives each patch the middle of its zones' keys (`sso_nki_keys.json`: lowest, highest,
    median; every program says 0-127), `LibInstrument::testPitch`, used by `ArticulationCheckDialog::testPitch`.
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
`[windows-build]`. The workflow keeps the build folder in a GitHub cache (`msvc-build-x64-<sha>`) and
restores the latest one, setting unchanged files' times back to 2000 so MSBuild compiles only what changed
since the cached commit (the owner, 2026-09-27: "don't rebuild everything every time"); a change to a
header in the precompiled header still rebuilds most. **Actions minutes** (the owner, 2026-09-27, after
run 72 was started only to see the merged code compile on MSVC; the owner then ran out of minutes, and on
2026-09-28 made the repository public, where standard runners are free, so minutes no longer limit builds.
The rest still holds: a build takes about 15 minutes of the owner's wait, and the runs list stays readable).
**Public repository:** everything in it is visible to anyone. Never commit Spitfire's or NI's files (samples,
`.nki`, presets, scripts, expression maps), the owner's own scores or extracts, logs with their paths, keys or
e-mail addresses; derived names, titles and key ranges only (as before). Start a Windows build only
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
