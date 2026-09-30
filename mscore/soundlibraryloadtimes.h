//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  LoadTimes: where the time goes when MuseScore loads a sound library's instances (the owner,
//  2026-09-28: "optimize load times of the SSO plugin"; Kontakt and SSO are only on the owner's
//  computer, so one run there has to tell everything). MuseScore --measure-load-times <library>
//  [score files…] [--extract-patches <file>]: a process of its own, like the background extract
//  (no window, below-normal priority, its own copy of the setups, one run at a time), that loads
//  what the scores need (every route: main patches, extras the notation plays, copies for other
//  tunings; without scores a few patches of each family, or those a file lists) and writes one
//  report, "<library> load times <date>.txt" in Documents/MuseScore Sound Library Check:
//
//    1 each patch alone      a new instance, its setup read (or made), setState by step
//                            (Vst3Plugin::Times), until a note sounds, until the memory settles,
//                            how much it grew by, and how long freeing it takes
//    2 as MuseScore loads    all the scores' instances one after the other on this thread, as at
//                            score open, then until the memory settles (the real wait)
//    3 on worker threads     the same with setState on 2, then 4 threads (SoundLibraryHost's
//                            io/soundLibraryLoadThreads): does the plug-in take it, is it faster
//    4 reuse                 a loaded instance given another patch (a spare taking a new patch)
//                            against a new instance: time, and whether the first patch's memory goes
//    5 probe                 on one patch: the mic levels at 0, and each saved script value that
//                            holds only 0 and 1 turned over (KontaktSetup::withScriptValues on
//                            Kontakt's own state): how much memory each saves and which
//                            articulations go silent (SSO's articulation switches under each
//                            technique unload its samples, most likely; the value that holds
//                            them isn't known). Each such value's elements one by one when that
//                            saved a lot
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#ifndef __SOUNDLIBRARYLOADTIMES_H__
#define __SOUNDLIBRARYLOADTIMES_H__

#include <memory>

#include <QString>
#include <QStringList>

#include "libmscore/soundlibrary.h"

namespace Ms {

class LoadTimes {
   public:
      struct Options {
            QStringList scores;           // score files; none: patchesFile, else a few patches of each family
            QString patchesFile;          // one patch name a line
            QString probePatch;           // phase 5's patch (empty: the first measured patch with 5 or more articulations)
            QList<int> threads { 2, 4 };  // phase 3
            bool probe { true };
            int probeMinutes { 40 };      // phase 5 stops after this long
            };
      // the report's path in report; false: nothing measured (no plug-in, no patch)
      static bool run(std::shared_ptr<const SoundLib::Library> library, const Options& options, QString* report);
      };

} // namespace Ms
#endif
