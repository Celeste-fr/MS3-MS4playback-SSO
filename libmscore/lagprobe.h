//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  LagProbe: where the interface's time goes (the owner, 2026-10-16: "MuseScore already feels kinda laggy with all
//  the SSO patches loaded and the measurement going in the background"). Off unless MS_LAG_PROBE=<log file> is set
//  in the environment (read once): then
//    scopes      LagProbe::Scope around the work that may hold the main thread (a command, layout, the even
//                levels' detect, the real range's and master trim's renders, paints, the host's idle timer …) records
//                its start and duration, its thread and its nesting depth on the main thread
//    threads     nameThread gives MuseScore's own threads a name the system shows (Windows SetThreadDescription,
//                Linux pthread_setname_np) so that mscore/lagprobesession.cpp can tell their CPU apart
//  The session (mscore/lagprobesession.h) drains the samples, adds the event loop's latency, runs the scripted edits
//  and writes the log. Nothing here changes what is played or drawn; with the variable unset a Scope costs one
//  test of a static bool.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __LAGPROBE_H__
#define __LAGPROBE_H__

#include <vector>

#include <QString>
#include <QtGlobal>

namespace Ms {
namespace LagProbe {

bool on();                          // MS_LAG_PROBE set
QString logPath();
qint64 nowNs();                     // monotonic, since the probe's first use
void markMainThread();              // the GUI thread (called once by the session)
bool isMainThread();
void nameThread(const char* name);  // (when on) this thread's name, as the system shows it

struct Sample {
      const char* name;
      QString detail;
      qint64 startNs;
      qint64 durNs;
      bool main;                    // on the GUI thread
      int depth;                    // nesting on the GUI thread (0: outermost scope)
      const char* parent;           // the enclosing scope (nullptr: none)
      const char* root;             // the outermost enclosing scope (nullptr: none)
      };
void add(const char* name, qint64 startNs, qint64 durNs, const QString& detail = QString(), int depth = 0,
         const char* parent = nullptr, const char* root = nullptr);
std::vector<Sample> take();         // the samples since the last take

// the audio callback (Seq::process: every synthesizer and plug-in it plays, Kontakt's DSP included), kept in atomics
// (no lock on the realtime thread): calls, time, the longest call, the calls longer than their period
struct AudioStats {
      qint64 calls { 0 };
      qint64 sumNs { 0 };
      qint64 maxNs { 0 };
      qint64 overruns { 0 };
      qint64 periodNs { 0 };        // the last call's period
      };
void addAudio(qint64 durNs, qint64 periodNs);
AudioStats takeAudio();

class Scope {
      const char* _name;
      qint64 _start { -1 };
      int _depth { 0 };
      const char* _parent { nullptr };
      const char* _root { nullptr };
      QString _detail;

   public:
      explicit Scope(const char* name);
      Scope(const char* name, const QString& detail);
      ~Scope();
      void setDetail(const QString& d) { if (_start >= 0) _detail = d; }
      };

} // namespace LagProbe
} // namespace Ms
#endif
