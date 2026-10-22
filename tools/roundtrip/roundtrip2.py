#!/usr/bin/env python3
"""roundtrip2.py [--fork <mscore>] [--ms36 <mscore 3.6.2>] [--out <dir>] [--jobs 4] [--mtest N] [--list f] [scores…]
— does a score survive a round trip to MuseScore 3.6? (CLAUDE.md › Rules: files survive a round trip to 3.6)

For each score: the fork saves it (A), MuseScore 3.6.2 opens A and saves it (B), the fork opens B
and saves it (C). Then
  text:  A's and C's score XML differ in how many lines
  pages: the fork's PNG pages of A and C: same count, and how many pixels differ
  midi:  the fork's MIDI export of A and C identical
Each app runs with fresh settings of its own (its own defaults). Results in roundtrip2/summary.tsv,
diffs in roundtrip2/<name>.diff. --mtest N adds N scores from each mtest folder of this repository.

Paths (moved into the repo 2026-10-21 from ~/MuseScore/ms3fork/ab/, whose install-ms4/ and ms37-ms4/ paths had gone
stale): --fork or $MS_FORK_BIN (the fork's mscore: an install's bin/mscore, or a build's main/mscore), --ms36 or
$MS36_BIN (MuseScore 3.6.2, e.g. ~/MuseScore/headless/mscore.sh), --out (default ./roundtrip2). Needs xvfb-run and
ImageMagick's compare. A list file's relative paths are relative to the list file."""
import concurrent.futures as cf, glob, json, os, shutil, subprocess, sys, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
FORK = os.environ.get('MS_FORK_BIN', '')
MS36 = os.environ.get('MS36_BIN', os.path.expanduser('~/MuseScore/headless/mscore.sh'))
OUT = os.path.join(os.getcwd(), 'roundtrip2')
XVFB = ['xvfb-run', '-a']

def run(app, cfg, *args, timeout=300):
    try:
        return subprocess.run(XVFB + [app, '-c', cfg] + list(args), capture_output=True, text=True,
                              timeout=timeout).returncode
    except subprocess.TimeoutExpired:
        return -1

def mscx(path):
    with zipfile.ZipFile(path) as z:
        name = next(n for n in z.namelist() if n.endswith('.mscx') and '/' not in n)
        return z.read(name).decode('utf-8', 'replace').splitlines()

def pixels(a, b):
    r = subprocess.run(['compare', '-metric', 'AE', a, b, 'null:'], capture_output=True, text=True)
    try:
        return int(float(r.stderr.split()[0]))
    except (ValueError, IndexError):
        return -1

def one(src, worker):
    name = os.path.splitext(os.path.basename(src))[0]
    rel = os.path.relpath(os.path.dirname(src), REPO).replace('/', '_')
    name = (rel + '__' + name) if not rel.startswith('..') else name
    d = os.path.join(OUT, name)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    cf_fork = os.path.join(OUT, 'cfg-fork-%d' % worker)
    cf_36 = os.path.join(OUT, 'cfg-36-%d' % worker)
    A, B, C = (os.path.join(d, x + '.mscz') for x in 'ABC')
    run(FORK, cf_fork, '-o', A, src)
    if not os.path.exists(A):
        return name, 'fork cannot read it', '', '', ''
    run(MS36, cf_36, '-o', B, A)
    if not os.path.exists(B):
        return name, 'MuseScore 3.6 cannot read A', '', '', ''
    run(FORK, cf_fork, '-o', C, B)
    if not os.path.exists(C):
        return name, 'fork cannot read B', '', '', ''
    a, c = mscx(A), mscx(C)
    import difflib
    diff = [l for l in difflib.unified_diff(a, c, 'A', 'C', lineterm='', n=1)]
    changed = sum(1 for l in diff if l[:1] in '+-' and l[:3] not in ('+++', '---'))
    open(os.path.join(OUT, name + '.diff'), 'w').write('\n'.join(diff) + '\n')
    # (each in a launch of its own: a second score in one launch renders a few pixels apart)
    for x, src_ in (('A', A), ('C', C)):
        jf = os.path.join(d, x + '.json')
        json.dump([{'in': src_, 'out': [os.path.join(d, x + '.png'), os.path.join(d, x + '.mid')]}], open(jf, 'w'))
        run(FORK, cf_fork, '-j', jf, timeout=600)
    pa = sorted(glob.glob(os.path.join(d, 'A-*.png')))
    pc = sorted(glob.glob(os.path.join(d, 'C-*.png')))
    if len(pa) != len(pc):
        pages = 'pages %d vs %d' % (len(pa), len(pc))
    else:
        px = [pixels(x, y) for x, y in zip(pa, pc)]
        pages = 'same' if all(p == 0 for p in px) else 'differ (%d px)' % sum(p for p in px if p > 0)
    ma, mc = os.path.join(d, 'A.mid'), os.path.join(d, 'C.mid')
    midi = ('same' if open(ma, 'rb').read() == open(mc, 'rb').read() else 'differ') \
        if os.path.exists(ma) and os.path.exists(mc) else 'missing'
    return name, 'ok', str(changed), pages, midi

def main(argv=None):
    global FORK, MS36, OUT
    args = list(sys.argv[1:] if argv is None else argv)
    if not args or args[0] in ('-h', '--help'):
        print(__doc__)
        return 0 if args else 2
    jobs = 4
    while args[:1] and args[0] in ('--fork', '--ms36', '--out', '--jobs'):
        k, v = args[0], args[1]
        args = args[2:]
        if k == '--fork': FORK = v
        elif k == '--ms36': MS36 = v
        elif k == '--out': OUT = os.path.abspath(v)
        else: jobs = int(v)
    for name, b in (('--fork / MS_FORK_BIN', FORK), ('--ms36 / MS36_BIN', MS36)):
        if not b or not os.path.exists(b):
            print('roundtrip2: no %s (%r)' % (name, b))
            return 2
    files = []
    if args[:1] == ['--mtest']:
        n = int(args[1]); args = args[2:]
        by = {}
        for f in sorted(glob.glob(os.path.join(REPO, 'mtest', '**', '*.mscx'), recursive=True)):
            if '-ref' in os.path.basename(f):
                continue
            by.setdefault(os.path.dirname(f), []).append(f)
        for k in sorted(by):
            files += by[k][:n]
    if args[:1] == ['--list']:
        base = os.path.dirname(os.path.abspath(args[1]))
        files += [os.path.join(base, l.strip()) if not os.path.isabs(l.strip()) else l.strip()
                  for l in open(args[1]) if l.strip()]
        args = args[2:]
    files += args
    os.makedirs(OUT, exist_ok=True)
    rows = []
    with cf.ThreadPoolExecutor(jobs) as ex:
        futs = {ex.submit(one, f, i % jobs): f for i, f in enumerate(files)}
        for fu in cf.as_completed(futs):
            r = fu.result()
            rows.append(r)
            print('\t'.join(r), flush=True)
    rows.sort()
    with open(os.path.join(OUT, 'summary.tsv'), 'w') as f:
        f.write('score\tstatus\ttext lines\tpages\tmidi\n')
        for r in rows:
            f.write('\t'.join(r) + '\n')
    bad = [r for r in rows if r[1] != 'ok' or r[2] != '0' or r[3] != 'same' or r[4] != 'same']
    print('%d scores, %d changed (%s)' % (len(rows), len(bad), os.path.join(OUT, 'summary.tsv')))
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main())
