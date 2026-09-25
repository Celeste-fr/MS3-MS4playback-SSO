# Sound libraries (external MIDI)

MuseScore can play chosen parts of a score through an external sample library, such as
Spitfire Symphony Orchestra in Kontakt, instead of its built-in synthesizer. It picks the
library's articulation for each note from the notation. For example:

| Notation | Articulation |
|---|---|
| no mark | Long |
| staccato / staccatissimo | Short / Spiccato |
| accent or marcato on a short note | Marcato |
| accent or marcato on a long note | Long marcato attack, where the library has one |
| tenuto on a short note | Tenuto (brass, woodwinds) |
| single-note tremolo, "trem.", "flz." | Tremolo / flutter (the note is played once, not repeated) |
| trill (symbol or line) | Trill sample at the written interval (minor or major 2nd …), when the library has one |
| fall, scoop | Fall, rip |
| "pizz." / "arco", snap pizzicato, "col legno" | Pizzicato, Bartók pizzicato, col legno |
| "con sord." / "senza sord.", mute symbols | Con sordino (muted, stopped) variants |
| "sul pont.", "sul tasto", "flautando", "cuivré", "ord." | Those variants, where the library has them |
| harmonic notehead (diamond), "harm." | Harmonics |

MuseScore sends each note's dynamics as note velocity plus a continuous dynamics controller
(Spitfire: CC1). It drives the library with articulation switches (Spitfire: UACC on CC32,
keyswitches or program changes for other libraries).

This only works with the **MuseScore 4** dynamics method (View › Synthesizer › Dynamics). That
is the default.

## Setup on Windows with Spitfire Symphony Orchestra

1. **Virtual MIDI cable.** Install [loopMIDI](https://www.tobias-erichsen.de/software/loopmidi.html)
   and create one port for every 16 library parts. A full orchestra usually needs 2.
2. **Host.** Start Kontakt standalone (or a DAW with Kontakt). Under *Options › MIDI*, enable the
   loopMIDI ports as inputs and assign them to Kontakt ports A, B, C, D in the same order.
3. **MuseScore.** Open *Edit › Preferences › I/O*:
   - PortAudio: choose the first loopMIDI port as *MIDI output* and the next ones as *MIDI output B, C, D*.
   - Sound library: choose *Spitfire Symphony Orchestra*.
   - Click *Show routing…* with your score open. For each part, it lists the patch to load, the
     port (A–D) and the MIDI channel (1–16).
4. **Patches.** Load each listed patch in Kontakt (the *All techniques* patch of that section).
   Set it to the port and channel shown. In the patch's articulation settings, set switching to
   **UACC**.
5. Play. Parts the library has no patch for (piano, percussion …) still play on MuseScore's
   built-in synthesizer.

To line up the library's sound with the built-in synthesizer, use *MIDI output latency* in the
same preferences section.

Notes:

- Audio export (WAV, MP3 …) cannot record the external library. Exported audio plays those
  parts with the built-in synthesizer. To record the library, use *File › Export › MIDI* and
  render the MIDI file in your DAW. The exported MIDI file includes the articulation switches
  and dynamics.
- Solo and section patches are picked from the MuseScore instrument (Violin → Solo Violin 1,
  Violins → Violins 1). "a2" / "a6" patches are picked from the part name (for example,
  "Flutes 1.2", "Horns a4"). "Violins II" gets the Violins 2 patch.
- Mixer volume and pan don't reach the external library. Use Kontakt's own mixer.

## The map files

A map lists the library's instruments, the MuseScore instrument ids each one serves, and the
switch value of each articulation. For example:

```xml
<SoundLibrary name="Spitfire Symphony Orchestra">
  <Switch type="cc" number="32"/>            <!-- or type="keyswitch" / type="program" -->
  <Dynamics cc="1" expression="127"/>        <!-- dynamics controller; CC11 is set to 127 -->
  <Instrument name="Violins 1" ids="violins">
    <Articulation name="Long" value="1" techniques="long legato"/>
    <Articulation name="Long CS" value="7" techniques="long legato" modifiers="muted"/>
    <Articulation name="Spiccato" value="42" techniques="short spiccato staccatissimo"/>
    …
  </Instrument>
</SoundLibrary>
```

- `techniques` lists what the articulation can play: `long legato short staccatissimo spiccato
  tenuto marcato longmarcato pizzicato bartok collegno tremolo trill-m2 trill-M2 trill-m3 trill-M3
  fall rip`.
- `modifiers` are variants: `muted harmonics sulpont sultasto flautando cuivre`. When no variant
  matches, the plain articulation plays.
- `partName` on an `Instrument` is a regular expression. That instrument is preferred for parts
  whose name matches it.
- For a keyswitch, `value` is the key's MIDI pitch. An `Instrument` can override `<Switch>` with
  its own `<Switch>` element inside it.

**The Spitfire UACC values have not been checked against the library.** They come from a
community articulation bank for Spitfire Symphony Orchestra
([Reaticulate](https://github.com/jtackaberry/reaticulate), `userbanks/Spitfire`). Each Spitfire
patch shows the UACC number of every articulation. If one differs, copy the map, correct the
value and choose your copy with the *…* button next to the library.
