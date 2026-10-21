# Sound libraries (external MIDI)

MuseScore can play chosen parts of a score through an external sample library, such as
Spitfire Symphony Orchestra in Kontakt, instead of its built-in synthesizer. It picks the
library's articulation for each note from the notation. For example:

| Notation | Articulation |
|---|---|
| no mark | Long |
| staccato / staccatissimo | Short / Spiccato (section strings: staccato Short 0.5, staccatissimo Spiccato) |
| accent or marcato on a short note | Marcato |
| accent or marcato on a long note | Long marcato attack, where the library has one |
| tenuto on a short note | Tenuto (brass, woodwinds), Short 1.0 (section strings) |
| single-note tremolo, "trem.", "flz." | Tremolo / flutter (the note is played once, not repeated) |
| trill (symbol or line) | Trill sample at the written interval (minor or major 2nd …), when the library has one |
| fall, scoop | Fall, rip |
| "pizz." / "arco", snap pizzicato, "col legno" | Pizzicato, Bartók pizzicato, col legno |
| "con sord." / "senza sord.", mute symbols | Con sordino (muted, stopped) variants |
| "sul pont.", "sul tasto", "flautando", "cuivré", "ord." | Those variants, where the library has them |
| "espr.", "espressivo", "molto vib." (until "non vib." or "ord.") | Long (Rachm.) for held notes, section strings (slurred notes keep the legato) |
| harmonic notehead (diamond), "harm." | Harmonics |

MuseScore sends each note's dynamics as note velocity plus a continuous dynamics controller
(Spitfire: CC1). It drives the library with articulation switches (Spitfire: UACC on CC32,
keyswitches or program changes for other libraries).

This only works with the **MuseScore 4** dynamics method (View › Synthesizer › Dynamics). That
is the default.

## Setup on Windows with Spitfire Symphony Orchestra (plug-in in MuseScore)

MuseScore hosts the library's plug-in itself (Kontakt, VST 3). There is no separate host, no
virtual MIDI cable, and audio export includes the library.

1. In *Edit › Preferences › I/O › Sound library*, choose *Spitfire Symphony Orchestra* and set
   *Play through* to *Its plug-in, in MuseScore*. MuseScore finds *Kontakt 8* or *Kontakt 7*
   in `C:\Program Files\Common Files\VST3` on its own. If yours is somewhere else, choose
   its `.vst3` with the *…* button next to *Plug-in*.
2. Play, or export audio (File › Export, or `mscore -o score.wav score.mscz`).

There is nothing to set up in Kontakt. MuseScore sets each patch up by itself: it finds SSO's
folder through Native Instruments' registry entry (or the folder chosen with *View › Sound
Library…* › *Library folder…*), reads the patch's `.nki` and loads it into Kontakt at the
library's defaults, with articulation switching set to UACC. *View › Sound Library…* lists each
part with the patch it plays; *Show* opens Kontakt's window to look at it (a change made there
lasts until the patch loads again).

Each library part gets its own plug-in instance. The first playback of a score loads them all,
and the samples take a moment. The library's audio is mixed in after MuseScore's reverb,
because SSO brings its own room. The setups MuseScore makes are kept in its data folder under
`soundlibraries/<library>/<patch>.vst3state` and made again when the `.nki`, the map's values or
Kontakt change. Setups made by hand before are moved to `old setups (not used)` there.

### Checking the map against the plug-in

*View › Sound Library…* › *Check articulations…* lists every patch of the library: the map's,
then all its others (SSO: all 700 `.nki`).

1. Tick the patches to check (*Tick all*, *Tick what needs checking*, or by hand), click
   *Check* and leave the computer alone. Kontakt's window has to stay visible. It takes about
   15 seconds per patch.
2. MuseScore opens the folder `Documents/MuseScore Sound Library Check`. Hand back the
   `.zip` it made.

MuseScore remembers each patch's last check (the *Last check* column). A patch is ticked for
checking only when it was never checked, when its setup or its map entry changed since, or
when its last check couldn't run. *Tick what needs checking* ticks just those; tick others by
hand to check them anyway.

- *Scan every value* also tries all 128 UACC values. It finds articulations the patch has and
  the map lacks, and map values the patch doesn't have (SSO shows "None" for them). It takes
  about a minute more per patch.
- The patches the map has no articulations for are always scanned, so their articulations can
  be added to the map from the results. (*Add a patch…*, for a library whose map doesn't list
  its files, adds one by name; it needs a setup file of its own.)

For each articulation value in the map, the check keeps a picture of Kontakt's window after
the switch. It also listens to whether the switch took. Each value's note is played after two
different articulations, and should sound the same both times. The verdicts:
- *switches*: the value switched the plug-in.
- *ignored*: the patch has nothing on that value.
- *silent*: the note plays nothing.
- *unclear*: the listening test could not decide.
- *sounds like N*: the value plays the same sound as value N.

The picture shows which articulation the patch selected.

## Setup on Windows with a host of its own (MIDI output)

Use this if the library runs in a DAW or standalone host (*Play through: MIDI output*).

1. **Virtual MIDI cable.** Install [loopMIDI](https://www.tobias-erichsen.de/software/loopmidi.html)
   and create one port for every 16 library parts. A full orchestra usually needs 2.
2. **Host.** Start Kontakt standalone (or a DAW with Kontakt). Enable the loopMIDI ports as
   MIDI inputs and assign them to Kontakt ports A, B, C, D in the same order.
3. **MuseScore.** Open *Edit › Preferences › I/O*:
   - PortAudio: choose the first loopMIDI port as *MIDI output* and the next ones as
     *MIDI output B, C, D*.
   - Sound library: choose *Spitfire Symphony Orchestra*, with *Play through: MIDI output*.
   - Click *Show routing…* (or *View › Sound Library…*) with your score open. For each part
     it lists the patch to load, the port (A–D) and the MIDI channel (1–16).
4. **Patches.** Load each listed patch in Kontakt on its port and channel, with articulation
   switching set to **UACC**.

To line up the library's sound with the built-in synthesizer, use *MIDI output latency*.
Audio export can't record the external host. Use *File › Export › MIDI*, which includes the
articulation switches and dynamics, and render that in the DAW.

Notes for both setups:

- Parts the library has no patch for (piano, percussion …) play on MuseScore's built-in
  synthesizer.
- Solo and section patches are picked from the MuseScore instrument (Violin → Solo Violin 1,
  Violins → Violins 1). "a2" / "a6" patches are picked from the part name (for example,
  "Flutes 1.2", "Horns a4"). "Violins II" gets the Violins 2 patch.
- Mixer volume and pan don't reach the library. Use Kontakt's own mixer.

## The map files

A map lists the library's instruments, the MuseScore instrument ids each one serves, and the
switch value of each articulation. For example:

```xml
<SoundLibrary name="Spitfire Symphony Orchestra">
  <Switch type="cc" number="32"/>            <!-- or type="keyswitch" / type="program" -->
  <Dynamics cc="1" expression="127"/>        <!-- dynamics controller; CC11 is set to 127 -->
  <Plugin files="Kontakt 8.vst3;Kontakt 7.vst3"/>  <!-- the plug-in to host, found in the VST3 folders -->
  <Files registry="Spitfire Symphony Orchestra"/>  <!-- MuseScore sets the patches up from their .nki -->
  <Instrument name="Violins 1" ids="violins"
              nki="Instruments/Symphonic Strings/Violins 1 - All techniques.nki" setup="$iooxo=3">
    <Articulation name="Long" value="1" techniques="long legato"/>
    <Articulation name="Long CS" value="7" techniques="long legato" modifiers="muted"/>
    <Articulation name="Spiccato" value="42" techniques="short spiccato staccatissimo"/>
    …
  </Instrument>
  <Patch name="Violins 1 - Core techniques" nki="Instruments/Symphonic Strings/Violins 1 - Core techniques.nki"
         setup="$iooxo=3"/>
</SoundLibrary>
```

- **Setups made by MuseScore** (a Kontakt library hosted in MuseScore). `<Files registry>` names
  the library's key under `HKEY_LOCAL_MACHINE\SOFTWARE\Native Instruments`; its `ContentDir` is
  the folder the `nki` paths start from (else the one chosen in *View › Sound Library… › Library
  folder…*). Each `Instrument` and `Patch` gives its `.nki`; `setup` lists values of the patch's
  script to set, `name=value;…` (only where the script has the name with a value of the same
  length: SSO's `$iooxo=3` is "UACC & UI only"). A `<Patch>` is one of the library's other
  patches: never chosen by notation, but set up and checked by *Check articulations*. `scan`
  (`values` or `keys`) marks one whose switch values or keys are still to be found (*Tick the
  patches to scan*); `pitch` is its test note. Once they are known, they are listed as
  `<Articulation name value/>` children (no techniques: for reference), with a `<Switch>` child when the patch switches otherwise than the library (Harp glissandi: `type="keyswitch"`, values are keys). A percussion `<Patch>` (SSO's ensembles) lists its drums' hits as `<Drum key name/>` (no pitch: reference). See `audio/vst3/kontaktsetup.h` for how a setup is made.
- `techniques` lists what the articulation can play (an empty list: no notation asks for it; it
  is listed for reference and checked by *Check articulations*, but never chosen): `long legato short staccatissimo spiccato
  tenuto marcato longmarcato pizzicato bartok collegno tremolo trill-m2 trill-M2 trill-m3 trill-M3
  fall rip`.
- `expect` (optional: `silent`, `ignored` or `unclear`) is what *Check articulations* hears for
  the value where that is known to be right (a patch that has no samples for it, or a sound the
  audio comparison can't judge but the pictures confirm). The check then counts it as passed,
  and a last check that found exactly these is shown as passed without checking again.
- `modifiers` are variants: `muted harmonics sulpont sultasto flautando cuivre sulg sulc bellsup
  pdlt multitongue performance`. When no variant matches, the plain articulation plays. Staff text sets them:
  "sul pont.", "sul tasto", "flautando", "cuivré", "sul G", "sul C", "bells up" ("campana in
  aria", "pavillons en l'air"), "près de la table" ("pdlt"), "double tongue" / "triple tongue",
  "performance" (a library's own legato patch: SSO's Performance patches; "non performance" ends it);
  "ord." ends them all.
- `partName` on an `Instrument` is a regular expression. That instrument is preferred for parts
  whose name matches it.
- For a keyswitch, `value` is the key's MIDI pitch. An `Instrument` can override `<Switch>` with
  its own `<Switch>` element inside it.
- **Extra patches.** An `Instrument` with `with="<another instrument's name>"` is a patch the
  parts of that instrument can also play (a legato patch, a single technique …). Each note plays
  the articulation that fits it best of all the part's patches; of equal fits, the one made for
  the technique (listed first). An extra patch is loaded only when the part's notation asks for
  one of its articulations and, hosted by MuseScore, once it is set up (*Check articulations ›
  Set up…*); until then the main patch plays those notes. A legato articulation's notes overlap
  the next note a little, as legato patches need. An articulation whose first technique is `legato`
  is a legato patch's own: slurred notes on it join by legato transitions (`legatoDelay`, below). One
  that plays slurs but lists another technique first (`techniques="long legato"`) attacks each
  slurred note anew (SSO's All techniques longs, which slurs play since 2026-10-06, the owner's
  choice; SSO's Performance patches play only under staff text "performance", since 2026-10-07).

  ```xml
  <Instrument name="Strings - Violins 1 - Legato" with="Violins 1">
    <Articulation name="Legato" value="20" techniques="legato"/>   <!-- slurred notes -->
  </Instrument>
  ```
- **Measured timing.** Optional attributes come from measuring the library (SSO's come from
  the owner's background timing run and plug-in extract, `tools/soundlibraries/sso_articulation_timing.json`
  and `sso_patch_measurements.json`, written by `gen_spitfire_sso.py`):
  - `legatoVelocity` on an `Articulation` (1-127): a legato transition's velocity, whatever the note's
    (Spitfire's Performance legato picks the transition by velocity: fast 85-127 "with accent", slow 85-127
    bowed). SSO: Violas - Performance 100, the other Performance patches none (measured per family, `gen_spitfire_sso.py` LEGATO_VELOCITY; on Violas - Performance, 2026-10-07: a run of slurred
    sixteenths arrives with an SD of 41 ms against the written times at 100, 49 ms at 64).
  - `attacks`, `transitions`, `reattack`, `reattackGap`, `overlap`, `legatoGap` on an `Articulation` of a legato patch that picks
    its sound by velocity and timing (`libmscore/performancetechnique.h`): `attacks="name:low-high:velocity …"`
    lists the attacks (a note after silence or a re-attack) by their velocity band and the velocity played,
    `transitions` the same for a note joined to the one before; `reattack` the velocity of a re-attack (Auto,
    right after a note of the patch), `reattackGap` (ms) how long before it the note before ends, `overlap` (ms)
    how long after a transition's note-on the note before ends, `legatoGap` (ms) the longest gap the patch still
    joins by transition (a note's own join in the Join lane: longer re-attacks). Names MuseScore knows: attacks `smooth`,
    `spiccato`, `accented`; transitions `portamento`, `fingered`, `bowed` (Note › Performance attack / transition
    in the Inspector; Auto: a slur's notes bowed, after a glissando portamento, an accent or marcato accented,
    else spiccato). SSO (`gen_spitfire_sso.py` performanceTechniques, the five string sections' Performance
    Legato): `attacks="smooth:1-9:5 spiccato:10-116:112 accented:117-127:122" transitions="portamento:1-19:5
    fingered:20-84:69 bowed:85-127:93" reattack="106" reattackGap="54" overlap="24" legatoGap="39"` (bands: Spitfire's article and
    measured on Violins 1 - Performance, as is legatoGap; velocities and times: the owner's own set, HANDOFF.md). The
    portamento's velocity is the middle of the band where the patch's PORT groups play alone: 5 (FING plays from 10 on
    Violins 1 / 2, Violas, Celli), the Basses' 10 (FING from 20) (2026-10-17, `sso_performance_layers.json`; it was 10
    everywhere, which also played FING). A bowed transition plays at `legatoVelocity` where the map has one.
  - `attackOnset="name:median[;pitch:ms,…] …"` on such an `Articulation`: an attack's own onset (ms after the note-on,
    measured as `onset`: the median over the measured notes, optionally by played pitch). With `[slurs] smoothStart`
    on, a note on that attack starts early by it (`[heldNotes] byPitch`: by pitch, else the median) instead of the
    articulation's `onset`. SSO (`gen_spitfire_sso.py` from `sso_smooth_onset.json`): the smooth attack of the five
    string sections' Performance Legato, e.g. Violas `attackOnset="smooth:236.2;50:198.3,59:259.5,…"`.
  - `avoid="keys:low-high:velocity …"` on such an `Articulation`: an attack (not a legato transition) at those keys
    (one key or `low-high`) with a velocity in the band plays at `velocity` instead, the nearest velocity measured
    clean: a faulty recording (a round robin with its own narrow tone). Not a note's own velocity; `[notes]
    avoidBadSamples` 0 turns it off. SSO (`gen_spitfire_sso.py` from `sso_bad_samples.json`): Celli - Performance
    `avoid="47-48:100-116:99"`.
  - `legatoDelay` on an `Articulation` (ms): a legato transition reaches its new pitch this long
    after its note-on (SSO's Performance patches: 60–690 ms, median 190). Either one number for
    every interval or the delay by interval, `interval:ms` pairs in semitones (the new note minus the
    one before; `legatoDelay="-12:200 -7:210 -5:240 … +7:300 +12:600"`): an interval between two listed
    ones takes the straight line between them, one beyond the widest the widest's (a chord before: its
    nearest note on the same patch). SSO's come from the legato grid (`sso_legato_grid.json`: 14
    intervals from -12 to +12, velocity changes nothing), for the string Performance patches from
    5 starting pitches (`sso_legato_grid_pitches.json`; +12 takes -12's). A slurred note that is a
    transition (its note before, on the same patch, is slurred into it) plays on its beat; the delay
    times only the glide of a transition bent to another tuning (`[tuning] bendAtArrival`, below). (The
    early start by this delay, `<Legato early>` and metaTag `soundLibraryLegatoEarly`, was removed
    2026-10-07; an older map's or score's is ignored.)
  - `peak` on an `Articulation` (ms): when a held note is loudest after its note-on (SSO: the median over the
    measured semitones of the onset check's mf perceived peak). With `[slurs] quick` a slurred note shorter than
    it plays the patch's tenuto short (1) or espressivo long (2) instead.
  - `heldOffset` on an `Articulation` of a velocity technique (dB, or `cc:dB` pairs by the written dynamic's CC,
    linear between, the ends' beyond): with `[levels] calibrated` 2 (option B') the note plays this much over the part's
    held Long matched at the same marking, real players' difference; without it the library's own loud-end difference.
    SSO (`gen_spitfire_sso.py` from `tools/soundlibraries/technique_offsets.json`): the five string sections' Spiccato
    and Short 0.5 `-3.4`, Short 1.0 `0.3`, Pizzicato e.g. Violins 1 `32:-7 112:-9`, the Basses' Bartók and Col Legno `0`.
  - `onset` on an `Articulation` (ms): a sustained note is heard this long after its note-on (its
    level 15 dB under the note's peak), one number or `pitch:ms` pairs by played MIDI pitch
    (`onset="55:65 68:55 78:65 89:40 97:80"`, linear between, the nearest end's beyond). A note that is
    not a legato transition (a lone held note, a slur's first note, any slurred note on an articulation
    that doesn't list `legato` first) starts early by it times `<Onset
    early>` percent; a chord by its notes' latest. As for transitions, the note just before on its
    track, when it plays on the same patch, loses at most a share of its length (none up to 125 ms,
    half from 250 ms), and it never starts before the chunk's or the repeat's start (a chunk of live
    playback doesn't end before a measure where a library part starts a note, up to twice its size).
    What ends on the same patch between the new and the written start ends at the new start (on a
    legato patch an overlap would play a transition instead of the note's attack), and the note's
    switch and the controllers sent at its tick move with it. SSO: 10-60 ms for most longs, 175-440 ms
    for sul tasto, flautando and harmonics (from the rest check's per-semitone full-level times by
    family; `gen_spitfire_sso.py`); since 2026-10-08 also every other technique the onset check measured (shorts,
    marcato, tremolos, trills, falls: measured pitches only, no family fit), not rips, effects, harp, keyboards or
    percussion.
  - (`legatoLevel` / `legatoLevelLong`, the level each transition arrived at, played by `[legato] levelBalance`: removed
    2026-10-07, ignored in an older map; the measurement stays in `sso_legato_levels.json`.)
  - `<Onset early="100"/>` (top level): that percent (SSO: 100). A score can set its own in *Mixer ›
    Advanced Options…* ("Held notes early by", metaTag `soundLibraryOnsetEarly`); 0 plays them on the beat.
  - `length` on an `Articulation` (seconds): how long its sample is (SSO's Short 0.5 / Short 1.0), for
    reference; with `from` (seconds): not chosen for a note meant to sound shorter than that (without a
    measured `from` a short is not skipped by length: the earlier 90 % of `length` had no source, removed
    2026-10-03; every SSO short with a `length` has a `from`): its written length times MS4's duration factor
    for its articulations (staccato 50 %, staccatissimo 25 %, tenuto 99 %, portato 74.5 %). SSO's
    `from` is measured: the meant length from which the short's sounding length (the last time it is
    within 10 dB of its peak; the note-off hardly cuts them) is closer to it than what would play
    otherwise (Spiccato for Short 0.5, Short 0.5 for Short 1.0): Violins 1 0.43 / 0.71 s (a staccato
    from 0.86 s written, a portato from 0.95 s), Violas 0.61 / 1.06, Basses 0.73 / 1.07.
  - `release` on an `Articulation` (ms): how long a sustained note rings after its note-off. A
    tuning copy (below) is retuned only after its notes' end plus the longer of `tail` and this.
    SSO's: the longest over the articulation's range (the rest check measured every semitone; neighbouring
    semitones can ring twice as long as the rest: Violins 1 - Performance 855 ms at its test pitch, 2180 at D4).
  - `bend` on an `Instrument` (cents): the patch bends its pitch this far either way at full pitch
    bend, linearly. Notes of other tunings on it are tuned by pitch bend (below).
  - `bendGlide` on an `Articulation`: `IOI:leave:arrive` triples (ms): after a note before IOI long (its start to
    the transition's), a legato transition's own pitch leaves the note before (a tenth of the way) leave ms and
    reaches the new note (nine tenths) arrive ms after its note-on; linear between the listed IOIs, the nearest
    one's beyond (SSO's five string sections: `sso_legato_glide.json`, `legato_glide/glide.py`, slurred runs of
    steps pitch-tracked). A transition bent to another tuning glides its bend then, with the transition; left
    out, from the arrival (`legatoDelay`) one cent a tick.
  - `bendSmoothing` on an `Articulation` with a `bendGlide` (ms): the patch smooths a pitch bend as a one-pole
    low-pass with this time constant (SSO's five string sections' Performance Legato: `sso_bend_smoothing.json`,
    `legato_glide/fit.py`, bend steps on a held note pitch-tracked). The glide is then sent so that the bend as
    heard reaches the tuning at the arrival: the note before's tuning until the leave, one value from the leave to
    the arrival (from where the heard bend is: (target - heard e) / (1 - e), e = exp(-(arrive - leave) / tau)),
    then the tuning. Left out: the glide is sent as the straight path itself.
  - `keyShift` on an `Instrument` (semitones, -48 .. 48, left out: 0): added to a note's MIDI pitch to get the key
    sent to the patch, for a patch whose keys sound off their number. SSO's timpani sound an octave under it (key 50
    = 72.8 Hz = D2; its keys 46 .. 74 all sound 12 semitones down), so it says `keyShift="12"` (the
    generator's `KEY_SHIFT`): a written D2 (38) goes on key 50. Only the key sent shifts (note-on and note-off, so
    Live's clips and the plain set too); the keyswitches, pitch tables, tunings and bends stay by the written pitch.
    A correctness fix of the map, not a playback adjustment.
  - `plainShift` on an `Instrument` (ms, 0 .. 1000, left out: not measured): with `[slurs] plainPerformance` a part
    played plain on this Performance patch has every event of the patch sent this much early (the median heard -
    written latency of plain notes, measured in context: `tools/soundlibraries/sso_plain_performance_shift.json`;
    SSO's five string sections' Performance patches, e.g. Violas 111.0). Left out: the notes play as written and the playback audit lists the patch
    (docs/PLAYBACK_SETTINGS.md › Plain Performance).
  - `mono` on an `Instrument` (`"1"`, left out: not): a monophonic patch, one line per plug-in slot (SSO's Performance
    patches: a note-on while a note is held is a legato transition that ends it). MuseScore gives a chord's other
    notes, and any note overlapping another, copies of the patch of their own ("voice lanes": `SoundLib::lanes`;
    docs/PLAYBACK_SETTINGS.md › Chords on monophonic patches), as it gives tunings copies. The SSO map's generator
    marks every "... - Performance" patch (`MONO`).
  - `speedCC` and `speedScript` on an `Instrument` (a controller 1-127 and a script value name; both or neither): the
    controller the patch's own script writes at each legato note-on from the note-on spacing, and the script value that
    sets which (0: none). With `[slurs] speedFromWritten` the patch's state gets `speedScript` = 0 and MuseScore sends the
    controller itself, by the same formula from the written spacing (docs/PLAYBACK_SETTINGS.md › Speed from written).
    SSO's five string-section Performance patches: `speedCC="119" speedScript="$vfm12"` (the script's "Leg. Speed CC",
    read from the patches' saved values: `tools/soundlibraries/sso_performance_layers.json`).
  - `maxVelocity` on an `Instrument` (1-127, left out: 127): the loudest velocity MuseScore sends to the patch;
    a louder note-on is sent at this. For a patch whose own script turns the top velocity into a different
    sound: SSO's percussion script (Kickstart) plays velocity 127 as a roll where its option "Roll on high
    vel." is ticked, which 13 one-drum metal patches ship with, so those say `maxVelocity="126"` (the
    generator's `ROLL_ON_HIGH_VELOCITY`). A correctness fix, not a playback adjustment: no preset turns it off.
    Applied to every library note-on after levels and delays (`MidiRenderer::libraryMaxVelocity`), so Live's
    clips and the plain set carry it.
  - `sectionLevel` on an `Instrument` (dB): with playback setting `[levels] sectionBalance`, every patch of
    the section (the main patch and its `with` patches, which take its value) plays this much louder, by
    its volume (`libmscore/contextlevel.h`). SSO's five string sections: a real orchestra's balance at mp
    (Weinzierl et al. 2018's sound power, SSO's section sizes) against SSO's measured levels at CC1 64,
    centred (the owner's M, 2026-10-11; since 2026-10-14 with mp where `realPlay` places it). SSO's woodwinds' and
    brass' patches the database has (the owner, 2026-10-14: from the solo data until ensemble data exists): the same
    on the strings' scale. All from `tools/soundlibraries/section_balance_winds.py` through `gen_spitfire_sso.py`.
  - `sectionLevel2` on an `Instrument` (dB): the same, played by `[levels] sectionBalance` 2 (Recommended and the default
    since 2026-10-14) instead of `sectionLevel`; a section without it plays its `sectionLevel`. SSO: the real side from
    Clark & Luce 1965's orchestral scales at mp as sound power (TU Berlin's directivity), against the same SSO levels
    (`tools/soundlibraries/section_balance_cl.py` -> `sso_cl_section_balance.json` -> `gen_spitfire_sso.py`).
  - `realSpan`, `longLevel` and `realPlay` on an `Instrument` (the main patch; its `with` patches take them): with
    playback setting `[levels] realRange` each note plays, by CC11 and the patches' volumes, a real player's level at
    its written dynamic less what the patch's held Long already changes with CC1 (`libmscore/contextlevel.h` › Real
    range). `realSpan="pitch:dB …"`: one player's softest-to-loudest single-note span in dB per pitch (Weinzierl et
    al. 2018, smoothed; the player's ppp / fff); `longLevel="cc/pitch:dB …"`: the held Long's level at CC1 cc less at 64
    (measured); `realPlay="a b"`: written pp and ff as fractions of the span above its soft end (the Meyer rule, the
    owner 2026-10-14: J. Meyer 2009, chapter 3, rapid scales against single-note extremes; p … f in equal steps
    between); left out: rule D (1/7, 6/7, seven equal steps). SSO's from `tools/soundlibraries/real_range.py`; the bass
    clarinet's and bass trombones' `realPlay` are the clarinet's and tenor trombone's, an assumption ("plays like its
    family", the owner 2026-10-14), not a measurement (Meyer gives them no levels). `realSteps="S"` (dB; `realRange`
    2, Clark & Luce spans, the default since 2026-10-14): the instrument's written pp -> ff as orchestral players play
    it, the markings in equal steps of S / 5 around mp, ppp / fff one step more, in place of `realPlay`'s fractions
    (Clark & Luce 1965, Table I; the bass clarinet, contrabassoon and bass trombones their family's, an assumption).
    Must be above 0.
  - `micLevels` and `micDefault` on an `Instrument`, `<Mics fader mix>` (top level): with playback setting
    `[levels] micCompensation`, a part whose mic controllers (the plug-in parameters `Mic 1-5 level`, `Mic Mix
    Distance`: their automation lanes) give another mix than the patch's default plays as loud as the
    default mix, by its patches' volumes (`libmscore/contextlevel.h` › Mics). `micLevels="Close:dB Tree:dB Ambient:dB
    Outrigger:dB Leader:dB"`: each mic (Mic 1-5) alone at its full fader against the default mix; `micDefault="Tree:127"`:
    the default faders 0-127 (a mic not listed 0); a `with` patch without its own takes its main's. `<Mics
    fader="value:dB …" mix="value:close/tree/ambient …"/>`: a mic's level at a fader value against 127 (0 silent; linear
    in dB between the values, in amplitude below the first) and what Mic Mix Distance sets Close, Tree and Ambient to,
    each against its full fader (`-` silent; linear in amplitude between the values); with Mic Mix Distance set the mic
    values are not played (SSO's script overrides them), Outrigger and Leader stay at their default. The level of a mix: the mics' powers added. SSO's measured on the VM (`tools/soundlibraries/mic_levels.py`
    -> `sso_mic_levels.json` -> `gen_spitfire_sso.py`).

  ```xml
  <Onset early="100"/>
  <Instrument name="Violins 1 - Performance" with="Violins 1" bend="99.1">
    <Articulation name="Legato" value="20" techniques="legato long" release="885"
                  legatoDelay="-12:200 -7:210 -5:240 -4:190 -3:210 -2:300 -1:350 +1:220 +2:240 +3:230 +4:300 +5:300 +7:300 +12:600"
                  onset="45"/>
  </Instrument>
  ```
- `keyShift` on an `Articulation` (semitones, -48 .. 48, left out: 0): added to the key of a note on that articulation,
  on top of its `Instrument`'s. Ample's natural harmonic (C#0 held) sounds 12 semitones over the key, so the map's
  Natural Harmonic says `keyShift="-12"`: a harmonic note (diamond notehead, "harm.") sounding E4 goes on key E3.
- `<Dynamics cc="-1"/>`: dynamics by note velocity only, no dynamics controller (Ample: CC1 is its vibrato).
- `<Guitar>` in an `Instrument` (branch `ample-phase2`, 2026-10-20; `SoundLib::Guitar`): the keys and rules guitar notation
  plays with (`libmscore/guitarplayback.cpp`, the renderer's last library pass). Attributes (keys are MIDI numbers, -1 or
  left out: none): `stringKeys` (strings 1..6, high to low; tapped at `stringVelocity` before a note with a string:
  tablature, or a string number 1-6), `harmonic` / `harmonicShift` (artificial harmonics: a fretted note with a diamond
  note `artificial="frets:semitones …"` above it sounds the stopped note + semitones, played as the natural harmonic),
  `palmMute`, `slide` (scoop / plop before the note, fall / doit `slideOut` ms before its end), `legatoSlide`
  (glissando line) and `hammer` (a legato slur, or text H / P, between two single notes): the first note held
  `overlap` ms into the second; the strummer: `strumMode` with `strumOn` / `strumOff` velocities, `detect` at
  `detectVelocity`, chord keys from `chordLow`, `strumKeys="down up downMuted upMuted"` at `strumVelocity`, `softStrum`
  and `rollGap` ms (`[guitar] softStrums` 1), `rollKeys` (six single-string keys in strum mode, strings 6..1: `[guitar]
  softStrums` 2's roll, the chord keys held) and `softRoll="velocity:ms …"` (the roll's velocity, the chord's held within
  the pairs' range, and its keys' spacing, linear between); `sampleSet` (the state controller whose value picks a `<StrumLevel
  set levels="velocity:CC7 …">`: the CC7 a strum plays at, log-linear between) and `capo` / `capoMax` (the capo
  parameter's controller id: a capo staff text sends it, each note's key sounding - capo) and `capoLead` (ms a change
  goes before the first note under it, after the previous note-on: Ample applies it from its next process block; a
  capo at the score's start plays the score that much later, the parameter at 0); `pinch="partial velocity
  delayMs lengthMs"` (`[guitar] pinchHarmonics`); `fxLength` ms and `<Fx text="regex" key>` (staff text, the whole text,
  case-insensitive, that plays an FX noise key). A bad value refuses the map.
- **Ample Guitar M** (`Ample Guitar M.xml`, generated: `python3 tools/soundlibraries/gen_ample.py <dump>`, the dump
  being amphost's parameter list of AGM 4.1.0, kept outside the repository): the steel-string guitars play it,
  hosted as `AGM.vst3`, an instance per part; every one of its 124 parameters is a lane (by its title), with CC7
  (Volume: the same gain as M-Master Vol, 64 = unity; hosted and in Live played as M-Master Vol, `hosted=`), CC1 (Vibrato), CC64 (Hold Pedal) and the sample set
  (Finger / Pick / Strum); keyswitches (Ample calls MIDI 60 "C3": its C0 is MIDI 24) Sustain 24, Natural Harmonic 25,
  Palm Mute 26 (staff text "muted", the palm mute line), Slide In / Out 27, Legato Slide 28, Hammer-On / Pull-Off 29,
  Slide Guitar 30 (the last four listed, not yet chosen by the notation); velocity 127 is Pop, so notes stop at 126.
  The map lists Ample's other keys (strings, positions, strummer, FX) as a comment.
- **Microtones.** `<Tuning method="varispeed" [tolerance="…" tail="…" maxLanes="…"]/>`: a plug-in
  that ignores a note's tuning (Kontakt) plays a part's notes on copies of the patch ("lanes"),
  one tuning each. Where the patch has `bend`, a lane's tuning within the range is played by the
  patch's own pitch bend (sent on the lane's channel before each note; a slurred note's glides from the note
  before's with its transition, `bendGlide` above), otherwise by playing the copy faster or slower (varispeed, hosted only), which also
  plays the plug-in's own timing (envelopes, scripts, effects) a little faster or slower (3 % for a quarter tone). Over MIDI out the bends go to
  the host with the notes. `tolerance` (cents): a note this close to a lane's tuning shares it
  (left out: half the smallest gap between two accidentals' values, 0.083); `tail` (seconds): how long a
  lane rings before it can be retuned (or the note's `release`, if longer; left out: twice the note's
  `release`, its ring to 60 dB under); `maxLanes`: copies per patch at most (left out: as many as the free
  memory holds at 245 MB a copy, shared by the score's parts). A score can set its own in *View › Sound Library…*.
- **Percussion kits.** An `Instrument` with `kit="1"` serves MuseScore's unpitched percussion
  and has no patch of its own. Its extra patches say which key plays each MuseScore drum sound
  (the note's pitch in the drumset): `<Drum pitch="38" key="62" name="Snare hit"/>`, with
  optional `velocity` (fixed) and `ids` (only for these MuseScore instruments). A sound no
  patch has plays on the built-in synthesizer. A `<Drum>` without `pitch` is a key no MuseScore
  sound plays: listed (and checked) so the map has every key of the patch, never chosen.
  `default="off"` marks a technique the patch has switched off until it is given a key
  (Spitfire's Kickstart): a setup made elsewhere, in a DAW, must switch it on too. Without `key` (and without
  `pitch`) it names a technique that is off at the patch's defaults and has no key at all: SSO's map lists every
  one (reference; never played, not checked) so the map is complete. `technique="roll"` marks the sound's roll key: a
  note with a single-note tremolo or a buzz roll plays it once, held for the note (a crescendo
  over it swells through the dynamics controller); a sound with no roll key plays the tremolo
  as repeated hits. `keyScan="1"` makes *Check articulations* play
  every key of the patch and picture what each one plays, to find its `<Drum>` entries, and
  its keyswitches (a silent key that leaves the window changed): a patch with no CC switching
  (Spitfire's "Kickstart" percussion) takes `<Switch type="keyswitch"/>` and its articulations'
  keys as values.

- **Controllers.** A `<Controller>` is something of the library MuseScore sets per part: a MIDI
  controller (`cc`, 0–119) or a parameter of the hosted plug-in, found by its title (`param`,
  plug-in hosting only). Values are 0–127 (a parameter gets value / 127). `default` is what a
  part plays it at unless it has a value of its own; without it the patch keeps its own
  setting. `patchDefault` (a `param` controller only; 0–1, the plug-in's normalized value) is
  that own setting as the library ships it, measured once in the real plug-in with MuseScore's
  setup loaded and settled and nothing set: MuseScore sends nothing for it, it is what an
  unautomated lane shows (its dashed line) for a patch that never loaded on this computer; once
  the patch loads, the plug-in's live value takes over, and a value that differs is logged in
  the setups folder's `load times.log` (the map then needs measuring again). `<Text>` children are staff texts (the whole text, as a regular expression,
  case-insensitive) that change a MIDI controller from their note on. A `<Controller>` at the
  top is for every instrument; one inside an `Instrument` replaces the top one of the same `id`
  (or adds to them).

  ```xml
  <Controller id="vibrato" name="Vibrato" cc="21" default="64">
    <Text match="senza vib\.?" value="0"/>
    <Text match="molto vib\.?" value="127"/>
  </Controller>
  <Controller id="release" name="Release" param="Release" patchDefault="0.496"/>
  ```

  `steps` and `display` say how the plug-in shows a value (the lane editor shows that text instead of 0-127; nothing
  is sent by them): `steps="N"`, the parameter's discrete positions k / N (a toggle 1, Ample's capo 11);
  `display="v:text …"`, the plug-in's text at lane values 0-1, ascending, no spaces in a text (a numeric text is
  read linearly between two entries, a stepped controller shows its nearest position's).

  `hosted="id"` on a MIDI controller names a `param` controller of the same patch that plays it for the hosted
  plug-in and in Live: every event of the CC on the patch's route (its lane, the part's value, the guitar pass's
  strum levels) goes as that parameter at the same tick, value CC / 127; MIDI out keeps the CC (an external
  plug-in's MIDI learn), a MIDI file writes the CC. For a CC Live can't carry (CC7: its clips have no carrier, the
  plain set leaves it to Live's fader) when the plug-in has the same control as a parameter: Ample's
  `<Controller id="volume" cc="7" hosted="m-master-vol"/>` (measured bit-identical to CC7). A name that is no
  parameter controller of the patch refuses the map.

  **State settings** (a non-Kontakt plug-in hosted in MuseScore): `<Controller id name state="Name" values="a b c">`
  is a setting the plug-in keeps only in its state, no parameter, no key (Ample's sample set: `state="DBName"
  values="AGMFg AGMPk AGMSt"`). Its lane's value at the score's start picks `values[round(v × (n − 1))]` (a lane's
  first point: the state can't change while playing), written into the part's state as the part loads
  (`audio/vst3/pluginstate.h`: `<Name Value="…"/>`, or `state="Parameter:Title"` for `<Parameter Name="Title"
  Value="…"/>`); a changed value loads the part again at the next play or export. Without a lane the plug-in keeps its
  own (`patchDefault`, 0-1, is that value on the lane's scale). A patch with state settings and no setup file starts
  from the plug-in's own default state. Not for Kontakt (its script values: `setup`).
  - A number instead of a list: `range="min max"` (and `decimals="n"`, default 0) writes `min + v × (max − min)`
    rounded, as the plug-in shows it ("61", "-23.1": no trailing zeros); `values` and `range` exclude each other. Give
    it `display="0:min 1:max"` (and `steps` = max − min for whole numbers): a stepped controller listing only its two
    ends reads linearly between them.
  - State names (`pluginstate.h`): `Name` = `<Name Value>`; `Parameter:Title` = `<Parameter Name="Title" Value>`;
    `Name[Key=k]` = the `Value` of the first `<Name>` whose `Key` is `k` (Ample's velocity layers `Layer[ID=1]`);
    `Name[Key=k]@Attr` = that element's attribute `Attr`, the element added (after the last `<Name>`) when the state has
    none.
  - A display text can't hold a space (the list is split at spaces): the generator writes a no-break space (U+00A0).
  - `<State name="…" value="…"/>` (top level): a value written into every instance's state, whatever the part sets
    (`Library::fixedState`; the part's state settings go on top). Ample: its MIDI-learn defaults, `Control[ID=12]@Parameter`
    = `M-Master Vol` (CC7, ID = 5 + CC) and the other six, so that a preset that dropped one can't silence the CC7
    volume lane or the guitar pass's strum levels. Live's set gets the same state (`SoundLibraryHost::setupState`).
  - `<PluginSettings file="…" name="…" bad="a b" text="…"/>` (top level): a plug-in's global settings file (a JUCE
    PropertiesFile, `file` under the user's Documents folder), read as an instance's state is made; `name` at a `bad`
    value logs `text` (load times.log, the console) and shows it in the status bar, once per session. Read only. Ample:
    `Ample Sound/AGM/Configs/AGM_info.xml`, `midiGuitarMode` `true` (keys ≥ 62 go silent on channel 1 in every
    instance).
  - `<Plugin files="…" backgroundLoad="s"/>`: the plug-in goes on loading on threads of its own after its state is set
    (Ample: the state's sample set, ~0.6 s of one core). An offline render's instances (the verification, live
    equivalence, audio export: `SoundLibraryExport::loadInstances`) each wait, at most `s` seconds, until every thread
    the instance started has ended or gone quiet and the process's memory has stopped growing
    (`audio/vst3/backgroundload.h`; logged in load times.log; `MS_BACKGROUND_LOAD_WAIT=<s>` overrides, 0 turns it off).
    Without it, Ample's renders differed from run to run (the Ample check of build 455). Kontakt needs none: the
    verification plays each patch a test note until it sounds.
  - A changed state value loads the part again at the next sync (play, export): the slot keeps the written values as
    one text (`PartControllers::valuesKey`), so any number of state lanes changed before it give one load, and an edit
    that leaves every written text as it was (a later point, a value rounding to the same number) none.

  ```xml
  <Controller id="m-capo" name="M-Capo" param="M-Capo" patchDefault="0" steps="11" display="0:0 0.0909:1 … 1:11"/>
  <Controller id="sample-set" name="Sample set" state="DBName" values="AGMFg AGMPk AGMSt" patchDefault="0"
              steps="2" display="0:Finger 0.5:Pick 1:Strum"/>
  <Controller id="vel-sensitivity" name="Velocity sensitivity" state="Parameter:Vel Sensitivity" range="0 100"
              patchDefault="1" steps="100" display="0:0 1:100"/>
  <Controller id="fx-reverb-send" name="Reverb send (dB)" state="Parameter:FX-Reverb-Send" range="-60 0" decimals="1"
              patchDefault="0.8" display="0:-60.0 1:0.0"/>
  <Controller id="velocity-layer-1" name="Velocity layer 1 threshold" state="Layer[ID=1]" range="1 127" …/>
  <State name="Control[ID=12]@Parameter" value="M-Master Vol"/>
  ```

  A part's controllers are set by automation lanes only (Continuous View, under the part's header:
  one lane per controller of its patches, by `id`, so a lane stays with the part when the library
  changes). A lane without points plays the `default` (else the patch's own value) and is drawn as
  a dashed line; a lane with one value from the score's start is the controller's setting (in a
  Live set Create / Update Live Set write, the parameter's plain value in Live's panel, not
  automation; a value turned there comes back as such a lane). Scores
  saved before 2026-10-15 kept *Controllers…* values in the metaTag `partControllers`: they are
  read as such lanes.

**The Spitfire UACC values have not been checked against the library.** They come from a
community articulation bank for Spitfire Symphony Orchestra
([Reaticulate](https://github.com/jtackaberry/reaticulate), `userbanks/Spitfire`). Each Spitfire
patch shows the UACC number of every articulation. If one differs, copy the map, correct the
value and choose your copy with the *…* button next to the library.
