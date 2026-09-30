//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  The sound library's controllers that are plug-in parameters (SoundLib::Controller::param:
//  Kontakt's "Mic 1 level", "Release" …) on the hosted instances: at each sync
//  (SoundLibraryHost::sync, the command-line export) and live, as the owner moves them in the
//  Controllers window (on every slot of the part at once: its patch, extras, copies for other
//  tunings). Vst3Plugin::setParameter hands the processor's change to the audio thread, so this
//  runs on the GUI thread during playback.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#ifndef __LIBRARYCONTROLLERS_H__
#define __LIBRARYCONTROLLERS_H__

#include <functional>
#include <map>
#include <vector>

#include "libmscore/partcontrollers.h"

namespace Ms {

class Part;
class Vst3Plugin;
class Vst3Synth;

namespace SoundLib { struct Route; }

namespace LibraryControllers {

// the route's controllers that are plug-in parameters, at the part's values; found by title
// (Vst3Plugin::parameterId). One the part has no value for plays as the patch has it: patchValues
// (the slot's record of the setup's value of each parameter a score set; null: a new instance, which
// has nothing to put back) keeps it, to put it back. skip: controller ids left alone (an automation
// lane plays them)
void applyParameters(Vst3Plugin* p, const SoundLib::Route& r, const std::map<const Part*, PartControllers::Values>& values,
                     std::map<unsigned, double>* patchValues, const std::vector<QString>& skip = {});

// the part's parameters on every loaded slot of it (routes: the score's), now; patchValues(slot): that
// slot's record (null: none). The slots set
std::vector<int> applyPart(Vst3Synth* vst, const std::vector<SoundLib::Route>& routes, const Part* part,
                           const std::map<const Part*, PartControllers::Values>& values,
                           const std::function<std::map<unsigned, double>*(int)>& patchValues,
                           const std::vector<QString>& skip = {});

}     // namespace LibraryControllers
}     // namespace Ms
#endif
