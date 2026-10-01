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
#include <vector>

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace Ms {
namespace KontaktSetup {

// FastLZ (levels 1 and 2 read; level 1 written), NI's compression
QByteArray fastlzDecompress(const QByteArray& src, int size, bool* ok);
QByteArray fastlzCompress(const QByteArray& data);

// Kontakt's state with the .nki's patch loaded, from Kontakt's state with nothing loaded;
// values: script values to set (name -> value, as Kontakt saves it: an array's elements separated
// by single spaces; where the patch's script has that name, of any length: a value longer or
// shorter than the saved one, as a Kickstart array with techniques switched on, resizes its
// entry); valuesSet: how many were set
// A Kickstart percussion patch's techniques, drums or mics switched on by values (%c2lsa, %x4jsr, %nvmxz
// against the .nki's): their sample groups loaded too (unpurgeSwitchedOn); groupsLoaded: how many
QByteArray fromEmpty(const QByteArray& emptyComponent, const QByteArray& nki, const QString& nkiFolder,
                     const std::map<QString, QByteArray>& values, QString* error, int* valuesSet = nullptr,
                     int* groupsLoaded = nullptr);

// Kickstart (the script of every SSO percussion patch) loads only the samples it plays, and a state keeps
// which groups are purged: a program (a PROGRAM body, its script values set) with the hit groups loaded
// that Kickstart's own rule loads with its values (technique on: %c2lsa, drum on: %x4jsr, the drum's mic
// on: %nvmxz) but not with defaults (the .nki's values) and that are purged; their zones too; every length
// unchanged. The program as it was when it isn't Kickstart's (or the rule doesn't give its flags at the
// defaults) or nothing is switched on. groups: how many groups were loaded
QByteArray unpurgeSwitchedOn(const QByteArray& program, const std::map<QString, QByteArray>& defaults,
                             int* groups = nullptr);
// a program's purged groups (their indexes in its group list)
std::vector<int> purgedGroups(const QByteArray& program);

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

// a state with its first slot's script values set (as fromEmpty sets them in an .nki's program: where
// the patch's script has that name, of any length), everything else kept (so
// Kontakt's own state stays its own: its sample list, its program as it saved it). For the load
// times measurement's probe of the patch's saved script values (which ones unload samples)
QByteArray withScriptValues(const QByteArray& component, const std::map<QString, QByteArray>& values, QString* error,
                            int* valuesSet = nullptr);

} // namespace KontaktSetup
} // namespace Ms
#endif
