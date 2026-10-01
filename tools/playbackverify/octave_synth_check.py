#!/usr/bin/env python3
"""Synthetic check of legato arrival timing (2026-10-01): two-note slurs with an equal-power crossfade at a known
time, a hall of -60 / -10 / -3 dB (2 s); the template fit (frame = a A + b B of the two notes' own spectra) against
analyze_sweep.arrival's harmonic ratio. Known answer: 0 ms. Template: -40..+60 for every interval; harmonic: +75..+320
for +12 (late, worse with the hall), -60..-70 for -12. Usage: octave_synth_check.py"""
import numpy as np, sys
from scipy.optimize import nnls
from scipy.signal import fftconvolve
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
import analyze_sweep as A
sr=48000
rng=np.random.default_rng(1)
def note(f,n,amps,vib=0.0):
    t=np.arange(n)/sr; ph=2*np.pi*f*(t+vib*np.sin(2*np.pi*5.5*t)/(2*np.pi*5.5*f)*f*0.004)
    return sum(a*np.sin(h*ph+rng.uniform(0,6)) for h,a in enumerate(amps,1) if h*f<sr*0.45)
def synth(pa,pb,T=1.2,xf=0.1,rev_db=-10,rt60=2.0,dur=2.6):
    fa=440*2**((pa-69)/12); fb=440*2**((pb-69)/12)
    n=int(dur*sr); t=np.arange(n)/sr
    aa=rng.uniform(0.2,1,16)/np.arange(1,17)**0.7; ab=rng.uniform(0.2,1,16)/np.arange(1,17)**0.7
    xa=note(fa,n,aa,1); xb=note(fb,n,ab,1)
    ga=np.clip((T+xf/2-t)/xf,0,1); gb=1-ga
    ga=np.sqrt(ga); gb=np.sqrt(gb)   # equal power crossfade: equal at T
    dry=ga*xa+gb*xb
    L=int(rt60*sr); ir=rng.standard_normal(L)*np.exp(-6.9*np.arange(L)/L)
    ir*=10**(rev_db/20)/np.sqrt((ir**2).sum())
    wet=fftconvolve(dry,ir)[:n]
    return dry+wet, fa, fb
def frames(x,N,hop,t0,t1):
    w=np.hanning(N); out=[];ts=[]
    for c in range(int(t0*sr),int(t1*sr),hop):
        s=c-N//2
        seg=x[max(0,s):s+N]
        if s<0: seg=np.concatenate([np.zeros(-s),seg])
        if len(seg)<N: seg=np.concatenate([seg,np.zeros(N-len(seg))])
        out.append(np.abs(np.fft.rfft(seg*w))**2); ts.append(c/sr)
    return np.array(ts),np.array(out)
def template_mid(x,on,fa,fb,first=1.2):
    N=8192 if min(fa,fb)<120 else 4096; hop=int(sr*0.01)
    freqs=np.fft.rfftfreq(N,1/sr); band=(freqs>50)&(freqs<8000)
    _,PA=frames(x,N,hop,on-0.5,on-0.1); _,PB=frames(x,N,hop,on+0.9,on+1.3)
    TA=PA.mean(0)[band]; TB=PB.mean(0)[band]
    ts,P=frames(x,N,hop,on-0.1,on+1.0)
    M=np.stack([TA,TB],1); sc=M.max()
    sh=[]
    for p in P:
        c,_=nnls(np.sqrt(M/sc),np.sqrt(p[band]/sc))  # magnitudes
        ea=c[0]**2*TA.sum(); eb=c[1]**2*TB.sum()
        sh.append(eb/(ea+eb+1e-30))
    sh=np.array(sh)
    res={}
    for q in (0.1,0.5,0.9):
        run=0
        for t,s in zip(ts,sh):
            run=run+1 if s>=q else 0
            if run==3: res[q]=round((t-0.02-on)*1000); break
    return res
def harm_mid(x,on,fa,fb):
    # analyze_sweep's arrival from 300 ms before
    m=x
    return A.arrival(m,sr,on,fa,fb,on-0.3,on+1.0)
if __name__=='__main__':
    for pa,iv in ((48,12),(60,12),(72,12),(60,-12),(60,7),(60,2),(40,12)):
        for rev in (-60,-10,-3):
            x,fa,fb=synth(pa,pa+iv,rev_db=rev)
            h=harm_mid(x,1.2,fa,fb)
            print(pa,iv,rev,'template',template_mid(x,1.2,fa,fb),'harm',None if h is None else round(h*1000))
