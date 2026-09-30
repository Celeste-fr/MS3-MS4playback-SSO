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
					"id": "obj-51",
					"maxclass": "newobj",
					"numinlets": 1,
					"numoutlets": 3,
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
						""
					],
					"saved_object_attributes": {
						"parameter_enable": 0
					},
					"textfile": {
						"text": "// MuseScore Link: the Max for Live device for \"Live plays the score\" (LIVE.md; MuseScore's side:\n// libmscore/liveclips.h, mscore/liveclips.h).\n//\n// One copy goes on each MIDI track that plays a library part, BEFORE the instrument (Kontakt). In\n// every copy the patcher (not this script) turns the carrier notes of MuseScore's clips (keys 114-127: controllers, pitch bend)\n// into their MIDI controllers, in Max's scheduler, sample-timed with the notes; this script is not in\n// that path. One copy (the first loaded; another takes over when it goes) is the hub:\n//   - it listens to MuseScore (OSC over UDP on localhost, port 9001 by default; answers on port + 1);\n//   - it writes each part's clip on its track: found by MIDI From = the part's MuseScore port and\n//     channel, else (a part's main patch) by track name = part name; one arrangement clip from beat\n//     0 over the whole score, named \"MuseScore: <part>\", its notes replaced at each change;\n//   - it never touches a clip it didn't make (another name), and doesn't make one over a track's\n//     other clips;\n//   - it sets Live's tempo to the score's first and puts a locator (\"MS 12\") at each played bar,\n//     only while Live is stopped, and only its own locators;\n//   - it reports Live's transport (~25 a second while playing) so MuseScore follows, and starts or\n//     stops Live when MuseScore's Play or Stop is pressed;\n//   - \"/ms/mode stream\": MuseScore plays through Live (the tracks' Monitor on In, which silences\n//     clips); \"clips\": Live plays the clips (Monitor on Auto).\n//   - \"Edit in MuseScore\" (the button, on any copy): the clip in Live's Detail View (any MIDI clip, not\n//     only MuseScore's) is read with every field and sent to MuseScore, which opens it as a score; the\n//     hub then applies MuseScore's edits to it by note id (remove_notes_by_id, apply_note_modifications\n//     with Live's own note data and only the edited fields changed, add_new_notes), checks the clip once\n//     a second and reports a change made in Live as a conflict (nothing more is written until MuseScore\n//     reads it again). LIVE.md \u203a Editing Live clips in MuseScore; mscore/liveclipmodel.h.\n// The Live Object Model is used from Max's low-priority thread only (messages from udpreceive go\n// through deferlow; the Tasks run there).\n//\n// Plain ECMAScript 5 so it runs in [js] and [v8] alike, and in the Node tests (tools/live/test).\n\nautowatch = 0;\ninlets = 1;\noutlets = 3;      // 0: OSC to MuseScore (udpsend), 1: udpsend's host / port, 2: status text\n\nvar PROTOCOL = 2;                       // 2: editing Live clips\nvar UNITS = 3840;                       // LiveClips::UNITS_PER_BEAT\nvar BATCH = 500;                        // notes per add_new_notes call\nvar HUB_STALE_MS = 5000;\n\nvar self = this;\n// shared by every copy of the device: which is the hub, and where each copy sits (as JSON: a\n// Global's values are safest as strings)\nvar g = new Global(\"musescore_link\");\nfunction registry() {\n      try { return JSON.parse(g.devices || \"{}\"); } catch (e) { return {}; }\n      }\nfunction saveRegistry(r) { g.devices = JSON.stringify(r); }\n\nvar me = { key: \"d\" + Math.floor(Math.random() * 1e9), track: 0, device: 0 };\nvar udpPort = 9001;\nvar isHub = false;\nvar session = \"\";\nvar receiver = null;          // hub: the udpreceive and deferlow it made\nvar deferrer = null;\nvar mode = \"clips\";\nvar pending = {};             // key -> a clip being received\nvar pendingSong = null;\nvar work = [];                // clips and the song to write, in order\nvar placed = {};              // key -> { track: id, clip: name } where the hub put it\nvar lastTransport = { playing: -1, beat: -1, sent: 0 };\nvar edits = {};               // key -> a clip edited in MuseScore\nvar editSerial = 0;\nvar lastEditRequest = \"\";\nvar heartbeat = null;\nvar worker = null;\nvar reporter = null;\nvar initialised = false;\n\nfunction now() { return new Date().getTime(); }\nfunction num(v) { return Number(Array.isArray(v) ? v[0] : v); }\nfunction str(v) {\n      if (Array.isArray(v))\n            return v.join(\" \");\n      return v === undefined || v === null ? \"\" : String(v);\n      }\nfunction loose(s) { return str(s).toLowerCase().replace(/[^a-z0-9]/g, \"\"); }\nfunction loosePort(s) { return loose(str(s).replace(/^\\s*ext:\\s*/i, \"\")); }\n\n// a LOM dictionary property (input_routing_type \u2026): a JSON string, sometimes in an array\nfunction displayName(v) {\n      var s = str(v);\n      try {\n            var o = JSON.parse(s);\n            var find = function(x) {\n                  if (!x || typeof x !== \"object\")\n                        return null;\n                  if (typeof x.display_name === \"string\")\n                        return x.display_name;\n                  for (var k in x) {\n                        var r = find(x[k]);\n                        if (r !== null)\n                              return r;\n                        }\n                  return null;\n                  };\n            var r = find(o);\n            if (r !== null)\n                  return r;\n            }\n      catch (e) {}\n      var m = /\"display_name\"\\s*:\\s*\"([^\"]*)\"/.exec(s);\n      return m ? m[1] : s;\n      }\n\n// [\"id\", 3, \"id\", 7] -> [3, 7]\nfunction ids(v) {\n      var out = [];\n      if (!Array.isArray(v))\n            v = str(v).split(\" \");\n      for (var i = 0; i + 1 < v.length; i += 2)\n            if (str(v[i]) === \"id\" && num(v[i + 1]) > 0)\n                  out.push(num(v[i + 1]));\n      return out;\n      }\n\nfunction status(text) {\n      outlet(2, \"set\", text);\n      }\n\nfunction send() {\n      var a = Array.prototype.slice.call(arguments);\n      outlet.apply(this, [0].concat(a));\n      }\n\n//---------------------------------------------------------\n//   the device's life\n//---------------------------------------------------------\n\n// live.thisdevice: the Live API is ready\nfunction bang() {\n      if (initialised)\n            return;\n      initialised = true;\n      var dev = new LiveAPI(\"this_device\");\n      me.device = num(dev.id);\n      var tr = new LiveAPI(\"this_device canonical_parent\");\n      me.track = num(tr.id);\n      var r = registry();\n      r[me.key] = { track: me.track, device: me.device, beat: now() };\n      saveRegistry(r);\n      heartbeat = new Task(beat, this);\n      heartbeat.interval = 1000;\n      heartbeat.repeat();\n      elect();\n      if (!isHub)\n            status(\"MuseScore Link: on this track (the hub is another copy)\");\n      }\n\nfunction beat() {\n      var r = registry();\n      if (r[me.key]) {\n            r[me.key].beat = now();\n            saveRegistry(r);\n            }\n      if (isHub) {\n            g.hubBeat = now();\n            if (Math.floor(now() / 1000) % 2 === 0)\n                  send(\"/live/hello\", session, PROTOCOL);\n            checkEdits();\n            }\n      else\n            elect();\n      }\n\nfunction elect() {\n      if (isHub)\n            return;\n      if (!g.hub || !g.hubBeat || now() - g.hubBeat > HUB_STALE_MS || !registry()[g.hub])\n            becomeHub();\n      }\n\nfunction becomeHub() {\n      g.hub = me.key;\n      g.hubBeat = now();\n      isHub = true;\n      session = \"s\" + Math.floor(Math.random() * 1e9);\n      openPort();\n      worker = new Task(workStep, this);\n      worker.interval = 20;\n      worker.repeat();\n      reporter = new Task(report, this);\n      reporter.interval = 40;\n      reporter.repeat();\n      send(\"/live/hello\", session, PROTOCOL);\n      status(\"MuseScore Link: hub, listening on UDP \" + udpPort + \", waiting for MuseScore\");\n      }\n\n// the hub alone listens: its udpreceive is made here (every copy binding the port would clash)\nfunction openPort() {\n      var p = self.patcher;\n      if (!p)\n            return;\n      if (receiver)\n            p.remove(receiver);\n      if (!deferrer) {\n            deferrer = p.newdefault(20, 600, \"deferlow\");\n            p.connect(deferrer, 0, self.box, 0);\n            }\n      receiver = p.newdefault(20, 570, \"udpreceive\", udpPort);\n      p.connect(receiver, 0, deferrer, 0);\n      outlet(1, \"host\", \"127.0.0.1\");\n      outlet(1, \"port\", udpPort + 1);\n      }\n\n// the Port box (\"port 9001\")\nfunction setPort(n) {\n      n = Math.floor(num(n));\n      if (!(n > 1023 && n < 65535) || n === udpPort)\n            return;\n      udpPort = n;\n      if (isHub)\n            openPort();\n      }\n\nfunction resync() {\n      if (isHub)\n            send(\"/live/resync\");\n      }\n\nfunction notifydeleted() {\n      var r = registry();\n      delete r[me.key];\n      saveRegistry(r);\n      if (isHub) {\n            g.hub = null;\n            g.hubBeat = 0;\n            }\n      if (heartbeat) heartbeat.cancel();\n      if (worker) worker.cancel();\n      if (reporter) reporter.cancel();\n      }\n\n//---------------------------------------------------------\n//   MuseScore's messages\n//---------------------------------------------------------\n\n// the \"Edit in MuseScore\" button (any copy: the hub does the work)\nfunction edit() {\n      if (isHub)\n            work.push({ kind: \"edit\" });\n      else {\n            g.editRequest = me.key + \":\" + now();\n            status(\"MuseScore Link: asked the hub to send the clip to MuseScore\");\n            }\n      }\n\nfunction anything() {\n      var a = arrayfromargs(arguments);\n      if (messagename === \"port\")\n            return setPort(a[0]);\n      if (messagename === \"edit\")\n            return edit();\n      if (!isHub)\n            return;\n      handle(messagename, a);\n      }\n\nfunction handle(address, a) {\n      if (address === \"/ms/mode\") {\n            mode = str(a[0]) === \"stream\" ? \"stream\" : \"clips\";\n            work.push({ kind: \"mode\" });\n            }\n      else if (address === \"/ms/song\") {\n            pendingSong = { gen: num(a[0]), bpm: num(a[1]), length: num(a[2]), count: num(a[3]), chunks: num(a[4]),\n                            hash: num(a[5]), cues: [], got: 0 };\n            if (pendingSong.chunks === 0)\n                  queueSong();\n            }\n      else if (address === \"/ms/cues\") {\n            if (!pendingSong || pendingSong.gen !== num(a[0]))\n                  return;\n            for (var i = 2; i + 1 < a.length; i += 2)\n                  pendingSong.cues.push({ time: num(a[i]) / UNITS, name: str(a[i + 1]) });\n            if (++pendingSong.got === pendingSong.chunks)\n                  queueSong();\n            }\n      else if (address === \"/ms/track\") {\n            var t = { gen: num(a[0]), key: str(a[1]), port: str(a[2]), channel: num(a[3]), part: str(a[4]), clip: str(a[5]),\n                      main: num(a[6]) !== 0, length: num(a[7]) / UNITS, count: num(a[8]), chunks: num(a[9]), hash: num(a[10]),\n                      notes: [], got: 0 };\n            pending[t.key] = t;\n            if (t.chunks === 0)\n                  queueClip(t);\n            }\n      else if (address === \"/ms/notes\") {\n            var p = pending[str(a[1])];\n            if (!p || p.gen !== num(a[0]))\n                  return;\n            for (var k = 3; k + 4 < a.length; k += 5)\n                  p.notes.push({ pitch: num(a[k]), start_time: num(a[k + 1]) / UNITS, duration: num(a[k + 2]) / UNITS,\n                                 velocity: num(a[k + 3]), mute: num(a[k + 4]) ? 1 : 0 });\n            if (++p.got === p.chunks)\n                  queueClip(p);\n            }\n      else if (address === \"/ms/clear\")\n            work.push({ kind: \"clear\", key: str(a[1]), port: str(a[2]), channel: num(a[3]), part: str(a[4]), clip: str(a[5]),\n                        main: true });\n      else if (address === \"/ms/play\") {\n            var song = new LiveAPI(\"live_set\");\n            song.set(\"current_song_time\", Math.max(0, num(a[0])));\n            if (!num(song.get(\"is_playing\")))\n                  song.call(\"continue_playing\");\n            }\n      else if (address === \"/ms/stop\")\n            new LiveAPI(\"live_set\").call(\"stop_playing\");\n      else if (address === \"/ms/clip/edit\")\n            work.push({ kind: \"edit\" });\n      else if (address === \"/ms/clip/write\") {\n            var e = edits[str(a[0])];\n            if (!e)\n                  return send(\"/live/clip/written\", str(a[0]), num(a[1]), \"gone\", 0);\n            if (num(a[1]) === e.lastWrite && e.reply) {           // sent again: applied once, answered again\n                  send.apply(this, e.reply);\n                  return;\n                  }\n            e.incoming = { write: num(a[1]), count: num(a[2]), chunks: num(a[3]), got: 0, ops: [] };\n            if (e.incoming.chunks === 0)\n                  queueWrite(e);\n            }\n      else if (address === \"/ms/clip/ops\") {\n            var ed = edits[str(a[0])];\n            if (!ed || !ed.incoming || ed.incoming.write !== num(a[1]))\n                  return;\n            for (var j = 3; j + 7 < a.length; j += 8)\n                  ed.incoming.ops.push({ op: num(a[j]), id: num(a[j + 1]), mask: num(a[j + 2]), pitch: num(a[j + 3]),\n                                         start: num(a[j + 4]), duration: num(a[j + 5]), velocity: num(a[j + 6]),\n                                         mute: num(a[j + 7]) });\n            if (++ed.incoming.got === ed.incoming.chunks)\n                  queueWrite(ed);\n            }\n      else if (address === \"/ms/clip/reload\") {\n            if (edits[str(a[0])])\n                  work.push({ kind: \"reload\", key: str(a[0]) });\n            }\n      else if (address === \"/ms/clip/close\")\n            delete edits[str(a[0])];\n      }\n\nfunction queueWrite(e) {\n      var w = e.incoming;\n      e.incoming = null;\n      work.push({ kind: \"write\", key: e.key, write: w });\n      }\n\nfunction queueClip(t) {\n      delete pending[t.key];\n      // a newer version of the same clip replaces one still waiting\n      for (var i = 0; i < work.length; ++i)\n            if (work[i].kind === \"clip\" && work[i].key === t.key) {\n                  work[i] = { kind: \"clip\", key: t.key, clip: t };\n                  return;\n                  }\n      work.push({ kind: \"clip\", key: t.key, clip: t });\n      }\n\nfunction queueSong() {\n      var s = pendingSong;\n      pendingSong = null;\n      for (var i = 0; i < work.length; ++i)\n            if (work[i].kind === \"song\") {\n                  work[i] = { kind: \"song\", song: s };\n                  return;\n                  }\n      work.unshift({ kind: \"song\", song: s });\n      }\n\n// one piece of work a turn (the Live API is slow: UDP keeps flowing between)\nfunction workStep() {\n      if (g.editRequest && g.editRequest !== lastEditRequest) {     // (a button on another copy)\n            lastEditRequest = g.editRequest;\n            if (isHub)\n                  work.push({ kind: \"edit\" });\n            }\n      if (!work.length)\n            return;\n      var w = work.shift();\n      try {\n            if (w.kind === \"clip\")\n                  writeClip(w.clip);\n            else if (w.kind === \"clear\")\n                  clearClip(w);\n            else if (w.kind === \"song\") {\n                  if (!writeSong(w.song))\n                        work.push(w);           // (Live is playing: the locators wait)\n                  }\n            else if (w.kind === \"mode\")\n                  applyMode();\n            else if (w.kind === \"edit\")\n                  startEdit();\n            else if (w.kind === \"reload\")\n                  sendClip(edits[w.key]);\n            else if (w.kind === \"write\")\n                  applyWrite(edits[w.key], w.write);\n            }\n      catch (e) {\n            if (w.kind === \"clip\")\n                  send(\"/live/applied\", w.clip.key, w.clip.hash, \"error: \" + e, \"\");\n            else if (w.kind === \"write\")\n                  send(\"/live/clip/written\", w.key, w.write.write, \"error: \" + e, 0);\n            post(\"MuseScore Link: \" + e + \"\\n\");\n            }\n      }\n\n//---------------------------------------------------------\n//   tracks and clips\n//---------------------------------------------------------\n\nfunction tracks() {\n      var song = new LiveAPI(\"live_set\");\n      var n = song.getcount(\"tracks\");\n      var out = [];\n      for (var i = 0; i < n; ++i)\n            out.push(new LiveAPI(\"live_set tracks \" + i));\n      return out;\n      }\n\n// the track that plays a part: MIDI From = its port and channel, else (main patch) its name\nfunction findTrack(t) {\n      var all = tracks();\n      var i;\n      for (i = 0; i < all.length; ++i) {\n            var tr = all[i];\n            if (num(tr.get(\"has_midi_input\")) !== 1)\n                  continue;\n            if (!t.port)\n                  break;\n            var type = displayName(tr.get(\"input_routing_type\"));\n            var ch = displayName(tr.get(\"input_routing_channel\"));\n            var lt = loosePort(type), lp = loosePort(t.port);\n            if (lt && lp && (lt === lp || lt.indexOf(lp) === 0) && loose(ch) === loose(\"Ch. \" + t.channel))\n                  return { api: tr, how: \"MIDI input\" };\n            }\n      // by name: a main patch's track is named after the part; another patch's (an extra, such as the\n      // Performance legato, or a copy for another tuning) after its clip, \"<part> \u2013 <patch>\", or after the\n      // patch alone when only one track has that name\n      var names = [];\n      if (t.main)\n            names.push(loose(t.part));\n      else {\n            var full = str(t.clip).replace(/^MuseScore:\\s*/, \"\");\n            names.push(loose(full));\n            }\n      var midi = [];\n      for (i = 0; i < all.length; ++i)\n            if (num(all[i].get(\"has_midi_input\")) === 1)\n                  midi.push(all[i]);\n      for (i = 0; i < midi.length; ++i)\n            if (names.indexOf(loose(midi[i].get(\"name\"))) >= 0)\n                  return { api: midi[i], how: \"name\" };\n      if (!t.main) {\n            var dash = str(t.clip).indexOf(\" \u2013 \");\n            var patch = dash >= 0 ? loose(str(t.clip).substring(dash + 3)) : \"\";\n            var hits = [];\n            for (i = 0; patch && i < midi.length; ++i)\n                  if (loose(midi[i].get(\"name\")) === patch)\n                        hits.push(midi[i]);\n            if (hits.length === 1)\n                  return { api: hits[0], how: \"patch name\" };\n            }\n      return null;\n      }\n\nfunction clipsOf(tr) {\n      var out = [];\n      var l = ids(tr.get(\"arrangement_clips\"));\n      for (var i = 0; i < l.length; ++i) {\n            var c = new LiveAPI(\"id \" + l[i]);\n            out.push({ id: l[i], api: c, name: str(c.get(\"name\")), start: num(c.get(\"start_time\")), end: num(c.get(\"end_time\")) });\n            }\n      return out;\n      }\n\n// is a MuseScore Link device on the track, before the instrument?\nfunction deviceCheck(tr) {\n      var mine = [];\n      var r = registry();\n      for (var k in r)\n            if (r[k].track === num(tr.id) && now() - r[k].beat < HUB_STALE_MS)\n                  mine.push(r[k].device);\n      if (!mine.length)\n            return \"no MuseScore Link device on the track (its controllers would reach the instrument as notes)\";\n      var devs = ids(tr.get(\"devices\"));\n      var link = -1, instrument = -1;\n      for (var i = 0; i < devs.length; ++i) {\n            if (mine.indexOf(devs[i]) >= 0 && link < 0)\n                  link = i;\n            if (instrument < 0 && str(new LiveAPI(\"id \" + devs[i]).get(\"class_name\")) === \"PluginDevice\")\n                  instrument = i;\n            }\n      if (link >= 0 && instrument >= 0 && link > instrument)\n            return \"the MuseScore Link device is after the instrument: move it before\";\n      return \"ok\";\n      }\n\nfunction setMonitor(tr) {\n      try {\n            tr.set(\"current_monitoring_state\", mode === \"clips\" ? 1 : 0);     // 1: Auto, 0: In\n            }\n      catch (e) {}\n      }\n\nfunction writeClip(t) {\n      var found = findTrack(t);\n      if (!found) {\n            send(\"/live/applied\", t.key, t.hash, \"no track (MIDI From \" + t.port + \" / Ch. \" + t.channel\n                 + \", or a track named \" + (t.main ? t.part : str(t.clip).replace(/^MuseScore:\\s*/, \"\")) + \")\", \"\");\n            return;\n            }\n      var tr = found.api;\n      var trackName = str(tr.get(\"name\"));\n      var clips = clipsOf(tr);\n      var ours = null, i;\n      for (i = 0; i < clips.length; ++i)\n            if (clips[i].name === t.clip)\n                  ours = clips[i];\n      var fits = ours && Math.abs(ours.start) < 1e-6 && Math.abs(ours.end - t.length) < 1e-6;\n      if (!fits) {\n            for (i = 0; i < clips.length; ++i)\n                  if (clips[i] !== ours && clips[i].start < t.length && clips[i].end > 0) {\n                        send(\"/live/applied\", t.key, t.hash, \"other clips on track \" + trackName + \" (the clip isn't made over them)\",\n                             trackName);\n                        return;\n                        }\n            if (ours)\n                  tr.call(\"delete_clip\", \"id\", ours.id);\n            var before = ids(tr.get(\"arrangement_clips\"));\n            tr.call(\"create_midi_clip\", 0, t.length);\n            var after = ids(tr.get(\"arrangement_clips\"));\n            var id = 0;\n            for (i = 0; i < after.length; ++i)\n                  if (before.indexOf(after[i]) < 0)\n                        id = after[i];\n            if (!id) {\n                  send(\"/live/applied\", t.key, t.hash, \"error: Live made no clip (a frozen track? Live 12.1.10 or later needed)\",\n                       trackName);\n                  return;\n                  }\n            ours = { id: id, api: new LiveAPI(\"id \" + id) };\n            ours.api.set(\"name\", t.clip);\n            }\n      var c = ours.api;\n      c.set(\"muted\", 0);\n      c.call(\"remove_notes_extended\", 0, 128, 0, t.length + 1);\n      for (i = 0; i < t.notes.length; i += BATCH)\n            c.call(\"add_new_notes\", { notes: t.notes.slice(i, i + BATCH) });\n      setMonitor(tr);\n      placed[t.key] = { track: num(tr.id), clip: t.clip };\n      send(\"/live/applied\", t.key, t.hash, deviceCheck(tr), trackName);\n      status(\"MuseScore Link: hub; last clip \" + t.clip + \" on \" + trackName);\n      }\n\nfunction clearClip(w) {\n      var found = findTrack(w);\n      if (!found)\n            return;\n      var clips = clipsOf(found.api);\n      for (var i = 0; i < clips.length; ++i)\n            if (clips[i].name === w.clip)\n                  found.api.call(\"delete_clip\", \"id\", clips[i].id);\n      delete placed[w.key];\n      }\n\nfunction applyMode() {\n      for (var k in placed) {\n            var tr = new LiveAPI(\"id \" + placed[k].track);\n            if (num(tr.id) > 0)\n                  setMonitor(tr);\n            }\n      }\n\n//---------------------------------------------------------\n//   tempo and locators\n//---------------------------------------------------------\n\nfunction writeSong(s) {\n      var song = new LiveAPI(\"live_set\");\n      if (Math.abs(num(song.get(\"tempo\")) - s.bpm) > 1e-4)\n            song.set(\"tempo\", s.bpm);\n      if (num(song.get(\"is_playing\")))\n            return false;\n      var back = num(song.get(\"current_song_time\"));\n      var cueList = function() {\n            var l = ids(song.get(\"cue_points\"));\n            var out = [];\n            for (var i = 0; i < l.length; ++i) {\n                  var c = new LiveAPI(\"id \" + l[i]);\n                  out.push({ id: l[i], api: c, time: num(c.get(\"time\")), name: str(c.get(\"name\")) });\n                  }\n            return out;\n            };\n      var managed = function(name) { return /^MS \\d/.test(name); };\n      var toggleAt = function(time) {\n            song.set(\"current_song_time\", time);\n            song.call(\"set_or_delete_cue\");\n            };\n      var want = {};\n      for (var i = 0; i < s.cues.length; ++i)\n            want[s.cues[i].time.toFixed(6)] = s.cues[i].name;\n      var have = cueList();\n      var at = {};\n      for (i = 0; i < have.length; ++i) {\n            var key = have[i].time.toFixed(6);\n            if (managed(have[i].name) && !(key in want))\n                  toggleAt(have[i].time);             // ours, not wanted: goes\n            else\n                  at[key] = have[i];\n            }\n      for (i = 0; i < s.cues.length; ++i) {\n            var k2 = s.cues[i].time.toFixed(6);\n            var there = at[k2];\n            if (there) {\n                  if (managed(there.name) && there.name !== s.cues[i].name)\n                        there.api.set(\"name\", s.cues[i].name);\n                  continue;                           // (a locator of the owner's there: left)\n                  }\n            toggleAt(s.cues[i].time);\n            var now2 = cueList();\n            for (var j = 0; j < now2.length; ++j)\n                  if (Math.abs(now2[j].time - s.cues[i].time) < 1e-6 && !managed(now2[j].name))\n                        now2[j].api.set(\"name\", s.cues[i].name);\n            }\n      song.set(\"current_song_time\", back);\n      send(\"/live/applied\", \"song\", s.hash, \"ok\", \"\");\n      return true;\n      }\n\n//---------------------------------------------------------\n//   Live's transport, for MuseScore to follow\n//---------------------------------------------------------\n\nfunction report() {\n      var song = new LiveAPI(\"live_set\");\n      var playing = num(song.get(\"is_playing\")) ? 1 : 0;\n      var b = num(song.get(\"current_song_time\"));\n      var t = now();\n      if (playing || playing !== lastTransport.playing || Math.abs(b - lastTransport.beat) > 1e-6 || t - lastTransport.sent > 1000) {\n            send(\"/live/transport\", playing, b, num(song.get(\"tempo\")));\n            lastTransport = { playing: playing, beat: b, sent: t };\n            }\n      }\n\n//---------------------------------------------------------\n//   editing a clip in MuseScore\n//---------------------------------------------------------\n\nvar FIELDS = [\"note_id\", \"pitch\", \"start_time\", \"duration\", \"velocity\", \"mute\", \"probability\", \"velocity_deviation\",\n              \"release_velocity\"];\nvar TICKS = 480;                        // MuseScore's ticks a beat (the edits' times)\nvar CLIP_NOTES_PER_PACKET = 24;         // LiveClipEdit::NOTES_PER_PACKET\n\n// a LOM call's dictionary: a JSON string, maybe in an array, or already an object\nfunction dict(v) {\n      if (v && typeof v === \"object\" && !Array.isArray(v))\n            return v;\n      var s = Array.isArray(v) ? v.join(\" \") : str(v);\n      try { return JSON.parse(s); } catch (e) { return null; }\n      }\n\nfunction readNotes(clip) {\n      var d = dict(clip.call(\"get_all_notes_extended\"));\n      return d && Array.isArray(d.notes) ? d.notes : [];\n      }\n\n// the clip's notes, whatever their order (FNV-1a over every field, by id)\nfunction hashNotes(notes) {\n      var l = notes.slice().sort(function(a, b) { return a.note_id - b.note_id; });\n      var h = 0x811c9dc5;\n      for (var i = 0; i < l.length; ++i) {\n            var s = \"\";\n            for (var k = 0; k < FIELDS.length; ++k)\n                  s += String(l[i][FIELDS[k]]) + \",\";\n            for (var c = 0; c < s.length; ++c) {\n                  h ^= s.charCodeAt(c);\n                  h = (h + ((h << 1) + (h << 4) + (h << 7) + (h << 8) + (h << 24))) >>> 0;\n                  }\n            }\n      return h | 0;\n      }\n\n// the clip shown in the Detail View (an arrangement or a session clip), else the highlighted session slot's\nfunction selectedClip() {\n      var c = new LiveAPI(\"live_set view detail_clip\");\n      if (num(c.id) > 0)\n            return c;\n      var slot = new LiveAPI(\"live_set view highlighted_clip_slot\");\n      if (num(slot.id) > 0 && num(slot.get(\"has_clip\"))) {\n            var l = ids(slot.get(\"clip\"));\n            if (l.length)\n                  return new LiveAPI(\"id \" + l[0]);\n            }\n      return null;\n      }\n\n// the clip's track (an arrangement clip's parent; a session clip's slot's parent)\nfunction trackOf(clip) {\n      var p = ids(clip.get(\"canonical_parent\"));\n      if (!p.length)\n            return null;\n      var o = new LiveAPI(\"id \" + p[0]);\n      if (str(o.type) === \"ClipSlot\") {\n            p = ids(o.get(\"canonical_parent\"));\n            o = p.length ? new LiveAPI(\"id \" + p[0]) : null;\n            }\n      return o;\n      }\n\n// a drum clip: a Drum Rack on its track, or a drum-like track name\nfunction isDrums(track, name) {\n      if (track) {\n            var devs = ids(track.get(\"devices\"));\n            for (var i = 0; i < devs.length; ++i)\n                  if (str(new LiveAPI(\"id \" + devs[i]).get(\"class_name\")) === \"DrumGroupDevice\")\n                        return true;\n            }\n      return /drum|kit|perc|beat/i.test(name);\n      }\n\nfunction startEdit() {\n      var clip = selectedClip();\n      if (!clip) {\n            status(\"MuseScore Link: select a MIDI clip first (its notes shown in the Clip View), then Edit in MuseScore\");\n            return;\n            }\n      if (!num(clip.get(\"is_midi_clip\"))) {\n            status(\"MuseScore Link: that is an audio clip; only MIDI clips can be edited in MuseScore\");\n            return;\n            }\n      var key = \"c\" + num(clip.id);\n      var e = edits[key];\n      if (!e) {\n            e = edits[key] = { key: key, clipId: num(clip.id), notes: [], hash: 0, conflict: false, lastWrite: 0, reply: null,\n                               incoming: null };\n            }\n      sendClip(e);\n      }\n\n// the clip to MuseScore (a new edit, the button again, or a reload after a conflict)\nfunction sendClip(e) {\n      if (!e)\n            return;\n      var clip = new LiveAPI(\"id \" + e.clipId);\n      if (!(num(clip.id) > 0)) {\n            send(\"/live/clip/gone\", e.key);\n            delete edits[e.key];\n            return;\n            }\n      var tr = trackOf(clip);\n      var trackName = tr ? str(tr.get(\"name\")) : \"\";\n      var clipName = str(clip.get(\"name\"));\n      var song = new LiveAPI(\"live_set\");\n      var notes = readNotes(clip);\n      e.notes = notes;\n      e.hash = hashNotes(notes);\n      e.conflict = false;\n      e.gen = ++editSerial;\n      var num_ = num(clip.get(\"signature_numerator\")) || num(song.get(\"signature_numerator\")) || 4;\n      var den = num(clip.get(\"signature_denominator\")) || num(song.get(\"signature_denominator\")) || 4;\n      var end = Math.max(num(clip.get(\"end_marker\")), num(clip.get(\"loop_end\")));\n      var chunks = Math.ceil(notes.length / CLIP_NOTES_PER_PACKET);\n      send(\"/live/clip/begin\", e.key, e.gen, trackName, clipName, isDrums(tr, trackName) ? 1 : 0, num(song.get(\"tempo\")),\n           num_, den, end, num(clip.get(\"loop_start\")), num(clip.get(\"loop_end\")), num(clip.get(\"looping\")) ? 1 : 0,\n           notes.length, chunks, e.hash);\n      for (var c = 0; c < chunks; ++c) {\n            var args = [\"/live/clip/notes\", e.key, e.gen, c];\n            var part = notes.slice(c * CLIP_NOTES_PER_PACKET, (c + 1) * CLIP_NOTES_PER_PACKET);\n            for (var i = 0; i < part.length; ++i) {\n                  var n = part[i];\n                  args.push(num(n.note_id), num(n.pitch), num(n.start_time), num(n.duration), num(n.velocity),\n                            num(n.mute) ? 1 : 0, n.probability === undefined ? 1 : num(n.probability),\n                            num(n.velocity_deviation || 0), n.release_velocity === undefined ? 64 : num(n.release_velocity));\n                  }\n            send.apply(this, args);\n            }\n      status(\"MuseScore Link: \" + clipName + \" (\" + trackName + \") sent to MuseScore\");\n      }\n\nfunction reply(e, args) {\n      e.reply = [\"/live/clip/written\"].concat(args);\n      send.apply(this, e.reply);\n      }\n\nfunction applyWrite(e, w) {\n      if (!e || !w)\n            return;\n      e.lastWrite = w.write;\n      var clip = new LiveAPI(\"id \" + e.clipId);\n      if (!(num(clip.id) > 0)) {\n            reply(e, [e.key, w.write, \"gone\", 0]);\n            delete edits[e.key];\n            return;\n            }\n      var before = readNotes(clip);\n      if (e.conflict || hashNotes(before) !== e.hash) {\n            e.conflict = true;\n            reply(e, [e.key, w.write, \"conflict\", hashNotes(before)]);\n            return;\n            }\n      var byId = {};\n      for (var i = 0; i < before.length; ++i)\n            byId[before[i].note_id] = before[i];\n      var removes = [], mods = [], adds = [];\n      for (i = 0; i < w.ops.length; ++i) {\n            var o = w.ops[i];\n            if (o.op === 1) {\n                  if (byId[o.id])\n                        removes.push(o.id);\n                  }\n            else if (o.op === 0) {\n                  var b = byId[o.id];\n                  if (!b)\n                        continue;\n                  var n = {};\n                  for (var k in b)                // Live's own note: only what was edited changes\n                        n[k] = b[k];\n                  if (o.mask & 1) n.pitch = o.pitch;\n                  if (o.mask & 2) n.start_time = o.start / TICKS;\n                  if (o.mask & 4) n.duration = o.duration / TICKS;\n                  if (o.mask & 8) n.velocity = o.velocity;\n                  if (o.mask & 16) n.mute = o.mute ? 1 : 0;\n                  mods.push(n);\n                  }\n            else if (o.op === 2)\n                  adds.push({ pitch: o.pitch, start_time: o.start / TICKS, duration: o.duration / TICKS, velocity: o.velocity,\n                              mute: o.mute ? 1 : 0 });\n            }\n      if (removes.length)\n            clip.call.apply(clip, [\"remove_notes_by_id\"].concat(removes));\n      if (mods.length)\n            clip.call(\"apply_note_modifications\", { notes: mods });\n      var added = [];\n      if (adds.length) {\n            var r = clip.call(\"add_new_notes\", { notes: adds });\n            var l = Array.isArray(r) ? r : (dict(r) && dict(r).note_ids) || str(r).replace(/[\\[\\],]/g, \" \").split(/\\s+/);\n            for (i = 0; i < l.length; ++i)\n                  if (str(l[i]) !== \"\" && !isNaN(Number(l[i])))\n                        added.push(Number(l[i]));\n            }\n      var after = readNotes(clip);\n      if (added.length !== adds.length) {         // (no ids returned: the new notes, matched to what was added)\n            added = [];\n            var had = {};\n            for (i = 0; i < before.length; ++i)\n                  had[before[i].note_id] = true;\n            var fresh = after.filter(function(x) { return !had[x.note_id]; });\n            for (i = 0; i < adds.length; ++i) {\n                  var best = -1, bd = 1e9;\n                  for (var j = 0; j < fresh.length; ++j) {\n                        if (fresh[j].pitch !== adds[i].pitch)\n                              continue;\n                        var dd = Math.abs(fresh[j].start_time - adds[i].start_time) + Math.abs(fresh[j].duration - adds[i].duration);\n                        if (dd < bd) {\n                              bd = dd;\n                              best = j;\n                              }\n                        }\n                  if (best >= 0) {\n                        added.push(fresh[best].note_id);\n                        fresh.splice(best, 1);\n                        }\n                  }\n            }\n      e.notes = after;\n      e.hash = hashNotes(after);\n      reply(e, [e.key, w.write, \"ok\", e.hash].concat(added));\n      status(\"MuseScore Link: \" + (removes.length + mods.length + adds.length) + \" note change(s) from MuseScore written\");\n      }\n\n// once a second: a clip edited in MuseScore changed in Live (or went)?\nfunction checkEdits() {\n      for (var key in edits) {\n            var e = edits[key];\n            if (e.conflict)\n                  continue;\n            var clip = new LiveAPI(\"id \" + e.clipId);\n            if (!(num(clip.id) > 0)) {\n                  send(\"/live/clip/gone\", key);\n                  delete edits[key];\n                  continue;\n                  }\n            var h = hashNotes(readNotes(clip));\n            if (h !== e.hash) {\n                  e.conflict = true;\n                  send(\"/live/clip/conflict\", key, h);\n                  status(\"MuseScore Link: \" + str(clip.get(\"name\")) + \" changed in Live: MuseScore stops writing to it\");\n                  }\n            }\n      }\n\n// (Node tests)\nif (typeof module !== \"undefined\")\n      module.exports = { handle: handle, workStep: workStep, displayName: displayName, ids: ids, loosePort: loosePort,\n                         findTrack: findTrack, writeSong: writeSong, report: report, hashNotes: hashNotes,\n                         checkEdits: checkEdits, edit: edit, state: function() {\n                               return { isHub: isHub, work: work, pending: pending, placed: placed, mode: mode, me: me,\n                                        edits: edits };\n                               } };\n",
						"filename": "none",
						"flags": 0,
						"embed": 1,
						"autowatch": 1
					}
				}
			},
			{
				"box": {
					"id": "obj-52",
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
					"id": "obj-53",
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
					"id": "obj-54",
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
							"parameter_type": 1,
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
					"id": "obj-55",
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
					"id": "obj-56",
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
					"id": "obj-57",
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
					"id": "obj-58",
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
					"id": "obj-59",
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
					"id": "obj-60",
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
					"id": "obj-61",
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
					"id": "obj-62",
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
					"id": "obj-63",
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
						"obj-51",
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
						"obj-52",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-51",
						1
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
						"obj-54",
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
						"obj-51",
						0
					]
				}
			},
			{
				"patchline": {
					"source": [
						"obj-57",
						0
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
						"obj-58",
						0
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
						"obj-51",
						2
					],
					"destination": [
						"obj-59",
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
						"obj-51",
						0
					]
				}
			}
		],
		"dependency_cache": [],
		"autosave": 0
	}
}
