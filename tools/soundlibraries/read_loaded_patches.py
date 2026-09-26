#!/usr/bin/env python3
# read_loaded_patches.py "<check folder>"
#
# Tells whether each patch of a Check articulations run was set up with the right Kontakt
# instrument: OCRs the loaded instrument's name from "<patch> (window).png" (Kontakt's rack list,
# left of the instrument, where Kontakt shows it however the instrument panel is scrolled) and
# compares it with the .nki the map's patch stands for. Needs tesseract and ImageMagick.
# The name is read where Kontakt 8 puts it with its side panel open (the owner's layout).
import difflib, glob, os, re, subprocess, sys, tempfile

# map patch name -> the SSO .nki name, where it isn't "<patch> - All techniques"
NKI = {
    'Solo Violin 1': 'Solo Violin - All techniques', 'Strings Ensemble': 'Ensembles - All techniques',
    'Piccolo': 'Piccolo Flute - All techniques', 'Contrabass Trombone': 'Contrabass Trombone Solo - All techniques',
    'Contrabass Tuba': 'Contrabass Tuba Solo - All techniques', 'Contrabassoon': 'ContraBassoon - All techniques',
    'Harp': 'Other - Harp', 'Grand Piano': 'Other - Grand Piano',
    'Motif Horns a4': 'Horns a4 - All techniques', 'Motif Trumpets a3': 'Trumpets a3 - All techniques',
    'Motif Trombones a5': 'Trombones a5 - All techniques',
}
for t in ('Timpani', 'Celeste', 'Glockenspiel', 'Xylophone', 'Marimba', 'Vibraphone', 'Crotales',
          'Tubular Bells', 'Desk Bells'):
    NKI[t] = 'Tuned - ' + t

def expected(patch):
    # extra patches (Performance, single techniques) are named as their .nki
    if ' - ' in patch:
        return patch
    return NKI.get(patch, patch + ' - All techniques')

def read(png, tmp):
    subprocess.run(['convert', png, '-crop', '280x22+40+163', '+repage', '-resize', '300%',
                    '-colorspace', 'Gray', '-negate', '-threshold', '55%', tmp], check=True)
    txt = subprocess.run(['tesseract', tmp, '-', '--psm', '7'], capture_output=True, text=True).stdout
    txt = re.sub(r'\s+s?S?M[OQ0]{2}\s*$', '', txt.strip())      # the S M buttons after the name
    return txt

def main(folder):
    tmp = os.path.join(tempfile.mkdtemp(), 'name.png')
    bad = 0
    for f in sorted(glob.glob(os.path.join(folder, '* (window).png'))):
        patch = os.path.basename(f)[:-len(' (window).png')]
        got = read(f, tmp)
        want = expected(patch)
        # Kontakt shortens long names ("Bass Trombone Solo - All ..."): compare what is shown
        shown = got.split('..')[0].rstrip('.- ')
        ratio = difflib.SequenceMatcher(None, shown.lower(), want[:len(shown)].lower()).ratio()
        # the numbers tell Violins 1 from 2 and Horns a2 from a4: they must be the same ("82" is
        # the OCR's "a2", so only digits that follow a letter-free start are compared as read)
        digits = lambda s: re.findall(r'\d', re.sub(r'\b8(\d)\b', r'a\1', s))
        ok = ratio >= 0.8 and len(shown) >= min(8, len(want)) and digits(shown) == digits(want[:len(shown)])
        bad += not ok
        print('%-4s %-24s loaded: %-34s expected: %s' % ('ok' if ok else 'BAD', patch, got, want))
    print('%d patch(es) to look at' % bad if bad else 'every patch had its instrument loaded')

if __name__ == '__main__':
    main(sys.argv[1])
