# Adding an Ample guitar: the pipeline

DORMANT (the owner, 2026-10-29: not buying the Ample guitars; the maps live in `tools/soundlibraries/ample/`, not shipped).
For an agent adding one of the Ample acoustic guitars (AGSJ, AG12, AGL, AGT; AGM is done) if he ever buys and activates one. Everything here was run on Ample Guitar M 4.1.0 (the VM's trial, 2026-10-21) as the regression case;
the four others have no trial, so their first contact is the bought copy. Read CLAUDE.md › Sound libraries › Ample
guitars and HANDOFF.md › Ample guitars first; the private reports named below are on the owner's dev machine.

## What is where

| Piece | Path | Does |
|---|---|---|
| amphost | `tools/soundlibraries/amphost/` (`amphost.cpp`, `CMakeLists.txt`, `build.sh`) | a small Windows VST 3 host: `info` (the parameter dump), `load`, `render <events> <wav>`. Only the VST 3 API; nothing of Ample's. Build on Linux with msvc-wine: `tools/soundlibraries/amphost/build.sh` -> `/tmp/amphost-build/amphost.exe` |
| products | `tools/soundlibraries/ample_products.py` | per product: name, plug-in file, MuseScore instrument ids, body, slide guitar key, natural-harmonic range (from the manuals); every number not measured is a `TODO(<experiment>)` |
| measured numbers | `tools/soundlibraries/ample_<code>.json` | what `measure_ample.py merge` wrote (numbers only, with each key's source) |
| generator | `tools/soundlibraries/gen_ample.py --product <code> [--state <default.state>] [--allow-todo] <dump> [out]` | the map `tools/soundlibraries/ample/<name>.xml`; stops on a TODO (naming the experiment) unless `--allow-todo` |
| measurements | `tools/ample/measure_ample.py --product <code> --out <private dir> <experiments…>` | the renders on the VM and their analysis; `merge` writes `ample_<code>.json` |
| check | `tools/ample/check.py --product <code> --build <VM build dir> --out <private dir> [--map <xml>] [--ref <results.json>]` | MuseScore's own renders of the check scores through the real plug-in, PASS / FAIL in `report.md` |
| check scores | `tools/ample/check_scores.py <dir>` | the check's MusicXML scores, from the repo's `mtest/libmscore/soundlibrary/guitar-techniques.musicxml` + the checks' bars (no owner score) |
| state tool | `tools/ample/ample_state.py xml\|names\|diff\|set\|summary` | read / diff / edit an Ample state (`.state`, `.vst3state`) by the names `audio/vst3/pluginstate.h` uses |
| VM access | `tools/ample/amplevm.py` | ssh / scp, the shared `C:\claude\quiet.lock`, watched runs |
| tests | `tools/soundlibraries/test_gen_ample.py`, `tools/ample/test_ample_tools.py` (both in `tools/run_fork_tests.sh`) | the generator's product switch, the state tool, the scores, the score patcher |

Environment for every VM step (nothing personal is committed):

```sh
export AMPLE_VM=user@<VM address>            # CLAUDE.md's VM notes; the key: AMPLE_VM_KEY (default ~/.ssh/id_ed25519_winvm)
export AMPLE_PAUSE=~/MuseScore/ms3fork/private/VM_PAUSE   # written while the VM lock is held
export PYTHONUNBUFFERED=1
```

Keep every `--out` folder under `~/MuseScore/ms3fork/private/` (states, dumps, renders are Ample's data).

## Step by step (one product, `<code>` = AGSJ / AG12 / AGL / AGT)

0. **Ask the owner** to install the product on the VM (the installers need the desktop: silent mode aborts) with the
   library at Ample's default path, and to activate it (Activation Manager; one activation covers all Ample guitars).
   Check: `Documents\Ample Sound\<code>\Configs\<code>_user.xml` no longer says `demo` / `0000`. Note C: free space
   (the library installer needs ~9 GB while it runs). Never change `<code>_info.xml` (Ample's global settings: hash
   it before and after every session; MIDI Guitar mode there silences keys >= 62).
1. **amphost**: `tools/soundlibraries/amphost/build.sh` (msvc-wine at `~/msvc/vs17`; any MSVC works).
2. **Dump and sample sets**: `tools/ample/measure_ample.py --product <code> --out ~/MuseScore/ms3fork/private/ample-<code> dump sets`.
   - `dump` writes `dump.txt` (the generator's input) and `default.state`, and prints the version, the parameter
     counts and the DBName. Compare the parameter titles with AGM's (`diff <(cut -f3 …/agm/dump.txt) <(cut -f3 dump.txt)`):
     the guitar pass needs `M-Master Vol` (CC7 hosted) and `M-Capo`; mic titles differ by product (AGSJ Body / Neck /
     AB; the others Middle / Side) and need nothing.
   - `sets` tries `<code>Fg`, `<code>Pk`, `<code>St` (AGM's naming). A set that renders silent doesn't exist under that
     name: read the real DBName codes in the plug-in's sample-library menu (GUI: `private/ample-state/tools/astate`,
     screenshots on a review page, CLAUDE.md › Rules) or by saving a state after choosing each set in the GUI and
     `ample_state.py diff`. AGL (nylon, classical) may have no Pick / Strum set: write the sets it has into
     `ample_products.py` as a `sampleSets` override or `ample_<code>.json`.
3. **Measurements**: `measure_ample.py --product <code> --out … keys range velocity cc strumlead strum softroll capo bgload stateclamp`
   (or `all`, the same list after dump / sets). About 30 minutes of VM time. `strum` and `softroll` play the set-up
   `strumLead` ahead (the product's, else AGM's 50 ms): run `strumlead` first and merge if it differs. What each fills and what to look at:

   | Experiment | Fills | Look at |
   |---|---|---|
   | keys | `keyswitchesSounding` | (a screen: read the json, AGM's palm mute 26 isn't caught) keys 24-30 change the note (AGT / AGM: 30 slide guitar); the strummer / FX zone (85-107) sounds or changes; anything else changing is new: read the manual |
   | range | `nhRange` | (a screen) the natural harmonic's lowest note (manual: E1 on AGSJ / AG12, E2 on AGL / AGM / AGT); palm mute and sustain E1-C5 |
   | velocity | `maxVelocity` | 127's centroid jump (Pop): 126 when it is there |
   | cc | (finding) | CC7 a gain (64 unity, 127 +6 dB); CC1 / CC11 / CC64 nothing; with the MIDI-learn entry CC11 = M-Master Vol it acts as CC7 |
   | strumlead | `strumLead` | the least set-up lead per block size (16-2048) that makes a strum bright (~1200 Hz); 50 ms when 2048 needs no more than AGM's one block |
   | strum | `strumLevels` | per set, the set-up `strumLead` before the strum key: the CC7 gain law's residual (< 1.5 dB), the band edges (velocity 31/32, 63/64, 95/96, 126/127), the CC7 per roll velocity |
   | softroll | `softRoll` | the 10-25 ms gaps at v20 / v40 against the strum at its StrumLevel |
   | capo | `capoLead` | the least lead before a note that works at blocks 64 / 512 / 2048 (AGM: the next block; 50 ms kept) |
   | bgload | `backgroundLoad` (check) | when a note after setState plays the asked set (AGM: Finger 0.25 s, Pick / Strum 1.0 s); the map's 60 s is a limit, not a time |
   | stateclamp | (finding) | every state setting at its assumed ends loads (no crash) and is kept; one past an end: what the plug-in does |

   Then `measure_ample.py --product <code> --out … merge` writes `tools/soundlibraries/ample_<code>.json`. Copy each
   key's private source into its `_sources` line by hand if it needs more than the experiment's name.
4. **Pinch**: `pinch` stays a TODO for a new product (meas3's by-numbers layering, no tool): `--allow-todo` keeps AGM's
   `19 60 2 45`, marked; the setting is 0 by default anyway. Same for `rollGap` / `softStrum` (meas3's spectral
   comparison; AGM's 15 ms / 30 are reasonable starting points): write them into the json with the source "assumed
   from AGM" and say so to the owner.
5. **Generate**: `python3 tools/soundlibraries/gen_ample.py --product <code> --state …/default.state …/dump.txt`. It
   prints the state settings the product lacks (left out) and the state-only names it doesn't know (TODO: measure
   them as `private/ample-state/RESULT.md` did, one GUI change at a time, then add rows to `state_settings()`).
   Diff the new map against `Ample Guitar M.xml`: only the names, ids, measured numbers and product keys may differ.
6. **Linux tests**: `tools/run_fork_tests.sh` through the wrapper (the map's own C++ tests are AGM's: `ampleMap`
   counts AGM's controllers; a new map is checked on the VM). Then `tools/check_windows_names.sh`.
7. **Check on the VM**: `tools/ample/check.py --product <code> --build 'C:\claude\<build>\ms' --map "tools/soundlibraries/ample/<name>.xml" --out ~/MuseScore/ms3fork/private/ample-<code>/check1`.
   The build must be one with the current C++; `--map` puts the new map into it. Every row must be PASS; INFO rows
   go into the report. Run it twice (or `--repeat 3` on tech-on, the default): determinism is a row.
8. **Ask the owner** (the review page with the clips, CLAUDE.md › Rules: the owner decides what is heard): the
   strum / soft strum / pinch settings by ear (`[guitar] strums`, `softStrums`, `pinchHarmonics`), the guitar's
   balance against others (no measured data: an assumption), and which library plays a score's guitars (one library
   at a time: OPEN_ITEMS.md 14; several steel guitars share the ids `guitar-steel*`).
9. **Commit** the map, `ample_<code>.json`, any product override; never the dump, states, renders or screenshots.
   Update CLAUDE.md › Ample guitars (the product list), HANDOFF.md, docs/TOOLS.md.

## What carries over from AGM and what must be measured again

| Fact | AGM (measured) | Other products |
|---|---|---|
| keyswitch keys 24-30, string keys 31-36, strummer keys (86, 97, 72 76 79 83, 62-71), FX keys 89-103 | measured | the same in all five manuals: **check** with `keys` |
| natural-harmonic range | E2 (40) up | manual: E1 on AGSJ / AG12; **check** with `range` |
| slide guitar key 30 | yes | AGT yes, AGSJ / AG12 / AGL no (manual) |
| Pop at 127 (maxVelocity 126) | yes | all five manuals; **measure** (`velocity`) |
| sample-set codes | AGMFg / AGMPk / AGMSt | **measure** (`sets`); AGL may differ |
| StrumLevel per set, softRoll | measured (meas3) | **measure** (`strum`, `softroll`): recorded strums differ per guitar |
| CC7 = M-Master Vol, linear gain, 64 unity | measured (MV.md) | **check** (`cc`): the MIDI-learn defaults come from the dump's state |
| capo applies from the next block (capoLead 50) | measured | **check** (`capo`) |
| background load after setState | Finger 0.25 s, Pick / Strum 1.0 s | **check** (`bgload`); the map's limit stays 60 |
| state settings (208 state-only names) and their ranges | partly measured, partly assumed (`stateclamp`) | **dump** + `--state` drop / report the differences |
| strumLead (the strummer's set-up an earlier block than the strum key) | 50 ms (blocks 16-2048) | **measure** (`strumlead`, `demo15`): the same strummer engine is likely, the numbers not proven |
| AGM load crash (cryptnet frames) | demo copy: amphost 0 / 60 (network on / blocked), MuseScore 2 / 15 | **check** with `crash` and `check.py` on the bought copy; check.py retries |
| Master Tune ignored by setState | measured | assume the same (Ample 4.x) |

## Checklist

- [ ] Owner installed and activated `<code>`; `<code>_info.xml` hashed
- [ ] amphost built
- [ ] `dump sets` run; DBName codes right (silent set = wrong code)
- [ ] `keys range velocity cc strumlead strum softroll capo bgload stateclamp` run; findings read
- [ ] `demo15 crash` run once on the bought copy (compare with AGM's demo results below)
- [ ] `merge`; `ample_<code>.json` sources filled; no TODO left but those the owner accepted as assumed
- [ ] map generated with `--state`; diff against AGM's map reviewed
- [ ] Linux tests and `check_windows_names.sh` pass
- [ ] `check.py` all PASS on the VM (twice)
- [ ] review page for the owner (clips + numbers; any GUI reading with its screenshot)
- [ ] owner's decisions recorded (settings by ear, which library, balance)
- [ ] committed (map, json); CLAUDE.md / HANDOFF.md / TOOLS.md updated; `<code>_info.xml` hash unchanged

## AGM results of 2026-10-21 (the regression numbers)

All on the VM's AGM 4.1.0 trial (demo, not activated), amphost unless "MuseScore" is said; private
`~/MuseScore/ms3fork/private/ample-pipeline/agm/*.json` and `check458/report.md`.

| Experiment | Result |
|---|---|
| dump | byte-identical to the 2026-10-18 dump (reproducible); 124 parameters, 208 state-only names; the map regenerated from it byte-identical before strumLead |
| sets | AGMFg / AGMPk / AGMSt load and sound (-14.1 / -13.6 / -21.7 dB) |
| velocity | 127 plays Pop (centroid 3663 Hz against 1122 at 126): maxVelocity 126 |
| cc | CC7 a clean gain (64 unity); CC1 / CC64 / CC11 nothing by default; with Control ID 16 -> M-Master Vol CC11 acts as CC7 and the two overwrite each other (last writer wins) |
| strumtiming / strumlead / demo15 / demofine | **the "demo" quiet strums are a set-up timing rule**: the strummer's set-up (strum mode, Detect, chord keys) in the strum key's own process block plays dark, partial strums (350-600 Hz, 2-6 dB quieter) and quiet ones at exact multiples of 7.5 s of render time; any earlier block is bright (~1200 Hz). Least lead: block 16 1 ms, 64 2, 512 6, 2048 about one block. Map `strumLead` 50. Back-to-back strums: the first one's release moved 25 ms earlier costs 0.6 dB of its last 25 ms |
| strum (with the lead) | StrumLevel Finger 20:4 40:13 60:20 80:42 100:59 120:76 (gain law residual SD 0.94 dB), Pick 20:6 40:13 60:19 80:34 100:52 120:68 (0.76), Strum 20:6 40:19 60:23 80:37 100:62 120:67 (0.80); bands at 31/32, 63/64, 95/96, 126/127 as before |
| softroll (with the lead) | v40: 15 ms matches the strum (-21.8 / -21.2 dB); v20: 5-25 ms all within 0.7 dB (level can't choose; meas3's 10 ms kept) |
| capo | least lead 2 ms at block 64, 12 at 512, 47 at 2048: capoLead 50 confirmed |
| bgload | after setState, Finger plays the asked set from 0.25 s, Pick / Strum from 1.0 s (null < -70 dB). MuseScore's hosted sync now waits too (`setStateWaiting`, test backgroundLoadHosted) |
| stateclamp | all 204 state settings at their assumed ends load and are kept; ~70 toggles clamp to 0/1 or -1/1; S-Pattern Index -1 / 8 and FX-Reverb-IRType -1 crash AGM (0xC0000005): 0-7 / >= 0 confirmed; T-Track Index forced to 0 without a tab; numbers are echoed as written (a range end isn't proven) |
| keys | a screen, not a decision: changed / sounded 25, 27-39, 89-103 against four references 9.6 dB apart; palm mute (26) not caught by the spectral criterion (meas heard it); 37-39 sound (below the chord zone); read with the manual |
| range | a screen: sustain / palm mute / slide guitar sound over 40-98; the harmonic in 41 of those notes (nhRange kept at the manual's 40-72) |
| crash | amphost single loads: 0 / 30 with the network, 0 / 30 with amphost's outbound traffic blocked by a firewall rule. MuseScore (check.py, its 13 renders + retries): #458 network on 2 crashes in 15 attempts; #463 network on 1 in 14; #463 with MuseScore's outbound traffic blocked 9 in 22 (two cases failed all 3 attempts). So blocking the network makes MuseScore's AGM loads crash far more often (licence check is the likely cause), while one plug-in load in amphost never crashed either way |
| check.py on #463 (strumLead) | 25 PASS, 0 FAIL: strum bright 1335 Hz / -8.7 dB (#458: 639 Hz / -14.2), bar 16's strum at 30 s as loud; every case without strums bit-identical to #458's renders, those with strums differ (intended) |
| check.py on #458 | 10 PASS, 1 FAIL (strum bright: 639 Hz, the strumLead fix isn't in #458), determinism 3/3, routes = sets, capo, capo from the start, state lanes, soft strums, volume lane, Create + 2 x Update Live Set, AGM_info.xml unchanged |
