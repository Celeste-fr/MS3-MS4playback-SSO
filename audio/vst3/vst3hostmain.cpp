//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  MuseScoreLibraryHost: the helper process that hosts the sound library's plug-in instances (vst3remote.h), started
//  by MuseScore; never by hand.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#include "vst3remote.h"

#ifdef _WIN32
#include <windows.h>
#include <cstdlib>
#endif

int main(int argc, char** argv)
      {
      return Ms::Vst3Remote::childMain(argc, argv);
      }

#ifdef _WIN32
// (a WIN32 executable, no console: its arguments as main has them)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
      {
      return main(__argc, __argv);
      }
#endif
