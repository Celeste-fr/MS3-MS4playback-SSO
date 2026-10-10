//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appears in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __PERFORMANCETECHNIQUE_H__
#define __PERFORMANCETECHNIQUE_H__

//---------------------------------------------------------
//   PerformanceTechnique
//    how a note plays on a library's Performance legato patch (SSO's "X - Performance", staff text
//    "performance"), which picks its sound by velocity and timing (HANDOFF.md › How the Performance patches
//    work): an attack (a note after silence, or a re-attack: the note before ends 40 ms or more before it) or a
//    transition from the note before (it overlaps, or follows within 39 ms). Each note may set either; Auto
//    plays what the notation says (the renderer, rendermidi.cpp performanceNote: a slur's first note
//    attacks, the notes under it transition, an accent attacks accented, a glissando into a note glides).
//    The velocities and gaps are the map's (<Articulation attacks transitions reattackGapMs overlapMs>).
//
//    Note::performanceAttack / performanceTransition (Pid::PERFORMANCE_ATTACK / _TRANSITION, the Inspector's
//    Note › Performance attack / transition, every selected note at once); a tie chain plays its first note's.
//    The score's metaTag "performanceTechniques" (kept by MuseScore 3.6 through a round trip) holds JSON
//    [{"tick", "track", "grace", "pitch", "attack", "transition", "velocity", "join"}] ("grace": the grace chord's
//    index, left out for the chord itself; the others left out when Auto), written on save from the notes
//    (each score of the file its own: the master score and each part), left out when every note is Auto;
//    read after loading and applied to the notes found (and their linked copies). Copy / paste keeps them
//    (written in the clipboard's XML only).
//
//    A note's own velocity and join (Note::libraryVelocity / libraryJoin, Pid::LIBRARY_VELOCITY / _JOIN; the
//    Velocity and Join lanes, mscore/notelanes.h) replace what the renderer would play: the velocity on any
//    library patch, the join on a Performance patch, where it also decides attack or transition: the note before
//    overlapping it, or a gap up to the map's legatoGap ms (SSO: 39, measured), goes on by transition, a longer gap
//    re-attacks (HANDOFF.md › How the Performance patches work). An Inspector technique sets them back to Auto.
//---------------------------------------------------------

#include <QString>

namespace Ms {

class Score;

namespace PerformanceTechnique {

// the Inspector's order (the combo boxes' indices)
enum class Attack : char { AUTO, SMOOTH, SPICCATO, ACCENTED };
enum class Transition : char { AUTO, PORTAMENTO, FINGERED, BOWED };

extern const char* const metaTag;

constexpr int JOIN_AUTO = -100000;    // a note's join as rendered
constexpr int JOIN_MAX = 2000;        // ms either way (the lane shows less)

const char* attackName(Attack a);               // "smooth", "spiccato", "accented" ("": Auto)
const char* transitionName(Transition t);       // "portamento", "fingered", "bowed" ("": Auto)
Attack attackFromName(const QString& s);
Transition transitionFromName(const QString& s);

// the metaTag: to the notes after loading (and out of the tags), from them on saving; empty: none
void read(Score* score);
QString write(const Score* score);

}     // namespace PerformanceTechnique
}     // namespace Ms
#endif
