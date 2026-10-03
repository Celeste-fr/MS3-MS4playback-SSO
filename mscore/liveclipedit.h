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
//     without asking. Its tab shows '*' only while edits aren't in Live yet (inSync); an unnamed clip is named by
//     its place (clipLabel). The status line is short and cut to its room (ElidedLabel), the details in its tooltip.
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
//   - Automation lanes (the owner, 2026-10-02): the clip's track's Live parameters (/live/params: the mixer, every
//     device's) are the tab's lanes (automationlanes.h), and the lanes are the clip's own envelopes in Live. On
//     /live/clip/where (a session clip) the envelopes are read from the MuseScore Envelopes script
//     (/ms/env/read, liveclipmodel.h) into the score's lanes (no undo step); each edit of a lane (the same 300 ms
//     debounce as the notes) writes the lanes changed since (/ms/env/write: a lane replaces its envelope, a lane
//     removed clears it), one write at a time, sent again after 3 s. The script hashes the envelopes: a change
//     made in Live is a conflict like the notes' (Reload from Live). An arrangement clip: no lanes (Live's API has
//     no envelopes for it, still in 12.4.6); the script not answering: no lanes, the status line says how to set it
//     up, asked again every 5 s.
//   - The Velocity lane (liveclipmodel.h › The Velocity lane, protocol 6): asked for when the tab opens (/ms/vel/ask)
//     and read into the lane (no undo step; in "write" the notes still at the velocity written get their originals);
//     sent after each change (/ms/vel/set), in "write" once Live confirmed the notes' write (with the originals).
//   - A new hub (the old copy deleted; /live/hello with another session): each clip edited here is handed to it
//     (/ms/clip/adopt with its last known hash), and edits made meanwhile are written.
//---------------------------------------------------------

#include <functional>
#include <map>
#include <set>

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
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
      // the clip's automation lanes: NONE (an older device: no place known), READING (the script asked), READY (the
      // lanes are the clip's envelopes), ARRANGEMENT (an arrangement clip: Live's API has no envelopes for it),
      // NO_SCRIPT (the MuseScore Envelopes script doesn't answer), FAILED
      enum class EnvState { NONE, READING, READY, ARRANGEMENT, NO_SCRIPT, FAILED };

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
            int place { LiveClipEdit::NO_PLACE };   // the session slot, ARRANGEMENT or not known (/live/clip/where)
            // playback through the clip's Live track
            int trackId { 0 };                  // the track's LOM id (0: not known yet: an older device)
            bool copy { false };                // a MuseScore Link copy (protocol 4+) is on it
            bool copyWas { false };             // (it was: its removal is a notice)
            // the automation lanes: the clip's envelopes, through the MuseScore Envelopes script
            EnvState env { EnvState::NONE };
            int envTrack { -1 };                // the clip's place (/live/clip/where)
            int envSlot { -1 };
            qint32 envHash { 0 };               // the envelopes in Live as the script last reported them
            int envExpected { 0 };              // lanes announced by /live/env/begin
            std::map<std::pair<int, int>, std::map<int, std::vector<std::pair<int, double>>>> envIncoming;
            std::map<std::pair<int, int>, int> envChunks;
            std::map<QString, QString> envSent;  // target -> its points' hash, as Live has them
            std::map<QString, QString> envPending;
            int envWrite { 0 };
            bool envInFlight { false };
            bool envChangedMeanwhile { false };
            std::vector<QByteArray> envPackets;
            qint64 envSentAt { 0 };             // (a read or a write)
            int envTries { 0 };
            int envWrites { 0 };
            QString envError;
            // the Velocity lane (liveclipmodel.h): its record in the MuseScore Link device (kept in the set)
            bool velAsked { false };            // /ms/vel/ask sent
            bool velRead { false };             // the record read back (or there was none, or the device is older)
            std::map<int, QVariantList> velIncoming;
            QString velSent;                    // the record as the device has it (velRecordKey)
            QString velPending;                 // … as sent, not confirmed yet
            int velSerial { 0 };
            bool velInFlight { false };
            bool velAfterWrite { false };       // (sent once the notes' write in flight is confirmed)
            std::vector<QByteArray> velPackets;
            qint64 velSentAt { 0 };
            int velTries { 0 };
            bool velKept { false };             // the device kept it in the set (the MuseScore Envelopes script)
            QString velStatus;                  // the device's word ("ok", or why it can't shape)
            };

      std::map<QString, Session> _sessions;     // by the device's clip key
      std::map<QString, LiveClipEdit::Clip> _incoming;
      std::set<QString> _dirty;
      QString _flushing;                        // (the clip being written by flush())
      int _audible { 0 };                       // the Live track made audible for MuseScore's playback (0: none)
      bool _playing { false };
      bool _seqConnected { false };
      QTimer* _audibleBeat { nullptr };
      void updateAudible();
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
      void sendEnv(const QByteArray& p);
      void readEnvelopes(const QString& key);
      void envelopesRead(const QString& key);
      void writeEnvelopes(const QString& key);
      void envWritten(const QString& key, int write, const QString& status, qint32 hash);
      void askVelocity(const QString& key);
      void velocityRead(const QString& key, bool found, const QVariantList& atoms);
      void sendVelocity(const QString& key);
      void updateStatus();
      void statusParts(const MasterScore* score, QStringList* parts, QStringList* details) const;
      bool inSync(const QString& key, const Session& s) const;
      void updateClean(const QString& key);
      void retitle(Session& s);
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
      // a clip read from Live, edited in score from now on (opened() with the imported score; the tests)
      void edit(const LiveClipEdit::Clip& clip, MasterScore* score);
      // the status line: short (the status bar; cut to its room), and the details (its tooltip)
      QString statusText(const MasterScore* score) const;
      QString statusDetails(const MasterScore* score) const;
      // a clip tab shows no '*' (its undo stack marked clean) while it is in sync with Live: every edit written
      // and confirmed, the envelopes too (the owner, 2026-10-03: edits are in Live as they are made, the '*'
      // only nagged to save); '*' while an edit waits, is being written or can't be (a conflict, the clip gone,
      // Live not answering). Undo and redo work as before: an undo makes it dirty and is written like any edit.
      bool inSync(const MasterScore* score) const;
      // the clip tab's Live track made audible while MuseScore plays through it (its mute, its groups' mute and the
      // solo set aside by the device, put back after; LIVE.md › Clip tabs play through Live): 0 none
      void setAudible(int track);
      int audible() const { return _audible; }
      void audibleBeat();                       // (each second while audible: the device's heartbeat)
      static void setSendHook(std::function<void(const QByteArray&)> hook);    // (the tests: every packet sent)
      State state(const MasterScore* score) const;
      // automation lanes of a clip tab: the Live parameters its lanes can be on (nullptr: none, the reason in
      // envText), and the clip's key; paramsChanged: the device sent a track's parameters
      const std::vector<LiveClipEdit::LiveParam>* liveParams(const Score* score) const;
      QString clipKey(const Score* score) const;
      EnvState envState(const MasterScore* score) const;
      QString envText(const MasterScore* score, QString* details = nullptr) const;   // (short; details: the long one)
      void paramsChanged(const QString& key);
      // the Velocity lane: "Write into notes" asks first (QSettings liveIntegration/velocityWriteNoAsk: "Don't ask
      // again"); false: the user said no
      static bool confirmVelocityWrite(QWidget* parent);
      QString velocityText(const MasterScore* score, QString* details = nullptr) const;
      };

}     // namespace LiveIntegration
}     // namespace Ms
#endif
