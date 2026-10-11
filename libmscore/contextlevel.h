//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __CONTEXTLEVEL_H__
#define __CONTEXTLEVEL_H__

//---------------------------------------------------------
//   The level rule (playback setting [levels] context; the owner, 2026-10-10: the same written dynamic sounds equally
//   loud in every string part and technique, judged in the music, not on isolated notes)
//
//   A note on an articulation with a measured context level (the SSO string sections' Performance Legato:
//   <Articulation contextLevel>, from tools/soundlibraries/performance_context/level_rule.py) plays its dynamics CC
//   at the written dynamic's value (Ms4::expressionLevel: no calibrated CC1 for it, CC1 is a timbre crossfade) and is
//   made louder by volume only: one gain per phrase (Articulation::contextLevelAt of the phrase's dynamic, median note
//   spacing and median pitch; a phrase is the slur, not a phrase mark, the note is in on its staff, else its chord
//   alone), so no level step inside a slur.
//
//   The gain is up to about 21 dB (Celli pp), more than CC11 can add (CC11 rests at 127). So a score's largest gain
//   G (every slur of every part with such an articulation, with [slurs] performance on: a superset of the slurs that
//   play on it; a note on it outside a slur, by staff text "performance", louder than G plays at G) is shared out
//   (volumes()):
//     - the patches with the articulation (the Performance patches) all play at the same volume P = G - S,
//     - every other patch of the library's parts (All techniques, and any other section) plays S = max(0, G - 6) dB
//       lower together (6: Live's track Volume top, TrackDelays::MAX_DB),
//   and each phrase plays gain - G by CC11 (<= 0). The volumes go through TrackDelays::patchGain (the Kontakt slot's
//   volume in MuseScore, LiveSetWriter::kontaktVolume / the plain set's Kontakt track Volume in Live); the CC11 goes
//   through the renderer's per-note levels (libraryNoteLevels), so Live's clips and the plain set carry it.
//
//   The section balance (playback setting [levels] sectionBalance; the owner, 2026-10-11: A′, the rule on the owner's
//   Live set R, approved and preferred over the owner's own balance): every patch of a section with a map level O
//   (<Instrument sectionLevel>, dB: SSO's five string sections, R's All techniques faders by ear plus the rule's S on
//   Whence 12-TET, gen_spitfire_sso.py SECTION_FADERS) plays O dB louder, staccatos and Performance alike, by the same
//   volumes. With the rule a phrase plays gain + O, a staccato O, everything less S' = max(0, the loudest phrase's
//   gain + O - 6): MuseScore plays A′ less one common constant. A part's Performance patch carries T = min(G, S' + 6 - O)
//   (its phrases' CC11: gain less T; G where it fits under +6, so the CC11 values are the rule's). Off: the rule alone.
//---------------------------------------------------------

#include <map>
#include <QString>

namespace Ms {

class Chord;
class MasterScore;
class Part;
class Score;
namespace Ms4 { class Dynamics; }
namespace SoundLib { struct Articulation; struct LibInstrument; class Library; }

namespace ContextLevel {

// whether the setting is on for the score
bool on(const Score* score);
// whether [levels] sectionBalance is on for the score
bool sectionBalance(const Score* score);
// whether the articulation has a measured context level
bool has(const SoundLib::Articulation* a);
// whether a patch has an articulation with one (a Performance patch)
bool has(const SoundLib::LibInstrument* patch);

// the gain in dB of the phrase the chord is in (above), its dynamic from dynamics at the phrase's first chord
// (utick = tick + tickOffset)
double gain(const SoundLib::Articulation& a, const Chord* chord, const Ms4::Dynamics& dynamics, int tickOffset);

struct Volumes {
      std::map<QString, double> patchDb;    // patch -> its volume in dB (P or -S; section balance: O (+ the part's G) - S')
      double top { 0.0 };                   // G (section balance: min(G, S' + MAX_DB - O))
      };
// per library part (none: both settings off, no library, or the rule alone and no phrase on such an articulation)
std::map<const Part*, Volumes> volumes(const MasterScore* score);

}     // namespace ContextLevel
}     // namespace Ms
#endif
