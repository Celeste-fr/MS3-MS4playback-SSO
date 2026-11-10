//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __SCRIPTENGINE_H__
#define __SCRIPTENGINE_H__

#include <atomic>
#include <functional>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QByteArray>

struct lua_State;
struct lua_Debug;

namespace Ms {

class Score;
class MasterScore;
class Note;
class Chord;

//---------------------------------------------------------
//   Score scripting in Lua (the console: mscore/scriptconsole.h; `mscore --lua script.lua score`)
//
//   A script queries the notes of a score and adds marks:
//         findall(strings, 1 <= measure <= 16, slur, slur.index == 1).add(marcato)
//   The language around those calls is Lua 5.4 (thirdparty/lua). The query library itself is written in Lua,
//   scorescript.lua in this folder (its header is the language's design: the source rewrite, conditions, names,
//   the sandbox), embedded at build time (embed_lua.cmake -> scorescript_lua.h) so that MuseScore, the browser
//   playground and a plain lua run the same code. This class is its host: it owns a lua_State, gives the library
//   the score as plain data and applies what a script asks for.
//
//   Why libmscore and not mscore/plugin (where the QML plug-in API lives): the engine needs only libmscore, and
//   here mtest/libmscore/scripting can test it, and the command line can run it, without the GUI.
//
//   Three steps, so that a preview can run off the GUI thread:
//    1. ScriptEngine::snapshot(score)  (the score's thread) walks the MASTER score once into a ScriptSnapshot:
//       parts (name, instrument id, family, group: the sections come from those, never from the part's name) and
//       notes. A note is a notehead that starts a sound: tie continuations are folded into their first notehead
//       (duration = the whole tied length), grace notes are listed with grace = true, rests never. Its values:
//       measure (the printed number, Measure::no() + 1), index (counted from 1), beat (in beats of the time
//       signature: 2/2 half notes, 6/8 dotted quarters; 1-based), tick, duration (quarters), pitch / name / octave
//       (written), sounding, degree (from the key signature in force and its mode, major when unknown), voice,
//       staff (of the part, where the note is drawn: cross-staff notes count on the staff they sit on), slurs
//       over its chord (index / length counted in chords that start a note, phrase marks left out), the chord's
//       marks, the dynamic in force (levels pppppp … ffffff; fp, sfp leave p; sf, sfz, rfz … change nothing; none
//       yet: no dynamic).
//    2. setSnapshot() gives it to the library (any thread, but always the engine's own).
//    3. run(): the script. Without Preview, add / remove / text reach the score at once through the library's
//       host callbacks, so run() without Preview belongs between startCmd() and endCmd() on the score's thread:
//       runOnScore() does all of that, one undo step per run, rolled back whole when the script fails.
//       With Preview nothing is called and nothing persistent changes: such a run needs no score at all.
//
//   Scripts write only what is visible and editable in the score: articulations (idempotent: never toggled off,
//   one per chord), fermatas, staff text.
//
//   Sandbox: only base / table / string / math / utf8 (and debug, for the library alone) are opened; the library
//   shows a script a whitelist of copies (scorescript.lua). A count hook stops a script after instructionLimit()
//   (and at cancel(), which any thread may call) with an error no pcall in the script can swallow; the state's
//   allocator refuses to grow past memoryLimit(). Known gap: time spent inside one C function (a pathological
//   string pattern) isn't counted.
//
//   Lua is built as C, its errors are longjmps: every call into the state happens inside lua_pcall, and the C
//   functions in scriptengine.cpp check their arguments before they make a C++ object.
//
//   Untested until the build: see HANDOFF.md › Lua console.
//---------------------------------------------------------

struct ScriptPart {
      QString name;           // Part::partName()
      QString instrument;     // Instrument::getId(), e.g. "violin"
      QString family;         // the instrument template's family id, e.g. "orchestral-strings"
      QString group;          // its group's id, e.g. "strings"
      };

struct ScriptNote {
      Note* note      { nullptr };  // valid while the score isn't edited (ScriptSnapshot::signature)
      int part        { 0 };        // 1-based index into ScriptSnapshot::parts
      int measure     { 0 };
      int index       { 0 };
      double beat     { 1.0 };
      int tick        { 0 };
      double duration { 0.0 };
      int pitch       { 0 };
      int sounding    { 0 };
      QString name;
      int degree      { 0 };
      int octave      { 0 };
      int voice       { 1 };
      int staff       { 1 };
      bool tied       { false };
      bool grace      { false };
      int chordId     { 0 };
      bool chord      { false };
      bool top        { false };
      bool bottom     { false };
      QVector<QPair<int, int>> slurs;     // index, length; the latest-starting slur last
      QStringList marks;
      QString dynamic;                    // empty: none in force
      };

struct ScriptSnapshot {
      MasterScore* score { nullptr };
      QVector<ScriptPart> parts;
      QVector<ScriptNote> notes;          // a note's id is its index + 1
      QByteArray signature;               // equal signatures: the same notes, selections stay valid
      };

struct ScriptLog {
      QString kind;                       // "print", "query" (a query's own result line), "value" (the console's echo)
      QString text;
      int line { 0 };                     // 0: unknown
      };

struct ScriptResult {
      QString error;                      // empty: the script ran through
      int errorLine { 0 };
      QVector<ScriptLog> log;
      QVector<int> matched;               // ids of every note a query of the run matched
      int changes { 0 };                  // marks and texts added or removed (in a preview: that would be)
      bool preview { false };
      bool ok() const { return error.isEmpty(); }
      };

struct ScriptCompletion {
      struct Item {
            QString word, kind, help, insert;
            };
      int from { 0 };                     // the completed word starts here (characters of the text)
      int to { 0 };
      QVector<Item> items;
      };

class ScriptEngine {
      lua_State* L { nullptr };
      int _lib  { -1 };                   // registry references: the library's table, its KILL error
      int _kill { -1 };
      QString _initError;
      ScriptSnapshot _snapshot;
      bool _editing { false };            // inside runOnScore: the host callbacks may edit
      QString _editError;

      std::atomic<bool> _cancel { false };
      qint64 _instructionLimit { 20000000 };
      qint64 _instructions { 0 };
      size_t _memoryLimit { size_t(512) * 1024 * 1024 };
      size_t _memory { 0 };

      static void* alloc(void* ud, void* ptr, size_t osize, size_t nsize);
      static void hook(lua_State*, lua_Debug*);
      static int hostAdd(lua_State*);
      static int hostRemove(lua_State*);
      static int hostText(lua_State*);
      static int hostLog(lua_State*);
      static ScriptEngine* self(lua_State*);

      bool protectedCall(int (*f)(lua_State*), void* arg, QString* error);
      Note* noteOf(int id) const;
      bool applyMark(Chord*, const QString& name, bool add);
      bool applyText(Chord*, const QString& text);

   public:
      enum Flag { Preview = 1, Persist = 2, Echo = 4 };

      ScriptEngine();
      ~ScriptEngine();
      ScriptEngine(const ScriptEngine&) = delete;
      ScriptEngine& operator=(const ScriptEngine&) = delete;

      bool valid() const                  { return L && _lib >= 0; }
      QString initError() const           { return _initError; }

      static ScriptSnapshot snapshot(Score*);
      static QStringList markNames();
      bool setSnapshot(const ScriptSnapshot&);
      const ScriptSnapshot& currentSnapshot() const { return _snapshot; }
      QVector<Note*> notes(const QVector<int>& ids) const;

      ScriptResult run(const QString& code, int flags = 0);
      ScriptResult runOnScore(Score*, const QString& code, int flags = 0);

      void cancel()                       { _cancel = true; }
      void setInstructionLimit(qint64 n)  { _instructionLimit = n; }
      qint64 instructionLimit() const     { return _instructionLimit; }
      void setMemoryLimit(size_t bytes)   { _memoryLimit = bytes; }
      void resetConsole();                // forget the console's variables

      // the editor's helpers (the library's M.check / M.complete / M.help / M.words)
      QString check(const QString& code, int* line = nullptr);
      ScriptCompletion complete(const QString& text, int cursor);
      QString help(const QString& word, QString* kind = nullptr);
      QVector<QPair<QString, QString>> words();        // word, kind

      std::function<void(const ScriptLog&)> onLog;     // each line as the script prints it (not in a preview)

      static QByteArray librarySource();               // scorescript.lua as embedded
      };

} // namespace Ms
#endif
