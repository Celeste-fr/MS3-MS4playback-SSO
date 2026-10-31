//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  PlayCapture (playcapture.h has the design)
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "playcapture.h"

#include <cstdlib>
#include <cstring>

#include <QAction>
#include <QElapsedTimer>
#include <QFile>
#include <QTimer>

#include "musescore.h"
#include "seq.h"
#include "shortcut.h"
#include "soundlibraryhost.h"
#include "libmscore/score.h"
#include "libmscore/repeatlist.h"
#include "audio/midi/msynthesizer.h"
#ifdef USE_VST3
#include "audio/vst3/vst3synth.h"
#endif

namespace Ms {

static void put32(QByteArray& b, quint32 v) { for (int i = 0; i < 4; ++i) b.append(char((v >> (8 * i)) & 0xff)); }
static void put16(QByteArray& b, quint16 v) { b.append(char(v & 0xff)); b.append(char(v >> 8)); }

static bool writeWav(const QString& path, const float* data, size_t frames, int rate)
      {
      QFile f(path);
      if (!f.open(QIODevice::WriteOnly))
            return false;
      const quint32 bytes = quint32(frames * 2 * sizeof(float));
      QByteArray h;
      h.append("RIFF");
      put32(h, 36 + bytes);
      h.append("WAVEfmt ");
      put32(h, 16);
      put16(h, 3);                  // IEEE float
      put16(h, 2);
      put32(h, quint32(rate));
      put32(h, quint32(rate) * 8);
      put16(h, 8);
      put16(h, 32);
      h.append("data");
      put32(h, bytes);
      f.write(h);
      f.write(reinterpret_cast<const char*>(data), qint64(bytes));
      return true;
      }

namespace {

class Session : public QObject {
      int _step { 0 };
      QElapsedTimer _clock;
      QTimer _timer;
      QString _path;
      double _stopAt { -1 };
      bool _stopped { false };
      unsigned _skippedAtPlay { 0 };

      static unsigned skippedBlocks()
            {
#ifdef USE_VST3
            if (seq && seq->synti())
                  if (Vst3Synth* v = dynamic_cast<Vst3Synth*>(seq->synti()->synthesizer("VST3")))
                        return v->skippedBlocks();
#endif
            return 0;
            }

      void finish(const QString& why)
            {
            AudioCapture* c = seq ? seq->capture() : nullptr;
            const size_t frames = c ? c->frames.load() : 0;
            if (c)
                  writeWav(_path, c->data.data(), frames, MScore::sampleRate);
            const unsigned skipped = skippedBlocks() - _skippedAtPlay;
            QFile t(_path + ".txt");
            if (t.open(QIODevice::WriteOnly | QIODevice::Text)) {
                  t.write(QString("result %1\nrate %2\nframes %3\nrenderAhead %4\noverflow %5\nlate %6\nskipped plug-in blocks %7\n")
                          .arg(why).arg(MScore::sampleRate).arg(frames).arg(seq ? seq->renderAheadMs() : 0)
                          .arg(c && c->overflow.load() ? 1 : 0).arg(seq ? seq->lateSummary() : QString()).arg(skipped).toUtf8());
                  }
            for (Score* s : mscore->scores())
                  if (!s->tmpName().isEmpty())
                        QFile::remove(s->tmpName());
            QFile::remove(dataPath + "/session");
            QTimer::singleShot(300, []() { std::_Exit(0); });
            }

      void tick()
            {
            Score* score = mscore->currentScore();
            if (_clock.elapsed() > 900000) {
                  _timer.stop();
                  finish("timeout");
                  return;
                  }
            if (!score || !seq || !seq->isRunning())
                  return;
            switch (_step) {
                  case 0: {
                        if (_clock.elapsed() < 2000 || SoundLibraryHost::instance()->preloading())
                              return;
                        QString error;
                        if (!SoundLibraryHost::instance()->readyToPlay(score, &error))
                              return;
                        const double from = qEnvironmentVariable("MS_PLAY_CAPTURE_FROM").toDouble();
                        MasterScore* ms = score->masterScore();
                        const int utick = ms->utime2utick(from);
                        ms->setPlayPos(Fraction::fromTicks(ms->repeatList().utick2tick(utick)));
                        _skippedAtPlay = skippedBlocks();
                        if (QAction* play = getAction("play"))
                              play->trigger();
                        _step = 1;
                        _clock.restart();
                        return;
                        }
                  case 1:
                        if (seq->heardPlaying())
                              _step = 2;
                        else if (_clock.elapsed() > 600000)
                              finish("never played");
                        return;
                  case 2:
                        if (_stopAt >= 0 && !_stopped && seq->heardPlaying()
                            && score->masterScore()->utick2utime(seq->getCurTick()) >= _stopAt) {
                              _stopped = true;
                              QAction* play = getAction("play");
                              if (play && play->isChecked())
                                    play->trigger();
                              }
                        if (!seq->isPlaying() && !seq->heardPlaying()) {
                              _timer.stop();
                              QTimer::singleShot(500, this, [this]() { finish("ok"); });
                              }
                        return;
                  }
            }

   public:
      Session(const QString& path) : _path(path)
            {
            if (qEnvironmentVariableIsSet("MS_PLAY_CAPTURE_STOP_AT"))
                  _stopAt = qEnvironmentVariable("MS_PLAY_CAPTURE_STOP_AT").toDouble();
            _clock.start();
            connect(&_timer, &QTimer::timeout, this, [this]() { tick(); });
            _timer.start(20);
            }
      };

} // namespace

void PlayCapture::startIfRequested()
      {
      const QString path = qEnvironmentVariable("MS_PLAY_CAPTURE");
      if (path.isEmpty() || !mscore)
            return;
      static Session* session = nullptr;
      if (!session)
            session = new Session(path);
      }

} // namespace Ms
