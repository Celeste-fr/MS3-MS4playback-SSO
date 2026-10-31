//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __PLAINLIVESET_H__
#define __PLAINLIVESET_H__

//---------------------------------------------------------
//   The plain Live set (LIVE.md › Create Live Set; the owner, 2026-10-06: "I'm done with automatic adjustments
//   altogether … just make musescore export everything in the correct techniques and configured so that it's
//   easiest for me to adjust in ableton"; then "track per technique, with a way to adjust them all at once, and
//   make sure all techniques in one section is collapsible under a single track", "One Kontakt per section",
//   "Plain set, no device"; and "make all techniques in an instrument collapsible and all instruments in a section
//   collapsible at the same time").
//
//   What the set holds, as data (the writer turns it into Live's tracks):
//     Section (a group track: the instruments.xml group of its parts' instruments: Strings, Woodwinds …)
//       Part (a group track: "Violins 1")
//         Kontakt: one per patch the part plays (its main patch, its extras), the library's plug-in on it;
//                  its lanes (the dynamics CC, CC11, the pedal, the pitch bend, the plug-in parameters MuseScore
//                  automates) are automation the owner adjusts there
//           Technique: one MIDI track per articulation the notation chose on that patch ("Long", "Spiccato" …),
//                  its notes, routed into the Kontakt; the switch (UACC CC32, a keyswitch, a program) travels
//                  with the technique's clip, so a note moved to another technique's track plays that one
//
//   The input is MuseScore's own render (MidiRenderer, as "Live plays the score" renders: setForLiveClips), so the
//   techniques are the ones MuseScore plays; the level adjustments are whatever the playback settings say
//   (docs/PLAYBACK_SETTINGS.md). The technique tracks' clips hold the written notes (the owner, 2026-10-11: "Live clips
//   hold the WRITTEN notes"; a second render at written times, MidiRenderer::setWrittenTiming): the timing MuseScore
//   gives them (early starts by onset, keepMs, the note before cut, Performance joins) is put back at playback, the
//   technique track's Track Delay its lookahead earlier and its MuseScore Link copy delaying each note into place
//   (livetiming.h; Technique::timing). The Kontakt track's Controllers clip keeps the controllers where MuseScore plays
//   them (a note's dynamic and CC11 before its played note-on, a bend at it). Microtones (the owner, 2026-10-10: "I will not accept
//   losing microtones"): Live has no varispeed, so Live's routes (SoundLib::routes forLive) give a patch that doesn't
//   bend a copy per tuning, each fixed: its own Kontakt track (Kontakt::cents, named "Violins 1 (−50 cents)"), whose
//   state the export tunes by Kontakt's own Tune (KontaktSetup::withTune; measured with SSO 2026-10-10: ±50 cents
//   within 2 cents, levels within 0.34 dB; in Live 12.4.6 -51.3 / -49.8 cents), its techniques' tracks under it. A
//   bending patch (Performance) keeps MuseScore's lanes, slurred notes on one lane gliding by the bend: a Kontakt per
//   lane too ("Violins 1 - Performance (copy 2)", cents NaN), its bends on its Controllers clip's bend envelope
//   (Live's -8192 .. 8191: MIDI's bend less 8192, measured 2026-10-10). Every tuning's Kontakt has its patch's volume.
//   A patch's copies share one Kontakt (multis; since 2026-10-10): a slot per copy, slot i on MIDI channel i
//   (KontaktSetup::withSlots: the export copies the patch's slot and tunes each), the first copy's techniques and
//   Controllers on the Kontakt track (channel 1), each other copy a MIDI track of its own ("Violins 1 (−50 cents)",
//   its Controllers clip, LiveSetWriter::Track::tuningCopy) with its techniques under it, all MIDI To the Kontakt's
//   channel (Live: "<n>-Kontakt 8"). Measured identical to a Kontakt per copy and ~85-90 MB less each (multis).
//   The Mixer's mute and solo are not in the notes (the owner, 2026-10-06: a part soloed when the set was made left the
//   others' clips silent): a note is muted only where its voice doesn't play (Staff::playbackVoice); a muted part's
//   Kontakt track is written deactivated, a solo is left out (adjusted in Live).
//
//   One limit (said to the owner when choosing): two techniques of one Kontakt starting at the same instant can't be
//   told apart by a single switch controller (Live sends both tracks' switches, then both notes): Layout::clashes
//   lists each such spot for the export to report.
//
//   Times are LiveClips units (UNITS_PER_BEAT a beat, at the score's first tempo: LiveClips::Timeline).
//---------------------------------------------------------

#include <map>
#include <set>
#include <vector>

#include <QString>
#include <QStringList>

#include "liveclips.h"
#include "livetiming.h"
#include "livesetwriter.h"
#include "trackdelays.h"

namespace Ms {

class EventMap;
class Part;
class Score;

namespace SoundLib {
class Library;
struct LibInstrument;
}

namespace PlainLiveSet {

struct Technique {
      QString name;                       // the articulation's name in the map ("Long", "Spiccato"); the patch's
                                          // name when it doesn't switch
      int value { -1 };                   // its switch value (CC value, keyswitch pitch, program); -1: none
      std::vector<LiveClips::Note> notes; // by start
      // the notes as MuseScore plays them (layout with a written render, livetiming.h): the track's Track Delay is
      // timing.lookaheadMs earlier, the MuseScore Link copy on it delays each note into place
      LiveTiming::Track timing;
      };

// a controller's (or the pitch bend's) value from each time on
struct Lane {
      int cc { 0 };                       // the controller; PITCH_BEND: the bend (0-16383, centre 8192)
      std::vector<std::pair<int, int>> points;        // (units, value), by time, a value only where it changes
      std::set<int> glides;               // units of the points a tick after the one before (a glide's steps: Live's
                                          // ramp between them is the step within a tick); every other point is a step
      };
constexpr int PITCH_BEND = -1;

// a plug-in parameter MuseScore automates (Automation: the part's lanes on its patch's <Controller param>)
struct ParamLane {
      QString title;                      // the parameter's title (the map's <Controller param>)
      std::vector<std::pair<int, float>> points;      // (units, value 0-1)
      };

struct Kontakt {
      const SoundLib::LibInstrument* instrument { nullptr };     // the patch
      QString patch;                      // its name
      int port { 0 };                     // the route MuseScore plays it on (SoundLib::Route, Live's)
      int channel { 0 };                  // 0-15
      int patchIndex { 0 };               // 0: the part's main patch, else its extras' index + 1
      std::vector<Technique> techniques;  // in the order of their first note
      std::vector<Lane> lanes;            // by controller, the pitch bend last
      std::vector<ParamLane> params;
      int lane { 0 };                     // the route's tuning lane (SoundLib::Route::lane, Live's: one per tuning)
      int voice { 0 };                    // a monophonic patch's voice (SoundLib::Route::voice): its copy's name says it
      double cents { 0 };                 // its tuning: the Kontakt's Tune (KontaktSetup::withTune, by the export);
                                          // NaN: a bending patch's lane, tuned by its bends
      double gain { 1.0 };                // its whole gain (TrackDelays::patchGain: what MuseScore's slot plays)
      double faderGain { 1.0 };           // of it on its track's Volume (TrackDelays::faderGain: the track levels' headroom)
      double gainStageDb { 0.0 };         // of it in MuseScore's Utility after the Kontakt (TrackDelays::gainStageDb)
      double trimDb { 0.0 };              // (in gainStageDb) the score's master trim
      double lowCutHz { 0.0 };            // [levels] lowCut: MuseScore's EQ Eight after the Kontakt (SoundLib::lowCutHz); 0 none
      double legacyGain { 1.0 };          // its gain as written until 2026-10-15 (TrackDelays::legacyPatchGain): on the fader
      };

struct PartTracks {
      const Part* part { nullptr };
      QString name;                       // the part's name
      std::vector<Kontakt> kontakts;      // the main patch first
      TrackDelays::Delays delays;         // its track delays (trackdelays.h): the group's, the Kontakt's, the techniques'
      };

struct Section {
      QString name;                       // "Strings", "Woodwinds" …
      std::vector<PartTracks> parts;      // in score order
      };

struct Clash {
      QString part;
      QString patch;
      int at { 0 };                       // units
      QStringList techniques;             // those starting there
      };

struct Layout {
      std::vector<Section> sections;      // in the order of their first part in the score
      std::vector<Clash> clashes;
      int length { 0 };                   // units: the played score
      double bpm { 120 };
      };

// the controllers left out of the lanes: the switch, banks and programs, the channel mode messages, and the
// Mixer's (CC7 / CC10 / CC91 / CC93: the Kontakt track's own volume and pan in Live)
bool laneController(int cc, const SoundLib::LibInstrument* instrument);

// a technique's name: the articulation's of the switch value on the patch; the patch's when it doesn't switch
QString techniqueName(const SoundLib::LibInstrument* li, int value);

// a part's Kontakts as Kontakt tracks: indices into kontakts, more than one when a patch's tuning copies share a
// Kontakt (a slot each; a copy with parameter automation alone, the others one Kontakt per headroom, 16 at most)
std::vector<std::vector<size_t>> multis(const std::vector<Kontakt>& kontakts);

// a tuned Kontakt's name suffix: " (−50 cents)"; a bending patch's copy " (copy 2)"; none on lane 0 at 0 cents or bending
QString tuningName(const Kontakt& k);

// the section of an instrument id (instruments.xml's group, a few merged: the percussion groups are "Percussion")
QString sectionName(const QString& instrumentId);

// events: the render as played (MidiRenderer::setForLiveClips); written: the same render at written times
// (MidiRenderer::setWrittenTiming, the owner, 2026-10-11: Live's clips hold the written notes), whose notes the
// techniques get, each with its played times from events (Technique::timing); nullptr: the played notes
Layout layout(const Score* score, const SoundLib::Library& library, const EventMap& events,
              const LiveClips::Timeline& tl, const EventMap* written = nullptr);

// the value a technique's switch controller rests at between its notes: the lowest the patch's articulations don't use
// (see switchClips)
int restValue(const SoundLib::LibInstrument* instrument);

// a technique's clips: its notes and its switch, which goes with the notes. A run: the technique's notes with no other
// technique of the Kontakt starting in between; its switch one unit before the run's first note. Times in beats.
//   - CC32 (Spitfire's UACC): a clip per run, from its switch to the next run's (or its last note's end), Sub = the
//     technique's value: Live sends CC0 0 and CC32 at the clip's start (it keeps no clip envelope on CC0 or CC32).
//   - another CC: one clip the whole song long whose envelope steps from restValue to the technique's value at each
//     run's switch and back one unit after the run's last note starts (Live sends an envelope's value when it changes).
//   - a keyswitch: one clip the whole song long, a key note one unit long at each run's switch.
std::vector<LiveSetWriter::Clip> switchClips(const Kontakt& kontakt, size_t technique, int length);

// a track's key (livetracks.h: what MuseScore knows the track by when the set comes back), written into its Info text
// (Live's Name/Annotation): KEY_PREFIX and its path joined by " / ": the section; section / part; section / part /
// Kontakt track; section / part / Kontakt track / technique. A " / " inside a name is written "/", so the path splits
// back; a key met again in one set gets " (2)", " (3)" …
extern const char* const KEY_PREFIX;            // "MuseScore: "
QString trackKey(const QStringList& path);
QStringList keyPath(const QString& annotation); // the path; empty: not a key

// the set's tracks (LiveSetWriter::Spec::tracks): per section a group, in it a group per part, in that per patch its
// Kontakt track (the part's Mixer; its lanes in a "Controllers" clip, its parameters as automation; the caller adds the
// plug-in, as for the routes' tracks) and a track per technique (switchClips, MIDI To the Kontakt); each with its key
// (trackKey) as its annotation. link: a MuseScore Link copy (Track::link; the device itself: Spec::link) on each technique
// track, its only device, so its MIDI passes through it before it goes To the Kontakt ("Edit in MuseScore" and a clip tab
// playing through Live on any track; tools/live/MuseScoreLink.js passes the notes and controllers on as they are, except
// notes on its carrier keys 114-127, which become controllers: LIVE.md › The plain set). Off: the plain set as before.
std::vector<LiveSetWriter::Track> tracks(const Layout& layout, bool link = false);

}     // namespace PlainLiveSet
}     // namespace Ms
#endif
