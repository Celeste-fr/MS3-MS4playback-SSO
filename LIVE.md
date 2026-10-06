# Playing through Ableton Live 12

Branch `live-integration` (2026-09-28, 2026-09-29). The owner's goal: MuseScore still plays by itself
exactly as before (Spitfire Symphony Orchestra hosted in Kontakt Player 8). Optionally it plays
through Ableton Live 12 instead. In that mode Live hosts SSO. Automation can be drawn in Live (read back
into the score when Live saves the set, so MuseScore alone plays it the same way) or in MuseScore's own
automation editor (2026-09-30: [Automation lanes in MuseScore and in Live](#automation-lanes-in-musescore-and-in-live));
both sides can edit every lane.

There are two ways to play through Live (and, besides them, [editing any Live MIDI clip in
MuseScore](#editing-live-clips-in-musescore)). For both, MuseScore can write the Live Set with every track set up:
[Create Live Set](#create-live-set).

1. **MuseScore plays through Live** (*Mixer › Play through Live*, 2026-09-28). MuseScore is the
   clock: it sends each part's notes, switches and controllers live to Live, plus MIDI clock and
   Song Position Pointer. Live has no notes of its own; its arrangement is in the score's
   quarter notes. Sections 1-5 below.
2. **Live plays the score** (*Mixer › Advanced Options… › Ableton Live › Live plays the score*,
   2026-09-29; the owner: "is it possible to have entered notes in the score live update in
   ableton?", then "what if it's the other way, when ableton is playing sound MuseScore switches
   off"). Each part's notes are a clip in Live's arrangement, kept up to date with every edit of
   the score. Live is the clock and plays; MuseScore sends the library nothing and follows Live.
   Section [Live plays the score](#live-plays-the-score).

How the first fits together:

- **Notes**: MuseScore sends each sound-library part to MIDI output A-D. Each part gets its own
  route (port and channel), the same one *View › Sound Library…* shows.
- **Sync**: MuseScore sends MIDI clock (24 per quarter note), Song Position Pointer and
  Start/Continue/Stop to a separate *MIDI sync output* (`libmscore/midisync.h`, `Seq::process`).
  Live follows tempo and position, jumps included.
- **Automation**: Live saves the automation in its set (.als). *Mixer › Advanced Options… ›
  Import automation from Live Set…* reads it into the score as lanes (editable in MuseScore too)
  (`libmscore/liveset.h`, `libmscore/automation.h`, `mscore/liveintegration.h`).
- **Switching**: *Mixer › Play through Live* switches between Live and the plug-in hosted by
  MuseScore.

## Set up on Windows

### 1. Virtual MIDI ports (loopMIDI)

Install loopMIDI (Tobias Erichsen) and create these ports:

- `MuseScore A`, plus `MuseScore B`, `MuseScore C` and `MuseScore D` as needed. Each port
  carries 16 parts, in score order.
- `MuseScore Sync`, for the clock only. Live turns Sync on per input port, and a port that also
  carries notes would need both settings on the same input.

### 2. MuseScore

**Preferences › I/O:**
- PortAudio: on.
- MIDI output: `MuseScore A`.
- Output latency: **20 ms** is a good start. At 0, PortMidi ignores the timestamps and sends each
  message when the audio block is computed, so clock and notes jitter by up to one audio
  buffer. With a latency, every message leaves at its exact time. Tested here: clocks within
  about 1 ms, and each beat's note within 1 ms of its clock.

**Preferences › I/O › Sound library:**
- Library: Spitfire Symphony Orchestra.
- Play through: MIDI output, or use the Mixer switch (see below).
- MIDI output B, C, D: `MuseScore B` … if the score has more than 16 library parts.
- MIDI sync output: `MuseScore Sync`.

**Mixer:**
- Global playback: *Sound library*.
- *Play through Live*: on. It asks first, because the patches loaded in MuseScore are released
  (their memory freed). Switching back loads them again.

*View › Sound Library…* lists each part's port and channel, plus the patch Live must load for it.

### 3. Live 12

**Settings › Link, Tempo & MIDI:**
- `MuseScore Sync` input: **Sync** on. Sync Type: MIDI Clock. Track and Remote off.
- `MuseScore A`-`D` inputs: **Track** on. Sync off.
- Link: off. Live cannot receive MIDI clock while Link is on (Live 12.1 manual, 34.3.2).
- Select `MuseScore Sync` in the MIDI Ports list to see its **Sync Delay**. Adjust it until
  Live's sound lines up with MuseScore's click (manual 34.3.3).

**Control bar:**
- Turn on **EXT** (or *Options › External Sync*). The upper LED next to it flashes when Live
  receives the clock.
- Keep Live's **Loop switch off**. With the loop on, Live wraps song positions into the loop
  (manual 34.3.2), so MuseScore's positions would land in the wrong place.

**One MIDI track per part:** *Mixer › Advanced Options… › Ableton Live › Create Live Set…* writes them
(section [Create Live Set](#create-live-set)). By hand, as a fallback:
- Kontakt 8 with the patch *View › Sound Library…* names for that part.
- **Articulation switching "UACC & UI only"** in each patch. This is what MuseScore's own setups
  use (`$iooxo` = 3). MuseScore switches articulations with UACC on CC32.
- *MIDI From*: `MuseScore A` (or B-D) and the part's channel (`Ch. n`).
- *Monitor*: **In**.

**Arrangement:**
- Work in the Arrangement view. Beat 1.1.1 is the score's start.
- MuseScore sets the tempo; Live follows it. Live's own tempo and time signature don't matter,
  but the arrangement is in played time: repeats are written out.

### 4. Drawing automation in Live

- Kontakt shows each SSO patch's named controls as automation slots: Dynamics, Vibrato,
  Expression, Release, Tightness, Mic 1-5, Mic Mix Distance, Variation, Reverb …
- For a Kontakt device, click *Configure* in its header, then click (or move) the control in
  Kontakt's window. It is added to Live's panel under Kontakt's name for it (manual 23.3.1.2).
- Draw its automation in the arrangement.
- MuseScore matches it by that name to the map's controller of the part (`param` title). Matching
  is loose, like `Vst3Plugin::parameterId`: case, punctuation and a slot number in front such as
  "#002" are ignored.
- **Leave Dynamics and Expression to MuseScore.** It sends CC1 and CC11 from the notation;
  automation of Kontakt's Dynamics slot would fight with them.
- A MIDI clip's CC envelopes are imported too. A CC goes to the map's controller on that CC, or
  else becomes a raw `cc<n>` lane.
- Automation in a repeat's second pass is left out, because a lane follows the score, not the
  repeats. The import report counts those points.

### 5. Saving, and bringing the automation back

- Save the set (Ctrl+S).
- In MuseScore: *Mixer › Advanced Options… › Ableton Live (this score) › Import automation from
  Live Set…*. Choose the `.als`. A report lists which track went to which part, and what was
  not imported (tracks without a part, parameters without a controller, Live's mixer).
- *Import again when Live saves it* (on by default): each save in Live re-imports the set. The
  re-import waits until playback stops.
- Every import is one undoable step. *Unlink* removes the link and the Live lanes.
- How a track finds its part:
  1. By its MIDI input: the port and channel of the part's route. The port names are MuseScore's
     MIDI outputs without the driver's prefix (`MMSystem,MuseScore A` → `MuseScore A`).
  2. Otherwise by track name = part name.
- Each import merges per lane (`Automation::merge`, 2026-09-30): a lane whose events in Live changed since it came
  from Live (and unedited in MuseScore) takes Live's; one edited in MuseScore since, unchanged in Live, keeps MuseScore's;
  the same envelope on both sides: they agree. **Changed on both sides since they last agreed, or never agreed and
  different (the owner, 2026-10-01: "whenever there's conflict, it asks the user to preserve one")**: a dialog lists
  each such lane (part, parameter, both curves, when each changed) with *MuseScore's* / *Live's* (and *All
  MuseScore's* / *All Live's*); *Import* applies the import with those choices as one undoable step, *Don't import
  now* changes nothing (`Automation::conflicts`, `LiveIntegration::askConflicts`; without a window, e.g. a
  command-line import, MuseScore's are kept and reported). A kept MuseScore lane is asked about again only when
  Live's changes again. Curved segments keep Live's curve (on the score's own axis).
- The lanes are stored in the score's metaTag `automation`, each marked `"source": "live"` with
  the set's path and time, the track and Live's parameter name and id. The metaTag `liveSet`
  holds the link. MuseScore 3.6 keeps both through a round trip (tested with 3.6.2).
- With *Play through Live* on, MuseScore sends nothing for these lanes, since Live plays them
  itself. It also sends no value of its own for those controllers.
- With *Play through Live* off, MuseScore's hosted Kontakt plays them as plug-in parameter
  events, the same way as in Live.

## Live plays the score

The owner, 2026-09-29: "is it possible to have entered notes in the score live update in ableton?",
then "what if it's the other way, when ableton is playing sound MuseScore switches off".

Each sound-library route of the score (a part's patch; its extra patches such as the Performance
legato and its copies for other tunings are routes of their own) becomes **one clip in Live's
arrangement**, on the track that plays it, from beat 1 over the whole score. The clip is rewritten a
moment after every edit in MuseScore (typing, undo, redo), only for the routes that changed. **Live
plays** the clips; MuseScore sends the library nothing and follows Live's transport.

### How it works

- **What a clip holds:** what MuseScore's playback renders for that route, repeats written out: the
  notes and their velocities, and the controllers as **carrier notes** on the top keys 116-127, one
  key per controller (UACC CC32, CC1, CC11, CC64 pedal … the table is `LiveClips::CARRIER_CCS`; the
  value is the velocity, 0 as 1, so 127 is exact and 1 plays as 0; the UACC switch is value + 1:
  `carrierVelocity`), and the **pitch bend** (the microtones of a patch with
  `bend=`, legato glides included) on keys 115 (its upper 7 bits) and 114 (its lower 7), each written
  when it changes (2026-09-30). The Live Object Model can write a clip's notes but not
  its MIDI controller envelopes, so the controllers travel as notes, and the **MuseScore Link**
  device, placed before Kontakt on the track, turns each carrier into its controller or the pitch bend
  (and drops its note-off). Keys 114-127 are never played as notes. So everything is in the clip and played by Live's own clock: sample-exact with the notes,
  also in an export or a freeze, and chased when playback starts in the middle (Live's *Chase MIDI
  Notes*: each carrier lasts until that controller's next value). A controller at a note's tick is
  placed just before it (0.26 ms at 120 bpm apart): switches first, then the controllers, the pitch bend
  last; at a tick without a note (a glide's step) the last of them is at the tick itself.
  Plug-in parameter events (MuseScore's own automation lanes of Kontakt's parameters: Vibrato, mics …) are
  not notes: the device sets those parameters itself while Live plays ([Automation lanes in MuseScore and in
  Live](#automation-lanes-in-musescore-and-in-live)); lanes Live's set already holds are left to Live. The part's *Controllers…* that are plug-in parameters are in each patch's state in the Live Set
  (Create Live Set, below).
- **Time:** the Live Object Model can't write the song's tempo automation, so Live plays at one tempo,
  the score's first, and the clips hold the notes at their real times (seconds as MuseScore plays them:
  tempo changes, rit./accel. lines, fermatas, repeats). With one tempo throughout, Live's bars are the
  score's bars. Otherwise Live's grid drifts from the bars; the device puts a locator at each played bar
  ("MS 12"; with a rehearsal mark "MS 17 B") so they can be found. Automation drawn in Live lines up
  with the notes either way; the import reads such a set's beats at its tempo back into score time.
- **Transport:** Live is the clock. Live's Play: MuseScore starts at Live's position (the score's cursor
  follows, MuseScore's own built-in parts play along); a difference of more than 0.15 s moves it; Live's
  Stop stops it; stopped, the cursor follows Live's position. MuseScore's Play starts Live at the play
  position instead, its Stop stops Live. MuseScore sends no MIDI clock in this mode.
- **The clips belong to MuseScore.** Notes edited in them in Live are overwritten at the next change of
  the score. Draw automation in the track's lanes, not in the clips. The device never touches a clip it
  didn't make (another name) and makes none over a track's other clips.
- **The link:** OSC over UDP on this computer only (127.0.0.1): MuseScore sends to port 9001
  (Preferences › Advanced `io/live/clipsPort`), the device answers on 9002. The device says hello every 2 s;
  a new device (the set opened again) gets everything again; each clip is confirmed and sent again if
  not. One device (the first loaded) does the work for all tracks; any other copy takes over when it goes.
- **Status:** Mixer › Advanced Options… › Ableton Live shows whether the device answers, when the last
  update was confirmed, and each route that found no track.
- **The connection lost** (the owner, 2026-10-02: "musescore should notify you if a connection stopped"): the
  device says hello every 2 s; none for 8 s (measured: up to 6.0 s apart while Live is busy) (Live or the set closed, the device deleted, its port changed), or
  its `/live/bye` (the hub copy deleted: at once), and MuseScore shows a yellow bar across the top of the
  score area (over the tabs), once per loss and only while something uses the link (a clip tab, Live plays the score, Play
  through Live): "Lost the connection to Live (the MuseScore Link device, UDP port 9001): …" with what stops
  working and what MuseScore does meanwhile (clip tabs: edits stay here and are written when Live is back,
  playback with MuseScore's own sounds; Live plays the score: MuseScore's Play plays only its own parts; Play
  through Live: MIDI still goes to the ports, unchecked). It stays until dismissed (×) or the device answers
  again, then a green "Connected to Live again" for 10 s. No dialog. A new hub (another copy took over, or the
  set was opened again) gets the clip tabs' clips back (`/ms/clip/adopt`: in sync if their notes are as
  MuseScore knew them, else a conflict), and their unwritten edits are written. Also notices: a clip tab's clip
  deleted in Live; the MuseScore Link copy removed from (or added again to) a clip tab's track. Not detected:
  the MIDI ports of Play through Live disappearing (loopMIDI closed) without the device.

### Setting it up

1. **Install the device**: MuseScore offers it ([The Live helpers](#the-live-helpers-installed-by-musescore)): at
   startup, when Live 12 is on the computer and its User Library lacks the device (or has another version), a prompt
   copies `MuseScore Link.amxd` to `<User Library>\Presets\MIDI Effects\Max MIDI Effect`; any time: *Mixer ›
   Advanced Options… › Ableton Live › Install Live helpers…*. By hand: copy `tools/live/MuseScore Link.amxd` (next to
   MuseScore3Evo.exe in the Windows build) there. It needs Max for Live (Suite) and Max 9 (Live 12.2 comes with
   9.0.7): its script runs in Max's `v8` object.
2. **Live's settings:** EXT (external sync) **off**: Live is the clock here. *Options › Chase MIDI
   Notes* on (the default).
3. **One MIDI track per route**: *Mixer › Advanced Options… › Ableton Live › **Create Live Set…*** writes a
   set with all of them, ready to open ([Create Live Set](#create-live-set)); a part added later: *Add missing
   tracks…*. By hand, as a fallback (*View › Sound Library…* lists the routes: each part, and under it (+)
   its extra patches such as "Solo Violin - Performance"):
   - Kontakt 8 with that patch, articulation switching "UACC & UI only", as in section 3 above;
   - **MuseScore Link before Kontakt** on the track (drag it to the left of Kontakt);
   - the track found either by *MIDI From* = the route's port and channel (as for playing through
     Live), or by **name**: the part's name for its main patch ("Violin"), "<part> – <patch>" for another
     patch ("Violin – Solo Violin - Performance"; any dash) or the patch's name alone when only one track
     has it.
   The device sets each track's *Monitor* to Auto, so the clips play (it sets In when you go back to
   *Play through Live*, where MuseScore's stream plays).
4. **In MuseScore:** Mixer › Advanced Options… › Ableton Live › **Live plays the score** (it also
   turns on *Play through Live*: the library goes to MIDI output, MuseScore's Kontakt instances are
   released). The status line should say the device answers and list no route without a track.
5. Play from Live or from MuseScore.

### What is tested, and what only Live can show

Tested here: the clips' contents (`tst_liveintegration` clipsTimeline, clipsControllers, clipsScore,
clipsChanges, clipsOsc, clipsImport); the device's script against a stand-in for Live
(`tools/live/test/test_device.js`: hub, tracks by port / name, clips replaced, the owner's clips left,
locators, tempo, transport, Monitor) and its patcher and `.amxd` (`test_patch.js`: carrier notes to
controllers); and the whole chain with a real MuseScore build (GUI under Xvfb) talking to the stand-in
over UDP (`tools/live/test/fake_live_server.js`): a two-part score with a repeat and a tempo change
gave four clips on four tracks found by name, locators at the right beats (60 then 120 bpm at bar 3),
and a note moved up in MuseScore reached its clip about a second later, only that clip sent again;
Live's Play from beat 4 started MuseScore at bar 2, its Stop stopped it.

Only real Live can show (to check first):
- the device loads (the `.amxd` container is written by `make_device.py`; Max may want it opened
  and saved once) and its `v8` script runs;
- `Track.create_midi_clip` and `add_new_notes` behave as documented, and a few thousand notes write
  quickly enough;
- the carriers become controllers before the notes at the same time (SSO's articulation right on the
  first note after a switch), in playback and in *Export Audio/Video*;
- the pitch bend carriers (keys 115 / 114) become one pitch bend (Max's `pack 224 0 0` banged from its left inlet),
  so a quarter-tone note on a Performance patch sounds in tune;
- chasing at a mid-song start sends the controllers in force;
- MuseScore following Live stays in step over a long piece.

## Create Live Set

The owner, 2026-09-30, about the tracks above: "that's so many manual steps. is the creating MIDI track and
renaming it, dragging in Kontakt 8, etc. possible to be automated?". MuseScore writes the Live Set itself.

### The plain set (2026-10-06): what Create Live Set writes now

The owner, 2026-10-06: "I'm done with automatic adjustments altogether … just make musescore export everything in
the correct techniques and configured so that it's easiest for me to adjust in ableton". Choices made then: a track
per technique, all of a section's techniques foldable under one track, one Kontakt per patch, plug-in parameters as
track automation, "Plain set, no device"; then "make all techniques in an instrument collapsible and all instruments
in a section collapsible at the same time".

- **Layout** (`libmscore/plainliveset.*`, from MuseScore's own render as *Live plays the score* renders it):
  a **group per section** (instruments.xml's group: Strings, Woodwinds …), in it a **group per part**, in that per
  patch the part plays a **Kontakt track** (the library's plug-in with the patch's setup, the part's Controllers,
  the part's Mixer) and **one MIDI track per technique** ("Violin – Long", "Violin – Spiccato" …), MIDI To the
  Kontakt track. Both group levels fold.
- **Clips**: a technique track's clips hold its notes and **their own switch**, one unit (1/3840 beat) before each
  run of the technique's notes (no other technique of the Kontakt starting in between), so a note moved to another
  technique's track plays that technique. **UACC (CC32)**: a clip per run, its **Sub** (Live's clip Bank / Sub / Pgm)
  the technique's value, its Bank alternating 0 / 1 run by run: Live sends CC0 Bank and CC32 Sub at the clip's start
  (it keeps no clip envelope on CC0 or CC32), but only when they differ from what that track sent last.
  Another CC: one clip the whole song, an envelope stepping from a rest value (the lowest the patch's articulations
  don't use) to the value and back after the run. A keyswitch patch: one clip, a key note one unit long per run. The
  Kontakt track's "Controllers" clip holds the dynamics (CC1), CC11 and the pedal as envelopes; plug-in parameters
  MuseScore automates are automation on the Kontakt track.
- **No automatic timing or levels**: what the playback settings say, off by default (docs/PLAYBACK_SETTINGS.md).
- **Reported, not in the set**: two techniques of one Kontakt starting at the same instant (one switch can't play
  both: the report gives the beat), program-change switches, notes of a copy for another tuning (played at the key's
  pitch), pitch bends (not written yet).
- **Add Missing Tracks** and the Live-against-MuseScore check still use the route set below (a track per route,
  the MuseScore Link device): `LiveSetKind::MISSING_ROUTES`, `ROUTES`.
- **Checked in Live 12.4.6** (the VM's trial, Drift standing in for Kontakt, 2026-10-06): the set opens without a
  dialog, both group levels fold (outer group TrackGroupId -1, inner the outer's Id, AudioOut/GroupTrack). MIDI To is
  `MidiOut/Track.<id>/TrackIn` (Upper the track's name, Lower "Track In"); the receiving track plays it only with
  monitoring **In** (MonitoringEnum 0: Auto plays nothing unarmed), so the Kontakt track is written In.
  `ControllerTargets.<i>`: 0 the pitch bend, 1 channel pressure, i = CC (i-2). Two envelope points at one time are
  a step. A clip's GrooveId must be -1 (one with an empty pool: "GroovePool corrupted"). Clips carry the song's time
  signature.
- **CC32 in Live 12.4.6** (the VM, a Max for Live MIDI monitor on the tracks, 2026-10-06): the clip-envelope chooser
  lists no CC0 and no CC32, a set's envelope on `ControllerTargets.34` is dropped at load and nothing is sent. A clip's
  Bank / Sub / Pgm (`BankSelectCoarse` / `BankSelectFine` / `ProgramChange`, -1 none) is sent once at the clip's start,
  also when playback starts inside the clip: CC0 (0 when Bank is none), CC32 Sub, the program change only when set;
  not re-sent when the playhead is moved while stopped. Clips starting together send theirs in no useful order.
  **Not re-sent when the track's last Bank and Sub were the same** (per track: a Long clip after the track's first Long
  clip sent nothing although other tracks sent other Subs in between); alternating the Bank 0 / 1 sends it.
- **SSO in Kontakt 8 follows the Sub** (the VM, "Violins 1 - All techniques", 2026-10-06; readings:
  https://claude.ai/artifact/TtoKjRAFT5L2NYF6HCk9B7) in **"UACC & UI only"** (`$iooxo=3`, as MuseScore's setups set it;
  in the library's default "Normal keyswitching" 7 of 8 clips picked a wrong technique), CC0 0 or 1 changes nothing;
  with the alternating Bank every run switched, also when playback started inside a clip. Pitch bends are left out (reported) until the scale of Live's clip bend envelope is measured. Test:
  `tst_liveequivalence` `plainSet` (`MS_PLAIN_SET_OUT=<file.als>` writes its set).

- **Read back** (`libmscore/livetracks.*`, metaTag `liveTracks`; the owner, 2026-10-06: "the mscz stores all tracks'
  automation"): *Import automation from Live Set…* on a saved plain set keeps every track's automation and mixer in the score.
  - **Each track's key** is written into its Info text (Name/Annotation): `MuseScore: Strings / Violin / Violin` for
    the Kontakt, `… / Violin / Long` for a technique (section, part, Kontakt track, technique; the groups
    `MuseScore: Strings`, `MuseScore: Strings / Violin`). Live 12.4.6 keeps it through Save As (the VM,
    2026-10-06, all 7 tracks' texts); without it a track is found by its groups' names and its own (a technique
    track also by its MIDI To), but only for keys the score recorded.
  - **Create Live Set records what it wrote** (in `liveTracks` › written: each track's kind, part, patch and the hash
    of each clip controller's envelopes as the reader reads the file back). An envelope of the Kontakt's Controllers
    clip whose hash is unchanged is MuseScore's own render and comes back as nothing; a changed or new one becomes a
    part lane (as before: `track`, `clipCC`, liveHash, pointsHash). Plug-in parameter automation on the Kontakt track
    becomes lanes; Create Live Set gives the score's parameter lanes the written envelope's liveHash, so an unchanged
    one keeps MuseScore's lane (Automation::merge). An extra patch's Kontakt: its changed envelopes are reported, not
    imported.
  - **Technique tracks**: their switch (the Sub, the switch envelope, keyswitch notes) is never a lane; another
    clip envelope there is reported as not imported.
  - **Mixer**: a Kontakt track's Volume / Pan / Track Activator is the part's Mixer when the part has only that
    Kontakt (set as the Mixer sets it: not an undo step). Every other track's static mixer values (where they aren't
    Live's defaults, or the part's Mixer for a second Kontakt) and every track's mixer automation (Volume, Pan,
    Speaker, as Live's events with their curves) are kept in `liveTracks` › tracks, the newer import replacing a
    track's entry; Create Live Set writes them into the next set. No sends: the plain set has no return tracks.
    Live 12.4.6 opens such a set, plays group Volume ramps and a Speaker off, and keeps the envelopes through Save As
    (the VM, 2026-10-06). A technique track has no instrument, so Live shows it no mixer and its Volume / Pan act on
    nothing (kept, not heard): a technique's level is set on its notes or the Kontakt's controllers.
  - Without the record (the score not saved after Create Live Set), the tracks match as any set's (MIDI input or
    name) and the Controllers clip's envelopes come back as lanes.
  - Test: `tst_liveequivalence` `plainSetReadBack`, `liveTracksJson`.

The sections below describe the route set.

### How to use it

1. Open the score. Play it once in MuseScore with the library's plug-in (or let the patches load at score open):
   each patch's first load resaves its setup as Kontakt's own state, which Kontakt loads about 20 times faster
   than a setup made from the `.nki` (docs/HISTORY.md › Load times). The report says how many patches were never loaded.
2. *Mixer › Advanced Options… › Ableton Live › **Create Live Set…***. The file dialog offers
   "<score title>.als" next to the score. A report lists the tracks, their devices and MIDI From, and what was
   left out.
3. Open it in Live 12 (*File › Open Live Set…*), save it where you like (Live may ask to save it in a project).
4. *Live plays the score* (or *Play through Live*) as before: the device finds every track.
5. **A part added later**: *Add missing tracks…* writes a small set with only the routes that have no track in
   Live yet. In Live's browser, go to that file, unfold it and drag its tracks into your set. Which routes have a
   track: the MuseScore Link device's report when it answers for this score with *Live plays the score* on
   (each route's clip "no track"), else the linked set as last saved (the automation import's link; MIDI From or
   the name, as the device looks), else all of them. The report says which.

### What is in the set

- **One MIDI track per sound-library route** (`SoundLib::routes`, in score order: each part's main patch, its extra
  patches, its copies for other tunings), named as the device finds it: the part's name for its main patch,
  "<part> – <patch>" for another patch, "<part> (2)" … for a copy for another tuning (`LiveSetWriter::trackName`,
  the clip names without "MuseScore: "). One colour per part.
- **MIDI From** = the route's port and channel ("MuseScore A", "Ch. 3"), when that MIDI output is set in
  Preferences; else *All Ins* (Live plays the score all the same: the device finds the tracks by name; for *Play
  through Live* set MIDI From by hand). *Monitor* Auto. MIDI To, audio: Live's defaults (Master).
- **Devices**: the **MuseScore Link** device, then the library's plug-in (**Kontakt 8**) holding the patch:
  - the device (a Max MIDI Effect) is referenced by path, as Live does: the copy in Live's **User Library**
    (Live's `Library.cfg` names the folder; else `Documents/Ableton/User Library`; the usual
    `Presets/MIDI Effects/Max MIDI Effect/MuseScore Link.amxd`, else anywhere in the library), else the one next
    to MuseScore3Evo.exe. The report says which, and warns when the User Library's copy differs from the one that
    comes with this MuseScore (an older device). Its saved *Port* is MuseScore's (`io/live/clipsPort`).
  - Kontakt's state is the patch's setup exactly as MuseScore loads it (`SoundLibraryHost::setupState`): Kontakt's
    own state when a load has resaved it, else made from the `.nki`; "UACC & UI only" (`$iooxo` 3) and **512
    voices** as MuseScore sets them. Kontakt's controller state: what the setup holds (empty for Kontakt).
  - **with the part's Controllers** (2026-09-30): when the part sets plug-in parameters of that patch in
    *Controllers…* (the piano's mic levels, a violin's vibrato …), the state is the one MuseScore plays: the setup
    loaded into a Kontakt instance as MuseScore loads it (its script settled, 1 s), the part's values set exactly as
    playback sets them, a quarter second for Kontakt to take them, then Kontakt's state
    (`SoundLibraryHost::stateWithControllers`). A patch without such values keeps its setup byte for byte. The same
    parameters are listed in Live's panel of Kontakt (as after *Configure*: name, id, value), so whether Live sets
    them again over the state or not, both agree, and they are ready for automation.
  - **with the part's automation lanes** (2026-09-30): each lane's parameter is in Live's panel too (at the patch's own
    value), so the MuseScore Link device finds it and plays the lane. The report lists per part
    and patch what was set. Controllers on a MIDI CC are in the clips (carriers), as before.
    **Changed later in MuseScore, they don't follow into the set** (the set is a file Live opened): create the set
    again, or change and automate them in Live (Live's panel has them).
  - **Live's mixer** = MuseScore's Mixer (2026-09-30): each track's Volume is the part's volume as MuseScore's host
    plays it, (v/100)² as a linear gain (100 = 0 dB, 127 = +4.2 dB, 0 = -70 dB, Live's lowest; MuseScore's 0 is
    silence), its Pan the same position (-1 … 1; Live's law is MuseScore's: constant power, sine / cosine, 0 dB in
    the centre, +3 dB fully panned, Live 12 manual, Audio Fact Sheet › Panning: the same gains at every position). A
    muted part: the track's Track Activator off. Solo is left out, as in MuseScore's audio export. Changed later:
    as the Controllers (create the set again, or use Live's faders). The owner's
    Live set confirmed the form: Live's `Vst3Preset/ProcessorState` is Kontakt's component state (an NI "hsin"
    container whose size field is the whole blob's), `ControllerState` empty, `Uid` Fields.0-3 = Kontakt 8's class id
    5653544E-694B386B-6F6E7461-6B742038 as signed 32-bit numbers.
  - left out, with a line in the report: a patch without a setup (its `.nki` not found), a kit's own route (a kit
    has no patch of its own: its drums are extra patches with tracks of their own), everything when MuseScore was
    built without plug-in hosting (the tracks then have the device only).
- **The song**: the score's first tempo (as *Live plays the score* plays it) and time signature. No clips (the
  device writes them), no automation, no return tracks, 8 empty scenes, loop off. (EXT and Link are Live's settings,
  not the set's: keep EXT off for *Live plays the score*.)

### How it is written, and what it rests on

`libmscore/livesetwriter.*` writes the XML and gzips it; `mscore/livesetexport.*` gathers the routes, the device and
the states, and runs the dialog.

- Live refuses a set it can't read ("The document could not be opened"). The format is Ableton's and undocumented,
  so nothing is left out of its structure on a guess: every element Live 12.2 writes is written, in its order, with
  Live's own default values, learned from the owner's two Live 12.2 sets (2026-09-30; `MinorVersion` 12.0_12203; kept
  outside the repository) and written by hand in the code. A set generator on GitHub (iron-static's
  `create_als.py`) had to add elements it had left out of tracks and mixers (Speaker, Sends, the 131
  ControllerTargets …) before Live 12.2 opened its sets; DawVert writes every element too. What is left out is content: return
  tracks (a set may have none: `SendsPre` and the tracks' `Sends` are then empty), clips, envelopes, the devices'
  browser `SourceContext` (written empty, as Live writes it for a mixer), other devices.
- Live's bookkeeping, kept consistent and checked before writing (`LiveSetWriter::validate`; nothing is written if it
  fails): every *pointee* id (AutomationTarget, ModulationTarget, Pointee, the modulation targets, MidiControllers'
  ControllerTargets) unique and below `NextPointeeId`; each track's MainSequencer and FreezeSequencer with one clip
  slot per scene; a track's devices with different ids. Binary data upper-case hex, 40 bytes a line, as Live writes it.
- Numbers learned: the time signature is (numerator − 1) + 99 × log2(denominator) (4/4 = 201; the tempo and time
  signature each also as an automation with one event at −63072000, as Live keeps them); the device's saved data
  is `{"Port" : [ 9001 ]}` as Max writes it; the file reference's `RelativePathType` 6 with a path relative to the
  User Library (as the owner's set has it for the device there), 0 with the absolute path alone for a copy outside
  it (unconfirmed); `OriginalCrc`: CRC-16 (polynomial 0x8005, start 0, no reflection) of the file's first 16 KiB,
  the one CRC-16 of the usual ones that gives the owner's set's value (35326) for its device (49211 bytes, the
  device of 6175e30), at a round length: one example only; Live finds the file by its path first.
- MIDI From for one port and channel: `MidiIn/External.Dev:<port>/<channel − 1>`, upper "<port>", lower "Ch. n",
  **by analogy** with how a Live 10 set writes a MIDI output to one port (`MidiOut/External.Dev:IAC Driver (Bus
  1)/0`, "IAC Driver (Bus 1)", "Ch. 1"); the owner's sets only had *All Ins*. If Live shows the input as missing,
  choose it once in Live; the tracks are found by name either way.

### What is tested, and what only Live can show

Tested here:
- `tst_liveintegration` liveSetWrite: a two-part score's routes as tracks (names, MIDI From, colours), tempo 60 and
  4/4, the device before Kontakt on each track, the states byte for byte (every byte value, a controller state too),
  the device's file reference and saved port, Kontakt's `Uid`, the tempo and time signature (mixer and default
  events), gzip; the automation import's reader reads it back (tracks, MIDI From, plug-ins, tempo) and would match
  each track to its part; All Ins and a track without devices; `validate` catches a wrong `NextPointeeId`, a pointee
  id twice, a clip slot missing, a truncated file; the time signature numbers, the CRC (CRC-16/UMTS's check value,
  16 KiB), the track names = the clips' without "MuseScore: ". liveSetMissing: which routes have a track (MIDI From;
  the part's name; "<part> – <patch>" with any dash; the patch alone on one track only; loose names).
- `tst_soundlibrary` liveSetTestSynth: with a real VST 3 (the test synth): its class id from its module
  (`Vst3Plugin::classInfo`, the words of its FUID as Live writes Kontakt's), its state taken apart
  (`Vst3Plugin::splitState`); a part with three extra patches ("<part> – <patch>" tracks) and one on two tunings
  ("(2)"), read back.
- The generated set's element tree against the owner's two Live 12.2 sets (`tools/live/test/compare_als_skeleton.py
  <generated> <Live's>`, run here, not in CI: it needs the owner's files): 1332 kinds of element compared (their
  attributes and children in order), none differ; left out only the content listed above (return tracks' sends,
  the browser contexts). A value-by-value diff against the owner's empty MIDI track, main track and settings:
  differences only in names, colours, MIDI From, tempo and view state.
- A MuseScore GUI build under Xvfb (the test synth as the library's plug-in, a User Library in a scratch home):
  *Create Live Set…* offered "Strings.als", wrote 2 tracks with the device from the User Library and each part's
  setup byte for byte; after linking a set with only the violin's track, *Add missing tracks…* wrote the cello's
  alone and said it found the tracks from the linked set.

Only real Live can show (to check first):
- that Live 12.2 opens the set at all, and without a message about the set or a device;
- that Kontakt restores each patch from the state (the right patch, "UACC & UI only", 512 voices) and how long a
  full orchestra takes to open (resaved setups: Kontakt's own states, as when MuseScore loads them);
- that the MuseScore Link device loads from the User Library path (else Live lists it as missing: *File Manager*
  can locate it), and that its saved port comes back;
- MIDI From = "MuseScore A" / "Ch. n" (the input's target form is unconfirmed);
- dragging tracks from the *Add missing tracks* set in Live's browser into an open set brings their devices and
  Kontakt's state along;
- the Controllers: Kontakt shows the part's values (e.g. the piano's Mic 1 level) and Live's panel lists them with
  the same values (the state itself was checked in real Kontakt on the VM: "Measured with SSO");
- the mixer: each track's fader and pan at the written values (e.g. -3.9 dB, 25L), a muted part's track deactivated.

### Open questions for the owner

- Live asks where to save a set opened from outside a project: save it next to the score, or in a Live project?
- The track colours: one per part, from a fixed list. By family (strings, woodwinds …) instead?
- Return tracks (a reverb / delay like Live's default set): none, since SSO brings its own room. Wanted?

## Automation lanes in MuseScore and in Live

The owner, 2026-09-30: "when you select a MIDI track, MuseScore lets you edit the notes AND show you automation
tracks for every possible parameter in SSO, in which you can draw automation curves just like you can in Ableton",
then "why not make it so that you can edit the automation curves in both and they sync up with each other?".

### Drawing them in MuseScore

In **Continuous View**, click any note (or anything) of a part the sound library plays, of a part Live plays (*Live
plays the score*) or of an *Edit in MuseScore* clip tab: its automation opens under its staves, on the score's own time
axis (a lane's point sits under the middle of its note's heads, the grid's bar lines on the staff's: since 2026-10-02,
when they stood ~1 sp right of them). Header row: *+* adds a lane (Dynamics (CC1), Expression (CC11), and every
control of the part's patches from the map: Vibrato, Mic Mix Distance, Mic 1-5, Release, Tightness, Mute …; a part
Live plays also its Live track's parameters, "Live: Operator › Tone"; a clip tab only its track's Live parameters:
below), *All* shows them all, the pencil is Draw Mode, the triangle folds. Empty lanes are hidden
until added; × hides a lane (it still plays). Editing as in Live 12 (manual 25.5): click adds a breakpoint (snapped to
the grid, which follows the zoom; Alt: free), drag moves (Shift: fine), double-click or Delete removes, Alt-drag a
segment curves it (Live's own Bézier: a curve drawn here is Live's curve, and back), Alt-double-click straightens,
Draw Mode drags grid-wide steps, a drag on the background selects, Ctrl+C / X / V / D copy, cut, paste (at the mouse,
in any lane) and duplicate, right-click for Edit Value…, Step / Ramp, Clear and Hide. Values 0-127. Every gesture is
one undo step. *View › Automation Lanes* turns the editor off. A **Dynamics** lane replaces the notation's CC1 from its
first point (the notation still sets short notes' velocities). Page View shows no lanes.

### How they reach Live

Max for Live's Object Model can neither write nor read a clip's envelopes or the arrangement's automation (the Clip
has `has_envelopes` and `clear_envelope` only, still in Live 12.4.6; Live's Python API has envelopes for session clips
only, "None for Arrangement clips"). So:

- **Any Live parameter of the part's track** (the owner, 2026-10-02: "the automation display wouldn't only work for
  sso, it works for any midi clip"): the device sends each route's track's parameters (`/live/params`: the mixer's
  volume and pan, every device's, MuseScore Link's left out; again when the track's devices change). A lane on one
  ("live:<device index>/<parameter index>") goes to the device like a plug-in parameter lane, titled by that target,
  and the device drives that parameter of its track (no plug-in needed). Only while Live plays the score: MuseScore's
  own playback (the hosted plug-in) can't play Live's devices (What still differs).

- **MuseScore → Live, while Live plays the score**: the MuseScore Link device sets the parameters itself. MuseScore
  sends each route's parameter lanes (`/ms/params`, `/ms/pvals`: the parameter's title and plug-in id, each value from
  its time on, ramps and curves sampled as MuseScore's renderer plays them); the track's copy of the device fills a
  table (the value in force at each millisecond; before the first point the parameter's own value) and plays it at
  Live's song position (`phasor~ @frequency 7864320 ticks @lock 1` → `index~` → `live.remote~`): sample-timed with
  Live's transport, a start mid-song included. The parameter must be in Live's panel of Kontakt (*Configure*): **Create
  Live Set** puts every lane's parameter there. Live names Kontakt's slots by number ("#001"), so the device matches
  them by the plug-in id MuseScore learnt from a loaded instance (`parameter ids.json` in the setups folder). The status
  line lists a parameter the track's panel lacks. CC lanes (Dynamics, Expression, CC controllers) travel as carriers in
  the clip, as before.
- **Live → MuseScore**: draw the parameter's automation in the track's lane in Live and save the set; with the set
  linked (*Import automation from Live Set…*, *Import again when Live saves it*) MuseScore reads it on each save and
  merges per lane (the newer edit wins). Such a lane is "in Live" (`Lane::playedByLive`): Live plays it, the device
  leaves it alone. Editing it in MuseScore makes MuseScore's version the newer one: the device plays it from then on,
  over Live's track automation, until Live's own changes again (a later save) and the conflict dialog asks which to keep.
  **Both moving one parameter, tried in Live 12.2 (2026-10-01)**: the track automated Kontakt's Vibrato at 0.9; with
  the device driving a MuseScore lane the parameter followed the lane (live.remote~ wins over the automation); when
  the device let go, Live kept the last driven value (0.656) although the automation stayed "active", until
  `re_enable_automation` was called: the device now calls it (150 ms after releasing) for a parameter Live automates,
  and Live's 0.9 came back. Keeping MuseScore's in the dialog leaves Live's automation in the set, overridden while
  the device drives it: delete it in Live (right-click › Delete Automation) so the set shows what plays (the dialog
  says so).
- **Create Live Set** makes every lane MuseScore's (the new set has none of Live's automation; one undo step).

What only real Live could show, and what was shown (the Windows VM, Live 12.2 unauthorized, Kontakt 8, SSO's Solo
Violin; the generated set cut to one track, MuseScore's datagrams sent by a script): the clip and the lane applied
(`/live/papplied ok`); playing from beat 0, the parameter read back each second followed the lane (0.1875, 0.375,
0.5625, 0.75, 0.97 up the ramp, back down, 0 at beat 16, the step 0.75 at 24) and Kontakt's own *Vibrato* slider
moved on screen. Export and freeze, and the lanes kept in the set: below. Not tried: a tempo change in Live while playing (the table
is refilled at Live's tempo, the clips assume one tempo anyway), MuseScore's own GUI against real Live (the Windows
build of this branch).

### Without MuseScore: the lanes kept in the set (2026-10-01)

Each copy of the device keeps its track's lanes, as last applied, in `[pattr Lanes]` … `[pattr Lanes4]`: Live parameters
of type Blob, *Stored Only*, saved in the device's `MxDBlob` next to `Port` (atoms only: `msl-lanes 2 <stamp> <part>
<parts> <length> <routes>` then per route `<key> <hash> <lanes>`, per lane `<title> <id> <atoms>` and its events packed;
at most 30000 atoms a store). When the set opens the stores give the lanes back, the device rebuilds its tables from
them and, as long as MuseScore hasn't sent its own for the track, plays them. **Create Live Set** writes them into the set
(`LiveSetWriter::linkBlob`); the device stores MuseScore's lanes **2 s after its edits pause** (one Live undo step,
"Change in MuseScore Link", per pause; playback follows each edit at once). Lanes needing more than the 4 stores are not
kept (a note in the report; they still play while MuseScore runs).

**Kept without Live undo steps (2026-10-03).** The owner: "if we can achieve possibly zero [undo steps], I'd be all for it".
With the MuseScore Envelopes control surface set up (the clip tabs' lanes need it already), the device keeps a track's
lanes, and the clip tabs' velocity curves (below), **in the track itself** through the script: Live's Python
`Track.set_data(key, value)` ("Store data for the given key in this object. The data is persistent and will be restored
when loading the Live Set"; Song and Track have it, a Clip doesn't, in 12.4.6). Max for Live's Object Model has no
`set_data`, so the device sends the atoms to the script (`/ms/keep/put`, UDP 9005, `core.py` › What MuseScore Link
keeps) and the hub asks for every track's values when the script first answers (`/ms/keep/ping` each second, `/ms/keep/ask`).
Then the `[pattr]` stores are left alone (the lanes go at once, no 2 s wait); a set from an older device, or without the
script, keeps using them as above (the script's value wins when both are there).
What was tried on the test VM (Live 12.4.6 trial, 2026-10-03, a research device `VLStore` and then this device):

| store | undo step | in the saved set | a later value |
|---|---|---|---|
| `[pattr]` Blob, *Stored Only* (the old stores) | yes (one per set) | yes | yes |
| `[pattr]` Blob, *Stored Only*, "Undo When Visible" off | yes | | |
| `[dict]` as a Live parameter (Blob) | yes | | |
| a Float parameter *Stored Only*, "Undo When Visible" off | yes | | |
| a Float parameter *Visible*, "Undo When Visible" off | **no** | yes | (one number a parameter, shown to Push: too small for curves) |
| `[dict]` / `[coll]` with `@embed 1` | no | **no** (only in the device's own file) | |
| Python `Song.set_data` / `Track.set_data` | **no** (`song.can_undo` stayed False) | **yes** (in the track's `ViewData`; 2.7 MB came back) | **yes** |

`set_data` doesn't mark the set as changed (no `*` in the title, *File › Save Live Set* greyed out), so the script then
folds the track's arrangement lane and unfolds it again (`Track.View.is_collapsed` twice: nothing visible changes, no undo
step, the title gets its `*`) and Save saves it. Checked with this device: a velocity curve and a lane set from MuseScore's
messages: `song.can_undo` still False, the title `velA*`, saved; the saved set's MuseScore Link `MxDBlob` had `"Lanes": [ 0 ]`
… (the stores untouched) and the track's `ViewData` both values; Live restarted with the set, no MuseScore: the device
drove the lane's mixer volume again ("seenSerial saved2") and shaped the clip's notes. A track's value moves with the
track (it is the track's); Live's undo doesn't touch it (MuseScore sends its own again when it changes).

**Packed** (`packLane`, the same in `MuseScoreLink.js` and `livesetwriter.cpp`, checked atom for atom): a lane's events
are its points and a ramp's steps (at each tick where the value reaches another 1e-4, a parameter's resolution:
`Automation::PARAM_RESOLUTION`, since 2026-10-03), so a long ramp is thousands of events. Stored: an event `time value`,
or a run of m evenly spaced steps `-m t1 v1 tm vm vh` on the parabola through the first, middle and last step (the
longest run found by doubling, then halving). Played back it is MuseScore's own staircase to within one MIDI step
(1/127: each step's value within half of it, and where its time is more than a tick (8 units) off, MuseScore's
staircase at that time within the other half). (The lane's own points can't be stored instead: repeats and tempo changes are already
unrolled into the events.) A 10-minute piece with 10 lanes, a point every 2 beats, curved and straight ramps: 300 480
event atoms → 49 663 stored (2 of the 4 stores) (`test_params.js`; also on the VM: stored, copied to a duplicated device
whole). Sets saved by the earlier device (`msl-lanes 1`, plain pairs) still load.

Tried in Live 12.2 on the VM: a set with the stores, MuseScore not running, played its Kontakt *Vibrato* lane (read back
each second along the curve); a duplicated device got the stores' value as last set (Live's own parameter state, which a
save writes), the 10-minute data included; five lane updates 0.3 s apart were stored once, and one *Undo* went back to
the value before them. While MuseScore is connected its lanes win: after a Live *Undo* of the stores the device stores
MuseScore's lanes again 2 s later (another undo step). Found on the way: the `v8` box as a Blob parameter
(`getvalueof`/`notifyclients`) is restored from the set but Live never takes its later values (a copy got the old one), so
the store is `[pattr]`; one `[pattr]` set to 34 010 atoms crashed Live (24 010 were fine). Reopening a set Live saved was
tried on 2026-10-02 (below).

**Owner test, done 2026-10-02 (Live 12.4.6 trial, the Windows VM, build 49b00a0, Kontakt 8 with SSO): PASS.** A violin
part with one note tied over 8 bars at 120 bpm and a *Vibrato* lane stepping 0 / 127 every bar (the score's
`automation` tag); *Create Live Set* (2 tracks, "Violin" and "Violin – Solo Violin - Performance", the report says
"2 track(s) keep their automation lanes in the MuseScore Link device"); the set opened in Live, MuseScore (window, *Live
plays the score*) wrote its clips, *File › Save Live Set* (12.4.6 insists on a Project folder: "Save As…"), MuseScore
killed, Live quit, the saved set reopened in a new Live with MuseScore not running: **both devices say "MuseScore Link:
1 plug-in parameter driven"**. Played from 0 (the transport started over the device's OSC, the track's *Vibrato*
parameter read through the LOM about every 0.27 s together with the song time, `start_playing`/`get`/`set` only): 0
until 1.92 s, 1 at 2.19 s, 1 until 3.83 s, 0 at 4.08 s, 0 until 6.01 s, 1 at 6.28 s, 1 until 7.64 s, then 0 at 8.48 s, 1
at 10.10 s, 0 at 12.04 s, 1 at 14.19 s and held (the last point): every switch between the two samples around its bar
line (2, 4, … 14 s), nothing in between, i.e. within the 0.27 s of reading spacing. Nothing seen is 12.4-specific
(only LOM `get`/`set`/`start_playing` were used), but **this set was saved by 12.4.6, which
12.2 can't open: it is not kept (VM only), and Create Live Set's output stays 12.2-openable**.

### Live's export and freeze (2026-10-01)

The device's path is all signal (`phasor~ … @lock 1` → `index~` → `live.remote~`), so it runs inside Live's rendering.
Measured on the VM with Operator's *Volume* driven by a lane (a step every beat, then a ramp; the device before
Operator on a MIDI track), level edges found in the audio:
- **Freeze** (Live's offline render, allowed unauthorized): every edge on its beat within ±1.5 ms (the analysis
  window's resolution); the ramp monotonic. With Kontakt (SSO Solo Violin, *Vibrato* 0/1 every 4 beats) the frozen
  audio's pitch spread alternates 0.9-1.6 Hz / 3.0-3.4 Hz with the lane.
- **Playback** (a take of the track's output recorded in Live): each edge late by the same amount within a take (sd ~1
  ms), but that amount changes from one start of playback to the next: +11, +17, +40, +40, +52 ms (512 and 4096-sample
  buffers, 90 and 120 bpm; the notes themselves on time). The same with the position made from `phasor~` plus
  `[plugphasor~]` (+17 / -0.2 ms in two takes), so it isn't the coarse phasor; `[plugsync~]` outputs nothing in a
  device. Cause not found; for vibrato, mic or release changes 0-50 ms is inaudible, for anything sharp it would not be.
- **Export Audio/Video** can't be tried in 12.2 unauthorized. Freeze renders the same way, so it should match.
  **Owner test, done 2026-10-02 (Live 12.4.6 trial, build 49b00a0): PASS, within what the instrument allows.** The
  score and set of the lanes test above (one note over 8 bars, *Vibrato* 0 / 127 every bar, 120 bpm); MuseScore quit;
  *File › Export Audio/Video*, render start 1.1.1, length 8 bars (16.000 s), WAV 16 bit. The "Violin" track
  (Solo Violin 1) exported **silence** (peak 0.000; in Live its meter stayed at zero too: the "Violin – Solo Violin -
  Performance" track made the sound; why the Violin route is silent was not investigated), so the Performance
  track's export was analysed (a Freeze of the same track as well, its file 18.3 s long). Method: the E5 (659 Hz)
  band-passed, every upward zero crossing's period → the pitch every 1.5 ms; clicks dropped (a 100 ms burst of
  647-676 Hz at 5.80-5.90 s, before bar line 6); the pitch spread = its sd over 200 ms. **Spread per bar (Hz): 0.64
  5.68 0.58 5.41 0.59 7.97 0.96 6.37** (0.2-1.0 where the lane is 0, 5.4-8.0 where it is 127: the lane is played,
  bars 1-8 in order). Each switch's time = the change point of the squared pitch deviation (two levels, best split
  within ±0.9 s of the bar line), offset from the bar line in ms:

  | bar line (s) | 2 | 4 | 6 | 8 | 10 | 12 | 14 |
  |---|---|---|---|---|---|---|---|
  | switch | on | off | on | off | on | off | on |
  | **Export** | +260 | +30 | +635 | +65 | +625 | +25 | +220 |
  | **Freeze** | +165 | +25 | -185 | +105 | +625 | +95 | +210 |

  The *off* switches are the usable ones: **export +25 to +65 ms (mean +40), freeze +25
  to +105 ms (mean +75)**, the same within the method's noise. The *on* switches are not: SSO's vibrato builds up over
  a few hundred ms and starts at a random LFO phase, so they land +165 to +635 ms after the bar line (one -185 ms comes
  from the click above); two renders of the same lane differ by up to 0.4 s there, so only "at the bar line to within
  the vibrato's own rise" can be said. The lane itself was read exactly in the first test (the parameter flips at the
  bar line, ±0.27 s of sampling). A sharper check of the offset needs a parameter that acts at once (Operator's
  *Volume*, the 2026-10-01 freeze test: ±1.5 ms). Seen in 12.4.6 on the way: Save asks for a Project folder
  (Save As…), the export dialog offers that folder, a Freeze lands in its `Samples/Processed/Freeze`; none of it is
  needed from the set MuseScore writes.

### Writing Live's own automation (option B, 2026-10-01)

What Live 12.2 offers, checked in the running program (`dir()` of its Python classes) and the docs:
- **Max for Live (LOM)**: a Clip has `has_envelopes`, `clear_envelope`, `clear_all_envelopes` only; no envelope class, no
  arrangement automation (docs.cycling74.com/apiref/lom, Live 12.3.5). Not in 12.2, not later.
- **Python Control Surface scripts**: `Clip.create_automation_envelope` / `automation_envelope` (Session clips only:
  "Returns None for Arrangement clips"), `Envelope.insert_step / value_at_time / events_in_range /
  delete_events_in_range`; `Envelope.create_event` (a breakpoint with curve coefficients) only from Live 12.4. No API
  writes a track's Arrangement automation. `Track.duplicate_clip_to_arrangement` exists.
- **The way that works in 12.2** (tried on the VM, `tools/live/research/MuseScoreAuto`): a Session clip with an envelope
  for the parameter, `duplicate_clip_to_arrangement` at the lane's start, then both clips deleted: the envelope becomes
  the **track's Arrangement automation** over that span (other spans untouched; it stays after the clips are deleted;
  Live plays it: the track's Pan read back within 0.02 of the curve). 193 points in 136 ms. Ramps are staircases of
  `insert_step`s (12.2 has no breakpoint insertion), i.e. MuseScore's sampled points. Catch: the copy replaces any clip
  on that span (MuseScore's clip was cut in two), so the script must copy MuseScore's whole clip (notes and envelopes)
  over its own span. It needs a Control Surface script installed and chosen once in Live's settings, and it would take
  over what the device's hub does today (it can open a UDP socket as AbletonOSC does).
- **Recording** instead: with *Automation Arm* (`session_automation_record`) and *Arrangement Record* (`record_mode`) on
  while playing, Live recorded nothing from the device: `live.remote~` (documented: no automation, no undo) and LiveAPI
  `set value` (tried: `automation_state` stayed 0). It could only be real time anyway.
- **Clipboard / file tricks**: Live's clipboard isn't reachable; editing the `.als` needs the set closed and reopened.

### Open questions for the owner

- Clip tabs: their lanes are now the clip's own envelopes (Editing Live clips in MuseScore › Automation lanes in a
  clip tab; the owner approved it 2026-10-02). Arrangement clips can't have them through Live's API: write those
  as the track's Arrangement automation with option B's copy trick (it replaces the clip by a copy: new note ids)?

## Live against MuseScore

The owner, 2026-09-30: **"make it a rule that Ableton's audio output and MuseScore's audio output for SSO must
match"** (CLAUDE.md › Rules: Live and MuseScore sound alike). Any change to SSO playback (renderer, hosting, mixer,
controllers, tuning) must reach the Live path too (the clips: notes and carriers; the generated set: Kontakt's state,
the track mixer; the MuseScore Link device) or be listed below as a difference to fix.

### What matches (2026-09-30)

- Everything in the rendering reaches the clips, as the same events: notes and velocities (early legato transitions,
  phrase marks, slur ends: tst_liveequivalence liveClipsLegatoEarly), the switches and every controller on a carrier
  key (UACC, CC1 dynamics, CC11, pedal, the map's CC controllers; 127 exact since 2026-09-30: before, CC11 127 played
  as 126 on every Live note, 0.07-0.08 dB under MuseScore on the VM), the **pitch bend** (keys 115 / 114, 14 bit:
  the microtones of patches with `bend=`, glides included; liveClipsBend).
- The controllers of a part's extra patches (the Performance legato …) come before the notes at their tick, as the
  main patch's (renderer fix, 2026-09-30: they were after the note, so in Live a Performance note on a new dynamic
  started at the old one: +4.0 dB on the VM's first note; MuseScore's own sound is unchanged by it).
- The part's **Controllers** that are plug-in parameters: in each patch's state in the set, and in Live's panel with
  the same value.
- **Automation lanes** (2026-09-30): CC lanes as carriers, plug-in parameter lanes through the device (curves sampled
  as MuseScore's renderer plays them; tst_liveequivalence liveEquivalenceAutomation: residual -56 dB, every note within
  0.01 dB; the 1 ms table is the whole difference). A Dynamics lane in the notation's CC1 place, in both.
- **Volume, pan, mute**: Live's track mixer, the same gains.
- The time: Live plays at the score's first tempo with the notes at their real times (tempo changes, fermatas,
  repeats).

### What still differs

- **Host-only playback settings** (docs/PLAYBACK_SETTINGS.md; playback.ini `[hosting]`): `settleSeconds` (how long a
  freshly loaded patch runs before MuseScore sets its controllers) acts
  in MuseScore's own hosting only; Live loads and mixes the plug-ins itself. Varispeed's glides too (each frame
  within a cent; varispeed can't reach Live); the pitch-bend glides are rendered and reach the clips. Every other
  playback setting acts in the renderer (the clips' events) or, `[hosting] maxVoices`, in the Kontakt states the Live
  Set carries, so Live gets it identically.
- **Microtones on patches without `bend=`** (the All techniques patches: SSO's pitch bend doesn't bend them): MuseScore
  plays them by varispeed on copies of the patch; a clip can't carry that, so in Live they play 12-TET. (The
  Performance patches, Solo Cello and the tuned percussion bend: those match.)
- **A note at the very start** (time 0) waits in Live for its carriers, which can't go before the clip's start: the
  switch, controllers and bend at 0, 0.26 ms each at 120 bpm (1-4 ms). The wait itself is inaudible, but SSO's
  Performance legato script is sensitive to where a line starts: on the VM the first note of a Solo Violin -
  Performance line came out 0.9 dB under MuseScore's and the legato note after it +1.2 dB; MuseScore's own stream
  with only its first note-on 1.4 ms later gives exactly Live's numbers, and the whole stream 1.4 ms later moves
  the first note 0.9 dB as well (the same sensitivity inside MuseScore). The equivalence check leaves the span of
  such notes out of its waveform measure and checks their onsets against the wait.
- **Carriers are spread 0.26 ms apart** (EPSILON, so they come before the note in a known order), and a bend whose
  two halves both change passes for 0.26 ms through a value between: a glide step or a dynamics change while a note
  sounds is up to a few tenths of a millisecond off MuseScore's. On the test synth this is the whole remaining
  difference (residual -32 dB; none without such changes: -327 dB); on SSO's Performance legato, the quarter-tone
  glide notes of the VM score were within 0.36 dB.
- **A carrier value of 1 plays as 0** (the UACC switch: 127 can't be carried, and it is no articulation): 128
  controller values in a note's 127 velocities.
- **MuseScore's automation lanes of plug-in parameters** play in Live through the device (since 2026-09-30), from a
  table of the value in force at each millisecond: a change comes up to 1 ms after MuseScore's (in Live's audio
  blocks, as MuseScore's host applies it in its own). While the device drives a parameter, Live's own automation of
  it is overridden (live.remote~).
- **Lanes on a Live track's own parameters** (an EQ, Operator, a rack's macros: "Live: …") play only in Live: MuseScore's
  host has no Live devices. A clip tab's lanes are the clip's envelopes: Live plays them when it plays the clip, but
  MuseScore's Play in a clip tab (notes sent to the track, the clip not launched) doesn't apply them (it could set the
  parameters as it plays, as the device does for the score; not built).
- **The plain set's technique and group tracks' mixer** (their Volume, Pan, Track Activator and its automation, and
  a second Kontakt track's, livetracks.h): kept in the score and written into the next set, but MuseScore doesn't play
  them; nor any track's mixer automation (a Kontakt's static mixer is the part's Mixer).
- **Controllers and the Mixer changed after the set was written** don't follow: create the set again, or use Live's
  panel and faders.
- The Play Panel's tempo slider (relTempo) and MuseScore's built-in (non-library) parts are not in Live.
- The Mixer's volume 0: silence in MuseScore, -70 dB in Live (Live's lowest fader value).
- Round robins: Kontakt picks its own on each render, in MuseScore and in Live alike (not a difference of the path,
  but no two renders are sample-identical).

### The check (`--live-equivalence`)

`mscore/liveequivalence.h`. MuseScore's own offline render (as an audio export: the patches loaded, script settled,
Controllers set, the Mixer in the host; one route at a time, summed) against "Live": the set MuseScore writes
(planLiveSet) and the clips it sends, turned back into MIDI exactly as the device's patcher does (carriers to CC /
pitch bend, their note-offs dropped), each track's plug-in loaded from the set's embedded state (settled, the
configured parameters set again), Live's mixer, summed; both in seconds. Compared: the whole render (correlation and
residual at the best lag within ±2 ms) and each note on its own track (level over its first 250 ms, the envelope's
onset lag). Thresholds: deterministic (the test synth, `--live-equivalence-strict`): correlation ≥ 0.999, residual
≤ -30 dB, every note ≤ 0.5 dB and ≤ 1 ms; with round robins (Kontakt): notes' median ≤ 1 dB, each ≤ 3 dB, onsets
≤ 5 ms, the whole render reported only.

```
MuseScore3Evo.exe --live-equivalence <folder> <score> [--live-equivalence-wav] [--live-equivalence-strict] [--verify-library <library>]
MuseScore3Evo.exe --create-live-set <out.als> <score> [--verify-library <library>]      (the report as <out.als>.txt)
MuseScore3Evo.exe --live-set-readback <file.als> [--verify-library <library>]           (<file.als> readback.txt)
```
(the working MuseScore's setups and settings; no window). `--live-set-readback` loads each plug-in state of a set into
the library's plug-in, settles it and reads back, by title, the parameters Live's panel lists: what the state holds
before anything sets it.

Tests (`tst_liveequivalence`, the test synth): liveEquivalence (quarter tones by bend with glides, a plug-in and a CC
Controller, volume 80 / pan 32): correlation 0.99970, residual -32.2 dB, every note within 0.02 dB and 0 ms; each
old way fails it (`MS_LIVE_EQUIVALENCE_FAULT`: no bend: correlation 0.50; no mixer: notes 4.6 dB off; the setup
without the Controllers: 8.8 dB off). liveEquivalenceLegato (early legato transitions on an extra patch, two tempi):
bit-identical after the start (residual -327 dB). liveSetControllersAndMix, liveClipsBend, liveClipsLegatoEarly.

### Measured with SSO (the Windows VM, 2026-09-30)

A small score written for it (not the owner's; kept outside the repository): a Solo Violin line with quarter tones
and slurs (Solo Violin 1 and its Performance legato, which bends ±99.6 cents) and a Grand Piano part, 90 bpm, the
Controllers Violin Vibrato 25, Piano Mic 1 level 20 and Mic 3 level 110, the Mixer at violin 88 / pan 31, piano
101 / pan 84 (a MusicXML import's). Kontakt 8 with SSO over the owner's share; builds run 268 (22c33e8) and run
271 (aea4507, with the fixes below).

- `--create-live-set`: 3 tracks (Violin, "Violin – Solo Violin - Performance", Piano), Controllers set in each
  patch's state ("Vibrato 25" on both violin patches, "Mic 1 level 20, Mic 3 level 110" on the piano), volume
  -2.2 dB / pan 26L and +0.2 dB / 16R.
- `--live-set-readback` of that file: each state loaded into a new Kontakt, settled, then read by title: Vibrato
  0.196850 = 25/127 (Live's id 1) on both violin tracks, Mic 1 level 0.157479 = 20/127 (id 7), Mic 3 level 0.866142
  = 110/127 (id 9): each the score's value.
- `--live-equivalence` (build 268, before the fixes of 74ec57f and aea4507): whole render correlation 0.99993,
  residual -36.8 dB; Piano 12 notes within 0.07 dB, 0 ms; the violin's Performance line median 0.08 dB, but its
  first note +4.65 dB and the legato note after it 26 ms off: found to be the controllers after the note (renderer
  fix) and CC11 126 (carrier values), confirmed by replaying the streams through Kontakt (the host below).
- Replayed through Kontakt with the fixed streams (the Performance line alone in a small host; MuseScore's stream
  against the clips as the device plays them): first note -0.9 dB, the legato note after it +1.2 dB (the start's
  wait, above), the glide notes within 0.36 dB, the rest identical.
- `--live-equivalence` with run 271: **PASS**. Whole render correlation 0.99961, residual -31.1 dB (after the start
  span, 3.0 s); the violin's Performance line 11 notes, median 0.01 dB, max 0.44 dB (the legato note after the
  first), onsets 0 ms; the first note -45.63 / -45.66 dB; Piano 12 notes 0.00 dB, 0 ms. (Every headless run with
  Kontakt on the VM exits with 0xC000000D after writing its results, the playback verify's too: Kontakt at exit.)

What the VM can't show: Live itself (not installed there). For the owner: open the set in Live 12.2 (LIVE.md ›
Create Live Set › Only real Live can show), and compare an export from Live with MuseScore's.

The streams were replayed with a small offline VST 3 host on the VM (not in this repository; it plays a list of
"seconds on|off|cc|pb …" lines through one instance loaded from a `.vst3state`); `tst_liveequivalence dumpEvents`
writes those lists for a score (MuseScore's events per route, and the device's MIDI from each clip).

## Editing Live clips in MuseScore

The owner, 2026-09-29: "eventually I want to be able to just edit any midi clip in MuseScore", then
"MuseScore shouldn't try to render a page when editing MIDI, it should just render the midi in the
Ctrl+Shift+V view" (Continuous View, `LayoutMode::LINE`).

Any MIDI clip in Live (not only MuseScore's "MuseScore: …" clips) opens in MuseScore as notation; each edit
there goes back into that clip, note by note. Notes you don't touch keep Live's exact data.

### How to use it

1. The **MuseScore Link** device (the same `.amxd`, regenerated) on any track of the set; MuseScore running
   (with the setting on: *Mixer › Advanced Options… › Ableton Live › Edit Live clips in MuseScore*, on by
   default; it only listens on 127.0.0.1). "Live plays the score" and "Play through Live" don't need to be on.
2. In Live, click the MIDI clip (an arrangement clip: a click on it is enough; a session clip: its notes shown in
   the Clip View).
3. Press **Edit in MuseScore** on the device (any copy of it). A tab "<track> › <clip>" opens in Continuous View
   (an unnamed clip: "<track> › session slot n" or "<track> › arrangement clip"; "(clip)" for the moment before
   the device says where it is).
4. Edit as usual. The status bar says "Live clip <track> › <clip>: in sync · n changes sent" (short, cut to the
   room it has: it never widens the window; the details, e.g. why there are no lanes, in its tooltip). Each edit
   reaches the clip about 0.3 s later. The tab and the window title show no `*` while the tab is in sync with Live
   (every edit written and confirmed): `*` only while an edit waits or is being written, or can't be (a
   conflict, the clip gone, Live not answering). Undo and redo work as always (an undo is written like an edit).
5. **Play** in MuseScore as usual: the notes sound through the clip's own Live track (its instrument and
   effects: a synth rack, Kontakt …), as long as a MuseScore Link copy is on that track (put one before the
   instrument; the status bar says "add MuseScore Link to the Live track … to hear it there" otherwise, and
   MuseScore's own sounds play). MuseScore's Play, Stop and cursor stay MuseScore's: Live's transport, position
   and clips are not touched, so Live may play or stand still meanwhile. Off: *Mixer › Advanced Options… ›
   Ableton Live › Clip tabs play through Live (the clip's own track)* (on by default). A muted track (or one
   in a muted group, or one silenced by another track's solo) is made audible while MuseScore plays and put
   back as it was when MuseScore stops (below, **Audible while MuseScore plays**).
6. Close the tab to stop. *Save* (Ctrl+S) opens no dialog: the status bar says the edits are already in Live and the
   set is saved in Live. An unsaved clip score closes without asking (its edits are in Live already); *Save
   As* makes an ordinary score of it.
7. **The MIDI keyboard** (2026-10-03): while the MuseScore Link device answers, notes from the MIDI input device are
   not sounded by MuseScore, because Live plays them on the selected track (monitoring follows selection). Note
   input from the keyboard still works, silently; clicking or typing notes in MuseScore still sounds. *Mixer › Advanced
   Options › Ableton Live › Live plays my MIDI keyboard* (QSettings `liveIntegration/liveSoundsMidiInput`, on by
   default); off: MuseScore sounds the keyboard too. Test: `tst_liveintegration::midiInputSilent`.

### How it works

- **Reading** (device): in the Arrangement View (`Application.View.focused_document_view` not "Session"), the
  selected track's arrangement clip under the insert marker (`Song.View.selected_track`, `Song.current_song_time`:
  a click on a clip selects its track and puts the marker there; the playhead while playing). The owner, 2026-10-04:
  a track's second clip clicked, the button opened its first: Live's Detail View keeps the clip it last showed, a
  click in the arrangement doesn't change `detail_clip`. Else the clip shown in the Detail View
  (`Song.View.detail_clip`), else the highlighted session slot's clip (`Song.View.highlighted_clip_slot`); `Clip.get_all_notes_extended` (Live 11.1+): every
  note with `note_id`, pitch, start, duration, velocity, mute, probability, velocity deviation, release
  velocity (MPE is not read and stays in the clip); the clip's `signature_numerator/denominator`,
  `loop_start/loop_end`, `end_marker`, `looping`; its track's name (`canonical_parent`, through the
  `ClipSlot` for a session clip) and devices; the song's `tempo`. Sent as `/live/clip/begin` + `/live/clip/notes`
  (protocol 2, chunked like `/ms/notes`; `mscore/liveclipmodel.h` has the messages).
- **The score** (`LiveClipEdit::importClip`): the clip as a Standard MIDI File through MuseScore's MIDI import
  (quantization, voices, tuplets, ties; beat tracking for "human performance" and pickup detection off, so bar
  lines stay on Live's beats). Clip time = score time: clip beat *b* is tick *b* × 480, from the clip's time 0,
  bars up to the clip's end (the later of end marker and loop end). The layout mode is set to Continuous View
  before the import, so no page layout ever runs (the importer doesn't set one; tested: one page, one system).
  Tempo: Live's song tempo (a tempo marking). Time signature: the clip's own. Every bar numbered.
  Lengths as Live has them: no "simplify durations" (it made 0.75-beat notes quarters), quantized to a
  sixteenth, or a 32nd when every start and end is within 1/32 beat of the 32nd grid and one is on an odd 32nd;
  an end snaps to the grid's nearest point (a humanized length within half a step: no tiny rests). Drums keep it.
- **Instrument**: from the track's name (MuseScore's instrument ids, track and long names, compared without
  case, digits and punctuation, plus a few short names: cello, bass, keys …), with its range (notes outside it
  red, as anywhere in MuseScore); else piano (MuseScore's sound while the track has no MuseScore Link) with no
  range (0-127, what MuseScore gives an instrument without one), so a synth's notes are never red.
- **Staves** (the owner, 2026-10-03: "show bass, treble, bass 15mb and treble 15ma staffs whenever there are any
  notes that fall inside them. they should act as ONE STAFF"): every pitched clip gets four staves braced
  together, treble 15ma, treble, bass, bass 15mb, each with one clef for the whole clip (the import's clef
  changes off), and only those with notes on them are shown (Continuous View hides the others: a low bass
  synth shows bass and bass 15mb, a pad treble and bass, an empty clip treble). A note is drawn on the staff
  where it needs the fewest ledger lines; where two need as many (middle C on treble or bass, B5 on treble or
  15ma, D2 on bass or 15mb) on the staff of the chord before it, else the one whose middle line is nearest the
  clip's median pitch. Each note is written on its band's staff, so clicking, range selection (a bar, shift+click),
  copy and paste, and note input work on what is shown, as on a piano's two staves (the owner, 2026-10-04, a Vital
  clip: "select is broken. copy paste is broken. I couldn't enter the first note": the notes then lived on a hidden
  staff, a band staff held only rests, and a note entered there was removed). After every edit (note input, pitch,
  paste; in the same undo step, `assignBands`) a chord whose pitch belongs to another band moves to that staff, the
  same chord object (`MoveToTrack`: it stays selected, keeps its ties, slurs and tuplet), into its voice there or
  another free one; a note entered on the "wrong" staff (a low note typed on treble) is moved the same way. A chord
  whose pitch fits its staff stays (no churn between two equal staves). The staff above the bands (where the import
  writes the notes, the tempo markings' staff) is never shown and keeps no notes. Rests read as on one staff (the owner,
  2026-10-05: "shouldn't there be no rest signs, because they are considered the same staff?"): no rest shows while
  any band staff has a note, and a silence shows one rest, on the staff of the note before it (else the note after
  it; a clip without notes: treble), so the band staves' bars add up together, not each on its own (a band staff's
  first voice holds the rests, other voices' rests hidden; `fillBandRests`). A chord over two bands is split by band into a free
  voice of the other band's staff, the band with most notes staying (View › Show Invisible off in the tab). Limits:
  a chord with ties, a tuplet or grace notes stays whole on one band; a chord (or a tuplet) with no free voice on its
  band's staff, or a chord in a nested tuplet, stays where it is and is drawn on its band cross-staff; a tie or beam
  between notes of two bands is drawn across the staves. The written and
  played pitches are the same (octave clefs change only where a note is drawn), so what is sent to Live and
  played is unchanged. **Drums**: a Drum Rack (`DrumGroupDevice`) on
  the track, or a track named like drum / kit / perc / beat: channel 10 in the file, so the import uses
  MuseScore's drumset (GM pitches: a Drum Rack's C1 = 36 is the kick).
- **Playback** while editing (the owner, 2026-10-02: "if I edit the clip, playback in musescore plays the
  piano, not the synth", then "when I press playback in musescore, it plays through the live plugins, but
  doesn't affect the time cursor in live"): through the clip's own track. MuseScore plays as always (its
  transport, tempo, cursor, loop, count-in); what its sequencer would play for the clip score goes to Live
  instead of its synthesizer (`Seq::playOnLiveTrack`): notes with their velocities, sustain / sostenuto / soft
  pedal and the pitch bend, all on channel 1 (`LiveClipEdit::LiveMidi`; the Mixer's volume, pan, reverb,
  programs and other controllers stay MuseScore's, so the Live instrument keeps its own settings). Each message
  is timed (the period's start + its frame: `livemidiout.h`, a sender thread of its own, the audio thread never
  touches the socket) and sent as `/ms/midi trackId status data1 data2` to the hub, whose **patcher** (not the
  script) passes it on at once: `[route /ms/midi]` → `[forward msl_m<track id>]` → the `[receive]` of the copy
  on that track (named by the script) → `[midiout]`, into the track's chain before the instrument. A MIDI
  effect's output isn't recorded into clips and needs no arming or monitoring; the clip's notes and properties,
  Live's transport, song time and clips are never touched. The hub tells MuseScore each edited clip's track and
  whether a copy of protocol 4 is on it (`/live/clip/track`, after the notes and when it changes); without one
  (or with the setting off, or the link lost) MuseScore's own sounds play and the status line says why. Every
  part of a clip score is "This part plays: MuseScore 3" (since 2026-10-02; was MuseScore 4, whose note model
  plays no note velocities: every note came at the dynamic's 64): each note plays the velocity the import took
  from Live, and a note muted in Live doesn't play (`applyMutes`: the Inspector's *Play* off, no undo step;
  turned on again in MuseScore it is unmuted in Live). Never the sound library. Live 12.3's `Track.insert_device` can't add it: "only native Live devices
  can be inserted. Max for Live devices and plug-ins are not supported" (LOM reference). Notes clicked or
  entered while editing go the same way. Latency: MuseScore sends each note when its own audio for that moment
  is computed (ahead of its output by its buffer), Live plays it after the hop through Max (a few ms, not
  measured in real Live) and its own output buffer; so the sound lags MuseScore's cursor by about Live's output
  latency minus MuseScore's. The clip score is never "the score Live plays" (`LiveClipsLink::setScore` skips it).
- **Audible while MuseScore plays** (the owner, 2026-10-03, option A: "as long as it returns to the previous state
  after MuseScore stops playing"): at Play MuseScore sends `/ms/cliptab/audible 1 <track id>`, again each second
  while it plays (a heartbeat), and `/ms/cliptab/audible 0 <track id>` at Stop, when the tab closes or plays
  elsewhere, and when MuseScore quits (`LiveClipEditor::setAudible`). The hub (`MuseScoreLink.js` › a clip tab's
  track audible) sets the track's `mute` (also its Track Activator) and each group track it is in
  (`Track.group_track`) to 0, and, when another track or return track is soloed and neither this track nor a group
  it is in is, its `solo` to 1. Each property it changed is kept with its old value and the value set (in the
  device's Global, so a new hub can put it back). At `0`, after 7 s without a heartbeat (measured: the device stalls up to 6 s while Live freezes or loads Kontakt tracks; docs/PLAYBACK_SETTINGS.md › Measured by sweeps) (MuseScore gone, the link
  lost), when the track's copy goes, or when the hub is deleted, each goes back to its old value **only if it
  still has the value set**: a mute or solo the user changed meanwhile stays as the user set it. The heartbeat
  sets nothing again. Live's transport, the clip and other tracks' mute are never touched. Solo: Live's
  *Exclusive Solo* preference is applied by its control surfaces (Ableton's own Remote Scripts un-solo the others
  when `song.exclusive_solo` is on), not by the LOM's `solo` setter, so the other soloed tracks stay soloed; should
  Live un-solo one all the same, the device notes it and solos it again at Stop (tested on the stand-in only).
  An older device ignores the message (the track stays as the user left it).
- **Marked as a clip editor** in `LiveClipEditor` only (a runtime property): nothing is written into the file.
  In sync with Live (`LiveClipEditor::inSync`: no edit waiting, none in flight, the envelopes too, no conflict),
  the score's undo stack is marked clean (`UndoStack::setClean`), so the tab and the window title show no `*`;
  `Save` still saves (a clip score is "created": it asks for a file name).
- **The round trip** (`LiveClipEdit::match`, `diff`): after the import each notation note (a tie chain, by its
  first note; grace notes left out) gets the Live note(s) it came from (same pitch, nearest start within a
  beat; two Live notes the import merged share one notation note). Its signature: pitch as played
  (`ppitch`: an 8va line counts), start tick, played length (`playTicks`), velocity (the note's absolute
  velocity, which the import sets), played or not (the Inspector's *Play* = Live's mute). After each edit
  (the score's `playlistChanged`, 300 ms after the last, not inside a command) the signatures now are compared
  with the baseline **by content**, not by object (MuseScore replaces notes on a duration change; undo brings
  old ones back):
  - the same signature: its Live notes stay as they are, nothing is sent;
  - paired by what stayed the same (velocity / play only; pitch only; length only; moved): a **modification**
    of only the fields edited: e.g. a pitch edit keeps the note's humanized start, length, velocity,
    probability …; an edited field is written from the notation exactly (a moved note lands on the grid);
  - a baseline note left over: **removed by id**; a notation note left over: **added** (velocity 100 unless
    the note has one).
  - Live notes outside the clip's time (before 0, after its end) or that no notation note stands for are never
    touched (the status line counts them).
- **Writing** (device): `remove_notes_by_id`, `apply_note_modifications` with Live's own note dictionaries and
  only the edited fields changed (the device keeps Live's doubles; MuseScore sends the edited fields in ticks,
  so nothing is rounded through float32), `add_new_notes` (its ids, else the new notes found by pitch and
  time). All Live 11.0+. Nothing else is called: no replace-all, no envelopes, no clip properties. One write at
  a time, numbered; the device answers `/live/clip/written` with the ids of the added notes and the clip's new
  hash; unanswered after 3 s MuseScore sends it again with the same number, which the device applies once.
- **Conflicts**: the device hashes the clip's notes (every field, FNV-1a) after each read and write and checks
  it once a second (and again before each write). A change made in Live (or Live's undo of a write): 
  `/live/clip/conflict`; nothing more is written. The status line says "conflict" with **Reload from Live**:
  the clip is read again into a new tab that replaces the old one (edits made in MuseScore since the conflict
  are dropped: the owner decides by pressing it). The clip deleted in Live: "the clip is gone".
- **Starting again**: *Edit in MuseScore* on a clip already open brings its tab to the front, or reads it again
  if it changed in Live. `/ms/clip/edit` (from MuseScore) does what the button does (used by tests only).

### Automation lanes in a clip tab (2026-10-02)

The owner, 2026-10-02: lanes drawn in an Edit-in-MuseScore tab "must be written into the Live clip's own envelopes",
"the automation display wouldn't only work for sso, it works for any midi clip".

- **The lanes**: the clip's track's Live parameters, whatever the track plays (a Samples From Mars rack's macros,
  Sampler's, Operator's, Kontakt's configured slots): the mixer's volume and pan, then every device's (*+* has a
  submenu per device). The device sends them (`/live/params`, protocol 5) with the clip's place (`/live/clip/where`:
  the track's index, the session slot's). No sound library or map is involved.
- **Where they are kept**: in the clip, as its envelopes (Live's Clip View › Envelopes shows them, the parameter with a
  red dot). Max for Live can't reach a clip's envelopes, even in Live 12.4.6 (a Clip has `has_envelopes`,
  `clear_envelope`, `clear_all_envelopes` only; calling `automation_envelope` from the device fails: tried), so a small
  **Control Surface script**, `tools/live/MuseScoreEnvelopes`, does it with Live's Python API
  (`Clip.automation_envelope` / `create_automation_envelope`, `Envelope.create_event` (Live 12.4), `events_in_range`,
  `value_at_time`, `delete_events_in_range`, `Clip.clear_envelope`); MuseScore talks to it directly (UDP 9005, its
  protocol in `core.py`). When the tab opens MuseScore reads the clip's envelopes into lanes (no undo step); each edit
  of a lane (300 ms after the last, like the notes) writes the lanes changed since: a lane replaces its envelope, a
  removed lane clears it. One write at a time, sent again after 3 s (applied once). The script hashes the clip's
  envelopes once a second: a change made in Live is a **conflict** like the notes' (*Reload from Live*); a write
  against an older hash is refused.
- **Set up once in Live** (the owner): the script's files go to `<User Library>/Remote Scripts/MuseScoreEnvelopes`;
  MuseScore installs them with the device ([The Live helpers](#the-live-helpers-installed-by-musescore)), by hand: copy
  `tools/live/MuseScoreEnvelopes` (next to MuseScore3Evo.exe in the Windows build) there. Restart Live, then
  *Settings › Tempo & MIDI › Control Surface*: `MuseScoreEnvelopes` in a free row, Input and Output None (by hand:
  Live keeps it in its binary preferences). Without it the tab has no lanes and the status line says how to set it
  up (asked again every 5 s); the first time in a run, while the device answers, a small window says it too
  (*Don't show again*), or offers to install the script when it isn't in the User Library. It listens on 127.0.0.1 only and
  writes `MuseScoreEnvelopes.log` next to itself.
- **What Live 12.4.6 does** (tried on the test VM, 2026-10-02, Operator on a MIDI track): `create_event` takes the
  parameter's value (`events_in_range` gives Live's stored one, which differs for Volume-like parameters: read with
  `value_at_time`); two breakpoints at one time are a jump (`value_at_time` at that time gives the earlier value);
  `delete_events_in_range` includes both ends; a breakpoint's curve (`EnvelopeEventControlCoefficients`) is ignored
  (always read back 0.5, straight). So a step is written as its value again just before the next point, a straight
  ramp as two breakpoints, a **curved ramp as straight pieces nowhere further than one MIDI step (1/127 of the range) from MuseScore's
  curve** (`Automation::flattenCurve`) (read back, the lane has
  those points). Before a lane's first point Live's envelope holds the first value (a MuseScore lane says nothing
  there); after the last both hold it. Played: the clip launched, Operator's *Transpose* read back four times a beat
  followed the envelope (0 → 40 over beats 0-4, -20 from beat 4, again at the loop).
- **Arrangement clips: no lanes.** Live's API gives no envelopes for them (`automation_envelope` None,
  `create_automation_envelope` "Not a session clip", still in 12.4.6); in Live an arrangement clip's device automation is
  the track's anyway. The status line says so. (Option B's copy trick could write the track's automation over the
  clip's span, but it replaces the clip: open question.)
- **MIDI CC lanes**: a clip's *MIDI Ctrl* envelopes (Pitch Bend, CC 1 …) are not device parameters and no API reaches
  them. Live's own **CC Control** device on the track gives CC lanes: its controls are parameters, so they are lanes.
- MuseScore's Play in the tab sends the notes to the track (above) but not the envelopes (What still differs).

### The Velocity lane of a clip tab (2026-10-03)

The owner, 2026-10-03: "I don't like that I can't automate velocity in Live. I want to be able to automate the velocity
in MuseScore, and I can toggle override the existing note velocities vs. use data saved in MuseScore Link." Then: one
curve a clip (scale or absolute a mode of that curve), and in "write" the notes' velocities before the curve kept so it
never scales scaled values.

- **Use**: in a clip tab (session or arrangement clip) *+* › **Velocity** (always offered, first). Draw it like any lane
  (points, ramps, Alt-drag curves, Draw Mode, copy / paste, undo). Before its first point the notes keep their own
  velocities. Right-click the lane:
  - **Scale the Notes' Velocities (0-200 %)** (the default): each note's velocity × 2u, u the lane's value at the note's
    start (the lane's middle: 100 %, unchanged), rounded, 1-127; or **Set the Velocities (1-127)**: round(127 u), 1-127
    (MIDI 1.0: a note-on's velocity has 7 bits and 0 is a note-off). The header shows the unit: "Velocity (%)" or
    "Velocity (1-127)", the value at the cursor "50 %" / "64"; *Edit Value…* asks in those units.
  - **Shape While Playing (Notes Unchanged)** (the default) or **Write into the Notes** (asks first; *Don't ask again*:
    QSettings `liveIntegration/velocityWriteNoAsk`). The header says "shaped" or "written".
  Changing either is an undo step, like an edit of the points.
- **Shape while playing**: the clip's notes stay as they are; the MuseScore Link copy on the clip's track (a MIDI effect
  before the instrument) changes each note-on as it passes, from the curve kept in the device: only while that clip plays
  (session: the clip in the track's playing slot, or a fired one from its launch; arrangement: within the clip's span,
  when the track follows the arrangement), at the note's clip time (its loop included); other clips on the track and other
  tracks are untouched. The curve is kept with the set (above, *Kept without Live undo steps*): the set plays it without
  MuseScore, also after it is opened again. Needs MuseScore Link (protocol 7) on the clip's track; without one the status
  line says the notes play unshaped in Live.
- **Write into the notes**: each note whose velocity the curve changes is written into the Live clip (a velocity-only
  `apply_note_modifications` through the edit path: undo, conflicts, *Reload from Live* as any edit). The notation keeps
  each note's velocity before the curve (its "original"; MuseScore compares Live with the notes as the curve makes them,
  `signaturesForLive`), so editing the curve, or setting it again, writes from the originals. The originals go to the
  device with the curve (by Live's note id, pitch and start, and the velocity written): when the tab opens again, a note
  still at the velocity written gets its original back in the notation; a note changed in Live since takes Live's as its
  original (and the curve over it is written at the next edit); a note without one (added later) keeps its own.
  *Write* → *Shape*: one write puts the originals back, then the device shapes; *Shape* → *Write*: written from the
  originals. A velocity edited in MuseScore is the note's new original.
- **MuseScore's own playback** of the tab (its sounds, or the notes it sends to the Live track) plays every note shaped
  by the lane in both outputs (`rendermidi.cpp` playNote: the part's "velocity" lane), as Live plays the clip. (The
  device's ring holds float32 codes: a product exactly half way between two velocities may round the other way there.)
- **How the device does it** (`MuseScoreLink.js` › velocity curves, `make_device.py`): not in the script (Max's `[v8]`
  runs in the low-priority thread) but in the patcher, in the scheduler: each note that isn't a carrier → `[t l b 0]`
  (the code reset, the song position, the note) → `[snapshot~]` of the song-position phasor → the ring's cell
  (`[expr]`: the tick, plus a bias, modulo the ring) → `[peek~ ---mslv]` → the code into the velocity `[expr]` (0: as it
  is; c ≥ 1: v (c − 1); c < 0: −c). The script fills the ring (`[buffer~ ---mslv]`, one cell a tick of song time, 480 a
  beat) every second and at once when the track's playing or fired slot or the transport changes, from the song position
  two seconds ahead, with what plays on the track then. **Numbers and where they come from**: the ring
  2 × ⌈2 s × 999 BPM / 60 × 480⌉ = 31 968 cells (two fills' windows at Live's fastest tempo: 999 BPM, measured: 12.4.6
  took 20 and 999 and refused 10, 1000, 5000 "Tempo out of range"; the fill every second is the device's heartbeat);
  the bias one signal vector (from `[dspstate~]`): `snapshot~` "reports the sample value in the most recently received
  signal vector" (Max 9's reference) and on the VM every note read exactly 64 samples (the vector) early (0.00218 beats at
  90 BPM, 44.1 kHz) until the bias was added; the launch quantization's grid from Live's `Song.Quantization` /
  `ClipLaunchQuantization` values (read from 12.4.6's Python API); a session clip's clip time from its `start_time`
  ("the time the clip was started", Live's API) and start marker, a legato one from `playing_position`.
- **Protocol 7** (after the clip tempo's 6) (`liveclipmodel.h` › The Velocity lane has the messages): MuseScore asks for the clip's record when the
  tab opens (`/ms/vel/ask`), sends it after each change (`/ms/vel/set`, chunked as the note packets: 24 × 9 atoms), in
  "write" after Live confirmed the notes' write; the hub keeps a track's records in its Global (`kvel<track id>`) and in
  the track (`set_data("musescore_vel")`), and tells the track's copies. A clip is found by its place: the session
  slot's index, or the arrangement clip's start (3840 units a beat), so a curve stays with a clip that isn't moved.
- **Tried in real Live** (the test VM, Live 12.4.6 trial, 2026-10-03; MuseScore's messages sent by a script; the device's
  note log and Live's MIDI Monitor after it): a 4-beat looping session clip, notes on each beat at velocity 100, curve
  50 % from beat 0 and 80 % from beat 2 → 50 50 80 80 in every loop (20 notes), launched while stopped and, quantized to
  the next bar, while playing; another clip on the track: 90 as written; an arrangement clip with an absolute curve
  (64, then 127) → 64 64 127 127, and nothing while the session overrode the arrangement; MIDI Monitor showed C3 50, C#3 50,
  D3 80, D#3 80 (a screenshot, not kept here); the curve read back after Live was restarted with the saved set. The
  final device (this commit's), the saved set opened again without MuseScore: the clip's curve (120 %) played every note
  at 120 over two and a half loops, the track's lane drove its parameter again, `song.can_undo` False.
  Not tried in real Live: MuseScore's GUI (the Windows build), "write" against real Live, a legato launch, follow actions.
- **Tests**: `tools/live/test/test_velocity.js` (the math, a session clip's ring with its loop, another clip left alone,
  a launch to come with the song's and the clip's quantization, arrangement clips, "write", kept by a stand-in of the
  script and given back after a reopen, an ask before the values came, a track's lanes kept by the script and not by the
  stores), `test_patch.js` (the patcher's shaper: codes, the reset, a note-off, carriers untouched, the log),
  `test_envelopes.py` › Keep (the script's side); `tst_liveintegration` clipVelocityLane, clipVelocityWrite,
  clipVelocityReopen, clipVelocityRecord.
- **Owner decisions** (confirmed 2026-10-04): the scale range 0-200 % with 100 % at the lane's middle (the owner's own
  example); "shape" (the notes untouched) as the default output.
### The song's tempo in a clip tab (2026-10-03)

The owner, 2026-10-03: "when I play a midi clip inside musescore, it doesn't respect the song tempo automation in the
main track." Before, a clip tab took Live's tempo once, as a tempo marking, when it opened. Now its tempo markings and
rit. / accel. follow the song (`mscore/cliptempo.{h,cpp}`, the session side in `liveclipedit.cpp`):

| Clip | Tempo in the tab | Changes |
|---|---|---|
| Session clip | Live's current song tempo (no arrangement automation applies) | followed as it changes |
| Arrangement clip, set found, tempo automation | the song's tempo automation under the clip | read again when Live saves the set |
| Arrangement clip, set found, no tempo automation | Live's current song tempo | followed |
| Arrangement clip, set not found (unsaved, saved elsewhere) | Live's current song tempo; a notice asks once | searched again when Live's lists change; *Choose Live Set…* |
| Arrangement clip, device older than protocol 6 | Live's current song tempo | followed |

- **Live's current tempo**: the device's `/live/transport` (each 40 ms while Live plays, once a second while stopped,
  and since protocol 6 at once when the tempo changes). MuseScore puts it in at most every 500 ms (the tab's poll),
  never while MuseScore plays (after it stops), by changing the tab's tempo marking in place: no undo step, nothing
  sent to Live.
- **The song's tempo automation**: no Live API reads arrangement automation, so the saved set is read
  (`libmscore/liveset.*`): the main track's (`MainTrack`, `MasterTrack` before Live 12) `Mixer/Tempo` has `Manual` and
  an `AutomationTarget Id`; the `AutomationEnvelope` pointing at that Id holds the tempo's breakpoints (`FloatEvent`
  Time = song beats, Value = bpm, Live's default event first, curves as `CurveControl…` like any envelope). The
  device says where the clip is in the song (`/live/clip/span`: Live's `start_time`, `end_time`, `start_marker`,
  `end_marker`, `loop_start`, `loop_end`, `looping`). From `start_time` Live plays the clip from its start marker; a
  looping clip goes on from `loop_start` at `loop_end`, until `end_time`.
- **Looping clips**: the tab shows each clip beat once (clip time = score time), Live plays it again in each pass
  under later tempos. **Each beat gets the tempo of the first pass that plays it** (a beat before the start marker:
  the second pass). Later passes are not shown. A beat Live never plays under the clip (before the start marker of a
  clip that doesn't loop, after `end_time`) holds the tempo next to it.
- **In the score**: a tempo marking at the start, where the tempo jumps and where a ramp ends; a ramp (linear in
  beats, as Live's envelope lies on the song's beats) as invisible tempo markings every 32nd (the grid MuseScore's own
  rit. / accel. lines play on, `tempochange.cpp`) at the ramp's tempo there, with the word *accel.* / *rit.* (system
  text) at its start; a curved ramp (Live's Bézier) as straight pieces no further than 0.005 bpm from the curve (half
  of 0.01 bpm, the smallest step Live shows a tempo in; `LiveSet::curve`), read as one *rit.* / *accel.*. Not rit. /
  accel. lines: MuseScore 3 ends a line at the end of the note or rest it ends in (`Spanner::computeEndElement`), while
  Live's breakpoints fall anywhere. The markings are on track 0: with the clip tab's band staves that staff (above the
  bands) is always hidden, the markings are still drawn (tested).
  Times are rounded to the score's ticks (480 a beat). Markings like "♩ = 97.5"
  (two decimals at most, as Live shows a tempo). The marks change no note: the tab sends nothing to Live for them.
  When only values change (a ramp's end tempo, Live's tempo) they change in place without an undo step. A new shape
  replaces them: one undo step once the tab has edits, none before.
- **Which file is the set** (Live's API doesn't say): the candidates, each read in the background and kept only if it
  has the clip (the track at the clip's index, return tracks left out, named as in Live, with an arrangement clip at
  the clip's `start_time` and `end_time`, within the device's float32 rounding):
  1. the set another clip tab found, the sets linked to open scores (*Import automation from Live Set…*), the sets
     chosen or found before (QSettings `liveIntegration/tempoSets`, the latest first);
  2. Live's own lists in each `Live <version>` preferences folder (newest version first; Windows
     `%APPDATA%\Ableton`, macOS `~/Library/Preferences/Ableton`): `Log.txt`'s `Loading document "…"` lines (the
     sets this Live opened, the latest first; Live's Core Library sets left out) and `Preferences.cfg`'s
     `RecentDocsList` (binary; the paths are UTF-16 strings, read as such). Seen in Live 12.4.6 on the test VM: the log
     names the set at each load; the recent list was written at Live's `SavePrefs` (in the log just before Live restarted),
     so a set saved for the first time may be found by the list only later.
  None has the clip: Live's tempo, and a notice once: "… Save the set in Live to use its tempo automation, or choose
  its file." with *Choose Live Set…* (also on the status line while not found). It is looked for again whenever
  Live's `Log.txt` or `Preferences.cfg` changes.
- **Saved again**: the found set's file is watched; 1.5 s after Live writes it (as *Import again when Live saves it*)
  it is read again and the tab's tempo follows. The clip moved in Live: the device sends its new place, the set is
  checked again (a move not yet saved: not found until saved).
- The status line says where the tempo comes from ("tempo: the song's automation", "tempo: Live's (120)", "…, set not
  found"), its tooltip the file and what to do.
- **Not done**: Live's *Re-enable Automation* state (tempo automation overridden by hand: Live plays the manual tempo;
  the tab still shows the automation); the main track's `UserTempoAutomation` element (empty in every set seen) is
  not read; a clip in a group track works like any (Live's API lists group tracks among the tracks, as the set does).

### The Live helpers, installed by MuseScore (2026-10-03)

The owner, 2026-10-03: "whenever MuseScore opens, it tries to see if it can find the correct files copied to the
correct place, and if not it prompts the user to copy it for them." (`mscore/livehelpers.{h,cpp}`)

- **When**: 3 s after MuseScore's window shows (nothing is blocked), and on demand: *Mixer › Advanced Options… ›
  Ableton Live › Install Live helpers…*. Nothing happens without Live 12 on the computer (a `Live 12*` folder in
  ProgramData / Program Files `\Ableton`, an "Ableton Live 12…" uninstall entry, or Live 12's preferences), a User
  Library, or the files next to MuseScore3Evo.exe (so: only the Windows builds).
- **The User Library**: Live's `Library.cfg` (`%APPDATA%\Ableton\Live <version>\Preferences`, the newest version
  whose folder exists; Live 12.2 and 12.4.6 write `<UserLibrary><LibraryProject>` with `ProjectPath` + `ProjectName`),
  else `Documents\Ableton\User Library` (Windows' Documents folder, also when it is in OneDrive). Create Live Set uses
  the same (before 2026-10-03 it read `ProjectPath` alone, one folder too high: the device was still found by the
  search, but its set's `RelativePath` began with "User Library/"; Live then found it by the absolute path).
- **The check**: `MuseScore Link.amxd` → `Presets\MIDI Effects\Max MIDI Effect\`, `MuseScoreEnvelopes\__init__.py`,
  `core.py`, `surface.py` → `Remote Scripts\MuseScoreEnvelopes\`, compared by content (SHA-1).
- **The prompt** (missing or different): a small window, not modal, listing each file, where it goes and why, with
  *Install* (*Update* when one is there in another version), *Not now* (asked at the next start) and *Don't ask
  again* (until a MuseScore comes with other files: QSettings `liveHelpers/dontAsk` holds the shipped files' hash).
  *Install* copies only these files (each replaced whole, atomically; nothing else in those folders is touched), pins
  them in OneDrive when the User Library is there (`attrib +P -U` on the `MuseScoreEnvelopes` folder and on each
  copied file: "Always keep on this device"; a failure is reported, not fatal) and says what Live needs next: a
  restart when Live runs and the script changed; the Control Surface choice when the script is new; for a new
  device version, reopening the sets (they refer to the device's file in the User Library: `livesetwriter.cpp`
  `linkDevice`), while a device dragged in from a MuseScore build folder still points there and is re-added once
  from Live's browser.
- **Not automated**: the Control Surface choice (Live's binary preferences). The hint above says how.
- **Tried on the Windows test VM** (Live 12.4.6 Trial running, build 6946f1f, a fresh settings folder; the VM's own
  copies moved aside and put back after): the prompt came after the Start Center was closed (build db3707b showed it
  behind the application-modal Start Center: fixed), listed the four files "not there yet"; *Install* copied them
  (hashes equal to the build's), said "Live is running: restart Live…" and the Control Surface step; restarted: no
  prompt; a line added to the build's `core.py`: the prompt listed only `core.py` ("another version"), button
  *Update*; *Don't ask again* wrote `liveHelpers/dontAsk` and the next start asked nothing. Not tried there: OneDrive
  pinning (the VM has no OneDrive), the Mixer button, the Control Surface hint, Live reopening a set after the device
  file changed. The Windows builds' `.py` files have CRLF line endings (the CI checkout): a copy taken from the
  repository (LF) counts as another version.

### What is tested, and what only Live can show

Tested here:
- The song's tempo in a clip tab, 2026-10-03: `tst_liveintegration` clipTempoSetRead (`tempo.xml`: a Live 12.4.6
  set's main track as Live wrote it, tracks cut down, tempo breakpoints added by a script: none of Live's making was
  at hand; the envelope, the arrangement clips, which set has the clip, the float32 tolerance), clipTempoMapping (a
  looping clip with its start marker inside the loop: each beat at its first pass; cut short; not looping with a
  start marker; the marks), clipTempoScore (the tempo map at each of a curve's pieces and every 32nd on Live's curve, a ramp,
  a jump; notes untouched and nothing to send; values in place without undo; a new shape as one undo step, undone),
  clipTempoFollowLive (a session clip: Live's tempo in place, no undo step, nothing written), clipTempoArrangement
  (the set found through a `Log.txt` listing a later set without the clip, the markings and words drawn on the band staves, the tempo under a looping clip, read again
  when saved, Live's tempo ignored meanwhile; moved where no set has it: Live's tempo and the reason; the file chosen;
  moved to a session slot), clipTempoLiveLists (`Log.txt` and `Preferences.cfg` as Live 12.4.6 writes them, versions
  newest first); `tools/live/test/test_cliptempo.js` (`/live/clip/span` after `/live/clip/where`, none for a session
  clip, again when the clip moves / loops, `/live/transport` at once on a tempo change).
  Not tried in real Live (the VM's Live was busy with another session): Live's `start_marker` / `loop_*` of an
  arrangement clip as assumed, a tempo envelope Live itself saved (curves especially), a first save's trace in Live's
  lists, MuseScore's GUI (the notice's button, the file watch on Windows).
- Clip tabs, 2026-10-03: `tst_liveintegration` clipTabClean (clean when opened even after an earlier undo step,
  dirty from an edit until `/live/clip/written` ok, clean then; undo: dirty, written, clean, redo still possible; a
  conflict stays dirty; the short status and its details), clipTitleUnnamed ("(clip)", "session slot n",
  "arrangement clip", file-name characters), clipTabAudible (`/ms/cliptab/audible` 1 at start, the heartbeat each
  second, 0 then 1 for another track, 0 at Stop, no heartbeat after); `test_cliptab.js` audible: a muted track
  un-muted and muted again (the heartbeat setting nothing), muted groups, another track or a return track soloed
  (this one soloed, the others left; a soloed group: nothing), an exclusive-solo stand-in (the others soloed again
  at Stop), the user's change during play kept, the heartbeat lost after 4 s, the track's copy or the hub deleted.
  The status bar under Xvfb: the main window's width unchanged with a very long status message and clip status.
- The Live helpers: `tst_liveintegration` liveHelpersLibrary (Live 12.4.6's `Library.cfg` from the test VM with the
  user's name replaced, `mtest/libmscore/liveintegration/Library.cfg`; versions newest first, 12.10 > 12.4.6; the
  Documents fallback; OneDrive paths), liveHelpersInstall (missing / different / up to date, the copy, an update of
  one file, other files untouched; in a temp dir).
- Clip-tab lanes: `tst_liveintegration` clipEnvelopeMapping (points → breakpoints and back, curves, the packets, the
  parameter lists), liveParamLanes (a part Live plays: a "live:" lane to the device titled by its target, nothing in
  MuseScore's own playback), laneTimeAxis (a point under its note's heads, the grid's bar line on the staff's);
  `tools/live/test/test_envelopes.py` (the script against a stand-in Live with envelopes as 12.4.6 behaves: read,
  write, a jump, a Volume-like parameter, the mixer, a quantized one, clear, chunks, once-only, conflict, arrangement,
  gone); `test_envparams.js` (the device: the parameters and the place, a device added, the track moved, an
  arrangement clip, a route's "live:" lanes driving a device's and the mixer's parameter). In real Live 12.4.6 (the VM,
  the script installed, MuseScore's datagrams sent by a script): read, write, read back, a change made in Live →
  conflict and the write refused; the new device's `/live/params` and `/live/clip/where` for a session clip on a track
  without the device; the envelopes in Live's Clip View (Transpose: steps and a ramp; Tone: a 16-piece curve).
  End to end on Linux: a real MuseScore GUI build (Xvfb) against `fake_live_server.js --edit-clip Keys --session` and
  `fake_envelopes_server.py` (the script's own code over the stand-in Live): the tab opened with the clip's Drive
  envelope as a lane ("Synth Rack › Drive"), *+* offered the track's devices as submenus (Mixer, Synth Rack › Cutoff), a
  point clicked into a lane, a new Cutoff lane and its points were each one write of only that lane, *Undo* wrote the
  lane back; a Cutoff breakpoint drawn "in Live": a conflict, and the next edit was not written.
  Not tried: MuseScore's GUI against real Live (a Windows build), a route's "live:" lane in real Live.
- `tst_liveintegration` clipEdit*: the import (Continuous View, one system, part and instrument from the track
  name, bars to the clip's end, tempo, time signature), each Live note matched; no edit → nothing sent; a pitch
  edit → one modification of that id, its humanized start, length, velocity, probability kept; velocity and
  mute; delete (removal by id) and a note written in its place (a modification of the same Live note); an added
  note (its tick, length, velocity 100); a shorter note (length only); a tie chain over the bar line (one Live
  note); a chord (only the edited note's id); added ids and undo (the added note removed by its id, a pitch edit
  undone goes back to Live's pitch); a drum clip (drumset, a snare changed to a clap); names → instruments;
  clipEditBands: the band staves (low: bass + 15mb, wide: treble + bass, very wide: all four, high: 15ma),
  no clef changes, no note out of range, nothing sent; a pitch edit over a band border (the other staff, one
  modification, undo and redo); a chord over two bands split into two voices (nothing sent, then one note's
  edit); notes outside the clip never touched; the write packets. clipTabMidi: what the Live
  track gets (notes, a key held twice released with the last, no Mixer controllers or programs, the pedals and
  bend once per change, MuseScore's stop releasing only what is down) and the `/ms/midi` packet. linkWatch: one
  notice per loss, none while nothing used the link, one when back; what the notices say.
- `tools/live/test/test_cliptab.js` (stand-in Live): each copy names its `[receive]` after its track and says
  protocol 4; the hub's udpreceive into the patcher's `[route /ms/midi]`; the clip's track and copy for an
  arrangement clip, a session clip and a track without the device; a copy removed and added (said once each); an
  older copy not counted; the hub deleted (`/live/bye`), another copy taking over, the clip adopted (in sync, a
  conflict, gone) and written to; nothing touching Live's transport, song time, launching or the clip's markers.
  `test_patch.js`: the patcher's path from `/ms/midi` to the track's `midiout` (another track's copy silent, the
  rest still through `deferlow` to the script). clipEditVelocityAndMute: a muted Live note doesn't play and
  nothing is written for it, played again → unmuted; the clip score renders Live's velocities.
- End to end, clip tabs: a real MuseScore GUI build (Xvfb, PulseAudio null sink) against `fake_live_server.js
  --edit-clip Synth --edit-at 5`, Play clicked: the stand-in's Synth track got each note as `/ms/midi` (first
  19-22 ms after the click, then 500 ms apart at 120 bpm, Live's velocities 87 81 91 70 99, the muted note left
  out); Stop at 1.7 s: the note sounding released (and CC 123) in 5 runs of 5 (one earlier run, before the
  CC 123 safety, missed the release). Nothing else reached the stand-in (`/ms/midi` only: no transport, no song
  time). `--no-copy`: no `/ms/midi`, MuseScore's own sounds. `--gone-at 9 --back-at 22`: "connection lost" 6 s
  after (MuseScore's own sounds from then), back at the new session: the clip adopted, playback through the
  track again; the yellow bar shown while lost (screenshot), the green one on return, each once.
- `tools/live/test/test_clipedit.js` (stand-in Live, `fakelive.js` with note ids, `get_all_notes_extended`,
  `apply_note_modifications`, `remove_notes_by_id`, Song.View): the Detail View's clip with every field; the
  button on a copy that isn't the hub; a session clip in 7/8 on a Drum Rack track; no clip selected; a write by
  id (untouched notes identical, a modified note keeps Live's other fields, the calls used); a write sent twice
  applied once; `add_new_notes` without ids; a change in Live → conflict, writes refused, reload, writes again;
  a change caught at the moment of a write; the clip deleted; the "Live plays the score" work left alone.
- End to end: a real MuseScore GUI build under Xvfb against `fake_live_server.js --edit-clip Violin --edit-at 8`:
  the tab opened in Continuous View ("Violin › Idea", status "in sync"); the first note clicked and raised with
  Up: 7 ms later only that note's pitch had changed in the stand-in's clip (67 → 68), its start 0.013, length,
  velocity 87.3, probability 0.75, velocity deviation and release velocity untouched, the other five notes
  identical. With `--live-change-at 22`: the conflict shown with *Reload from Live*, an edit made meanwhile not
  written, the reload replaced the tab without a question (Live's changed velocity kept), and the next edit was
  written. With "Live plays the score" on as well: the clip score was never sent as a "MuseScore:" clip.

Only real Live can show (to check first):
- that a click on an arrangement clip moves `Song.current_song_time` into it and selects its track (the owner saw
  `detail_clip` stay on the clip shown before, 2026-10-04), and `canonical_parent` of a session clip being its
  `ClipSlot`;
- how Max hands `get_all_notes_extended`'s dictionary to the `v8` script (read as JSON text, an array of it, or
  an object) and `add_new_notes`' list of ids (read as an array, JSON or text; else found by matching);
- that `apply_note_modifications` with a complete note dictionary leaves the note's MPE alone;
- the device's new *Edit in MuseScore* button (`live.text`) sends once per click;
- the hash staying the same between Live's own reads (no float noise), so no false conflicts;
- clip tabs through Live: `Patcher.getnamed("msl_in")` in `v8`, `[receive]` renamed by "set", `[forward]` to it
  from another device, `[sprintf msl_m%ld]` + `[prepend send]` making "send msl_m<id>", `[midiout]` of a MIDI
  effect reaching the instrument after it (not recorded), how soon notes arrive (the hop through Max), and no
  stuck notes when MuseScore stops;
- a clip tab's track made audible: `Track.group_track`, setting `mute` / `solo` from the device (the deferlow
  thread), solo set by the LOM with *Exclusive Solo* on leaving the others soloed, the Track Activator following
  `mute`, and setting the LOM from `notifydeleted` when the hub is deleted.

### Open questions for the owner

- A moved note lands exactly on the notation's grid. The alternative: keep its humanized offset (move by the
  difference).
- The instrument from the track's name, else piano; a drum clip by a Drum Rack or the track's name. Other rules?
- Notes added in MuseScore get velocity 100 when the note has none set.
- A looping arrangement clip shows each beat at the tempo of its first pass. Alternatives: the tab shows every pass
  of the clip under the arrangement (unrolled), or the tempo of a chosen pass.
- A clip tab sends notes, pedals and the pitch bend to its Live track, not the score's dynamics as controllers
  (CC 1 / 11 would change many synths' timbre) nor the Mixer's volume. Should it send any of them?

## What the owner's Live set confirmed, and what it didn't

The .als reader follows the element names that open-source readers use: DawVert, dawtool, and
abletoolz's Live 12 fixtures; the test sets are written by hand
(`mtest/libmscore/liveintegration/liveset.xml`). The owner's own Live 12.2 set (2026-09-29, one
Kontakt 8 track with an SSO patch, Vibrato and Release automated, one segment curved, a clip with a
CC 21 envelope, a second track at "All Ins"; kept outside the repository) was then read:

- **Confirmed:** Kontakt's parameters by their SSO names (Vibrato, Release), values normalized 0-1,
  the curved segment read without error, the clip's CC 21 envelope. Release was missed at first and
  matches since then.
- **Not confirmed:** how Live stores *MIDI From* = one port and channel. The owner's track took its
  input from the computer keyboard, so it was matched by its name. The reader tries the display
  strings ("MuseScore A" / "Ext: MuseScore A", "Ch. 3"), then the target's string, then the name.
- **Not compared:** the curve's exact shape against Live's drawing (read as a cubic Bézier in the
  segment's box, straight pieces within one MIDI step of it).
- `ParameterId` is kept in each lane as `paramId`, not used.

## Kontakt's state from the set (not done)

Each Kontakt device in the set carries Kontakt's own state (`Vst3Preset/ProcessorState`, hex). It is
the same kind of data as MuseScore's `.vst3state` setups: an NI container with a multi that
embeds the patch. It could be loaded into MuseScore's instance so that both hold identical
settings. This is not done, for three reasons:

- **Not needed for SSO's own controls.** SSO's patches name their automation slots in their
  script, so a patch loaded by MuseScore and the same patch loaded in Live have the same named
  slots (the extract of all 700 patches). Matching by name is enough.
- **It would replace MuseScore's made setups** (`KontaktSetup`, "made setups.json", the resave
  after the first load) with a state from outside. Kontakt refused made setups before (run 107)
  and loaded some slowly (run 112), so a foreign state needs its own tries.
- **It would carry Live-side choices into MuseScore:** UACC off if it wasn't set in Live, Live's
  other tweaks, sample paths.

It becomes worth doing if the owner assigns Kontakt host-automation slots to controls SSO doesn't
name. Those assignments live only in Kontakt's state. For that case each lane already keeps
Live's `paramId`.

## Fallback: Tracktion Waveform

From the research of 2026-09-28 (tracktion_engine's source, Tracktion's developer on the JUCE
forum):

- **Waveform cannot follow MIDI clock or Song Position Pointer.**
  - Tracktion's developer (JUCE forum, 2022-04-06): "we can send outgoing MIDI clock but not
    sync to incoming".
  - The engine reads MIDI Timecode (MTC, with drift correction) and MMC on its MIDI inputs, but
    has no incoming clock or SPP.
  - So as a follower it would need **MTC + MMC** from MuseScore instead of clock and SPP, and
    MuseScore doesn't send those yet. MTC is time-based, not beat-based, so a tempo map would
    not travel either: Waveform's edit would have to hold the same tempo map, or be kept in
    seconds.
- **The `.tracktionedit` format is XML**, the engine's ValueTree written out.
  - `EditFileOperations::writeToFile` writes `edit.state.createXml()`. Temporary saves may use
    JUCE's binary ValueTree format, and `loadEditFromFile` reads both.
  - Plug-in automation is an `AUTOMATIONCURVE` element (`paramID`) with `POINT t= v= c=`
    children. `c` = 0 is linear; −0.5 … 0.5 is a Bézier (`getBezierPoint`).
  - Times are in **seconds** for plug-in parameter curves (`TimeBase::time`).
  - The format is not documented as such, but tracktion_engine's source defines it
    (`tracktion_AutomationCurve.cpp`, `tracktion_Identifiers.h`). A reader like `LiveSet` would
    be about as much work, plus seconds → ticks through the score's tempo map.
- **Embedding tracktion_engine instead:**
  - Licence: GPLv3-or-later or commercial.
  - It is a JUCE module (JUCE as a git submodule, C++20) and hosts VST 3 through JUCE.
  - It would bring a second plug-in host and audio engine into MuseScore, next to the VST 3
    hosting this fork already has (`audio/vst3`), mainly for automation editing that MuseScore
    could draw itself.
  - The repository is about 1.6 GB with its history.
  - MuseScore 3's build (Qt 5, C++17 in places) would need C++20 for that module.

In short: as a DAW beside MuseScore, Waveform needs MTC/MMC output from MuseScore. As a library,
tracktion_engine is a large dependency for what the existing hosting plus a lane editor would
do.

## Architecture map

(Moved from CLAUDE.md on 2026-10-04, to keep that file short: agents read it whole.)

`libmscore/midisync.h`, `liveset.*` (automation from a set), `liveclips.*` + `mscore/liveclips.*`
(Live plays the score; carrier notes 114-127), `mscore/liveclipmodel.*` + `liveclipedit.*` (edit Live clips; a clip
tab's tempo follows the song: `mscore/cliptempo.*`, an arrangement clip's from the saved set's main-track tempo automation
(`/live/clip/span`, protocol 6; the set file found via Live's `Log.txt` / `Preferences.cfg`, watched), else Live's tempo; a clip tab plays through its own Live track: `Seq::playOnLiveTrack`, `livemidiout.h`, the copy on that track plays `/ms/midi`; QSettings `liveIntegration/clipTabsPlayLive`; while MuseScore plays, the device un-mutes / solos that track and puts it back: `/ms/cliptab/audible`; a clip tab shows no `*` while in sync with Live; status-bar labels are `mscore/elidedlabel.h`; four band staves acting as one, each chord drawn cross-staff on its band after every command: `makeBandStaves`, `assignBands`, `Score::setEndCmdHook`, `Score::lineHidesEmptyStaves`), the connection-loss notices (`LinkWatch`, `mscore/liveclips.h`),
`libmscore/plainliveset.*` (the plain set), `libmscore/livetracks.*` (the plain set read back: track keys in the
Info text, metaTag `liveTracks`), `libmscore/livesetwriter.*` + `mscore/livesetexport.*` (Create Live Set; compare format changes with
`tools/live/test/compare_als_skeleton.py`), `mscore/liveequivalence.*`, `tools/live/` (Max for Live device, Node
tests, `fake_live_server.js`). Plug-in parameter lanes MuseScore plays reach Live through the device (`/ms/params`,
`/ms/pvals`; tables played by `live.remote~` at Live's song position; Kontakt's slots "#001" matched by
`SoundLibraryHost::knownParameterId`). The device keeps each track's lanes, packed (`packLane`, same in the device
and `livesetwriter.cpp`), in `[pattr Lanes]` … stores in the set, so a set plays them without MuseScore; Create Live
Set writes them; stored 2 s after edits pause. Live's own Arrangement automation can't be written in 12.2 except by
a Control Surface script (`tools/live/research/`, not used). **Lanes of any Live track** (not only SSO, owner
2026-10-02): the device sends each route's and edited clip's track parameters (`/live/params`, protocol 5;
`LiveClipEdit::TrackParams`); a lane `live:<device>/<param>` is that parameter (routes: driven by the device; clip
tabs: `AutomationLanes` offers only these). A **clip tab's lanes are the clip's own envelopes**, written and read by
the Control Surface script `tools/live/MuseScoreEnvelopes` (Python API, Live 12.4 `create_event`; Max for Live can't;
the owner installs it once: LIVE.md › Automation lanes in a clip tab), MuseScore talking to it on UDP 9005
(`liveclipedit.cpp` env*). **Live helpers** (`mscore/livehelpers.*`): at startup (and *Install Live helpers…* in the
Mixer) MuseScore checks Live 12's User Library (its `Library.cfg`) for the device and the script shipped in `bin`
and offers to install / update them (OneDrive-pinned); the Control Surface choice stays manual (a one-time hint). Session clips only (arrangement clips have no envelopes in Live's API, 12.4.6); curves go
as straight pieces within one MIDI step of the curve (Live ignores a breakpoint's curve). Tests: `tools/live/test/test_envelopes.py`,
`test_envparams.js`, `tst_liveintegration` clipEnvelopeMapping / liveParamLanes / laneTimeAxis. A clip tab's
**Velocity lane** (protocol 7, `liveclipmodel.h` › The Velocity lane; LIVE.md): scale 0-200 % or absolute, "shape"
(the device changes note-ons in its patcher from a ring the script fills, per playing clip; `MuseScoreLink.js` › velocity
curves) or "write" (velocity-only edits from the notes' originals); MuseScore's own playback shaped too (rendermidi
playNote). **Kept without undo steps**: with the MuseScore Envelopes script the device keeps a track's lanes and velocity
curves in the track (`Track.set_data` through `/ms/keep`, the set marked changed by folding the track twice); the
`[pattr]` stores only without it. Tests: `test_velocity.js`, `tst_liveintegration` clipVelocity*.

## Files

- `libmscore/midisync.{h,cpp}`: the clock (pure scheduling; tests in `tst_liveintegration`).
- `mscore/seq.{h,cpp}`: `Seq::process` (period clocks, `syncFlush`), `Seq::setPos` (locate).
- `audiodrivers/pm.*`, `pa.*`, `driver.h`: the sync port (`io/portMidi/syncOutputDevice`) and
  `putSync`.
- `libmscore/liveset.{h,cpp}`: the .als reader, and matching to parts and controllers.
- `libmscore/automation.{h,cpp}`: lanes, curves, `Lane::extra`, `source()`, `playedByLive()`, `merge`, `Edit`
  (the editor's operations), `undoWrite`; `mscore/automationlanes.{h,cpp}`: the editor.
- `mscore/liveintegration.{h,cpp}`: the Mixer switch, the import, the link, the watcher.
- `mscore/liveclips.{h,cpp}`: Live plays the score (`LiveClipsLink`, which owns the UDP socket for both).
- `libmscore/livesetwriter.{h,cpp}`: Create Live Set, the writer (and `validate`); `mscore/livesetexport.{h,cpp}`: the
  routes, the device, the states, the dialog. `tools/live/test/compare_als_skeleton.py`: a written set against one
  Live saved.
- `mscore/liveclipmodel.{h,cpp}`: editing Live clips: the import, the baseline, the diff, the messages;
  `mscore/liveclipedit.{h,cpp}`: the sessions, tabs and status line. `mscore/livehelpers.{h,cpp}`: the device and the
  envelopes script checked and installed into Live's User Library.
- `tools/live/`: the device (`MuseScoreLink.js`, `make_device.py`) and its tests (`test/`); `MuseScoreEnvelopes/`:
  the Control Surface script that writes a clip tab's lanes into the clip's envelopes (`test/test_envelopes.py`).
- `mscore/liveequivalence.{h,cpp}`: Live against MuseScore (the check, `readBack`); test `tst_liveequivalence`.
- Tests: `mtest/libmscore/liveintegration`. Fixtures: `liveset.xml` (written by hand, gzipped by
  the test) and `violin-flute.musicxml`.
