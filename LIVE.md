# Playing through Ableton Live 12

Branch `live-integration` (2026-09-28, 2026-09-29). The owner's goal: MuseScore still plays by itself
exactly as before (Spitfire Symphony Orchestra hosted in Kontakt Player 8). Optionally it plays
through Ableton Live 12 instead. In that mode Live hosts SSO and all automation is drawn in Live. The
automation is then read back into the score, so MuseScore alone plays it the same way. In
MuseScore it is read-only: it is edited only in Live.

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
  Import automation from Live Set…* reads it into the score as read-only lanes
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
- Playback, all parts: *Sound library*.
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
  Plug-in parameter events (MuseScore's own automation lanes) are left out: Live's automation lanes play
  those. The part's *Controllers…* that are plug-in parameters are in each patch's state in the Live Set
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

### Setting it up

1. **Install the device.** Copy `tools/live/MuseScore Link.amxd` (in the Windows build's folder:
   `MuseScore Link.amxd` next to MuseScore3Evo.exe) into Live's User Library, e.g.
   `Documents\Ableton\User Library\Presets\MIDI Effects\Max MIDI Effect`. It needs Max for Live
   (Suite) and Max 9 (Live 12.2 comes with 9.0.7): its script runs in Max's `v8` object.
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
    them again over the state or not, both agree, and they are ready for automation. The report lists per part
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
- **Volume, pan, mute**: Live's track mixer, the same gains.
- The time: Live plays at the score's first tempo with the notes at their real times (tempo changes, fermatas,
  repeats).

### What still differs

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
- **MuseScore's own automation lanes of plug-in parameters** (ME_PARAMETER events) are not in the clips: in Live,
  Live's automation lanes play those (the imported ones come from there).
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
2. In Live, click the MIDI clip so its notes show in the Clip View (arrangement or session clip).
3. Press **Edit in MuseScore** on the device (any copy of it). A tab "<track> › <clip>" opens in Continuous View.
4. Edit as usual. The status bar says "Editing Live clip <track> › <clip>: in sync (n note change(s) sent)".
   Each edit reaches the clip about 0.3 s later.
5. Close the tab to stop. An unsaved clip score closes without asking (its edits are in Live already); *Save
   As* makes an ordinary score of it.

### How it works

- **Reading** (device): the clip shown in the Detail View (`Song.View.detail_clip`), else the highlighted
  session slot's clip (`Song.View.highlighted_clip_slot`); `Clip.get_all_notes_extended` (Live 11.1+): every
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
  Tempo: Live's song tempo (a tempo marking). Time signature: the clip's own.
- **Instrument**: from the track's name (MuseScore's instrument ids, track and long names, compared without
  case, digits and punctuation, plus a few short names: cello, bass, keys …), else piano; a piano gets a
  grand staff (the import's left / right hand split) only when the notes don't fit one clef (treble A3-C6 or
  bass E2-G4). Change it in the Instruments dialog as usual. **Drums**: a Drum Rack (`DrumGroupDevice`) on
  the track, or a track named like drum / kit / perc / beat: channel 10 in the file, so the import uses
  MuseScore's drumset (GM pitches: a Drum Rack's C1 = 36 is the kick).
- **Playback** while editing: MuseScore's own sounds (every part "This part plays: MuseScore 4"): no Kontakt
  instance loaded for a quick edit, nothing sent to Live's tracks. Change it in the Mixer if wanted. The clip
  score is never "the score Live plays" (`LiveClipsLink::setScore` skips it).
- **Marked as a clip editor** in `LiveClipEditor` only (a runtime property): nothing is written into the file.
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

### What is tested, and what only Live can show

Tested here:
- `tst_liveintegration` clipEdit*: the import (Continuous View, one system, part and instrument from the track
  name, bars to the clip's end, tempo, time signature), each Live note matched; no edit → nothing sent; a pitch
  edit → one modification of that id, its humanized start, length, velocity, probability kept; velocity and
  mute; delete (removal by id) and a note written in its place (a modification of the same Live note); an added
  note (its tick, length, velocity 100); a shorter note (length only); a tie chain over the bar line (one Live
  note); a chord (only the edited note's id); added ids and undo (the added note removed by its id, a pitch edit
  undone goes back to Live's pitch); a drum clip (drumset, a snare changed to a clap); names → instruments and
  the grand staff rule; notes outside the clip never touched; the write packets.
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
- `Song.View.detail_clip` for an arrangement clip selected in the arrangement (the LOM says "the clip currently
  displayed in the Detail View"), and `canonical_parent` of a session clip being its `ClipSlot`;
- how Max hands `get_all_notes_extended`'s dictionary to the `v8` script (read as JSON text, an array of it, or
  an object) and `add_new_notes`' list of ids (read as an array, JSON or text; else found by matching);
- that `apply_note_modifications` with a complete note dictionary leaves the note's MPE alone;
- the device's new *Edit in MuseScore* button (`live.text`) sends once per click;
- the hash staying the same between Live's own reads (no float noise), so no false conflicts.

### Open questions for the owner

- A moved note lands exactly on the notation's grid. The alternative: keep its humanized offset (move by the
  difference).
- The instrument from the track's name, else piano; a drum clip by a Drum Rack or the track's name. Other rules?
- Playback of the clip score with MuseScore's own sounds. Should it play through the Live track instead?
- Notes added in MuseScore get velocity 100 when the note has none set.

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
  segment's box, 16 straight pieces).
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

## Files

- `libmscore/midisync.{h,cpp}`: the clock (pure scheduling; tests in `tst_liveintegration`).
- `mscore/seq.{h,cpp}`: `Seq::process` (period clocks, `syncFlush`), `Seq::setPos` (locate).
- `audiodrivers/pm.*`, `pa.*`, `driver.h`: the sync port (`io/portMidi/syncOutputDevice`) and
  `putSync`.
- `libmscore/liveset.{h,cpp}`: the .als reader, and matching to parts and controllers.
- `libmscore/automation.{h,cpp}`: `Lane::extra`, `source()`, `readOnly()`, `replaceSource`.
- `mscore/liveintegration.{h,cpp}`: the Mixer switch, the import, the link, the watcher.
- `mscore/liveclips.{h,cpp}`: Live plays the score (`LiveClipsLink`, which owns the UDP socket for both).
- `libmscore/livesetwriter.{h,cpp}`: Create Live Set, the writer (and `validate`); `mscore/livesetexport.{h,cpp}`: the
  routes, the device, the states, the dialog. `tools/live/test/compare_als_skeleton.py`: a written set against one
  Live saved.
- `mscore/liveclipmodel.{h,cpp}`: editing Live clips: the import, the baseline, the diff, the messages;
  `mscore/liveclipedit.{h,cpp}`: the sessions, tabs and status line.
- `tools/live/`: the device (`MuseScoreLink.js`, `make_device.py`) and its tests (`test/`).
- `mscore/liveequivalence.{h,cpp}`: Live against MuseScore (the check, `readBack`); test `tst_liveequivalence`.
- Tests: `mtest/libmscore/liveintegration`. Fixtures: `liveset.xml` (written by hand, gzipped by
  the test) and `violin-flute.musicxml`.
