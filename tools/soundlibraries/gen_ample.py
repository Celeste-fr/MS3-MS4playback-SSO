#!/usr/bin/env python3
"""Generate share/soundlibraries/Ample Guitar M.xml from a parameter dump of the plug-in.

    python3 tools/soundlibraries/gen_ample.py <agm-info.txt> ["share/soundlibraries/Ample Guitar M.xml"]

The dump is amphost's `info` output for Ample Guitar M (AGM 4.1.0): one line per VST 3 parameter
(index, id=, title, units=, steps=, default=, now=, flags=) and, for some, a `values:` line with the plug-in's own text
at 23 normalized values (i / 22). It stays outside the repository (the owner's machine: ~/MuseScore/ms3fork/private/
ample-map/agm-info.txt); the map holds only what is derived from it: titles, ids, defaults, steps, value texts.

What the map gets:
- every real parameter as a <Controller param=...>: 124 in AGM 4.1.0 (the 16 x 130 "MIDI CC n|cc" proxies that JUCE
  adds for IMidiMapping are left out: MuseScore sends those as MIDI). Id: the title in lower case, other characters
  "-" ("M-Mic 1 Vol" -> "m-mic-1-vol"; the Lite's titles are the same, its VST 3 ids are not: controllers are found by
  title). patchDefault: the dump's default. steps / display: how the plug-in shows the value (README: Controllers).
- the MIDI controllers measured to do something (~/MuseScore/ms3fork/private/ample-full/meas2/RESULT.md): CC7 (the
  same gain as M-Master Vol, 64 = unity), CC1 (vibrato), CC64 (hold pedal: listed, little measured effect). CC7 is
  hosted="m-master-vol": hosted and in Live it plays as M-Master Vol (value cc / 127; measured bit-identical to CC7,
  ~/MuseScore/ms3fork/private/ample-state/MV.md), MIDI out as CC7.
- the sample set (Finger / Pick / Strum), kept only in the plug-in's state (<DBName Value="AGMFg|AGMPk|AGMSt"/>), as a
  state controller (README: Controllers, state=).
- the keyswitches measured (Ample names MIDI 60 "C3", so its C0 is MIDI 24): sustain C0 24, natural harmonic C#0 25
  (sounding 12 over the key: keyShift -12), palm mute D0 26, slide in / out D#0 27, legato slide E0 28, hammer-on /
  pull-off F0 29, slide guitar F#0 30. Pop (C0 + velocity 127) is the instrument's maxVelocity 126: a dynamic never
  plays it by accident; phase 2 plays it from the notation.
"""

import re
import sys
from xml.sax.saxutils import quoteattr

KEYSWITCHES = [
    # name, key, techniques, modifiers, keyShift
    ("Sustain", 24, "long legato short staccatissimo spiccato tenuto marcato longmarcato", "", 0),
    ("Natural Harmonic", 25, "long legato short staccatissimo spiccato tenuto marcato longmarcato", "harmonics", -12),
    ("Palm Mute", 26, "long legato short staccatissimo spiccato tenuto marcato longmarcato", "muted", 0),
    # (the guitar pass plays these from slides and hammer-ons: <Guitar>; slide guitar: no notation asks for it)
    ("Slide In / Out", 27, "", "", 0),
    ("Legato Slide", 28, "", "", 0),
    ("Hammer-On / Pull-Off", 29, "", "", 0),
    ("Slide Guitar", 30, "", "", 0),
]

# Ample's other keys (MIDI numbers; measured in AGM 4.1.0 unless said): for phase 2, written as comments
OTHER_KEYS = [
    ("string forcing, strings 6..1 (velocity 127 also forces the fret position)", "31-36"),
    ("position forcing (C#1, then a position key E1-G#2)", "37; 40-56"),
    ("strummer chord keys (Detect: the chord's notes; Select: slot C1 + n)", "36-59"),
    ("strummer single strings 6..1 of the chord", "62 64 65 67 69 71"),
    ("strum keys: C4 down open, D4 down open, E4 up open, F4 / F#4 mute voices, G4 down muted, A4 down muted, "
     "B4 up muted; velocity bands 1-31 / 32-63 / 64-95 / 96-126 / 127 (pop strum on the open ones)",
     "72 74 76 77 78 79 81 83"),
    ("strum speed (Finger / Pick: gap ~1.31 x (127 - velocity) ms; no effect in the Strum set)", "85"),
    ("chord mode: velocity 127 Detect, 1 Select", "86"),
    ("FX noises: F5 scratch, F#5 slap, G5 muting, G#5 strum mute, A5 / A#5 down / up noise 1, B5 / C6 down / up "
     "noise 2, F6 hit top open, F#6 hit top mute, G6 hit rim", "89-96 101-103"),
    ("strum mode: velocity >= 64 on, <= 63 off (or the S-Strum Toggle parameter)", "97"),
    ("note repeater (manual)", "98"),
    ("auto legato mode (manual: high velocity auto slide, low auto HO/PO)", "99"),
    ("open string first (manual: high velocity on, low off)", "104"),
    ("position mode (manual: low velocity Melody, high Chord)", "107"),
]

SAMPLE_SETS = [("Finger", "AGMFg"), ("Pick", "AGMPk"), ("Strum", "AGMSt")]

# guitar notation's keys and rules (<Guitar>, soundlibrary.h Guitar, played by libmscore/guitarplayback.cpp; phase 2,
# 2026-10-20). Measured in AGM 4.1.0 (2026-10-18, the private measurements meas / meas2 / meas3) unless said:
# - string forcing G0-C1 = strings 6..1 at velocity 127 (127 also forces the fret position);
# - the strummer: strum mode C#6 (velocity >= 64 on, <= 63 off), Detect D5 at 127, the chord's notes in C1-B2 with the
#   root in C1-B1, strum keys C4 down open, E4 up open, G4 down muted, B4 up muted; velocity 96-126 plays all six
#   strings (110: inside the band; the band's level moves only 1-2 dB, its timbre not at all);
# - STRUM_LEVELS: the CC7 at which the recorded Full strum (v110) is as loud as a six-string roll at that velocity
#   (meas3 item 1, "CC7 for equal level"; CC7 is a clean gain, 64 = unity). The Pick set was not measured: Finger's
#   numbers (an assumption: its in-band velocity range is Finger's, meas2 A);
# - softStrum 30: rolls are darker than a CC7-lowered strum only at v <= 30 (meas3); rollGap 15 ms (10-25 ms all lie
#   inside the recorded strums' spread);
# - rollKeys / softRoll ([guitar] softStrums 2): the single-string keys D3-B3 (strings 6..1) in strum mode play the held
#   chord's strings one at a time (meas, meas2 B; no damping at note-off); meas3 item 1's level pairs: a six-string keys
#   roll at velocity 20 / 40 is as loud as the CC7-lowered recorded strum at 10 / 15 ms apart (Strum set: v20 10 ms
#   -29.2 dB vs the CC7-7 strum's -29.1; v40 15 ms -23.0 vs CC7 19's -22.8; Finger within 0.6 dB). The roll takes
#   the chord's velocity held within 20-40 (as StrumLevel holds its ends: below v20 the strum plays at v20's CC7, so
#   does the roll), the spacing linear between the two pairs (an assumption: only the ends measured as pairs);
# - artificial harmonics: string physics (touching k frets above the stopped note sounds partial n: 12 -> 2nd, +12;
#   7 -> 3rd, +19; 5 -> 4th, +24; 4 -> 5th, +28; 3 -> 6th, +31), played as the natural harmonic at that pitch (the only
#   harmonic timbre Ample has, meas);
# - pinch: the 3rd harmonic (+19, the measured targets), the palm-muted fretted note at v60, 2 ms after, 45 ms long
#   (meas3 item 2: closest by numbers to a pinch's profile, one instance; keyswitches latch);
# - capoLead 50 ms: AGM applies an M-Capo change from its next process block only (2026-10-20, amphost, 80 trials at
#   blocks of 64-2048 frames: a note-on in the same block as the change plays at the old capo wherever it lies in the
#   block, one in a later block at the new; a ringing note keeps its pitch). MuseScore's hosting splits its blocks at
#   events, so any lead works there; 50 ms covers a host's fixed blocks up to 2048 frames at 44.1 kHz (Live's buffer);
# - overlap 20 ms (a legato slide / hammer-on needs the two notes to overlap; any overlap measured works: the fork's
#   choice), slideOut 200 ms (the slide out falls within 160 ms, meas), FX keys F5-C6, F6-G6 (all 11 sound, meas).
STRUM_LEVELS = [
    ("AGMSt", "20:8 40:16 60:20 80:32 100:64 120:69"),
    ("AGMFg", "20:6 40:11 60:13 80:27 100:39 120:50"),
    ("AGMPk", "20:6 40:11 60:13 80:27 100:39 120:50"),
]
GUITAR_FX = [
    ("scratch", 89), ("slap", 90), ("muting", 91), ("strum mute", 92), ("down ?noise( 1)?", 93), ("up ?noise( 1)?", 94),
    ("down ?noise 2", 95), ("up ?noise 2", 96), ("hit top( open)?", 101), ("hit top mute(d)?", 102), ("hit rim", 103),
]
GUITAR = [
    ("stringKeys", "36 35 34 33 32 31"), ("stringVelocity", "127"), ("harmonic", "25"), ("harmonicShift", "-12"),
    ("palmMute", "26"), ("slide", "27"), ("legatoSlide", "28"), ("hammer", "29"),
    ("strumMode", "97"), ("strumOn", "100"), ("strumOff", "1"), ("detect", "86"), ("detectVelocity", "127"),
    ("chordLow", "36"), ("strumKeys", "72 76 79 83"), ("strumVelocity", "110"), ("softStrum", "30"), ("rollGap", "15"),
    ("rollKeys", "62 64 65 67 69 71"), ("softRoll", "20:10 40:15"),
    ("sampleSet", "sample-set"), ("capo", "m-capo"), ("capoMax", "11"), ("capoLead", "50"), ("artificial", "3:31 4:28 5:24 7:19 12:12"),
    ("pinch", "19 60 2 45"), ("overlap", "20"), ("slideOut", "200"), ("fxLength", "500"),
]


def guitar_xml():
    out = ["    <!-- guitar notation's keys and rules (libmscore/guitarplayback.cpp; measured, gen_ample.py GUITAR) -->",
           "    <Guitar %s>" % " ".join("%s=%s" % (k, quoteattr(v)) for k, v in GUITAR)]
    for sample_set, levels in STRUM_LEVELS:
        out.append("      <StrumLevel set=%s levels=%s/>" % (quoteattr(sample_set), quoteattr(levels)))
    for text, key in GUITAR_FX:
        out.append("      <Fx text=%s key=\"%d\"/>" % (quoteattr(text), key))
    out.append("    </Guitar>")
    return out

# state-only settings (<Controller state="Parameter:…">; README: Controllers, state=): every setting AGM 4.1.0 keeps in
# its saved state as <Parameter Name Value> and not as a VST 3 parameter (208 of its 330), measured 2026-10-20
# (~/MuseScore/ms3fork/private/ample-state/RESULT.md: one GUI change at a time, the state diffed; hand-edited states
# loaded by setState with no GUI and heard / seen). The value is written as the plug-in shows it (a number as displayed,
# FX toggles 1 / -1). Written only when the part has a lane for it (a change loads the part again, as the sample set);
# unset, the plug-in keeps its own. Settings that are VST 3 parameters stay parameter lanes (Sound Mode, Play Mode, …).
# Each row: (state name without "Parameter:", id, name, kind, spec, default as written in the default state)
#   kind "list": spec = [(written, shown)]; kind "range": spec = (min, max, decimals)
# Ranges and menus not seen in the GUI are the fork's assumptions (marked "assumed" below; a value past the plug-in's
# own range is the plug-in's to clamp or ignore): a Windows check must confirm them (CTL.md).
# Left out: Master Tune (setState stores it and the GUI shows it, but the engine keeps playing at the old tuning until it
# is moved in the GUI: measured 452 Hz -> pitch unchanged, headless and in a GUI host; the fork tunes by its own pitch
# bend), Num Host Params (the plug-in's bookkeeping), S-SEQ Play Tog / T-Play Toggle (transport: play the SEQ / Tab now).
OFF_ON = [("0", "Off"), ("1", "On")]
FX_OFF_ON = [("-1", "Off"), ("1", "On")]          # (the FX panel's toggles: 1 on, -1 off, measured f01 / f08)
NOTES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def numbered(lo, hi, first=None):
    """[(written, shown)] for lo..hi, shown from first (default: as written)"""
    first = lo if first is None else first
    return [(str(v), str(first + v - lo)) for v in range(lo, hi + 1)]


def chord_types():
    # S-Chord Type: 0 Maj, 1 min, 2 7 seen in the Strummer's slots (shots/strum-rootmenu.png); the rest of Ample's type
    # menu was not read: 3-23 assumed, shown by number
    return [("0", "Maj"), ("1", "min"), ("2", "7")] + [(str(v), "type %d" % v) for v in range(3, 24)]


def state_settings():
    rows = [
        # Settings › MIDI / General / Cycle (measured: s10, s20, s21, s22-s24; proofs h05 / h10 heard)
        ("Vel Sensitivity", "vel-sensitivity", "Velocity sensitivity", "range", (0, 100, 0), "100"),
        ("Max Voices", "max-voices", "Max voices", "range", (1, 256, 0), "128"),   # (256: what Doubling sets; the top assumed)
        ("Auto Off Velocity", "auto-off-velocity", "Auto off velocity", "list", OFF_ON, "1"),
        ("Off Velocity", "off-velocity", "Off velocity", "range", (1, 127, 0), "65"),   # (no visible control; 1-127 assumed)
        ("Cycle Mode Switch", "cycle-mode", "Sample cycle", "list",
         [("0", "Separate"), ("1", "Round Robin"), ("2", "Random")], "0"),
        ("Cycle Plus Tog", "cycle-x3", "Cycle X 3", "list", OFF_ON, "0"),
        # Main (m15: the power button by the faders; m09: the note icon by Capo, its meaning not measured)
        ("M-Fret Sound", "fret-sound", "Fret sound", "list", OFF_ON, "1"),
        ("M-Capo Logic Switch", "capo-logic", "Capo logic", "list", [("0", "0"), ("1", "1")], "1"),
    ]
    rows += [("Open String %d" % n, "open-string-%d" % n, "Open string %d" % n, "list", OFF_ON, "1")
             for n in range(6, 0, -1)]          # (all 1 in the default state; meaning not measured)
    # FX › EQ, 8 bands: type (0 / 2 / 4 in the default state: band 2 a 100 Hz cut, band 1 a -3 dB bell, band 3 a 6 kHz
    # cut; 0-4 assumed, shown by number), order (1 / 2 seen; 1-4 assumed), node placed (Activated) and band on (Enabled)
    for n in range(1, 9):
        rows += [
            ("FX-EQ-FilterType%d" % n, "fx-eq-filtertype%d" % n, "EQ band %d type" % n, "list", numbered(0, 4), ""),
            ("FX-EQ-Order%d" % n, "fx-eq-order%d" % n, "EQ band %d order" % n, "list", numbered(1, 4), ""),
            ("FX-EQ-Activated%d" % n, "fx-eq-activated%d" % n, "EQ band %d placed" % n, "list", FX_OFF_ON, ""),
            ("FX-EQ-Enabled%d" % n, "fx-eq-enabled%d" % n, "EQ band %d on" % n, "list", FX_OFF_ON, ""),
        ]
    # FX › Reverb (f06-f09: IR 3 -> 0, Size 100 -> 130, Toggle 1 -> -1, Send -12 -> -23.1; IR 3 shows "Large Hall";
    # the other ranges assumed)
    rows += [
        ("FX-Reverb-Toggle", "fx-reverb-toggle", "Reverb on", "list", FX_OFF_ON, "1"),
        ("FX-Reverb-IRType", "fx-reverb-irtype", "Reverb IR", "list",
         [("0", "IR 0"), ("1", "IR 1"), ("2", "IR 2"), ("3", "Large Hall")], "3"),
        ("FX-Reverb-Size", "fx-reverb-size", "Reverb size", "range", (0, 200, 0), "100"),
        ("FX-Reverb-Send", "fx-reverb-send", "Reverb send (dB)", "range", (-60, 0, 1), "-12"),
        ("FX-Reverb-PreDelay", "fx-reverb-predelay", "Reverb pre-delay (ms)", "range", (0, 200, 0), "0"),
        ("FX-Reverb-BassGainEnabled", "fx-reverb-bassgainenabled", "Reverb bass on", "list", FX_OFF_ON, "-1"),
        ("FX-Reverb-BassGain", "fx-reverb-bassgain", "Reverb bass (dB)", "range", (-12, 12, 1), "0"),
        ("FX-Reverb-TrebleGainEnabled", "fx-reverb-treblegainenabled", "Reverb treble on", "list", FX_OFF_ON, "-1"),
        ("FX-Reverb-TrebleGain", "fx-reverb-treblegain", "Reverb treble (dB)", "range", (-12, 12, 1), "0"),
        ("FX-Reverb-VolumeGain", "fx-reverb-volumegain", "Reverb volume (dB)", "range", (-12, 12, 1), "0"),
    ]
    # FX › Compressor extras (0 in the default state: 0 / 1 assumed; the crest-factor times 0.2 / 1, 0-10 assumed)
    rows += [
        ("FX-Comp-SoftKnee", "fx-comp-softknee", "Compressor soft knee", "list", OFF_ON, "0"),
        ("FX-Comp-RMSDetection", "fx-comp-rmsdetection", "Compressor RMS", "list", OFF_ON, "0"),
        ("FX-Comp-AutoGain", "fx-comp-autogain", "Compressor auto gain", "list", OFF_ON, "0"),
        ("FX-Comp-AutoRelease", "fx-comp-autorelease", "Compressor auto release", "list", OFF_ON, "0"),
        ("FX-Comp-CFAttackTime", "fx-comp-cfattacktime", "Compressor CF attack", "range", (0, 10, 2), "0.2"),
        ("FX-Comp-CFReleaseTime", "fx-comp-cfreleasetime", "Compressor CF release", "range", (0, 10, 2), "1"),
    ]
    # FX › Delay (grid 4, BPM 56, times 1-3 in the default state; ranges assumed). Ample's own names differ between taps
    # 1-2 and 3-6 ("Highcut-Enabled1" / "HighcutEnabled3"): written exactly
    rows += [
        ("FX-Delay-Grid", "fx-delay-grid", "Delay grid", "list", numbered(0, 8), "4"),
        ("FX-Delay-BPM", "fx-delay-bpm", "Delay BPM", "range", (20, 300, 0), "56"),
        ("FX-Delay-BPMSync", "fx-delay-bpmsync", "Delay BPM sync", "list", FX_OFF_ON, "-1"),
    ]
    for n in range(1, 7):
        dash = "-" if n <= 2 else ""
        rows += [
            ("FX-Delay-Activated%d" % n, "fx-delay-activated%d" % n, "Delay tap %d placed" % n, "list", FX_OFF_ON, ""),
            ("FX-Delay-Enabled%d" % n, "fx-delay-enabled%d" % n, "Delay tap %d on" % n, "list", FX_OFF_ON, ""),
            ("FX-Delay-Time%d" % n, "fx-delay-time%d" % n, "Delay tap %d time" % n, "list", numbered(1, 16), ""),
            ("FX-Delay-Highcut%sEnabled%d" % (dash, n), "fx-delay-highcut-enabled%d" % n, "Delay tap %d high cut on" % n,
             "list", FX_OFF_ON, ""),
            ("FX-Delay-Highcut%sFreq%d" % (dash, n), "fx-delay-highcut-freq%d" % n, "Delay tap %d high cut (Hz)" % n,
             "range", (1000, 20000, 0), ""),
            ("FX-Delay-Lowcut%sEnabled%d" % (dash, n), "fx-delay-lowcut-enabled%d" % n, "Delay tap %d low cut on" % n,
             "list", FX_OFF_ON, ""),
            ("FX-Delay-Lowcut%sFreq%d" % (dash, n), "fx-delay-lowcut-freq%d" % n, "Delay tap %d low cut (Hz)" % n,
             "range", (20, 2000, 0), ""),
        ]
    # Strummer: SEQ slot (t12: 0 -> 1 = slot 2), chord slots 1-24 (Select mode: these, not <Chords>, are loaded; proof
    # h20), the Detect mode's chord and range, the page and slot shown. Positions shown 1.. (0 = "1"; 0-7 assumed)
    rows += [
        ("S-Pattern Index", "s-pattern-index", "SEQ slot", "list", numbered(0, 7, 1), "0"),
        ("S-TempoMode", "s-tempomode", "SEQ tempo mode", "list", [("0", "0"), ("1", "1")], "0"),   # (meaning not measured)
        ("S-Chord Index", "s-chord-index", "Chord slot shown", "list", numbered(0, 23, 1), "0"),
        ("S-Chord Page", "s-chord-page", "Chord page", "list", [("0", "1-12"), ("1", "13-24")], "0"),
    ]
    for n in range(1, 25):
        rows += [
            ("S-Chord Root %d" % n, "s-chord-root-%d" % n, "Chord %d root" % n, "list",
             [(str(i), t) for i, t in enumerate(NOTES)], ""),
            ("S-Chord Type %d" % n, "s-chord-type-%d" % n, "Chord %d type" % n, "list", chord_types(), ""),
            ("S-Chord Pos %d" % n, "s-chord-pos-%d" % n, "Chord %d position" % n, "list", numbered(0, 7, 1), ""),
        ]
    rows += [
        ("S-Dt Chord R", "s-dt-chord-r", "Detected chord root", "list", [(str(i), t) for i, t in enumerate(NOTES)], "0"),
        ("S-Dt Chord T", "s-dt-chord-t", "Detected chord type", "list", chord_types(), "0"),
        ("S-Dt Chord P", "s-dt-chord-p", "Detected chord position", "list", numbered(0, 7, 1), "0"),
        ("S-Dt Chord Bass", "s-dt-chord-bass", "Detect bass", "range", (0, 24, 0), "12"),
        ("S-Dt Chord Treble", "s-dt-chord-treble", "Detect treble", "range", (0, 24, 0), "12"),
    ]
    # Tab player (b01-b04 diffed; the rest by name) and Riffer (by name). Time signature / quantize: the indices of the
    # Strummer's S-SEQ Time Sig 0-5 / S-SEQ Quantize 0-6 (assumed the same menus)
    rows += [
        ("T-Swing", "t-swing", "Tab humanize timing", "range", (0, 100, 0), "0"),
        ("T-Vel Hum", "t-vel-hum", "Tab humanize velocity", "range", (0, 100, 0), "0"),
        ("T-Vel Prop", "t-vel-prop", "Tab velocity %", "range", (0, 200, 0), "100"),
        ("T-Strum T Prop", "t-strum-t-prop", "Tab strum time %", "range", (0, 200, 0), "100"),
        ("T-Fixed by Strum", "t-fixed-by-strum", "Tab fixed by Strummer", "list", OFF_ON, "0"),
        ("T-Sync Toggle", "t-sync-toggle", "Tab sync", "list", OFF_ON, "1"),
        ("T-Loop Toggle", "t-loop-toggle", "Tab loop", "list", OFF_ON, "0"),
        ("T-Playback Toggle", "t-playback-toggle", "Tab follows host playback", "list", OFF_ON, "0"),
        ("T-Track Index", "t-track-index", "Tab track", "list", numbered(0, 15, 1), "0"),
        ("R-Feedback", "r-feedback", "Riffer feedback", "list", OFF_ON, "1"),
        ("R-Metronome", "r-metronome", "Riffer metronome", "list", OFF_ON, "0"),
        ("R-Insert Vel", "r-insert-vel", "Riffer insert velocity", "range", (1, 127, 0), "80"),
        ("R-Sync Toggle", "r-sync-toggle", "Riffer sync", "list", OFF_ON, "0"),
        ("R-Sync Host Playback Toggle", "r-sync-host-playback", "Riffer follows host playback", "list", OFF_ON, "0"),
        ("R-Time Sig", "r-time-sig", "Riffer time signature", "list", numbered(0, 5), "2"),
        ("R-Quantize", "r-quantize", "Riffer quantize", "list", numbered(0, 6), "2"),
    ]
    return rows


# the velocity layer thresholds (Settings › MIDI, three points: <Layer ID="1..3" Value="96 / 64 / 32"/>, measured s16 /
# s19; proof h09 heard): state names Layer[ID=n] (pluginstate.h); 1-127 assumed
VELOCITY_LAYERS = [(1, 96), (2, 64), (3, 32)]

# Ample's MIDI-learn defaults (<Control ID="5 + CC" Parameter="…"/>; ID 4 = pitch bend), written into every instance's
# state (<State>, Library::fixedState), so that a preset or setup that dropped one can't silence a lane: the map's CC7
# volume lane and the guitar pass's strum levels as CC7 for MIDI out (an external Ample; hosted and in Live they play as
# M-Master Vol, hosted="m-master-vol"), CC1 vibrato, CC64 hold pedal, and Ample's other defaults. The Live set's state
# gets the same entries (SoundLibraryHost::setupState). Measured: without ID 12, CC7 127 -> 20 changes nothing (h07)
CONTROL_DEFAULTS = [(4, "M-Pitch Bend"), (6, "M-Vibrato"), (7, "M-FA Gain"), (12, "M-Master Vol"), (15, "M-Pan"),
                    (69, "Hold Pedal Tog"), (77, "M-Res Release")]

# the global settings file every AGM instance reads (Documents\Ample Sound\AGM\Configs\AGM_info.xml): MIDI Guitar mode
# on forces channel 1 onto string 6, and every key >= 62 is silent (measured, RESULT.md). Not per part: warned about
MIDI_GUITAR_WARNING = ("Ample Sound/AGM/Configs/AGM_info.xml", "midiGuitarMode", "true 1",
                       "Ample's MIDI Guitar mode is on (Settings › MIDI): keys from D4 (62) up are silent on channel 1. "
                       "Turn it off in Ample's settings")


def state_controller_xml(row):
    name, cid, shown, kind, spec, default = row
    attrs = [("id", cid), ("name", shown), ("state", "Parameter:" + name)]
    if kind == "list":
        n = len(spec) - 1
        attrs.append(("values", " ".join(w for w, _ in spec)))
        if default:
            k = [w for w, _ in spec].index(default)
            attrs.append(("patchDefault", fmt(k / n)))
        attrs.append(("steps", str(n)))
        attrs.append(("display", " ".join("%s:%s" % (fmt(k / n), t.replace(" ", " ")) for k, (_, t) in enumerate(spec))))
    else:
        lo, hi, dec = spec
        attrs.append(("range", "%s %s" % (fmt(lo), fmt(hi))))
        if dec:
            attrs.append(("decimals", str(dec)))
        if default:
            attrs.append(("patchDefault", fmt((float(default) - lo) / (hi - lo), 6)))
        if dec == 0 and hi - lo <= 1000:
            attrs.append(("steps", str(int(hi - lo))))
        attrs.append(("display", "0:%s 1:%s" % ("%.*f" % (dec, lo), "%.*f" % (dec, hi))))
    return "  <Controller %s/>" % " ".join("%s=%s" % (k, quoteattr(v)) for k, v in attrs)


def layer_controller_xml(n, default):
    lo, hi = 1, 127
    attrs = [("id", "velocity-layer-%d" % n), ("name", "Velocity layer %d threshold" % n), ("state", "Layer[ID=%d]" % n),
             ("range", "%d %d" % (lo, hi)), ("patchDefault", fmt((default - lo) / (hi - lo), 6)), ("steps", str(hi - lo)),
             ("display", "0:%d 1:%d" % (lo, hi))]
    return "  <Controller %s/>" % " ".join("%s=%s" % (k, quoteattr(v)) for k, v in attrs)


INSTRUMENT_IDS = "guitar-steel guitar-steel-treble-clef guitar-steel-tablature"

LINE = re.compile(r"^(\d+)\tid=(\d+)\t(.*?)\tunits=(.*?)\tsteps=(\d+)\tdefault=([0-9.]+)\tnow=([0-9.]+) '(.*)'\tflags=(\d+)$")
VALUE = re.compile(r"([0-9.]+)='([^']*)'")


def read_dump(path):
    """the dump's parameters: dicts with index, id, title, steps, default, text (the default's), values [(v, text)]"""
    params = []
    with open(path, encoding="utf-8") as f:
        for raw in f:
            line = raw.rstrip("\n")
            m = LINE.match(line)
            if m:
                params.append({"index": int(m.group(1)), "id": int(m.group(2)), "title": m.group(3),
                               "steps": int(m.group(5)), "default": float(m.group(6)), "text": m.group(8),
                               "flags": int(m.group(9)), "values": []})
            elif line.strip().startswith("values:") and params:
                vals = [(float(v), t) for v, t in VALUE.findall(line)]
                # (23 points at i / 22, printed to 3 decimals: their exact values)
                if len(vals) == 23:
                    vals = [(i / 22, t) for i, (_, t) in enumerate(vals)]
                params[-1]["values"] = vals
    return params


def real_parameters(params):
    return [p for p in params if not p["title"].startswith("MIDI CC ")]


def slug(title):
    return re.sub(r"-+", "-", re.sub(r"[^a-z0-9]+", "-", title.lower())).strip("-")


def number(text):
    try:
        return float(text)
    except ValueError:
        return None


def decimals(text):
    return len(text) - text.index(".") - 1 if "." in text else 0


def fmt(v, places=4):
    s = "%.*f" % (places, v)
    s = s.rstrip("0").rstrip(".")
    return s if s else "0"


def stepped(p):
    """(steps, [(v, text)]) for a parameter with discrete positions, else None"""
    vals = p["values"]
    if p["steps"] == 1:
        if vals:
            off, on = vals[0][1], vals[-1][1]
        elif p["text"] in ("Off", "On"):
            off, on = "Off", "On"
        else:
            return None
        return 1, [(0.0, off), (1.0, on)]
    if not vals:
        return None
    texts = [t for _, t in vals]
    nums = [number(t) for t in texts]
    if any(n is None or "." in t for n, t in zip(nums, texts)):
        return None
    if len(set(texts)) == len(texts):           # every point its own value: continuous (S-Strum Time 1-1000)
        return None
    if any(b < a for a, b in zip(nums, nums[1:])):
        return None
    distinct = sorted(set(nums))
    if distinct != list(range(int(distinct[0]), int(distinct[-1]) + 1)):
        return None
    n = len(distinct) - 1
    out = []
    for k in range(n + 1):
        x = k / n
        nearest = min(vals, key=lambda vt: abs(vt[0] - x))
        out.append((x, nearest[1]))
    # (each position's text: the integers in order)
    if [number(t) for _, t in out] != distinct:
        return None
    return n, out


def continuous_display(p):
    """[(v, text)]: the dump's points, numeric ones thinned to those a straight line between neighbours doesn't give"""
    vals = p["values"]
    if not vals:
        return []
    nums = [number(t) for _, t in vals]
    if any(n is None for n in nums):
        return vals
    kept = [0]
    i = 1
    while i < len(vals) - 1:
        a = kept[-1]
        b = i + 1
        # can the line from a to b give every point between (to a unit of the last decimal: each text is rounded)?
        ok = True
        for j in range(a + 1, b):
            x0, x1, x = vals[a][0], vals[b][0], vals[j][0]
            y = nums[a] + (nums[b] - nums[a]) * (x - x0) / (x1 - x0)
            if abs(y - nums[j]) > 10 ** -decimals(vals[j][1]) + 1e-9:
                ok = False
                break
        if not ok:
            kept.append(i)
        i += 1
    kept.append(len(vals) - 1)
    return [vals[k] for k in kept]


def controller_xml(p):
    attrs = [("id", slug(p["title"])), ("name", p["title"]), ("param", p["title"]), ("patchDefault", fmt(p["default"], 6))]
    st = stepped(p)
    if st:
        attrs.append(("steps", str(st[0])))
        display = st[1]
    else:
        display = continuous_display(p)
    if display:
        attrs.append(("display", " ".join("%s:%s" % (fmt(v), t) for v, t in display)))
    a = " ".join("%s=%s" % (k, quoteattr(v)) for k, v in attrs)
    return "  <Controller %s/>   <!-- VST 3 id %d -->" % (a, p["id"])


def generate(params):
    real = real_parameters(params)
    ids = [slug(p["title"]) for p in real]
    if len(set(ids)) != len(ids):
        raise SystemExit("two parameters with the same id")
    out = []
    w = out.append
    w('<?xml version="1.0" encoding="UTF-8"?>')
    w("<!-- Generated by tools/soundlibraries/gen_ample.py from a parameter dump of Ample Guitar M 4.1.0 (the dump is")
    w("     not in the repository). Do not edit by hand: change the generator and run it again. -->")
    w('<SoundLibrary name="Ample Guitar M">')
    w('  <Switch type="keyswitch"/>')
    w("  <!-- dynamics by velocity only: CC1 is Ample's vibrato -->")
    w('  <Dynamics cc="-1"/>')
    w('  <Plugin files="AGM.vst3"/>')
    w("  <!-- MIDI controllers measured to act (CC7 and M-Master Vol are one gain: 64 = 1.0 = unity, 127 = +6 dB; use")
    w("       one of the two. CC11 and CC12 do nothing measurable). Pitch bend: the M-Pitch Bend parameter, heard")
    w("       only with Poly Bender Tog on. CC7 hosted and in Live as M-Master Vol, value cc / 127 (bit-identical to CC7,")
    w("       applied from the start of the host block holding it, smoothed ~13 ms; Live's clips carry no CC7), MIDI")
    w("       out as CC7 -->")
    # (hosted= only when the dump has the parameter: the map refuses one naming no parameter controller)
    hosted = ' hosted="m-master-vol"' if any(p["title"] == "M-Master Vol" for p in real) else ""
    w('  <Controller id="volume" name="Volume (CC7)" cc="7"%s/>' % hosted)
    w('  <Controller id="vibrato" name="Vibrato (CC1)" cc="1"/>')
    w('  <Controller id="hold-pedal" name="Hold Pedal (CC64)" cc="64"/>')
    w("  <!-- the sample set, kept only in the plug-in's state: written into the part's state as it loads -->")
    w('  <Controller id="sample-set" name="Sample set" state="DBName" values=%s patchDefault="0" steps="%d" display=%s/>'
      % (quoteattr(" ".join(db for _, db in SAMPLE_SETS)), len(SAMPLE_SETS) - 1,
         quoteattr(" ".join("%s:%s" % (fmt(i / (len(SAMPLE_SETS) - 1)), n) for i, (n, _) in enumerate(SAMPLE_SETS)))))
    w("  <!-- every parameter of the plug-in (%d; the MIDI CC proxies left out) -->" % len(real))
    for p in real:
        w(controller_xml(p))
    titles = set(p["title"] for p in real)
    rows = state_settings()
    for r in rows:
        if r[0] in titles:
            raise SystemExit("%s is a VST 3 parameter: a parameter lane, not a state controller" % r[0])
    if len(set(r[1] for r in rows) | set(ids)) != len(rows) + len(ids):
        raise SystemExit("two controllers with the same id")
    w("  <!-- the settings kept only in the plug-in's state (%d; gen_ample.py state_settings; written, as the plug-in shows" % len(rows))
    w("       them, into the part's state as it loads when the part has a lane: a change loads the part again) -->")
    for r in rows:
        w(state_controller_xml(r))
    w("  <!-- the velocity layer thresholds (Settings › MIDI): <Layer ID=\"n\" Value=\"…\"/> in the state -->")
    for n, default in VELOCITY_LAYERS:
        w(layer_controller_xml(n, default))
    w("  <!-- written into every instance's state: Ample's MIDI-learn defaults (CC7 = M-Master Vol: the volume lane and the")
    w("       strum levels rely on it through MIDI out). Master Tune is not a state controller: setState stores it but the")
    w("       sound ignores it -->")
    for cid, param in CONTROL_DEFAULTS:
        w('  <State name="Control[ID=%d]@Parameter" value=%s/>' % (cid, quoteattr(param)))
    f, name, bad, text = MIDI_GUITAR_WARNING
    w("  <!-- a global Ample setting that silences parts (every instance reads it): warned about as a part loads -->")
    w("  <PluginSettings file=%s name=%s bad=%s text=%s/>" % (quoteattr(f), quoteattr(name), quoteattr(bad), quoteattr(text)))
    w('  <Instrument name="Ample Guitar M" ids="%s" maxVelocity="126">' % INSTRUMENT_IDS)
    w("    <!-- keyswitches (Ample's C0 = MIDI 24); velocity 127 on Sustain is Pop, so a dynamic stops at 126 -->")
    for name, key, techniques, modifiers, shift in KEYSWITCHES:
        a = [("name", name), ("value", str(key)), ("techniques", techniques)]
        if modifiers:
            a.append(("modifiers", modifiers))
        if shift:
            a.append(("keyShift", str(shift)))
        w("    <Articulation %s/>" % " ".join("%s=%s" % (k, quoteattr(v)) for k, v in a))
    out.extend(guitar_xml())
    w("    <!-- Ample's other keys, for reference (MIDI numbers):")
    for what, keys in OTHER_KEYS:
        w("         %s: %s" % (keys, what.replace("--", "-")))
    w("    -->")
    w("  </Instrument>")
    # a second (third) instance when a part's staff text asks for another sample set ("pick", "strum"; "finger" back):
    # an extra patch per set, its articulations with the set's modifier (chosen over the main patch's), its own state
    # controller picking its DBName (the part's Sample set lane sets the main patch only). Loaded only where used.
    for i, (set_name, db) in enumerate(SAMPLE_SETS):
        if i == 0:
            continue
        mod = set_name.lower()
        w('  <Instrument name="Ample Guitar M (%s)" with="Ample Guitar M" maxVelocity="126">' % set_name)
        w('    <Controller id="sample-set-%s" name="Sample set (%s instance)" state="DBName" values=%s default="%d" patchDefault="%s"/>'
          % (mod, set_name, quoteattr(" ".join(d for _, d in SAMPLE_SETS)), round(127 * i / (len(SAMPLE_SETS) - 1)),
             fmt(i / (len(SAMPLE_SETS) - 1))))
        for name, key, techniques, modifiers, shift in KEYSWITCHES:
            if not techniques:
                continue
            a = [("name", name), ("value", str(key)), ("techniques", techniques),
                 ("modifiers", (modifiers + " " + mod).strip())]
            if shift:
                a.append(("keyShift", str(shift)))
            w("    <Articulation %s/>" % " ".join("%s=%s" % (k, quoteattr(v)) for k, v in a))
        w("  </Instrument>")
    w("</SoundLibrary>")
    return "\n".join(out) + "\n", len(real)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    out_path = argv[2] if len(argv) > 2 else "share/soundlibraries/Ample Guitar M.xml"
    text, n = generate(read_dump(argv[1]))
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(text)
    print("%s: %d parameters" % (out_path, n))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
