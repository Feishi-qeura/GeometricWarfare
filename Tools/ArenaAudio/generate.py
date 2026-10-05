"""Original Geometric Warfare score. Deterministic additive/FM synthesis; no samples.

Theme: A C E G | B E D C, answered by A G E D. Harmony Am F C G.
All envelopes terminate smoothly, with wraparound tails for seamless bar loops.
"""
import json, wave
from pathlib import Path
import numpy as np

RATE=48000
RNG=np.random.default_rng(1052026)
ROOT=Path(__file__).resolve().parents[2]/'Assets/Audio/GeometricWarfare'
LIBRARY=[]

def hz(midi): return 440*2**((midi-69)/12)
def tone(freq,duration,style='pluck'):
    t=np.arange(round(duration*RATE))/RATE
    attack=np.minimum(t/.006,1)
    tail=np.minimum((duration-t)/.016,1)
    if style=='pad':
        env=np.sin(np.pi*t/duration)**2
        y=sum(np.sin(2*np.pi*freq*k*t)*(.45/k**2) for k in (1,2,3,5))
    elif style=='bass':
        env=attack*tail*np.exp(-t*3)
        y=np.sin(2*np.pi*freq*t)+.2*np.sin(2*np.pi*freq*2*t)
    else:
        env=attack*tail*np.exp(-t*6/duration)
        y=np.sin(2*np.pi*freq*t+1.1*np.sin(2*np.pi*freq*2*t)*np.exp(-t*14))+.12*np.sin(2*np.pi*freq*3*t)
    return y*env

def drum(kind):
    duration={'kick':.24,'snare':.16,'hat':.05}[kind]
    t=np.arange(round(duration*RATE))/RATE
    noise=RNG.uniform(-1,1,len(t))
    if kind=='kick': y=np.sin(2*np.pi*(48*t+4*(1-np.exp(-t*30))))*np.exp(-t*22)
    elif kind=='snare': y=(.7*noise+.25*np.sin(2*np.pi*180*t))*np.exp(-t*35)
    else: y=(noise-np.roll(noise,1))*.23*np.exp(-t*90)
    return y*np.minimum(t/.002,1)*np.minimum((duration-t)/.01,1)

def write(name,y,loop=False,category='sfx'):
    y=np.asarray(y)
    # Fixed headroom; per-cue normalization does not sacrifice runtime mix control.
    y=y/(max(.01,float(np.max(np.abs(y)))))*(.34 if category=='music' else .45)
    y-=np.mean(y,axis=0)
    y=np.clip(y,-.5,.5)
    if loop:
        # 2 ms smooth seam, never a long silence between phrases.
        n=96
        bridge=(y[0]+y[-1])/2
        w=np.linspace(0,1,n)
        if y.ndim>1:w=w[:,None]
        y[:n]=bridge*(1-w)+y[:n]*w
        y[-n:]=y[-n:]*(1-w)+bridge*w
    with wave.open(str(ROOT/(name+'.wav')),'wb') as wav:
        wav.setnchannels(2 if y.ndim>1 else 1);wav.setsampwidth(2);wav.setframerate(RATE)
        wav.writeframes((y*32767).astype('<i2').tobytes())
    LIBRARY.append(dict(name=name,loop=loop,category=category,duration=len(y)/RATE))

def music(name,bpm,bars):
    beat=60/bpm;length=round(bars*4*beat*RATE)
    out=np.zeros((length,2))
    def add(y,at,gain=.3,pan=0):
        idx=(round(at*RATE)+np.arange(len(y)))%length
        np.add.at(out[:,0],idx,y*gain*(1-pan*.5))
        np.add.at(out[:,1],idx,y*gain*(1+pan*.5))
    theme=[69,72,76,79,71,76,74,72,69,67,64,62,64,67,71,76]
    roots=[45,41,48,43]
    boss=name=='Boss';sprint=name=='Sprint';results=name=='Results';assist=name=='HostAssist'
    for bar in range(bars):
        root=roots[bar%4];time=bar*4*beat
        chord=[root+24,root+27 if bar%4 in (0,1) else root+28,root+31]
        for j,note in enumerate(chord):add(tone(hz(note),4*beat,'pad'),time,.16,-.55+j*.55)
        for step in range(8):
            if results and step%2:continue
            note=theme[(bar*4+step)%len(theme)]+(12 if sprint or assist else -12 if boss else 0)
            add(tone(hz(note),beat*.85),time+step*beat/2,.24 if bar%8>=4 or assist else .16,(-1 if step%2 else 1)*.5)
        for step in range(4 if not sprint else 8):
            add(tone(hz(root+(12 if step%4==3 else 0)),beat*.7,'bass'),time+step*beat/(2 if sprint else 1),.30 if boss else .23)
        if not results:
            for step in (0,2):add(drum('kick'),time+step*beat,.7)
            for step in (1,3):add(drum('snare'),time+step*beat,.20 if boss else .15)
            for step in range(8 if sprint else 4):add(drum('hat'),time+step*beat/(2 if sprint else 1),.12)
        elif bar%2==0:add(drum('kick'),time,.16)
        if boss:
            for step in (1.5,3.5):add(tone(hz(root+36),.25),time+step*beat,.17)
    if assist:
        out*=np.minimum(np.arange(length)/(.012*RATE),1)[:,None]*np.minimum((length-np.arange(length))/(.35*RATE),1)[:,None]
    write(name,out,not assist,'music')

def impact(name,freq,duration,noise=.2,sweep=-80):
    t=np.arange(round(duration*RATE))/RATE
    env=np.minimum(t/.002,1)*np.minimum((duration-t)/.014,1)*np.exp(-t*7/duration)
    y=(np.sin(2*np.pi*(freq*t+sweep*t*t/duration))+.16*np.sin(2*np.pi*freq*2.41*t)+noise*RNG.uniform(-1,1,len(t)))*env
    write(name,y)

def notes(name,sequence,duration,noise=0):
    y=np.zeros(round(duration*RATE))
    for i,note in enumerate(sequence):
        start=round(i*duration/len(sequence)*.72*RATE)
        v=tone(hz(note),duration/len(sequence)*1.5)
        count=min(len(v),len(y)-start);y[start:start+count]+=v[:count]
    if noise:
        t=np.arange(len(y))/RATE;y+=RNG.uniform(-1,1,len(y))*noise*np.exp(-t*30)
    write(name,y)

def generate():
    ROOT.mkdir(parents=True,exist_ok=True)
    for name,bpm,bars in [('Battle',112,32),('Boss',112,32),('Sprint',140,32),('Results',112,8),('HostAssist',112,2)]:music(name,bpm,bars)
    for args in [('Pistol',540,.105,.18,-160),('Shotgun',190,.30,.8,-70),('Sniper',880,.38,.25,-270),('Rifle',410,.12,.30,-120),('MachineGun',330,.065,.40,-90),('Rocket',110,.58,.5,-35),('FighterHit',940,.11,.1,-250),('NpcHit',300,.14,.22,-95),('BossBullet',140,.62,.32,-60),('BossStompImpact',65,.8,.6,-20)]:impact(*args)
    for args in [('FighterDeath',[81,76,72,57],.45,.22),('NpcDeath',[64,59,52],.36,.3),('Orb',[81,88],.16,0),('WeaponPickup',[64,71,76],.28,0),('WeaponBreak',[52,64,71,76],.42,.6),('EvolutionPickup',[69,76,81,88],.45,0),('EvolutionBreak',[57,69,76,81,88],.6,.4),('BossStompJump',[45,52,57,64],.85,.12)]:notes(*args)
    for name,duration in [('BossLaserWindup',5),('BossLaserRage',3),('BossLaserBeam',.5)]:
        t=np.arange(round(duration*RATE))/RATE
        if name=='BossLaserBeam':
            y=(np.sin(2*np.pi*192*t)+.24*np.sin(2*np.pi*768*t))*(.7+.3*np.cos(2*np.pi*8*t))
            write(name,y,True)
        else:
            y=np.sin(2*np.pi*(160*t+180*t*t/duration))*(.16+.5*t/duration)
            y*=np.minimum(t/.04,1)*np.minimum((duration-t)/.04,1);write(name,y)
    (ROOT/'manifest.json').write_text(json.dumps(LIBRARY,indent=2)+'\n')
    print(f'Rendered {len(LIBRARY)} original audio assets to {ROOT}')

if __name__=='__main__':generate()
