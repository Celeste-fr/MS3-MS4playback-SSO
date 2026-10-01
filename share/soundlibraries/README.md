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
  `<Articulation name value/>` children (no techniques: for reference). See `audio/vst3/kontaktsetup.h` for how a setup is made.
- `techniques` lists what the articulation can play (an empty list: no notation asks for it; it
  is listed for reference and checked by *Check articulations*, but never chosen): `long legato short staccatissimo spiccato
  tenuto marcato longmarcato pizzicato bartok collegno tremolo trill-m2 trill-M2 trill-m3 trill-M3
  fall rip`.
- `expect` (optional: `silent`, `ignored` or `unclear`) is what *Check articulations* hears for
  the value where that is known to be right (a patch that has no samples for it, or a sound the
  audio comparison can't judge but the pictures confirm). The check then counts it as passed,
  and a last check that found exactly these is shown as passed without checking again.
- `modifiers` are variants: `muted harmonics sulpont sultasto flautando cuivre sulg sulc bellsup
  pdlt multitongue`. When no variant matches, the plain articulation plays. Staff text sets them:
  "sul pont.", "sul tasto", "flautando", "cuivré", "sul G", "sul C", "bells up" ("campana in
  aria", "pavillons en l'air"), "près de la table" ("pdlt"), "double tongue" / "triple tongue";
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
  the next note a little, as legato patches need.

  ```xml
  <Instrument name="Violins 1 - Performance" with="Violins 1">
    <Articulation name="Legato" value="20" techniques="legato"/>   <!-- slurred notes -->
  </Instrument>
  ```
- **Measured timing.** Three optional attributes come from measuring the library (SSO's come from
  the owner's background timing run and plug-in extract, `tools/soundlibraries/sso_articulation_timing.json`
  and `sso_patch_measurements.json`, written by `gen_spitfire_sso.py`):
  - `legatoDelay` on an `Articulation` (ms): a legato transition reaches its new pitch this long
    after its note-on (SSO's Performance patches: 60–690 ms, median 190). Either one number for
    every interval or the delay by interval, `interval:ms` pairs in semitones (the new note minus the
    one before; `legatoDelay="-12:200 -7:210 -5:240 … +7:300 +12:600"`): an interval between two listed
    ones takes the straight line between them, one beyond the widest the widest's (a chord before: its
    nearest note on the same patch). SSO's come from the legato grid (`sso_legato_grid.json`: 14
    intervals from -12 to +12, velocity changes nothing). A slurred note that is a
    transition (its note before, on the same patch, is slurred into it) starts early by this times
    `<Legato early>` percent, so the new pitch lands near the beat. The note before loses at most
    a share of its length: none up to 125 ms, rising linearly to half at 250 ms and longer (fast
    runs, where SSO's transitions are faster, stay even); it is also capped at the chunk's or the
    repeat's start; its note-off, the
    controllers and the switches stay where they were. The first note of a slur, the note after
    it and a repeated key are not moved.
  - `<Legato early="100"/>` (top level): that percent (SSO: 100, measured: the new pitch then fully arrives a
    median 40 ms after the beat). A score can set its own in
    *Mixer › Advanced Options…* ("Legato transitions early by", metaTag `soundLibraryLegatoEarly`);
    0 plays transitions on the beat.
  - `release` on an `Articulation` (ms): how long a sustained note rings after its note-off. A
    tuning copy (below) is retuned only after its notes' end plus the longer of `tail` and this.
  - `bend` on an `Instrument` (cents): the patch bends its pitch this far either way at full pitch
    bend, linearly. Notes of other tunings on it are tuned by pitch bend (below).

  ```xml
  <Legato early="100"/>
  <Instrument name="Violins 1 - Performance" with="Violins 1" bend="99.1">
    <Articulation name="Legato" value="20" techniques="legato long" release="885"
                  legatoDelay="-12:200 -7:210 -5:240 -4:190 -3:210 -2:300 -1:350 +1:220 +2:240 +3:230 +4:300 +5:300 +7:300 +12:600"/>
  </Instrument>
  ```
- **Microtones.** `<Tuning method="varispeed" tolerance="0.5" tail="1.5" maxLanes="4"/>`: a plug-in
  that ignores a note's tuning (Kontakt) plays a part's notes on copies of the patch ("lanes"),
  one tuning each. Where the patch has `bend`, a lane's tuning within the range is played by the
  patch's own pitch bend (sent on the lane's channel before each note, gliding 30 ms for a slurred
  note), otherwise by playing the copy faster or slower (varispeed, hosted only), which also
  plays the plug-in's own timing (envelopes, scripts, effects) a little faster or slower (3 % for a quarter tone). Over MIDI out the bends go to
  the host with the notes. `tolerance` (cents): a note this close to a lane's tuning shares it;
  `tail` (seconds): how long a lane rings before it can be retuned (or the note's `release`, if
  longer); `maxLanes`: copies per patch at most. A score can set its own in *View › Sound Library…*.
- **Percussion kits.** An `Instrument` with `kit="1"` serves MuseScore's unpitched percussion
  and has no patch of its own. Its extra patches say which key plays each MuseScore drum sound
  (the note's pitch in the drumset): `<Drum pitch="38" key="62" name="Snare hit"/>`, with
  optional `velocity` (fixed) and `ids` (only for these MuseScore instruments). A sound no
  patch has plays on the built-in synthesizer. A `<Drum>` without `pitch` is a key no MuseScore
  sound plays: listed (and checked) so the map has every key of the patch, never chosen.
  `default="off"` marks a technique the patch has switched off until it is given a key
  (Spitfire's Kickstart): a setup made elsewhere, in a DAW, must switch it on too. (SSO's map leaves such
  keys out: MuseScore sets patches up at the library's defaults.) `technique="roll"` marks the sound's roll key: a
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
  setting. `<Text>` children are staff texts (the whole text, as a regular expression,
  case-insensitive) that change a MIDI controller from their note on. A `<Controller>` at the
  top is for every instrument; one inside an `Instrument` replaces the top one of the same `id`
  (or adds to them).

  ```xml
  <Controller id="vibrato" name="Vibrato" cc="21" default="64">
    <Text match="senza vib\.?" value="0"/>
    <Text match="molto vib\.?" value="127"/>
  </Controller>
  <Controller id="release" name="Release" param="Release"/>
  ```

  *View › Sound Library…* › *Controllers…* sets a part's values. They are kept in the score
  (as the metaTag `partControllers`, which MuseScore 3.6 keeps) by controller `id`, so a value
  stays with the part when the library changes.

**The Spitfire UACC values have not been checked against the library.** They come from a
community articulation bank for Spitfire Symphony Orchestra
([Reaticulate](https://github.com/jtackaberry/reaticulate), `userbanks/Spitfire`). Each Spitfire
patch shows the UACC number of every articulation. If one differs, copy the map, correct the
value and choose your copy with the *…* button next to the library.
