"""Validate an actual Unreal master-submix capture; counters alone cannot prove sound."""
import json,sys,wave
from pathlib import Path
import numpy as np
path=Path(sys.argv[1])
with wave.open(str(path)) as w:
    assert w.getsampwidth()==2 and w.getnchannels()==2 and w.getframerate()==48000
    x=np.frombuffer(w.readframes(w.getnframes()),dtype='<i2').astype(float)/32768
    result=dict(seconds=w.getnframes()/w.getframerate(),channels=w.getnchannels(),rate=w.getframerate(),rms=float(np.sqrt(np.mean(x*x))),peak=float(np.max(np.abs(x))),clipped_samples=int(np.count_nonzero(np.abs(x)>=.999)))
assert result['seconds']>=30 and result['rms']>.001 and result['clipped_samples']==0,result
path.with_suffix('.analysis.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result))
