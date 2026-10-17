# whence_variants.py <out dir> <dump> [<dump with slurs/transitionsEarly=0>] [--routes 0-12,1-2]: kthost event files of a
# score's bending Performance routes as MuseScore renders them (tst_playbackaudit auditScore's MS_AUDIT_DUMP: "<route>
# museScore.txt", predicted.tsv), and variants that each change one thing, to find which part of the context keeps the
# quarter tones from sounding (gen.py's runs, in isolation, reach them):
#   A   as rendered;
#   E   the second dump (transitions sent on the beat), when given;
#   V   every legato transition at the map's legatoVelocity (Violas 100, else 93) instead of the Performance technique's;
#   M   CC1 64 throughout (the level rule plays the written dynamic; in the 2026-10-17 dump CC1 was already 64 on both
#       routes, so M = A event for event: its render is a second pass of A, the noise floor);
#   L   no CC11 (even levels, track levels);
#   B   no pitch bend (the keys in 12-TET: how far the bend has to carry);
#   S   each note's bend as a step at its note-on (no glide).
# Writes <out>/ev/<route>_<v>.txt (+1.0 s, as the heard check), <out>/ev/list.txt and <out>/index.json in gen.py's
# format (ana.py reads it): each note's on / next on (s, in the file), key, written target and the note before's.
import csv, json, os, sys

OFFSET = 1.0
args = [a for a in sys.argv[1:] if not a.startswith('--')]
routes = ['0-12', '1-2']
for i, a in enumerate(sys.argv):
    if a == '--routes':
        routes = sys.argv[i + 1].split(',')
        args = [x for x in args if x != sys.argv[i + 1]]
OUT, DUMP = args[0], args[1]
DUMP_E = args[2] if len(args) > 2 else None
LEGATO_VELOCITY = {'Violas - Performance': 100}
os.makedirs(f'{OUT}/ev', exist_ok=True)


def read(dump, route):
    ev = [l.split() for l in open(f'{dump}/{route} museScore.txt') if l.strip() and not l.startswith('#')]
    rows = [r for r in csv.DictReader(open(f'{dump}/predicted.tsv'), delimiter='\t') if r['route'] == route]
    return ev, rows


def notes_of(ev, rows):
    """each note-on of the file paired in order with its row (same key), its target and the next note-on"""
    ons = [(float(x[0]) + OFFSET, int(x[2])) for x in ev if x[1] == 'on' and int(x[3]) > 0]
    rows = sorted(rows, key=lambda r: float(r['written']) + float(r['sentMs']) / 1000)
    assert len(ons) == len(rows), (len(ons), len(rows))
    out = []
    for k, ((t, key), r) in enumerate(zip(ons, rows)):
        assert key == int(r['pitch']), (t, key, r['pitch'])
        target = key + float(r['cents']) / 100
        prev = out[-1]['target'] if out and int(r['transition']) else None
        nxt = ons[k + 1][0] if k + 1 < len(ons) else t + 0.3
        out.append(dict(on=t, next=nxt, key=key, target=target, prev=prev, ioi=round(nxt - t, 3), k=1 if prev is not None else 0,
                        take=0, bar=int(r['bar']), beat=float(r['beat'])))
    return out


def unit(c, R):
    return max(0, min(16383, int(round(8192 + c / R * (8192 if c < 0 else 8191)))))


def write(name, lines):
    open(f'{OUT}/ev/{name}.txt', 'w').write('\r\n'.join('%.6f %s' % (float(x[0]) + OFFSET, ' '.join(x[1:])) for x in lines) + '\r\n')


idx = {}
lst = []
rmap = {l.split('|')[0]: l.strip().split('|') for l in open(f'{DUMP}/routemap.txt') if l.strip()}
for route in routes:
    patch, R = rmap[route][2], float(rmap[route][5])
    ev, rows = read(DUMP, route)
    notes = notes_of(ev, rows)
    tv = LEGATO_VELOCITY.get(patch, 93)
    var = {'A': ev,
           'V': None,
           'M': [x[:3] + ['64'] if x[1] == 'cc' and x[2] == '1' else x for x in ev],
           'L': [x for x in ev if not (x[1] == 'cc' and x[2] == '11')],
           'B': [x for x in ev if x[1] != 'pb'],
           'S': None}
    # V: the transitions (a note-on with a note before) at the legato velocity
    on_k = iter(notes)
    v = []
    for x in ev:
        if x[1] == 'on' and int(x[3]) > 0:
            n = next(on_k)
            v.append(x[:3] + [str(tv)] if n['prev'] is not None else x)
        else:
            v.append(x)
    var['V'] = v
    # S: a step bend at each note-on to its target (the note's cents)
    on_k = iter(notes)
    s = []
    for x in ev:
        if x[1] == 'pb':
            continue
        if x[1] == 'on' and int(x[3]) > 0:
            n = next(on_k)
            s.append([x[0], 'pb', str(unit((n['target'] - n['key']) * 100, R)), '0'])
        s.append(x)
    var['S'] = s
    if DUMP_E:
        ev_e, rows_e = read(DUMP_E, route)
        var['E'] = ev_e
    for k, lines in var.items():
        name = f'{route}_{k}'
        write(name, lines)
        ns = notes if k != 'E' else notes_of(ev_e, rows_e)
        idx[name] = dict(patch=patch, variant=k, route=route, notes=ns, probe=ns[0]['key'])
        lst.append(f'{name}|{patch}|{ns[0]["key"]}')
open(f'{OUT}/ev/list.txt', 'w').write('\r\n'.join(lst) + '\r\n')
json.dump(idx, open(f'{OUT}/index.json', 'w'), indent=0)
print(' '.join(sorted(idx)))
