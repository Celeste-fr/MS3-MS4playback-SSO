//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __NOTEPREVIEW_H__
#define __NOTEPREVIEW_H__

//---------------------------------------------------------
//   NotePreview
//    a note clicked, entered or typed sounds as playback plays it (the owner, 2026-10-15: "send a clicked note on a
//    library part to its SSO patch, with its technique and the same patch volume and levels playback uses, so a click
//    sounds like that note does in playback, in tone and level"; "the clicked note should match the global playback
//    library"). The part's effective playback mode (partplayback.h: its own, else the global one, playbackmode.h)
//    decides, read at each click (a switch of the mode changes the next click at once):
//
//    MS3      MuseScore 3.6's click: velocity 80 on the part's channel, the single-note-dynamics controller at 80
//             (MuseScore::play, unchanged: 3.6 does so)
//    MS4      the note as MS4 playback renders it on the built-in synthesizer: its note-on (velocity, MS4's channel
//             slot), with the controllers in force on that channel there (MS4's dynamics on CC11, its preset)
//    LIBRARY  a part the library plays (the note's note-on rendered external, soundlibrary.h): the note-on on its
//             route (patch, tuning lane: a varispeed copy or a bending patch's bend) with what is in force on that
//             route at it: the technique switch, CC1 (dynamics), CC11 (levels: calibrated, section balance, real
//             range, mic compensation, even levels, track levels), every Controller, pitch bend. The patch
//             volumes are the slots' (SoundLibraryHost::applyMixer, also while stopped) or, over MIDI out, the
//             DAW's. A part set to the library that the library lacks plays MS4
//
//    The events are taken from the note's playback chunk (MidiRenderer::getChunkAt: the 10-measure chunk Seq
//    renders it in), rendered as Seq renders it, so a click and playback can't differ: "in force" is the last
//    controller / pitch bend / switch on the note-on's channel and route at or before its note-on (events at its
//    own instant too; another note's note-on is no state). Sent all at once, the note-on now (no onset early or
//    start lead: a click starts at once). A tied-on note plays the note its tie starts from; an excerpt's note its
//    master score's. A note playback doesn't sound (play off) gives no events.
//---------------------------------------------------------

#include <vector>

#include <QString>

#include "audio/midi/event.h"
#include "rendermidi.h"

namespace Ms {

class MasterScore;
class Note;
class Part;
class SynthesizerState;

namespace NotePreview {

enum class Sound : signed char { MS3, MS4, LIBRARY };

// the effective mode's note model for the part (rendermidi.cpp's methodOf): MS3 (MuseScore 3.6's dynamics) or MS4
// (MS4 mode, and the library's parts); the global one from the synthesizer's dynamics method (3: MS4)
Sound modelOf(const Part* part, const SynthesizerState& synthState);

struct Preview {
      Sound sound { Sound::MS3 };
      std::vector<NPlayEvent> on;         // in order: each route's (channel's) state in force, then the note-ons
      std::vector<NPlayEvent> off;        // a note-off for each note-on (velocity 0, same channel and route)
      bool empty() const { return on.empty(); }
      };

// the note playback sounds for this one: the master score's, the tie's first
const Note* playedNote(const Note* note);

// from a rendered map holding the notes' chunk (notes: played notes, playedNote); sound: LIBRARY when a note-on
// is external, else MS4
Preview fromEvents(const EventMap& events, const std::vector<const Note*>& notes);

// the chunk of the first note rendered as Seq renders playback (renderer: one for the notes' master score, its
// own: not Seq's playback renderer); MS3 parts: empty, sound MS3 (the caller plays 3.6's click). chunkEvents: the
// rendered chunk (a cache: reused when chunkKey is the chunk's utick and stateKey hasn't changed)
Preview render(MidiRenderer& renderer, const SynthesizerState& synthState, const std::vector<const Note*>& notes,
               EventMap* chunkEvents = nullptr, int* chunkKey = nullptr);

// what a rendered chunk depends on besides the notes' positions: the score's undo position, its metaTags, the
// library, its routes and output, the playback settings, the global mode (synthState's method, the library's
// default): the cache's key
QString stateKey(const MasterScore* score, const SynthesizerState& synthState);

//---------------------------------------------------------
//   Previewer
//    render() with a renderer of its own for one score (Seq's: not its playback renderer, which renders in the
//    background) and the chunk it rendered last, kept while stateKey stays the same: another note of the chunk
//    renders nothing; an edit, a metaTag, the library, its routes or the mode changing renders again at the next
//    click (a switch of the global mode changes the next click's sound, never a stale one)
//---------------------------------------------------------

class Previewer {
      MidiRenderer _renderer { nullptr };
      const MasterScore* _score { nullptr };
      QString _key;
      EventMap _chunk;
      int _chunkKey { -1 };

   public:
      Preview preview(const std::vector<const Note*>& notes, const SynthesizerState& synthState);
      };

}     // namespace NotePreview
}     // namespace Ms
#endif
