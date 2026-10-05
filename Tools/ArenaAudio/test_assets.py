"""Validate rendered mothers, not generator implementation details."""
import json, unittest, wave
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parents[2] / 'Assets/Audio/GeometricWarfare'

class AudioAssetTests(unittest.TestCase):
    def test_rendered_library(self):
        self.assertTrue((ROOT/'manifest.json').exists(), 'Audio library has not been rendered')
        manifest = json.loads((ROOT/'manifest.json').read_text())
        self.assertEqual(len(manifest), 26)
        signatures=[]
        for asset in manifest:
            with self.subTest(asset=asset['name']), wave.open(str(ROOT/(asset['name']+'.wav'))) as w:
                self.assertEqual(w.getframerate(),48000)
                self.assertEqual(w.getsampwidth(),2)
                self.assertEqual(w.getnchannels(),2 if asset['category']=='music' else 1)
                samples=np.frombuffer(w.readframes(w.getnframes()),dtype='<i2').astype(float)/32768
                self.assertGreater(np.sqrt(np.mean(samples*samples)),.005,'silent asset')
                self.assertLessEqual(np.max(np.abs(samples)),.5012,'headroom')
                self.assertAlmostEqual(w.getnframes()/48000,asset['duration'],places=4)
                if asset['loop']:
                    channels=w.getnchannels()
                    self.assertLess(np.max(np.abs(samples[:channels]-samples[-channels:])),.02,'loop click')
                signatures.append(hash(samples.tobytes()))
        self.assertEqual(len(set(signatures)),26,'all cues must be distinct')

if __name__=='__main__': unittest.main()
