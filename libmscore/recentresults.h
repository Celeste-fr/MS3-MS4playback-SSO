//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __RECENTRESULTS_H__
#define __RECENTRESULTS_H__

#include <deque>
#include <utility>

#include <QString>

namespace Ms {

class MasterScore;

// RecentResults' tag for a computation on the score's playback: what its key may lack (contextlevel.cpp)
QString untrackedInputs(const MasterScore* score);

//---------------------------------------------------------
//   EditLag
//    the edit-lag speedups (results kept by their inputs, the even levels' plug-ins loaded off the main thread) are
//    sound-identical, so always on. MS_EDIT_LAG_OFF=1 (or setOn(false): tests, the lag probe's comparison blocks) runs
//    the code as it was before them, to compare speed and results
//---------------------------------------------------------

namespace EditLag {
bool on();
void setOn(bool on);
}

//---------------------------------------------------------
//   RecentResults
//    the last few results of a computation by the key of everything they depend on (the edit lag, branch edit-lag:
//    an undo or redo goes back to a state seen before). The newest first; the caller locks.
//    The tag: what may change the inputs without changing the key (ContextLevel::untrackedInputs: values written on
//    notes outside the undo stack, the global playback mode, all of the score's tags …). The newest entry is found by
//    its key alone, exactly as the single cached result it replaces was; an older one only while its tag is still the
//    same, so that it holds what a computation now would give
//---------------------------------------------------------

template <typename T, size_t N = 8>
class RecentResults {
      struct Item {
            QString key;
            QString tag;
            T value;
            };
      std::deque<Item> _items;

   public:
      const T* find(const QString& key, const QString& tag)
            {
            for (auto it = _items.begin(); it != _items.end(); ++it) {
                  if (it->key != key)
                        continue;
                  if (it == _items.begin())
                        return &it->value;
                  if (it->tag != tag || !EditLag::on())         // (off: the single result, as before)
                        return nullptr;
                  Item x = std::move(*it);
                  _items.erase(it);
                  _items.push_front(std::move(x));
                  return &_items.front().value;
                  }
            return nullptr;
            }
      void put(const QString& key, const QString& tag, const T& value)
            {
            for (auto it = _items.begin(); it != _items.end(); ++it)
                  if (it->key == key) {
                        _items.erase(it);
                        break;
                        }
            _items.push_front(Item { key, tag, value });
            while (_items.size() > N)
                  _items.pop_back();
            }
      void clear() { _items.clear(); }
      };

}     // namespace Ms
#endif
