//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  PlaybackVerifier: MuseScore --verify-playback <score or folder> … (the owner, 2026-09-28:
//  "figure out a way to automatically verify that the plugin plays back properly"). Each score
//  is rendered as File › Export audio renders it, its sound library parts on the hosted plug-in
//  (offline), and again with MuseScore's built-in synth as the expectation; the rendered notes
//  (score->renderMidi, with the library's routes) are then checked against the audio
//  (audio/vst3/playbackverify.h: attacks, notes cut short, silence, clipping, timing drift). A
//  score with several parts: each library part is also rendered alone (both ways), so a finding
//  names its part. Each finding says where (measure, beat, part, pitches, time in the audio) and
//  what else happens there in the rendered events (the pedal, an articulation switch, dynamics,
//  the same key released just before), so its cause shows in the report; the counts of those per
//  kind of event, flagged against all strikes, too.
//
//  Output: <out>/Playback verify <date>/ and a zip of it: report.json (every finding with its
//  context), summary.txt (the same in lines), per analysed part "<score> [<part>] strikes.tsv" and
//  "notes.tsv" (every strike's and note's measures, to look again at the thresholds),
//  "<score> events.tsv" (the library's events as rendered), clips/ (2.5 s of the rendering and of
//  the built-in synth's around each finding, mono 16 bit, at most 40 a score) and with --verify-wav
//  the full renders. Run as a background process like the extract (musescore.cpp
//  verifyInBackground: its own setups copy, lock, log). tools/playbackverify/read_verify_report.py
//  reads it; VERIFY.md explains it for the owner and for agents.
//
//  --verify-shareable: only report.json and summary.txt (the flagged notes and their context): no
//  clips, no lists of every note and event, so a report on the owner's own music can be posted.
//
//  --verify-audio <file>: no rendering with the library; the file (an export of the score made
//  elsewhere, e.g. on the owner's PC) is checked instead, against the same score's events.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#ifndef __PLAYBACKVERIFIER_H__
#define __PLAYBACKVERIFIER_H__

#include <functional>

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace Ms {

class PlaybackVerifier {
   public:
      struct Options {
            QStringList inputs;           // scores and folders of scores
            QString out;                  // where the report folder goes (default: Documents/MuseScore Sound Library Check)
            QString audio;                // --verify-audio: an export to check instead of rendering
            bool wav { false };           // also the full renders
            bool shareable { false };     // only report.json and summary.txt: no clips, no note lists
            int maxClips { 40 };          // per score
            };
      static QStringList scoreFiles(const QStringList& inputs);
      // the report's folder, or empty on failure; log: progress lines
      static QString run(const Options& options, std::function<void(const QString&)> log, QString* zip = nullptr);
      };

} // namespace Ms
#endif
