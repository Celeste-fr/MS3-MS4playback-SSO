//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  KontaktSetup: a Kontakt patch's setup made from its .nki file, without loading the patch by
//  hand in Kontakt's window (the owner, 2026-09-27: MuseScore sets every patch up by itself, at
//  the library's defaults; tools/soundlibraries/make_setups.py is the same in Python, and
//  docs/HISTORY.md › "Setups made from the .nki files" tells how it was found).
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

// the instrument's voice limit (Kontakt's instrument header › Max voices): a state with its first slot's
// program's limit set to maxVoices, everything else kept (the very bytes when it already is); *before:
// what it was (-1, and *error, when the program has none where Kontakt keeps it: the state comes back
// unchanged). Empty with *error when the state has no program in its first slot
QByteArray withMaxVoices(const QByteArray& component, int maxVoices, QString* error, int* before = nullptr);
int maxVoices(const QByteArray& program);       // a program's (slotProgram, nkiProgram); -1: not found

// the instrument's Tune (Kontakt's instrument header › Tune, kept as a frequency ratio): a state with its
// first slot's program tuned by cents, everything else kept (the very bytes when it already is); *before: what
// it was in cents (NaN, and *error, when the program has none where Kontakt keeps it: the state comes back
// unchanged). Empty with *error when the state has no program in its first slot. For a tuning lane's copy in
// Live (Kontakt ignores VST 3 note tuning; All techniques patches don't bend)
QByteArray withTune(const QByteArray& component, double cents, QString* error, double* before = nullptr);
double tune(const QByteArray& program);         // a program's Tune in cents; NaN: not found

// a multi of the first slot's program: slot i a copy of it tuned by cents[i] (Tune as withTune; NaN or 0: kept),
// listening to MIDI channel i + 1 only (MULTI_CONFIGURATION's row i, byte 0: 0 Omni, n channel n); at most 16 slots.
// For a patch's tuning copies in Live: one Kontakt, each copy's techniques MIDI To its channel (plainliveset.h).
// Measured with SSO (VM, kthost, 2026-10-10): a 3-slot multi of Violins 1 at 0 / -50 / +50 cents plays as three
// instances (null -111 dB at a -35 dB signal), a 2-slot Violins 1 - Performance with bends on one channel as two
// (-108 dB, as two runs of one); each slot ~222 MB (All techniques) where an instance takes ~312 MB: Kontakt
// shares no samples between slots. Empty with *error when the state has no program in its first slot
QByteArray withSlots(const QByteArray& component, const std::vector<double>& cents, QString* error);
struct Slot {
      double cents { 0 };                 // its program's Tune (NaN: not found)
      int channel { -1 };                 // its MULTI_CONFIGURATION channel byte (0 Omni, n channel n)
      std::map<QString, QByteArray> values;     // its program's script values (scriptValues)
      };
std::vector<Slot> slotsOf(const QByteArray& component);   // a state's slots, in order

// SSO's string sections' Performance patches' random sources ([slurs] fixedTransitions): the intensities of a state's
// first slot's external modulators whose source is Kontakt's random generator (source type 10), in program order;
// empty (*error) when its group list can't be read. withRandomSources: the state with them set (values: one value for
// all, or one each), everything else kept (the very bytes when they already are); *count how many changed. Measured
// (Windows test VM, kthost, 2026-10-20, sso-legato-fixes RESULT): at 0, two renders of Whence null at -93 dB (Violas),
// -71 (Violins 1), -97 (Celli), -86 (Basses) against -7..-21 as shipped; each note as loud as the shipped takes' mean
// (median within 0.15 dB)
std::vector<float> randomSources(const QByteArray& component, QString* error);
QByteArray withRandomSources(const QByteArray& component, const std::vector<float>& values, QString* error,
                             int* count = nullptr);

} // namespace KontaktSetup
} // namespace Ms
#endif
