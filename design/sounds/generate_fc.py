"""Original fast FC-inspired arrangements of E; no sampled game melodies."""
import json
import math
import random

from generate import RATE, ROOT, STATES, TAU, save
from generate_chiptune import hz


def voice(midi, duration, duty, triangle=False, long_tail=False):
    # Quantized console-style pitch and stepped envelope, with tiny click fades.
    frequency = hz(midi)
    divisor = 32 if triangle else 16
    timer = round(1789773 / (divisor * frequency) - 1)
    frequency = 1789773 / (divisor * (timer + 1))
    harmonics = range(1, int(5400 / frequency) + 1)
    result = []
    for i in range(round(duration * RATE)):
        t = i / RATE
        phase = TAU * frequency * t
        if triangle:
            value = 8 / math.pi**2 * sum(
                (-1)**((k-1)//2) * math.sin(k*phase) / k**2
                for k in harmonics if k % 2)
            envelope = .85
        else:
            value = sum(2 * math.sin(math.pi*k*duty) / (math.pi*k)
                        * math.cos(k*phase-math.pi*k*duty) for k in harmonics)
            envelope = max(2, 15 - int(t / (.022 if long_tail else .012))) / 15
        fade = min(1, t/.0015, max(0, (duration-t)/.002))
        result.append(value * envelope * fade)
    return result


def arrangement(key, state):
    # Each lane is monophonic: pulse lead, pulse harmony, triangle bass.
    fast = key == 'E3'
    beat = .075 if fast else .095
    if state == 'running':
        lead = [(0,72,1),(1,76,1),(2,79,1),(3,84,2)]
        harmony = [(0,60,1),(1,64,1),(2,67,1),(3,72,1),(4,67,1)]
        bass = [(0,48,2),(2,55,1),(3,60,2)]
    elif state == 'question':
        lead = [(0,79,1.2),(2,81,2)]
        harmony = [(0,62,1),(1,67,1),(2,69,1),(3,67,1)]
        bass = [(0,50,1),(2,57,2)]
    else:
        lead = [(0,72,1),(1,76,1),(2,79,1),(3,84,3)]
        harmony = [(0,60,1),(1,64,1),(2,67,1),(3,76,1),(4,79,1),(5,76,1)]
        bass = [(0,48,1),(1,55,1),(2,55,1),(3,48,3)]
    if key == 'E2':
        # Exactly two/two/four audible note events; accompaniment shares onsets.
        if state == 'running':
            lead = [(0,72,1),(1,79,2)]
            harmony = [(0,60,1),(1,67,2)]
            bass = [(0,48,1),(1,55,2)]
        elif state == 'question':
            harmony = [(0,62,1.2),(2,69,2)]
        else:
            harmony = [(0,60,1),(1,64,1),(2,67,1),(3,76,3)]
            bass = [(0,48,1),(1,52,1),(2,55,1),(3,48,3)]
        assert len({start for lane in (lead,harmony,bass) for start,_,_ in lane}) == (4 if state == 'done' else 2)
    data = [0.] * round(1.5 * RATE)
    for lane, gain, duty, triangle in [(lead,.66,.125 if fast else .25,False),
                                        (harmony,.25,.5,False),(bass,.35,.5,True)]:
        for start, midi, length in lane:
            long_tail = key == 'E2' and state == 'done' and start == 3
            duration = beat*length*.84 + (.18 if long_tail else 0)
            samples = voice(midi, duration, duty, triangle, long_tail)
            offset = round(start*beat*RATE)
            for i, value in enumerate(samples):
                data[offset+i] += gain*value
    if fast:
        # Very quiet short noise ticks add a bouncy game rhythm without a drum loop.
        rng = random.Random(1985)
        for start in (0,2,3):
            offset = round(start*beat*RATE)
            previous = 0
            for i in range(round(.018*RATE)):
                sample = rng.choice((-1,1))
                value = (sample-previous)*.022*math.exp(-i/(RATE*.004))
                data[offset+i] += value*min(1,i/12)
                previous = sample
    # Level-match previews, leave substantial headroom, and keep silence truly silent.
    rms = math.sqrt(sum(x*x for x in data)/len(data))
    target = 10**(-23/20)*{'running':.87,'question':1,'done':.95}[state]
    scale = min(target/rms,.34/max(map(abs,data)))
    data = [x*scale for x in data]
    data[0] = data[-1] = 0
    return data


def main():
    manifest = {'style':'original E motifs; fast dry pulse/triangle FC-inspired arrangement',
                'format':'16000 Hz mono PCM16 WAV','order':list(STATES),
                'onsets_seconds':[.45,3.05,5.65],'palettes':{}}
    for key,name in [('E2','红白机原味 · 快节奏'),('E3','跳跃冒险 · 更快')]:
        preview = [0.] * round(8.2*RATE)
        sounds = []
        for index,state in enumerate(STATES):
            samples = arrangement(key,state)
            sounds.append(save(f'{key}-{state}.wav',samples))
            offset = round((.45+2.6*index)*RATE)
            preview[offset:offset+len(samples)] = samples
            last = max(i for i,x in enumerate(samples) if abs(x)>.0001)/RATE
            assert last < (.72 if key == 'E2' and state == 'done' else .60), (key,state,last)
            print(f'PASS {key}/{state}: audible duration {last:.3f}s, no clipping')
        manifest['palettes'][key] = {'name':name,'sounds':sounds,
                                    'preview':save(f'{key}-preview.wav',preview)}
    (ROOT/'manifest-fc.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf8')


if __name__ == '__main__':
    main()
