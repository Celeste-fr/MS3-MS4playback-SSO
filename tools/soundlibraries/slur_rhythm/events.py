# events.py [--cc11=<v>] <dump dir> <out dir> <code prefix> [first bar] [last bar] [part,part,...]: kthost event files from a
# dumpEvents dump (tst_liveequivalence, MS_DUMP_CHUNKED=1: the events as MuseScore's playback renders them), one file per
# route of the chosen parts (all: every part), the events of bars first..last (bars counted from 1; note-offs of notes
# begun before the end kept, events before the first bar's start dropped except controllers, which are moved to it),
# times + 1.0 s (kthost starts at 0; a lead before the score's 0 fits). Writes <out>/<prefix>_<route>.txt and appends
# "<prefix>_<route>|<patch>" to <out>/list.txt. Render each on the Windows VM with
#   kthost.exe "Kontakt 8.vst3" "<patch>.vst3state" <prefix>_<route>.txt <prefix>_<route>.wav --tail 3
import os, sys

# --cc11=<v>: every CC11 event dropped and CC11 <v> sent at the start: the patch's own level, as the map's onsets were
# measured (sso_smooth_onset.json: CC11 127). MuseScore's playback sends CC11 under the patch's gain stage (contextlevel.h),
# which kthost doesn't apply, and loudness.py's perceived envelope is not level-invariant (Violas grid, first notes'
# -15 dB onset: 197 ms at the dump's CC11 10-18, 157 ms 20 dB louder)
CC11 = None
for a in list(sys.argv):
    if a.startswith('--cc11='):
        CC11 = int(a.split('=')[1])
        sys.argv.remove(a)
dump, out, prefix = sys.argv[1], sys.argv[2], sys.argv[3]
first = int(sys.argv[4]) if len(sys.argv) > 4 else 1
last = int(sys.argv[5]) if len(sys.argv) > 5 else 0
parts = set(sys.argv[6].split(',')) if len(sys.argv) > 6 and sys.argv[6] else None
os.makedirs(out, exist_ok=True)
bars = [float(x) for x in open(os.path.join(dump, 'bars.txt')).read().split()]
lead = None
lst = []
for ln in open(os.path.join(dump, 'routemap.txt')).read().splitlines():
    route, part, patch, lane, cents, bend = ln.split('|')
    if parts and part not in parts:
        continue
    f = os.path.join(dump, route + ' museScore.txt')
    if not os.path.exists(f):
        continue
    ev = [e.split() for e in open(f).read().splitlines() if e.strip() and not e.startswith('#')]
    if lead is None:
        lead = float(open(os.path.join(dump, '0-1 museScore.txt')).readline().split()[0])
    start = bars[first - 1] + lead - 0.6 if first > 1 else -1e9          # (early starts reach up to ~0.5 s before)
    end = (bars[last] if 0 < last < len(bars) else 1e9) + lead
    keep, sounding, ctl = [], set(), {}
    for x in ev:
        t, ty = float(x[0]), x[1]
        if t < start:
            if ty in ('cc', 'pb'):
                ctl[(ty, x[2]) if ty == 'cc' else ty] = x
            continue
        if t < end:
            keep.append(x)
            if ty == 'on':
                sounding.add(x[2])
            elif ty == 'off':
                sounding.discard(x[2])
        elif ty == 'off' and x[2] in sounding:
            keep.append(x)
            sounding.discard(x[2])
    if not any(x[1] == 'on' for x in keep):
        continue
    shift = start if start > -1e8 else 0.0               # (kthost time = dump time - shift + 1.0)
    if CC11 is not None:
        keep = [x for x in keep if not (x[1] == 'cc' and x[2] == '11')]
        ctl = {k: x for k, x in ctl.items() if k != ('cc', '11')}
        ctl[('cc', '11')] = ['0', 'cc', '11', str(CC11)]
    lines = ['%.6f %s' % (1.0, ' '.join(x[1:])) for x in ctl.values()]
    lines += ['%.6f %s' % (float(x[0]) - shift + 1.0, ' '.join(x[1:])) for x in keep]
    code = '%s_%s' % (prefix, route)
    open(os.path.join(out, code + '.txt'), 'w').write('\r\n'.join(lines) + '\r\n')
    lst.append('%s|%s' % (code, patch))
    print(code, part, patch, lane, sum(1 for x in keep if x[1] == 'on'), 'notes; shift', round(shift, 6))
with open(os.path.join(out, 'list.txt'), 'a') as fl:
    fl.write(''.join(l + '\n' for l in lst))
with open(os.path.join(out, prefix + '.shift'), 'w') as fs:
    fs.write('%.6f %.6f\n' % ((bars[first - 1] + lead - 0.6) if first > 1 else 0.0, lead))   # shift, lead (s)
