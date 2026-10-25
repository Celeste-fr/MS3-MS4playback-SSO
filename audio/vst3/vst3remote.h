//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Vst3Remote: hosted plug-in instances in separate host processes (msvst3host), so that several load
//  (Kontakt's setState: one CPU-bound thread per process) at the same time. A remote instance is a
//  Vst3Plugin like any other (vst3plugin.h): the child process runs a local Vst3Plugin and replays the
//  calls made on the remote one, in the same order, on the same kind of thread (GUI calls on its main
//  thread, audio calls on its audio thread), so it plays what the instance would play in MuseScore.
//
//    control   GUI-thread calls: a request and its answer over two pipes, one at a time per process
//    audio     per instance, a shared-memory segment: the audio thread's calls since the last block
//              (commands), the block's length, its output; a semaphore wakes the child, another the caller.
//              Synchronous: the block is rendered within the caller's process() (no added latency);
//              Vst3Synth posts every slot's block first (beginProcess), so the processes render at once
//
//  Setting [soundlibrary] hostProcesses (MS_VST3_HOST_PROCESSES wins): 0 in MuseScore's process (as
//  before), K > 0 up to K processes, -1 one per physical core. A process that dies leaves its instances
//  silent (alive() false); the others and MuseScore go on.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#ifndef __VST3REMOTE_H__
#define __VST3REMOTE_H__

#include <atomic>
#include <cstdint>
#include <memory>

#include <QByteArray>
#include <QString>

namespace Ms {
namespace Vst3Remote {

// GUI-thread calls (control channel)
enum class Op : quint32 {
      PREWARM = 1,      // path: the plug-in's module loaded (a process's first instance waits less)
      LOAD,             // id, path, sampleRate, maxBlock, segment name, command capacity, environment -> ok, error, name, times, single
      DESTROY,          // id
      SET_OFFLINE,      // id, bool -> bool
      SET_SAMPLE_RATE,  // id, double -> bool
      STATE,            // id -> bytes
      SET_STATE,        // id, bytes, environment (added by Instance::call) -> bool, times
      KEY_NAMES,        // id -> map, source
      IDLE,             // id
      DESCRIBE,         // id, has base, base (JSON) -> JSON
      COMPONENT_STATE,  // id -> bytes
      CONTROLLER_STATE, // id -> bytes
      PARAMETERS,       // id -> list
      PARAMETER,        // id, param -> double
      PARAMETER_TEXT,   // id, param, value -> text
      SET_PARAMETER,    // id, param, value
      PARAMETER_ID,     // id, title -> long
      CONTROLLER_PARAMETER, // id, channel, cc -> long
      TAKE_REPORTED,    // id -> list
      SHOW_EDITOR,      // id, title -> bool
      HOST_QUERIES,     // -> JSON
      QUIT,
      };

// audio-thread calls since the last block (the audio channel's commands), replayed in order
struct Command {
      qint32 kind;                  // CommandKind
      qint32 i1, i2, i3, i4;
      float f;
      double x, y;
      };
enum CommandKind : qint32 { CMD_MIDI = 1, CMD_PITCH, CMD_ALL_NOTES_OFF, CMD_QUEUE_PARAMETER };

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
//    the parent's side of a remote instance (Vst3PluginPrivate holds it)
//---------------------------------------------------------

class Instance {
   public:
      struct Impl;                  // (its process wakes it when it ends)
   private:
      std::unique_ptr<Impl> d;
   public:
      Instance();
      ~Instance();                  // destroyed in the child

      // a new instance in one of the processes; nullptr: none (error)
      static std::unique_ptr<Instance> load(const QString& path, double sampleRate, int maxBlock, QString* error,
                                            QByteArray* loadReply);

      bool alive() const;
      int process() const;          // which host process (0 …)

      // control: the request's arguments (after op and id) -> the answer; empty with *ok false when the process is gone.
      // flush: the commands still queued are sent first (the calls that may not run while process() runs)
      QByteArray call(Op op, const QByteArray& args, bool* ok = nullptr, bool flush = false);
      bool tryCall(Op op, const QByteArray& args, QByteArray* reply);   // (skipped while the process is busy)
      void loaded();                // its first setState is over (choosing a process for the next load)

      // audio
      void command(const Command& c);
      void restartTransport();      // any thread, lock-free
      void beginProcess(int frames);
      void process(int frames, float* interleavedStereo);   // adds the output (beginProcess first, if not yet)
      void settle(double seconds);
      double secondsSinceState() const;   // the child's, as of its last request (any thread)
      };

// processes for a load batch of instances (started now, each loading the module): no more than the setting allows
void prepare(const QString& path, int instances);
// the setting (MS_VST3_HOST_PROCESSES, else what setProcesses was given): 0 in-process; resolved (-1: auto)
int processes();
void setProcesses(int k);
// every host process told to quit, waited for (MuseScore's exit, Vst3Plugin::unloadModules)
void shutdown();
int running();                      // host processes running now

// the child: msvst3host's main
int childMain(int argc, char** argv);

} // namespace Vst3Remote
} // namespace Ms
#endif
