#!/usr/bin/env python3
"""Reads a report of MuseScore --verify-playback (VERIFY.md): the zip the owner hands back, its folder,
or a folder of such zips (the newest is read).

  read_verify_report.py <zip | folder>                 the verdict, per score and part: counts, what
                                                       the flagged moments have in common, each finding
  read_verify_report.py <zip | folder> --near <s> [--part <name>] [--window <s>]
                                                       the notes, strikes and events around a time (the
                                                       measures behind a finding; score time in seconds)
  read_verify_report.py <zip | folder> --json          the report itself

A zip is read in place (nothing is extracted). Standard library only.
"""
import argparse
import csv
import io
import json
import os
import sys
import zipfile


class Report:
    def __init__(self, path):
        self.zip = None
        if os.path.isdir(path):
            zips = sorted((os.path.join(path, f) for f in os.listdir(path) if f.startswith('Playback verify') and f.endswith('.zip')),
                          key=os.path.getmtime)
            if os.path.exists(os.path.join(path, 'report.json')):
                self.root = path
            elif zips:
                path = zips[-1]
            else:
                sys.exit(f'no report in {path}')
        if not os.path.isdir(path):
            self.zip = zipfile.ZipFile(path)
            names = self.zip.namelist()
            self.root = names[0].split('/')[0] if names else ''
        self.name = path

    def read(self, rel):
        if self.zip:
            try:
                return self.zip.read(self.root + '/' + rel).decode('utf-8')
            except KeyError:
                return None
        p = os.path.join(self.root, rel)
        return open(p, encoding='utf-8').read() if os.path.exists(p) else None

    def tsv(self, rel):
        text = self.read(rel)
        return list(csv.DictReader(io.StringIO(text), delimiter='\t')) if text else []


def mmss(s):
    return f'{int(s // 60)}:{s % 60:06.3f}'


def overview(rep):
    j = json.loads(rep.read('report.json'))
    print(f'{rep.name}\n{j["verdict"].upper()}: {j["findingCount"]} findings; MuseScore {j["museScore"]} {j.get("build", "")}; '
          f'library {j["library"]}' + (f'; checked the file {j["audio"]}' if j.get('audio') else ''))
    print('totals:', ', '.join(f'{v} {k}' for k, v in sorted(j['totals'].items()) if v))
    for sc in j['scores']:
        print(f'\n# {sc["file"]}' + (f'  ERROR: {sc["error"]}' if 'error' in sc else ''))
        if 'error' in sc:
            continue
        print(f'  {sc.get("length", 0):.0f} s, peak {sc.get("peakDb")} dBFS, {len(sc.get("clipping", []))} clipping, '
              f'rendered in {sc.get("renderSeconds", 0):.0f} s')
        for c in sc.get('clipping', [])[:10]:
            print(f'  CLIPPING at {mmss(c["time"])} for {c["length"] * 1000:.1f} ms, {c["peakDb"]:+.1f} dBFS')
        for part in sc.get('parts', []):
            print(f'  ## {part["name"]} ({part["patches"]}): {part["notes"]} notes, {part["strikes"]} strikes, '
                  f'offset {part["offset"] * 1000:.0f} ms; weak {part["weakStrikes"]} (not flagged: {part["unclear"]} also weak in the built-in synth, {part.get("weakButSounding", 0)} with their notes at their level)')
            flagged = [f for f in part['findings'] if f['kind'] in ('missing-attack', 'missing-note')]
            tags = part.get('atStrikes', {})
            if flagged and tags:
                n = max((v['flagged'] for v in tags.values()), default=0)
                rows = sorted(tags.items(), key=lambda kv: -kv[1]['flagged'] / max(1, kv[1]['all']))
                print('     at the flagged strikes (flagged with it, all with it, of all strikes):')
                for k, v in rows:
                    if v['flagged']:
                        print(f'       {k:32} {v["flagged"]:4} {v["all"]:6} / {part["strikes"]}')
            for f in part['findings']:
                where = f'm. {f["measure"]} beat {f["beat"]}' if 'measure' in f else ''
                ctx = '; '.join(f.get('context', []))
                print(f'   {f["kind"]:15} {where:16} {mmss(f["time"])} {f.get("pitchNames", ""):14} {f["text"]}'
                      + (f' | {ctx}' if ctx else '') + (f' [{f["clip"]}]' if 'clip' in f else ''))


def near(rep, t, part, window):
    j = json.loads(rep.read('report.json'))
    for sc in j['scores']:
        base = os.path.splitext(sc['file'])[0]
        parts = sc.get('parts', [])
        for p in parts:
            if part and part.lower() not in p['name'].lower():
                continue
            prefix = base if len(parts) == 1 else base + ' ' + p['name']
            for what in ('strikes', 'notes'):
                rows = [r for r in rep.tsv(f'{prefix} {what}.tsv') if abs(float(r['time']) - t) <= window]
                if rows:
                    print(f'== {prefix} {what}.tsv')
                    print('\t'.join(rows[0].keys()))
                    for r in rows:
                        print('\t'.join(r.values()))
        rows = [r for r in rep.tsv(f'{base} events.tsv') if abs(float(r['time']) - t) <= window]
        if rows:
            print(f'== {base} events.tsv (the library\'s events)')
            for r in rows:
                print('\t'.join(r.values()))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('report')
    ap.add_argument('--near', type=float)
    ap.add_argument('--part')
    ap.add_argument('--window', type=float, default=0.3)
    ap.add_argument('--json', action='store_true')
    a = ap.parse_args()
    rep = Report(a.report)
    if a.json:
        print(rep.read('report.json'))
    elif a.near is not None:
        near(rep, a.near, a.part, a.window)
    else:
        overview(rep)


if __name__ == '__main__':
    main()
