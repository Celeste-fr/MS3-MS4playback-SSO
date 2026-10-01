//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __MSCORE_LIVECLIPS_H__
#define __MSCORE_LIVECLIPS_H__

//---------------------------------------------------------
//   "Live plays the score" (LIVE.md; libmscore/liveclips.h has the clips, the timeline and the
//   protocol). The link with the MuseScore Link device in Live:
//
//   - While on (Mixer › Advanced Options… › Ableton Live: "Live plays the score"; QSettings
//     liveIntegration/clips; with Mixer › Play through Live on) and the device answers
//     (/live/hello), the score in front is rendered as playback renders it, 300 ms after its last
//     change (every edit, undo and redo end in its playlistChanged), a few measures per turn of the
//     event loop (~10 ms each, so typing never waits; a change meanwhile starts it again). Only
//     the routes whose clip changed (hash) are sent, paced (8 datagrams every 15 ms), and sent
//     again when the device doesn't confirm them within 3 s. The device asks for everything again
//     when it loads (a new session) or on its Resync button.
//   - Live is the clock. MuseScore's sequencer sends nothing to the library's routes and no MIDI
//     clock (Seq::setLiveClips). It follows Live's transport (/live/transport): Live starts, it
//     starts at Live's position (beat -> seconds -> tick); a difference over 80 ms moves it there;
//     Live stops, it stops; stopped, its cursor follows Live's. So MuseScore's own (built-in)
//     parts play along, as a slave, and the score's cursor follows.
//   - MuseScore's Play starts Live instead (/ms/play at the play position), and MuseScore follows;
//     its Stop stops Live.
//   - The UDP socket also carries the editing of Live clips (liveclipedit.h): it is bound while this
//     is on or that setting is (liveIntegration/editClips, on by default); /live/clip/… goes there, and
//     without "Live plays the score" a hello only records the device (no resync, no /ms/mode).
//   All on the GUI thread, never the audio thread.
//---------------------------------------------------------

#include <deque>
#include <map>
#include <memory>

#include <QObject>
#include <QPointer>
#include <QString>

#include "audio/midi/event.h"
#include "libmscore/liveclips.h"

class QTimer;
class QUdpSocket;

namespace Ms {

class MasterScore;
class MidiRenderer;

namespace LiveIntegration {

class LiveClipsLink : public QObject {
      Q_OBJECT

      struct Sent {
            LiveClips::Track track;       // (its notes: for a resend)
            qint64 sentAt { 0 };
            int tries { 0 };
            bool confirmed { false };
            QString status;               // the device's: "ok", "no track", …
            QString liveTrack;            // the Live track it went to
            // its plug-in parameter lanes (/ms/params; the device sets them on the track's plug-in)
            qint64 paramsSentAt { 0 };
            int paramsTries { 0 };
            bool paramsConfirmed { false };
            QString paramsStatus;         // "ok", "missing: Vibrato" …
            };

      bool _on { false };
      int _port { LiveClips::DEFAULT_PORT };
      QUdpSocket* _socket { nullptr };
      QPointer<MasterScore> _score;
      QTimer* _debounce { nullptr };
      QTimer* _step { nullptr };
      QTimer* _pace { nullptr };
      QTimer* _poll { nullptr };

      std::unique_ptr<MidiRenderer> _renderer;
      EventMap _events;
      int _nextUtick { -1 };              // rendering: the next chunk's; -1: not rendering
      qint64 _renderStarted { 0 };
      int _routesGeneration { -1 };
      QStringList _portNames;
      LiveClips::Timeline _timeline;

      std::map<QString, Sent> _sent;
      LiveClips::Song _song;
      bool _songConfirmed { false };
      QString _songStatus;
      std::deque<QByteArray> _queue;
      int _generation { 0 };
      QString _session;
      int _deviceProtocol { 0 };    // the device's (/live/hello): parameter lanes from 3 on
      qint64 _lastHello { 0 };
      qint64 _lastSync { 0 };
      bool _wasConnected { false };
      QString _sentMode;

      // Live's transport
      bool _livePlaying { false };
      double _liveBeat { 0 };
      double _liveBpm { 0 };
      bool _following { false };          // MuseScore plays because Live does
      bool _startingFromLive { false };
      qint64 _startedAt { 0 };            // when MuseScore was started to follow Live (the sequencer's state follows later)

      void bindSocket();
      void read();
      void received(const QString& address, const QVariantList& args);
      void transport(bool playing, double beat, double bpm);
      void scoreChanged();
      void startRender();
      void renderStep();
      void finish();
      void enqueue(const LiveClips::Track& t);
      void enqueueParams(const LiveClips::Track& t);
      void send(const QByteArray& packet);
      void sendSome();
      void poll();
      void resync();
      void sendMode();
      void applySeqMode();

   signals:
      void statusChanged();

   public:
      LiveClipsLink();
      ~LiveClipsLink();
      static LiveClipsLink* instance();

      static bool enabledSetting();
      bool isOn() const { return _on; }             // the setting
      bool active() const;                          // on, and the library plays through MIDI output (Live)
      void setOn(bool on);
      void setScore(MasterScore* score);
      bool connected() const;
      QString statusText() const;
      // Seq::start / stop: in this mode MuseScore's Play and Stop act on Live (true: handled)
      bool startRequested(int utick);
      void stopRequested();
      // the library's output changed (Mixer › Play through Live)
      void outputChanged();
      // the socket is shared with the clip editor (liveclipedit.h): bound while either is on
      void updateSocket();
      void sendDatagram(const QByteArray& packet) { send(packet); }
      bool deviceAnswers() const;                   // a hello within 6 s (either feature)
      // the routes ("<port>:<channel>" keys) the device found no track for, for the score Live plays now;
      // *known: false unless the device answers for this score and has confirmed every route's clip
      QStringList keysWithoutTrack(const MasterScore* score, bool* known) const;
      };

}     // namespace LiveIntegration
}     // namespace Ms
#endif
