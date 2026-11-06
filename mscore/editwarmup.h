//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  EditWarmup: after an edit, while stopped, the score's whole-score renders that the next sound needs (the volumes'
//  real range, the master trim: ContextLevel::warmStep) made one chunk per event-loop pass instead of at once inside
//  the edit (the edit lag, branch edit-lag, 2026-10-17: the Inspector's first question after each edit rendered them,
//  ~200 ms of each 300-500 ms edit on the Windows VM). Nothing sounds differently: whoever needs them before they are
//  done (a click's preview, Play, an export, the measurer) gets them at once as before, the warm-up's work finished
//  rather than redone (the same inputs give the same render); while pending, the host's 50 ms mixer timer leaves the
//  slots' gains as they are (nothing plays while stopped) and sets them once done. The Inspector's Performance rows
//  wait for warmed(). Off with EditLag (MS_EDIT_LAG_OFF=1): as before
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __EDITWARMUP_H__
#define __EDITWARMUP_H__

#include <QObject>
#include <QPointer>
#include <QTimer>

namespace Ms {

class MasterScore;
class Score;

class EditWarmup : public QObject {
      Q_OBJECT

      QTimer _timer;
      QPointer<MasterScore> _score;
      int _undoState { -1 };
      bool _pending { false };

      EditWarmup();
      void step();
      void done();

   public:
      static EditWarmup* instance();
      void edited(Score* cs);                   // after a command (MuseScore::endCmd)
      bool pending(const Score* s) const;       // the score's renders not made yet (stopped)
      void cancel();                            // its score goes (dropped, warmed() sent)

   signals:
      void warmed();                            // pending no more
      };

}     // namespace Ms
#endif
