//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __LANEFOLDS_H__
#define __LANEFOLDS_H__

//---------------------------------------------------------
//   The automation editor's folding (automationlanes.h; the owner, 2026-10-12: "what if every instruments' automation
//   rows get shown at the same time, and I can expand or contract them at will?"), as Live's track headers: every
//   part with lanes always shows its header row; which parts' lanes are open is this, per score.
//   A part not listed is folded: new parts and a newly opened score start folded. The score's selection never changes
//   it. Kept for the session in memory only (not in the file: folding never marks a score changed); undo / redo keep
//   it, a part that no longer exists is dropped (keepOnly).
//   Alt-click on a header (Live's): every part to the clicked one's new state (toggleAll).
//   No Qt, no score: tested in tst_liveintegration (laneFolds).
//---------------------------------------------------------

#include <map>
#include <set>
#include <vector>

namespace Ms {

class Part;
class Score;

class LaneFolds {
      std::map<const Score*, std::set<const Part*>> _open;

   public:
      bool isOpen(const Score* s, const Part* p) const
            {
            auto it = _open.find(s);
            return it != _open.end() && it->second.count(p);
            }
      bool anyOpen(const Score* s) const
            {
            auto it = _open.find(s);
            return it != _open.end() && !it->second.empty();
            }
      void setOpen(const Score* s, const Part* p, bool on)
            {
            if (on) {
                  _open[s].insert(p);
                  return;
                  }
            auto it = _open.find(s);
            if (it == _open.end())
                  return;
            it->second.erase(p);
            if (it->second.empty())
                  _open.erase(it);
            }
      // the part's new state
      bool toggle(const Score* s, const Part* p)
            {
            const bool on = !isOpen(s, p);
            setOpen(s, p, on);
            return on;
            }
      // Alt-click on a header: every one of parts to the clicked part's new state
      bool toggleAll(const Score* s, const Part* clicked, const std::vector<const Part*>& parts)
            {
            const bool on = !isOpen(s, clicked);
            for (const Part* p : parts)
                  setOpen(s, p, on);
            setOpen(s, clicked, on);
            return on;
            }
      // the score's parts now (after undo / redo, a part removed): the others go
      void keepOnly(const Score* s, const std::set<const Part*>& parts)
            {
            auto it = _open.find(s);
            if (it == _open.end())
                  return;
            for (auto i = it->second.begin(); i != it->second.end(); )
                  i = parts.count(*i) ? std::next(i) : it->second.erase(i);
            if (it->second.empty())
                  _open.erase(it);
            }
      };

}     // namespace Ms
#endif
