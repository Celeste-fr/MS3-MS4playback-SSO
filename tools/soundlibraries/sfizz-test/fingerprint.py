import sys, wave, numpy as np
w=wave.open(sys.argv[1]); r=w.getframerate(); n=w.getnframes()
x=np.frombuffer(w.readframes(n),dtype=np.int16).astype(float).reshape(-1,w.getnchannels()).mean(1)/32768
K2UACC={2:1,3:40,4:42,5:56,6:10,7:61}
NAMES={1:'Long',40:'staccato',42:'Spiccato',56:'Pizzicato',10:'Long Harmonics',61:'Short Harmonics'}
# (onset s, midi pitch, what the score has, expected UACC)
notes=[(0.0,72,'whole note',1),(2.0,72,'staccato',40),(2.5,74,'staccato',40),(3.0,76,'staccatissimo',42),(3.5,77,'accent (0.5 s)',1),
       (4.0,79,'tremolo (no sample: repeated)',1),(5.0,76,'trill (no sample: notes)',1),(6.0,72,'pizz.',56),(6.5,74,'pizz.',56),
       (7.0,76,'arco, con sord. (no CS: Long)',1),(8.0,81,'senza sord., diamond head',10),(9.0,74,'trill (no sample: notes)',1)]
ok=0
for t,p,what,exp in notes:
    f0=440*2**((p-69)/12)
    seg=x[int((t+0.03)*r):int((t+0.18)*r)]
    sp=np.abs(np.fft.rfft(seg*np.hanning(len(seg)),8*len(seg))); fr=np.fft.rfftfreq(8*len(seg),1/r)
    amp=lambda f: sp[(fr>f*0.985)&(fr<f*1.015)].max()
    a1=amp(f0); ks={k:amp(f0*k) for k in K2UACC}
    k=max(ks,key=ks.get); got=K2UACC[k] if ks[k]>0.2*a1 else None
    good=got==exp; ok+=good
    print(f"{t:4.1f}s {what:32s} expected {exp:3d} {NAMES[exp]:16s} heard {str(got):>4s} {NAMES.get(got,'-'):16s} {'OK' if good else 'MISMATCH'}")
print(f"{ok}/{len(notes)} as expected")
