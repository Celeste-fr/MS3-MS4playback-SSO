//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

//  The host of the score-script library: scriptengine.h has the design.
//  Layout of this file: the marks' table, the snapshot (libmscore -> plain data), the Lua state and its limits,
//  the host callbacks (plain data -> edits), run / the editor's helpers.

#include "scriptengine.h"

#include <cstdlib>
#include <cstring>
#include <limits>
#include <QCryptographicHash>
#include <QHash>

#include "libmscore/score.h"
#include "libmscore/part.h"
#include "libmscore/staff.h"
#include "libmscore/measure.h"
#include "libmscore/segment.h"
#include "libmscore/chord.h"
#include "libmscore/note.h"
#include "libmscore/tie.h"
#include "libmscore/slur.h"
#include "libmscore/articulation.h"
#include "libmscore/fermata.h"
#include "libmscore/dynamic.h"
#include "libmscore/stafftext.h"
#include "libmscore/instrument.h"
#include "libmscore/instrtemplate.h"
#include "libmscore/key.h"
#include "libmscore/pitchspelling.h"
#include "libmscore/navigate.h"
#include "libmscore/sym.h"
#include "libmscore/undo.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include "scorescript_lua.h"        // generated: scorescript.lua as bytes (embed_lua.cmake)

namespace Ms {

//---------------------------------------------------------
//   marks
//    the names a script uses and the symbols they stand for. The first symbol is the one added (above: layout
//    turns it below where it belongs); every listed symbol counts as "has".
//---------------------------------------------------------

struct MarkDef {
      const char* name;
      bool fermata;
      SymId syms[6];
      };

static const MarkDef MARKS[] = {
      { "staccato",      false, { SymId::articStaccatoAbove, SymId::articStaccatoBelow, SymId::noSym } },
      { "staccatissimo", false, { SymId::articStaccatissimoAbove, SymId::articStaccatissimoBelow,
                                  SymId::articStaccatissimoWedgeAbove, SymId::articStaccatissimoWedgeBelow,
                                  SymId::articStaccatissimoStrokeAbove, SymId::articStaccatissimoStrokeBelow } },
      { "tenuto",        false, { SymId::articTenutoAbove, SymId::articTenutoBelow, SymId::noSym } },
      { "accent",        false, { SymId::articAccentAbove, SymId::articAccentBelow, SymId::noSym } },
      { "marcato",       false, { SymId::articMarcatoAbove, SymId::articMarcatoBelow, SymId::noSym } },
      { "portato",       false, { SymId::articTenutoStaccatoAbove, SymId::articTenutoStaccatoBelow, SymId::noSym } },
      { "fermata",       true,  { SymId::fermataAbove, SymId::fermataBelow, SymId::noSym } },
      { "upbow",         false, { SymId::stringsUpBow, SymId::noSym } },
      { "downbow",       false, { SymId::stringsDownBow, SymId::noSym } },
      };

static const MarkDef* markDef(const QString& name)
      {
      for (const MarkDef& d : MARKS)
            if (name == d.name)
                  return &d;
      return nullptr;
      }

static bool hasSym(const MarkDef& d, SymId id)
      {
      for (SymId s : d.syms) {
            if (s == SymId::noSym)
                  break;
            if (s == id)
                  return true;
            }
      return false;
      }

QStringList ScriptEngine::markNames()
      {
      QStringList l;
      for (const MarkDef& d : MARKS)
            l << d.name;
      return l;
      }

//---------------------------------------------------------
//   fermataOf
//    the fermata over a chord: an annotation of its segment on the chord's staff
//---------------------------------------------------------

static Fermata* fermataOf(const Chord* c)
      {
      if (c->isGrace() || !c->segment())
            return nullptr;
      for (Element* e : c->segment()->annotations())
            if (e->isFermata() && e->staffIdx() == c->staffIdx())
                  return toFermata(e);
      return nullptr;
      }

static QStringList marksOf(const Chord* c)
      {
      QStringList l;
      for (const MarkDef& d : MARKS) {
            bool has = false;
            if (d.fermata)
                  has = fermataOf(c) != nullptr;
            else
                  for (const Articulation* a : c->articulations())
                        if (hasSym(d, a->symId()))
                              has = true;
            if (has)
                  l << d.name;
            }
      return l;
      }

//---------------------------------------------------------
//   snapshot helpers
//---------------------------------------------------------

static bool startsNote(const Chord* c)
      {
      for (const Note* n : c->notes())
            if (!n->tieBack())
                  return true;
      return false;
      }

// the level a dynamic leaves in force ("" = it changes nothing: sf, sfz, rfz, a custom text …)
static QString dynamicLevel(const Dynamic* d)
      {
      static const char* const levels[] = { "pppppp", "ppppp", "pppp", "ppp", "pp", "p", "mp", "mf", "f", "ff", "fff",
                                            "ffff", "fffff", "ffffff" };
      const QString name = d->dynamicTypeName();
      for (const char* l : levels)
            if (name == l)
                  return name;
      if (name == "fp" || name == "sfp")
            return "p";
      if (name == "sfpp")
            return "pp";
      if (name == "pf")
            return "f";
      return QString();
      }

static QString noteName(int tpc, int pitch, int* octave)
      {
      static const char letters[] = "FCGDAEB";
      const int t = tpc + 1;                          // F double flat = 0
      const int alter = (t >= 0 ? t / 7 : -((6 - t) / 7)) - 2;
      QString s(QChar(letters[((t % 7) + 7) % 7]));
      for (int i = 0; i < qAbs(alter); ++i)
            s += alter < 0 ? 'b' : '#';
      const int o = (pitch - alter) / 12 - 1;
      if (octave)
            *octave = o;
      return s + QString::number(o);
      }

// scale degree 1-7 of a note name in the key signature in force (letters only, as Score::scaleDegreeStep)
static int degreeOf(int tpc, const KeySigEvent& key)
      {
      int fifths = key.isValid() ? int(key.key()) : 0;
      switch (key.mode()) {
            case KeyMode::MINOR:
            case KeyMode::AEOLIAN:    fifths += 3; break;
            case KeyMode::DORIAN:     fifths += 2; break;
            case KeyMode::PHRYGIAN:   fifths += 4; break;
            case KeyMode::LYDIAN:     fifths -= 1; break;
            case KeyMode::MIXOLYDIAN: fifths += 1; break;
            case KeyMode::LOCRIAN:    fifths += 5; break;
            default: break;
            }
      return ((tpc2step(tpc) - tpc2step(Tpc::TPC_C + fifths)) % 7 + 7) % 7 + 1;
      }

static ScriptPart partInfo(const Part* p)
      {
      ScriptPart sp;
      sp.name = p->partName().isEmpty() ? p->longName() : p->partName();
      const Instrument* in = p->instrument();
      const InstrumentTemplate* t = nullptr;
      if (in) {
            sp.instrument = in->getId();
            if (!sp.instrument.isEmpty())
                  t = searchTemplate(sp.instrument);
            if (!t && !in->instrumentId().isEmpty())
                  t = searchTemplateForMusicXmlId(in->instrumentId());
            }
      if (t) {
            if (sp.instrument.isEmpty())
                  sp.instrument = t->id;
            if (t->family)
                  sp.family = t->family->id;
            for (const InstrumentGroup* g : instrumentGroups)
                  for (const InstrumentTemplate* gt : g->instrumentTemplates)
                        if (gt == t || gt->id == t->id) {
                              if (sp.group.isEmpty())
                                    sp.group = g->id;
                              }
            }
      return sp;
      }

//---------------------------------------------------------
//   snapshot
//---------------------------------------------------------

ScriptSnapshot ScriptEngine::snapshot(Score* s)
      {
      ScriptSnapshot snap;
      if (!s)
            return snap;
      MasterScore* score = s->masterScore();
      snap.score = score;
      QCryptographicHash hash(QCryptographicHash::Md5);

      QHash<const Part*, int> partIndex;
      for (const Part* p : score->parts()) {
            snap.parts.append(partInfo(p));
            partIndex.insert(p, snap.parts.size());
            hash.addData(snap.parts.last().name.toUtf8());
            hash.addData(snap.parts.last().instrument.toUtf8());
            }

      // slurs: for every chord that starts a note, its place under each slur over it (start order)
      QHash<const Chord*, QVector<QPair<int, int>>> slurs;
      for (const auto& i : score->spannerMap().map()) {
            Spanner* sp = i.second;
            if (!sp->isSlur() || toSlur(sp)->phraseMark())
                  continue;
            Element* se = sp->startElement();
            Element* ee = sp->endElement();
            if (!se || !se->isChordRest())
                  continue;
            QVector<const Chord*> under;
            const Fraction end = sp->tick2();
            int guard = 0;
            for (ChordRest* cr = toChordRest(se); cr && cr->tick() <= end && guard < 100000; cr = nextChordRest(cr, true), ++guard) {
                  if (cr->isChord() && startsNote(toChord(cr)))
                        under.append(toChord(cr));
                  if (cr == ee)
                        break;
                  }
            for (int k = 0; k < under.size(); ++k)
                  slurs[under[k]].append(qMakePair(k + 1, under.size()));
            }

      QVector<QString> dynamics(score->nstaves());
      int chordId = 0;
      int measureIndex = 0;

      auto addChord = [&](Chord* c, Chord* main, Measure* m, double beat) {
            ++chordId;
            const int staffIdx = main->staffIdx();
            const Part* part = main->part();
            const Fraction tick = main->tick();
            const Staff* staff = score->staff(staffIdx);
            const KeySigEvent key = staff ? staff->keySigEvent(tick) : KeySigEvent();
            const Instrument* in = part ? part->instrument(tick) : nullptr;
            const int transpose = in ? in->transpose().chromatic : 0;
            const QStringList marks = marksOf(c);
            const Note* up = c->upNote();
            const Note* down = c->downNote();
            for (Note* n : c->notes()) {
                  if (n->tieBack())             // a tie's continuation: part of the note it comes from
                        continue;
                  ScriptNote sn;
                  sn.note     = n;
                  sn.part     = partIndex.value(part, 0);
                  sn.measure  = m->no() + 1;
                  sn.index    = measureIndex;
                  sn.beat     = beat;
                  sn.tick     = tick.ticks();
                  Fraction len(0, 1);
                  int guard = 0;
                  for (const Note* t = n; t && guard < 10000; t = t->tieFor() ? t->tieFor()->endNote() : nullptr, ++guard)
                        len += c->isGrace() ? t->chord()->durationTypeTicks() : t->chord()->actualTicks();
                  sn.duration = len.numerator() * 4.0 / len.denominator();
                  sn.sounding = n->ppitch();
                  sn.pitch    = n->pitch() - transpose;
                  sn.name     = noteName(n->tpc2(), sn.pitch, &sn.octave);
                  sn.degree   = degreeOf(n->tpc(), key);
                  sn.voice    = c->voice() + 1;
                  sn.staff    = c->vStaffIdx() - (part ? part->startTrack() / VOICES : 0) + 1;
                  sn.tied     = n->tieFor() != nullptr;
                  sn.grace    = c->isGrace();
                  sn.chordId  = chordId;
                  sn.chord    = c->notes().size() > 1;
                  sn.top      = n == up;
                  sn.bottom   = n == down;
                  sn.slurs    = slurs.value(c);
                  sn.marks    = marks;
                  if (staffIdx >= 0 && staffIdx < dynamics.size())
                        sn.dynamic = dynamics[staffIdx];
                  snap.notes.append(sn);
                  const quintptr ptr = quintptr(n);
                  const int sig[3] = { sn.tick, n->pitch(), n->tpc() };
                  hash.addData(reinterpret_cast<const char*>(&ptr), sizeof(ptr));
                  hash.addData(reinterpret_cast<const char*>(sig), sizeof(sig));
                  }
            };

      for (Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
            ++measureIndex;
            const Fraction ts = m->timesig();
            Fraction unit(1, qMax(1, ts.denominator()));
            if (ts.numerator() > 3 && ts.numerator() % 3 == 0 && ts.denominator() >= 8)
                  unit = Fraction(3, ts.denominator());           // compound time: 6/8 has two beats
            for (Segment* seg = m->first(SegmentType::ChordRest); seg; seg = seg->next(SegmentType::ChordRest)) {
                  for (Element* e : seg->annotations()) {
                        if (!e->isDynamic())
                              continue;
                        const Dynamic* d = toDynamic(e);
                        const QString level = dynamicLevel(d);
                        if (level.isEmpty())
                              continue;
                        const int si = d->staffIdx();
                        const Part* dp = d->part();
                        for (int k = 0; k < dynamics.size(); ++k) {
                              const bool applies = d->dynRange() == Dynamic::Range::SYSTEM
                                 || (d->dynRange() == Dynamic::Range::PART && score->staff(k)->part() == dp)
                                 || k == si;
                              if (applies)
                                    dynamics[k] = level;
                              }
                        }
                  const Fraction pos = (seg->tick() - m->tick()) / unit;
                  const int whole = pos.numerator() / pos.denominator();
                  const double beat = 1.0 + whole + double(pos.numerator() - whole * pos.denominator()) / pos.denominator();
                  for (int track = 0; track < score->ntracks(); ++track) {
                        Element* e = seg->element(track);
                        if (!e || !e->isChord())
                              continue;
                        Chord* c = toChord(e);
                        for (Chord* g : c->graceNotes())
                              addChord(g, c, m, beat);
                        addChord(c, c, m, beat);
                        }
                  }
            }
      snap.signature = hash.result().toHex();
      return snap;
      }

//---------------------------------------------------------
//   the Lua state
//---------------------------------------------------------

void* ScriptEngine::alloc(void* ud, void* ptr, size_t osize, size_t nsize)
      {
      ScriptEngine* e = static_cast<ScriptEngine*>(ud);
      const size_t old = ptr ? osize : 0;
      if (nsize == 0) {
            free(ptr);
            e->_memory -= qMin(old, e->_memory);
            return nullptr;
            }
      if (nsize > old && e->_memory + (nsize - old) > e->_memoryLimit)
            return nullptr;                     // Lua raises "not enough memory"; the library reports it
      void* p = realloc(ptr, nsize);
      if (p)
            e->_memory = e->_memory - qMin(old, e->_memory) + nsize;
      return p;
      }

ScriptEngine* ScriptEngine::self(lua_State* L)
      {
      return *static_cast<ScriptEngine**>(lua_getextraspace(L));
      }

//---------------------------------------------------------
//   hook
//    every 1000 instructions: past the limit, or cancelled, raise the library's KILL error once. The script's
//    pcall (the library's wrapper) and the library's own pcalls pass it on.
//---------------------------------------------------------

void ScriptEngine::hook(lua_State* L, lua_Debug*)
      {
      ScriptEngine* e = self(L);
      e->_instructions += 1000;
      if (e->_instructions < 0)
            return;                             // fired already
      if (e->_cancel.load() || (e->_instructionLimit > 0 && e->_instructions > e->_instructionLimit)) {
            e->_instructions = std::numeric_limits<qint64>::min() / 2;
            lua_rawgeti(L, LUA_REGISTRYINDEX, e->_kill);
            lua_error(L);
            }
      }

QByteArray ScriptEngine::librarySource()
      {
      return QByteArray(reinterpret_cast<const char*>(scorescript_lua), int(scorescript_lua_len));
      }

static int traceless(lua_State* L)        // the message as it is (a table error stays a table)
      {
      return 1;
      }

//---------------------------------------------------------
//   protectedCall
//    f(L) with arg as its light userdata argument, inside lua_pcall: nothing here may raise outside one
//---------------------------------------------------------

bool ScriptEngine::protectedCall(int (*f)(lua_State*), void* arg, QString* error)
      {
      if (!L)
            return false;
      const int top = lua_gettop(L);
      if (!lua_checkstack(L, 8))
            return false;
      lua_pushcfunction(L, traceless);
      lua_pushcfunction(L, f);
      lua_pushlightuserdata(L, arg);
      const int status = lua_pcall(L, 1, 0, top + 1);
      if (status != LUA_OK && error) {
            const char* msg = lua_tostring(L, -1);
            *error = msg ? QString::fromUtf8(msg) : QString("script engine error %1").arg(status);
            }
      lua_settop(L, top);
      return status == LUA_OK;
      }

static void setFn(lua_State* L, const char* name, lua_CFunction f)
      {
      lua_pushcfunction(L, f);
      lua_setfield(L, -2, name);
      }

static int openLibrary(lua_State* L)
      {
      // only what the library needs; io, os, package and coroutine are never opened
      static const luaL_Reg libs[] = {
            { LUA_GNAME, luaopen_base }, { LUA_TABLIBNAME, luaopen_table }, { LUA_STRLIBNAME, luaopen_string },
            { LUA_MATHLIBNAME, luaopen_math }, { LUA_UTF8LIBNAME, luaopen_utf8 }, { LUA_DBLIBNAME, luaopen_debug },
            { nullptr, nullptr } };
      for (const luaL_Reg* l = libs; l->func; ++l) {
            luaL_requiref(L, l->name, l->func, 1);
            lua_pop(L, 1);
            }
      if (luaL_loadbufferx(L, reinterpret_cast<const char*>(scorescript_lua), scorescript_lua_len, "=scorescript", "t") != LUA_OK)
            return lua_error(L);
      lua_call(L, 0, 1);                        // -> the library's table M
      // the library has taken what it needs as locals: nothing that loads code or inspects the state stays
      // reachable, even though no script ever sees these globals (it sees the library's whitelist)
      static const char* const gone[] = { "debug", "load", "loadstring", "dofile", "loadfile", "require", "collectgarbage" };
      for (const char* g : gone) {
            lua_pushnil(L);
            lua_setglobal(L, g);
            }
      return 1;
      }

ScriptEngine::ScriptEngine()
      {
      L = lua_newstate(alloc, this);
      if (!L) {
            _initError = "no memory for the script engine";
            return;
            }
      *static_cast<ScriptEngine**>(lua_getextraspace(L)) = this;
      lua_pushcfunction(L, openLibrary);
      if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
            const char* msg = lua_tostring(L, -1);
            _initError = QString("score script library: %1").arg(msg ? msg : "?");
            lua_close(L);
            L = nullptr;
            return;
            }
      lua_getfield(L, -1, "KILL");
      _kill = luaL_ref(L, LUA_REGISTRYINDEX);
      // the host callbacks: M.setHost{add, remove, text, log}
      lua_getfield(L, -1, "setHost");
      lua_createtable(L, 0, 4);
      setFn(L, "add", hostAdd);
      setFn(L, "remove", hostRemove);
      setFn(L, "text", hostText);
      setFn(L, "log", hostLog);
      if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
            _initError = QString("score script library: %1").arg(lua_tostring(L, -1));
            lua_close(L);
            L = nullptr;
            return;
            }
      _lib = luaL_ref(L, LUA_REGISTRYINDEX);
      }

ScriptEngine::~ScriptEngine()
      {
      if (L)
            lua_close(L);
      }

//---------------------------------------------------------
//   setSnapshot
//---------------------------------------------------------

static void setStr(lua_State* L, const char* k, const QString& v)
      {
      const QByteArray a = v.toUtf8();
      lua_pushlstring(L, a.constData(), size_t(a.size()));
      lua_setfield(L, -2, k);
      }

static void setInt(lua_State* L, const char* k, lua_Integer v)
      {
      lua_pushinteger(L, v);
      lua_setfield(L, -2, k);
      }

static void setNum(lua_State* L, const char* k, double v)
      {
      if (v == double(qint64(v)))               // whole beats and lengths as integers: they print as 1, not 1.0
            lua_pushinteger(L, lua_Integer(v));
      else
            lua_pushnumber(L, v);
      lua_setfield(L, -2, k);
      }

static void setBool(lua_State* L, const char* k, bool v)
      {
      lua_pushboolean(L, v);
      lua_setfield(L, -2, k);
      }

struct SnapshotArg {
      const ScriptSnapshot* snap;
      int lib;
      };

static int pushSnapshot(lua_State* L)
      {
      const SnapshotArg* a = static_cast<const SnapshotArg*>(lua_touserdata(L, 1));
      const ScriptSnapshot& snap = *a->snap;
      luaL_checkstack(L, 16, "snapshot");
      lua_rawgeti(L, LUA_REGISTRYINDEX, a->lib);
      lua_getfield(L, -1, "setScore");
      lua_createtable(L, 0, 4);                                   // data
      setStr(L, "signature", QString::fromLatin1(snap.signature));

      const QStringList marks = ScriptEngine::markNames();
      lua_createtable(L, marks.size(), 0);
      for (int i = 0; i < marks.size(); ++i) {
            lua_pushstring(L, marks[i].toUtf8().constData());
            lua_rawseti(L, -2, i + 1);
            }
      lua_setfield(L, -2, "marks");

      lua_createtable(L, snap.parts.size(), 0);
      for (int i = 0; i < snap.parts.size(); ++i) {
            const ScriptPart& p = snap.parts[i];
            lua_createtable(L, 0, 4);
            setStr(L, "name", p.name);
            if (!p.instrument.isEmpty())
                  setStr(L, "instrument", p.instrument);
            if (!p.family.isEmpty())
                  setStr(L, "family", p.family);
            if (!p.group.isEmpty())
                  setStr(L, "group", p.group);
            lua_rawseti(L, -2, i + 1);
            }
      lua_setfield(L, -2, "parts");

      lua_createtable(L, snap.notes.size(), 0);
      for (int i = 0; i < snap.notes.size(); ++i) {
            const ScriptNote& n = snap.notes[i];
            lua_createtable(L, 0, 22);
            setInt(L, "id", i + 1);
            setInt(L, "part", n.part);
            setInt(L, "measure", n.measure);
            setInt(L, "index", n.index);
            setNum(L, "beat", n.beat);
            setInt(L, "tick", n.tick);
            setNum(L, "duration", n.duration);
            setInt(L, "pitch", n.pitch);
            setInt(L, "sounding", n.sounding);
            setStr(L, "name", n.name);
            setInt(L, "degree", n.degree);
            setInt(L, "octave", n.octave);
            setInt(L, "voice", n.voice);
            setInt(L, "staff", n.staff);
            setBool(L, "tied", n.tied);
            setBool(L, "grace", n.grace);
            setInt(L, "chordId", n.chordId);
            setBool(L, "chord", n.chord);
            setBool(L, "top", n.top);
            setBool(L, "bottom", n.bottom);
            lua_createtable(L, n.slurs.size(), 0);
            for (int k = 0; k < n.slurs.size(); ++k) {
                  lua_createtable(L, 0, 2);
                  setInt(L, "index", n.slurs[k].first);
                  setInt(L, "length", n.slurs[k].second);
                  lua_rawseti(L, -2, k + 1);
                  }
            lua_setfield(L, -2, "slurs");
            lua_createtable(L, n.marks.size(), 0);
            for (int k = 0; k < n.marks.size(); ++k) {
                  lua_pushstring(L, n.marks[k].toUtf8().constData());
                  lua_rawseti(L, -2, k + 1);
                  }
            lua_setfield(L, -2, "marks");
            if (!n.dynamic.isEmpty())
                  setStr(L, "dynamic", n.dynamic);
            lua_rawseti(L, -2, i + 1);
            }
      lua_setfield(L, -2, "notes");
      lua_call(L, 1, 0);
      return 0;
      }

bool ScriptEngine::setSnapshot(const ScriptSnapshot& snap)
      {
      if (!valid())
            return false;
      _snapshot = snap;
      SnapshotArg a { &_snapshot, _lib };
      QString error;
      if (!protectedCall(pushSnapshot, &a, &error)) {
            _initError = error;
            qWarning("ScriptEngine::setSnapshot: %s", qPrintable(error));
            return false;
            }
      return true;
      }

Note* ScriptEngine::noteOf(int id) const
      {
      if (id < 1 || id > _snapshot.notes.size())
            return nullptr;
      return _snapshot.notes[id - 1].note;
      }

QVector<Note*> ScriptEngine::notes(const QVector<int>& ids) const
      {
      QVector<Note*> l;
      for (int id : ids)
            if (Note* n = noteOf(id))
                  l.append(n);
      return l;
      }

//---------------------------------------------------------
//   edits
//    visible, editable marks only; idempotent (never Score::addArticulation, which toggles)
//---------------------------------------------------------

bool ScriptEngine::applyMark(Chord* c, const QString& name, bool add)
      {
      const MarkDef* d = markDef(name);
      Score* score = c->score();
      if (!d) {
            _editError = QString("unknown mark '%1'").arg(name);
            return false;
            }
      if (d->fermata) {
            Fermata* f = fermataOf(c);
            if (add && !f && !c->isGrace() && c->segment()) {
                  Fermata* nf = new Fermata(d->syms[0], score);
                  nf->setTrack(c->track());
                  nf->setPlacement(Placement::ABOVE);
                  nf->setParent(c->segment());
                  score->undoAddElement(nf);
                  }
            else if (!add && f)
                  score->undoRemoveElement(f);
            return true;
            }
      QVector<Articulation*> present;
      for (Articulation* a : c->articulations())
            if (hasSym(*d, a->symId()))
                  present.append(a);
      if (add && present.isEmpty()) {
            Articulation* a = new Articulation(d->syms[0], score);
            a->setParent(c);
            a->setTrack(c->track());            // (as Score::addArticulation: it reaches the parts)
            score->undoAddElement(a);
            }
      else if (!add)
            for (Articulation* a : present)
                  score->undoRemoveElement(a);
      return true;
      }

bool ScriptEngine::applyText(Chord* c, const QString& text)
      {
      Segment* seg = c->segment();
      if (!seg || c->isGrace()) {
            if (c->isGrace() && c->parent() && c->parent()->isChord())
                  seg = toChord(c->parent())->segment();
            if (!seg)
                  return true;
            }
      for (Element* e : seg->annotations())     // the same text there already: nothing to add
            if (e->isStaffText() && e->staffIdx() == c->staffIdx() && toStaffText(e)->plainText() == text)
                  return true;
      Score* score = c->score();
      StaffText* t = new StaffText(score);
      t->setTrack(c->staffIdx() * VOICES);
      t->setParent(seg);
      t->setPlainText(text);
      score->undoAddElement(t);
      return true;
      }

// (each: its arguments first, then the edit; luaL_error only when no C++ object is alive)

int ScriptEngine::hostAdd(lua_State* L)
      {
      const int id = int(luaL_checkinteger(L, 1));
      const char* value = luaL_checkstring(L, 2);
      ScriptEngine* e = self(L);
      if (!e->_editing)
            return 0;
      Note* n = e->noteOf(id);
      if (n && e->applyMark(n->chord(), QString::fromUtf8(value), true))
            return 0;
      return luaL_error(L, "can't add %s here", value);
      }

int ScriptEngine::hostRemove(lua_State* L)
      {
      const int id = int(luaL_checkinteger(L, 1));
      const char* value = luaL_checkstring(L, 2);
      ScriptEngine* e = self(L);
      if (!e->_editing)
            return 0;
      Note* n = e->noteOf(id);
      if (n && e->applyMark(n->chord(), QString::fromUtf8(value), false))
            return 0;
      return luaL_error(L, "can't remove %s here", value);
      }

int ScriptEngine::hostText(lua_State* L)
      {
      const int id = int(luaL_checkinteger(L, 1));
      const char* value = luaL_checkstring(L, 2);
      ScriptEngine* e = self(L);
      if (!e->_editing)
            return 0;
      Note* n = e->noteOf(id);
      if (n && e->applyText(n->chord(), QString::fromUtf8(value)))
            return 0;
      return luaL_error(L, "can't add text here");
      }

int ScriptEngine::hostLog(lua_State* L)
      {
      const char* kind = luaL_checkstring(L, 1);
      const char* text = luaL_checkstring(L, 2);
      const int line = int(luaL_optinteger(L, 3, 0));
      ScriptEngine* e = self(L);
      if (e->onLog) {
            ScriptLog l;
            l.kind = QString::fromUtf8(kind);
            l.text = QString::fromUtf8(text);
            l.line = line;
            e->onLog(l);
            }
      return 0;
      }

//---------------------------------------------------------
//   run
//---------------------------------------------------------

struct RunArg {
      int lib;
      QByteArray code;
      int flags;
      ScriptResult* result;
      };

static QString fieldStr(lua_State* L, int idx, const char* k)
      {
      lua_getfield(L, idx, k);
      size_t len = 0;
      const char* s = lua_isstring(L, -1) ? lua_tolstring(L, -1, &len) : nullptr;
      const QString r = s ? QString::fromUtf8(s, int(len)) : QString();
      lua_pop(L, 1);
      return r;
      }

static int fieldInt(lua_State* L, int idx, const char* k)
      {
      lua_getfield(L, idx, k);
      const int r = lua_isnumber(L, -1) ? int(lua_tointeger(L, -1)) : 0;
      lua_pop(L, 1);
      return r;
      }

static int runScript(lua_State* L)
      {
      RunArg* a = static_cast<RunArg*>(lua_touserdata(L, 1));
      lua_rawgeti(L, LUA_REGISTRYINDEX, a->lib);
      lua_getfield(L, -1, "run");
      lua_pushlstring(L, a->code.constData(), size_t(a->code.size()));
      lua_createtable(L, 0, 4);
      setBool(L, "preview", a->flags & ScriptEngine::Preview);
      setBool(L, "persist", a->flags & ScriptEngine::Persist);
      setBool(L, "echo", a->flags & ScriptEngine::Echo);
      setInt(L, "limit", 0);                    // the hook of this engine limits
      lua_call(L, 2, 1);
      const int r = lua_gettop(L);
      ScriptResult& res = *a->result;
      res.error     = fieldStr(L, r, "error");
      res.errorLine = fieldInt(L, r, "errorLine");
      lua_getfield(L, r, "preview");
      res.preview = lua_toboolean(L, -1);
      lua_pop(L, 1);
      lua_getfield(L, r, "log");
      for (lua_Integer i = 1, n = lua_Integer(lua_rawlen(L, -1)); i <= n; ++i) {
            lua_rawgeti(L, -1, i);
            ScriptLog l;
            l.kind = fieldStr(L, lua_gettop(L), "kind");
            l.text = fieldStr(L, lua_gettop(L), "text");
            l.line = fieldInt(L, lua_gettop(L), "line");
            res.log.append(l);
            lua_pop(L, 1);
            }
      lua_pop(L, 1);
      lua_getfield(L, r, "matched");
      for (lua_Integer i = 1, n = lua_Integer(lua_rawlen(L, -1)); i <= n; ++i) {
            lua_rawgeti(L, -1, i);
            res.matched.append(int(lua_tointeger(L, -1)));
            lua_pop(L, 1);
            }
      lua_pop(L, 1);
      lua_getfield(L, r, "ops");
      res.changes = int(lua_rawlen(L, -1));
      lua_pop(L, 1);
      return 0;
      }

ScriptResult ScriptEngine::run(const QString& code, int flags)
      {
      ScriptResult res;
      res.preview = flags & Preview;
      if (!valid()) {
            res.error = _initError.isEmpty() ? QString("the script engine isn't available") : _initError;
            return res;
            }
      RunArg a { _lib, code.toUtf8(), flags, &res };
      _cancel = false;
      _instructions = 0;
      lua_sethook(L, hook, LUA_MASKCOUNT, 1000);
      QString error;
      const bool ok = protectedCall(runScript, &a, &error);
      lua_sethook(L, nullptr, 0, 0);
      if (!ok) {                                // the library itself failed (out of memory, the limit inside it)
            res = ScriptResult();
            res.preview = flags & Preview;
            if (_instructions < 0)
                  res.error = "the script ran too long and was stopped (an endless loop?)";
            else if (error.contains("not enough memory"))
                  res.error = "the script used too much memory and was stopped";
            else
                  res.error = QString("script engine: %1").arg(error);
            }
      lua_gc(L, LUA_GCCOLLECT, 0);
      return res;
      }

//---------------------------------------------------------
//   runOnScore
//    the master score's notes, the script, its edits as one undo step (none when it fails or changes nothing)
//---------------------------------------------------------

ScriptResult ScriptEngine::runOnScore(Score* s, const QString& code, int flags)
      {
      ScriptResult res;
      if (!s || !valid()) {
            res.error = !s ? QString("no score") : (_initError.isEmpty() ? QString("the script engine isn't available") : _initError);
            return res;
            }
      MasterScore* score = s->masterScore();
      if (!setSnapshot(snapshot(score))) {
            res.error = _initError;
            return res;
            }
      if (flags & Preview)
            return run(code, flags);
      score->startCmd();
      _editing = true;
      res = run(code, flags);
      _editing = false;
      score->endCmd(!res.ok());                 // a failed script leaves the score as it was
      return res;
      }

//---------------------------------------------------------
//   the editor's helpers
//---------------------------------------------------------

struct CallArg {
      int lib;
      const char* fn;
      QByteArray arg;
      int number;
      QString first, second;
      int line;
      ScriptCompletion* completion;
      QVector<QPair<QString, QString>>* words;
      };

static int callHelper(lua_State* L)
      {
      CallArg* a = static_cast<CallArg*>(lua_touserdata(L, 1));
      lua_rawgeti(L, LUA_REGISTRYINDEX, a->lib);
      lua_getfield(L, -1, a->fn);
      if (a->words) {
            lua_call(L, 0, 1);
            lua_pushnil(L);
            while (lua_next(L, -2)) {
                  if (lua_type(L, -2) == LUA_TSTRING && lua_type(L, -1) == LUA_TSTRING)
                        a->words->append(qMakePair(QString::fromUtf8(lua_tostring(L, -2)), QString::fromUtf8(lua_tostring(L, -1))));
                  lua_pop(L, 1);
                  }
            return 0;
            }
      lua_pushlstring(L, a->arg.constData(), size_t(a->arg.size()));
      if (a->completion) {
            lua_pushinteger(L, a->number);
            lua_call(L, 2, 1);
            const int r = lua_gettop(L);
            a->completion->from = fieldInt(L, r, "from");
            a->completion->to   = fieldInt(L, r, "to");
            lua_getfield(L, r, "items");
            for (lua_Integer i = 1, n = lua_Integer(lua_rawlen(L, -1)); i <= n; ++i) {
                  lua_rawgeti(L, -1, i);
                  ScriptCompletion::Item it;
                  it.word   = fieldStr(L, lua_gettop(L), "word");
                  it.kind   = fieldStr(L, lua_gettop(L), "kind");
                  it.help   = fieldStr(L, lua_gettop(L), "help");
                  it.insert = fieldStr(L, lua_gettop(L), "insert");
                  a->completion->items.append(it);
                  lua_pop(L, 1);
                  }
            return 0;
            }
      lua_call(L, 1, 2);
      if (lua_isstring(L, -2))
            a->first = QString::fromUtf8(lua_tostring(L, -2));
      if (lua_type(L, -1) == LUA_TSTRING)
            a->second = QString::fromUtf8(lua_tostring(L, -1));
      else if (lua_isnumber(L, -1))
            a->line = int(lua_tointeger(L, -1));
      return 0;
      }

QString ScriptEngine::check(const QString& code, int* line)
      {
      if (line)
            *line = 0;
      if (!valid())
            return QString();
      CallArg a { _lib, "check", code.toUtf8(), 0, QString(), QString(), 0, nullptr, nullptr };
      if (!protectedCall(callHelper, &a, nullptr))
            return QString();
      if (line)
            *line = a.line;
      return a.first;
      }

QString ScriptEngine::help(const QString& word, QString* kind)
      {
      if (!valid())
            return QString();
      CallArg a { _lib, "help", word.toUtf8(), 0, QString(), QString(), 0, nullptr, nullptr };
      if (!protectedCall(callHelper, &a, nullptr))
            return QString();
      if (kind)
            *kind = a.second;
      return a.first;
      }

ScriptCompletion ScriptEngine::complete(const QString& text, int cursor)
      {
      ScriptCompletion c;
      c.from = c.to = cursor;
      if (!valid())
            return c;
      const QByteArray utf8 = text.toUtf8();
      const int bytes = text.left(cursor).toUtf8().size();
      CallArg a { _lib, "complete", utf8, bytes, QString(), QString(), 0, &c, nullptr };
      if (!protectedCall(callHelper, &a, nullptr)) {
            c = ScriptCompletion();
            c.from = c.to = cursor;
            return c;
            }
      // the library counts bytes from 1; the editor characters from 0
      c.from = QString::fromUtf8(utf8.left(qMax(0, c.from - 1))).size();
      c.to   = cursor;
      return c;
      }

QVector<QPair<QString, QString>> ScriptEngine::words()
      {
      QVector<QPair<QString, QString>> w;
      if (!valid())
            return w;
      CallArg a { _lib, "words", QByteArray(), 0, QString(), QString(), 0, nullptr, &w };
      protectedCall(callHelper, &a, nullptr);
      return w;
      }

void ScriptEngine::resetConsole()
      {
      if (!valid())
            return;
      CallArg a { _lib, "resetConsole", QByteArray(), 0, QString(), QString(), 0, nullptr, nullptr };
      protectedCall(callHelper, &a, nullptr);
      }

} // namespace Ms
