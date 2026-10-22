//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  PluginState: load-time settings written into a non-Kontakt plug-in's component state (SoundLib::Controller::state:
//  a setting the plug-in keeps only in its state, no parameter, no key; Ample's sample set Finger / Pick / Strum,
//  <DBName Value="AGMFg"/>). The Kontakt counterpart is KontaktSetup::withScriptValues (a patch's script values); this
//  one knows nothing of Kontakt.
//
//  A state is XML text inside the component bytes. The element and attribute set, by the value's name:
//    "Name"             <Name Value="…"/>                   (the first such element)
//    "Parameter:Title"  <Parameter Name="Title" Value="…"/>  (Ample's state keeps every setting so, 330 in AGM 4.1.0,
//                                                              208 of them no VST 3 parameter)
//    "Name[Key=k]"      <Name … Key="k" … Value="…"/>       (the first <Name> whose attribute Key is k: Ample's velocity
//                                                              layer thresholds, <Layer ID="1" Value="96"/>)
//    "Name[Key=k]@Attr" its attribute Attr; with no such element one is added, <Name Key="k" Attr="…"/>, after the last
//                       <Name> (else before the root's end tag): Ample's MIDI-learn map, <Control ID="12"
//                       Parameter="M-Master Vol"/> = CC7 (ID 5 + CC), which a preset may have dropped
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

// a JUCE PropertiesFile's value (<PROPERTIES><VALUE name="…" val="…"/>: a plug-in's global settings file, e.g. Ample's
// Documents/Ample Sound/AGM/Configs/AGM_info.xml), null: not in it
QString propertiesValue(const QByteArray& xml, const QString& name);

// a Kontakt array file's value at index (an .nka as a script's save_array writes it: the array's name, "%name", on the
// first line, then one value a line; SSO's shared settings, Share__Settings.nka), null: not an array file or no such
// index
QString nkaValue(const QByteArray& nka, int index);

}     // namespace PluginState
}     // namespace Ms
#endif
