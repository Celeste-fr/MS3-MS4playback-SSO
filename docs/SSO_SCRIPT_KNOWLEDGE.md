# SSO script knowledge (2026-10-21)

What SSO's patches do, read from the owner's .nki files (PAR_SCRIPT chunks in plain text and the group data; nothing decrypted). No script text. Each number is "read from the patch's script" or "read from the patch's group data"; where it sets playback it is still to be confirmed by measurement. Private detail with line references: ~/MuseScore/ms3fork/private/sso-scripts/KNOWLEDGE.md (not in repo).

**Scope.** Every patch the map uses (700 .nki) runs 16 distinct scripts:
- one main engine shared by 538 patches;
- five variants of it: Performance, Motif Brass, Curated Ensembles / Harp / Piano, solo strings, Solo Violin;
- Kickstart, for the percussion;
- small helpers: polyphonic-legato voice splitting, harp pedalling, a velocity preprocessor on two Total Performance
  patches, and UI-only scripts.

The Performance variant differs from the main engine only in small things. The behaviour of the Performance patches
comes from their group data.

Each finding below is marked (a) already used by the fork, (b) contradicts a fork assumption, or (c) not used yet.

### Legato (read from the patch's script)

- **Transition or fresh attack.** A note becomes a transition when:
  - the voice before it is still held, or
  - it starts less than "Legato overlap" after the release before it and on a different key. Legato overlap is 40 ms on
    the string Performance patches and 25 ms elsewhere.

  A note on the same key is always a fresh attack, because "Bridge repeats" is off in every patch. (a)
- **Leap over an octave.** The phrase restarts: the note is a fresh attack. (a) The audit's LEAP.
- **Dropped transitions.** A transition sent less than 15 ms after the previous note-on of its legato line, at velocity
  64 or below, is not played at all. (a) The audit's DROPPED.
- **CC119 (speed).** At each transition the script writes CC119 = 126 − floor(ms × 127 / 1000). Here ms is the time from
  the line's previous note-on to this one, as played, whether that previous note was a fresh attack or a transition.
  - The new note's voice starts 1 µs after that write, so the patch reads the new value.
  - (a) `[slurs] speedFromWritten`.
- **Transition velocity.** The strings' Performance legato uses the note's own velocity: no velocity-from-CC flag is set.
  (a)

### Performance transitions (read from the patch's group data)

- **Velocity.** At velocity 64, four transition layers sound together: fingered, runs, runs-release and fast portamento.
  - Portamento is velocity 1-19.
  - Detached is velocity 85-127.
  - On the Basses, fingered starts at velocity 20.
- **CC119.** CC119 does two things:
  - it moves where each layer's sample starts, within a range of 10-208 ms depending on the layer and section;
  - it crossfades the fast layers: runs at ×0.72 up to about 240 ms of spacing, then rising to 1; fast portamento at
    ×0.18-0.2, rising to 0.92 below about 110 ms.

  Each section has its own curves. Examples:
  - Violas fingered: no skip at spacings of 488 ms or more, the full 73 ms skip at 236 ms or less.
  - Basses fingered (167 ms) and runs-release (208 ms): always fully skipped.

  All Performance samples are 48 kHz.
- **(b) The arrival model doesn't match what SSO reacts to.**
  - The model is keyed on the written note length. SSO reacts to the played note-on spacing.
  - When a slur's first transition is sent early by more than the attack before it, SSO sees a shorter note. It then
    plays faster material and arrives sooner than modelled.
  - This is the likely cause of the Violas' early transitions in Whence bars 9 and 13. It also explains why one constant
    per patch does not fit the Celli and Basses residues.
  - To try: `[slurs] speedFromWritten` 1 together with plainTiming or transitionsEarly, in a heard check.

### Round robins (read from the patch's script)

- **Counting.** Each key keeps its own counter, which advances by 1 per note-on.
- **Resets.** The counter resets:
  - after a set time with no note-on on any key: 1500 ms on the Performance patches, 750 ms on four solo Performance
    patches, never on most others;
  - at transport start, only where "Reset on transport" is on. It is on in 527 of the 538 main-engine patches, but **off
    in the five string Performance patches**, and in Oboe Principal Total Performance and Tuba Solo Performance.
- **(b)** `restartTransport()` does not reset those patches. A pass that starts less than 1.5 s after the previous one
  plays different takes.
- **Built 2026-10-21**: `[notes] transportReset` (0 by default): "Reset on transport" on as a playback script value. kthost
  on the VM: with it and fixedTransitions, passes repeat at -95 .. -100 dB; without it -3 .. -6 dB with the modulators fixed.
  The 1.5 s idle reset also works offline (it counts processed audio).

### Dynamics, levels, releases (read from the patch's group data and script)

- **Dynamics.** CC1 (the Dynamics slider) and CC11 (Expression) act only through group modulators.
  - All-techniques longs crossfade p, m and f layers.
  - Sul tasto and flautando have a single recorded layer, scaled linearly by CC1: from 0.20-0.22 at 0 to 1.0 at 127.
  - No script or group volume sets their level. Their quietness is in the recordings. (Answers "what sets the sul tasto
    level": nothing adjustable in the patch.)
- **Velocity curve.** It is linear in every patch.
- **(c) The Hall trigger.** When CC1 falls by 10 or more in one step while a Performance note is held, every held key
  plays a diminuendo-release sample, at the level from before the drop. Whence has no such case. **Built 2026-10-21**:
  the audit's HALL; measured on the VM (one step 64 -> 50: a layer 14-25 dB under the note; two steps 1 ms apart: none;
  two values at the same sample: as one step); MuseScore makes it at a subito dynamic inside a slur and at fp;
  `[levels] hallGuard` (0 by default) splits such steps.
- **Releases.**
  - All-techniques long releases are chosen by how long the note was held: 2000 ms span, 1500 for harmonics and
    flautando.
  - Some release volumes fall with held time, to 0.43.
  - At a Performance transition, the old note's tail depends on whether that note was a fresh attack held at least
    90 ms, or a transition held at least 150 ms.

### Attacks (read from the patch's group data)

- **(c) Tightness.** The Tightness slider (default 0.5039) moves the start of the shorts within 20 ms. The direction
  depends on the articulation:
  - spiccato, pizzicato, Bartók and col legno skip 10 ms at the default and 20 ms at 0;
  - Short CS, Brushed and Short Harmonics skip nothing at the default and 20 ms at 1.

  **Measured 2026-10-21** (branch sso-script-fixes2, kthost on the Windows VM; docs/PLAYBACK_SETTINGS.md › Tightness): the
  direction read here is the wrong way round. Tightness 1 starts spiccato, pizzicato and col legno 10 ms earlier (Celli's
  pizzicato / col legno 15), Violins 1's Short CS 20 ms earlier; Tightness 0 the same amounts later; Short Brushed CS and
  Celli's Short CS don't move; the level doesn't change. Not offered yet.

### Other (read from the patch's script)

- **Shared settings.** A settings file in Kontakt's data folder, shared by every SSO patch on a machine, can override a
  patch's saved articulation mode, Reset on transport, reset key, transpose, CC-mapped velocity, Hall trigger and
  keyswitch start. **Built 2026-10-21**: on Windows the file is `%LOCALAPPDATA%\Spitfire Symphony Orchestra\Data\Share__Settings.nka`
  (Kontakt's data folder for an encoded library); the map's `<PluginSettings>` check it as a patch loads and in every
  render's preflight. The VM's (2026-10-21): nothing shared. The owner's PC: not checked.
- **Tempo.** Some groups are gated by the host tempo: BPM ranges and tempo-sync flags. (c) To try: check the tempo that
  MuseScore reports to the plug-in.
- **Kickstart.**
  - Velocity 127 becomes 126 unless "Roll on high vel." is ticked. (a) `maxVelocity` 126.
  - A choke hit fades the instrument's other hits over 120 ms.
  - Round robins are per hit, and reset on transport.
- **Mics.** Mic levels never purge samples; only a mic's on/off button does. (a) Mic compensation is safe.
- **Total Performance velocity preprocessor.** On Tenor Trombone Solo and Trumpet Solo Total Performance, a preprocessor
  replaces the first note's velocity with CC112, and an overlapping note's velocity with CC113, when those CCs are
  non-zero. The fork must not send CC112 or CC113 to these two patches. **Built 2026-10-21**: the map's `avoidCC="112 113"`;
  the renderer drops them (a `cc112` automation lane was the only way to send them); the audit's AVOIDED CC.
