# History and background

Read only when you need the background of something: why a rule or number is what it is, what a run or
measurement showed, what the owner tried and said. The current-state guide is `CLAUDE.md`; current open items
are in `HANDOFF.md`. Nothing here is required to work correctly: every rule, gotcha, path and format an agent
needs is in CLAUDE.md, HANDOFF.md, LIVE.md, VERIFY.md, PLAYABILITY.md or the header comments.

This file holds, verbatim and by section, the former CLAUDE.md and HANDOFF.md as of 2026-10-01 (branch
`live-set-export` after merging `claude/intelligent-volta-gx7gmw`), when CLAUDE.md was rewritten as a short
current-state guide. Headings are one level deeper than they were; the text is unchanged. Where it describes a
design, the code's header comments are the maintained version. New history goes in commit messages (and, for
long narratives, a dated section appended here).

Contents:
- Part 1: the former CLAUDE.md (architecture in full detail, the owner's runs, measurements, CI history)
- Part 2: the former HANDOFF.md (dated work logs since 2026-09-25)
- Part 3: the measurement branch's HANDOFF.md additions (VM measurements, sweeps, extraction state; merged 2026-10-02)

# Part 1: the former CLAUDE.md

## CLAUDE.md: notes for agents working on this repository

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

### Branches

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
- `live-set-export` (2026-09-30; worktree `wt-liveset`): Create Live Set, with live-integration, live-clip-edit,
  piano-v37-fixes and legato-timing merged; Live against MuseScore (the rule below).
- `live-integration` (2026-09-28, from main at f12a240; worktree `wt-live`): playing through
  Ableton Live 12: MIDI clock / SPP out, *Mixer › Play through Live*, automation imported from a
  Live Set as read-only lanes. **Read `LIVE.md`** (design, the owner's setup, what is unverified,
  the Tracktion fallback). Test `tst_liveintegration`.
- CI runs by hand only (`.github/workflows/build_all.yml`, workflow_dispatch). This kept the
  private repo's Actions minutes; the repository is public since 2026-09-28 (the owner ran out of
  minutes), and public repositories run Actions on GitHub's standard runners for free.

### Rule: readings of music go on a review page

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

### Rule: Live and MuseScore sound alike

The owner (2026-09-30): **"make it a rule that Ableton's audio output and MuseScore's audio output for SSO must
match."** Any change to SSO playback (the renderer, the hosting, the Mixer, the Controllers, the tuning) must reach
the Live path too: the clips "Live plays the score" sends (notes and carrier notes: `libmscore/liveclips.*`), the
set Create Live Set writes (each patch's Kontakt state, Live's track mixer: `mscore/livesetexport.*`,
`libmscore/livesetwriter.*`) or the MuseScore Link device (`tools/live/`). Otherwise list it in LIVE.md › Live
against MuseScore › What still differs, as a difference to fix, and keep that list current. Check with
`tst_liveequivalence` (the test synth, strict: the whole render must match) and, for SSO, `MuseScore3Evo.exe
--live-equivalence <folder> <score>` on the Windows VM (LIVE.md › The check), and say which you ran.

### Rule: files survive a round trip to MuseScore 3.6

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

Note input by scale degree (the owner, 2026-10-01): numpad 1-7 (`note-degree-1` … `-7`, pitched staves, as the letter
keys) enter the key's degrees: `Score::scaleDegreeStep` counts letters from the tonic of the staff's key signature where
notes go (as written; its mode picks the tonic: C minor's 1 is C, A minor's A; no mode counts as major, so three flats
is E flat), then `cmdAddPitch` as for that letter (key signature accidentals, nearest octave). No raised leading note in
minor. `chord-degree-n` / `insert-degree-n` have no default keys (Ctrl+digits are tuplets; Shift / Alt with the numpad are
taken on Windows); *Add › Notes › Scale Degree*. A user's own shortcuts.xml saved before an action existed now gives that
action its default keys where the file doesn't bind them elsewhere (`Shortcut::load`). Qt on Linux (xcb) matches
shortcuts without the keypad modifier, so a `NumPad+n` binding never wins there over the plain digit's (durations);
`ScoreView::event` takes numpad 1-7 itself when a degree action holds that key and is enabled (`numpadDegreeAction`).
Tried in the GUI under Xvfb: C minor, numpad 1-7 gave C D E♭ F G A♭ B♭; an old user shortcuts.xml binding numpad 3
elsewhere left Degree 3 without a key, the others got theirs. Not tried on Windows. Test `tst_keysig::scaleDegrees`.

### Layout of the fork-specific code

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

Phrase marks (`libmscore/slur.h`, "Phrase marks"; the owner, 2026-09-30): MuseScore has no phrase-mark
element, phrase marks are drawn as slurs, and playback (MS4's rule, `Ms4::chordArticulations`) plays every
slur legato (the library then picks the Performance legato patch and overlaps the notes). A slur can be
marked as a phrase mark: right-click › *Phrase mark (no legato)* (checkable; the selected slurs with it),
the Inspector's Slur section, *Add › Lines › Phrase mark* / **Alt+S** (`add-phrase-mark`; shortcuts.xml,
-Mac (Option+S), _AZERTY): like S it adds a slur (note entry too), marked; with slurs selected it toggles
them (all become phrase marks unless all are, one undo step). A phrase mark is not a slur for playback in
any mode: no Art::Legato (MS4 model and the library), not cutting off another slur (`Dynamics::build`'s
collision-free intervals), not 100 % gate time in the MS3 model (`createPlayEvents`), not a bow stroke for
the playability checker; an ordinary slur inside it plays legato as before. Drawn on screen in
`Playability::openStringColor` (the preference, default slate grey #7d8791; `SlurSegment::draw`), selected
in the selection colour, printed / PDF / PNG / SVG in black; a colour the user set on the slur wins.
`Slur` has Pid::PHRASE_MARK (linked: parts follow; a segment passes it to its slur), not written in the
slur's XML (only in the clipboard's, so copy / paste keeps it): the metaTag `phraseMarks` (JSON tick,
tick2, track, track2), written on save by every score of the file (master and parts) from its slurs,
read after loading (`MasterScore::read`: the master's onto its slurs and their linked copies, each part's
onto its own), matched by start, end and track (a slur 3.6 moved or deleted loses it; nothing else gains
it). Absent when there are none, so such files are unchanged. Test `tst_phrasemark` (playback in MS4 and
MS3 models, file, undo, copy / paste, parts) and `tst_soundlibrary::renderPhraseMark`. No automatic phrase
detection (the owner asked for the manual toggle only).

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

Playability checker (branch `playability-checker`; `PLAYABILITY.md` has the plan and state): the owner's
Playability Checker plugin built in (the plugin is deprecated). `libmscore/playabilityrules.*` (string
rules: stops, harmonics, bowing limits, each with its textbook source), `libmscore/playability.*` (the pass
after each layout, `Score::playability()`; `inspect()` for the panel), marks drawn in `Note::draw`, never
saved or printed; `libmscore/playabilitydiagram.*` (fingerboard, harmonic board and W1 wind register graph
as display lists; wind data `playabilitywinds.h`, generated by `tools/playability/gen_winds.py` from the
plugin's `winds.js`); `mscore/playabilitypanel.*` (*View › Playability Panel*). Preferences
`score/playability/*` and *View › Check Playability / Mark Open Strings*. Scordatura: the part's String Data,
and "scord. F D A E" / "normal tuning" staff texts; *View › Show Scordatura as Fingered* places notes on retuned
strings as fingered (`Note::displayTpc`/`displayEpitch`, not saved; `Note::write` saves the sounding
accidentals, so the file doesn't change) (PLAYABILITY.md). Test `tst_playability` (parity with
the plugin's results and drawings on its test scores, plus scordatura).

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
  copies the part's controllers (dynamics, pedal) to every patch (before the notes at their tick, as the main
  patch's: 2026-09-30, found by the Live equivalence check; hosted it made no difference, in Live the note started
  at the old dynamic). An articulation switch
  (`NPlayEvent::librarySwitch`) goes before each note. Events carry the route
  (`NPlayEvent::setExternal(port, channel)`); the old duplicate-controller pass compares routes,
  not channels. A legato articulation's note lasts DIVISION/16 into the next (Spitfire legato
  needs the overlap), only while a slur goes on past it: a slur's last note ends on time, so the
  unslurred note after it gets its own attack, not a legato transition (the owner, 2026-09-30). A sampled trill or tremolo plays the note once (`SndConfig::ms4Once`).
  **Legato transitions start early** (the owner, 2026-09-30, "go ahead"; branch `legato-timing`): SSO's 42
  Performance patches reach a slurred note's new pitch 70-430 ms after its note-on (median 180 over 252
  transitions, a 4-22 dB dip; the timing check, `sso_articulation_timing.json` `legato`), so slurred notes sounded
  late. `<Articulation legatoDelay>` (ms; `gen_spitfire_sso.py`; **by interval since 2026-10-01**: `interval:ms` pairs
  from the legato grid `sso_legato_grid.json`, per patch and interval the median over 9 velocities, `Articulation::
  legatoDelays` / `legatoDelayAt(interval)` (±12 left out for now: the grid's octaves have octave errors, Basses +12 680,
  and the sweep heard octave slurs 120-176 ms early; beyond ±7 the ±7 delay): linear between listed intervals, the widest's beyond, one number for
  every interval when there is no grid; the interval is from the nearest note of the chord before that goes on legato
  on the patch. Medians over the 45 patches -12: 210, -7: 230, -5: 190, -1: 170, +1: 150, +2: 160, +7: 230, +12: 360;
  per patch 60-690; velocity changes nothing, median 190 at each. Before, one number per patch, the median of the
  timing check's six transitions, +2 / -5 only: leaps of a fourth or more stayed 100-280 ms late). **The 8 string
  Performance patches from 5 starting pitches** (`sso_legato_grid_pitches.json`, the rest check's legatopitches part,
  timed by harmonics; `legatoDelayFromPitches`): per interval the median 50 % time over the starting pitches (the
  crossing the sweep's arrival measures), -12 … +7; +12 (405-680, the first note's room) takes -12's 110-205, where the
  sweep of 3f0cda5 put octave slurs (+12 121 ms early, -12 86 early on the ±7 delay). Basses 70-160 (were 210-300),
  Violins 2 140-175 (230-280): the sweep's early half notes there. The other 35 Performance patches: the grid above,
  octaves on ±7 still (not measured from several pitches yet; the single-pitch grid has no common offset to correct
  by: against the 5-pitch 50 % time it reads a median 95-98 ms long on Basses / Violins 2, 58 on Violas, -25 … +30 on
  the others, so it stays until `legatopitches` measures those 35; HANDOFF.md › Legato and onset timing). And
  `<Legato early="…"/>` (percent; per score metaTag `soundLibraryLegatoEarly`, *Mixer › Advanced Options…* "Legato
  transitions early by", `SoundLibraryOptions::_legatoEarly`; `SoundLib::legatoEarly`): a transition (the
  `legatoTransition` lambda in `collectMeasureEventsMs4`: legato on a patch with a delay, the chord just before on
  its track slurred into it (`slurGoesOn`) and legato on the same patch, in the same pass, no key struck again, no
  grace notes or arpeggio before) starts `delay × percent` earlier in time (`SndConfig::libEarly`, converted at the
  tempo there with `utick2utime` / `utime2utick` in `collectNote`), not before the chunk (`libChunkStart`) or the
  pass's start, and the note before loses at most a share of its length in time (`libEarliest`): none up to 125 ms,
  rising linearly to half at 250 ms and longer (`qBound(0.0, (len - 0.125) / 0.25, 0.5)`). **Fast runs (the owner,
  2026-09-30)**: the cap was half; in "Whence" the cellos' sixteenths at 110 (136 ms) under 4-note slurs (Celli
  Performance, delay above a sixteenth) all got the full half: as played 68 / 136 / 136 / 205 ms, levels −46.4 /
  −44.8 / −40.5 / −44.9 dB (on the beat: 136 ms each, −43.8 / −43.1 / −43.6 / −44.2) -- a slur's first note squeezed,
  the third ~4 dB louder. SSO plays faster transitions in fast passages. Measured on the VM: capped at a quarter
  (102 / 136 / 136 / 170 ms) still −47.0 / −44.7 / −42.8 / −45.6 (4.2 dB spread, against 1.1 on the beat), so a
  136 ms note gets next to nothing (6 ms); eighths at 120 (250 ms, 8-note slurs) are as even shifted by half as on
  the beat (~3 dB spread, all three) and land best with half (pitch a median 58 ms after the beat; a quarter 123, on
  the beat 183), so 250 ms keeps the half. Quarters at 60 / 120 unchanged (median 45 / 31 ms). Test
  `legatoEarlyFastRun` (legato-fast.musicxml). Its note-off, the switches and the
  controllers stay; the previous note still overlaps it. A chunk doesn't end where a library part's slur goes on
  (`libSlurAcross`), so a transition is never a chunk's first note. The glide of its tuning lane moves with the note-on.
  Built-in playback untouched. The playback verify tool reads the notes from the same events (the reference too),
  so a shifted note is judged at its new time; its drift check now leaves out strikes that are all legato (a
  transition's onset is where the slide puts it, no timing mark: a legato window against a detached one read as
  drift, on the owner's Violins before this change too; test `playbackVerifyDrift`). Test `legatoEarly`
  **Held notes early by their onset** (2026-10-01, HANDOFF problem 3, "slow attacks"): SSO's held notes reach full
  level a median 175 ms after the note-on (Violas ~300, flautando / sul tasto / harmonics up to 1 s), but that is
  mostly the bow's swell: the sound starts within 15-50 ms. Measured on the VM (build ce7d801, lone held notes, 13
  instruments x 3 registers x pp / mf / ff, Violins 1 sul tasto / flautando / harmonics; K-weighted level in 10 ms
  hops against the note's peak in its first 1.5 s): the time to -15 dB under the peak is the perceptual onset (Vos &
  Rasch 1981; -6 dB jumps 150 ms where a swell levels off), 10-60 ms for most longs (brass median 26, Performance
  strings 42, woodwinds 37), 175-440 ms for the slow techniques; per family it follows the rest check's per-semitone mf
  full time (`sso_sound_range.json`): brass 0.11 full + 21 ms, strings 0.15 full + 22, woodwinds 0.28 full + 8, sul
  tasto / flautando / harmonics 0.58 full - 118 (rms 12-48 ms). `<Articulation onset>` (ms or `pitch:ms` pairs;
  `Articulation::onsets` / `onsetAt(pitch)`, the same reader as legatoDelay, `readKeyedMs` / `keyedMsAt`) on longs and
  legato only (gen_spitfire_sso.py `onset()`: smoothed, Douglas-Peucker within 10 ms / 10 %) and `<Onset early="100"/>`
  (percent; **measured since 2026-10-01** where the rest check's onset part ran, `sso_sound_onset.json`: per semitone
  the mf -15 dB time on the perceived envelope less 30 ms of analysis latency (plucks come out at 28-53 ms; power was used
  first and badly under-read slow swells: Violins 1 Super Sul Tasto 122 ms on power, 598 perceived, and the sweep of
  build ed3a294 heard sul tasto 240-600 ms and flautando 145-255 ms late); unmeasured sounds: per articulation kind
  (`articulationKind`, brass apart), the measured patches' median of onset / mf full-level time times the patch's own
  full-level time (Long 0.39, Legato 0.31 (brass 0.22), Sul Tasto 0.57, Super Sul Tasto 0.87, Flautando 0.66, Harmonics
  0.48), the family fit only where no kind has 20 measured semitones; a sul tasto / flautando / harmonics semitone
  whose mf peak is at 1.3 s or later (still rising at the check's 1.5 s end: 40-86 % of theirs, no other sound's) takes
  the -10 dB time, since its full level is ~5 dB higher (the sweep of 3f0cda5, its peak up to ~2 s after the note-on,
  heard those 123-223 ms late; -10 comes 130-177 ms after -15; `onsetMs`; dropped in c27da62: the sweep of e6f44e6
  heard such notes up to 399 ms early, so now the -15 dB time, never more than a neighbouring semitone's: err late).
  Legato delays from the 5-pitch grid of all 43 Performance patches since c27da62, octaves from each patch's median
  of -7 -5 +5 +7 (both measured octaves unusable).
  a289780 (calibrated on the sweeps of e6f44e6 and c27da62): swells each semitone's own -15 dB time (predicted median
  +59 ms, none early); -12 the measured one, +12 the fourths' and fifths' median + 60 / 45 / 95 ms; Oboe Solo and
  Violins 2 legato +60 / +45.
  2de0b27: Violins 2 halved to +25. 34cb6c4: both legato octaves from the template-fit timing (tMidMs, the measurement
  branch's 42c6117: each frame a mix of the two notes' spectra, the new note's 50 % power share; synthetic check
  within -40 ... +60 ms), the +45 / +60 / +95 stand-ins gone; family +12 / -12 strings 350 / 210, woodwinds 125 / 170,
  brass 130 / 140 ms. All 112 non-percussion map instruments
  measured since f8908f8 (2026-10-01);
  metaTag `soundLibraryOnsetEarly`, *Mixer › Advanced Options…* "Held notes early by"; `SoundLib::onsetEarly`).
  Renderer (`collect` in `collectMeasureEventsMs4`): a library note that is not a legato transition, not tied into, with
  no grace notes or arpeggio before, plays `onset × percent` early (its chord's latest onset, so a chord starts
  together; `SndConfig::libEarly` as for transitions), capped by the note just before on its track on the same patch as
  transitions are (`onsetEarliest`: none up to 125 ms, half from 250 ms), the chunk's and the pass's start. Each such
  note goes in `libShifts` (`SndConfig::libOn` / `libWrittenOn`); `finishLibraryEvents` first ends what ends on the same
  channel and patch between the new and the written start at the new start (a Performance patch would play the overlap
  as a legato transition, not the note's attack; not a note that started after the new start), and moves the note's
  switch (put at the written tick) and the channel's controllers at the chord's tick (its dynamic; only when no other
  note of the channel starts in between) to the new start. Live playback's chunks don't end before a measure where a
  library part starts a note (`libNoteAfter`), up to twice the chunk size; export, verify and Live clips render a pass
  as one chunk. Test `onsetEarly` (legato-early.musicxml: a lone note by pitch, slurs' first notes, a repeated key, the
  switch moved, the note before ended, 0 %).
  (legato-early.musicxml). Interval: +2 and -5 differ by up to 250 ms on some patches, either way round (Oboes a2
  90 / 340, Bass Flute 220 / 100): with two intervals measured, one number per patch.
  **Measured on the VM with SSO (2026-09-30, build a1b1e74 against df273c3)**: Solo Violin, Violins 1 and Flute Solo
  Performance, slurred D5 E5 F5 A5 D6 C6 G5 D5 at 60 and 120 bpm (42 transitions) plus an eighth run at 120; when the
  new pitch is within 35 cents (YIN every 5 ms) after its beat: median 230 ms before (85-465), at 50 % 128, at 75 % 80,
  at 100 % 40 (20 of 42 within ±40 ms, one 59 ms early); per part at 100 %: Solo Violin 26 / 25, Violins 130 / 102,
  Flute 36 / 31 ms (60 / 120 bpm). Leaps of a fourth or fifth stay 100-280 ms late (slower than the +2 / -5 the delay
  is from; Violins' A5>D6, C6>G5, Flute's C6>G5, G5>D5). The eighth runs are capped (half an eighth, 125 ms): median
  +58 ms. Fresh first notes: 3-45 ms, unchanged. **Default `<Legato early="100"/>`**: the full arrival median lands
  40 ms late, where the ear already hears the new note (the owner expected the full delay might feel early: on the
  fully-arrived measure it doesn't; 75 % left transitions 80 ms late). Test score and analysis: the job's
  tmp/legato (legato-timing.musicxml, analyze.py).
  Dynamics go on the library's CC (CC1 for Spitfire).
  Shorts (the owner, 2026-09-28: at pp the staccatos stood out; their velocity was MS4's soundfont one, 56 at
  pp and 65 at mf, while CC1 went 32 → 80, and Spitfire's shorts take their dynamics from velocity only):
  a base listed in `<Dynamics velocity="short staccatissimo spiccato marcato tenuto pizzicato bartok collegno">`
  (`Library::velocityDynamics`) gets `NoteResult::levelVelocity`: the dynamic level on CC1's scale
  (`expressionLevel`), times MS4's velocity over a plain note's (an accent: pp 32 → 48, mf 80 → 108; the
  curve's peak would make an accented pp short 113). Longs and legato keep MS4's velocity (it doesn't
  set SSO's legato speed: the timing check's two-note transitions at velocity 20 / 64 / 110 took the same time on
  most Performance patches, median spread 0 ms over 84 patch-interval pairs, 46 identical, the widest 270 ms with no
  common direction; `sso_articulation_timing.json`). The library's dynamics CC now goes ahead of the notes at its tick (a long starting on
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
  (bin) starts it from its own folder (a patch list file dropped on it); a second run stops at the lock. (It
  first copied the install to `%LOCALAPPDATA%\MuseScore background dynamics check`; the owner, 2026-09-28: "you
  don't need MuseScore to copy itself, just run at its original location (I'll keep the folders separate)".) Tried here headless with the test synth (a map DynTest.xml, the synth's state as setups):
  2 patches in 4 s, the unused value skipped, harmonics measured an octave up, the balance report in the summary.
  **The owner's background run (2026-09-28 01:40, 56 min, 159 patches, 417 curves)**: shorts balanced (Violas
  Spiccato -14 / -12 / -7 -> within 0.3 dB). On the controller, a patch other than the held note's differs
  (Violas' All techniques Long +10 / +3 / -2 dB against the Performance legato; Rachm. +11 at pp): one CC per
  patch fitted to its long fixed Long / Rachm. / Marcato at mf but put tremolo and trills 5-11 dB off (left
  out; `calibratedController` kept). **The owner's choice: controller articulations stay as Spitfire made
  them**; only the shorts are calibrated, and the report flags only a short out of its velocity range.
  12 patches "played nothing": no instrument templates in the background process, all tested at 60; mapped
  patches now carry `pitch=` (their .nki's median zone key), and a patch no notation plays isn't loaded.
  **Section strings' shorts and Long (Rachm.)** (the owner, 2026-09-28): staccato plays Short 0.5,
  staccatissimo Spiccato, tenuto / portato Short 1.0 (Violins 1/2, Violas, Celli, Basses, Strings
  Ensemble); staff text "espr." / "espressivo" / "molto vib." / "con vibrato" sets the modifier
  `espressivo` (until "non vib." / "senza vib." / "ord."): a held note plays Long (Rachm.) (Rachmaninoff:
  Spitfire's romantic long), a slurred one keeps the Performance legato (legato is tried before long).
  Not the default held sound (the owner). Short Brushed (CS) and Fx stay unmapped.
  **Automation (infrastructure, no UI yet; the owner, 2026-09-28: "start building a full automation system …
  just build the infrastructure")**: `libmscore/automation.*`. Per part, lanes: a target (a map controller id —
  vibrato, mic1, … — or "cc<n>") and points (score tick, value 0-1, step / linear to the next); before the
  first point a lane says nothing, after the last it stays. metaTag `automation` (JSON, by part as
  partcontrollers; kept by 3.6). `Lane::valueAt`, `Lane::events(tick1, tick2, step, resolution)` (the value at a
  chunk's start, points, ramps every 30 ticks). Rendered (`LibPart::automation`, built with the routes, rebuilt
  when the metaTag changes): a lane takes its controller's place (part value, staff texts); a CC lane as
  CC events, a plug-in parameter's as **ME_PARAMETER** events (event.h 0x5: dataA the controller's index in
  the main patch's allControllers, the value 0-1 in tuning(): MuseScore's event bytes can't hold more —
  a first try with controller numbers from 4096 was cut to a byte and became CC1). Only with the plug-in
  output. Seq::playEvent passes ME_PARAMETER to the plug-in; MIDI export skips it. `Vst3Synth::deliver`:
  `_parameters[slot][index]` -> `Vst3Plugin::queueParameter` (processor only, audio thread);
  `SoundLibraryHost::sync` and the command-line export fill the ids (`parameterIds`: the part's main patch
  controllers' titles, looked up on each slot's instance). The SSO extract (sso_patch_controls.json) names
  what can be automated per patch (Dynamics, Expression, Mic 1-5 level, Mic Mix Distance, Release,
  Tightness, Vibrato, Variation, Reverb …). Not yet: a UI; a dynamics lane (CC1 from notation stays);
  which mic "Mic 1 … 5" is; whether Spitfire's scripts keep a value set this way (untried with Kontakt).
  Test `automation`.
  **Recommended short notes' balance** (the owner, 2026-09-28: "measure out a recommended number for each of the
  sections, and add a button called recommended"): each dynamics note also gets a perceived loudness
  (`ArticulationCheck::perceivedLoudnessDb`: K-weighting (BS.1770), auditory filters one ERB apart with a
  rounded-exponential shape (Glasberg & Moore 1990), each (E + A)^0.3 - A^0.3 summed (threshold A),
  short-term smoothing attack 22 / release 50 ms, its peak as 33.2 log10: Glasberg & Moore 2002 simplified).
  Hard Bark bands first: a tone split over two bands came out several dB louder (2 kHz +3 over 4 kHz);
  overlapping filters fix it. `DynamicsCurve::perceived` ("perceived" in dynamics.json). `recommendedBalance`:
  per family, shorts matched in energy to the held note (balance 0) at pp / mf / ff, how much louder they
  sound, the median negated to 0.5 dB. Advanced Options › *Recommended* (right of *Library's*); the report
  lists the recommendations. Needs one background dynamics run with this build. Test `perceivedLoudness`.
  **Attack salience** (the owner, 2026-09-28, "Whence": strings -4 dB "def sounds better", the loudness
  model said +1: a short's sharp, bright attack stands out more than its loudness, and short-term loudness's
  22 ms smoothing hides the transient). `ArticulationCheck::attackSalience(clip)`: Glasberg & Moore 2002's
  instantaneous loudness from the same ERB-spaced filters as 4th-order gammatones in the time domain (Patterson
  et al. 1992, Hohmann 2002's complex one-pole cascade; power per 1 ms on perceivedLoudnessDb's scale, same
  compression; short FFT windows, G&M's own multi-resolution way, smeared a steady tone over more filters: +3 dB),
  smoothed only by a
  5 ms temporal window (ERD ~8 ms, Plack & Moore 1990): `fastDb`; the same with Zwicker's sharpness weighting
  g(z) per band (DIN 45692; brightness makes onsets salient: Huang & Elhilali 2017): `salienceDb`; the rise
  10 -> 90 % (`riseMs`, informative only). The dynamics check stores `DynamicsResult::attack` / `riseMs` per curve
  point (results.json "attack", "riseMs"), dynamics.json "attack" (`DynamicsCurve::attack`, `attackAt`).
  Model (`SoundLib::recommendation`, `fitSalience`): per family, over the energy-matched shorts at pp/mf/ff,
  L = median perceived difference, S = median of (attack diff - perceived diff); recommended = -(L + w S).
  ONE free parameter, w (weight of attack salience), least squares over the owner's references
  (`heard()`: map `<Dynamics heard="strings=-4">` = `Library::heardBalance`, overridden/extended by
  dynamics.json "heardBalanceDb", set in Advanced Options › *Heard right* menu, `SoundLibraryOptions::setHeard`):
  w = sum S(-L-t) / sum S^2, not under 0; one reference is reproduced exactly. No attack curves (a dynamics.json
  from before) or no reference with them: loudness only, exactly as before. The report ("# Dynamics balance")
  lists per family loudness only, with attack salience (L, S, notes), heard right, and w with the families it
  was fitted on; each short's line: at mf matched, how it sounds and its attack beyond that. The test synth's
  articulation 62: a short with a 4 ms click of high harmonics. Tests `attackSalience`, `salienceFit`,
  `dynamicsCheck`. Untried with SSO: needs a re-measurement (the 2026-09-28 12:30 one has no attack curves);
  S for SSO's families, hence w and the other families' numbers, unknown until then. What S holds (synthetic,
  at one loudest-50-ms energy, attack minus perceived): steady tones 0-1 dB (mid; +3 at 4 kHz, +13 at 8 kHz:
  the sharpness weighting), a softly rising decaying short +3.6, the same with a 3 ms 7-12 kHz click +23. So
  S is mostly "how much short-term loudness's 22 ms integration marks a short down" plus the attack's click
  and brightness; w between 0 (loudness only) and 1 (no integration beyond the ear's 5 ms window).
  **Even dynamic steps** (the owner, 2026-09-28; "try both", decide by ear): SSO's held notes climb 5–12 dB pp→mf
  and 1–4 dB (sometimes less than 0) mf→ff on CC1 32/80/112. `SoundLib::evenStep(heldCurve, mode, cc)`: the held
  note's own range, ppp (CC 16) to fff (127), split linearly over MS4's CC scale; judged on the energy or the
  perceived curve; reached by VOLUME (CC1 unchanged, so each marking keeps Spitfire's recording/tone; CC11 turned
  down where the curve is above the step; it can't go up: CC11 is at 127 otherwise) or RECORDING (another CC1,
  the curve's inverse; the tone moves). Per score: metaTag `soundLibraryEvenSteps` = volume-hearing /
  volume-energy / recording-hearing / recording-energy (none: off), Advanced Options › *Even dynamic steps*.
  Rendering: `renderMs4Dynamics` per channel (`heldCurves`), CC11 only when no automation lane has CC11; the
  shorts' velocity is matched at the CC1 sent (`libVelocity`). The volume needs the held note's CC11 curve:
  the dynamics check measures it (CC11 16…112 with CC1 and velocity 80, `DynamicsResult::expression` /
  `expressionPerceived`, "expression" in dynamics.json; 127 = the curve's 80). Test `evenDynamicSteps`.
  **UI (2026-09-28, the owner: "tidy up the Sound Library menu")**: *View › Sound Library…* is the overview: a
  tree (`SoundLibraryDialog::_tree`), each part's row with its patch, *Controllers…* (kept until the automation
  system: the owner), setup state, *Show*, memory; its extra patches (+) and copies for other tunings (~) under
  it, closed. The settings are in **Mixer › Advanced Options…** (and a button in the Sound Library window):
  `SoundLibraryOptions` — this score: copies for other tunings (metaTag `soundLibraryLanes`) and the short
  notes' balance per family (metaTag `soundLibraryShortBalance`, `shortNotesBalance` / `writeShortBalance`:
  only what differs from the library's calibration; `calibratedVelocity(…, family, score)`); the library:
  its folder, *Measure dynamics in the background* / *Scan drum keys in the background* (start this
  MuseScore with --check-dynamics / --scan-keys). *Check articulations* has no button any more (its dialog
  runs the background jobs).
  **Short notes' balance per family** (the owner, 2026-09-28, "Whence" bar 15: staccatos quite a bit louder than
  legato): the calibration matched them (Violins 1: Spiccato at velocity 88 and the legato at CC 64 both -39.1 dB
  loudest 50 ms; uncalibrated, Spiccato at ~64 was -46 dB: Spitfire's own balance ~7 dB under), but a bright
  bow attack stands out by ear more than a flat 50 ms energy says. Balance per family ("why not just do this
  regardless"): `DynamicsCalibration::familyBalanceDb` ("familyBalanceDb" in dynamics.json; `balanceFor`, else
  `balanceDb`), `SoundLib::family(main)` from the main patch's folder (strings, solo strings, woodwinds, brass,
  other), `calibratedVelocity(…, family)`; *View › Sound Library…* one box per family; the report's per-patch
  header shows the family and its setting.
  **Shorts by the note's length** (the owner, 2026-09-28, "Whence" bar 8: accented staccato eighths at 110 on
  Short 0'5 rang on; "I thought we were already calculating the note's actual length"): `<Articulation
  length>` (Short 0.5 = 0.5 s, Short 1.0 = 1.0 s; gen_spitfire_sso.py `LENGTHS`) is skipped for a note under
  90 % of it (`choose`, `Want::seconds`); staccato falls back to spiccato, portato (staccato + tenuto) Short
  1'0 -> Short 0'5 -> spiccato, a fast plain tenuto Short 0'5 -> the held note (never a bouncing spiccato).
  **By measured sounding length since 2026-10-01** (HANDOFF problem 4): `<Articulation from>` (seconds,
  `Articulation::fromSeconds`) replaces the 90 % rule where it is given. The rest check's shorts part
  (`sso_short_lengths.json`: each short held 50-2000 ms at its test pitch and an octave either side, the last time its
  perceived loudness is within 10 dB of its peak) shows the note-off hardly cuts them: Spiccato 0.28-0.47 s whatever
  the note, Short 0.5 0.45-1.0, Short 1.0 0.47-1.26 growing with the note up to ~1 s (the -20 dB body, ~1 s, is mostly
  the hall). The target is the note's meant length (`Want::soundSeconds`: written length times MS4's duration factor
  for its articulations, ms4tables.h strings: staccato 50 %, staccatissimo 25 %, tenuto 99 %, portato the average
  74.5 %; the owner, 2026-10-01: a portato must stay detached, a 0.8 s one had played Short 1.0 ringing 130 ms into
  the next note). gen_spitfire_sso.py `shortFrom`: the meant length from which the articulation's sounding length
  (held as MS4 holds it) is closer to it than its fallback's (Short 0.5 against Spiccato, Short 1.0 against Short
  0.5) for every longer note: Violins 1 0.43 / 0.71 s, Violins 2 0.42 / 0.76, Violas 0.61 / 1.06, Celli 0.51 / 0.84, Basses 0.73 / 1.07
  (nominal 0.45 / 0.90); unmeasured patches (Strings Ensemble) keep the nominal rule.
  A note's length: `SoundLib::noteSeconds` / `TempoMap::writtenTime` (the tempo map: tempo changes, gradual
  tempo lines, fermatas; not the Play Panel's speed, `relTempo`, which used to change the choice) over the
  whole tie chain (it used the first chord only). The playability checker's `secondsBetween` uses it too
  (it read tempo texts only: no rit./accel. lines, fermatas). Tests `spitfireMap` (by length),
  `noteSecondsWritten`. "Whence" bars 7-8 Violins 2: Spiccato (were Short 0'5).
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
  **Timing, in the background** (the owner, 2026-09-29: "yes, build", the timing and legato run after the
  controller links): `Measure SSO timing in background.bat` starts `--extract-library … --check-timing`
  (`checkTimingMode`, as `--check-dynamics`: no supervisor, its own setups copy, lock and log `background timing
  check.log`; `runHeadless(…, timing)`, `_timingOnly`: `dynamicsPatch` calls `measureTiming`). Every mapped
  patch, each articulation a notation chooses, offline (`ArticulationCheck::timing`, 5 ms windows of the note's
  power against its own peak): start (30 dB under the peak), full (6 dB under) and peak at pp / mf / ff (velocity
  = CC1 = 32 / 80 / 112); mf held 2.5 s: how long it sounds (a short's own length) or, sustained, its release (to
  30 dB under its level before the note-off, tail up to 6 s); how long a 0.1 s note sounds; a legato articulation
  (the Performance patches): two notes slurred as MuseScore plays them (30 ms overlap) at velocity 20 / 64 / 110,
  +2 and -5 semitones, the pitch every 10 ms in 80 ms frames against the first note's
  (`PluginExtract::centsShift`): when it leaves the first note (35 cents), when it arrives (within 35 cents for 3
  frames), the level's dip. results.json `timing`; `tools/soundlibraries/timing_from_check.py <zips>` keeps the
  derived numbers as `sso_articulation_timing.json` and prints medians. Test `timingCheck` (the test synth's new
  14, a 200 ms attack and 300 ms ring, and 24, a one-voice legato gliding 300 / 150 / 60 ms by velocity: found
  250-280 / 130-140 / 50-60 ms; a glide under the 80 ms frame leaves and arrives in one frame). Tried here headless
  with the test synth (a map with a patch and its Performance extra, the synth's state as setups): 2 patches in 30 s,
  the unused articulation left out, summary, results.json and zip, read back by `timing_from_check.py`; a legato
  articulation's pitch analysis takes about 26 s of processor time. Untried with Kontakt; estimated about an hour
  for the ~160 patches, as the dynamics run. Not yet used by playback (early
  note starts, shorts by length, legato overlap by speed would use it).
  **The owner's first run (build 249, 2026-09-29 18:17) hung** on its 20th patch (the log's last line "Clarinets a2:
  loading"; MuseScore still in Task Manager). Its 19 patches: longs at full level after 100-1000 ms (Flautando and
  Sul Tasto slowest), shorts 35-155 ms, releases 0.6-2.9 s; but a short's "length" to 40 dB was the room's ring (Spiccato
  2 s) and the All techniques' Long (`techniques="long legato"`) was measured as a legato (retriggers, the same at
  every velocity). Now (`TIMING_VERSION` 2): `bodyMs` / `shortNoteBodyMs` (to 20 dB under the peak: the note) beside
  the 40 dB lengths; sustains = within 20 dB of its peak just before the release; legato only on an articulation whose
  first technique is "legato" (the Performance patches). The run is supervised like the extract (`superviseExtract(root,
  "background timing check")`: rounds, `background timing check current.txt / round n.txt / crash.txt`, a hang after 15
  minutes without a log line tried once more, then left out; a crash twice), and a start leaves out the patches an
  earlier run of this version timed (`timedBefore`), so the owner's 19 are timed again once. Test switches
  `MS_EXTRACT_TEST_CRASH` / `MS_EXTRACT_TEST_HANG=<patch>` now in `check()` too. Tried here with the test synth: a patch
  hanging every time (`MS_EXTRACT_HANG_MINUTES=1`) stopped twice and left out, the rest done, a second start timing only
  it.
  **The owner's second run (build 254, 2026-09-30 07:29-07:47, 17 minutes, no hang)**: 96 patches timed (the other 63 are
  kits, percussion and patches no notation plays), kept as `tools/soundlibraries/sso_articulation_timing.json`
  (`timing_from_check.py`). Kontakt offline renders a patch's notes in about a second; the first run's minutes were the
  legato analysis on patches without legato. Repeatable: Violins 1's numbers are the first run's to the 5 ms. Found:
  held notes at full level after 175 ms (median; 20-1540: Violas 305, Flautando / Sul Tasto up to 1 s), brass and
  woodwinds 60-120 ms; releases 855 ms (median); shorts' bodies (20 dB) 265-2670 ms, median 1 s (the hall's ring is in
  the recordings). **Legato (the 42 Performance patches): the second note's pitch arrives 70-430 ms after its note-on
  (median 185), mostly in one step (a recorded slur, not a glide), with a 4-22 dB dip; velocity 20 / 64 / 110 makes
  little or no difference in most patches** (Basses, Celli, Solo Violin 2, Flute Solo the exceptions). So a slurred
  note sounds its pitch that late: playing slurred notes earlier by about that much (per patch) would put them in time;
  not done. MuseScore crashed as it closed (c000000d, after the zip; the supervisor logs it and goes on).
  **Every sound** (the owner, 2026-09-30, "yes", after the list of what dynamics and timing hadn't covered; no other
  branch had it: main's dynamics check, with attack salience, still skips kits and keyswitched patches):
  `Measure what's left of SSO in background.bat` (was "Measure every SSO sound …" until 2026-09-30: each step leaves out
  the patches earlier runs did, so the same .bat measures only what is left) runs `--check-dynamics --all-sounds
  --extract-patches all`, then
  `--check-timing --all-sounds …` (`allSoundsMode`; `runHeadless(…, everything)`, `_everything`). Every patch of the
  library (the 541 `<Patch>` too), every articulation (also those no notation plays), each drum hit with a key on its
  own key (no switch, no other key tried: `soundsToMeasure`, `drumSettings`; results `drum`, `key`, value -1), a
  keyswitched patch switched by its key (`Settings::switchIsKey`: the key 50 ms before the note; tuned percussion, Harp
  glissandi), a patch with neither its one sound (value -1, no switch, "(its sound)"). The calibration still gets only
  what a notation plays; the rest is in results.json only (`"everything": true`). Both runs supervised
  (`supervisedCheck()`: the dynamics check too with `--all-sounds`) and resumable (`timedBefore(timing, everything)`: an
  error counts as done). A second run in the same minute gets its own folder (" (2)"; the two runs had shared one).
  Readers: `timing_from_check.py` (now with `key`) and `dynamics_from_check.py` → `sso_sound_dynamics.json`. Tried
  here with the test synth (a patch with an unplayed articulation, a keyswitched one, a drum patch with two hits and one
  off, a one-sound `<Patch>`): every sound measured, drums at their keys, 4 curves into the calibration (the notated
  ones), a second start leaving all out.
  **The owner's dynamics run (build 261, 2026-09-30 10:42-11:34, 52 minutes, no crash or hang)**: all 700 patches, 1804
  sounds (782 articulations, 504 drum hits: every keyed `<Drum>` of the map's 53 drum patches, 518 one-sound patches), kept
  as `tools/soundlibraries/sso_sound_dynamics.json` (`dynamics_from_check.py`). Driven by velocity 887, the controller 717,
  both 120, neither 75 (swells, falls, rips, FX, rolls: recorded at one dynamic). Silent at every pitch: Violins 2 Long
  Sul G, the Core patches' Long Sul G / C, Tenor Trombones a2 Fx Glissandi; Violins 1 Long Sul G and Celli Long Sul C
  "sound" only at CC 32 (-66 / -78 dB, then -100: the previous note's tail), so silent too. Some velocity curves aren't
  monotonic (Alto Flute Marcato 48 under 32: velocity layers and round robins). 492 curves went into the working
  `dynamics.json` (the notated ones). Timing, the second half, was stopped by the owner after 87 patches (it looked stuck:
  a Performance patch takes 12-14 s); `results.json` is rewritten after each patch, so a restart goes on from there.
  **Again with the merged build (run 267, 2026-09-30 15:27-16:05, 37 minutes)**: the Documents folder had been emptied,
  so every patch was measured again; the same 1804 sounds, every curve point the same to 0.0 dB (Kontakt offline, each
  patch freshly loaded, is exactly repeatable), now with each note's attack salience and rise (`attack`, `riseMs`, kept
  in `sso_sound_dynamics.json`). A resume needs the earlier runs' folders in Documents/MuseScore Sound Library Check.
  **Timing of every sound (run 267 build, 2026-09-30 16:05-16:41, 36 minutes, no crash or hang)**: all 700 patches,
  1804 sounds, kept as `sso_articulation_timing.json` (was 96 patches). The 96 timed before came out the same (median
  0 ms, 90 % 0 ms, at most 170 ms). Silent: the same 7 as in the dynamics. Held notes at full level after 220 ms
  (median; strings 150-300, woodwinds and brass 70-145), releases 890 ms, shorts' bodies 850 ms (75-4250), drum hits full
  after 25 ms, bodies 740 ms. Legato measured on the 42 Performance patches (84 transitions per velocity, as before);
  not on Horn Solo / Horns a2 - Legato and Oboe Principal - Total Performance (no map articulation named legato: their
  one sound was timed, not a slur).
  **The rest of every sound** (the owner, 2026-10-01: "measure everything left in the VM, I'm sick of doing everything
  manually"; HANDOFF's "Not measured" items 1-5): `--check-rest [--rest-parts range,repeats,controls,legato]`
  (`checkRestMode`, supervised and resumable like the timing check: `restBefore`, `REST_VERSION`; log `background rest
  check.log`; the third step of `Measure what's left of SSO in background.bat`). `ArticulationCheckDialog::measureRest`,
  `ArticulationCheck::rest`, per sound of every patch (`soundsToMeasure(…, true)`): **range**: every semitone from the
  test pitch down and up until 4 in a row are silent (a note "sounds" 10 dB over what was left of the last one; a
  keyswitched patch never plays its keys or under the highest; a drum hit: its key only), pp / mf / ff (velocity = CC1 =
  32 / 80 / 112; mf held `MF_SECONDS` 1.5 s with its tail, pp and ff 1 s): loudest 50 ms, perceived loudness, attack
  salience and rise, start / full / peak, and at mf body, sustains, release; **repeats**: the test pitch at mf 8 times
  (round robins); **controls**: every control the map names anywhere (a `<Patch>` lists none) that the loaded patch has
  (`parameterId`), at 0 / 0.25 / 0.5 / 0.75 / 1, then back at its own value (read first); **legato** (a sound whose
  first technique is legato, or with no techniques in a patch named "Legato" / "Total Performance": Horn Solo /
  Horns a2 - Legato, Oboe Principal): slurs at velocity 1, 16 … 127 (9) × -12, -7, -5 … +5, +7, +12 (14), as timing's
  (`legatoPair`, now shared). results.json `rest` (per sound: `range`, `repeats`, `controls` [control, value, note],
  `legato`; a note is an array in `restFields` order), `controls` (id, title, its own value). A note's perceived
  loudness and attack are worked out on other threads while Kontakt plays the next ones. `PluginExtract::centsShift`
  takes the reference's `pitchSpectrum` computed once (it was recomputed for every 10 ms frame: ~86 % of a slur's
  analysis). Test `restCheck`. Tried here headless with the test synth (a CC patch with a slow, a short and a silent
  articulation and a Tone control, a legato extra, two drum hits, a one-sound `<Patch>`): all measured in 6 minutes,
  the silent one reported, the zip as the timing check's. **Tests here run in the build's mount namespace**:
  `TESTSYNTH` is the build directory's absolute path, so a copied build directory (`../ninja-cray.sh`) loads the
  other worktree's test synth when run outside it (`../run-cray.sh`): timingCheck "failed" that way.
  **Onset, shorts' lengths, legato after short notes** (2026-10-01, the measurements the legato-timing fixes asked for:
  slow attacks shifted by when they are heard, shorts chosen by how long they really sound, legato speed after short
  notes): three more rest parts, only when named (`--rest-parts onset,shorts,legatolengths`; `RestSettings::onset`,
  `shorts`, `legatoLengths`). **onset**: the range's walk again with pp / mf / ff each held `ONSET_SECONDS` 1.5 s
  (results `onset`); every note (the range's too) now has `perceivedPeakMs`, `onset20Ms` … `onset10Ms` (the first
  time the short-term perceived loudness, `perceivedEnvelope`: perceivedLoudnessDb's 22 / 50 ms smoothed loudness
  every 5 ms, its window's centre, is within 20 / 15 / 12 / 10 dB of its peak in the first 1.5 s) and
  `energyOnset20Ms` … (the same on the 5 ms power windows), appended to `restFields`. **shorts**: mf held 0.05 / 0.1 /
  0.25 / 0.5 / 1 / 2 s at the test pitch and an octave (else a fifth) under and over it, each with its tail: the
  last time within 6 / 10 / 15 / 20 dB of its peak, perceived and power (results `shorts`). **legatolengths**: slurs
  at velocity 64, +2 +5 +7 +12 -5 -12, the first note held 0.1 / 0.2 / 0.3 / 0.5 / 1 s (its pitch reference from a
  separate 1.2 s note; results `legatoLengths`, each with `firstMs`). Reader `tools/soundlibraries/onset_from_check.py`
  → `sso_sound_onset.json`, `sso_short_lengths.json`, `sso_legato_lengths.json`; `rest_from_check.py` leaves these
  runs out. Test `restCheck` (the test synth's 14: power within 20 / 10 dB at 20 / 63 ms of its 200 ms linear attack,
  perceived -10 dB ~100 ms; 1 held: sounds as long as held; 24's glide the same after every first-note length).
  **legatopitches** (the owner: the grid from several pitches, octaves usable): the legato sound's range by probes
  every 3 semitones, slurs from 10 / 30 / 50 / 70 / 90 % of it, -12 … +12 (12 intervals) at mf, timed by harmonics
  (`legatoHarmonic`; the old `centsShift` search took octaves for each other: the grid's ±12 arrivals were up to
  ~700 ms): leave / mid / arrive = 10 / 50 / 90 % of the way between the two pitches' harmonic levels; results
  `legatoPitches`, `legatoRange` → `sso_legato_grid_pitches.json`.
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
  (undoable).
  **Live (2026-09-29, branch `live-controls`; the owner: "make what I do in the mixer or sound library
  controller reflect in the live playback")**: the Controllers window (`ControllersWindow` in
  soundlibraryhost.cpp) is not modal and no longer stops playback; one per part (raised when open).
  Each move / tick is heard at once: plug-in parameters on every loaded slot of the part at once
  (`SoundLibraryHost::applyPartControllers` -> `LibraryControllers::applyPart`, audio/vst3/librarycontrollers.*:
  patch, extras, copies for other tunings; unticked: `Slot::patchValues` puts the patch's own back); MIDI
  controllers by `Seq::libraryControllersChanged` -> `PartControllers::liveChanges` (the main patch's CCs on
  every route of the part, as the renderer sends them; not sent where a staff text is in force at the play
  position; left out where an automation lane plays the controller) sent through the sequencer (the audio
  thread, `Seq::putEvent`, hosted or MIDI out). The events rendered ahead before the change (~10 measures,
  a CC at each chunk's start) would put the old value back: `PartControllers::LiveOverrides` in the audio
  thread (SeqMsgId::LIBRARY_CC_LIVE) plays a route's CC whose value is one the part had before as the live
  value (-1: dropped), until the score is rendered again (`collectEvents` sets `_libLiveClear`). The metaTag
  follows 200 ms after a change without undo (`Seq::waitForRendering` first: the background renderer reads
  metaTags) and emits `playlistChanged`, so a restart renders what is heard; OK turns it into one undoable
  `ChangeMetaTags` from the values at open; Cancel / closing puts them back live. `Vst3Plugin::setParameter`
  is now safe from the GUI thread while the audio thread plays: the processor's change goes through the
  component handler's `edits` (mutex, taken at the next `process()`), not straight into `inChanges` (the
  audio thread's; the old path raced at sync too). Limits: a staff text's value equal to an old part value
  is corrected too until playback restarts; a CC with no default, unticked, keeps its last value in the
  plug-in (MuseScore never knew the patch's own). Tests `liveControllers`, `liveParameters` (all slots,
  heard on sounding notes, Cancel, untick, a lane, 1000 settings while another thread plays),
  `liveMidiControllers`. Not tried with Kontakt. From an extract: `tools/soundlibraries/controllers_from_extract.py <folder>` prints
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
    tail, seconds, or the note's articulation's measured release if longer: `<Articulation release>` ms;
    retuning a lane while a release rings moved its pitch; `legato-timing`. SSO's, since 2026-10-01, the longest
    over the articulation's range from the rest check's per-semitone releases, `sso_sound_range.json`: they differ by
    pitch far more than by articulation, pairs of neighbouring semitones ringing twice as long, e.g. Violins 1 -
    Performance 855 ms at the test pitch, 2180 at D4; 287 articulations, median 1120 ms, 77 over the 1.5 s tail, up to
    3.5 s on strings' flautando and 4.7-6.1 s on timpani rolls and tubular bells; was the test pitch's only, 241
    articulations, median 855, 12 over 1.5 s), retuned, else a new lane; past maxLanes (memory) the lane quiet longest is retuned.
    Tied notes follow their first note, grace notes their chord. `routes()` gives each patch one route
    per lane (`Route::lane`), so each lane is an instance with the same setup; the renderer
    (`libLanes`, `finishLibraryEvents`) sends a note's events to its lane and the part's switches and
    controllers to all lanes. `Vst3Synth::setVarispeed` (from the map, in sync and export): a note-on
    sets its slot's speed from the note's tuning, at once when the slot is silent, else gliding 30 ms (80 ms until 2026-09-28: the owner's
    slurred 16th E quarter-sharp after a D quarter-flat, 110 bpm, glided a semitone over most of its 136 ms);
    the note goes to the plug-in with no tuning. 12-tone equal scores need no lane (every tuning 0); a
    temperament (meantone, JI) can need several per part, hence maxLanes. The Sound Library dialog lists
    lanes as "~ <part> (other tuning n)".
  - **Pitch bend instead of varispeed where the patch bends** (the owner, 2026-09-30; branch `legato-timing`):
    varispeed also plays the plug-in's own time faster (3 % for a quarter tone: its script's envelopes, legato
    timing, effects); SSO's bend is Kontakt's per-voice resampling of the sample, so the sample's recorded vibrato
    moves 3 % either way (measured on the VM: Solo Violin Performance held D5 5.58 Hz, D5+ 5.76 Hz with varispeed
    and 5.76 Hz with bend, E5- 5.43 both). Pitch is as exact both ways (held D5+ / E5-: +0.5 / -0.1 cents
    varispeed, -0.5 / +0.2 bend; a slurred E5- +3.5 / +2.5), which also confirms the linear bend at ±50 cents on a
    Performance patch within 3 cents. The real gain: over MIDI out (a DAW) varispeed doesn't exist, the bends do. The owner's extracts (`sso_patch_measurements.json` `pitchBend`: cents at bend 0 / 16383; the cents at
    4096 … 12288 on a straight line) give `<Instrument bend>` (cents at full deflection) to a patch that bends
    cleanly: both ways ≥ 50 cents and within 3 % of each other: the 43 Performance patches ±99-105, Solo Cello and
    the tuned percussion ±195 (not the kits' patches: a kit plays no tunings; not the All techniques patches: they
    don't bend). Memory: the same (a bend moves the whole instance; the lanes stay as they are). Renderer
    (`libraryPitchBends`, the end of `finishLibraryEvents`; `libBend` per channel and patch): each note-on on a
    bending patch's lane gets its tuning's bend (`SoundLib::bendValue`, 14 bit, linear, centre 8192) right before
    it (absolute: playback may start anywhere; only in a part with microtones) and plays with tuning 0, so `Vst3Synth` engages no varispeed; a
    legato transition (`libGlideFrom`: the note before on the same lane) glides from that note's bend in 3 ms steps
    over 30 ms (`LEGATO_GLIDE`), steps cut at the lane's next note-on; a tuning beyond the range keeps varispeed for
    all of it, the bend at the centre. The bends are ME_PITCHBEND events on the lane's route: hosted
    (`Vst3Plugin::midi` → kPitchBend) and over MIDI out alike. Live clips carry them (branch `live-set-export`,
    2026-09-30): two carrier keys, 115 the upper and 114 the lower 7 bits, each written when it changes, turned into
    one pitch bend by the MuseScore Link device; varispeed (patches without `bend=`) can't reach Live: those stay
    12-TET there (LIVE.md › Live against MuseScore). Test `tuningBend` (bend 200 on the test synth: bends, glides, a narrower range
    falls back to varispeed, ±50 heard within 4 cents, no slot's varispeed engaged).
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
- Live integration (branch `live-integration`, `LIVE.md`): automation lanes may carry a `source`;
  `"source": "live"` lanes come from a Live Set (`libmscore/liveset.*`) and are **read-only**
  (`Lane::readOnly`, only `Automation::replaceSource` changes them; a lane editor must not edit
  them). Through MIDI output they send nothing (Live plays them); with the hosted plug-in they play.
  MIDI sync out: `libmscore/midisync.h`, `Seq::process` / `setPos`, preference
  `io/portMidi/syncOutputDevice`.
  **Live plays the score** (2026-09-29; LIVE.md › Live plays the score): each library route as a playable
  arrangement clip in Live, rewritten after every edit, Live the clock and MuseScore following it.
  `libmscore/liveclips.*` (the clips from the rendering: controllers as carrier notes on keys 116-127, the pitch
  bend on 115 / 114 (14 bit), the value as the velocity (`carrierVelocity`: 127 exact, 1 as 0; UACC value + 1),
  real times at the score's first tempo, bar locators, the OSC protocol), `mscore/liveclips.*` (`LiveClipsLink`:
  debounced rendering, per-route hashes, confirmations, Live's transport followed; `Seq::setLiveClips`: no
  library events, no MIDI clock), `tools/live/` (the MuseScore Link Max for Live device: `MuseScoreLink.js`,
  `make_device.py` writes the patcher and `MuseScore Link.amxd`; Node tests against a stand-in Live, and
  `fake_live_server.js` to run a real MuseScore against it: GUI under Xvfb and a PulseAudio null sink; with
  `QT_QPA_PLATFORM=offscreen` the link never bound its port, cause not looked into). Tests `tst_liveintegration` clips*. Not tried in real Live.
  **Editing Live clips in MuseScore** (branch `live-clip-edit`, 2026-09-29; LIVE.md › Editing Live clips in
  MuseScore; the owner: "just edit any midi clip in MuseScore", in Continuous View): the device's *Edit in
  MuseScore* button sends the Detail View's clip (`get_all_notes_extended`, every field, note ids); MuseScore
  opens it as a new tab through the MIDI import (`mscore/liveclipmodel.*`: `importClip`, layout mode LINE set
  before the import, instrument from the track name else piano, drums by a Drum Rack; `match`/`diff`: notation
  notes by signature against a baseline of Live notes, content not object identity; only edited fields sent),
  `mscore/liveclipedit.*` (`LiveClipEditor`: sessions, debounced writes, resend, conflict → *Reload from Live*,
  status bar widget; a clip score is marked only in memory and plays with MuseScore 4 sounds). The UDP socket
  is `LiveClipsLink`'s, bound while "Live plays the score" or the clip-edit setting (`liveIntegration/editClips`,
  default on) is on; `/live/clip/*` goes to the editor; protocol 2. The importer has
  `FileData::forcedInstrument`. Tests `tst_liveintegration` clipEdit*, `tools/live/test/test_clipedit.js`;
  `fake_live_server.js --edit-clip <track> --edit-at <s> [--live-change-at <s>]` for a GUI run. Not tried in
  real Live.
  **Create Live Set** (branch `live-set-export`, 2026-09-30; LIVE.md › Create Live Set; the owner: the manual
  track setup is "so many manual steps"): *Mixer › Advanced Options… › Ableton Live › Create Live Set…* / *Add
  missing tracks…* write a Live 12 set: a MIDI track per route named as the device finds it, MIDI From = the route,
  the MuseScore Link device (referenced in Live's User Library, else next to the exe) before Kontakt holding
  `SoundLibraryHost::setupState` (resaved Kontakt state, 512 voices), or, where the part sets plug-in Controllers,
  `SoundLibraryHost::stateWithControllers` (the setup loaded and settled as playback loads it, the part's values
  applied by `applyParameters`, then getState; the same parameters listed in Live's panel, PluginDevice
  ParameterList); each track's Live mixer from `SoundLib::partMix` (`LiveSetWriter::mixGain` (v/100)², `mixPan`,
  a muted part's Track Activator off). `libmscore/livesetwriter.*` writes **every
  element Live 12.2 writes, in its order, with Live's defaults** (learned from the owner's sets, written by hand; the
  owner's files never go in the repository) and checks pointee ids / NextPointeeId / clip slots per scene
  (`validate`) before writing; `mscore/livesetexport.*` gathers routes, device and states. To change the format,
  compare with a set Live saved: `tools/live/test/compare_als_skeleton.py <generated.als> <live.als>` (the test
  writes one with `MS_LIVESET_OUT=<file> ./tst_liveintegration liveSetWrite`). `Vst3Plugin::classInfo` (class
  name and FUID words without an instance), `splitState` / `joinState` (a setup's parts). Tests `tst_liveintegration`
  liveSetWrite / liveSetMissing, `tst_soundlibrary` liveSetTestSynth, `tst_liveequivalence`.
  **Live against MuseScore** (2026-09-30, the rule above; LIVE.md › Live against MuseScore): `mscore/liveequivalence.*`
  renders the score as MuseScore does and as Live would (the set's states, the clips turned into MIDI as the
  device does, Live's mixer) and compares them; `MuseScore3Evo.exe --live-equivalence <folder> <score>`,
  `--create-live-set <out.als> <score>`, `--live-set-readback <file.als>` (musescore.cpp `liveSetInBackground`).
  Test `tst_liveequivalence` (links mscoreapp like tst_palette: tst_soundlibrary can't, both carry stringutils' moc;
  uses tst_soundlibrary's test synth). Unconfirmed: that Live opens it; MIDI From's
  one-port target form; `OriginalCrc` (CRC-16/UMTS of the first 16 KiB, from one example).
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

### Building (Linux container)

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

### Playback verification (`VERIFY.md`)

The owner, 2026-09-28: every playback bug needed an audio export by hand on Windows, sent over and
analysed here; "figure out a way to automatically verify that the plugin plays back properly".
`MuseScore3Evo.exe --verify-playback <score | folder | default> [more …] [--verify-library <lib>]
[--verify-out <folder>] [--verify-audio <file>] [--verify-wav] [--verify-shareable]`
(`verifyInBackground` in musescore.cpp; `mscore/playbackverify.*`; the analysis in
`audio/vst3/playbackverify.*`, whose header lists every check and threshold). A background process
like the extract: no window, below-normal priority, its own setups copy (`background verify
setups`; dynamics.json always the working one's), lock and log (`background playback verify.log`),
DialogWatch; the report folder opens when done unless `--verify-out`. Per score: the events as an
export renders them (the working MuseScore's synthesizer.xml) and again with the library off (the
built-in synth: the expectation); the library's patches loaded once (`SoundLibraryExport::
loadInstances`, shared with the command-line export) and rendered offline as `saveAudio` does, mixed
and each library part alone. Findings: `missing-attack` (a strike), `missing-note` (one note,
chords' octaves included), `cut-short` (held articulations), `silence`, `clipping`, `drift`, each with
measure, beat, part, pitches, times and the events on its slot around it (pedal, switch, CC1/CC11,
parameters, same key released, an earlier same-key note still on, notes the slot started in the 2 s
before). Output `Playback verify <date>/` + zip: report.json, summary.txt, per part strikes.tsv and
notes.tsv, events.tsv, clips/ (2.5 s of the rendering and the built-in synth's per finding);
`--verify-shareable` keeps only the two reports (no audio, no note lists: the owner's music in a
public place). `Verify SSO playback in background.bat` (bin) runs it on `share/verifyplayback`
(two scores from `tools/playbackverify/make_verify_scores.py`) or on scores dropped on it.
**Reading a report**: `tools/playbackverify/read_verify_report.py <zip|folder> [--near <s>]`.
**An export the owner sends**: `--verify-audio <wav>` checks it against the score's events here
(this build's events: say so when the export came from an older one).
Calibrated on the owner's piano score (files kept outside the repository): the pedal-change export
(prog44) 88 missing attacks, 85 at pedal changes (the hand analysis' 28 + 1 among them; comparing with
the fixed export, ~97 of the 220 pedal-change chords were lost, not 28); the next export (new44) 8,
5 of them where 3a342ce later moved the pedal; the latest (v3) no missing attack, 38 missing notes,
16 in bars 59-62 where the owner hears staccato notes missing, clustered where the slot started
20-39 notes in the 2 s before (a voice limit?). Test without Kontakt: `MS_VERIFY_FAULT`
(`pedal-drop:<ms>`, `drop:<n>`, `truncate:<n>:<ms>`; `MS_VERIFY_FAULT_LOG`) in `Vst3Synth`;
`tools/playbackverify/try_with_testsynth.sh <build> <install> [work]` runs it headless with the test
synth clean and with each fault, `check_faults.py` compares (clean: 0 findings; every detectable fault
found; nothing else). The test synth now releases a note in 10 ms (an abrupt stop's click looked like
an attack). Found with it: MS4 lengths overlap repeated notes of the same key (164 strikes in the
owner's piano score), and a one-voice-per-key plug-in then ends the new note with the old note-off;
whether Kontakt does is not known. Optional runner: `.github/workflows/verify_playback_owner_pc.yml`
(workflow_dispatch only, environment `owner-pc` with the owner as required reviewer, no checkout,
label `sso`; not set up; VERIFY.md has the risk and the steps). The Ableton route was assessed
(VERIFY.md): it hosts Kontakt outside MuseScore's hosting and export paths, where the bugs were, and
Live can't run headless.
**First run with Kontakt (the owner, 2026-09-29, build 1c3d699, 30 s)**: the test scores gave 10 findings;
9 `missing-note` on the Solo Violin / Solo Cello Performance legato lines were false (the owner heard A4 and
B4 in the m3 clip): that patch sits 15-20 dB under the solo patch against the built-in synth and its notes
build up over ~200 ms. Now the note levels' baseline is per patch (slot) and legato or not, and a note
the library plays with a "Legato" articulation is judged on its held level (40 / 70 % of it), no attack or
rise needed (test `playbackVerifyLegato`; on the owner's clips those 9 come out -5 … +5 dB, the limit is
-20). Piano m13 beat 1 (C3 E4 E5, the same keys released 6 ms before, pedal up +40 ms): a missing
attack not yet confirmed by ear.

### Tests and known state

- `mtest/libmscore/soundlibrary` (`tst_soundlibrary`): text techniques, `choose`, the
  Spitfire map's instrument matching, and a rendered MusicXML score (the switch per note,
  routing, sampled ornaments), the playback verification's analysis (`playbackVerify`,
  `playbackVerifyDrift`), the Controllers window's live changes (`liveControllers`, `liveParameters`,
  `liveMidiControllers`), phrase marks (`renderPhraseMark`). All pass (50 counting initTestCase and cleanup, 2026-10-01; 3 skipped without the owner's files;
  `vst3Settle`, `kontaktMaxVoices` 2026-09-29; `liveSetTestSynth` 2026-09-30).
- `mtest/libmscore/liveequivalence` (`tst_liveequivalence`): Create Live Set with the part's Controllers and the
  Mixer, the clips' pitch bend and early legato notes, and Live against MuseScore on the test synth. All 7 pass
  (counting initTestCase and cleanup; 2026-09-30).
- `mtest/libmscore/tuning` (`tst_tuning`): the built-in tuning (see "Tuning"). All 13 pass.
- `mtest/libmscore/midi` (`tst_midi`): 68 of 73 failed until 2026-10-04 (references older than
  the MS4 note model, rendered with the default method 3). Since then the 3.x tests render with
  3.6's method and pass 3.6's references except the deliberate differences (bends, fermata
  stretch), and `eventsMs4` covers MS4 with the fork's own references; `ms4Parts` is walked in
  score order, so same-tick events no longer follow pointer order. The export's bend range
  RPN said 12 semitones while bends were written for 24 (exported bends played half size
  elsewhere): fixed then.
- There is no way to hear Kontakt, SSO or Windows here. Anything about them is either untested
  or tested with a stand-in; say which when you report.

### Plugin hosting (VST3)

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
  A Kickstart percussion patch whose values switch techniques, drums or mics on gets their samples loaded too
  (`KontaktSetup::unpurgeSwitchedOn`, see Kits › Techniques switched on in a setup).
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
- **A patch's own script, and its voice limit** (branch `piano-v37-fixes`, 2026-09-29; the owner's Piano v3.7:
  notes missing "randomly"). Found on the Windows VM (Kontakt 8 + SSO, see VERIFY.md › With the real library):
  - Kontakt's engine runs only while the plug-in is processed, and `MasterSynthesizer` processes `Vst3Synth`
    only from its first event on (`MasterSynthesizer::play` sets it active; `Seq::setScoreView` resets it).
    Until then SSO's patches show "INSTRUMENT NOT INITIALISED!" in Kontakt's window: their KSP script has not
    run. When it runs (the first render or play) it puts the patch's own control values back, so a parameter
    set before is lost: the part's Controllers… (mic 1-4) set at score open were heard only from the second
    play or export on. Measured with a test host: a parameter set after 12 ms of the plug-in's audio is
    lost, after 50 ms it holds. Now `Vst3Plugin::settle()` (1 s of silence processed and discarded, since
    the last setState) runs after each setup is loaded (`loadSetup`, `finishLoad`), before the instance
    goes into its slot and before the controllers are set; `secondsSinceState()` says how far it ran. Test
    synth: `MSTESTSYNTH_INIT_MS` does the same as SSO's script (tst `vst3Settle`).
  - With the mics on, a note uses several voices, and the Grand Piano's limit (the instrument header's
    Max, 256) dropped 31-40 notes of the piece in busy bars (20-39 notes in 2 s): the owner's missing
    notes (export e39fe815) and the "after playing, exports drop notes" of 2026-09-29 (it was the second
    render, the first with the mics). Every Kontakt patch MuseScore sets up now gets 512
    (`SoundLibraryHost::KONTAKT_MAX_VOICES`, applied in `setupState` as it loads: `KontaktSetup::
    withMaxVoices`; the owner approved "just raise all voices to 512"; `MS_KONTAKT_MAX_VOICES` another
    value, 0 the patch's own). Kontakt keeps it in the program's VOICE_GROUPS (0x32): the entry
    "<instrument>", 3 bytes, u32 Max voices, u32 fade-out ms (found by changing Max in Kontakt's window and
    diffing the state: that field only). At 1024 the render differs from 512's by -82 dB rms (quiet ends of
    a few stolen voices).
- **Load times** (the owner, 2026-09-28: "optimize load times of the SSO plugin"; branch `sso-load-times`).
  Where the time goes: (a) Kontakt's setState, 0.06-0.8 s a patch from its own state, 2-43 s from a setup
  made from the `.nki` (its first load); (b) Kontakt's samples, loaded after setState returns (the extract:
  "until it sounds" 3-5 s; ~0.7 GB a patch at the 60 kB preload); (c) MuseScore's own: `syncSome` ran
  `SoundLib::routes` (the whole notation twice: extras, tuning lanes; 0.3-0.5 s on a 21-part, 300-measure
  score, tst `routesTiming`) before every instance it loaded at score open and at every play, and paused
  100 ms between loads. Done: `routesFor` (soundlibraryhost.cpp) keeps the routes until the undo stack's
  state, the library, `routesGeneration` or the playback-mode / copies metaTags change; the gap is 0
  (`PRELOAD_GAP_MS`); `Vst3Plugin::looseTitle` by hand (the title index: 8.3 → 0.9 ms for 4145 titles).
  Test synth, GUI under Xvfb, 33 instances with setState at 100 ms: main had 30 loaded after 30 s, this
  build 33 in 5.0 s. **Kontakt's own states shared** (`importResaved`): a setup not resaved here is
  taken from another setups folder (the working one, the background runs' copies) where the same `.nki`
  and values were resaved, if its sample list is version 3 (`KontaktSetup::sampleListVersion`; a made
  one, version 2, is refused); a resaved setup no longer depends on Kontakt's empty state (a Kontakt
  update made every patch slow again). **`load times.log`** now has each new instance by step
  (`Vst3Plugin::times`: module, create, buses, activate; "one object" when the component is its own
  controller), each load's setup size and read time and setState by step (component, controller, MIDI
  mapping), a summary per batch ("At score open", "At play") and when the process's memory settled after
  it (the real wait). **Worker threads** (`io/soundLibraryLoadThreads`, Advanced preferences, default 0;
  `MS_SOUNDLIBRARY_LOAD_THREADS`): setState of up to n instances on worker threads (`Pending`,
  `beginLoad` / `finishLoad` / `harvest`), the window free meanwhile; off until the owner's measurement
  shows Kontakt takes it (VST 3 wants setState on the UI thread). **Measurement**: `Measure SSO load
  times in background.bat` (bin) → `MuseScore --measure-load-times <library> [scores…]
  [--extract-patches <file>] [--measure-probe <patch>] [--measure-threads 2,4] [--measure-no-probe]`
  (`mscore/soundlibraryloadtimes.*`, `extractInBackground`; normal priority): report `<library> load
  times <date>.txt` in Documents/MuseScore Sound Library Check, phases 1 each patch alone (steps, until it
  sounds, memory and when it settles, freeing), 2 the score as at score open, 3 on 2 and 4 threads, 4 an
  instance reused, 5 the probe (Mic 1-5 at 0; each saved script value of 0s and 1s turned over in
  Kontakt's own state, `KontaktSetup::withScriptValues`: memory, value kept, articulations silent; the
  best one element by element). The test synth stands in with `MSTESTSYNTH_SETSTATE_MS`,
  `MSTESTSYNTH_STREAM_MS`, `MSTESTSYNTH_STREAM_MB` (tst `vst3LoadTimes`). Headless runs here need
  `HOME` isolated, `~/.vst3/mstestsynth.vst3` linked, `application/startup/firstStart=false` and the
  splash / start center off in `MuseScore3Evo.ini`, a score as `.mscz` (MusicXML asks about Edwin), no
  `session` file; GUI runs with sound: a PulseAudio null sink (`pulseaudio -n --load=module-null-sink
  --load=module-native-protocol-unix`, `XDG_RUNTIME_DIR` of its own). SSO itself: the owner's screenshot
  shows a switch under each technique (most likely it unloads that technique's samples): which saved
  script value holds them is what the probe looks for; if found, a setup per score with only the techniques
  it plays (a new `setup=` value per part) would cut memory and load time the most. Not tried with Kontakt.
- `mscore/vst3editor.*`: the plug-in's editor window (HWND, NSView or X11 plus IRunLoop).
- `Seq::putEvent`: in plugin mode, external events go to `Vst3Synth` with the slot as the
  channel.
- **The Mixer on library parts** (the owner, 2026-09-28: "the mixer panning tool doesn't work … make all
  buttons in the Mixer work with SSO"; branch `mixer-sso`). Before, a library part's volume, pan, reverb and
  chorus went as CC7 / CC10 / CC91 / CC93 to its *built-in* channel only, which plays nothing of it; mute
  and solo held back its new notes but left sounding ones on (their note-offs were held back too). Now:
  - hosted (Output::PLUGIN): `Vst3Synth::setMix(slot, volume, pan, muted)` applies them in the host to each
    slot's stereo output before the slots are summed (any plug-in, whatever its script does with CC7 / CC10;
    live and in audio export alike). Volume (v/100)² (FluidSynth's CC7 curve, 40 log10, relative to the
    default 100 = 0 dB: a part at volume 100 / pan 64 plays bit-identical to before; MusicXML imports centre
    at pan 63, 0.1 dB to the left, as for the built-in sounds); pan constant power with
    0 dB in the middle (a balance: hard left = left +3 dB, right silent; as Live pans a stereo track); mute /
    solo = gain 0. Gains glide (one-pole, 5 ms) so moves and mutes don't click. The plug-ins get no CC7 /
    CC10 (Kontakt follows them by default: applied twice). `SoundLib::partMix(part, withSolo)`: the part's
    first instrument's first channel's values (the renderer plays the part there; the Mixer's part row sets
    all its channels), muted when every channel of the part is muted (a single channel row muted still
    silences only its own notes, `NPlayEvent::isMuted`). `SoundLibraryHost::applyMixer` sets every slot of the
    part (patch, extras, copies for other tunings: `_slotParts` from the last sync) after each sync (score
    open, play), at once from `Seq::setController` / the Mixer's mute and solo, and every 50 ms (the idle
    timer: OSC, the old part editor, the "play part only" box; since `live-controls` it runs from the first
    sync, a score-open preload's too, not only after a complete one). Export: `SoundLibraryExport` sets
    `setExportMix` from the routes with mute but not solo, as MuseScore's export treats its own sounds.
  - MIDI out (Output::MIDI): `Seq::libraryMixerChanged` sends CC7 / CC10 / CC91 / CC93 on each of the part's
    routes (only changed values; all again at each play), so a DAW or Kontakt there can follow.
  - Mute / solo: `Seq::stopNotes(channel)` also sends sustain off and all notes off on the part's routes, and
    `Vst3Synth` ends every key still on at CC123 (note-offs), for a plug-in that maps no CC123.
  - Reverb / chorus, hosted: disabled with a tooltip. The library brings its own room (SSO's mic positions,
    *Controllers…*), and MuseScore's reverb here is a master insert on the built-in sounds, not a send: its
    Mixer knobs do nothing for built-in parts either (FluidSynth's own effects are off, as in MS4; unchanged).
  - Patch drop-down: a library part shows its library patch, disabled (the General MIDI patch comes back with
    "This part plays:" MuseScore 3 / 4); a kit keeps it (its sounds the library lacks play the GM kit). MIDI
    port / channel: disabled; over MIDI out they show the library's route. Details panel: `updateLibrary`.
  Audit (✓ worked before; → now):

  | Control | Built-in part | Library, hosted | Library, MIDI out |
  |---|---|---|---|
  | Volume | CC7 ✓ | nothing → slot gain, all its slots | nothing → CC7 on its routes |
  | Pan | CC10 ✓ | nothing → constant-power balance | nothing → CC10 |
  | Mute / solo | notes held, stopNotes ✓ | new notes held, sounding ones hung → slots silenced + notes off | same → CC64 0 + CC123 on routes |
  | Reverb / chorus | CC91/93, no effect (FluidSynth effects off) | nothing → disabled, tooltip | nothing → CC91 / CC93 |
  | Patch | program ✓ | GM list, no effect → library patch shown | same |
  | Port / channel | MuseScore's mapping ✓ | no effect → disabled | showed MuseScore's → the library's route |
  | Master volume, voice mutes, drumset, playback-mode row | ✓ | ✓ (mode row now refreshes the panel) | ✓ |
  | Audio export | mute ✓, solo not | muted parts played, volume/pan ignored → export mix | (no audio) |

  - Live while playing (checked 2026-09-29, branch `live-controls`): volume, pan, mute and solo reach the slots
    at once, playing or not, also slots still loading in the background (the mix is per slot) and right after a
    play starts. "This part plays:" and the "Playback, all parts" drop-down still stop playback (the part's
    notes move to other synthesizers: a new rendering and maybe patches to load); so do the Advanced Options.
  Tests: `tst_soundlibrary::partMix`, `mixerSlot` (test synth: defaults bit-identical, 50 → -12.04 dB, 127 →
  +4.15 dB, hard left right = 0 and left +3.01 dB, a quarter left keeps the power, mute silent with a glide,
  export values of their own, CC123 ends notes), `mixerScore` (a part's 4 patch instances panned hard left:
  right 0; two tuning copies at volume 50: -12.04 dB; mute: silent; the piano soloed: exactly the piano's
  sound, and an export ignores the solo). tst_soundlibrary 32 passed, 2 skipped (29 before). A built-in
  audio export (Dawn) is bit-identical to main's. Not tried in the GUI with a hosted plug-in.
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
  **The owner's first run (build b903d9a, 2026-09-28 08:06) crashed** 16 s into Violins 1, patch 1: an access
  violation in `Kontakt 8.vst3` (Windows' Application Error record); no extract written. It had been switched
  offline right after its setup and rendered flat out while it still loaded the patch. Now, as in Check
  articulations (offline with Kontakt since run 3), the patch first sounds in real time, then goes offline and
  must sound again; the log names each step (sounds / pitch bend / every controller) and the current one at
  least once a minute. Tried here with the test synth only.
  **The second run (build fe5d050, run 211, 08:54):** Violins 1 and 2 done (about 12 s a patch offline), then a crash
  on Violas' controllers. Its data showed a fault of that build: each controller was put back to the value Kontakt
  reported for its parameter, which is 0 for a CC it never received, so CC 7 (volume) at 0 silenced the patch and
  every later controller and parameter read "no effect". Gone: a background run searches each controller's own value
  by sound, as the dialog did before Quick (offline that is quick), and `controllers()` compares the patch at the end
  with the start (JSON `endDistanceDb`; summary "the patch after them against before", "!" when not put back).
  **A supervisor** (`superviseExtract`, musescore.cpp): `--extract-library` without `--extract-child` starts the
  extract as child processes one round after another. Each writes at every patch's start that patch and those after
  it to `background extract current.txt` (`ArticulationCheckDialog::setProgressFile`), removed at a normal end. A round
  that leaves it (a crash, Kontakt broken, or nothing in the log for 15 minutes: killed) has that patch left out, its
  folder zipped as it is, and the next round goes on with the rest (at most 100 rounds; the log names every patch left
  out). The children don't open a "stopped working" window (SetErrorMode). Tried here with the test synth and
  `MS_EXTRACT_TEST_CRASH=<patch>` (the child aborts on that patch): 3 patches, the 2nd left out, two zips; the hang
  timeout (`MS_EXTRACT_HANG_MINUTES`) not tried.
  **The third run (build a9c2251, 09:31), 2 hours in:** 11 access violations (Violas, Strings Ensemble, Flute Solo,
  Oboe Solo, Oboes a2, Clarinet Solo, Contrabass Clarinet, Horn Solo, Trumpet Solo, Trumpets a2, Timpani), each a few
  seconds into "every controller" offline, those patches left out; six low or high patches (Basses, Piccolo,
  Contrabassoon, Contrabass Trombone, Cimbassi a2, Contrabass Tuba) silent at their test pitch (this branch's map had
  no `pitch=` yet: merged from main, 273e186), 2 minutes waiting then 6 in real time each; the estimate started over
  in every round (180 → 3081 → 181 min). Now:
  - a silent patch tries the pitches around its test pitch (±12, 7, −5, ±24; 8 s each after 20 s), keeps the one that
    sounds (JSON `mapPitch` when it moved; log "silent at …, sounds at …");
  - each step is written before it is tried to `background extract step.txt` ("<patch>\t<step>": load, until it
    sounds, offline, describe, pitch bend, controllers: baseline, cc N, parameters: baseline, parameter <id>, switch
    N, controllers: end), and on Windows the fault's module and offset to `background extract crash.txt`
    (`SetUnhandledExceptionFilter` in the child); the supervisor logs both ("… crashed (exit code c0000005;
    exception C0000005 in Kontakt 8.vst3 +0x…) on Violas at cc 32");
  - a crash at a controller, parameter, switch value or pitch bend: the patch once more without that step
    (`background extract skip.txt`; `PluginExtract::Settings::step/skip`, JSON `skippedAfterCrash`), up to 3 tries;
    elsewhere once more as it was; then left out;
  - the time left from the run's start over every round (`MS_EXTRACT_RUN_START` from the supervisor, patches
    finished in `background extract finished.txt`).
  Test: `MS_EXTRACT_TEST_CRASH_STEP="<patch>\t<step>"` aborts the child at that step.
  **What the third run measured** (the owner stopped it at 11:27 after 108 patches; its 15 zips): 44 patches complete
  (every controller put back, pitch bend, parameters). 50 were left silent: CC 32, SSO's UACC, was tried on patches
  that take no switching (Performance, single techniques) and left them on "None", so everything after it measured
  silence; a patch's controller run now never touches the library's switch CC (`librarySwitchCC`). 14 were silent at
  their test note (fixed above). Found: **the Performance patches bend the pitch**, +97 to +105 cents at full bend
  (all 35 measured; the All techniques patches don't: 0-17 cents, round robins), the Kickstart percussion +195.
  A controller run now leaves out patches an earlier extract of the library measured completely
  (`measuredBefore`: an extract folder's JSON that sounded, has `endDistanceDb` within the patch's noise, parameters,
  and pitch bend when asked; `MS_EXTRACT_REDO=1` measures them again). Percussion with decaying round robins (Toys,
  Bongos, Wood, Xylophone, Marimba, Snare 1) show nearly every CC as "sound" and may never count as put back.
  **The fourth run (build d966da4, 13:09):** the 44 left out as meant; in 9 minutes 20 patches and 5 crashes, all in
  Kontakt 8.vst3 at +0x84AC98, +0x84DB9C or +0x8E5873, at cc 23 (Basses, Contrabass Trombone), parameter 8 (Horn Solo),
  parameter 2048 (Contrabass Trombone again, once cc 23 was left out) and parameter 5 (Timpani); each patch went through
  without its step. Violas, Strings Ensemble, Flute Solo, Oboe Solo and the others that crashed in the third run went
  through. So the crashes come and go rather than follow a step: now the first crash at a step is tried again as it
  was, and a step is left out only at a second crash there (4 tries); a patch measured with a step left out
  (`skippedAfterCrash` in its controllers, parameters or switches) is measured again by the next run.
  Its first 92 minutes (81 patches): 74 took about 30 s each; 7 percussion patches (Bongos, Congas, Snare 3,
  Timbales, Toys, Wood) 7.6 minutes each, silent at their map `pitch=` (the one-drum patches' samples sit on keys
  0-31; Spitfire's script lays the hits out higher), so 2 minutes of waiting and every controller in real time on
  silence. Now a patch with `<Drum key>` entries is tested on a hit's key (else its first key; `testPitch`), and a
  patch that played nothing at any pitch tried is described only (`controllers.notMeasured`).
  Kontakt's own crash notice (the owner, 15:22: "Kontakt 8 has encountered a major problem and has been terminated",
  a `.nicrash` in Documents/Native Instruments/Kontakt 8/Crashlogs) is a message box in the crashed process waiting for
  OK, so the child's own DialogWatch can't close it; the supervisor now looks every 2 s for a window of the child with
  that text (`childHasCrashNotice`, Windows only) and ends the child, a crash like any other.
  **The owner's `.nicrash` (15:22, build 46d8b0d) is a minidump**: the access violation is on the main thread, in
  Kontakt 8.vst3 +0x8e5944 reading address 0x2b8 (a field of a null object), with Kontakt frames (sqlite3.dll among
  them) over Qt's event dispatch (Qt5Core / Qt5Widgets) over MuseScore3Evo.exe: Kontakt handling one of its own window
  messages while `Pump::run` let Qt process events between blocks, on 'Brass - Bass Trombone Solo - Long Cuivre' at
  cc 26; not in `process()` nor in a parameter change. The background controller run had never opened Kontakt's
  window; every run with the window open (Check articulations, the dialog's extract, the pictures run) went without a
  crash. So the controller run now opens the window off the screen, not activated, a Tool window marked as the run's
  own for `DialogWatch` (`markOwnWindow`), as the pictures run does, and the controllers' window changes are measured
  too (which named control each moves, `controllersToControls`). A guess from one dump; untried with Kontakt. (The
  owner's WER folders are the working MuseScore's ntdll c000000d fail-fasts, builds 6aba63d9 / 6aba99ca, no dumps.)
  **The fifth run finished** (the owner, 2026-09-28 13:09-18:48, restarted twice on newer builds, d966da4 then
  7b2623f / 46d8b0d; not run 222's off-screen window, so whether the window helps is still unknown): 121 crashes, all in
  Kontakt 8.vst3 (+0x84DB9C 53, +0x84AC98 38, +0x8E5873 9, +0x8E5944 8, +0x85729A 6, +0x84ACC8 4, +0x8663BF 1), at cc 23
  (58), a parameter (43) or cc 22-26; one Kontakt crash notice (15:22, before the supervisor closed them); no hang. With
  the third run's data, **695 of the 700 patches measured**: 663 complete, 6 with cc 23 left out after two crashes there
  (Bass Trombone Solo / Bass Trombones a2 - Long, Harp - Slid CPU-friendly, Celli / Violins 1 - Trill (Minor 2nd),
  Clarinet Solo - Long Flutter), 26 not put back within their noise (decaying percussion round robins, the Fanfares,
  Multitongue, Flutter, Toys: sounds that differ note to note; the data is there). Missing: Cimbassi a2 - Long (crashed
  5 times at 3 steps: left out); Curated Woodwind Ensembles, Bass Trombone Solo - Fall, Field Drum and Cimbassi a2 - Long
  Alt were measured in two rounds a restart cut off, whose folders weren't zipped (so a later run counted them done).
  The derived numbers are kept as `tools/soundlibraries/sso_patch_measurements.json` (`measurements_from_extract.py
  <folders or zips>`: per patch status, test pitch, pitch bend down / up, each controller and parameter that changes
  the sound with its levels, brightness and balance; 0.8 MB). Found:
  - **Pitch bend, linear** (the cents at bends 4096 … 12288 on a straight line): every Performance patch (43) and Horn
    Solo / Horns a2 - Legato **±100 cents** (98-105); the Kickstart percussion (tuned, drums, ensembles), Solo Cello
    (All techniques and Long) and the solo strings' Long Harmonics ±195; the rest none (within their round robins, ±25).
    So the held and slurred notes, which play the Performance patches, could be tuned by pitch bend within a semitone
    (the owner's first choice, 2026-09-27) instead of varispeed; not done.
  - Controllers that change the sound: CC 7, 10, 11, 111 on every patch, 23 on 678, 1 and 103 on about 380, 22 and 24
    on about 315, 64 on 232, 25 on 196; parameters Expression 687, Articulation Controller 563, Mic 2 level 487, Mic
    Mix Distance 441, Dynamics 377, Mic 1 level 304, Mic 3 level 275 (a parameter set to 0 and 1 on one held note;
    "no effect" can mean the mic mix hides it). Which named control each CC moves needs the window (run 222 on).
  The last round's MuseScore crashed as it closed, after its last patch, and the supervisor logged "ended before its
  first patch; stopped": the progress file is now emptied at a normal end, not removed, and such a crash is logged as
  one on closing.
  **Links run: only what is still needed** (the owner, 2026-09-29: "make a version that only extracts data we still
  need"). `Link SSO controllers in background.bat` starts `--extract-library … --extract-plan "SSO controller links
  plan.txt" --extract-pitch-bend` (the plan installed next to it from `main/`, made by `tools/soundlibraries/links_plan.py`
  from `sso_patch_measurements.json` and `sso_patch_controls.json`; `ArticulationCheckDialog::setPlanFile` /
  `PlanEntry`; `--extract-plan` implies `--extract-controllers`, is the patch list unless `--extract-patches` is given,
  and is passed on to every round). A line `<patch>\tall`: everything, as before (the 11 patches left incomplete). A
  line `<patch>\tpitch=…\tcc=1:98,7:102…\tparams=Dynamics;…` (689 patches): the window open off the screen, only the
  controllers that changed the sound (about 12, not 122; `Settings::onlyControllers`), each put back at the value the
  earlier run found (`patchValues`, no search), only the patch's named parameters (`onlyParameters`), at the pitch that
  sounded; no pitch bend, no switches; the JSON says `"plan": "links"` and has `controllersToControls`. Offline, the
  window is drawn by Kontakt's own timers, so each picture first gets 250 ms of real time (`REAL_GRAB_MS`; the fast
  pump's pictures were of before the change). Patches a links run did (`linkedBefore`) or, for "all", measured
  completely are left out, so a stopped run can start again. `measurements_from_extract.py` adds each patch's `links`
  (cc -> named control) to the measurements. Tried here with the test synth (a links patch tried its 3 listed
  controllers, CC 1 put back at the plan's value, no pitch bend; an "all" patch as before; a restart left both out; a
  crash test went on in 4 rounds with the plan); the test synth has no window, so the links themselves, the real-time
  pictures and the time (estimated 3-5 hours for 700) are untried with Kontakt. `sso_patch_controls.json`'s 58 trill and
  measured-tremolo keys were cut at the bracket ("… Trill (Major 2nd) (… Trill"): fixed (the map didn't use them).
  The supervisor's "try n of m" said 4 / 2 where 5 / 3 tries are made: fixed.
  **Two patches a group** (the owner's run on build 226, 2026-09-28 20:14: about 25 s a patch, so 700 about 5 hours;
  "this'll take forever"): which control a controller moves is Spitfire's script's, and patches share scripts, so
  `links_plan.py` groups them (`groups()`: the same named controls in the same folder family, the `.nki`'s folder under
  Instruments, "Individual techniques" by the name's first part: 64 groups) and plans 2 of each (`--per-group`, spread
  by name; `--all`: every patch): 113 patches plus the 11 in full, about an hour. `measurements_from_extract.py` gives
  a group's other patches the links its measured ones agree on, for the controllers each changes (`linksFrom`), and
  lists a group whose patches disagree ("links differ": measure it with `--all`). The plan is a file next to the .bat:
  a new one needs no new build. Tried with made-up links JSONs only.
  **The owner's links run (build 226, 2026-09-28 20:14 - 09-29 01:00, all 700 on the first plan): 2 crashes in 4 hours**
  (121 in the fifth run, without the window): so Kontakt's window open is what keeps it from crashing, as the minidump
  suggested. The 11 "all" patches came out complete (now 668 complete, 28 not put back, 4 with cc 23 left out). **Its
  links were wrong**: 83 % of the changed regions started at the window's top, Kontakt's CPU and voice meters, which move
  with every note, so every box was most of the window and the matching said CC 1 -> Mic 5 level, CC 23 -> Mic 1 level.
  Now each change is kept as its 16-pixel cells (`PluginExtract::changedCells`, JSON `cells`, `noiseCells` from the
  baselines, `cellSize`, `windowSize`), and `PluginExtract::controlsMoved` (was `controllersToControls` in
  soundlibrarycheck.cpp) leaves out the cells that change by themselves (the baselines', and any cell more than 40 % of
  a patch's tries changed) and matches by the cells' overlap. `linkedBefore` counts only JSONs with `noiseCells`, and
  `measurements_from_extract.py` takes links only from them. Test `controlsMoved` (a Kontakt-like window: meters that
  change every time, five sliders). Untried with Kontakt: the next links run (2 a group) is its first try.
  **First look at that run (build 236; a folder of 53 patches the owner sent, 2026-09-29):** the cells work, but CC 23
  came out as Mic 1 level on the Contrabass Tuba and a few others: Kontakt's own frame moves too, its header's output
  meter (y 32-47, with the level: a mic fader changes it) and its instrument rack's slot meter (x 304, with every note),
  and those cells aren't in 40 % of the tries. So Kontakt's frame (top 48, left 352 pixels; the window is 1377 x 679 in
  all 53) is left out too: `controllers.frame`, written for Kontakt (`KONTAKT_FRAME_TOP/LEFT`), read by `controlsMoved`,
  and `measurements_from_extract.py` recomputes every patch's links from its cells (`controls_moved`, the frame taken
  as Kontakt's when a JSON has none), so build 236's data needs no new run. On the 53: CC 1 Dynamics, 11 Expression,
  16 Mute, 17 Release, 18 Variation, 21 (and 104 on the Performance patches) Vibrato, 22-25 Mic 1-4 level, 40-46 Harp
  Pedal 1-7; CC 7, 10, 64, 66, 103, 111 and pitch bend change the sound but no named control. Test `controlsMoved`
  has a header meter that follows one mic: wrong without the frame, right with it.
  **The links run finished** (build 236, 2026-09-29 06:16-07:08, 117 patches in 51 minutes, 1 Kontakt crash, retried):
  with the earlier runs, **698 of 700 patches have their links** (113 measured, 585 from their group; no group's
  patches disagree), kept in `sso_patch_measurements.json` (`links`, `linksFrom`): CC 1 Dynamics, 11 Expression, 16 Mute,
  17 Release, 18 Variation (Tightness on 2 patches that have no Variation), 21 Vibrato (104 too on the Performance
  patches), 22-25 Mic 1-4 level (fader columns x 416 / 480 / 544 / 608), 40-46 Harp Pedal 1-7. Vibraphone and Curated
  Ensembles - Tutti - Low Wood String Stab got none. A patch's own "Mic n level" parameter sometimes changed nothing
  (already at the value tried), and the controller then matched Mic Mix Distance (it moves faders 1 and 3): a control
  whose cells strictly contain the controller's is no longer a match (`controlsMoved`, `controls_moved`), so the group
  gives the mic. The 4 patches with cc 23 left out after crashes (Bass Trombone Solo / Bass Trombones a2 - Long, Celli -
  Trill (Minor 2nd), Clarinet Solo - Long Flutter) take it from a later run of theirs that measured it though it
  wasn't put back within its noise (`fromOtherRun`): now **672 complete, 28 not put back**.
  **When the owner hands it back**, run
  `tools/soundlibraries/read_plugin_data.py <folder or zip> [--full]`: it prints (and writes report.txt)
  the plug-in, its parameters by family, the mapping, programs, what each patch changed against the empty
  plug-in, what each CC and parameter does, and the state blobs' zlib streams and strings (`_decoded/`).
- Tested with the test synth (`tst_soundlibrary::pluginDescribe`, `pluginExtract`: it now has a "Tone"
  parameter no CC maps to and twelve placeholder "Macro n"; since 2026-09-30 its state keeps Tone (24 bytes), as
  Kontakt's keeps a script's controls, and `MSTESTSYNTH_INIT_MS` puts the state's Tone back). In the GUI under Xvfb with sfizz (Solo Violin 1
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
    **Per articulation, from the same files** (2026-09-29, `tools/soundlibraries/nki_articulation_details.py <library.json>`
    → `sso_nki_articulation_details.json`, by map name, 700 patches, 1457 articulations): keys its zones cover,
    recorded notes, variants (Vib, Non Vib, Alt Attack …), round robins, dynamic layers named in its groups (the
    longs': crossfaded on CC1 by the script), velocity layers in its zones (the shorts' dynamics), release samples,
    looped, median recorded length and lead-in skipped (`startMs`, ~100 ms). The same names as
    `sso_nki_articulations.json` but the instrument-name headers (Timpani's "Timpani"), Harp glissandi's sections
    ("Scale gliss Upwards / Fast") and `noSamples` for Long Sul G / C in the All techniques and Core patches (their
    groups map nothing: Voices 0, as the owner heard). `sso_nki_groups.json`: each patch's raw group list under the
    first mic (name, zones, key and velocity range) for the trees this reading doesn't cover. Names and numbers only.
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
    **Techniques switched on in a setup** (branch `sso-kit-unpurge`, 2026-09-30): the map's nine `"<patch> (all on)"`
    `<Patch>`es (gen_spitfire_sso.py) set Kickstart's arrays whole (`%c2lsa` on, `%4jwcn` keys; KontaktSetup writes
    values of another length), but what they switched on was silent: Kickstart loads only the samples it plays, and
    switching a technique on in its window calls `purge_group` for its groups; Kontakt saves each group's purge flag,
    and a loaded state never purges or loads again. Found by diffing Kontakt's states before and after switching
    Drums - Low's Bass Drum Roll on in the window (kthost's editor on the VM): only `%c2lsa[3]`, `$fqm41` and the
    flags of the tree mic's Roll / Roll HS groups (and their zones) changed. `KontaktSetup::unpurgeSwitchedOn`
    (kontaktsetup.cpp has the layout: group private byte len-55, zone private byte 47; each group's Kickstart
    metadata, the floats from 1e-6 in its private data: mic, hit, drum) applies Kickstart's own purge rule (a hit
    group is purged when its drum is off, `%x4jsr`, its mic is off for its drum, bit mic-1 of `%nvmxz`, or its
    technique is off, `%c2lsa`; it gives every hit group's flag in all nine kits and ensembles at their defaults,
    else the program is left alone) with the values set: `fromEmpty` loads the purged groups it now plays. Nothing
    else changes; a patch whose values switch nothing on is byte for byte as before. `madeFrom` records
    `kickstartUnpurge` for a patch whose values set `%c2lsa` (part of what it is made from: these are made again,
    and Kontakt's own states resaved before, silent, are never imported). Also found: Kickstart's round-robin
    reset keyswitches (`$nd5ia` on, from `$bcqbk` 24, one key per round robin of the technique with the most that
    is on) grew over 24-38 once the cymbals' Brush or Rain Sheet were on (13-15 round robins), so the ensembles'
    own keys 36-37 played nothing (Metal Clangs and Traditional Orchestra "(all on)" played nothing at their test
    key 36); and a drum off at the defaults (`%x4jsr`: Traditional Orchestra's Cymbal Med, Unpitched - Metal's two
    triangles) never plays. The "(all on)" setups now also set `%x4jsr` all on and `$nd5ia=0`. Verified with kthost
    offline renders on the VM: the nine setups made by this code, every key 0-127 after an all-sound-off (CC120): all
    477 `<Drum>` keys start a sound out of silence (peaks -0 … -70 dB; the quietest are slow FX, bows and swells),
    the switched-on ones (silent before) included, and 36-37 again. Test `kontaktKickstartUnpurge`
    (a synthetic program; with `SSO_KICKSTART_NKI` a real kit, Drums - Low: exactly the window's groups 104-121).
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

### Automation editor and lanes in Live (branch `automation-editor`, 2026-09-30 / 10-01)

  **The editor (2026-09-30, branch `automation-editor`; the owner: "when you select a MIDI track, MuseScore lets you
  edit the notes AND show you automation tracks for every possible parameter in SSO, in which you can draw automation
  curves just like you can in Ableton")**: `mscore/automationlanes.*` (`AutomationLanes`, one per ScoreView; header
  comment lists every gesture). Continuous View only: selecting an element of a library part unfolds its lanes under
  its staves; room from `Score::setAutomationSpace` (view state, never saved) in `System::layout2` (`SysStaff::lanesY`);
  ticks to x through the laid-out segments. Hooks in events.cpp (mouse, keys via ShortcutOverride while a lane has the
  focus, context menu), `ScoreView::paintEvent`, `startUndoRedo`, `layoutChanged`, `MuseScore::selectionChanged`;
  *View › Automation Lanes* (`toggle-automation-lanes`, QSettings `ui/canvas/automationLanes`). Model:
  `Automation::Edit` (add / move / remove / curve / draw / copy / paste, pure functions), `undoWrite` (one
  ChangeMetaTags per gesture). **Curves**: a LINEAR point may carry Live's Bézier control points (CurveControl1X/1Y/2X/2Y
  in the segment's box; metaTag 4th element, absent when straight, so old files are unchanged); the editor's Alt-drag
  sets a curvature k (`setCurvature`). **Dynamics lane**: a lane on the library's dynamics CC replaces the notation's
  CC1 from its first point (`LibPart::dynamicsLaneFrom`). **No read-only lanes** any more (the owner: "edit the
  automation curves in both and they sync up"): `liveHash` / `pointsHash` in a lane's extra, `Lane::playedByLive`,
  `Automation::merge` on import (the newer edit wins; changed on both sides: `Automation::conflicts`, a dialog asks per
  lane, the owner 2026-10-01). Tests `automationCurves`, `automationEditing`, `automationMerge`;
  3.6 round trip of a score with curved lanes (ab/roundtrip2.py): 0 lines, pages and MIDI identical. GUI tried under
  Xvfb (no unit test drives the widget).
- Live integration, automation lanes (2026-09-30):
  `"source": "live"` lanes come from a Live Set (`libmscore/liveset.*`). Read-only until 2026-09-30; now editable
  (`automation-editor`): `Lane::playedByLive` (unedited since Live's set had it) decides who plays it. Through MIDI
  output a lane Live plays sends nothing; with the hosted plug-in every lane plays. Plug-in parameter lanes MuseScore
  plays reach Live through the MuseScore Link device (protocol 3: `/ms/params`, `/ms/pvals`; live.remote~ from a table
  at Live's song position; `SoundLibraryHost::rememberParameterIds` / `knownParameterId`, since Live names Kontakt's
  slots "#001"). Tried in real Live 12.2 on the VM (LIVE.md).
- Lanes kept in the Live Set, export / freeze, option B research (2026-10-01, `automation-live-research`): LIVE.md ›
  Automation lanes in MuseScore and in Live.

# Part 2: the former HANDOFF.md

## Handoff: state of the sound library work (2026-09-25)

The work so far was done in a claude.ai cloud session, which has ended. This file tells the next
agent (Claude Code on the owner's local machine) where things stand. Read `CLAUDE.md` first: it
describes the architecture, the build, the articulation check and every run the owner has
made. The commit messages on this branch explain each step in detail.

### Legato and onset timing (2026-10-01, branch `claude/intelligent-volta-gx7gmw`)

On top of `legato-timing` (3f0cda5): 696341d takes the measurement branch's data (f8908f8 on
`claude/intelligent-cray-6pd4o1`; its HANDOFF.md, "Measurements for the legato-timing fixes", has the VM jobs, the
sweep tools and the sweep results of ed3a294 and 3f0cda5). Not heard or swept with SSO yet: the next step is the VM
sweep (`make_sweep_scores.py`, task `claude-sweep`) on a Windows build of this branch against 3f0cda5's.
Open, deliberately not changed (no data here supports a change):
- **Octave slurs of the other 35 Performance patches** (winds, brass, Solo Cello and the Sul G / Sul C Performance patches) still use
  their ±7 delay. Only the 8 string Performance patches have the 5-pitch grid (`sso_legato_grid_pitches.json`);
  `sso_legato_grid.json`'s ±12 are octave errors. Needs the rest check's `legatopitches` part on those 35 (VM, build
  c7338f4; the measurement HANDOFF's "Resume"). The single-pitch grid can't be corrected by a rule: on the 8 string
  patches its arrival is above the 5-pitch 50 % time by a median 95 ms (Basses), 98 (Violins 2), 58 (Violas), 25-30
  (Celli, Violins 1), -25…+5 (the three solo strings), per interval -150…+190: no common offset to carry to the winds.
- **Long slurred notes early** (sweep of 3f0cda5: ≥ 0.5 s notes to -156 ms in Basses, Horn, Violas, Violins 2 -51…-76):
  Basses, Violas and Violins 2 now use the 5-pitch grid, which is exactly where the single-pitch grid read 58-98 ms too
  long (above); whether that closes it needs the sweep. Horn Solo - Performance is still on the single-pitch grid
  (no 5-pitch data): leave it until `legatopitches` measures it.
- **Strings' slurred sixteenths 59-84 ms late**: the price of the `legatoTransition` cap (none up to 125 ms, half from
  250; CLAUDE.md › Fast runs), chosen on the VM for even levels in fast runs (a quarter's shift: 4.2 dB spread). Not
  changed: undoing it needs a measurement that shows even levels with a larger shift.

### Where things are

- Branch: `main` (called `ms4-playback` until 2026-09-27), the default branch (the
  sound-library branch was merged into it and deleted on 2026-09-25). Develop and push there;
  no pull requests.
- Latest Windows build: run 11 (commit 6bc3a62). All 10 `tst_soundlibrary` tests passed on
  Windows. Artifact `MuseScore-soundlibrary-win64`:
  https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/36178930191
- That build has:
  - the check memory (`checks.json`; *Tick what needs checking*);
  - the scan of every value 0–127;
  - *Add a patch…* for patches missing from the map;
  - the map fixes from the owner's full 42-patch run: Legato 20 is removed from the woodwinds
    and brass, and trills 70/71 from Trumpets a6.

### Tuning and Ethanol bar 14 (2026-09-26, edb5eec)

Built-in tuning is in (see `CLAUDE.md` › Tuning) along with the all-sound-off after a faded MS3
hairpin (Ethanol bar 14). Still open:
- the playback regression (`ab/trace/regress.sh`) has not been run on edb5eec. The `ab/` harness
  is not in the repository, so a new cloud container doesn't have it;
- *Tools › Tuning…*: the owner won't test it for now (2026-09-26: "leave it for now"); Ethanol
  bar 14 confirmed good;
- the owner: try the Playback drop-down (Mixer, Play Panel) switching MS3 / MS4 / SSO;
- the owner: pitches for the 49 accidentals MuseScore gives none;
- the kits' hit lists: done (2026-09-26, the owner's screenshots); the 42 one-drum patches' keys: a key
  scan (*Tick the patches to scan*), named from the `.nki` group order (CLAUDE.md › Articulations from the files);
- the owner: re-check the tuned percussion (Timpani, Celeste, Marimba, Vibraphone, Tubular
  Bells) with the fixed key scan.

### Extract plug-in data (2026-09-27)

The owner asked for all the data the SSO plug-in gives, for more control of it. *Check articulations…* ›
*Extract plug-in data* (see `CLAUDE.md` › Extract plug-in data). Next: the owner runs it (first without
*Try every controller* on all patches: fast; then with it on one patch per family: strings, woodwinds,
brass, tuned percussion, a Performance patch, Grand Piano) and hands back the zip. From it: which CCs
Spitfire's patches answer (expression, vibrato, release, tightness, reverb …) and their values, which
parameters Kontakt exposes by name, and whether its state holds anything readable. Then decide with the
owner which of those MuseScore should drive.

The MuseScore side for that is in (2026-09-27, see `CLAUDE.md` › Controllers): map `<Controller>`
(a CC or a plug-in parameter by title, default, staff texts), per-part values in the score, sent
in playback and export, set in *View › Sound Library…* › *Controllers…*. When an extract with
*Try every controller* arrives: run `tools/soundlibraries/controllers_from_extract.py <zip>`,
choose with the owner which controllers MuseScore should set and what they are called, put them
in `CONTROLLERS` / `PATCH_CONTROLLERS` of `gen_spitfire_sso.py`, regenerate the map.

### Extract library files (2026-09-27)

The owner asked to extract all the data possible from SSO for a full integration later. The plug-in's side is
*Extract plug-in data* (above); the library's files are `tools/soundlibraries/extract_library_files.py`
(see `CLAUDE.md` › Extract library files). Next: the owner downloads `ExtractLibraryFiles-win64` from the
workflow "Tool: Extract library files", double-clicks `ExtractLibraryFiles.exe` (or gives it the SSO folder)
and hands back the zip it names. From it (`--report`, then `library.json` and `archives/`):
- first run (03:27, library.json left out: 800 MB): SSO's 700 patches are readable, none encrypted, although
  the owner has the free Kontakt Player (see `CLAUDE.md` › Extract library files); 432,829 sample names.
  The extractor now sums zones up per group (small enough to send); the owner re-runs it to send the groups
- from the groups (articulation × dynamic × round robin, mic headers, release triggers), their key and
  velocity ranges and samples: check the map's articulations, ranges and keyswitches against the patches
  themselves (e.g. why Violins Long Sul G is silent in "All techniques");
- the sample names in the `.nkx` archives: which articulations, dynamics, round robins and mic positions
  exist per instrument, and their ranges;
- ProductHints, registry and database rows: the library's id, version and product names.
Then decide with the owner what goes into the map (ranges per articulation, round-robin counts, release
samples) and what MuseScore should drive.

### SSO's controls in MuseScore (2026-09-27)

The map drives SSO's named controls as Kontakt parameters (see `CLAUDE.md` › Controllers): per part in
*View › Sound Library… › Controllers…*. Next: the owner tries them on a build with this, checks which
controls say "not in <patch>" (a wrong title guess: fix `_p(...)` in `gen_spitfire_sso.py` with the title
Kontakt shows) and which mic "Mic 1" … "Mic 5" is, then the "?" names become real ones. Untried with
Kontakt: whether Spitfire's script keeps a value set this way.

### Plug-in extract, first run (2026-09-27 03:58)

Violins 1 only (see `CLAUDE.md` › Extract plug-in data › First run on SSO). Its 15 named automation slots
(Dynamics, Vibrato, Release, Tightness, Expression, Mic 1-5, Mic Mix Distance, Articulation Controller)
are what the Controllers map should drive, by parameter title. Open: whether every patch names the same
slots (a quick Extract plug-in data without *Try every controller* on one patch per family answers it), and
generating setups from the `.nki` files (Kontakt's state embeds the whole patch in a multi: a writer for
that multi, then one generated setup tried by the owner).

### Setups made by MuseScore (2026-09-27)

The owner loaded a setup made by `make_setups.py` in run 91 ("everything looks correct"), then asked for it
in MuseScore with no manual set-up, every patch at the library's defaults. Done (see `CLAUDE.md` › Plugin
hosting › Setups made by MuseScore): `KontaktSetup` (C++), the map's `<Files>` / `nki=` / `setup=` / `<Patch>`,
the host making setups on demand, *Set up…* / *Save setup* / *Add a patch…* gone, all 700 patches in Check
articulations with *Tick all*. The owner's `learned_settings.json` showed `$iooxo` 3 as the one setting.
Next, with the next Windows build: the owner plays a score (setups made on first load; the hand-made ones
moved to `old setups (not used)`), then *Check articulations* › *Tick all* › *Extract plug-in data* (describe
only) for every patch, and a *Check* of the map's patches with the made setups. If Kontakt refuses a setup
or its samples are missing: compare with make_setups.py's (which Kontakt loaded) and look at the sample
list first. Kits: at the library's defaults now (the owner: what they have off comes from the one-drum
patches, whose keys at their defaults aren't known yet: a key scan of those, or the owner's screenshots).

### Dynamics across techniques (2026-09-28)

Calibration in (see `CLAUDE.md` › Dynamics calibration, › Faster, in the background). Next: the owner
double-clicks `Measure SSO dynamics in background.bat` (every map patch, Performance ones included, in a
copy of MuseScore of its own) and hands back the zip; the summary's "# Dynamics balance" shows what's left
("!" lines).

Shorts on velocity now follow the dynamics (see `CLAUDE.md` › Shorts). Next: the owner runs *Check articulations*
with *Dynamics* ticked on the patches they use (one per family at least: strings, woodwinds, brass, a
Performance patch) and hands back the zip; from summary.txt, fix `<Dynamics velocity>` in `gen_spitfire_sso.py`
(techniques flagged "on velocity but not listed" / "not needed") and look at the flagged spans.

### Even dynamic steps (2026-09-28): in, waiting for the owner's ear

See `CLAUDE.md` › Even dynamic steps. Next: the owner runs *Measure dynamics in the background* with this build
(it measures the held notes' CC11 curves; the recording modes work from the older measurement too), restarts,
then compares the four modes of Advanced Options › *Even dynamic steps* on a score and picks one. Then: make
the chosen one the default (or keep the switch) and drop the others if the owner wants.

### Attack salience in the Recommended balance (2026-09-28, branch `attack-prominence`)

See `CLAUDE.md` › Attack salience. The owner's ear put strings' shorts at -4 dB where the loudness model said +1.
The recommendation now adds the shorts' attack salience with one weight fitted to what the owner heard right
(map: strings -4). Next: the owner runs *Measure dynamics in the background* with a build of this branch
(~5 min; it fills the "attack" curves), restarts, and reads summary.txt's "# Dynamics balance": per family
loudness only / with attack salience, and the fitted weight. Strings will show -4 (the reference); the other
families are the model's prediction: the owner tries them by ear (e.g. brass, woodwinds in a score) and, where
one sounds right at another value, sets it in Advanced Options › *Heard right* (the fit then uses every
reference, least squares). If the weight comes out 0 or the report says the attacks stand out no more than
their loudness, the feature doesn't explain the ear: look at the per-short "attack … beyond that" numbers.

### Automation (2026-09-28): infrastructure in, no UI; being redone through Ableton Live

The owner (2026-09-28): edit automation in Live 12 instead of an editor of our own; MuseScore keeps playing on
its own, Live's automation imported back read-only. Built on branch `live-integration` (worktree `wt-live`).
See `CLAUDE.md` › Automation. Earlier next steps: the UI (lanes to draw per part, targets from the part's patch's
controllers), a dynamics lane on top of the notation's, the map's controller entries for SSO's named
controls (`gen_spitfire_sso.py` from sso_patch_controls.json), and one try with Kontakt that a parameter
set by automation is heard (e.g. Vibrato on Violins 1).

### The Mixer on library parts (2026-09-28, branch `mixer-sso`): in, waiting for the owner's Windows check

The owner: "the mixer panning tool doesn't work … make all buttons in the Mixer work with SSO". See `CLAUDE.md`
› Plugin hosting › The Mixer on library parts (audit table, design). Volume and pan act in MuseScore on each
Kontakt instance's output (not CC7 / CC10), mute and solo silence the part's instances, reverb and chorus are
disabled for hosted parts (SSO's own room), the patch and port / channel show the library's. Tested here with
the test synth only. The owner, on a Windows build of this branch, with an SSO score:
- pan a string part hard left and right while it plays (it should move smoothly, no clicks), and a part with
  extra patches (a Performance legato plus shorts) and one with copies for other tunings: every note follows;
- volume down and up while playing; volume 100 / pan centre sounds exactly as before;
- mute and solo while long notes ring: they stop at once, nothing hangs, unmute brings the next notes back;
- export audio with a part panned, quieter and muted: the file matches (solo is ignored in an export, as for
  MuseScore's own sounds);
- the Mixer's details panel for an SSO part: reverb / chorus greyed with the tooltip, the patch reads the SSO
  patch, port / channel greyed; switch the part to "MuseScore 4" in "This part plays:": all back as before;
- if Kontakt was relied on for pan (its own Pan knob): it stays where the setup put it; the Mixer adds to it.

### Controllers and the Mixer live while playing (2026-09-29, branch `live-controls`): in, waiting for the owner's Windows check

The owner: "make what I do in the mixer or sound library controller reflect in the live playback". The
Controllers window (*View › Sound Library…* › *Controllers…*) was modal and stopped playback on OK; plug-in
parameters were only set at the next play. Now it is a window of its own that playback goes on behind: each
move is heard at once (Kontakt parameters on all the part's patches, extras and tuning copies; MIDI controllers
sent now and the ~10 measures rendered ahead corrected), OK keeps the values (undoable), Cancel puts back what
the part had. The Mixer was already live (volume, pan, mute, solo); the idle timer now also runs while the
patches load at score open. See `CLAUDE.md` › Controllers › Live. Tested with the test synth only (unit tests;
the window itself not run here). The owner, on a Windows build of this branch, with an SSO score playing:
- open *Controllers…* for a string part and drag Mic 1-5 levels / Mic Mix Distance / Release while it plays:
  the sound should change at once, without clicks or dropouts; the same on a part with extra patches (the
  Performance legato plus shorts): every patch follows;
- untick a control while playing: the patch's own setting comes back; Cancel: everything back as it was
  when the window opened; OK: playback goes on, Ctrl+Z undoes the change (heard from the next play);
- stop and start playback with the window open: the values stay as set in the window;
- does Kontakt keep a value set this way (a Spitfire script could reset a mic fader on the next note)? If a
  change is heard and then jumps back, say which control;
- the Mixer while playing, right after pressing play and just after opening a score (patches still loading):
  volume, pan, mute, solo act at once.

### Live integration (2026-09-28, branch `live-integration`)

See `LIVE.md`. In: MIDI clock / SPP / start-stop out on a sync port (tested end to end on Linux with
ALSA: clocks within ~1 ms of the notes), *Mixer › Play through Live*, *Advanced Options… › Import
automation from Live Set…* (read-only lanes, auto re-import, undoable, survives 3.6.2). Next: the owner
tries it on Windows with loopMIDI and Live 12, and sends a small `.als` (see LIVE.md › What still needs
the owner's Live set) to verify the reader's MIDI-input and curve parsing. Not merged into main.

### Merging with claude/intelligent-cray-6pd4o1 (prepared 2026-09-28; done 2026-09-28 on that branch, as planned)

The owner decides what goes to main; this is the plan. Both branches start at main 16e6933. That
branch was still being pushed to (a38eeba, 00:47 UTC): merge once its session pauses.
- Its commits not here: the background extract (098e85a, b7b274a, 32e8bfc, a38eeba), 4d1d657 (a setup
  Kontakt gives back unchanged isn't taken for its state), 036dc3e's NIKT title rule (soundlibrarycheck.cpp;
  the rest of 036dc3e is 9fa4ce2 here), 17ee5fb, 22219702 (soundfont), b209f4e (notes).
- `git merge-tree` conflicts, four files: `audio/vst3/vst3synth.cpp/.h` → this branch's (its `deliver`
  keeps varispeed; the other side is a subset); `.github/workflows/test_soundlibrary_windows.yml` → this
  branch's soundfont fix (67e5a42: kept with the cache, no download at each run; drop 22219702's step);
  `CLAUDE.md` → this branch's text, adding from the other: Kontakt receives the pitch bend (run 134),
  Timpani bends ±2 semitones. Everything else merges cleanly.
- After merging: build, `tst_soundlibrary`, `tst_tuning`; check the NIKT rule and 4d1d657 are in.

### SSO load times (2026-09-28, branch `sso-load-times`): in, waiting for the owner's measurement

The owner: "have a sub agent try to optimize load times of the SSO plugin". See `CLAUDE.md` › Plugin
hosting › Load times. In: the routes worked out once (not before every instance at score open and at every
play), no 100 ms pause between loads, a faster title index, Kontakt's own states shared between the
working setups and the background runs' copies (no slow first load for a patch any run has loaded), a
resaved setup kept over a Kontakt update, `load times.log` by step and per batch with when the memory
settled. Off until measured: setState on worker threads (`io/soundLibraryLoadThreads`).

The owner, once, with a build of this branch: close MuseScore, drag the score to speed up onto `Measure SSO
load times in background.bat` (bin), wait (20-60 min), hand back `Documents\MuseScore Sound Library
Check\Spitfire Symphony Orchestra load times <date>.txt`. Then, optionally, open that score in the new build
and hand back `load times.log` (setups folder) too: the real score open with the new code. From the report:
- phase 1: where a patch's time goes (new instance vs setState's component / controller; "one object" and a
  controller part as long as the component would mean Kontakt parses its state twice), how long until it
  sounds and how much memory;
- phases 2 and 3: whether Kontakt takes setState on worker threads, and how much faster the score is ready.
  Faster and no failure: make `io/soundLibraryLoadThreads` 2 or 4 by default. A hang (the run's last line
  says so) or failures: leave it off;
- phase 4: whether a spare taking a new patch frees the old one's samples and is faster than a new instance;
- phase 5: whether the Mic levels at 0 free memory (then mics a score doesn't use could be set to 0), and
  which saved script value, turned to 0, frees memory and silences which articulations. If one holds the
  technique switches: a setup per part with only the techniques its notation plays (`usedPatches` already
  knows them per patch), made with `KontaktSetup::withScriptValues` from Kontakt's own state.

### Piano v3.7's missing notes (2026-09-29, branch `piano-v37-fixes`): fixed, checked on the Windows VM

Measured with Kontakt 8 + SSO on the VM (see CLAUDE.md › Plugin hosting › A patch's own script, VERIFY.md ›
With the real library). Causes: (1) the Grand Piano's 256 voices, too few once the score's mics are on
(31-40 notes dropped in busy bars, the owner's e39fe815 export); (2) the patch's script initialised only at
the first render or play, which then dropped the Controllers set at score open (so only the second render
had the mics, and the drops); (3) bars 68-69 (a key struck while still sounding) was 7755d65, confirmed.
Fixed: 512 voices for every Kontakt patch MuseScore sets up, and `Vst3Plugin::settle` after each load.
The owner: listen to the clips handed over (bars 49-52, 59-62, 68-69) and to a live play of the piece with a
build of this branch; say whether the mic mix sounds as set in Controllers… from the first play on.

### What the owner is doing now

The fifth run (all 42 patches, scanned) is in: see `CLAUDE.md` › Tried by the owner. Steps 1, 2
and 4 of the last request are done (Oboe Solo and Solo Cello pass; the scanned articulations
are mapped where notation can ask for them). Still open:

- Sixth run (build before the extras): Motif Brass's six UACC values are right (names and
  sound); Grand Piano has Direct (1) and Tape (2), now mapped; every patch had its instrument
  loaded. The tuned percussion has no UACC setting: keyswitches from C-2 (Kickstart). The key
  scan now finds keyswitches; the owner runs *Check* on the tuned patches they use, then map
  them like Timpani (KEYSWITCHED and SPITFIRE_ADD in `gen_spitfire_sso.py`; techniques from the
  names on the sheet, e.g. Roll → tremolo, Muted → muted, Swell → none).
- Violins Long Sul G / Celli Long Sul C: settled, Spitfire's "All techniques" patch plays no
  samples for them (Kontakt's Voices stays 0).
- Extra patches and percussion kits are in (see `CLAUDE.md`). The owner was asked to set up,
  in *Check articulations*, the Performance patches of the instruments they use, and the
  percussion patches (Drums - High / Low, Unpitched - Metal / Wood, Other - Toys), then Check
  (tick *Scan every value* for the Performance patches: their UACC value is a guess) and send
  the zip. From it:
  - a Performance patch's scan: its real articulation values; fix LEGATO / PERFORMANCE in
    `gen_spitfire_sso.py`;
  - a percussion patch's key sheet (`<patch>.png`, `results.json` "keys"): which key plays
    which sound; fill `DRUMS` in `gen_spitfire_sso.py` with (MuseScore drum pitch, key, name,
    ids). MuseScore's drum pitches are GM-like (35/36 bass drum, 38 snare, 49/57 crash, 51 ride,
    52 chinese, 55 splash, 80/81 triangle, 54 tambourine, 56 cowbell, 75 claves, 76/77 wood
    block, 85 castanets …); check `share/instruments/instruments.xml` for each instrument.

### When the zip arrives

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
5. Update the "Tried by the owner" part of `CLAUDE.md` with what the run showed.
6. Build and run `tst_soundlibrary` locally if you can (`CLAUDE.md` › Building). The
   `spitfireMap` test checks instrument matching.
7. Push. If code changed (not just the map) and the owner needs a new Windows build, put
   `[windows-build]` in the last commit message. The workflow then builds, tests and uploads
   in about 15 minutes. Use Actions minutes conservatively (the owner, 2026-09-27; see
   `CLAUDE.md`): only when the owner needs a new build to try something;
   - validate locally first;
   - batch fixes into one run;
   - don't retry blindly.

   A map-only change also needs a new build to reach the owner, because the XML is installed
   with MuseScore. Alternatively the owner can copy the regenerated XML over
   `share/soundlibraries/` in their installed folder.

### Things only the cloud session could do

- The owner's review page "SSO Articulation Map" (a claude.ai artifact, see `CLAUDE.md`) was
  read with claude.ai's ArtifactData tool. Claude Code on a local machine can't read it, so
  ask the owner to paste their marks if they're needed.
- Nothing here can hear Kontakt or SSO. Say whether something was tested with the owner's
  Kontakt, the test synth or sfizz when you report.

### Playback verification (2026-09-28, branch `playback-verify`)

`MuseScore --verify-playback` and `Verify SSO playback in background.bat` (see `VERIFY.md`, `CLAUDE.md` ›
Playback verification): renders scores through Kontakt offline as an export does, again with the
built-in synth, and reports missing chords and notes, notes cut short, silences, clipping and drift,
each with what happens on its slot at that moment. Tried here only: with the MS Test Synth and injected
faults (all detectable faults found, nothing else flagged) and on the owner's piano exports through
`--verify-audio`. Next:
- the owner: a Windows build of this branch (the branch doesn't trigger the build workflow: *Run
  workflow* on it), then double-click the `.bat` once (the test scores) and once with the piano score
  dropped on it, and hand back both zips. Check with `read_verify_report.py`: the run time, whether the
  clean test scores pass with Kontakt (thresholds were set on one piano score: strings and winds
  through SSO are untried), and the piano score's missing notes in bars 59-62 with their "notes in the
  2 s before" (a voice limit: then Kontakt's voice settings, or fewer release samples);
- look at what the report says about repeated notes of the same key (an earlier note still on when
  the next starts): with the test synth that cuts the new note; with Kontakt unknown;
- the self-hosted runner workflow is there but optional; the owner is wary of it (2026-09-28).

# Part 3: the measurement branch's HANDOFF.md (merged 2026-10-02)

Added verbatim when `claude/intelligent-cray-6pd4o1` (the measurement branch: VM measurements, Kickstart unpurge,
percussion hit lists, controller / timing / rest runs) was merged into `main` on 2026-10-02. Its additions to the
former CLAUDE.md are in Part 1, each in its section. Below: the sections it added to the former HANDOFF.md (headings
one level deeper), then the lines it changed in older sections. "Start here" describes that branch on 2026-10-01.

### Measurements for the legato-timing fixes (2026-10-01, measurement agent; the fixer works on branch `legato-timing`)

The owner: a measuring agent and a fixing agent; this branch holds the measurement tools and data, `legato-timing`
the playback changes (legato delay by interval, held notes early by their onset, shorts by real length, releases).
- **New rest parts** (aff12be, Windows build https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/36848821507,
  on the VM as `C:\claude\MuseScore-soundlibrary-win64-aff12be`): `--rest-parts onset,shorts,legatolengths` (CLAUDE.md ›
  "Onset, shorts' lengths, legato after short notes"); reader `tools/soundlibraries/onset_from_check.py` →
  `sso_sound_onset.json`, `sso_short_lengths.json`, `sso_legato_lengths.json`.
- **VM job** (task `claude-measure` → `C:\claude\measure\measure.cmd` → `job5.cmd`, log `job5-out.txt`): onset of the
  string sections and solo winds / horn and their Performance patches (`onsetA.txt`), shorts for the fixer's set
  (`shortsF.txt`), shorts of the rest of `onsetA.txt`, legato lengths on 6 Performance patches (`legL.txt`), then onset
  and shorts of every other non-percussion map instrument (`onsetB.txt`) and legato lengths of the other Performance
  patches (`legB.txt`).
- **Regression sweeps** (6820405): `tools/playbackverify/make_sweep_scores.py` (16 instruments; convert to .mscx with
  MuseScore 3.6, then `fix_sweep_ids.py`), rendered on the VM by task `claude-sweep` (`C:\claude\sweep\run.cmd` reads
  `args.txt`: `<build sha> [score filter]`; outputs `C:\claude\sweep\out-<sha>\<score>`), read with
  `tools/playbackverify/analyze_sweep.py <notes.json> <part library.wav>`.
- **Data so far** (aef45b6): `sso_sound_onset.json` 20 patches, `sso_short_lengths.json` 24, `sso_legato_lengths.json` 6
  (no dependence of SSO's transition on the first note's length). The note-off barely shortens SSO shorts (Spiccato
  ~280 ms, Short 0.5 ~500-700, Short 1.0 ~500 at 0.05-0.25 s, ~950-1250 from 1 s).
- **sso_legato_grid.json's ±12 arrivals are wrong** (centsShift's free search takes an octave for the unison: up to
  ~700 ms). c7338f4 adds `--rest-parts legatopitches` (harmonic timing, 5 start pitches, mf, octaves usable; build
  https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/36875559985) → `sso_legato_grid_pitches.json`.
- **Sweep ed3a294 (fixes 1, 2, 4 + shorts by length) vs aff12be (none)**, 13 of 16 instruments (Oboe, Solo Violin,
  Violins 1 lost: their renders were deleted before analysis): slurred pitch arrival from +86..+293 ms late to
  -86..+89 (intervals 1-7 pooled -26..+19) but octaves 120-176 ms early (the grid's ±12); ≥ 0.5 s notes 16-56 ms
  early overall (the grid's arrive = end of the step); held mf onsets 20-112 → -1..+67 ms; sul tasto (Long Super
  Sul Tasto) still 240-600 ms late, flautando 145-255 (fixer then switched onsets to perceived t15). Per-note results:
  `~/.claude/jobs/ed934dfd/tmp/cray/sweepres/<sha>/` (not in the repository); compare with `compare_sweeps.py`.
- **Paused by the owner (2026-10-01 ~08:15 VM time).** Measured and committed: `sso_sound_onset.json` 112 patches
  (every non-percussion map instrument), `sso_short_lengths.json` 24, `sso_legato_lengths.json` 6,
  `sso_legato_grid_pitches.json` 8 (the string Performance patches; -12…+7 usable, mid 160-245 ms; **+12 still
  unreliable**, median 540: the old note's reverb keeps the lower pitch's odd harmonics).
  **Done 2026-10-01 after the resume** (build c7338f4, `job7.cmd`): `sso_legato_grid_pitches.json` 43 Performance
  patches, `sso_short_lengths.json` 112, `sso_legato_lengths.json` 43. Grid mid times, family medians: strings 210-240,
  woodwinds 120-130, brass 130 ms; **octaves still not trustworthy** (+12: 375-550, the reverb; -12: 70-160, the new
  lower note's own harmonics rise before the step): use each patch's other intervals for ±12.
  **Resume**: on the VM, `C:\claude\measure\job6.cmd` with its onsetB step removed (restBefore skips what is done;
  use build c7338f4 for every step, aff12be is deleted), task `claude-measure`; read every results folder since
  2026-10-01 0329 in `Documents\MuseScore Sound Library Check` with `onset_from_check.py`.
- **Sweep 3f0cda5 vs aff12be** (all 16): held mf onsets -14..+57 ms; sul tasto 45-185 (Violins 1 123, Violas 185),
  flautando -39..+223 (Violins 1 223, Violas 160); slurs -7…+7 pooled within -16..+9 but octaves still early (-12:
  -86, +12: -121); strings' sixteenths +59..+84 late, ≥ 0.5 s notes early in Basses (to -156), Horn, Violas,
  Violins 2 (-51..-76). Rerun: `make_sweep_scores.py`, convert, `fix_sweep_ids.py`, copy to `C:\claude\sweep\scores`,
  `args.txt` = build shas one per line, `schtasks /Run /TN claude-sweep`, then `sweep/fetch.sh <sha>` (in the job
  tmp `~/.claude/jobs/ed934dfd/tmp/cray/`; it deletes each render's audio once analysed) and `compare_sweeps.py`.
- **Sweep e6f44e6 (claude/intelligent-volta-gx7gmw: onsets from the 112, string grid) vs 3f0cda5**: slurs 1-7 pooled
  -6..+19; string octaves -86 → -8, winds -84 and brass -111 unchanged (no grid then); Basses / Violas / Violins 2
  long slurred notes no longer early (Violins 2 now +32..+52 late at every length); strings' sixteenths +19..+62;
  slurred sixteenths' level spread 4.5-12.4 dB in every instrument, unchanged; sul tasto / flautando -10 dB time
  median 253 → 149 ms but over-shifted in places (Violins 2 sul tasto high -399 at -15 dB, Celli flautando -189 /
  -216) and still late in others (Violas sul tasto mid 567, Violins 1 flautando low 536 at -10 dB).
- **Octaves timed by templates** (f0bef96; the coordinator: a reliable +12): each frame fitted as a A + b B of the two
  notes' own spectra, the second note's share of the power at 10 / 50 / 90 % (`tLeaveMs` / `tMidMs` / `tArriveMs`, appended
  to `sso_legato_grid_pitches.json`'s rows; `arriveTMs` in the sweep analysis). Synthetic slurs with a known answer
  (`tools/playbackverify/octave_synth_check.py`): -40..+60 ms for every interval and hall level, where the harmonic
  ratio was +75..+320 late for +12 and 60-70 early for -12. On real SSO it agrees with the harmonic times for
  non-octaves (grid: median +10 ms; sweep: +1, 80 % within ±50). Grid mid by templates: brass +12 130 / -12 140 /
  others 140; woodwinds 125 / 170 / 140; strings 350 / 210 / 240 (string upward octaves really are ~100 ms slower).
  The a289780 sweep re-read with templates: octaves strings +12 +60 / -12 +40, woodwinds -50 / +60, brass -70 / +90.
- **VM IP** changed to 172.29.253.109 (2026-10-01 host reboot). Disk is tight: `sweep/fetch.sh` deletes each render's
  audio once analysed; keep VM outputs under ~2 GB.

### Start here (2026-10-01: the cloud session ends, the work moves to the owner's Debian VM)

- **Branch**: `claude/intelligent-cray-6pd4o1`, last commit e82a202, everything pushed, `main` merged in on
  2026-09-30 (341aa78). Work on your own branch; the owner decides what goes to `main` (CLAUDE.md › Branches).
  To continue this session's conversation itself: `claude --teleport session_01DpUCQZEy2ysGW5HaKShRvm` in a clean
  clone (same claude.ai account).
- **Latest Windows build**: run 267, https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/36783186961
  (artifact `MuseScore-soundlibrary-win64-eddd1bc`). Windows builds come from `[windows-build]` in a pushed commit
  message on the branch (`.github/workflows/test_soundlibrary_windows.yml`). Name builds by run number and link.
- **What runs where**: Kontakt and SSO run only on the owner's Windows PC. The VM (like the cloud container before)
  can build MuseScore, run the tests with the test synth (`mtest/libmscore/soundlibrary/testsynth`) and read the
  zips the owner sends back; it can't play SSO. Build steps: CLAUDE.md › Building (Linux container); headless runs
  with the test synth: CLAUDE.md › Load times (isolated `HOME`, `~/.vst3/mstestsynth.vst3` linked, …).
- **The owner's direction (2026-09-30)**: "your job is only to collect data". The extraction is done (below); the
  playback problems it found are listed for whoever builds playback next, not fixed.
- **Background runs on Windows** (bat files next to the exe, each with its own log in Documents/MuseScore Sound
  Library Check, supervised: a crash or hang is retried, then that patch left out): `Measure what's left of SSO in
  background.bat` (dynamics, then timing, of every sound; skips what earlier runs did, as long as their folders are
  still in Documents/MuseScore Sound Library Check). A run that looks stuck usually isn't: Performance patches take
  12-16 s each. Hand-backs are zips; read them with `tools/soundlibraries/dynamics_from_check.py` and
  `timing_from_check.py` (derived numbers only go into the repository: it is public).
- **Gotcha**: a `.bat.in` line inside `if ( … )` must not contain parentheses (an `echo` with "(…)" closed the block
  and the window vanished at once, run 266).
- **Measured on the Windows test VM (2026-09-30/10-01; the owner: "measure everything left in the VM, I'm sick of
  doing everything manually")**: the VM (see the memory note / CLAUDE.md) runs Kontakt + SSO itself now; scheduled task
  `claude-measure` → `C:\claude\measure\job.cmd`. What the old "Not measured" list had:
  1-5. `--check-rest` (CLAUDE.md › The rest of every sound): every sound of all 700 patches across its range at pp / mf /
     ff, 8 repeats (round robins), every control at 0-1 in 5 steps, a 9 × 14 legato grid on 45 patches (Horn Solo /
     Horns a2 - Legato and Oboe Principal included) → `sso_sound_range.json`, `sso_sound_repeats.json`,
     `sso_sound_controls.json`, `sso_legato_grid.json` (`rest_from_check.py`).
  6. Percussion techniques off at the defaults: Kickstart's `%c2lsa` / `%4jwcn` (+ `%x4jsr`, `$nd5ia`) and the groups'
     purge flags (`KontaktSetup::unpurgeSwitchedOn`); 9 measurement-only "(all on)" `<Patch>`es switch every off
     technique on at a free key; their dynamics, timing and rest measured with build 0e9f0c4: all 258 switched-on techniques sound (in the rest check Tam Tam FX Scrape and Wind Gong FX Bow, slow swells, missed its 10 dB "sounds" test).
  7. Vibraphone: links as its group's (CC 11 Expression, CC 23 Mic 2 level; Kickstart doesn't redraw offline). Low Wood
     String Stab: no CC moves a named control (only Kontakt's volume / pan and CC 111). `sso_patch_measurements.json`.
  Still not measured: parameters only in 5 steps; legato from the test pitch only; the controls' effect at the test
  pitch only.

### For the next agent: playback problems the extraction found (2026-09-30; not fixed, the owner: "your job is only to collect data")

All numbers are in `tools/soundlibraries/` (see CLAUDE.md for how each was measured). Ask the owner before building any.
1. **Slurred notes sound late.** On the 42 Performance patches the second note of a slur reaches its pitch 70-430 ms
   after its note-on (median 185; `sso_articulation_timing.json`, `legato`), in one step, with a 4-22 dB dip. Starting
   slurred notes earlier by the patch's own delay would put them in time.
2. **Velocity barely changes SSO's legato speed** (20 / 64 / 110 give the same transition on most patches). The renderer
   keeps MS4's velocity for legato "because Spitfire's legato speed is on velocity" (CLAUDE.md, Shorts): that reason
   doesn't hold for SSO; velocity can be used for something else or left.
3. **Slow attacks.** Held notes reach full level 175 ms after the note-on (median), Violas ~300 ms, Flautando / Sul Tasto
   / Harmonics up to 1 s; brass and woodwinds 60-120 ms (`fullMs`). Same remedy as 1, per articulation.
4. **Shorts ring on.** A short's body (to 20 dB under its peak) is 265-2670 ms, median ~1 s, the hall in the recording;
   a 0.1 s note sounds 370-1000 ms (`bodyMs`, `shortNoteBodyMs`). Matters for choosing Short 0.5 / 1.0 by length
   (`<Articulation length>` uses Spitfire's nominal 0.5 / 1.0 s).
5. **Releases** ring 0.6-2.9 s after the note-off (`releaseMs`, median 855 ms); a lane (tuning copy) is reused after
   `tail` 1.5 s, shorter than some releases (Flautando 2.9 s, Sul Tasto 2 s, Super Sul Tasto 2.4 s).
6. **Microtones by pitch bend**: the Performance patches and Horn Solo / Horns a2 - Legato bend ±100 cents linearly,
   Kickstart percussion and some solo strings ±195 (`sso_patch_measurements.json`, `pitchBend`). The owner's first choice
   (2026-09-27) was pitch bend; varispeed was built because the All techniques patches don't bend.
7. **Controller links** (`sso_patch_measurements.json`, `links`): CC 16 Mute, 17 Release, 18 Variation (Tightness on 2),
   21 (and 104 on Performance) Vibrato, 22-25 Mic 1-4 level, 40-46 Harp Pedal 1-7, besides CC 1 / 11. The map drives
   these as Kontakt parameters by title; CCs would also work.
8. **Patches that play nothing where the map might send notes**: Long Sul G / Sul C in Violins 1 / 2 and Celli All
   techniques and Core (`noSamples` in `sso_nki_articulation_details.json`); Rain Sheet Swell mp; Low Ensemble's Toms 3-5
   and Field Drum Rim / X Stick share keys (the owner: a real conflict).
9. **Kit techniques that are off** at the kits' defaults (rolls, swells …) are only in the one-drum patches; the owner's
   plan: switch one on in the kit when a score needs it (CLAUDE.md, Kits).

### How complete the extraction is (2026-09-30)

- **From the files (all 700 `.nki`)**: every patch's groups, zones, key and velocity ranges, articulation names, round
  robins, dynamic / velocity layers, release groups, loops, lengths; the 279 archives' 432,829 sample names. Done.
- **Switching**: every map value checked by picture and ear; the 12 Core / Decorative and 4 Curated patches scanned;
  Harp glissandi keyswitches; every percussion hit list (82 lists, 519 hits). Done.
- **Named controls** of all 700 patches; **controllers and parameters** measured on all 700 (672 put back within their
  noise, 28 measured but not put back: round-robin percussion, Fanfares, Flutter); **links** on 698 (Vibraphone and Curated
  Tutti - Low Wood String Stab have none); **pitch bend** on all 700. Done, apart from those 2 links.
- **Dynamics curves**: every sound of all 700 patches (1804: articulations, 504 drum hits, one-sound patches;
  `sso_sound_dynamics.json`, build 261's run, 2026-09-30). Done.
- **Timing and legato**: every sound of all 700 patches (`sso_articulation_timing.json`, run 267's build, 2026-09-30).
  Done, but for the slur transitions of Horn Solo - Legato, Horns a2 - Legato and Oboe Principal - Total Performance
  (timed as one sound: the run measures a slur only on an articulation whose first technique is legato).
- **Parameters** measured at 0 and 1 only, not the curve between.

### Where things were (2026-09-25; superseded by "Start here")

- Branch then: `main`; latest Windows build then: run 11 (6bc3a62).

### Lines the measurement branch changed in older HANDOFF.md sections

In "Tuning and Ethanol bar 14", the kits' item became:

- the kits' hit lists: done (2026-09-26, the owner's screenshots); the 42 one-drum patches: scanned in the
  background (2026-09-28 01:46, which keys sound: `sso_drum_keys_sounding.json`); they don't follow the `.nki` group
  order, so keys are named from the owner's hit-list screenshots. Snare 1 / 2 and Triangle 1 / 2 are in the map (the
  sounds the kit patches lack at their defaults); the other 38 and the 6 ensembles: read from the owner's picture run of 2026-09-28 05:02 and confirmed, in `tools/soundlibraries/sso_percussion_hits.json` (with notes on conflicts and techniques that are off); the 42 one-drum lists and the 6 ensembles' are in the map (reference), with every technique that is off as a key-less entry; next, when a score needs a technique a kit patch has off (the rolls of bongos, congas, bass drum, field drum, cymbals, tam-tam, thunder sheet, tambourine, sleigh bells, castanets; Rain Sheet Swell mp …): a system that switches it on in the kit patch and gives it a key (the owner, 2026-09-28: to be built later); Harp glissandi's modes: in the map (reviewed); the three-mic patches' mics: done (Close, Tree, Ambient);

In "Things only the cloud session could do", added:

- Every controller and pitch bend on all 700 patches: `Measure SSO controllers in background.bat` (offline, no window; the owner to run it and hand back the extract zips; read with `read_plugin_data.py`). The first run (build b903d9a) crashed in Kontakt 16 s into Violins 1: it went offline while the patch still loaded; now only after the patch sounds in real time. The second (build fe5d050) crashed on Violas and put CC 7 back to 0 (silence, so later controllers read nothing); now each controller's own value is searched and a supervisor process restarts the run after a crash or hang, leaving that patch out (see CLAUDE.md). **Done (2026-09-28 18:48): 695 of 700 measured**, kept as `tools/soundlibraries/sso_patch_measurements.json`; the 5 then missing (Cimbassi a2 - Long, Curated Woodwind Ensembles, Bass Trombone Solo - Fall, Field Drum, Cimbassi a2 - Long Alt) came in later runs: all 700 are in `sso_patch_measurements.json` (672 complete, 28 not put back). Done (2026-09-29): 698 of 700 patches' controller links in `sso_patch_measurements.json` (see CLAUDE.md › The links run finished); the 4 with cc 23 left out now take it from a later run (672 complete, 28 not put back); per-articulation key ranges, round robins, dynamic / velocity layers, releases and lengths from the `.nki` files in `sso_nki_articulation_details.json` (CLAUDE.md › Articulations from the files). Timing per articulation and legato transitions: built (2026-09-29), `Measure SSO timing in background.bat` (CLAUDE.md › Timing, in the background); done (build 254, 2026-09-30: 96 patches in 17 minutes; the first run on build 249 had hung, now supervised and resumable); kept as `sso_articulation_timing.json`. Not yet used by playback: slurred notes on the Performance patches sound their pitch 70-430 ms after the note-on (median 185; velocity barely changes it), so starting them earlier per patch would put them in time; attacks of 100-1000 ms likewise (ask the owner first). Optional, not built: parameters between 0 and 1, loudness curves beyond the ~160 patches. Before that (run 226's links were wrong, box-based): the owner ran `Link SSO controllers in background.bat` again on the build with window cells (which named control each controller moves on 2 patches of each of 64 groups, 113, everything on the 11 left incomplete; see CLAUDE.md › Links run, › Two patches a group) and hands back the extract folders and the log; then `measurements_from_extract.py` on them and the earlier ones. Open: the Performance patches bend ±100 cents linearly, so their microtones could use pitch bend instead of varispeed (the owner's first choice); ask before building it.
