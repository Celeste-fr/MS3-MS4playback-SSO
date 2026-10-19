//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  PluginState: load-time settings written into a non-Kontakt plug-in's component state (SoundLib::Controller::state:
//  a setting the plug-in keeps only in its state, no parameter, no key; Ample's sample set Finger / Pick / Strum,
//  <DBName Value="AGMFg"/>). The Kontakt counterpart is KontaktSetup::withScriptValues (a patch's script values); this
//  one knows nothing of Kontakt.
//
//  A state is XML text inside the component bytes. Two element forms are set, by the value's name:
//    "Name"             <Name Value="…"/>                   (the first such element)
//    "Parameter:Title"  <Parameter Name="Title" Value="…"/>  (Ample's state keeps every setting so, 330 in AGM 4.1.0,
//                                                              208 of them no VST 3 parameter)
//  A value of the same length is replaced in place. One of another length needs the chunk's sizes: the JUCE VST 2 chunk
//  in a VST 3 wrapper that Ample's plug-ins write ("VstW" header, an FXB "CcnK"/"FBCh" bank whose chunk starts at 176
//  with "VC2!", the XML's length little-endian, the XML, JUCE's private data), whose three sizes are set again; any other
//  layout only takes values of the same length.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#ifndef __PLUGINSTATE_H__
#define __PLUGINSTATE_H__

#include <map>
#include <QByteArray>
#include <QString>

namespace Ms {
namespace PluginState {

// the component with the values set; empty on error (a name not found, a length the layout can't take: *error). set:
// how many were set (those already at the value count)
QByteArray withValues(const QByteArray& component, const std::map<QString, QByteArray>& values, QString* error = nullptr,
                      int* set = nullptr);

// the value of a name in the component (as above), empty: not found
QByteArray value(const QByteArray& component, const QString& name);

}     // namespace PluginState
}     // namespace Ms
#endif
