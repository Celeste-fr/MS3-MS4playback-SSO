{
	"patcher": {
		"fileversion": 1,
		"appversion": {
			"major": 9,
			"minor": 0,
			"revision": 7,
			"architecture": "x64",
			"modernui": 1
		},
		"classnamespace": "box",
		"rect": [
			100,
			100,
			900,
			700
		],
		"openinpresentation": 1,
		"default_fontsize": 10.0,
		"default_fontface": 0,
		"default_fontname": "Arial",
		"gridonopen": 1,
		"gridsize": [
			15.0,
			15.0
		],
		"gridsnaponopen": 1,
		"objectsnaponopen": 1,
		"statusbarvisible": 2,
		"toolbarvisible": 1,
		"boxanimatetime": 200,
		"enablehscroll": 1,
		"enablevscroll": 1,
		"devicewidth": 330,
		"description": "MuseScore Link: the score's parts as clips, kept up to date by MuseScore; turns their controller notes into MIDI controllers. Put it before the instrument.",
		"digest": "",
		"tags": "",
		"style": "",
		"subpatcher_template": "",
		"boxes": [
			{
				"box": {
					"id": "obj-1",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						30,
						30,
						58,
						22
					],
					"text": "midiin",
					"outlettype": [
						"int"
					]
				}
			},
			{
				"box": {
					"id": "obj-2",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 8,
					"patching_rect": [
						30,
						70,
						79,
						22
					],
					"text": "midiparse",
					"outlettype": [
						"",
						"",
						"",
						"int",
						"int",
						"",
						"int",
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-3",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 15,
					"patching_rect": [
						30,
						110,
						400,
						22
					],
					"text": "route 127 126 125 124 123 122 121 120 119 118 117 116 115 114",
					"outlettype": [
						"",
						"",
						"",
						"",
						"",
						"",
						"",
						"",
						"",
						"",
						"",
						"",
						"",
						"",
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-4",
					"maxclass": "newobj",
					"numinlets": 7,
					"numoutlets": 2,
					"patching_rect": [
						30,
						330,
						86,
						22
					],
					"text": "midiformat",
					"outlettype": [
						"int",
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-5",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						420,
						330,
						44,
						22
					],
					"text": "iter",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-6",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 0,
					"patching_rect": [
						30,
						370,
						65,
						22
					],
					"text": "midiout"
				}
			},
			{
				"box": {
					"id": "obj-7",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 2,
					"patching_rect": [
						30,
						160,
						51,
						22
					],
					"text": "sel 0",
					"outlettype": [
						"bang",
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-8",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 1,
					"patching_rect": [
						30,
						200,
						40,
						22
					],
					"text": "- 1",
					"outlettype": [
						"int"
					]
				}
			},
			{
				"box": {
					"id": "obj-9",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						30,
						240,
						100,
						22
					],
					"text": "prepend 176 32",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-10",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 2,
					"patching_rect": [
						90,
						160,
						51,
						22
					],
					"text": "sel 0",
					"outlettype": [
						"bang",
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-11",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						90,
						200,
						110,
						22
					],
					"text": "expr $i1*($i1>1)",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-12",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						90,
						240,
						100,
						22
					],
					"text": "prepend 176 1",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-13",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 2,
					"patching_rect": [
						150,
						160,
						51,
						22
					],
					"text": "sel 0",
					"outlettype": [
						"bang",
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-14",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						150,
						200,
						110,
						22
					],
					"text": "expr $i1*($i1>1)",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-15",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						150,
						240,
						100,
						22
					],
					"text": "prepend 176 11",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-16",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 2,
					"patching_rect": [
						210,
						160,
						51,
						22
					],
					"text": "sel 0",
					"outlettype": [
						"bang",
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-17",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						210,
						200,
						110,
						22
					],
					"text": "expr $i1*($i1>1)",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-18",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						210,
						240,
						100,
						22
					],
					"text": "prepend 176 64",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-19",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 2,
					"patching_rect": [
						270,
						160,
						51,
						22
					],
					"text": "sel 0",
					"outlettype": [
						"bang",
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-20",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						270,
						200,
						110,
						22
					],
					"text": "expr $i1*($i1>1)",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-21",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						270,
						240,
						100,
						22
					],
					"text": "prepend 176 2",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-22",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 2,
					"patching_rect": [
						330,
						160,
						51,
						22
					],
					"text": "sel 0",
					"outlettype": [
						"bang",
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-23",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						330,
						200,
						110,
						22
					],
					"text": "expr $i1*($i1>1)",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-24",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						330,
						240,
						100,
						22
					],
					"text": "prepend 176 4",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-25",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 2,
					"patching_rect": [
						390,
						160,
						51,
						22
					],
					"text": "sel 0",
					"outlettype": [
						"bang",
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-26",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						390,
						200,
						110,
						22
					],
					"text": "expr $i1*($i1>1)",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-27",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						390,
						240,
						100,
						22
					],
					"text": "prepend 176 21",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-28",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 2,
					"patching_rect": [
						450,
						160,
						51,
						22
					],
					"text": "sel 0",
					"outlettype": [
						"bang",
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-29",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						450,
						200,
						110,
						22
					],
					"text": "expr $i1*($i1>1)",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-30",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						450,
						240,
						100,
						22
					],
					"text": "prepend 176 5",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-31",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 2,
					"patching_rect": [
						510,
						160,
						51,
						22
					],
					"text": "sel 0",
					"outlettype": [
						"bang",
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-32",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						510,
						200,
						110,
						22
					],
					"text": "expr $i1*($i1>1)",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-33",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						510,
						240,
						100,
						22
					],
					"text": "prepend 176 65",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-34",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 2,
					"patching_rect": [
						570,
						160,
						51,
						22
					],
					"text": "sel 0",
					"outlettype": [
						"bang",
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-35",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						570,
						200,
						110,
						22
					],
					"text": "expr $i1*($i1>1)",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-36",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						570,
						240,
						100,
						22
					],
					"text": "prepend 176 66",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-37",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 2,
					"patching_rect": [
						630,
						160,
						51,
						22
					],
					"text": "sel 0",
					"outlettype": [
						"bang",
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-38",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						630,
						200,
						110,
						22
					],
					"text": "expr $i1*($i1>1)",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-39",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						630,
						240,
						100,
						22
					],
					"text": "prepend 176 67",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-40",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 2,
					"patching_rect": [
						690,
						160,
						51,
						22
					],
					"text": "sel 0",
					"outlettype": [
						"bang",
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-41",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						690,
						200,
						110,
						22
					],
					"text": "expr $i1*($i1>1)",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-42",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						690,
						240,
						100,
						22
					],
					"text": "prepend 176 68",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-43",
					"maxclass": "newobj",
					"numinlets": 3,
					"numoutlets": 1,
					"patching_rect": [
						810,
						280,
						90,
						22
					],
					"text": "pack 224 0 64",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-44",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 2,
					"patching_rect": [
						750,
						160,
						51,
						22
					],
					"text": "sel 0",
					"outlettype": [
						"bang",
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-45",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						750,
						200,
						110,
						22
					],
					"text": "expr $i1*($i1>1)",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-46",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 2,
					"patching_rect": [
						750,
						240,
						51,
						22
					],
					"text": "t b i",
					"outlettype": [
						"bang",
						"int"
					]
				}
			},
			{
				"box": {
					"id": "obj-47",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 2,
					"patching_rect": [
						870,
						160,
						51,
						22
					],
					"text": "sel 0",
					"outlettype": [
						"bang",
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-48",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						870,
						200,
						110,
						22
					],
					"text": "expr $i1*($i1>1)",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-49",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 2,
					"patching_rect": [
						870,
						240,
						51,
						22
					],
					"text": "t b i",
					"outlettype": [
						"bang",
						"int"
					]
				}
			},
			{
				"box": {
					"id": "obj-50",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 2,
					"patching_rect": [
						30,
						560,
						100,
						22
					],
					"text": "route /ms/midi",
					"outlettype": [
						"",
						""
					],
					"varname": "msl_in"
				}
			},
			{
				"box": {
					"id": "obj-51",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						150,
						590,
						72,
						22
					],
					"text": "deferlow",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-52",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 2,
					"patching_rect": [
						30,
						590,
						51,
						22
					],
					"text": "t l l",
					"outlettype": [
						"",
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-53",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 2,
					"patching_rect": [
						90,
						620,
						86,
						22
					],
					"text": "zl.slice 1",
					"outlettype": [
						"",
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-54",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 2,
					"patching_rect": [
						30,
						650,
						86,
						22
					],
					"text": "zl.slice 1",
					"outlettype": [
						"",
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-55",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						90,
						650,
						128,
						22
					],
					"text": "sprintf msl_m%ld",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-56",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						90,
						680,
						100,
						22
					],
					"text": "prepend send",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-57",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 0,
					"patching_rect": [
						30,
						710,
						65,
						22
					],
					"text": "forward"
				}
			},
			{
				"box": {
					"id": "obj-58",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						420,
						300,
						65,
						22
					],
					"text": "receive",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-59",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 3,
					"patching_rect": [
						500,
						30,
						121,
						22
					],
					"text": "live.thisdevice",
					"outlettype": [
						"bang",
						"int",
						"int"
					]
				}
			},
			{
				"box": {
					"id": "obj-60",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 8,
					"patching_rect": [
						500,
						450,
						120,
						22
					],
					"text": "v8",
					"outlettype": [
						"",
						"",
						"",
						"",
						"",
						"",
						"",
						""
					],
					"saved_object_attributes": {
						"parameter_enable": 0
					},
					"textfile": {
						"text": "// MuseScore Link: the Max for Live device for \"Live plays the score\" (LIVE.md; MuseScore's side:\n// libmscore/liveclips.h, mscore/liveclips.h).\n//\n// One copy goes on each MIDI track that plays a library part, BEFORE the instrument (Kontakt). In\n// every copy the patcher (not this script) turns the carrier notes of MuseScore's clips (keys 114-127: controllers, pitch bend)\n// into their MIDI controllers, in Max's scheduler, sample-timed with the notes; this script is not in\n// that path. One copy (the first loaded; another takes over when it goes) is the hub:\n//   - it listens to MuseScore (OSC over UDP on localhost, port 9001 by default; answers on port + 1);\n//   - it writes each part's clip on its track: found by MIDI From = the part's MuseScore port and\n//     channel, else (a part's main patch) by track name = part name; one arrangement clip from beat\n//     0 over the whole score, named \"MuseScore: <part>\", its notes replaced at each change;\n//   - it never touches a clip it didn't make (another name), and doesn't make one over a track's\n//     other clips;\n//   - it sets Live's tempo to the score's first and puts a locator (\"MS 12\") at each played bar,\n//     only while Live is stopped, and only its own locators;\n//   - it reports Live's transport (~25 a second while playing) so MuseScore follows, and starts or\n//     stops Live when MuseScore's Play or Stop is pressed;\n//   - \"/ms/mode stream\": MuseScore plays through Live (the tracks' Monitor on In, which silences\n//     clips); \"clips\": Live plays the clips (Monitor on Auto).\n//   - \"Edit in MuseScore\" (the button, on any copy): the clip in Live's Detail View (any MIDI clip, not\n//     only MuseScore's) is read with every field and sent to MuseScore, which opens it as a score; the\n//     hub then applies MuseScore's edits to it by note id (remove_notes_by_id, apply_note_modifications\n//     with Live's own note data and only the edited fields changed, add_new_notes), checks the clip once\n//     a second and reports a change made in Live as a conflict (nothing more is written until MuseScore\n//     reads it again). LIVE.md \u203a Editing Live clips in MuseScore; mscore/liveclipmodel.h.\n//   - a clip tab plays through its track (protocol 4; mscore/liveclipmodel.h): MuseScore sends what it plays as\n//     /ms/midi trackId status data1 data2; the hub's patcher (not this script) passes it on at once, in Max's\n//     scheduler: [route /ms/midi] -> [forward] to \"msl_m<trackId>\"; every copy's [receive] is named after its\n//     own track (\"set msl_m<track id>\" from outlet 7) -> iter -> midiout, into the track's chain before the\n//     instrument (nothing recorded, no arming). The hub tells MuseScore each edited clip's track and whether a\n//     copy of protocol 4+ is on it (/live/clip/track, again when that changes). Live's transport, song time and\n//     clips are never touched for it. /live/bye when the hub goes; /ms/clip/adopt: a new hub takes over a clip.\n//   - automation lanes of any Live track (protocol 5; /ms/params/ask: all of them again; the owner, 2026-10-02: \"the automation display wouldn't only work\n//     for sso, it works for any midi clip\"): for each edited clip and each route's track the hub sends the track's\n//     automatable parameters, /live/params key:s hash:i chunk:i chunks:i (d:i p:i name:s min:f max:f quantized:i) \u00d7 n\n//     (d -1: the mixer, p 0 volume, 1 pan; else devices[d].parameters[p]; MuseScore Link itself left out), again\n//     when the track's devices change (checked once a second), and each edited clip's place, /live/clip/where key:s\n//     track:i slot:i (the track's index; the session slot's, -1 for an arrangement clip), again when it moves.\n//     A clip tab's lanes are written into the clip's own envelopes by the MuseScore Envelopes Control Surface\n//     script (tools/live/MuseScoreEnvelopes: Max for Live can't), MuseScore talking to it directly. A route's lane\n//     titled \"live:<d>/<p>\" is that parameter of the track (no plug-in needed), driven as below.\n//   - plug-in parameter lanes (MuseScore's automation of Kontakt's parameters): the LOM can't write clip\n//     envelopes or arrangement automation, so each copy drives its own track's plug-in parameters\n//     itself while Live plays. Below, \"Parameter lanes\".\n// The Live Object Model is used from Max's low-priority thread only (messages from udpreceive go\n// through deferlow; the Tasks run there).\n//\n// Parameter lanes (protocol 3):\n//   MuseScore -> hub: /ms/params gen:i key:s lanes:i hash:i, then per lane /ms/pvals gen:i key:s lane:i\n//     title:s pid:i (the plug-in's parameter id, -1: not known) chunk:i chunks:i (time:i value:f) \u00d7 n (time in UNITS from the song start, value 0-1, a\n//     step from that time on; \u2264 100 pairs a packet). lanes 0: the route drives nothing any more.\n//   hub -> copies: only the hub hears MuseScore, so the lanes go through the Global: \"p<track id>\" holds\n//     the track's entry, JSON { serial, length (beats), routes: { key: { hash, lanes: [{ title, ev:\n//     [time, value, \u2026] }] } } } (every route on that track), g.pserial the last serial. Then\n//     messnamed(\"msl_params\", track, serial); every copy's [receive msl_params] -> [deferlow] (no\n//     re-entry into the sending script) -> \"msl_params track serial\" here; the copy on that track\n//     schedules the work (a Task). Each copy also checks its entry's serial once a second (a missed\n//     message, a copy loaded later).\n//   copy -> hub: the copy writes \"pr<track id>\" = JSON { serial, results: { key: { status } } }; the hub\n//     polls it (workStep) and answers /live/papplied key:s hash:i status:s track:s (status \"ok\", or\n//     \"missing: Vibrato, Mic 1 level\" / \"too many lanes (16 at most): \u2026\" / \"no plug-in on the track\",\n//     \"; \" between; \"no track \u2026\"; \"no MuseScore Link device on the track\" after 3 s without an answer,\n//     and the real answer still sent when it comes). Only the first copy on a track (the track's device\n//     order) drives; another releases.\n//   in a copy: the patcher's song-position signal, [phasor~ @frequency 7864320 ticks @lock 1] (16384 quarter\n//     notes, phase-locked to Live's transport) -> [*~ f], f = 16384 \u00d7 60000 / Live's tempo (outlet 4):\n//     the song position in ms. 16 slots: [buffer~ ---mslp<k>] read by [index~] at that ms (step-hold,\n//     1 ms) -> [live.remote~] k (its right inlet: \"id n\" takes the parameter, \"id 0\" releases). The\n//     buffer holds the value in force at each ms of the song, in the parameter's range (min + v \u00d7\n//     (max - min)); before a lane's first event: the parameter's value as read from the LOM before\n//     it was taken (kept while driven; put back when released). A title matches a parameter of the\n//     track's plug-in (the first PluginDevice, else the first non-Max device after this one) as\n//     Vst3Plugin::looseTitle compares them. The \"---\" prefix comes resolved from [loadmess prefix\n//     ---mslp]. Tables are rewritten when Live's tempo changes (checked once a second).\n//   debugging (hub): /ms/probe id:i what:s -> /live/probe id:i text:s (what \"pos\": the ms signal now,\n//     through [snapshot~]; \"state\": this copy's slots and the Global's entries; else a LOM path: its\n//     id, type and info); /ms/probecall id:i path:s fn:s args\u2026 -> /live/probe id text (call, or \"get\" /\n//     \"set\" a property).\n//\n// Plain ECMAScript 5 so it runs in [js] and [v8] alike, and in the Node tests (tools/live/test).\n\nautowatch = 0;\ninlets = 1;\noutlets = 8;      // 0: OSC to MuseScore (udpsend), 1: udpsend's host / port, 2: status text,\n                  // 3: \"k id n\" to the slots' live.remote~ (route 0 \u2026 15), 4: the ms factor ([*~]), 5: bang [snapshot~],\n                  // 6: the lanes to keep in the Live Set ([pattr Lanes]),\n                  // 7: \"set msl_m<track id>\" to the [receive] that plays MuseScore's notes for a clip tab\n\nvar PROTOCOL = 5;                       // 2: editing Live clips; 3: parameter lanes; 4: clip tabs play through their track;\n                                        // 5: the tracks' parameters and the clips' places (automation lanes of any track)\nvar UNITS = 3840;                       // LiveClips::UNITS_PER_BEAT\nvar BATCH = 500;                        // notes per add_new_notes call\nvar HUB_STALE_MS = 5000;\n\nvar self = this;\n// shared by every copy of the device: which is the hub, and where each copy sits (as JSON: a\n// Global's values are safest as strings)\nvar g = new Global(\"musescore_link\");\nfunction registry() {\n      try { return JSON.parse(g.devices || \"{}\"); } catch (e) { return {}; }\n      }\nfunction saveRegistry(r) { g.devices = JSON.stringify(r); }\n\nvar me = { key: \"d\" + Math.floor(Math.random() * 1e9), track: 0, device: 0 };\nvar udpPort = 9001;\nvar isHub = false;\nvar session = \"\";\nvar receiver = null;          // hub: the udpreceive and deferlow it made\nvar deferrer = null;\nvar mode = \"clips\";\nvar pending = {};             // key -> a clip being received\nvar pendingSong = null;\nvar work = [];                // clips and the song to write, in order\nvar placed = {};              // key -> { track: id, clip: name } where the hub put it\nvar lastTransport = { playing: -1, beat: -1, sent: 0 };\nvar edits = {};               // key -> a clip edited in MuseScore\nvar editSerial = 0;\nvar lastEditRequest = \"\";\nvar heartbeat = null;\nvar worker = null;\nvar reporter = null;\nvar initialised = false;\n// parameter lanes: the hub's side\nvar SLOTS = 16;                         // make_device.py SLOTS\nvar PERIOD_QUARTERS = 16384;            // the phasor~'s period (make_device.py PERIOD_TICKS / 480)\nvar MAX_MS = 3600000;                   // a table's length at most (an hour)\nvar POKE = 8192;                        // values a Buffer.poke\nvar NO_DEVICE_MS = 3000;\nvar routes = {};              // key -> the route as /ms/track gave it (to find its track)\nvar pendingParams = {};       // key -> lanes being received\nvar paramTracks = {};         // key -> the track its lanes were put on\nvar waiting = {};             // key -> lanes handed to a copy, its answer awaited\nvar songBeats = 0;            // the song's length (/ms/song)\nvar keepTasks = [];           // one-shot Tasks, kept referenced until they run\nvar probes = [];              // \"pos\" probes awaiting the snapshot~\n// \u2026 every copy's\nvar prefix = \"\";              // the slots' buffer~ names' resolved \"---mslp\"\nvar slots = [];               // k -> { id, sig } the parameter slot k drives\nvar bases = {};               // parameter id -> { value, min, max } before it was driven\nvar seenSerial = -1;          // the entry last applied\nvar filledBpm = 0;\nvar paramTask = null;\n// \u2026 and kept in the Live Set ([pattr Lanes], a Live parameter of type Blob, Stored Only: outlet 6 sets it, its value\n// comes back as \"lanes \u2026\" when the set opens), so the lanes play without MuseScore (LIVE.md \u203a Automation lanes \u203a\n// Without MuseScore). The track's lanes as last applied:\nvar saved = null;             // { length (beats), routes: { key: { hash, lanes: [{ title, pid, ev }] } } }\nvar savedSerial = 0;          // counts the values the set gave back (an entry's serial \"saved<n>\")\nvar SAVED_TAG = \"msl-lanes\";\nvar SAVED_VERSION = 2;                  // 1: (time value) pairs; 2: pairs and runs (packLane)\nvar STORE_DELAY_MS = 2000;              // the stores are set once MuseScore's lane edits pause this long (Live: one\n                                        // undo step a pause, not one a keystroke; playback follows at once)\nvar STORES = 4;                         // make_device.py STORES: [pattr Lanes], [pattr Lanes2] \u2026\nvar STORE_ATOMS = 30000;                // atoms a store at most (Live 12.2 crashed on a [pattr] set to 34010 atoms;\n                                        // 24010 copied fine: the test VM, 2026-10-01)\nvar parts = [];                         // k -> the atoms store k gave back\nvar sentParts = [];                     // k -> the atoms last sent to store k (as JSON: its echo is left alone)\nvar keptStatus = \"\";                    // \"\" or why the lanes aren't kept in the set\nvar storeTask = null;                   // sets the stores STORE_DELAY_MS after the last change\nvar restoredLog = [];         // (debugging: the values the set gave back, for the \"state\" probe)\n\nfunction now() { return new Date().getTime(); }\nfunction num(v) { return Number(Array.isArray(v) ? v[0] : v); }\nfunction str(v) {\n      if (Array.isArray(v))\n            return v.join(\" \");\n      return v === undefined || v === null ? \"\" : String(v);\n      }\nfunction loose(s) { return str(s).toLowerCase().replace(/[^a-z0-9]/g, \"\"); }\nfunction loosePort(s) { return loose(str(s).replace(/^\\s*ext:\\s*/i, \"\")); }\n\n// a LOM dictionary property (input_routing_type \u2026): a JSON string, sometimes in an array\nfunction displayName(v) {\n      var s = str(v);\n      try {\n            var o = JSON.parse(s);\n            var find = function(x) {\n                  if (!x || typeof x !== \"object\")\n                        return null;\n                  if (typeof x.display_name === \"string\")\n                        return x.display_name;\n                  for (var k in x) {\n                        var r = find(x[k]);\n                        if (r !== null)\n                              return r;\n                        }\n                  return null;\n                  };\n            var r = find(o);\n            if (r !== null)\n                  return r;\n            }\n      catch (e) {}\n      var m = /\"display_name\"\\s*:\\s*\"([^\"]*)\"/.exec(s);\n      return m ? m[1] : s;\n      }\n\n// [\"id\", 3, \"id\", 7] -> [3, 7]\nfunction ids(v) {\n      var out = [];\n      if (!Array.isArray(v))\n            v = str(v).split(\" \");\n      for (var i = 0; i + 1 < v.length; i += 2)\n            if (str(v[i]) === \"id\" && num(v[i + 1]) > 0)\n                  out.push(num(v[i + 1]));\n      return out;\n      }\n\nfunction status(text) {\n      outlet(2, \"set\", text);\n      }\n\nfunction send() {\n      var a = Array.prototype.slice.call(arguments);\n      outlet.apply(this, [0].concat(a));\n      }\n\n//---------------------------------------------------------\n//   the device's life\n//---------------------------------------------------------\n\n// live.thisdevice: the Live API is ready\nfunction bang() {\n      if (initialised)\n            return;\n      initialised = true;\n      var dev = new LiveAPI(\"this_device\");\n      me.device = num(dev.id);\n      var tr = new LiveAPI(\"this_device canonical_parent\");\n      me.track = num(tr.id);\n      var r = registry();\n      r[me.key] = { track: me.track, device: me.device, beat: now(), protocol: PROTOCOL };\n      saveRegistry(r);\n      outlet(7, \"set\", \"msl_m\" + me.track);         // (MuseScore's notes for a clip tab on this track)\n      heartbeat = new Task(beat, this);\n      heartbeat.interval = 1000;\n      heartbeat.repeat();\n      paramTask = new Task(applyParams, this);\n      storeTask = new Task(flushStores, this);\n      elect();\n      if (!isHub)\n            status(\"MuseScore Link: on this track (the hub is another copy)\");\n      outlet(4, msFactor(liveTempo()));\n      paramsCheck(!!saved);\n      }\n\nfunction beat() {\n      var r = registry();\n      if (r[me.key]) {\n            r[me.key].beat = now();\n            saveRegistry(r);\n            }\n      if (isHub) {\n            g.hubBeat = now();\n            if (Math.floor(now() / 1000) % 2 === 0)\n                  send(\"/live/hello\", session, PROTOCOL);\n            checkEdits();\n            checkRouteParams();\n            }\n      else\n            elect();\n      paramsCheck();\n      }\n\nfunction elect() {\n      if (isHub)\n            return;\n      if (!g.hub || !g.hubBeat || now() - g.hubBeat > HUB_STALE_MS || !registry()[g.hub])\n            becomeHub();\n      }\n\nfunction becomeHub() {\n      g.hub = me.key;\n      g.hubBeat = now();\n      isHub = true;\n      session = \"s\" + Math.floor(Math.random() * 1e9);\n      openPort();\n      worker = new Task(workStep, this);\n      worker.interval = 20;\n      worker.repeat();\n      reporter = new Task(report, this);\n      reporter.interval = 40;\n      reporter.repeat();\n      send(\"/live/hello\", session, PROTOCOL);\n      status(\"MuseScore Link: hub, listening on UDP \" + udpPort + \", waiting for MuseScore\");\n      }\n\n// the hub alone listens: its udpreceive is made here (every copy binding the port would clash)\nfunction openPort() {\n      var p = self.patcher;\n      if (!p)\n            return;\n      if (receiver)\n            p.remove(receiver);\n      // the patcher's [route /ms/midi] (MuseScore's notes, straight on to the tracks' copies; the rest through its\n      // [deferlow] to this script), else (an older patcher) a deferlow made here\n      var into = p.getnamed ? p.getnamed(\"msl_in\") : null;\n      if (!into && !deferrer) {\n            deferrer = p.newdefault(20, 600, \"deferlow\");\n            p.connect(deferrer, 0, self.box, 0);\n            }\n      receiver = p.newdefault(20, 570, \"udpreceive\", udpPort);\n      p.connect(receiver, 0, into || deferrer, 0);\n      outlet(1, \"host\", \"127.0.0.1\");\n      outlet(1, \"port\", udpPort + 1);\n      }\n\n// the Port box (\"port 9001\")\nfunction setPort(n) {\n      n = Math.round(num(n));             // (a Float parameter in Live: 9001.0)\n      if (!(n > 1023 && n < 65535) || n === udpPort)\n            return;\n      udpPort = n;\n      if (isHub)\n            openPort();\n      }\n\nfunction resync() {\n      if (isHub)\n            send(\"/live/resync\");\n      }\n\nfunction notifydeleted() {\n      var r = registry();\n      delete r[me.key];\n      saveRegistry(r);\n      if (isHub) {\n            g.hub = null;\n            g.hubBeat = 0;\n            send(\"/live/bye\", session);           // (MuseScore: the connection lost now, not in 6 s)\n            }\n      if (heartbeat) heartbeat.cancel();\n      if (worker) worker.cancel();\n      if (reporter) reporter.cancel();\n      if (paramTask) paramTask.cancel();\n      if (storeTask) storeTask.cancel();\n      }\n\n//---------------------------------------------------------\n//   MuseScore's messages\n//---------------------------------------------------------\n\n// the \"Edit in MuseScore\" button (any copy: the hub does the work)\nfunction edit() {\n      if (isHub)\n            work.push({ kind: \"edit\" });\n      else {\n            g.editRequest = me.key + \":\" + now();\n            status(\"MuseScore Link: asked the hub to send the clip to MuseScore\");\n            }\n      }\n\nfunction anything() {\n      var a = arrayfromargs(arguments);\n      if (messagename === \"port\")\n            return setPort(a[0]);\n      if (messagename === \"edit\")\n            return edit();\n      if (messagename === \"prefix\") {                         // ([loadmess prefix ---mslp], resolved)\n            prefix = str(a[0]);\n            return paramsCheck(true);\n            }\n      if (messagename === \"msl_params\") {                     // (the hub put lanes on a track)\n            if (num(a[0]) === me.track && me.track)\n                  paramsCheck();\n            return;\n            }\n      if (messagename === \"lanes\")                            // ([pattr Lanes]: the value the Live Set kept)\n            return restoreSaved(a);\n      if (messagename === \"posvalue\")                         // ([snapshot~]: a \"pos\" probe's answer)\n            return answerPos(num(a[0]));\n      if (!isHub)\n            return;\n      handle(messagename, a);\n      }\n\nfunction handle(address, a) {\n      if (address === \"/ms/mode\") {\n            mode = str(a[0]) === \"stream\" ? \"stream\" : \"clips\";\n            work.push({ kind: \"mode\" });\n            }\n      else if (address === \"/ms/song\") {\n            pendingSong = { gen: num(a[0]), bpm: num(a[1]), length: num(a[2]), count: num(a[3]), chunks: num(a[4]),\n                            hash: num(a[5]), cues: [], got: 0 };\n            songBeats = pendingSong.length / UNITS;\n            if (pendingSong.chunks === 0)\n                  queueSong();\n            }\n      else if (address === \"/ms/cues\") {\n            if (!pendingSong || pendingSong.gen !== num(a[0]))\n                  return;\n            for (var i = 2; i + 1 < a.length; i += 2)\n                  pendingSong.cues.push({ time: num(a[i]) / UNITS, name: str(a[i + 1]) });\n            if (++pendingSong.got === pendingSong.chunks)\n                  queueSong();\n            }\n      else if (address === \"/ms/track\") {\n            var t = { gen: num(a[0]), key: str(a[1]), port: str(a[2]), channel: num(a[3]), part: str(a[4]), clip: str(a[5]),\n                      main: num(a[6]) !== 0, length: num(a[7]) / UNITS, count: num(a[8]), chunks: num(a[9]), hash: num(a[10]),\n                      notes: [], got: 0 };\n            pending[t.key] = t;\n            routes[t.key] = { key: t.key, port: t.port, channel: t.channel, part: t.part, clip: t.clip, main: t.main };\n            if (t.chunks === 0)\n                  queueClip(t);\n            }\n      else if (address === \"/ms/notes\") {\n            var p = pending[str(a[1])];\n            if (!p || p.gen !== num(a[0]))\n                  return;\n            for (var k = 3; k + 4 < a.length; k += 5)\n                  p.notes.push({ pitch: num(a[k]), start_time: num(a[k + 1]) / UNITS, duration: num(a[k + 2]) / UNITS,\n                                 velocity: num(a[k + 3]), mute: num(a[k + 4]) ? 1 : 0 });\n            if (++p.got === p.chunks)\n                  queueClip(p);\n            }\n      else if (address === \"/ms/clear\") {\n            work.push({ kind: \"clear\", key: str(a[1]), port: str(a[2]), channel: num(a[3]), part: str(a[4]), clip: str(a[5]),\n                        main: true });\n            if (paramTracks[str(a[1])])                         // (its parameters released, nothing answered)\n                  queueParams({ gen: num(a[0]), key: str(a[1]), lanes: 0, hash: 0, got: {}, done: 0, silent: true });\n            }\n      else if (address === \"/ms/params\") {\n            var q = { gen: num(a[0]), key: str(a[1]), lanes: Math.max(0, num(a[2])), hash: num(a[3]), got: {}, done: 0 };\n            pendingParams[q.key] = q;\n            if (q.lanes === 0)\n                  queueParams(q);\n            }\n      else if (address === \"/ms/pvals\")\n            paramValues(a);\n      else if (address === \"/ms/probe\")\n            probe(num(a[0]), str(a[1]));\n      else if (address === \"/ms/probecall\")\n            probeCall(num(a[0]), str(a[1]), str(a[2]), a.slice(3));\n      else if (address === \"/ms/play\") {\n            var song = new LiveAPI(\"live_set\");\n            song.set(\"current_song_time\", Math.max(0, num(a[0])));\n            if (!num(song.get(\"is_playing\")))\n                  song.call(\"continue_playing\");\n            }\n      else if (address === \"/ms/stop\")\n            new LiveAPI(\"live_set\").call(\"stop_playing\");\n      else if (address === \"/ms/params/ask\") {        // (MuseScore started again: every track's parameters, next second)\n            for (var pk in placed)\n                  placed[pk].devs = null;\n            for (var ek in edits)\n                  edits[ek].devs = null;\n            }\n      else if (address === \"/ms/clip/edit\")\n            work.push({ kind: \"edit\" });\n      else if (address === \"/ms/clip/write\") {\n            var e = edits[str(a[0])];\n            if (!e)\n                  return send(\"/live/clip/written\", str(a[0]), num(a[1]), \"gone\", 0);\n            if (num(a[1]) === e.lastWrite && e.reply) {           // sent again: applied once, answered again\n                  send.apply(this, e.reply);\n                  return;\n                  }\n            e.incoming = { write: num(a[1]), count: num(a[2]), chunks: num(a[3]), got: 0, ops: [] };\n            if (e.incoming.chunks === 0)\n                  queueWrite(e);\n            }\n      else if (address === \"/ms/clip/ops\") {\n            var ed = edits[str(a[0])];\n            if (!ed || !ed.incoming || ed.incoming.write !== num(a[1]))\n                  return;\n            for (var j = 3; j + 7 < a.length; j += 8)\n                  ed.incoming.ops.push({ op: num(a[j]), id: num(a[j + 1]), mask: num(a[j + 2]), pitch: num(a[j + 3]),\n                                         start: num(a[j + 4]), duration: num(a[j + 5]), velocity: num(a[j + 6]),\n                                         mute: num(a[j + 7]) });\n            if (++ed.incoming.got === ed.incoming.chunks)\n                  queueWrite(ed);\n            }\n      else if (address === \"/ms/clip/reload\") {\n            if (edits[str(a[0])])\n                  work.push({ kind: \"reload\", key: str(a[0]) });\n            }\n      else if (address === \"/ms/clip/close\")\n            delete edits[str(a[0])];\n      else if (address === \"/ms/clip/adopt\")\n            adoptEdit(str(a[0]), num(a[1]));\n      else if (address === \"/ms/midi\")            // (only with an older patcher: its [route /ms/midi] passes them on)\n            messnamed(\"msl_m\" + num(a[0]), num(a[1]), num(a[2]), num(a[3]));\n      }\n\n// a packet of one lane's events: a chunk sent again replaces itself; another gen's are dropped\nfunction paramValues(a) {\n      var q = pendingParams[str(a[1])];\n      var lane = num(a[2]);\n      if (!q || q.gen !== num(a[0]) || !(lane >= 0 && lane < q.lanes))\n            return;\n      var l = q.got[lane];\n      if (!l)\n            l = q.got[lane] = { title: str(a[3]), pid: num(a[4]), chunks: num(a[6]), parts: {}, n: 0, complete: false };\n      var c = num(a[5]);\n      if (l.chunks > 0 && !(c >= 0 && c < l.chunks))\n            return;\n      var ev = [];\n      for (var k = 7; k + 1 < a.length; k += 2)\n            ev.push(num(a[k]), num(a[k + 1]));\n      if (!l.parts[c])\n            ++l.n;\n      l.parts[c] = ev;\n      if (!l.complete && l.n >= l.chunks) {\n            l.complete = true;\n            if (++q.done === q.lanes)\n                  queueParams(q);\n            }\n      }\n\nfunction queueParams(q) {\n      delete pendingParams[q.key];\n      var lanes = [];\n      for (var i = 0; i < q.lanes; ++i) {\n            var l = q.got[i], ev = [];\n            for (var c = 0; c < Math.max(1, l.chunks); ++c)\n                  ev = ev.concat(l.parts[c] || []);\n            lanes.push({ title: l.title, pid: l.pid, ev: ev });\n            }\n      var w = { kind: \"params\", key: q.key, hash: q.hash, lanes: lanes, silent: !!q.silent };\n      for (i = 0; i < work.length; ++i)\n            if (work[i].kind === \"params\" && work[i].key === q.key) {\n                  work[i] = w;\n                  return;\n                  }\n      work.push(w);\n      }\n\nfunction queueWrite(e) {\n      var w = e.incoming;\n      e.incoming = null;\n      work.push({ kind: \"write\", key: e.key, write: w });\n      }\n\nfunction queueClip(t) {\n      delete pending[t.key];\n      // a newer version of the same clip replaces one still waiting\n      for (var i = 0; i < work.length; ++i)\n            if (work[i].kind === \"clip\" && work[i].key === t.key) {\n                  work[i] = { kind: \"clip\", key: t.key, clip: t };\n                  return;\n                  }\n      work.push({ kind: \"clip\", key: t.key, clip: t });\n      }\n\nfunction queueSong() {\n      var s = pendingSong;\n      pendingSong = null;\n      for (var i = 0; i < work.length; ++i)\n            if (work[i].kind === \"song\") {\n                  work[i] = { kind: \"song\", song: s };\n                  return;\n                  }\n      work.unshift({ kind: \"song\", song: s });\n      }\n\n// one piece of work a turn (the Live API is slow: UDP keeps flowing between)\nfunction workStep() {\n      if (g.editRequest && g.editRequest !== lastEditRequest) {     // (a button on another copy)\n            lastEditRequest = g.editRequest;\n            if (isHub)\n                  work.push({ kind: \"edit\" });\n            }\n      pollParams();\n      if (!work.length)\n            return;\n      var w = work.shift();\n      try {\n            if (w.kind === \"clip\")\n                  writeClip(w.clip);\n            else if (w.kind === \"clear\")\n                  clearClip(w);\n            else if (w.kind === \"song\") {\n                  if (!writeSong(w.song))\n                        work.push(w);           // (Live is playing: the locators wait)\n                  }\n            else if (w.kind === \"mode\")\n                  applyMode();\n            else if (w.kind === \"edit\")\n                  startEdit();\n            else if (w.kind === \"reload\")\n                  sendClip(edits[w.key]);\n            else if (w.kind === \"write\")\n                  applyWrite(edits[w.key], w.write);\n            else if (w.kind === \"params\")\n                  writeParams(w);\n            }\n      catch (e) {\n            if (w.kind === \"clip\")\n                  send(\"/live/applied\", w.clip.key, w.clip.hash, \"error: \" + e, \"\");\n            else if (w.kind === \"write\")\n                  send(\"/live/clip/written\", w.key, w.write.write, \"error: \" + e, 0);\n            else if (w.kind === \"params\" && !w.silent)\n                  send(\"/live/papplied\", w.key, w.hash, \"error: \" + e, \"\");\n            post(\"MuseScore Link: \" + e + \"\\n\");\n            }\n      }\n\n//---------------------------------------------------------\n//   tracks and clips\n//---------------------------------------------------------\n\nfunction tracks() {\n      var song = new LiveAPI(\"live_set\");\n      var n = song.getcount(\"tracks\");\n      var out = [];\n      for (var i = 0; i < n; ++i)\n            out.push(new LiveAPI(\"live_set tracks \" + i));\n      return out;\n      }\n\n// the track that plays a part: MIDI From = its port and channel, else (main patch) its name\nfunction findTrack(t) {\n      var all = tracks();\n      var i;\n      for (i = 0; i < all.length; ++i) {\n            var tr = all[i];\n            if (num(tr.get(\"has_midi_input\")) !== 1)\n                  continue;\n            if (!t.port)\n                  break;\n            var type = displayName(tr.get(\"input_routing_type\"));\n            var ch = displayName(tr.get(\"input_routing_channel\"));\n            var lt = loosePort(type), lp = loosePort(t.port);\n            if (lt && lp && (lt === lp || lt.indexOf(lp) === 0) && loose(ch) === loose(\"Ch. \" + t.channel))\n                  return { api: tr, how: \"MIDI input\" };\n            }\n      // by name: a main patch's track is named after the part; another patch's (an extra, such as the\n      // Performance legato, or a copy for another tuning) after its clip, \"<part> \u2013 <patch>\", or after the\n      // patch alone when only one track has that name\n      var names = [];\n      if (t.main)\n            names.push(loose(t.part));\n      else {\n            var full = str(t.clip).replace(/^MuseScore:\\s*/, \"\");\n            names.push(loose(full));\n            }\n      var midi = [];\n      for (i = 0; i < all.length; ++i)\n            if (num(all[i].get(\"has_midi_input\")) === 1)\n                  midi.push(all[i]);\n      for (i = 0; i < midi.length; ++i)\n            if (names.indexOf(loose(midi[i].get(\"name\"))) >= 0)\n                  return { api: midi[i], how: \"name\" };\n      if (!t.main) {\n            var dash = str(t.clip).indexOf(\" \u2013 \");\n            var patch = dash >= 0 ? loose(str(t.clip).substring(dash + 3)) : \"\";\n            var hits = [];\n            for (i = 0; patch && i < midi.length; ++i)\n                  if (loose(midi[i].get(\"name\")) === patch)\n                        hits.push(midi[i]);\n            if (hits.length === 1)\n                  return { api: hits[0], how: \"patch name\" };\n            }\n      return null;\n      }\n\nfunction clipsOf(tr) {\n      var out = [];\n      var l = ids(tr.get(\"arrangement_clips\"));\n      for (var i = 0; i < l.length; ++i) {\n            var c = new LiveAPI(\"id \" + l[i]);\n            out.push({ id: l[i], api: c, name: str(c.get(\"name\")), start: num(c.get(\"start_time\")), end: num(c.get(\"end_time\")) });\n            }\n      return out;\n      }\n\n// is a MuseScore Link device on the track, before the instrument?\nfunction deviceCheck(tr) {\n      var mine = [];\n      var r = registry();\n      for (var k in r)\n            if (r[k].track === num(tr.id) && now() - r[k].beat < HUB_STALE_MS)\n                  mine.push(r[k].device);\n      if (!mine.length)\n            return \"no MuseScore Link device on the track (its controllers would reach the instrument as notes)\";\n      var devs = ids(tr.get(\"devices\"));\n      var link = -1, instrument = -1;\n      for (var i = 0; i < devs.length; ++i) {\n            if (mine.indexOf(devs[i]) >= 0 && link < 0)\n                  link = i;\n            if (instrument < 0 && str(new LiveAPI(\"id \" + devs[i]).get(\"class_name\")) === \"PluginDevice\")\n                  instrument = i;\n            }\n      if (link >= 0 && instrument >= 0 && link > instrument)\n            return \"the MuseScore Link device is after the instrument: move it before\";\n      return \"ok\";\n      }\n\nfunction setMonitor(tr) {\n      try {\n            tr.set(\"current_monitoring_state\", mode === \"clips\" ? 1 : 0);     // 1: Auto, 0: In\n            }\n      catch (e) {}\n      }\n\nfunction writeClip(t) {\n      var found = findTrack(t);\n      if (!found) {\n            send(\"/live/applied\", t.key, t.hash, \"no track (MIDI From \" + t.port + \" / Ch. \" + t.channel\n                 + \", or a track named \" + (t.main ? t.part : str(t.clip).replace(/^MuseScore:\\s*/, \"\")) + \")\", \"\");\n            return;\n            }\n      var tr = found.api;\n      var trackName = str(tr.get(\"name\"));\n      var clips = clipsOf(tr);\n      var ours = null, i;\n      for (i = 0; i < clips.length; ++i)\n            if (clips[i].name === t.clip)\n                  ours = clips[i];\n      var fits = ours && Math.abs(ours.start) < 1e-6 && Math.abs(ours.end - t.length) < 1e-6;\n      if (!fits) {\n            for (i = 0; i < clips.length; ++i)\n                  if (clips[i] !== ours && clips[i].start < t.length && clips[i].end > 0) {\n                        send(\"/live/applied\", t.key, t.hash, \"other clips on track \" + trackName + \" (the clip isn't made over them)\",\n                             trackName);\n                        return;\n                        }\n            if (ours)\n                  tr.call(\"delete_clip\", \"id\", ours.id);\n            var before = ids(tr.get(\"arrangement_clips\"));\n            tr.call(\"create_midi_clip\", 0, t.length);\n            var after = ids(tr.get(\"arrangement_clips\"));\n            var id = 0;\n            for (i = 0; i < after.length; ++i)\n                  if (before.indexOf(after[i]) < 0)\n                        id = after[i];\n            if (!id) {\n                  send(\"/live/applied\", t.key, t.hash, \"error: Live made no clip (a frozen track? Live 12.1.10 or later needed)\",\n                       trackName);\n                  return;\n                  }\n            ours = { id: id, api: new LiveAPI(\"id \" + id) };\n            ours.api.set(\"name\", t.clip);\n            }\n      var c = ours.api;\n      c.set(\"muted\", 0);\n      c.call(\"remove_notes_extended\", 0, 128, 0, t.length + 1);\n      for (i = 0; i < t.notes.length; i += BATCH)\n            c.call(\"add_new_notes\", { notes: t.notes.slice(i, i + BATCH) });\n      setMonitor(tr);\n      var was = placed[t.key];\n      placed[t.key] = { track: num(tr.id), clip: t.clip, devs: was && was.track === num(tr.id) ? was.devs : null };\n      send(\"/live/applied\", t.key, t.hash, deviceCheck(tr), trackName);\n      sendParams(placed[t.key], t.key, tr);\n      status(\"MuseScore Link: hub; last clip \" + t.clip + \" on \" + trackName);\n      }\n\nfunction clearClip(w) {\n      var found = findTrack(w);\n      if (!found)\n            return;\n      var clips = clipsOf(found.api);\n      for (var i = 0; i < clips.length; ++i)\n            if (clips[i].name === w.clip)\n                  found.api.call(\"delete_clip\", \"id\", clips[i].id);\n      delete placed[w.key];\n      }\n\nfunction applyMode() {\n      for (var k in placed) {\n            var tr = new LiveAPI(\"id \" + placed[k].track);\n            if (num(tr.id) > 0)\n                  setMonitor(tr);\n            }\n      }\n\n//---------------------------------------------------------\n//   tempo and locators\n//---------------------------------------------------------\n\nfunction writeSong(s) {\n      var song = new LiveAPI(\"live_set\");\n      if (Math.abs(num(song.get(\"tempo\")) - s.bpm) > 1e-4)\n            song.set(\"tempo\", s.bpm);\n      if (num(song.get(\"is_playing\")))\n            return false;\n      var back = num(song.get(\"current_song_time\"));\n      var cueList = function() {\n            var l = ids(song.get(\"cue_points\"));\n            var out = [];\n            for (var i = 0; i < l.length; ++i) {\n                  var c = new LiveAPI(\"id \" + l[i]);\n                  out.push({ id: l[i], api: c, time: num(c.get(\"time\")), name: str(c.get(\"name\")) });\n                  }\n            return out;\n            };\n      var managed = function(name) { return /^MS \\d/.test(name); };\n      var toggleAt = function(time) {\n            song.set(\"current_song_time\", time);\n            song.call(\"set_or_delete_cue\");\n            };\n      var want = {};\n      for (var i = 0; i < s.cues.length; ++i)\n            want[s.cues[i].time.toFixed(6)] = s.cues[i].name;\n      var have = cueList();\n      var at = {};\n      for (i = 0; i < have.length; ++i) {\n            var key = have[i].time.toFixed(6);\n            if (managed(have[i].name) && !(key in want))\n                  toggleAt(have[i].time);             // ours, not wanted: goes\n            else\n                  at[key] = have[i];\n            }\n      for (i = 0; i < s.cues.length; ++i) {\n            var k2 = s.cues[i].time.toFixed(6);\n            var there = at[k2];\n            if (there) {\n                  if (managed(there.name) && there.name !== s.cues[i].name)\n                        there.api.set(\"name\", s.cues[i].name);\n                  continue;                           // (a locator of the owner's there: left)\n                  }\n            toggleAt(s.cues[i].time);\n            var now2 = cueList();\n            for (var j = 0; j < now2.length; ++j)\n                  if (Math.abs(now2[j].time - s.cues[i].time) < 1e-6 && !managed(now2[j].name))\n                        now2[j].api.set(\"name\", s.cues[i].name);\n            }\n      song.set(\"current_song_time\", back);\n      send(\"/live/applied\", \"song\", s.hash, \"ok\", \"\");\n      return true;\n      }\n\n//---------------------------------------------------------\n//   Live's transport, for MuseScore to follow\n//---------------------------------------------------------\n\nfunction report() {\n      var song = new LiveAPI(\"live_set\");\n      var playing = num(song.get(\"is_playing\")) ? 1 : 0;\n      var b = num(song.get(\"current_song_time\"));\n      var t = now();\n      if (playing || playing !== lastTransport.playing || Math.abs(b - lastTransport.beat) > 1e-6 || t - lastTransport.sent > 1000) {\n            send(\"/live/transport\", playing, b, num(song.get(\"tempo\")));\n            lastTransport = { playing: playing, beat: b, sent: t };\n            }\n      }\n\n//---------------------------------------------------------\n//   editing a clip in MuseScore\n//---------------------------------------------------------\n\nvar FIELDS = [\"note_id\", \"pitch\", \"start_time\", \"duration\", \"velocity\", \"mute\", \"probability\", \"velocity_deviation\",\n              \"release_velocity\"];\nvar TICKS = 480;                        // MuseScore's ticks a beat (the edits' times)\nvar CLIP_NOTES_PER_PACKET = 24;         // LiveClipEdit::NOTES_PER_PACKET\n\n// a LOM call's dictionary: a JSON string, maybe in an array, or already an object\nfunction dict(v) {\n      if (v && typeof v === \"object\" && !Array.isArray(v))\n            return v;\n      var s = Array.isArray(v) ? v.join(\" \") : str(v);\n      try { return JSON.parse(s); } catch (e) { return null; }\n      }\n\nfunction readNotes(clip) {\n      var d = dict(clip.call(\"get_all_notes_extended\"));\n      return d && Array.isArray(d.notes) ? d.notes : [];\n      }\n\n// the clip's notes, whatever their order (FNV-1a over every field, by id)\nfunction hashNotes(notes) {\n      var l = notes.slice().sort(function(a, b) { return a.note_id - b.note_id; });\n      var h = 0x811c9dc5;\n      for (var i = 0; i < l.length; ++i) {\n            var s = \"\";\n            for (var k = 0; k < FIELDS.length; ++k)\n                  s += String(l[i][FIELDS[k]]) + \",\";\n            for (var c = 0; c < s.length; ++c) {\n                  h ^= s.charCodeAt(c);\n                  h = (h + ((h << 1) + (h << 4) + (h << 7) + (h << 8) + (h << 24))) >>> 0;\n                  }\n            }\n      return h | 0;\n      }\n\n// the clip shown in the Detail View (an arrangement or a session clip), else the highlighted session slot's\nfunction selectedClip() {\n      var c = new LiveAPI(\"live_set view detail_clip\");\n      if (num(c.id) > 0)\n            return c;\n      var slot = new LiveAPI(\"live_set view highlighted_clip_slot\");\n      if (num(slot.id) > 0 && num(slot.get(\"has_clip\"))) {\n            var l = ids(slot.get(\"clip\"));\n            if (l.length)\n                  return new LiveAPI(\"id \" + l[0]);\n            }\n      return null;\n      }\n\n// the clip's track (an arrangement clip's parent; a session clip's slot's parent)\nfunction trackOf(clip) {\n      var p = ids(clip.get(\"canonical_parent\"));\n      if (!p.length)\n            return null;\n      var o = new LiveAPI(\"id \" + p[0]);\n      if (str(o.type) === \"ClipSlot\") {\n            p = ids(o.get(\"canonical_parent\"));\n            o = p.length ? new LiveAPI(\"id \" + p[0]) : null;\n            }\n      return o;\n      }\n\n// a drum clip: a Drum Rack on its track, or a drum-like track name\nfunction isDrums(track, name) {\n      if (track) {\n            var devs = ids(track.get(\"devices\"));\n            for (var i = 0; i < devs.length; ++i)\n                  if (str(new LiveAPI(\"id \" + devs[i]).get(\"class_name\")) === \"DrumGroupDevice\")\n                        return true;\n            }\n      return /drum|kit|perc|beat/i.test(name);\n      }\n\nfunction startEdit() {\n      var clip = selectedClip();\n      if (!clip) {\n            status(\"MuseScore Link: select a MIDI clip first (its notes shown in the Clip View), then Edit in MuseScore\");\n            return;\n            }\n      if (!num(clip.get(\"is_midi_clip\"))) {\n            status(\"MuseScore Link: that is an audio clip; only MIDI clips can be edited in MuseScore\");\n            return;\n            }\n      var key = \"c\" + num(clip.id);\n      var e = edits[key];\n      if (!e) {\n            e = edits[key] = { key: key, clipId: num(clip.id), notes: [], hash: 0, conflict: false, lastWrite: 0, reply: null,\n                               incoming: null };\n            }\n      sendClip(e);\n      }\n\n// the clip to MuseScore (a new edit, the button again, or a reload after a conflict)\nfunction sendClip(e) {\n      if (!e)\n            return;\n      var clip = new LiveAPI(\"id \" + e.clipId);\n      if (!(num(clip.id) > 0)) {\n            send(\"/live/clip/gone\", e.key);\n            delete edits[e.key];\n            return;\n            }\n      var tr = trackOf(clip);\n      var trackName = tr ? str(tr.get(\"name\")) : \"\";\n      var clipName = str(clip.get(\"name\"));\n      var song = new LiveAPI(\"live_set\");\n      var notes = readNotes(clip);\n      e.notes = notes;\n      e.hash = hashNotes(notes);\n      e.conflict = false;\n      e.gen = ++editSerial;\n      var num_ = num(clip.get(\"signature_numerator\")) || num(song.get(\"signature_numerator\")) || 4;\n      var den = num(clip.get(\"signature_denominator\")) || num(song.get(\"signature_denominator\")) || 4;\n      var end = Math.max(num(clip.get(\"end_marker\")), num(clip.get(\"loop_end\")));\n      var chunks = Math.ceil(notes.length / CLIP_NOTES_PER_PACKET);\n      send(\"/live/clip/begin\", e.key, e.gen, trackName, clipName, isDrums(tr, trackName) ? 1 : 0, num(song.get(\"tempo\")),\n           num_, den, end, num(clip.get(\"loop_start\")), num(clip.get(\"loop_end\")), num(clip.get(\"looping\")) ? 1 : 0,\n           notes.length, chunks, e.hash);\n      for (var c = 0; c < chunks; ++c) {\n            var args = [\"/live/clip/notes\", e.key, e.gen, c];\n            var part = notes.slice(c * CLIP_NOTES_PER_PACKET, (c + 1) * CLIP_NOTES_PER_PACKET);\n            for (var i = 0; i < part.length; ++i) {\n                  var n = part[i];\n                  args.push(num(n.note_id), num(n.pitch), num(n.start_time), num(n.duration), num(n.velocity),\n                            num(n.mute) ? 1 : 0, n.probability === undefined ? 1 : num(n.probability),\n                            num(n.velocity_deviation || 0), n.release_velocity === undefined ? 64 : num(n.release_velocity));\n                  }\n            send.apply(this, args);\n            }\n      e.trackId = tr ? num(tr.id) : 0;\n      e.trackName = trackName;\n      e.copy = copyOn(e.trackId);\n      send(\"/live/clip/track\", e.key, e.trackId, e.copy ? 1 : 0, trackName);\n      sendWhere(e, clip, tr, true);\n      e.devs = null;\n      sendParams(e, e.key, tr);\n      status(\"MuseScore Link: \" + clipName + \" (\" + trackName + \") sent to MuseScore\");\n      }\n\n// a MuseScore Link copy of protocol 4+ (it plays MuseScore's notes) on the track, alive\nfunction copyOn(trackId) {\n      if (!trackId)\n            return false;\n      var r = registry();\n      for (var k in r)\n            if (r[k].track === trackId && (r[k].protocol || 0) >= 4 && now() - r[k].beat < HUB_STALE_MS)\n                  return true;\n      return false;\n      }\n\n// a new hub takes over a clip MuseScore edits (the copy that was the hub went): hash as MuseScore knows the notes\nfunction adoptEdit(key, hash) {\n      var e = edits[key];\n      if (!e) {\n            var id = Number(key.replace(/^c/, \"\"));\n            var clip = id > 0 ? new LiveAPI(\"id \" + id) : null;\n            if (!clip || !(num(clip.id) > 0)) {\n                  send(\"/live/clip/gone\", key);\n                  return;\n                  }\n            e = edits[key] = { key: key, clipId: id, notes: [], hash: 0, conflict: false, lastWrite: 0, reply: null,\n                               incoming: null };\n            e.notes = readNotes(clip);\n            e.hash = hashNotes(e.notes);\n            var tr = trackOf(clip);\n            e.trackId = tr ? num(tr.id) : 0;\n            e.trackName = tr ? str(tr.get(\"name\")) : \"\";\n            }\n      if (e.hash !== hash) {\n            e.conflict = true;\n            send(\"/live/clip/conflict\", key, e.hash);\n            }\n      e.copy = copyOn(e.trackId);\n      send(\"/live/clip/track\", key, e.trackId, e.copy ? 1 : 0, e.trackName);\n      }\n\nfunction reply(e, args) {\n      e.reply = [\"/live/clip/written\"].concat(args);\n      send.apply(this, e.reply);\n      }\n\nfunction applyWrite(e, w) {\n      if (!e || !w)\n            return;\n      e.lastWrite = w.write;\n      var clip = new LiveAPI(\"id \" + e.clipId);\n      if (!(num(clip.id) > 0)) {\n            reply(e, [e.key, w.write, \"gone\", 0]);\n            delete edits[e.key];\n            return;\n            }\n      var before = readNotes(clip);\n      if (e.conflict || hashNotes(before) !== e.hash) {\n            e.conflict = true;\n            reply(e, [e.key, w.write, \"conflict\", hashNotes(before)]);\n            return;\n            }\n      var byId = {};\n      for (var i = 0; i < before.length; ++i)\n            byId[before[i].note_id] = before[i];\n      var removes = [], mods = [], adds = [];\n      for (i = 0; i < w.ops.length; ++i) {\n            var o = w.ops[i];\n            if (o.op === 1) {\n                  if (byId[o.id])\n                        removes.push(o.id);\n                  }\n            else if (o.op === 0) {\n                  var b = byId[o.id];\n                  if (!b)\n                        continue;\n                  var n = {};\n                  for (var k in b)                // Live's own note: only what was edited changes\n                        n[k] = b[k];\n                  if (o.mask & 1) n.pitch = o.pitch;\n                  if (o.mask & 2) n.start_time = o.start / TICKS;\n                  if (o.mask & 4) n.duration = o.duration / TICKS;\n                  if (o.mask & 8) n.velocity = o.velocity;\n                  if (o.mask & 16) n.mute = o.mute ? 1 : 0;\n                  mods.push(n);\n                  }\n            else if (o.op === 2)\n                  adds.push({ pitch: o.pitch, start_time: o.start / TICKS, duration: o.duration / TICKS, velocity: o.velocity,\n                              mute: o.mute ? 1 : 0 });\n            }\n      if (removes.length)\n            clip.call.apply(clip, [\"remove_notes_by_id\"].concat(removes));\n      if (mods.length)\n            clip.call(\"apply_note_modifications\", { notes: mods });\n      var added = [];\n      if (adds.length) {\n            var r = clip.call(\"add_new_notes\", { notes: adds });\n            var l = Array.isArray(r) ? r : (dict(r) && dict(r).note_ids) || str(r).replace(/[\\[\\],]/g, \" \").split(/\\s+/);\n            for (i = 0; i < l.length; ++i)\n                  if (str(l[i]) !== \"\" && !isNaN(Number(l[i])))\n                        added.push(Number(l[i]));\n            }\n      var after = readNotes(clip);\n      if (added.length !== adds.length) {         // (no ids returned: the new notes, matched to what was added)\n            added = [];\n            var had = {};\n            for (i = 0; i < before.length; ++i)\n                  had[before[i].note_id] = true;\n            var fresh = after.filter(function(x) { return !had[x.note_id]; });\n            for (i = 0; i < adds.length; ++i) {\n                  var best = -1, bd = 1e9;\n                  for (var j = 0; j < fresh.length; ++j) {\n                        if (fresh[j].pitch !== adds[i].pitch)\n                              continue;\n                        var dd = Math.abs(fresh[j].start_time - adds[i].start_time) + Math.abs(fresh[j].duration - adds[i].duration);\n                        if (dd < bd) {\n                              bd = dd;\n                              best = j;\n                              }\n                        }\n                  if (best >= 0) {\n                        added.push(fresh[best].note_id);\n                        fresh.splice(best, 1);\n                        }\n                  }\n            }\n      e.notes = after;\n      e.hash = hashNotes(after);\n      reply(e, [e.key, w.write, \"ok\", e.hash].concat(added));\n      status(\"MuseScore Link: \" + (removes.length + mods.length + adds.length) + \" note change(s) from MuseScore written\");\n      }\n\n// once a second: a clip edited in MuseScore changed in Live (or went)?\nfunction checkEdits() {\n      for (var key in edits) {\n            var e = edits[key];\n            var copy = copyOn(e.trackId);                 // (a copy added to or removed from its track)\n            if (e.trackId && copy !== e.copy) {\n                  e.copy = copy;\n                  send(\"/live/clip/track\", key, e.trackId, copy ? 1 : 0, e.trackName || \"\");\n                  }\n            if (e.conflict)\n                  continue;\n            var clip = new LiveAPI(\"id \" + e.clipId);\n            if (!(num(clip.id) > 0)) {\n                  send(\"/live/clip/gone\", key);\n                  delete edits[key];\n                  continue;\n                  }\n            var etr = trackOf(clip);\n            sendWhere(e, clip, etr, false);\n            sendParams(e, key, etr);\n            var h = hashNotes(readNotes(clip));\n            if (h !== e.hash) {\n                  e.conflict = true;\n                  send(\"/live/clip/conflict\", key, h);\n                  status(\"MuseScore Link: \" + str(clip.get(\"name\")) + \" changed in Live: MuseScore stops writing to it\");\n                  }\n            }\n      }\n\n//---------------------------------------------------------\n//   the tracks' parameters and the clips' places (protocol 5)\n//---------------------------------------------------------\n\nvar PARAMS_PER_PACKET = 16;             // about 0.8 kB a datagram\n\n// the clip's place as Live's Python API finds it: the track's index, the session slot's (-1: an arrangement clip)\nfunction whereOf(clip, tr) {\n      var t = -1, s = -1;\n      if (tr) {\n            t = ids(new LiveAPI(\"live_set\").get(\"tracks\")).indexOf(num(tr.id));\n            var p = ids(clip.get(\"canonical_parent\"));\n            if (p.length && str(new LiveAPI(\"id \" + p[0]).type) === \"ClipSlot\")\n                  s = ids(tr.get(\"clip_slots\")).indexOf(p[0]);\n            }\n      return { track: t, slot: s };\n      }\n\nfunction sendWhere(e, clip, tr, always) {\n      var w = whereOf(clip, tr);\n      if (!always && e.where && e.where.track === w.track && e.where.slot === w.slot)\n            return;\n      e.where = w;\n      send(\"/live/clip/where\", e.key, w.track, w.slot);\n      }\n\nfunction isLink(dev) {\n      return /^Mx/.test(str(dev.get(\"class_name\"))) && /^MuseScore Link/.test(str(dev.get(\"name\")));\n      }\n\n// what a lane can automate on the track: the mixer's volume and pan, then every device's parameters (the device's\n// own name before the parameter's), MuseScore Link's left out\nfunction trackParams(tr) {\n      var out = [{ d: -1, p: 0, name: \"Mixer \u203a Volume\", min: 0, max: 1, q: 0 },\n                 { d: -1, p: 1, name: \"Mixer \u203a Pan\", min: -1, max: 1, q: 0 }];\n      var devs = ids(tr.get(\"devices\"));\n      for (var i = 0; i < devs.length; ++i) {\n            var dv = new LiveAPI(\"id \" + devs[i]);\n            if (isLink(dv))\n                  continue;\n            var dn = str(dv.get(\"name\"));\n            var pl = ids(dv.get(\"parameters\"));\n            for (var j = 0; j < pl.length; ++j) {\n                  var pa = new LiveAPI(\"id \" + pl[j]);\n                  out.push({ d: i, p: j, name: dn + \" \u203a \" + str(pa.get(\"name\")), min: num(pa.get(\"min\")) || 0,\n                             max: num(pa.get(\"max\")), q: num(pa.get(\"is_quantized\")) ? 1 : 0 });\n                  }\n            }\n      return out;\n      }\n\nfunction hashParams(list) {\n      var h = 2166136261;\n      var text = \"\";\n      for (var i = 0; i < list.length; ++i)\n            text += list[i].d + \"/\" + list[i].p + \"/\" + list[i].name + \"/\" + list[i].min + \"/\" + list[i].max + \"/\" + list[i].q + \";\";\n      for (var k = 0; k < text.length; ++k) {\n            h ^= text.charCodeAt(k) & 0xff;\n            h = Math.imul ? (Math.imul(h, 16777619) >>> 0) : ((h * 16777619) % 4294967296) >>> 0;\n            }\n      return h | 0;\n      }\n\n// the track's parameters to MuseScore (key: an edited clip's or a route's), when its devices changed since `holder`\n// last sent them (holder.devs: the device ids then)\nfunction sendParams(holder, key, tr) {\n      var devs = tr ? ids(tr.get(\"devices\")).join(\",\") : \"-\";\n      if (holder.devs === devs)\n            return;\n      holder.devs = devs;\n      var list = tr ? trackParams(tr) : [];\n      var h = hashParams(list);\n      var chunks = Math.max(1, Math.ceil(list.length / PARAMS_PER_PACKET));\n      for (var c = 0; c < chunks; ++c) {\n            var args = [\"/live/params\", key, h, c, chunks];\n            var part = list.slice(c * PARAMS_PER_PACKET, (c + 1) * PARAMS_PER_PACKET);\n            for (var i = 0; i < part.length; ++i)\n                  args.push(part[i].d, part[i].p, part[i].name, part[i].min, part[i].max, part[i].q);\n            send.apply(this, args);\n            }\n      }\n\n// the routes' tracks: their devices changed (a device added in Live)\nfunction checkRouteParams() {\n      for (var k in placed) {\n            var tr = new LiveAPI(\"id \" + placed[k].track);\n            if (num(tr.id) > 0)\n                  sendParams(placed[k], k, tr);\n            }\n      }\n\n// a lane titled \"live:<d>/<p>\": that parameter of the track (protocol 5), its LOM id (0: none)\nfunction liveParam(tr, title) {\n      var m = /^live:(-?\\d+)\\/(\\d+)$/.exec(str(title));\n      if (!m || !tr)\n            return 0;\n      var d = Number(m[1]), p = Number(m[2]);\n      if (d < 0) {\n            var mx = ids(tr.get(\"mixer_device\"));\n            if (!mx.length)\n                  return 0;\n            var x = ids(new LiveAPI(\"id \" + mx[0]).get(p === 0 ? \"volume\" : \"panning\"));\n            return x.length ? x[0] : 0;\n            }\n      var devs = ids(tr.get(\"devices\"));\n      if (d >= devs.length)\n            return 0;\n      var pl = ids(new LiveAPI(\"id \" + devs[d]).get(\"parameters\"));\n      return p < pl.length ? pl[p] : 0;\n      }\n\n//---------------------------------------------------------\n//   parameter lanes: the hub\n//---------------------------------------------------------\n\nfunction entry(track) {\n      try { return JSON.parse(g[\"p\" + track] || \"null\"); } catch (e) { return null; }\n      }\n\n// a route's lanes into its track's entry (null: out of it); the copies told. Returns the entry's serial\nfunction putRoute(track, key, route) {\n      var e = entry(track) || { routes: {} };\n      if (route)\n            e.routes[key] = route;\n      else\n            delete e.routes[key];\n      g.pserial = (Number(g.pserial) || 0) + 1;\n      e.serial = g.pserial;\n      e.length = songBeats;\n      g[\"p\" + track] = JSON.stringify(e);\n      if (typeof messnamed === \"function\")\n            messnamed(\"msl_params\", track, e.serial);\n      return e.serial;\n      }\n\nfunction writeParams(w) {\n      var tr = null;\n      if (placed[w.key])\n            tr = new LiveAPI(\"id \" + placed[w.key].track);\n      else if (routes[w.key]) {\n            var found = findTrack(routes[w.key]);\n            tr = found ? found.api : null;\n            }\n      var track = tr && num(tr.id) > 0 ? num(tr.id) : 0;\n      var old = paramTracks[w.key];\n      if (old && old !== track)\n            putRoute(old, w.key, null);\n      if (!track) {\n            delete paramTracks[w.key];\n            if (!w.silent)\n                  send(\"/live/papplied\", w.key, w.hash, w.lanes.length ? \"no track (its clip's track isn't known)\" : \"ok\", \"\");\n            return;\n            }\n      var serial = putRoute(track, w.key, w.lanes.length ? { hash: w.hash, lanes: w.lanes } : null);\n      if (w.lanes.length)\n            paramTracks[w.key] = track;\n      else\n            delete paramTracks[w.key];\n      if (!w.silent)\n            waiting[w.key] = { track: track, serial: serial, hash: w.hash, name: str(tr.get(\"name\")), since: now(), told: false };\n      }\n\n// the copies' answers (each copy writes \"pr<track>\"): /live/papplied\nfunction pollParams() {\n      for (var key in waiting) {\n            var w = waiting[key];\n            var r = null;\n            try { r = JSON.parse(g[\"pr\" + w.track] || \"null\"); } catch (e) {}\n            if (r && r.serial >= w.serial) {\n                  var res = r.results && r.results[key];\n                  send(\"/live/papplied\", key, w.hash, res ? res.status : \"ok\", w.name);\n                  delete waiting[key];\n                  }\n            else if (!w.told && now() - w.since > NO_DEVICE_MS) {\n                  w.told = true;\n                  send(\"/live/papplied\", key, w.hash, \"no MuseScore Link device on the track (its parameters can't be driven)\",\n                       w.name);\n                  }\n            }\n      }\n\n//---------------------------------------------------------\n//   parameter lanes: each copy, its own track\n//---------------------------------------------------------\n\nfunction liveTempo() {\n      try { return num(new LiveAPI(\"live_set\").get(\"tempo\")) || 120; } catch (e) { return 120; }\n      }\nfunction msFactor(bpm) { return PERIOD_QUARTERS * 60000 / bpm; }\n\n// Vst3Plugin::looseTitle: lower case, a slot number in front left out, then a-z 0-9 only\nfunction looseTitle(t) {\n      return str(t).toLowerCase().replace(/^\\s*#?\\d+\\s*[:.)-]?\\s+/, \"\").replace(/[^a-z0-9]/g, \"\");\n      }\n\n// the entry changed (or Live's tempo, or the prefix came): the work, in a Task\nfunction paramsCheck(force) {\n      if (!me.track)\n            return;\n      var e = currentEntry();\n      var used = false;\n      for (var k = 0; k < SLOTS; ++k)\n            if (slots[k])\n                  used = true;\n      var due = force || (e && e.serial !== seenSerial) || (used && Math.abs(liveTempo() - filledBpm) > 1e-6);\n      if (!due)\n            return;\n      if (paramTask)\n            paramTask.schedule(0);\n      else\n            applyParams();\n      }\n\n// the first registered copy on the track (in the track's device order) drives it\nfunction drivesTrack(tr) {\n      var mine = [];\n      var r = registry();\n      for (var k in r)\n            if (r[k].track === me.track && (k === me.key || now() - r[k].beat < HUB_STALE_MS))\n                  mine.push(r[k].device);\n      var devs = ids(tr.get(\"devices\"));\n      for (var i = 0; i < devs.length; ++i)\n            if (mine.indexOf(devs[i]) >= 0)\n                  return devs[i] === me.device;\n      return true;\n      }\n\n// the track's plug-in: the first PluginDevice, else the first device after this one that isn't a Max device\nfunction pluginOf(tr) {\n      var devs = ids(tr.get(\"devices\"));\n      var i, after = -1;\n      for (i = 0; i < devs.length; ++i)\n            if (/PluginDevice$/.test(str(new LiveAPI(\"id \" + devs[i]).get(\"class_name\"))))\n                  return new LiveAPI(\"id \" + devs[i]);\n      for (i = 0; i < devs.length; ++i)\n            if (devs[i] === me.device)\n                  after = i;\n      for (i = after + 1; after >= 0 && i < devs.length; ++i)\n            if (!/^Mx/.test(str(new LiveAPI(\"id \" + devs[i]).get(\"class_name\"))))\n                  return new LiveAPI(\"id \" + devs[i]);\n      return null;\n      }\n\nfunction release(k) {\n      var s = slots[k];\n      if (!s)\n            return;\n      outlet(3, k, \"id\", 0);\n      var b = bases[s.id];\n      if (b) {\n            // Live's own automation of it comes back (tried in Live 12.2: live.remote~ overrides the track's\n            // automation while it drives, and setting the value leaves that automation overridden); else Live's\n            // value back as it was\n            // (re-enabled a little later: right after \"id 0\" live.remote~ still holds it, and Live keeps the last\n            // driven value until something re-enables its automation)\n            try {\n                  var api = new LiveAPI(\"id \" + s.id);\n                  if (num(api.get(\"automation_state\")) > 0) {\n                        var pid = s.id;\n                        var later = new Task(function() {\n                              try { new LiveAPI(\"id \" + pid).call(\"re_enable_automation\"); } catch (e2) {}\n                              }, self);\n                        later.schedule(150);\n                        keepTasks.push(later);            // (held until it ran)\n                        if (keepTasks.length > 32)\n                              keepTasks.shift();\n                        }\n                  else\n                        api.set(\"value\", b.value);\n                  } catch (e) {}\n            delete bases[s.id];\n            }\n      slots[k] = null;\n      }\n\nfunction applyParams() {\n      if (!me.track)\n            return;\n      var e = currentEntry();\n      if (!e || !prefix)\n            return;                           // (the prefix comes from loadmess: then again)\n      var tr = new LiveAPI(\"id \" + me.track);\n      var bpm = liveTempo();\n      outlet(4, msFactor(bpm));\n      var k, had = 0;\n      for (k = 0; k < SLOTS; ++k)\n            if (slots[k])\n                  ++had;\n      if (!drivesTrack(tr)) {                 // (another copy on this track does it)\n            for (k = 0; k < SLOTS; ++k)\n                  release(k);\n            seenSerial = e.serial;\n            return;\n            }\n      var plugin = pluginOf(tr);\n      var params = [];                        // { id, name, loose }\n      if (plugin) {\n            var pl = ids(plugin.get(\"parameters\"));\n            for (var i = 0; i < pl.length; ++i) {\n                  var name = str(new LiveAPI(\"id \" + pl[i]).get(\"name\"));\n                  params.push({ id: pl[i], name: name, exact: str(name).toLowerCase().replace(/[^a-z0-9]/g, \"\"),\n                                loose: looseTitle(name) });\n                  }\n            }\n      // by title; else by the plug-in's parameter id where Live names the parameter by its slot only (Kontakt in\n      // Live 12.2: \"#001\" for the slot whose title MuseScore's host reads as \"Vibrato\", id 1)\n      var find = function(title, pid) {\n            var x = str(title).toLowerCase().replace(/[^a-z0-9]/g, \"\"), y = looseTitle(title), j, m;\n            for (j = 0; x && j < params.length; ++j)\n                  if (params[j].exact === x)\n                        return params[j].id;\n            for (j = 0; y && j < params.length; ++j)\n                  if (params[j].loose === y)\n                        return params[j].id;\n            for (j = 0; pid >= 0 && j < params.length; ++j) {\n                  m = /^\\s*#?0*(\\d+)\\b/.exec(params[j].name);\n                  if (m && Number(m[1]) === pid)\n                        return params[j].id;\n                  }\n            return 0;\n            };\n      var want = [], wanted = {}, results = {};\n      for (var key in e.routes) {\n            var route = e.routes[key], missing = [], extra = [], noPlugin = false;\n            for (var l = 0; l < route.lanes.length; ++l) {\n                  var lane = route.lanes[l], id = 0;\n                  if (/^live:/.test(str(lane.title)))\n                        id = liveParam(tr, lane.title);\n                  else if (!plugin) {\n                        noPlugin = true;\n                        continue;\n                        }\n                  else\n                        id = find(lane.title, lane.pid === undefined ? -1 : num(lane.pid));\n                  if (!id)\n                        missing.push(lane.title);\n                  else if (wanted[id])\n                        continue;                         // (two lanes on one parameter: the first)\n                  else if (want.length >= SLOTS)\n                        extra.push(lane.title);\n                  else {\n                        wanted[id] = true;\n                        want.push({ id: id, lane: lane });\n                        }\n                  }\n            var st = [];\n            if (noPlugin)\n                  st.push(\"no plug-in on the track\");\n            if (missing.length)\n                  st.push(\"missing: \" + missing.join(\", \"));\n            if (extra.length)\n                  st.push(\"too many lanes (\" + SLOTS + \" at most): \" + extra.join(\", \"));\n            results[key] = { status: st.length ? st.join(\"; \") : \"ok\" };\n            }\n      // slots: a parameter keeps its slot; the others go\n      var at = {};\n      for (k = 0; k < SLOTS; ++k) {\n            if (slots[k] && wanted[slots[k].id])\n                  at[slots[k].id] = k;\n            else\n                  release(k);\n            }\n      for (i = 0; i < want.length; ++i) {\n            var w = want[i];\n            if (at[w.id] === undefined)\n                  for (k = 0; k < SLOTS; ++k)\n                        if (!slots[k]) {\n                              slots[k] = { id: w.id, sig: \"\", fresh: true };\n                              at[w.id] = k;\n                              break;\n                              }\n            k = at[w.id];\n            var p = new LiveAPI(\"id \" + w.id);\n            if (!bases[w.id])                       // (read before it is taken)\n                  bases[w.id] = { value: num(p.get(\"value\")), min: num(p.get(\"min\")), max: num(p.get(\"max\")) };\n            fillSlot(k, w.lane, bases[w.id], bpm, num(e.length));\n            if (slots[k].fresh) {\n                  slots[k].fresh = false;\n                  outlet(3, k, \"id\", w.id);\n                  }\n            }\n      filledBpm = bpm;\n      seenSerial = e.serial;\n      if (!e.fromSet)\n            keep(e);\n      g[\"pr\" + me.track] = JSON.stringify({ serial: e.serial, results: results });\n      if (want.length || had)\n            status(\"MuseScore Link: \" + want.length + \" plug-in parameter\" + (want.length === 1 ? \"\" : \"s\") + \" driven\"\n                   + (isHub ? \" (hub)\" : \"\"));\n      }\n\n//---------------------------------------------------------\n//   parameter lanes kept in the Live Set\n//---------------------------------------------------------\n\n// the lanes this copy plays: the hub's entry for the track (MuseScore's word in this Live session), else those\n// the set kept (MuseScore not running, or not yet connected)\nfunction currentEntry() {\n      var e = entry(me.track);\n      if (e)\n            return e;\n      if (!saved)\n            return null;\n      return { serial: \"saved\" + savedSerial, length: saved.length, routes: saved.routes, fromSet: true };\n      }\n\n// the hub's entry applied: kept in [pattr Lanes] (Live saves it with the set)\nfunction keep(e) {\n      var next = { length: num(e.length) || 0, routes: e.routes || {} };\n      if (saved && JSON.stringify(saved) === JSON.stringify(next))\n            return;\n      saved = next;\n      if (storeTask)\n            storeTask.schedule(STORE_DELAY_MS);   // (again: the delay starts over)\n      else\n            flushStores();\n      }\n\n// the lanes into the stores (only the parts that changed)\nfunction flushStores() {\n      var chunks = splitSaved(encodeSaved(saved));\n      keptStatus = chunks ? \"\" : \"too many lane events to keep in the Live Set (they play while MuseScore runs)\";\n      if (!chunks)\n            chunks = splitSaved(encodeSaved(null));\n      for (var k = 0; k < STORES; ++k) {      // (Live: one undo step, \"Change in MuseScore Link\")\n            var part = k < chunks.length ? chunks[k] : [SAVED_TAG, SAVED_VERSION, 0, k, 0];\n            var j = JSON.stringify(part);\n            if (sentParts[k] === j)\n                  continue;\n            sentParts[k] = j;\n            outlet(6, [k].concat(part));\n            }\n      }\n\n// the encoded lanes in the stores' parts: each \"msl-lanes 2 <stamp> <k> <parts> \u2026\" (the stamp: parts of one value);\n// null: too long\nfunction splitSaved(a) {\n      var data = a.slice(2), n = Math.max(1, Math.ceil(data.length / (STORE_ATOMS - 5)));\n      if (n > STORES)\n            return null;\n      var stamp = Math.floor(Math.random() * 1e9), out = [];\n      for (var k = 0; k < n; ++k)\n            out.push([SAVED_TAG, SAVED_VERSION, stamp, k, n].concat(data.slice(k * (STORE_ATOMS - 5), (k + 1) * (STORE_ATOMS - 5))));\n      return out;\n      }\n\n// the saved value as atoms (numbers and symbols: no JSON symbol to intern in Max at each change):\n//   msl-lanes 2 <length beats> <routes> then per route: <key> <hash> <lanes>, per lane: <title> <pid> <atoms>\n//   then the lane's events packed (packLane) in <atoms> atoms (version 1: <pairs>, then (time value) \u00d7 pairs)\nfunction encodeSaved(sv) {\n      var a = [SAVED_TAG, SAVED_VERSION, sv ? num(sv.length) || 0 : 0];\n      var keys = [];\n      if (sv)\n            for (var k in sv.routes)\n                  keys.push(k);\n      a.push(keys.length);\n      for (var i = 0; i < keys.length; ++i) {\n            var r = sv.routes[keys[i]];\n            a.push(keys[i], num(r.hash) || 0, r.lanes.length);\n            for (var l = 0; l < r.lanes.length; ++l) {\n                  var lane = r.lanes[l];\n                  var packed = packLane(lane.ev);\n                  a.push(str(lane.title), lane.pid === undefined ? -1 : num(lane.pid), packed.length);\n                  for (var j = 0; j < packed.length; ++j)\n                        a.push(packed[j]);\n                  }\n            }\n      return a;\n      }\n\n// a lane's events (time value \u2026, as MuseScore sends them: a point, then a ramp's steps every 30 ticks as far as\n// the value moves by 0.001) in fewer atoms: an event \"time value\" (time >= 0), or a run of m >= 3 evenly spaced\n// steps \"-m t1 v1 tm vm vh\": step k (0 \u2026 m-1) at round(t1 + k (tm - t1) / (m - 1)), its value on the parabola\n// through v1 (k = 0), vh (k = h = floor((m - 1) / 2)) and vm (k = m - 1). A straight ramp is one run, a curved one\n// one or a few. A run is taken while each step's value is within PACK_DV of the original, and its time within 2\n// units (0.26 ms at 120 bpm) of it unless the step itself moves by no more than PACK_DV (a slow ramp's 0.001 steps\n// come at uneven times): what plays is MuseScore's staircase to within 0.0015.\n// livesetwriter.cpp packLane does the same (Create Live Set)\nvar PACK_DV = 0.0015;\nvar PACK_MAX = 4096;                    // steps a run at most\nfunction runValue(k, m, v1, vh, vm) {\n      var h = Math.floor((m - 1) / 2), e = m - 1;\n      if (h === 0)\n            return v1 + k * (vm - v1) / e;\n      return v1 * (k - h) * (k - e) / (h * e) - vh * k * (k - e) / (h * (e - h)) + vm * k * (k - h) / (e * (e - h));\n      }\nfunction runFits(ev, i, j) {\n      var m = j - i + 1, t1 = num(ev[2 * i]), tm = num(ev[2 * j]);\n      if (!(tm > t1))\n            return false;\n      var v1 = num(ev[2 * i + 1]), vm = num(ev[2 * j + 1]), vh = num(ev[2 * (i + Math.floor((m - 1) / 2)) + 1]);\n      for (var k = 1; k < m - 1; ++k) {\n            var t = Math.round(t1 + k * (tm - t1) / (m - 1)), o = num(ev[2 * (i + k) + 1]);\n            if (Math.abs(runValue(k, m, v1, vh, vm) - o) > PACK_DV)\n                  return false;\n            if (Math.abs(t - num(ev[2 * (i + k)])) > 2 && Math.abs(o - num(ev[2 * (i + k) - 1])) > PACK_DV)\n                  return false;\n            }\n      return true;\n      }\nfunction packLane(ev) {\n      var n = Math.floor(ev.length / 2), out = [], i = 0;\n      while (i < n) {\n            var j = i + 2, best = -1;\n            while (j < n && j - i < PACK_MAX && runFits(ev, i, j)) {\n                  best = j;\n                  ++j;\n                  }\n            if (best >= 0) {\n                  var m = best - i + 1;\n                  out.push(-m, num(ev[2 * i]), num(ev[2 * i + 1]), num(ev[2 * best]), num(ev[2 * best + 1]),\n                           num(ev[2 * (i + Math.floor((m - 1) / 2)) + 1]));\n                  i = best + 1;\n                  }\n            else {\n                  out.push(num(ev[2 * i]), num(ev[2 * i + 1]));\n                  ++i;\n                  }\n            }\n      return out;\n      }\nfunction unpackLane(a) {\n      var ev = [], p = 0;\n      while (p < a.length) {\n            var x = num(a[p]);\n            if (x >= 0) {\n                  if (p + 1 >= a.length)\n                        return null;\n                  ev.push(x, num(a[p + 1]));\n                  p += 2;\n                  continue;\n                  }\n            var m = -x;\n            if (p + 5 >= a.length || m < 3)\n                  return null;\n            var t1 = num(a[p + 1]), v1 = num(a[p + 2]), tm = num(a[p + 3]), vm = num(a[p + 4]), vh = num(a[p + 5]);\n            for (var k = 0; k < m; ++k)\n                  ev.push(Math.round(t1 + k * (tm - t1) / (m - 1)), runValue(k, m, v1, vh, vm));\n            p += 6;\n            }\n      return ev;\n      }\n\n// the atoms back (null: not this format, or cut short)\nfunction decodeSaved(a) {\n      if (!a || a.length < 4 || str(a[0]) !== SAVED_TAG || !(num(a[1]) === 1 || num(a[1]) === 2))\n            return null;\n      var version = num(a[1]);\n      var sv = { length: num(a[2]) || 0, routes: {} };\n      var p = 4, n = num(a[3]);\n      for (var i = 0; i < n; ++i) {\n            if (p + 3 > a.length)\n                  return null;\n            var key = str(a[p]), route = { hash: num(a[p + 1]), lanes: [] }, lanes = num(a[p + 2]);\n            p += 3;\n            for (var l = 0; l < lanes; ++l) {\n                  if (p + 3 > a.length)\n                        return null;\n                  var lane = { title: str(a[p]), pid: num(a[p + 1]), ev: [] }, count = num(a[p + 2]);\n                  p += 3;\n                  if (version === 1)\n                        count *= 2;           // (pairs)\n                  if (p + count > a.length)\n                        return null;\n                  lane.ev = version === 1 ? a.slice(p, p + count).map(num) : unpackLane(a.slice(p, p + count));\n                  if (!lane.ev)\n                        return null;\n                  p += count;\n                  route.lanes.push(lane);\n                  }\n            sv.routes[key] = route;\n            }\n      return sv;\n      }\n\n// a store's value (\"lanes <k> \u2026\"): the set opened, the device pasted or duplicated, Live's undo. When the parts of\n// one value are all there, they are the lanes (an echo of what the script set is left alone)\nfunction restoreSaved(args) {\n      var k = num(args[0]), part = args.slice(1);\n      restoredLog.push(\"lanes \" + k + \": \" + part.length + \" atoms: \" + str(part.slice(0, 6)));\n      if (restoredLog.length > 20)\n            restoredLog.shift();\n      try {                                   // (debugging: the \"saved\" probe)\n            var info = JSON.parse(g.savedInfo || \"{}\");\n            info[me.key] = restoredLog.slice(-4);\n            g.savedInfo = JSON.stringify(info);\n            }\n      catch (e) {}\n      if (!(k >= 0 && k < STORES))\n            return;\n      if (sentParts[k] === JSON.stringify(part))\n            return;\n      parts[k] = part;\n      sentParts[k] = undefined;\n      // the parts of one value: same stamp, 0 \u2026 n-1\n      var p0 = parts[0];\n      if (!p0 || str(p0[0]) !== SAVED_TAG || !(num(p0[1]) === 1 || num(p0[1]) === 2))\n            return;\n      var stamp = num(p0[2]), n = num(p0[4]), data = [];\n      if (n === 0) {                          // (nothing kept)\n            if (saved) {\n                  saved = null;\n                  ++savedSerial;\n                  paramsCheck(true);\n                  }\n            return;\n            }\n      for (var i = 0; i < n; ++i) {\n            var pi = parts[i];\n            if (!pi || num(pi[2]) !== stamp || num(pi[3]) !== i || num(pi[4]) !== n)\n                  return;                     // (another part still to come)\n            data = data.concat(pi.slice(5));\n            }\n      var sv = decodeSaved([SAVED_TAG, num(p0[1])].concat(data));\n      saved = sv && Object.keys(sv.routes).length ? sv : null;\n      ++savedSerial;\n      paramsCheck(true);\n      }\n\n// slot k's table: the value in force at each ms of the song, in the parameter's range\nfunction fillSlot(k, lane, base, bpm, lengthBeats) {\n      var msPerBeat = 60000 / bpm, span = base.max - base.min;\n      var ev = [];\n      for (var i = 0; i + 1 < lane.ev.length; i += 2)\n            ev.push({ at: Math.max(0, Math.round(lane.ev[i] / UNITS * msPerBeat)), v: base.min + lane.ev[i + 1] * span, n: i });\n      ev.sort(function(a, b) { return a.at - b.at || a.n - b.n; });\n      var last = ev.length ? ev[ev.length - 1].at : 0;\n      var n = Math.min(MAX_MS, Math.ceil(Math.max(lengthBeats * msPerBeat, last + 1000)) + 1);\n      var sig = n + \"|\" + bpm + \"|\" + base.value + \"|\" + base.min + \"|\" + base.max + \"|\" + lane.ev.join(\",\");\n      if (slots[k].sig === sig)\n            return;\n      var b = new Buffer(prefix + k);\n      b.send(\"sizeinsamps\", n, 1);\n      var v = base.value, j = 0, chunk = [];\n      for (i = 0; i < n; ++i) {\n            while (j < ev.length && ev[j].at <= i)\n                  v = ev[j++].v;\n            chunk.push(v);\n            if (chunk.length === POKE || i === n - 1) {\n                  b.poke(1, i + 1 - chunk.length, chunk);\n                  chunk = [];\n                  }\n            }\n      slots[k].sig = sig;\n      }\n\n//---------------------------------------------------------\n//   debugging in real Live (hub): /ms/probe, /ms/probecall\n//---------------------------------------------------------\n\nfunction probeText(v) {\n      if (v === undefined || v === null)\n            return \"\";\n      if (typeof v === \"object\")\n            try { return JSON.stringify(v); } catch (e) {}\n      return str(v);\n      }\n\nfunction probe(id, what) {\n      if (what === \"pos\") {\n            probes.push(id);\n            outlet(5, \"bang\");                  // ([snapshot~] answers \"posvalue <ms>\")\n            return;\n            }\n      if (what === \"saved\") {                   // (the lanes kept in the set: every copy's, through the Global)\n            return send(\"/live/probe\", id, str(g.savedInfo).substring(0, 7000));\n            }\n      if (what === \"state\") {\n            var o = { me: me, prefix: prefix, slots: slots, bases: bases, seenSerial: seenSerial, filledBpm: filledBpm,\n                      saved: saved ? { length: saved.length, routes: Object.keys(saved.routes), atoms: encodeSaved(saved).length }\n                                   : null, savedSerial: savedSerial, restoredLog: restoredLog,\n                      waiting: waiting, paramTracks: paramTracks, entries: {} };\n            var r = registry();\n            for (var k in r)\n                  if (!o.entries[r[k].track])\n                        o.entries[r[k].track] = { p: str(g[\"p\" + r[k].track]).substring(0, 1500),\n                                                  pr: str(g[\"pr\" + r[k].track]) };\n            return send(\"/live/probe\", id, probeText(o).substring(0, 7000));\n            }\n      try {\n            var api = new LiveAPI(what);\n            var text = \"id \" + num(api.id) + \" type \" + str(api.type) + \" path \" + str(api.path) + \"\\n\" + str(api.info);\n            send(\"/live/probe\", id, text.substring(0, 7000));\n            }\n      catch (e) {\n            send(\"/live/probe\", id, \"error: \" + e);\n            }\n      }\n\nfunction answerPos(ms) {\n      var t = \"pos \" + ms + \" ms; factor \" + msFactor(liveTempo()) + \"; tempo \" + liveTempo() + \"; song time \"\n              + num(new LiveAPI(\"live_set\").get(\"current_song_time\"));\n      while (probes.length)\n            send(\"/live/probe\", probes.shift(), t);\n      }\n\nfunction probeCall(id, path, fn, args) {\n      try {\n            var api = new LiveAPI(path);\n            var r;\n            if (fn === \"get\")\n                  r = api.get(str(args[0]));\n            else if (fn === \"set\")\n                  r = api.set.apply(api, [str(args[0])].concat(args.slice(1)));\n            else\n                  r = api.call.apply(api, [fn].concat(args));\n            send(\"/live/probe\", id, probeText(r).substring(0, 7000));\n            }\n      catch (e) {\n            send(\"/live/probe\", id, \"error: \" + e);\n            }\n      }\n\n// (Node tests)\nif (typeof module !== \"undefined\")\n      module.exports = { handle: handle, workStep: workStep, displayName: displayName, ids: ids, loosePort: loosePort,\n                         findTrack: findTrack, writeSong: writeSong, report: report, hashNotes: hashNotes,\n                         checkEdits: checkEdits, edit: edit, looseTitle: looseTitle, applyParams: applyParams,\n                         pollParams: pollParams, restoreSaved: restoreSaved, encodeSaved: encodeSaved, packLane: packLane,\n                         unpackLane: unpackLane, flushStores: flushStores,\n                         decodeSaved: decodeSaved, state: function() {\n                               return { isHub: isHub, work: work, pending: pending, placed: placed, mode: mode, me: me,\n                                        edits: edits, slots: slots, bases: bases, waiting: waiting, prefix: prefix,\n                                        pendingParams: pendingParams, saved: saved };\n                               } };\n",
						"filename": "none",
						"flags": 0,
						"embed": 1,
						"autowatch": 1
					}
				}
			},
			{
				"box": {
					"id": "obj-61",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 5,
					"patching_rect": [
						700,
						420,
						120,
						22
					],
					"text": "route 0 1 2 3",
					"outlettype": [
						"",
						"",
						"",
						"",
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-62",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 3,
					"patching_rect": [
						700,
						450,
						100,
						22
					],
					"text": "pattr Lanes",
					"outlettype": [
						"",
						"",
						""
					],
					"varname": "Lanes",
					"parameter_enable": 1,
					"saved_object_attributes": {
						"parameter_enable": 1
					},
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Lanes",
							"parameter_shortname": "Lanes",
							"parameter_type": 3,
							"parameter_invisible": 1,
							"parameter_initial_enable": 0
						}
					}
				}
			},
			{
				"box": {
					"id": "obj-63",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						700,
						480,
						121,
						22
					],
					"text": "prepend lanes 0",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-64",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 3,
					"patching_rect": [
						810,
						450,
						100,
						22
					],
					"text": "pattr Lanes2",
					"outlettype": [
						"",
						"",
						""
					],
					"varname": "Lanes2",
					"parameter_enable": 1,
					"saved_object_attributes": {
						"parameter_enable": 1
					},
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Lanes2",
							"parameter_shortname": "Lanes2",
							"parameter_type": 3,
							"parameter_invisible": 1,
							"parameter_initial_enable": 0
						}
					}
				}
			},
			{
				"box": {
					"id": "obj-65",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						810,
						480,
						121,
						22
					],
					"text": "prepend lanes 1",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-66",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 3,
					"patching_rect": [
						920,
						450,
						100,
						22
					],
					"text": "pattr Lanes3",
					"outlettype": [
						"",
						"",
						""
					],
					"varname": "Lanes3",
					"parameter_enable": 1,
					"saved_object_attributes": {
						"parameter_enable": 1
					},
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Lanes3",
							"parameter_shortname": "Lanes3",
							"parameter_type": 3,
							"parameter_invisible": 1,
							"parameter_initial_enable": 0
						}
					}
				}
			},
			{
				"box": {
					"id": "obj-67",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						920,
						480,
						121,
						22
					],
					"text": "prepend lanes 2",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-68",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 3,
					"patching_rect": [
						1030,
						450,
						100,
						22
					],
					"text": "pattr Lanes4",
					"outlettype": [
						"",
						"",
						""
					],
					"varname": "Lanes4",
					"parameter_enable": 1,
					"saved_object_attributes": {
						"parameter_enable": 1
					},
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Lanes4",
							"parameter_shortname": "Lanes4",
							"parameter_type": 3,
							"parameter_invisible": 1,
							"parameter_initial_enable": 0
						}
					}
				}
			},
			{
				"box": {
					"id": "obj-69",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						1030,
						480,
						121,
						22
					],
					"text": "prepend lanes 3",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-70",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 0,
					"patching_rect": [
						500,
						500,
						170,
						22
					],
					"text": "udpsend 127.0.0.1 9002"
				}
			},
			{
				"box": {
					"id": "obj-71",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 1,
					"patching_rect": [
						30,
						450,
						296,
						22
					],
					"text": "phasor~ @frequency 7864320 ticks @lock 1",
					"outlettype": [
						"signal"
					]
				}
			},
			{
				"box": {
					"id": "obj-72",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 1,
					"patching_rect": [
						30,
						490,
						51,
						22
					],
					"text": "*~ 1.",
					"outlettype": [
						"signal"
					]
				}
			},
			{
				"box": {
					"id": "obj-73",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 1,
					"patching_rect": [
						200,
						490,
						79,
						22
					],
					"text": "snapshot~",
					"outlettype": [
						"float"
					]
				}
			},
			{
				"box": {
					"id": "obj-74",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						200,
						520,
						128,
						22
					],
					"text": "prepend posvalue",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-75",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 17,
					"patching_rect": [
						30,
						650,
						300,
						22
					],
					"text": "route 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15",
					"outlettype": [
						"",
						"",
						"",
						"",
						"",
						"",
						"",
						"",
						"",
						"",
						"",
						"",
						"",
						"",
						"",
						"",
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-76",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 2,
					"patching_rect": [
						30,
						690,
						65,
						22
					],
					"text": "buffer~ ---mslp0",
					"outlettype": [
						"float",
						"bang"
					]
				}
			},
			{
				"box": {
					"id": "obj-77",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 1,
					"patching_rect": [
						30,
						720,
						65,
						22
					],
					"text": "index~ ---mslp0",
					"outlettype": [
						"signal"
					]
				}
			},
			{
				"box": {
					"id": "obj-78",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 0,
					"patching_rect": [
						30,
						750,
						65,
						22
					],
					"text": "live.remote~"
				}
			},
			{
				"box": {
					"id": "obj-79",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 2,
					"patching_rect": [
						100,
						690,
						65,
						22
					],
					"text": "buffer~ ---mslp1",
					"outlettype": [
						"float",
						"bang"
					]
				}
			},
			{
				"box": {
					"id": "obj-80",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 1,
					"patching_rect": [
						100,
						720,
						65,
						22
					],
					"text": "index~ ---mslp1",
					"outlettype": [
						"signal"
					]
				}
			},
			{
				"box": {
					"id": "obj-81",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 0,
					"patching_rect": [
						100,
						750,
						65,
						22
					],
					"text": "live.remote~"
				}
			},
			{
				"box": {
					"id": "obj-82",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 2,
					"patching_rect": [
						170,
						690,
						65,
						22
					],
					"text": "buffer~ ---mslp2",
					"outlettype": [
						"float",
						"bang"
					]
				}
			},
			{
				"box": {
					"id": "obj-83",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 1,
					"patching_rect": [
						170,
						720,
						65,
						22
					],
					"text": "index~ ---mslp2",
					"outlettype": [
						"signal"
					]
				}
			},
			{
				"box": {
					"id": "obj-84",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 0,
					"patching_rect": [
						170,
						750,
						65,
						22
					],
					"text": "live.remote~"
				}
			},
			{
				"box": {
					"id": "obj-85",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 2,
					"patching_rect": [
						240,
						690,
						65,
						22
					],
					"text": "buffer~ ---mslp3",
					"outlettype": [
						"float",
						"bang"
					]
				}
			},
			{
				"box": {
					"id": "obj-86",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 1,
					"patching_rect": [
						240,
						720,
						65,
						22
					],
					"text": "index~ ---mslp3",
					"outlettype": [
						"signal"
					]
				}
			},
			{
				"box": {
					"id": "obj-87",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 0,
					"patching_rect": [
						240,
						750,
						65,
						22
					],
					"text": "live.remote~"
				}
			},
			{
				"box": {
					"id": "obj-88",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 2,
					"patching_rect": [
						310,
						690,
						65,
						22
					],
					"text": "buffer~ ---mslp4",
					"outlettype": [
						"float",
						"bang"
					]
				}
			},
			{
				"box": {
					"id": "obj-89",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 1,
					"patching_rect": [
						310,
						720,
						65,
						22
					],
					"text": "index~ ---mslp4",
					"outlettype": [
						"signal"
					]
				}
			},
			{
				"box": {
					"id": "obj-90",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 0,
					"patching_rect": [
						310,
						750,
						65,
						22
					],
					"text": "live.remote~"
				}
			},
			{
				"box": {
					"id": "obj-91",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 2,
					"patching_rect": [
						380,
						690,
						65,
						22
					],
					"text": "buffer~ ---mslp5",
					"outlettype": [
						"float",
						"bang"
					]
				}
			},
			{
				"box": {
					"id": "obj-92",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 1,
					"patching_rect": [
						380,
						720,
						65,
						22
					],
					"text": "index~ ---mslp5",
					"outlettype": [
						"signal"
					]
				}
			},
			{
				"box": {
					"id": "obj-93",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 0,
					"patching_rect": [
						380,
						750,
						65,
						22
					],
					"text": "live.remote~"
				}
			},
			{
				"box": {
					"id": "obj-94",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 2,
					"patching_rect": [
						450,
						690,
						65,
						22
					],
					"text": "buffer~ ---mslp6",
					"outlettype": [
						"float",
						"bang"
					]
				}
			},
			{
				"box": {
					"id": "obj-95",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 1,
					"patching_rect": [
						450,
						720,
						65,
						22
					],
					"text": "index~ ---mslp6",
					"outlettype": [
						"signal"
					]
				}
			},
			{
				"box": {
					"id": "obj-96",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 0,
					"patching_rect": [
						450,
						750,
						65,
						22
					],
					"text": "live.remote~"
				}
			},
			{
				"box": {
					"id": "obj-97",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 2,
					"patching_rect": [
						520,
						690,
						65,
						22
					],
					"text": "buffer~ ---mslp7",
					"outlettype": [
						"float",
						"bang"
					]
				}
			},
			{
				"box": {
					"id": "obj-98",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 1,
					"patching_rect": [
						520,
						720,
						65,
						22
					],
					"text": "index~ ---mslp7",
					"outlettype": [
						"signal"
					]
				}
			},
			{
				"box": {
					"id": "obj-99",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 0,
					"patching_rect": [
						520,
						750,
						65,
						22
					],
					"text": "live.remote~"
				}
			},
			{
				"box": {
					"id": "obj-100",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 2,
					"patching_rect": [
						590,
						690,
						65,
						22
					],
					"text": "buffer~ ---mslp8",
					"outlettype": [
						"float",
						"bang"
					]
				}
			},
			{
				"box": {
					"id": "obj-101",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 1,
					"patching_rect": [
						590,
						720,
						65,
						22
					],
					"text": "index~ ---mslp8",
					"outlettype": [
						"signal"
					]
				}
			},
			{
				"box": {
					"id": "obj-102",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 0,
					"patching_rect": [
						590,
						750,
						65,
						22
					],
					"text": "live.remote~"
				}
			},
			{
				"box": {
					"id": "obj-103",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 2,
					"patching_rect": [
						660,
						690,
						65,
						22
					],
					"text": "buffer~ ---mslp9",
					"outlettype": [
						"float",
						"bang"
					]
				}
			},
			{
				"box": {
					"id": "obj-104",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 1,
					"patching_rect": [
						660,
						720,
						65,
						22
					],
					"text": "index~ ---mslp9",
					"outlettype": [
						"signal"
					]
				}
			},
			{
				"box": {
					"id": "obj-105",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 0,
					"patching_rect": [
						660,
						750,
						65,
						22
					],
					"text": "live.remote~"
				}
			},
			{
				"box": {
					"id": "obj-106",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 2,
					"patching_rect": [
						730,
						690,
						65,
						22
					],
					"text": "buffer~ ---mslp10",
					"outlettype": [
						"float",
						"bang"
					]
				}
			},
			{
				"box": {
					"id": "obj-107",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 1,
					"patching_rect": [
						730,
						720,
						65,
						22
					],
					"text": "index~ ---mslp10",
					"outlettype": [
						"signal"
					]
				}
			},
			{
				"box": {
					"id": "obj-108",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 0,
					"patching_rect": [
						730,
						750,
						65,
						22
					],
					"text": "live.remote~"
				}
			},
			{
				"box": {
					"id": "obj-109",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 2,
					"patching_rect": [
						800,
						690,
						65,
						22
					],
					"text": "buffer~ ---mslp11",
					"outlettype": [
						"float",
						"bang"
					]
				}
			},
			{
				"box": {
					"id": "obj-110",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 1,
					"patching_rect": [
						800,
						720,
						65,
						22
					],
					"text": "index~ ---mslp11",
					"outlettype": [
						"signal"
					]
				}
			},
			{
				"box": {
					"id": "obj-111",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 0,
					"patching_rect": [
						800,
						750,
						65,
						22
					],
					"text": "live.remote~"
				}
			},
			{
				"box": {
					"id": "obj-112",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 2,
					"patching_rect": [
						870,
						690,
						65,
						22
					],
					"text": "buffer~ ---mslp12",
					"outlettype": [
						"float",
						"bang"
					]
				}
			},
			{
				"box": {
					"id": "obj-113",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 1,
					"patching_rect": [
						870,
						720,
						65,
						22
					],
					"text": "index~ ---mslp12",
					"outlettype": [
						"signal"
					]
				}
			},
			{
				"box": {
					"id": "obj-114",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 0,
					"patching_rect": [
						870,
						750,
						65,
						22
					],
					"text": "live.remote~"
				}
			},
			{
				"box": {
					"id": "obj-115",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 2,
					"patching_rect": [
						940,
						690,
						65,
						22
					],
					"text": "buffer~ ---mslp13",
					"outlettype": [
						"float",
						"bang"
					]
				}
			},
			{
				"box": {
					"id": "obj-116",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 1,
					"patching_rect": [
						940,
						720,
						65,
						22
					],
					"text": "index~ ---mslp13",
					"outlettype": [
						"signal"
					]
				}
			},
			{
				"box": {
					"id": "obj-117",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 0,
					"patching_rect": [
						940,
						750,
						65,
						22
					],
					"text": "live.remote~"
				}
			},
			{
				"box": {
					"id": "obj-118",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 2,
					"patching_rect": [
						1010,
						690,
						65,
						22
					],
					"text": "buffer~ ---mslp14",
					"outlettype": [
						"float",
						"bang"
					]
				}
			},
			{
				"box": {
					"id": "obj-119",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 1,
					"patching_rect": [
						1010,
						720,
						65,
						22
					],
					"text": "index~ ---mslp14",
					"outlettype": [
						"signal"
					]
				}
			},
			{
				"box": {
					"id": "obj-120",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 0,
					"patching_rect": [
						1010,
						750,
						65,
						22
					],
					"text": "live.remote~"
				}
			},
			{
				"box": {
					"id": "obj-121",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 2,
					"patching_rect": [
						1080,
						690,
						65,
						22
					],
					"text": "buffer~ ---mslp15",
					"outlettype": [
						"float",
						"bang"
					]
				}
			},
			{
				"box": {
					"id": "obj-122",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 1,
					"patching_rect": [
						1080,
						720,
						65,
						22
					],
					"text": "index~ ---mslp15",
					"outlettype": [
						"signal"
					]
				}
			},
			{
				"box": {
					"id": "obj-123",
					"maxclass": "newobj",
					"numinlets": 2,
					"numoutlets": 0,
					"patching_rect": [
						1080,
						750,
						65,
						22
					],
					"text": "live.remote~"
				}
			},
			{
				"box": {
					"id": "obj-124",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						650,
						450,
						177,
						22
					],
					"text": "loadmess prefix ---mslp",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-125",
					"maxclass": "newobj",
					"numinlets": 0,
					"numoutlets": 1,
					"patching_rect": [
						650,
						480,
						142,
						22
					],
					"text": "receive msl_params",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-126",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						650,
						505,
						72,
						22
					],
					"text": "deferlow",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-127",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						650,
						530,
						142,
						22
					],
					"text": "prepend msl_params",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-128",
					"maxclass": "comment",
					"numinlets": 1,
					"numoutlets": 0,
					"patching_rect": [
						650,
						30,
						150,
						20
					],
					"text": "MuseScore Link",
					"presentation": 1,
					"presentation_rect": [
						6,
						4,
						150,
						20
					],
					"fontsize": 12.0,
					"fontface": 1
				}
			},
			{
				"box": {
					"id": "obj-129",
					"maxclass": "live.numbox",
					"numinlets": 1,
					"numoutlets": 2,
					"patching_rect": [
						650,
						60,
						50,
						15
					],
					"outlettype": [
						"",
						"float"
					],
					"presentation": 1,
					"presentation_rect": [
						160,
						6,
						50,
						15
					],
					"varname": "Port",
					"parameter_enable": 1,
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Port",
							"parameter_shortname": "Port",
							"parameter_type": 0,
							"parameter_mmin": 1024,
							"parameter_mmax": 65000,
							"parameter_initial_enable": 1,
							"parameter_initial": [
								9001
							],
							"parameter_unitstyle": 0,
							"parameter_invisible": 1
						}
					}
				}
			},
			{
				"box": {
					"id": "obj-130",
					"maxclass": "comment",
					"numinlets": 1,
					"numoutlets": 0,
					"patching_rect": [
						705,
						60,
						60,
						18
					],
					"text": "UDP port",
					"presentation": 1,
					"presentation_rect": [
						212,
						5,
						60,
						18
					]
				}
			},
			{
				"box": {
					"id": "obj-131",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						650,
						90,
						100,
						22
					],
					"text": "prepend port",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-132",
					"maxclass": "live.text",
					"numinlets": 1,
					"numoutlets": 2,
					"patching_rect": [
						650,
						120,
						60,
						18
					],
					"text": "Resync",
					"outlettype": [
						"",
						""
					],
					"presentation": 1,
					"presentation_rect": [
						270,
						5,
						55,
						18
					],
					"mode": 0,
					"texton": "Resync",
					"varname": "Resync",
					"parameter_enable": 1,
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Resync",
							"parameter_shortname": "Resync",
							"parameter_type": 2,
							"parameter_enum": [
								"off",
								"on"
							],
							"parameter_mmax": 1,
							"parameter_invisible": 2
						}
					}
				}
			},
			{
				"box": {
					"id": "obj-133",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						650,
						150,
						72,
						22
					],
					"text": "t resync",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-134",
					"maxclass": "comment",
					"numinlets": 1,
					"numoutlets": 0,
					"patching_rect": [
						650,
						200,
						320,
						40
					],
					"text": "MuseScore Link: loading",
					"presentation": 1,
					"presentation_rect": [
						6,
						26,
						320,
						40
					],
					"linecount": 3
				}
			},
			{
				"box": {
					"id": "obj-135",
					"maxclass": "comment",
					"numinlets": 1,
					"numoutlets": 0,
					"patching_rect": [
						650,
						250,
						320,
						30
					],
					"text": "MuseScore owns its \"MuseScore:\" clips: edits to them here are overwritten. Keep this device before the instrument.",
					"presentation": 1,
					"presentation_rect": [
						6,
						68,
						320,
						28
					],
					"linecount": 2,
					"fontsize": 9.0,
					"textcolor": [
						0.6,
						0.6,
						0.6,
						1.0
					]
				}
			},
			{
				"box": {
					"id": "obj-136",
					"maxclass": "live.text",
					"numinlets": 1,
					"numoutlets": 2,
					"patching_rect": [
						650,
						290,
						110,
						18
					],
					"text": "Edit in MuseScore",
					"outlettype": [
						"",
						""
					],
					"presentation": 1,
					"presentation_rect": [
						6,
						100,
						110,
						18
					],
					"mode": 0,
					"texton": "Edit in MuseScore",
					"varname": "Edit in MuseScore",
					"parameter_enable": 1,
					"saved_attribute_attributes": {
						"valueof": {
							"parameter_longname": "Edit in MuseScore",
							"parameter_shortname": "Edit",
							"parameter_type": 2,
							"parameter_enum": [
								"off",
								"on"
							],
							"parameter_mmax": 1,
							"parameter_invisible": 2
						}
					}
				}
			},
			{
				"box": {
					"id": "obj-137",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 1,
					"patching_rect": [
						650,
						320,
						58,
						22
					],
					"text": "t edit",
					"outlettype": [
						""
					]
				}
			},
			{
				"box": {
					"id": "obj-138",
					"maxclass": "comment",
					"numinlets": 1,
					"numoutlets": 0,
					"patching_rect": [
						770,
						290,
						200,
						30
					],
					"text": "the MIDI clip shown in Live's Clip View, as notation; edits go back to it",
					"presentation": 1,
					"presentation_rect": [
						120,
						99,
						205,
						28
					],
					"linecount": 2,
					"fontsize": 9.0,
					"textcolor": [
						0.6,
						0.6,
						0.6,
						1.0
					]
				}
			}
		],
		"lines": [
			{
				"patchline": {
					"source": [
						"obj-1",
						0
					],
					"destination": [
						"obj-2",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-2",
						0
					],
					"destination": [
						"obj-3",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-3",
						0
					],
					"destination": [
						"obj-7",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-7",
						1
					],
					"destination": [
						"obj-8",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-8",
						0
					],
					"destination": [
						"obj-9",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-9",
						0
					],
					"destination": [
						"obj-5",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-3",
						1
					],
					"destination": [
						"obj-10",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-10",
						1
					],
					"destination": [
						"obj-11",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-11",
						0
					],
					"destination": [
						"obj-12",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-12",
						0
					],
					"destination": [
						"obj-5",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-3",
						2
					],
					"destination": [
						"obj-13",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-13",
						1
					],
					"destination": [
						"obj-14",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-14",
						0
					],
					"destination": [
						"obj-15",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-15",
						0
					],
					"destination": [
						"obj-5",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-3",
						3
					],
					"destination": [
						"obj-16",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-16",
						1
					],
					"destination": [
						"obj-17",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-17",
						0
					],
					"destination": [
						"obj-18",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-18",
						0
					],
					"destination": [
						"obj-5",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-3",
						4
					],
					"destination": [
						"obj-19",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-19",
						1
					],
					"destination": [
						"obj-20",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-20",
						0
					],
					"destination": [
						"obj-21",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-21",
						0
					],
					"destination": [
						"obj-5",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-3",
						5
					],
					"destination": [
						"obj-22",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-22",
						1
					],
					"destination": [
						"obj-23",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-23",
						0
					],
					"destination": [
						"obj-24",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-24",
						0
					],
					"destination": [
						"obj-5",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-3",
						6
					],
					"destination": [
						"obj-25",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-25",
						1
					],
					"destination": [
						"obj-26",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-26",
						0
					],
					"destination": [
						"obj-27",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-27",
						0
					],
					"destination": [
						"obj-5",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-3",
						7
					],
					"destination": [
						"obj-28",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-28",
						1
					],
					"destination": [
						"obj-29",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-29",
						0
					],
					"destination": [
						"obj-30",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-30",
						0
					],
					"destination": [
						"obj-5",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-3",
						8
					],
					"destination": [
						"obj-31",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-31",
						1
					],
					"destination": [
						"obj-32",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-32",
						0
					],
					"destination": [
						"obj-33",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-33",
						0
					],
					"destination": [
						"obj-5",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-3",
						9
					],
					"destination": [
						"obj-34",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-34",
						1
					],
					"destination": [
						"obj-35",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-35",
						0
					],
					"destination": [
						"obj-36",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-36",
						0
					],
					"destination": [
						"obj-5",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-3",
						10
					],
					"destination": [
						"obj-37",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-37",
						1
					],
					"destination": [
						"obj-38",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-38",
						0
					],
					"destination": [
						"obj-39",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-39",
						0
					],
					"destination": [
						"obj-5",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-3",
						11
					],
					"destination": [
						"obj-40",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-40",
						1
					],
					"destination": [
						"obj-41",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-41",
						0
					],
					"destination": [
						"obj-42",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-42",
						0
					],
					"destination": [
						"obj-5",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-3",
						12
					],
					"destination": [
						"obj-44",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-44",
						1
					],
					"destination": [
						"obj-45",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-45",
						0
					],
					"destination": [
						"obj-46",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-46",
						1
					],
					"destination": [
						"obj-43",
						2
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-46",
						0
					],
					"destination": [
						"obj-43",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-3",
						13
					],
					"destination": [
						"obj-47",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-47",
						1
					],
					"destination": [
						"obj-48",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-48",
						0
					],
					"destination": [
						"obj-49",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-49",
						1
					],
					"destination": [
						"obj-43",
						1
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-49",
						0
					],
					"destination": [
						"obj-43",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-43",
						0
					],
					"destination": [
						"obj-5",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-3",
						14
					],
					"destination": [
						"obj-4",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-2",
						1
					],
					"destination": [
						"obj-4",
						1
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-2",
						2
					],
					"destination": [
						"obj-4",
						2
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-2",
						3
					],
					"destination": [
						"obj-4",
						3
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-2",
						4
					],
					"destination": [
						"obj-4",
						4
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-2",
						5
					],
					"destination": [
						"obj-4",
						5
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-2",
						6
					],
					"destination": [
						"obj-4",
						6
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-4",
						0
					],
					"destination": [
						"obj-6",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-5",
						0
					],
					"destination": [
						"obj-6",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-50",
						0
					],
					"destination": [
						"obj-52",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-50",
						1
					],
					"destination": [
						"obj-51",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-52",
						1
					],
					"destination": [
						"obj-53",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-53",
						0
					],
					"destination": [
						"obj-55",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-55",
						0
					],
					"destination": [
						"obj-56",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-56",
						0
					],
					"destination": [
						"obj-57",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-52",
						0
					],
					"destination": [
						"obj-54",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-54",
						1
					],
					"destination": [
						"obj-57",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-58",
						0
					],
					"destination": [
						"obj-5",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-60",
						6
					],
					"destination": [
						"obj-61",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-61",
						0
					],
					"destination": [
						"obj-62",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-62",
						0
					],
					"destination": [
						"obj-63",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-63",
						0
					],
					"destination": [
						"obj-60",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-61",
						1
					],
					"destination": [
						"obj-64",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-64",
						0
					],
					"destination": [
						"obj-65",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-65",
						0
					],
					"destination": [
						"obj-60",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-61",
						2
					],
					"destination": [
						"obj-66",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-66",
						0
					],
					"destination": [
						"obj-67",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-67",
						0
					],
					"destination": [
						"obj-60",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-61",
						3
					],
					"destination": [
						"obj-68",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-68",
						0
					],
					"destination": [
						"obj-69",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-69",
						0
					],
					"destination": [
						"obj-60",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-60",
						7
					],
					"destination": [
						"obj-58",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-51",
						0
					],
					"destination": [
						"obj-60",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-59",
						0
					],
					"destination": [
						"obj-60",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-60",
						0
					],
					"destination": [
						"obj-70",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-60",
						1
					],
					"destination": [
						"obj-70",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-71",
						0
					],
					"destination": [
						"obj-72",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-60",
						4
					],
					"destination": [
						"obj-72",
						1
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-72",
						0
					],
					"destination": [
						"obj-73",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-60",
						5
					],
					"destination": [
						"obj-73",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-73",
						0
					],
					"destination": [
						"obj-74",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-74",
						0
					],
					"destination": [
						"obj-60",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-60",
						3
					],
					"destination": [
						"obj-75",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-72",
						0
					],
					"destination": [
						"obj-77",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-77",
						0
					],
					"destination": [
						"obj-78",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-75",
						0
					],
					"destination": [
						"obj-78",
						1
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-72",
						0
					],
					"destination": [
						"obj-80",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-80",
						0
					],
					"destination": [
						"obj-81",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-75",
						1
					],
					"destination": [
						"obj-81",
						1
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-72",
						0
					],
					"destination": [
						"obj-83",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-83",
						0
					],
					"destination": [
						"obj-84",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-75",
						2
					],
					"destination": [
						"obj-84",
						1
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-72",
						0
					],
					"destination": [
						"obj-86",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-86",
						0
					],
					"destination": [
						"obj-87",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-75",
						3
					],
					"destination": [
						"obj-87",
						1
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-72",
						0
					],
					"destination": [
						"obj-89",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-89",
						0
					],
					"destination": [
						"obj-90",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-75",
						4
					],
					"destination": [
						"obj-90",
						1
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-72",
						0
					],
					"destination": [
						"obj-92",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-92",
						0
					],
					"destination": [
						"obj-93",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-75",
						5
					],
					"destination": [
						"obj-93",
						1
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-72",
						0
					],
					"destination": [
						"obj-95",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-95",
						0
					],
					"destination": [
						"obj-96",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-75",
						6
					],
					"destination": [
						"obj-96",
						1
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-72",
						0
					],
					"destination": [
						"obj-98",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-98",
						0
					],
					"destination": [
						"obj-99",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-75",
						7
					],
					"destination": [
						"obj-99",
						1
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-72",
						0
					],
					"destination": [
						"obj-101",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-101",
						0
					],
					"destination": [
						"obj-102",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-75",
						8
					],
					"destination": [
						"obj-102",
						1
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-72",
						0
					],
					"destination": [
						"obj-104",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-104",
						0
					],
					"destination": [
						"obj-105",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-75",
						9
					],
					"destination": [
						"obj-105",
						1
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-72",
						0
					],
					"destination": [
						"obj-107",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-107",
						0
					],
					"destination": [
						"obj-108",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-75",
						10
					],
					"destination": [
						"obj-108",
						1
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-72",
						0
					],
					"destination": [
						"obj-110",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-110",
						0
					],
					"destination": [
						"obj-111",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-75",
						11
					],
					"destination": [
						"obj-111",
						1
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-72",
						0
					],
					"destination": [
						"obj-113",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-113",
						0
					],
					"destination": [
						"obj-114",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-75",
						12
					],
					"destination": [
						"obj-114",
						1
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-72",
						0
					],
					"destination": [
						"obj-116",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-116",
						0
					],
					"destination": [
						"obj-117",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-75",
						13
					],
					"destination": [
						"obj-117",
						1
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-72",
						0
					],
					"destination": [
						"obj-119",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-119",
						0
					],
					"destination": [
						"obj-120",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-75",
						14
					],
					"destination": [
						"obj-120",
						1
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-72",
						0
					],
					"destination": [
						"obj-122",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-122",
						0
					],
					"destination": [
						"obj-123",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-75",
						15
					],
					"destination": [
						"obj-123",
						1
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-124",
						0
					],
					"destination": [
						"obj-60",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-125",
						0
					],
					"destination": [
						"obj-126",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-126",
						0
					],
					"destination": [
						"obj-127",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-127",
						0
					],
					"destination": [
						"obj-60",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-129",
						0
					],
					"destination": [
						"obj-131",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-131",
						0
					],
					"destination": [
						"obj-60",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-132",
						0
					],
					"destination": [
						"obj-133",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-133",
						0
					],
					"destination": [
						"obj-60",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-60",
						2
					],
					"destination": [
						"obj-134",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-136",
						0
					],
					"destination": [
						"obj-137",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-137",
						0
					],
					"destination": [
						"obj-60",
						0
					]
				}
			}
		],
		"dependency_cache": [],
		"autosave": 0
	}
}
