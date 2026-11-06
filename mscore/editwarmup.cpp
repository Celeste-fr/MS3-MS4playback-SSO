//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "editwarmup.h"

#include "libmscore/contextlevel.h"
#include "libmscore/lagprobe.h"
#include "libmscore/recentresults.h"
#include "libmscore/score.h"
#include "libmscore/soundlibrary.h"
#include "libmscore/undo.h"
#include "seq.h"

namespace Ms {

EditWarmup::EditWarmup()
      {
      _timer.setInterval(0);        // (a step per event-loop pass: input is handled between them)
      connect(&_timer, &QTimer::timeout, this, [this]() { step(); });
      }

EditWarmup* EditWarmup::instance()
      {
      static EditWarmup* w = new EditWarmup;
      return w;
      }

void EditWarmup::edited(Score* cs)
      {
      if (!EditLag::on() || !cs || !SoundLib::current())
            return;
      MasterScore* ms = cs->masterScore();
      if (!ms->undoStack() || (ms == _score && ms->undoStack()->state() == _undoState && !_pending))
            return;                       // (a command that changed nothing: a selection)
      if (seq && !seq->isStopped())
            return;                       // (playing: asked at once, as before)
      if (_score && _score != ms)
            ContextLevel::dropWarmup();
      _score = ms;
      _undoState = ms->undoStack()->state();
      _pending = true;
      _timer.start();
      }

bool EditWarmup::pending(const Score* s) const
      {
      return _pending && s && _score && s->masterScore() == _score;
      }

void EditWarmup::step()
      {
      if (!_score || (seq && !seq->isStopped())) {
            done();                       // (the score went, or it plays: whoever asks renders at once, as before)
            return;
            }
      bool finished = false;
      {
            LagProbe::Scope probe("EditWarmup::step");
            finished = ContextLevel::warmStep(_score);
      }
      if (finished)
            done();
      }

void EditWarmup::done()
      {
      _timer.stop();
      ContextLevel::dropWarmup();         // (a finished job was taken by volumes() / masterTrim(); an unfinished one goes)
      if (!_pending)
            return;
      _pending = false;
      emit warmed();
      }

void EditWarmup::cancel()
      {
      _score = nullptr;
      done();
      }

}     // namespace Ms
