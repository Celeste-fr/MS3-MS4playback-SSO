//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  msvst3host: a host process for hosted plug-in instances (vst3remote.h), started by MuseScore.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#include "vst3remote.h"

int main(int argc, char** argv)
      {
      return Ms::Vst3Remote::childMain(argc, argv);
      }
