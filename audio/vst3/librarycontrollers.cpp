//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#include "librarycontrollers.h"

#include <algorithm>

#include "libmscore/partplayback.h"
#include "libmscore/soundlibrary.h"
#include "vst3plugin.h"
#include "vst3synth.h"

namespace Ms {
namespace LibraryControllers {

//---------------------------------------------------------
//   applyParameters
//---------------------------------------------------------

void applyParameters(Vst3Plugin* p, const SoundLib::Route& r, const std::map<const Part*, PartControllers::Values>& values,
                     std::map<unsigned, double>* patchValues, const std::vector<QString>& skip)
      {
      if (!p || !r.instrument)
            return;
      for (const SoundLib::Controller& c : r.instrument->allControllers) {
            if (c.param.isEmpty() || std::find(skip.begin(), skip.end(), c.id) != skip.end())
                  continue;
            const int value = PartControllers::value(r.part, c, values);
            if (value < 0 && (!patchValues || patchValues->empty()))
                  continue;
            const long id = p->parameterId(c.param);
            if (value < 0) {                        // no value (another score's, or unticked): the patch's own again
                  if (id >= 0 && patchValues->count(unsigned(id))) {
                        p->setParameter(unsigned(id), patchValues->at(unsigned(id)));
                        patchValues->erase(unsigned(id));
                        }
                  continue;
                  }
            if (id >= 0 && patchValues && !patchValues->count(unsigned(id)))
                  (*patchValues)[unsigned(id)] = p->parameter(unsigned(id));
            if (id < 0) {
                  qWarning("Sound library: %s has no parameter \"%s\" (controller %s)", qPrintable(p->name()),
                           qPrintable(c.param), qPrintable(c.id));
                  continue;
                  }
            p->setParameter(unsigned(id), value / 127.0);
            }
      }

//---------------------------------------------------------
//   applyPart
//---------------------------------------------------------

std::vector<int> applyPart(Vst3Synth* vst, const std::vector<SoundLib::Route>& routes, const Part* part,
                           const std::map<const Part*, PartControllers::Values>& values,
                           const std::function<std::map<unsigned, double>*(int)>& patchValues,
                           const std::vector<QString>& skip)
      {
      std::vector<int> set;
      const Part* master = PartPlaybackModes::masterPart(part);
      if (!vst || !master)
            return set;
      for (const SoundLib::Route& r : routes) {
            if (PartPlaybackModes::masterPart(r.part) != master || !r.instrument || r.instrument->kit)
                  continue;
            const int slot = r.port * 16 + r.channel;
            Vst3Plugin* p = vst->plugin(slot);
            if (!p)
                  continue;
            applyParameters(p, r, values, patchValues ? patchValues(slot) : nullptr, skip);
            set.push_back(slot);
            }
      return set;
      }

}     // namespace LibraryControllers
}     // namespace Ms
