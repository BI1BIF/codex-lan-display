"""Original, deterministic notification sounds; Python standard library only.

Run this file to regenerate four audition sets and twelve device-ready PCM WAVs.
The audition slots are running at 0.45s, question at 3.05s, done at 5.65s.
"""
import array
import hashlib
import json
import math
from pathlib import Path
import sys
import wave
import zipfile

RATE = 16000
ROOT = Path(__file__).resolve().parent
TAU = 2 * math.pi
STATES = ('running', 'question', 'done')
PALETTES = {
    'A': ('柔光电子铃', 'warm', {
        'running': [(0, 523.25, .31, .80), (.16, 783.99, .48, .68)],
        'question': [(0, 659.25, .36, .76), (.24, 739.99, .62, .88)],
        'done': [(0, 783.99, .43, .66), (.17, 659.25, .46, .74), (.36, 523.25, .68, .92)],
    }),
    'B': ('清透晶体', 'glass', {
        'running': [(0, 659.25, .30, .77), (.12, 987.77, .48, .65)],
        'question': [(0, 783.99, .25, .68), (.27, 880, .52, .86)],
        'done': [(0, 659.25, .47, .65), (.15, 783.99, .48, .68), (.31, 1046.50, .72, .88)],
    }),
    'C': ('低调舱音', 'console', {
        'running': [(0, 392, .27, .85), (.14, 587.33, .36, .72)],
        'question': [(0, 440, .26, .76), (.30, 493.88, .46, .94)],
        'done': [(0, 587.33, .35, .70), (.16, 493.88, .38, .74), (.34, 392, .57, .96)],
    }),
    'D': ('轻盈脉冲', 'orbit', {
        'running': [(0, 440, .27, .68), (.14, 554.37, .27, .73), (.28, 659.25, .40, .80)],
        'question': [(0, 554.37, .25, .77), (.22, 659.25, .25, .70), (.50, 739.99, .48, .90)],
        'done': [(0, 659.25, .43, .67), (.18, 880, .46, .70), (.38, 1108.73, .67, .86)],
    }),
}


def voice(kind, frequency, duration):
    samples = []
    for i in range(round(duration * RATE)):
        t = i / RATE
        # A smooth envelope avoids buzzer-like edges and discontinuous starts.
        attack = 1 - math.exp(-t / (.014 if kind in ('warm', 'orbit') else .009))
        tail = min(1, max(0, (duration-t) / .065))
        envelope = attack * math.exp(-t / (duration * .32)) * (.5-.5*math.cos(math.pi*tail))
        phase = TAU * frequency * t
        if kind == 'warm':
            sample = math.sin(phase) + .16*math.exp(-t/.16)*math.sin(phase*2) + .04*math.exp(-t/.08)*math.sin(phase*3)
        elif kind == 'glass':
            sample = .88*math.sin(phase + .55*math.exp(-t/.12)*math.sin(phase*2)) + .09*math.exp(-t/.19)*math.sin(phase*3.01)
        elif kind == 'console':
            phase += TAU*12*.035*(1-math.exp(-t/.035))
            sample = math.sin(phase) - .07*math.sin(phase*3) + .12*math.sin(phase*.5)
        else:
            phase += TAU*frequency*.018*.07*(1-math.exp(-t/.07))
            sample = .88*math.sin(phase) + .11*math.sin(phase*2.002)
            envelope *= .91 + .09*math.cos(TAU*6*t)
        samples.append(sample*envelope)
    return samples


def sound(kind, notes, state):
    dry = [0.0] * round(1.5 * RATE)
    for start, frequency, duration, gain in notes:
        offset = round(start*RATE)
        for i, sample in enumerate(voice(kind, frequency, duration)):
            dry[offset+i] += sample*gain
    mixed = dry[:]
    # Short, quiet reflections add space without a long notification tail.
    for seconds, gain in ((.053, .105), (.097, .060), (.151, .032)):
        delay = round(seconds*RATE)
        for i in range(len(dry)-delay):
            mixed[i+delay] += dry[i]*gain
    mean = sum(mixed)/len(mixed)
    mixed = [x-mean for x in mixed]
    fade = round(.012*RATE)
    for i in range(fade):
        factor = .5-.5*math.cos(math.pi*i/(fade-1))
        mixed[i] *= factor
        mixed[-1-i] *= factor
    rms = math.sqrt(sum(x*x for x in mixed)/len(mixed))
    relative = {'running': .87, 'question': 1.0, 'done': .95}[state]
    gain = min((10**(-22/20))*relative/rms, .34/max(abs(x) for x in mixed))
    return [x*gain for x in mixed]


def save(name, samples):
    assert samples and all(math.isfinite(x) for x in samples)
    assert max(abs(x) for x in samples) < .35
    assert abs(samples[0]) < 1e-8 and abs(samples[-1]) < 1e-8
    pcm = array.array('h', (round(x*32767) for x in samples))
    if sys.byteorder != 'little':
        pcm.byteswap()
    path = ROOT/name
    with wave.open(str(path), 'wb') as stream:
        stream.setparams((1, 2, RATE, 0, 'NONE', 'not compressed'))
        stream.writeframes(pcm.tobytes())
    with wave.open(str(path), 'rb') as check:
        assert (check.getnchannels(), check.getsampwidth(), check.getframerate(), check.getnframes()) == (1, 2, RATE, len(samples))
    rms = math.sqrt(sum(x*x for x in samples)/len(samples))
    return {'file': name, 'seconds': len(samples)/RATE,
            'peak_dbfs': round(20*math.log10(max(abs(x) for x in samples)), 2),
            'rms_dbfs': round(20*math.log10(rms), 2),
            'dc_offset': round(sum(samples)/len(samples), 7),
            'clipped_samples': 0, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


def main():
    manifest = {'format': 'PCM signed 16-bit little-endian mono WAV, 16000 Hz',
                'original_synthesis': True, 'audition_order': list(STATES),
                'audition_onsets_seconds': [.45, 3.05, 5.65], 'palettes': {}}
    for key, (name, kind, motifs) in PALETTES.items():
        preview = [0.0] * round(8.2*RATE)
        items = []
        for index, state in enumerate(STATES):
            samples = sound(kind, motifs[state], state)
            items.append(save(f'{key}-{state}.wav', samples))
            start = round((.45+index*2.6)*RATE)
            preview[start:start+len(samples)] = samples
        audition = save(f'{key}-preview.wav', preview)
        manifest['palettes'][key] = {'name': name, 'sounds': items, 'preview': audition}
        print(f'{key} {name}: three sounds + {audition["seconds"]}s audition; checks passed')
    (ROOT/'manifest.json').write_text(json.dumps(manifest, indent=2, ensure_ascii=False), encoding='utf8')
    with zipfile.ZipFile(ROOT/'codex-sound-auditions.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(list(ROOT.glob('*.wav')) + list(ROOT.glob('*.mp3'))):
            archive.write(path, path.name)
        for name in ('manifest.json', 'README.zh-CN.md', 'generate.py', 'index.html'):
            archive.write(ROOT/name, name)


if __name__ == '__main__':
    main()
