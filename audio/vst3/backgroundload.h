//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  BackgroundLoad: waiting for a plug-in that goes on loading on threads of its own after setState
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#ifndef __BACKGROUNDLOAD_H__
#define __BACKGROUNDLOAD_H__

#include <map>
#include <cstdint>

namespace Ms {

//---------------------------------------------------------
//   BackgroundLoad
//    Ample Guitar M loads the sample set its state names (DBName: Finger / Pick / Strum) on a thread it starts in
//    setState, and goes on after setState returns: measured on the Windows VM (FIX4 of the Ample check of build 455,
//    2026-10-20, amphost4 / amphost5 there): a set other than the one loaded at the instance's creation takes ~0.6 s
//    of one core (the working set grows ~25 MB / 70 ms, then that thread ends). Neither its parameters, its state nor a
//    restartComponent tell when it is done; processing isn't needed (it finishes in wall time with nothing processed),
//    and until it is, notes play whatever is there (a G3 on Pick: -67 to -34 dB against -25.5 loaded, other timbres).
//    An offline render that starts at once (the verification, live equivalence, audio export: faster than real time)
//    so came out different on every run (build 455's agm2-on, three instances: the Finger route's first seconds,
//    once the Strum route), and a second instance's setState while the first still loaded crashed AGM.vst3 once in
//    seven runs of amphost5 (an access violation in AGM.vst3).
//
//    The wait: the process's threads are noted (the constructor) before the instance is made; wait() polls every
//    POLL_MS and returns once a window (WINDOW_MS) is quiet: no thread started since used QUIET or more of a CPU in it
//    (Windows QueryThreadCycleTime, QUIET_CYCLES; Linux /proc/self/task/<tid>/schedstat, QUIET_NS) and the process's
//    memory (working set / resident) moved less than QUIET_BYTES (a loader waiting on a cold disk uses little CPU, but
//    what it reads still arrives in memory), and every new thread that was ever busy has ended or been quiet for
//    LONG_MS. At least one window long, at most limit seconds. Measured margin (Windows VM, Ample): the loader
//    ~950-1000 M cycles a window and ~25 MB per 70 ms, Ample's other new threads 0.6-0.7 M cycles; QUIET_CYCLES 20 M
//    (2 % of the loader's). Used only for a map that says so (<Plugin backgroundLoad>, Library::backgroundLoad: Ample),
//    after each instance's setup, so that no two instances load at once. macOS (no hosting): returns at once.
//---------------------------------------------------------

class BackgroundLoad {
   public:
      static constexpr int WINDOW_MS = 200;
      static constexpr int POLL_MS = 50;
      static constexpr double QUIET_CYCLES = 20e6;          // Windows: per WINDOW_MS
      static constexpr double QUIET_NS = 8e6;               // Linux: 8 ms on a CPU per WINDOW_MS (4 %)
      static constexpr double QUIET_BYTES = 2.0 * 1024 * 1024;   // the process's memory per WINDOW_MS
      static constexpr int LONG_MS = 1000;

      BackgroundLoad();                   // the process's threads now

      struct Result {
            double seconds { 0 };         // waited
            bool quiet { false };         // false: the limit was reached first
            int threads { 0 };            // threads started since the constructor, still there at the end
            double busiest { 0 };         // the last window's busiest new thread (cycles / ns)
            double memoryMB { 0 };        // the process's memory: its change over the wait
            };
      // the environment's MS_BACKGROUND_LOAD_WAIT (seconds) replaces limit when set; 0 turns the wait off
      Result wait(double limitSeconds) const;

      // the process's threads and the CPU each has used (cycles / ns); the process's memory (bytes)
      static std::map<uint64_t, double> threads();
      static double memory();

   private:
      std::map<uint64_t, double> _before;
      };

}     // namespace Ms
#endif
