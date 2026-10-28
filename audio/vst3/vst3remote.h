//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Vst3Remote: hosted plug-in instances (Kontakt + SSO, Ample …) in helper processes (MuseScoreLibraryHost[.exe],
//  next to MuseScore3Evo[.exe]) instead of MuseScore's own (the owner approved, 2026-10-25: "seems good"). Why
//  (measured): Kontakt does its setState work on its process's GUI thread even when it is called from a worker (build
//  #480 on the VM: MuseScore's GUI thread at 64 % CPU, the worker's 0.6 %; the window froze 8-18 s per big patch), and a
//  process's setState is one CPU-bound thread (VM, ktload: Whence's 10 states in 1 process 37 s, 2 processes 21 s, 3
//  processes 19 s). In helpers the work runs on the helper's main thread: MuseScore's window stays free, several helpers
//  load at the same time, and a plug-in that crashes takes only its helper down.
//
//  The cut is inside Vst3Plugin (vst3plugin.h): load() makes either a local instance (the code as before, untouched) or a
//  remote one whose calls go to a real local Vst3Plugin in a helper, replayed in the same order on the same kind of
//  thread (GUI calls on the helper's main thread, audio calls on its audio thread). So the plug-in sees the same call
//  sequence as in MuseScore's process: the same events in the same blocks of the same lengths, the same parameter
//  queue, the same varispeed (it runs in the helper's Vst3Plugin::process). That is what makes the output sample-
//  identical, a property of the design and not a tuning result (tests: tst_soundlibrary::remoteNull …).
//
//    control   GUI-thread calls (load, setState, state, settle, parameters, editor …): length-prefixed QDataStream
//              messages over two pipes; each request carries a sequence number and its answer comes back with it, so
//              several MuseScore threads may wait on one helper at once (a worker's setState and the GUI's
//              setParameter). The helper runs them on its main thread in the order sent (a nested event loop inside
//              a plug-in's call never runs the next one: Child::drain). Busy: a long call (LOAD, SET_STATE, DESTROY …)
//              is in flight. MuseScore's GUI thread never waits on a busy helper for what it can do without: a
//              parameter change and idle are sent without waiting, a parameter read is answered from what was last
//              read or set (Vst3Plugin's remote cache), and every load step that touches the plug-in runs on a worker
//              (SoundLibraryHost::startSetState)
//    audio     per instance a shared-memory segment: the audio thread's calls since the last block (commands), the
//              block's length and the caller's MXCSR (the floating-point mode the block runs in), the output. A
//              semaphore wakes the helper's audio thread (MMCSS "Pro Audio" on Windows), another the caller.
//              Synchronous: the block is rendered within the caller's process(), no added latency; Vst3Synth posts
//              every slot's block first (beginProcess) and collects them in slot order, so the helpers render a block
//              at the same time and the slots are summed exactly as before (0 + x = x, then the same adds). A load's
//              settle (REQ_SETTLE) runs on the helper's settle thread, not its audio thread, so a loading instance never
//              holds up the blocks of the others in its helper (helper-stall, 2026-10-27: VM ~2.6 s callback stalls)
//
//  Setting [soundlibrary] hostProcesses (MS_VST3_HOST_PROCESSES wins): 0 in MuseScore's process (as before), K > 0 up to K
//  helpers, -1 (the default) automatic: one helper per physical core but one (at least 2 where there are 2 or more cores
//  with 4 or more logical processors: the VM's 2 cores loaded 1.75x faster in 2 helpers), never more than the instances
//  a load needs. Helpers start when a batch of loads starts (each loading the plug-in's module meanwhile) and are kept
//  for the session; an instance goes to the helper that already hosts the same patch (Kontakt shares its samples within
//  a process: the even levels' measurer's copies), else to the helper loading the fewest, then holding the fewest.
//  If no helper can be started (the executable missing, the system refuses), the instance is made in MuseScore's process
//  as before, said once in load times.log (setLog).
//
//  A helper that dies (crash, killed) is noticed at once (its pipe closes; its process handle / waitpid): every caller
//  waiting on it is woken, its instances play silence from then on (alive() false), MuseScore goes on.
//  SoundLibraryHost::checkHostProcesses says so (status bar, load times.log), stops a playback that used them, loads the
//  lost instances again (a new helper, from their setups and prepared states) and plays on.
//
//  kheap (processallocator.h) in a helper: ProcessAllocator::start() as in MuseScore, i.e. #480's scoped mode: mimalloc
//  only on a thread inside a LoadScope (Vst3Plugin::setState, here on the helper's main thread, where Kontakt does the
//  work, so the load gets its speed-up back), never on the helper's audio thread. MS_KHEAP_OFF=1 reaches the helpers.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#ifndef __VST3REMOTE_H__
#define __VST3REMOTE_H__

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <QByteArray>
#include <QString>

namespace Ms {
namespace Vst3Remote {

// control-channel requests (the arguments after op, id and the sequence number; -> the answer)
enum class Op : quint32 {
      LOAD = 2,         // path, sampleRate, maxBlock, segment name, command capacity, environment -> ok, error, name, single, times
      DESTROY,          // -
      SET_OFFLINE,      // bool -> bool
      SET_SAMPLE_RATE,  // double -> bool
      STATE,            // -> bytes
      SET_STATE,        // bytes, environment -> bool, times
      KEY_NAMES,        // -> map, source
      IDLE,             // -
      DESCRIBE,         // has base, base (JSON) -> JSON
      COMPONENT_STATE,  // -> bytes
      CONTROLLER_STATE, // -> bytes
      PARAMETERS,       // -> list
      PARAMETER,        // param -> double
      PARAMETER_TEXT,   // param, value -> text
      SET_PARAMETER,    // param, value
      PARAMETER_ID,     // title -> long
      CONTROLLER_PARAMETER, // channel, cc -> long
      TAKE_REPORTED,    // -> list
      SHOW_EDITOR,      // title -> bool
      HOST_QUERIES,     // -> JSON
      QUIT,
      PARAMETER_VALUES, // -> the list with every parameter's value (the remote cache, after a load)
      BACKGROUND_MARK,  // - (BackgroundLoad: the helper's threads now)
      BACKGROUND_WAIT,  // limit -> seconds, quiet, threads, busiest, memoryMB
      CLOSE_EDITOR,     // -
      SET_CONTROLLER_PARAMETER, // param, value (setParameter's controller side; its processor side: CMD_EDIT)
      };

// audio-thread calls since the last block (the audio channel's commands), replayed in order
struct Command {
      qint32 kind;                  // CommandKind
      qint32 i1, i2, i3, i4;
      float f;
      double x, y;
      };
// (CMD_EDIT: a GUI thread's setParameter, the processor's side: taken at the next block as Vst3Plugin::processDirect takes the
// edits, after the block's own commands; the controller's side goes to the helper's main thread)
enum CommandKind : qint32 { CMD_MIDI = 1, CMD_PITCH, CMD_ALL_NOTES_OFF, CMD_QUEUE_PARAMETER, CMD_EDIT };

// what a request on the audio channel does
enum RequestKind : qint32 {
      REQ_PROCESS = 1,      // the commands, then process(frames); the output in the segment (or kept: REQ_FETCH)
      REQ_COMMANDS,         // the commands only (more than the command area holds, or before a GUI call)
      REQ_FETCH,            // frames [offset, offset + count) of the last REQ_PROCESS longer than the output area
      REQ_SETTLE,           // the commands, then settle(seconds)
      };

class Process;

//---------------------------------------------------------
//   Instance
//    MuseScore's side of a remote instance (Vst3PluginPrivate holds it)
//---------------------------------------------------------

class Instance {
   public:
      struct Impl;                  // (its process wakes it when it ends)
   private:
      std::unique_ptr<Impl> d;
   public:
      Instance();
      ~Instance();                  // destroyed in the helper (without waiting)

      // a new instance in one of the helpers; nullptr: none (*error). *noHelper: no helper could be started (the caller
      // makes the instance in MuseScore's process). key: the patch it will load (a helper holding the same goes first)
      static std::unique_ptr<Instance> load(const QString& path, double sampleRate, int maxBlock, const QString& key,
                                            QString* error, QByteArray* loadReply, bool* noHelper);

      bool alive() const;
      int process() const;          // which helper (0 …)
      qint64 pid() const;           // its process id (tests, the editor's foreground permission)
      bool busy() const;            // its helper runs a long call now (or hasn't started yet)

      // control: the request's arguments -> the answer; empty with *ok false when the helper is gone. flush: the commands
      // still queued are sent first (the calls that may not run while process() runs). wait false: sent, not waited for
      QByteArray call(Op op, const QByteArray& args, bool* ok = nullptr, bool flush = false, bool wait = true);
      void loaded();                // its first setState is over (choosing a helper for the next load)

      // audio
      void command(const Command& c);
      // any thread (setParameter): the processor's side of an edit, for the next block (CMD_EDIT)
      void edit(unsigned id, double normalized);
      void restartTransport();      // any thread, lock-free
      void beginProcess(int frames);
      void process(int frames, float* interleavedStereo);   // adds the output (beginProcess first, if not yet)
      void settle(double seconds);
      double secondsSinceState() const;   // the helper's, as of its last request (any thread)
      };

// helpers for a batch of loads (started now, each loading the module): no more than processes() allows
void prepare(const QString& path, int instances);
// the setting resolved (MS_VST3_HOST_PROCESSES, else what setProcesses was given, else [soundlibrary] hostProcesses;
// -1: automatic): 0 in MuseScore's process. 0 also once no helper could be started (unavailable())
int processes();
void setProcesses(int k);
bool unavailable();                 // a helper couldn't be started this session: instances in MuseScore's process
// every helper told to quit and waited for (the command-line jobs' exit, before Vst3Plugin::unloadModules). quick: every
// helper ended at once instead (MuseScore's window closes: nothing a helper holds is kept, no load or teardown waited for)
void shutdown(bool quick = false);
int running();                      // helpers running now
std::vector<qint64> processIds();   // theirs (tests: a helper killed)
// what happened to the helpers (started, allocator, ended, fell back): a line each, for load times.log (any thread)
void setLog(std::function<void(const QString&)> log);
// helpers that ended without being told to (crashed, killed) since the last call, by index
std::vector<int> takeEnded();

// audio round trips (REQ_PROCESS): the caller's wait less the helper's own process() time, per block. One line: blocks,
// median / p99 / max µs of the overhead and of the whole round trip (MS_AUDIO_TIMING's line, pa.cpp). reset: cleared
QString timingSummary(bool reset);

// (Windows) the helper with this process id may bring its window to the front (the plug-in's editor)
void allowForeground(qint64 pid);

// the helper: MuseScoreLibraryHost's main
int childMain(int argc, char** argv);

} // namespace Vst3Remote
} // namespace Ms
#endif
