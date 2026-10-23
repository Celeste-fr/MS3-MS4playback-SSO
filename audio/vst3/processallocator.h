//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  ProcessAllocator: on Windows, mimalloc answers the C runtime's malloc family for the whole process
//  (kheap.dll next to MuseScore3Evo.exe: tools/soundlibraries/kheap/kheap.cpp has the rules that keep every
//  block on its own heap). The owner, 2026-10-23, after his PC's run of the sso-load kit: Kontakt's setState of
//  SSO's patches makes millions of small allocations (18.9 million for Violins 1 - Performance); with mimalloc
//  Whence's states loaded in -15 % (his PC) / -12 % (the VM), memory +17 %; renders null at the floor
//  (HANDOFF.md › SSO load). Always on, load time only, never a playback setting. MS_KHEAP_OFF=1: the system
//  allocator, to compare. Without kheap.dll, or when its hooks can't all be made: the system allocator, said in
//  description() (logged in load times.log). Linux and macOS: nothing.
//
//  MuseScore itself is built with the static CRT (/MT): its own allocations stay on the process heap; mimalloc answers
//  the DLLs on ucrtbase (Qt, Kontakt and the other plug-ins). A block crossing between the two (the static CRT and
//  ucrtbase share the process heap, so such frees worked before) still lands right: the static CRT's free / realloc /
//  _msize of a mimalloc block reach ntdll's HeapFree / HeapReAlloc / HeapSize, which kheap gives to mimalloc, and
//  ucrtbase's of a process-heap block go to the original. Not covered: _aligned_free in one CRT of an _aligned_malloc
//  block of the other (none known).
//
//  start() is the first thing main() does (before Qt's application and any plug-in); description() says which
//  allocator is in use and why.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#ifndef __PROCESSALLOCATOR_H__
#define __PROCESSALLOCATOR_H__

namespace Ms {
namespace ProcessAllocator {
void start();
const char* description();
bool mimalloc();
}
}

#endif
