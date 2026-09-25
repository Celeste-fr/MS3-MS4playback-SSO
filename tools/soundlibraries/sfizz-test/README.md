# Sound library test with a real sampler (sfizz)

Kontakt and SSO can't run in the Linux container, so this is the closest stand-in. sfizz is
an open-source SFZ sampler with a VST 3 version. It maps MIDI CCs to parameters through
`IMidiMapping`, the way Kontakt does.

1. Build sfizz's VST 3:
   `git clone --recurse-submodules https://github.com/sfztools/sfizz-ui` and
   `cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DPLUGIN_LV2=OFF -DPLUGIN_LV2_UI=OFF -DPLUGIN_VST3=ON`.
   It needs libsamplerate, the xcb dev packages, cairo, pango, fontconfig and gtk3. The
   file dialog in its editor needs zenity.
2. Run `python3 make_uacc_sfz.py uaccsfz` (add `--ornaments` for tremolo and trill samples).
3. Save a setup in either of two ways:
   - `python3 make_setup.py uaccsfz/uacc-test.sfz "<dataPath>/soundlibraries/Spitfire Symphony Orchestra/Solo Violin 1.vst3state"`
     (the dataPath is `~/.local/share/MuseScore/MuseScore3Evo` here).
   - In the GUI (Xvfb): *View › Sound Library…* › *Show*, load the SFZ, then close the window.
4. Add to the user ini (`~/.config/MuseScore/MuseScore3Evo.ini`):
   ```
   [io]
   soundLibrary=<install>/share/mscore-3.7/soundlibraries/Spitfire Symphony Orchestra.xml
   soundLibraryOutput=plugin
   soundLibraryPlugin=<sfizz build>/sfizz.vst3
   ```
5. Run `QT_QPA_PLATFORM=offscreen <install>/bin/mscore -o out.wav mtest/libmscore/soundlibrary/articulations.musicxml`
   and then `python3 fingerprint.py out.wav`. It prints, for each violin note, which UACC
   articulation was heard against which one was expected.

Results on 2026-09-25: 12/12 notes as expected, both with the hand-written setup and with a
setup saved from the GUI. With `--ornaments` (tremolo and trill samples added to the map
copy), the tremolo and both trills played their samples with one attack each.
