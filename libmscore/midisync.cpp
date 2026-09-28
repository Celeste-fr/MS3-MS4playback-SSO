//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "midisync.h"

#include <algorithm>

namespace Ms {
namespace MidiSync {

QString toString(const Message& m)
      {
      const QString t = QString::number(m.seconds, 'f', 4);
      switch (m.status) {
            case SONGPOS:  return QString("%1 SPP %2").arg(t).arg(m.value);
            case CLOCK:    return QString("%1 clock %2").arg(t).arg(m.value);
            case START:    return QString("%1 start").arg(t);
            case CONTINUE: return QString("%1 continue").arg(t);
            case STOP:     return QString("%1 stop").arg(t);
            }
      return QString("%1 %2").arg(t).arg(m.status, 0, 16);
      }

std::vector<Message> schedule(const std::function<double(int)>& utick2seconds, int startUtick, double endSeconds, double periodSeconds)
      {
      std::vector<Message> out;
      auto push = [&out](const Message& m) { out.push_back(m); };
      Clock clock;
      const double t0 = utick2seconds(startUtick);
      clock.start(startUtick, t0, push);
      for (double t = t0; t < endSeconds; t += periodSeconds)
            clock.run(std::min(t + periodSeconds, endSeconds), utick2seconds, push);
      clock.stop(endSeconds, push);
      return out;
      }

}     // namespace MidiSync
}     // namespace Ms
