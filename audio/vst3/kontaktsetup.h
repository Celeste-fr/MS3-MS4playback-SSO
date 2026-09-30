//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  KontaktSetup: a Kontakt patch's setup made from its .nki file, without loading the patch by
//  hand in Kontakt's window (the owner, 2026-09-27: MuseScore sets every patch up by itself, at
//  the library's defaults; tools/soundlibraries/make_setups.py is the same in Python, and
//  CLAUDE.md › "Setups made from the .nki files" tells how it was found).
//
//  Kontakt's state (what VST 3's getState gives, the component part of a .vst3state setup) is an
//  NI container like an .nki: "hsin" items (a 64-bit size, a 32-byte header, a stack of data
//  chunks, a version, children), some chunks holding a sub-tree (FastLZ compressed). Its preset
//  data is a Kontakt multi: a BANK whose SLOT_LIST holds the loaded programs, and the bank's
//  sample list (FILENAME_LIST_EX). A patch's .nki holds the program and its sample list (paths
//  relative to the .nki). So Kontakt with the patch loaded is Kontakt with nothing loaded (a
//  fresh instance's state) with:
//    - the .nki's program in the first slot (the slot's container as Kontakt 8.9 writes it),
//      its script's saved values set where asked (SSO: "$iooxo" 3, UACC switching);
//    - the .nki's sample list, every path made absolute from the .nki's folder;
//    - the .nki's authorization (the library's SNPID) and the library fields of its sound header;
//    - the multi marked as not empty (MULTI_CONFIGURATION, and the marker after the preset data:
//      the .nki's, a7636734; Kontakt's with nothing loaded, 8565620d, makes Kontakt refuse it).
//  Compared with the owner's own setup of Violins 1 (Kontakt 8.9) only the save time and two ids
//  differ. The script's code is copied as it is, never read out. Nothing is decrypted: an .nki
//  whose preset data is encrypted can't be used (SSO's aren't).
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#ifndef __KONTAKTSETUP_H__
#define __KONTAKTSETUP_H__

#include <map>

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace Ms {
namespace KontaktSetup {

// FastLZ (levels 1 and 2 read; level 1 written), NI's compression
QByteArray fastlzDecompress(const QByteArray& src, int size, bool* ok);
QByteArray fastlzCompress(const QByteArray& data);

// Kontakt's state with the .nki's patch loaded, from Kontakt's state with nothing loaded;
// values: script values to set (name -> value; only where the patch's script has that name with
// a value of the same length); valuesSet: how many were set
QByteArray fromEmpty(const QByteArray& emptyComponent, const QByteArray& nki, const QString& nkiFolder,
                     const std::map<QString, QByteArray>& values, QString* error, int* valuesSet = nullptr);

// what is in a state or an .nki (for checks and tests)
QByteArray slotProgram(const QByteArray& component, QString* error);   // a state's first slot's program
QByteArray nkiProgram(const QByteArray& nki, QString* error);
QString programName(const QByteArray& program);
std::map<QString, QByteArray> scriptValues(const QByteArray& program); // all its scripts' values
QStringList samplePaths(const QByteArray& component, QString* error);   // a state's sample list
QByteArray presetTail(const QByteArray& data);  // what follows a state's or an .nki's preset data (its marker)
// the version of a state's sample list (FILENAME_LIST_EX): 2 as in an .nki (a setup made from one), 3
// as Kontakt 8 writes it (its own state, which it loads about 20 times faster); -1: none, unreadable
int sampleListVersion(const QByteArray& component);

// a state with its first slot's script values set (as fromEmpty sets them in an .nki's program: only
// where the patch's script has that name with a value of the same length), everything else kept (so
// Kontakt's own state stays its own: its sample list, its program as it saved it). For the load
// times measurement's probe of the patch's saved script values (which ones unload samples)
QByteArray withScriptValues(const QByteArray& component, const std::map<QString, QByteArray>& values, QString* error,
                            int* valuesSet = nullptr);

// the instrument's voice limit (Kontakt's instrument header › Max voices): a state with its first slot's
// program's limit set to maxVoices, everything else kept (the very bytes when it already is); *before:
// what it was (-1, and *error, when the program has none where Kontakt keeps it: the state comes back
// unchanged). Empty with *error when the state has no program in its first slot
QByteArray withMaxVoices(const QByteArray& component, int maxVoices, QString* error, int* before = nullptr);
int maxVoices(const QByteArray& program);       // a program's (slotProgram, nkiProgram); -1: not found

} // namespace KontaktSetup
} // namespace Ms
#endif
