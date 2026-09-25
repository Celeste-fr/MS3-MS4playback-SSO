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

## Setup on Windows with Spitfire Symphony Orchestra (plug-in in MuseScore)

MuseScore hosts the library's plug-in itself (Kontakt, VST 3). There is no separate host, no
virtual MIDI cable, and audio export includes the library.

1. In *Edit › Preferences › I/O › Sound library*, choose *Spitfire Symphony Orchestra* and set
   *Play through* to *Its plug-in, in MuseScore*. MuseScore finds *Kontakt 8* or *Kontakt 7*
   in `C:\Program Files\Common Files\VST3` on its own. If yours is somewhere else, choose
   its `.vst3` with the *…* button next to *Plug-in*.
2. Open a score and choose *View › Sound Library…*. The dialog lists each part with the
   library patch it plays. For every patch marked *Not set up yet*:
   - Click *Show*. Kontakt opens with an empty rack.
   - Load that SSO patch (the one with all the section's articulations). Set its articulation
     switching to **UACC** and its MIDI channel to 1 (or omni).
   - Close the window. MuseScore keeps this setup for the instrument and loads it by itself in
     every score from then on. To keep later changes, use *Save setup*.
3. Play, or export audio (File › Export, or `mscore -o score.wav score.mscz`).

Each library part gets its own plug-in instance. The first playback of a score loads them all,
and the samples take a moment. The library's audio is mixed in after MuseScore's reverb,
because SSO brings its own room. Setups are stored in MuseScore's data folder under
`soundlibraries/<library>/<patch>.vst3state`.

### Checking the map against the plug-in

*View › Sound Library…* › *Check articulations…* lists every patch of the library.

1. For each patch not set up yet, click *Set up…*, load the patch in Kontakt, set it to
   **UACC & UI only** and close the window. This is the same setup playback uses, so it is
   only done once.
2. Click *Check* and leave the computer alone. Kontakt's window has to stay visible. It takes
   about 15 seconds per patch.
3. MuseScore opens the folder `Documents/MuseScore Sound Library Check`. Hand back the
   `.zip` it made.

MuseScore remembers each patch's last check (the *Last check* column). A patch is ticked for
checking only when it was never checked, when its setup or its map entry changed since, or
when its last check couldn't run. *Tick what needs checking* ticks just those; tick others by
hand to check them anyway.

- *Scan every value* also tries all 128 UACC values. It finds articulations the patch has and
  the map lacks, and map values the patch doesn't have (SSO shows "None" for them). It takes
  about a minute more per patch.
- *Add a patch…* adds a patch the map lacks (e.g. *Trombones a5*). Set it up like the others.
  Its check is always a scan, so its articulations can be added to the map from the results.

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
- `modifiers` are variants: `muted harmonics sulpont sultasto flautando cuivre sulg sulc bellsup
  pdlt multitongue`. When no variant matches, the plain articulation plays. Staff text sets them:
  "sul pont.", "sul tasto", "flautando", "cuivré", "sul G", "sul C", "bells up" ("campana in
  aria", "pavillons en l'air"), "près de la table" ("pdlt"), "double tongue" / "triple tongue";
  "ord." ends them all.
- `partName` on an `Instrument` is a regular expression. That instrument is preferred for parts
  whose name matches it.
- For a keyswitch, `value` is the key's MIDI pitch. An `Instrument` can override `<Switch>` with
  its own `<Switch>` element inside it.

**The Spitfire UACC values have not been checked against the library.** They come from a
community articulation bank for Spitfire Symphony Orchestra
([Reaticulate](https://github.com/jtackaberry/reaticulate), `userbanks/Spitfire`). Each Spitfire
patch shows the UACC number of every articulation. If one differs, copy the map, correct the
value and choose your copy with the *…* button next to the library.
