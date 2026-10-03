//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __MSCORE_LIVECLIPEDIT_H__
#define __MSCORE_LIVECLIPEDIT_H__

//---------------------------------------------------------
//   Editing Live clips in MuseScore: the sessions (LIVE.md › Editing Live clips in MuseScore; the
//   model, the diff and the protocol are in liveclipmodel.h).
//
//   - The MuseScore Link device's "Edit in MuseScore" button reads the clip in Live's Detail View and
//     sends it (/live/clip/begin + notes) over the link's socket (LiveClipsLink owns it and hands
//     every /live/clip/… message here). A new tab opens with the clip as a score, in Continuous View.
//     The same clip again: its tab comes to the front (read again if it changed in Live).
//   - A clip score is marked here only (a property of this object, not of the file): saving it gives
//     an ordinary score; closing its tab ends the editing (/ms/clip/close); an unsaved one closes
//     without asking.
//   - Each edit (the score's playlistChanged, 300 ms after the last, not in the middle of a command)
//     is diffed against the baseline and the operations are sent (/ms/clip/write + ops); one write at
//     a time, confirmed by /live/clip/written with the ids of the added notes, sent again (same write
//     number: the device applies it once) when unconfirmed after 3 s.
//   - A change made in Live meanwhile (the device checks the clip's notes every second): nothing more
//     is written; the status line says "conflict" and offers "Reload from Live" (the score is read
//     again into a new tab that replaces this one; edits not yet sent are lost, and the owner decides).
//   - Playback (the owner, 2026-10-02: "when I press playback in musescore, it plays through the live plugins,
//     but doesn't affect the time cursor in live"): with "Clip tabs play through Live" on (QSettings
//     liveIntegration/clipTabsPlayLive, default on), a MuseScore Link copy of protocol 4+ on the clip's track
//     (/live/clip/track) and the device answering, the tab in front sends what MuseScore plays to that track
//     (Seq::setLiveTrack, livemidiout.h); MuseScore's transport and cursor stay MuseScore's, Live's are never
//     touched. Otherwise (no copy on the track, the setting off, the link lost) MuseScore's own sounds play;
//     the status line says which, and a copy removed or the link lost is a notice (LiveClipsLink::notice).
//   - A new hub (the old copy deleted; /live/hello with another session): each clip edited here is handed to it
//     (/ms/clip/adopt with its last known hash), and edits made meanwhile are written.
//---------------------------------------------------------

#include <map>
#include <set>

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>

#include "liveclipmodel.h"

class QLabel;
class QPushButton;
class QTimer;
class QWidget;

namespace Ms {

class MasterScore;
class Score;

namespace LiveIntegration {

class LiveClipEditor : public QObject {
      Q_OBJECT

   public:
      enum class State { SYNC, SENDING, CONFLICT, RELOADING, GONE, NO_ANSWER, FAILED };

   private:
      struct Session {
            LiveClipEdit::Clip clip;            // as read (notes: Live's at the last read)
            QPointer<MasterScore> score;
            LiveClipEdit::Baseline base;
            State state { State::SYNC };
            qint32 liveHash { 0 };              // the clip's notes in Live as the device last reported them
            int write { 0 };                    // the last write sent
            bool inFlight { false };
            LiveClipEdit::Diff pending;
            std::vector<QByteArray> packets;    // (the write, for a resend)
            qint64 sentAt { 0 };
            int tries { 0 };
            bool changedMeanwhile { false };
            int writes { 0 };                   // confirmed writes
            int notesChanged { 0 };             // notes modified, removed and added in Live so far
            QString error;
            // playback through the clip's Live track
            int trackId { 0 };                  // the track's LOM id (0: not known yet: an older device)
            bool copy { false };                // a MuseScore Link copy (protocol 4+) is on it
            bool copyWas { false };             // (it was: its removal is a notice)
            };

      std::map<QString, Session> _sessions;     // by the device's clip key
      std::map<QString, LiveClipEdit::Clip> _incoming;
      std::set<QString> _dirty;
      std::set<const MasterScore*> _replaced;   // tabs being replaced by a reload (closed without asking)
      QTimer* _debounce { nullptr };
      QTimer* _poll { nullptr };
      QPointer<MasterScore> _current;
      QWidget* _status { nullptr };
      QLabel* _statusLabel { nullptr };
      QPushButton* _reload { nullptr };

      Session* sessionOf(const Score* score);
      void opened(LiveClipEdit::Clip clip);
      void scoreChanged(MasterScore* score);
      void flush();
      void write(const QString& key);
      void written(const QString& key, int write, const QString& status, qint32 hash, const std::vector<int>& ids);
      void poll();
      void send(const QByteArray& p);
      void updateStatus();
      void updateRouting();
      int _routed { -1 };                       // the track the sequencer was last given (-1: never)

   signals:
      void statusChanged();

   public:
      LiveClipEditor();
      static LiveClipEditor* instance();

      static bool enabledSetting();                 // QSettings liveIntegration/editClips (default on)
      static void setEnabledSetting(bool on);
      static bool playLiveSetting();                // QSettings liveIntegration/clipTabsPlayLive (default on)
      static void setPlayLiveSetting(bool on);

      // the link (liveclips.h): checked each second and at each hello; a new hub got the clips
      void linkChanged();
      void newDevice();
      void countTabs(int* tabs, int* throughLive) const;
      // the Live track a clip score plays through now (0: MuseScore's own sounds)
      int liveTrack(const MasterScore* score) const;

      bool isClipScore(const Score* score) const;
      bool unsavedClipScore(const MasterScore* score) const;  // closes without asking
      void received(const QString& address, const QVariantList& args);
      void scoreClosed(MasterScore* score);
      void setCurrentScore(MasterScore* score);
      void reload(MasterScore* score);
      QString statusText(const MasterScore* score) const;
      State state(const MasterScore* score) const;
      };

}     // namespace LiveIntegration
}     // namespace Ms
#endif
