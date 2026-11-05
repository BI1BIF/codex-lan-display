"""Original 8-bit-style chord notifications, using band-limited pulse voices.

Reuses the PCM writer and its boundary/level/read-back assertions. No dependencies.
"""
import json
import math

from generate import RATE, ROOT, STATES, TAU, save


def hz(midi):
    return 440 * 2 ** ((midi-69)/12)


def note(start, midi, length, gain=1):
    return start, hz(midi), length, gain


def chord(start, midis, length, gain):
    return [note(start, midi, length, gain) for midi in midis]


PALETTES = {
    'E': ('经典掌机', .5, 3400, {
        'running': chord(0, [60, 64, 67], .42, .15) + [note(i*.075, m, .14 if i<3 else .23, .65) for i, m in enumerate([72, 76, 79, 84])],
        'question': chord(0, [62, 67, 69], .68, .15) + [note(0, 79, .16, .62), note(.25, 81, .30, .72)],
        'done': [note(0, 72, .12, .65), note(.12, 76, .12, .65), note(.24, 79, .13, .68)] + chord(.39, [72, 76, 84], .43, .42),
    }),
    'F': ('像素冒险', .25, 3000, {
        'running': chord(0, [60, 67], .46, .19) + [note(0, 76, .12, .70), note(.13, 79, .12, .62), note(.26, 83, .21, .68)],
        'question': chord(0, [65, 69, 74], .72, .18) + [note(.04, 81, .12, .64), note(.26, 84, .12, .58), note(.45, 86, .20, .65)],
        'done': [note(0, 67, .12, .61), note(.13, 72, .12, .66), note(.26, 76, .13, .66)] + chord(.40, [60, 72, 76, 79], .48, .32),
    }),
    'G': ('柔和存档', .5, 1900, {
        'running': chord(0, [65, 69, 72], .48, .21) + [note(0, 77, .19, .55), note(.17, 81, .28, .58)],
        'question': chord(0, [67, 69, 74], .76, .19) + [note(.03, 79, .20, .50), note(.29, 81, .35, .64)],
        'done': [note(0, 76, .18, .52), note(.18, 79, .19, .52)] + chord(.35, [60, 67, 72, 76], .64, .34),
    }),
}


def pulse_voice(key, frequency, duration):
    _, duty, cutoff, _ = PALETTES[key]
    # Limit harmonics before sampling: a raw discontinuous square aliases and sounds harsher.
    harmonics = [(k, 2*math.sin(math.pi*k*duty)/(math.pi*k))
                 for k in range(1, int(cutoff/frequency)+1)]
    samples = []
    attack = .012 if key == 'G' else .005
    for i in range(round(duration*RATE)):
        t = i/RATE
        phase = TAU*frequency*t
        pulse = sum(weight*math.cos(k*phase-math.pi*k*duty) for k, weight in harmonics)
        triangle = (8/math.pi**2) * (math.sin(phase)-math.sin(3*phase)/9+math.sin(5*phase)/25)
        value = (.50*pulse+.50*triangle) if key == 'G' else (.84*pulse+.16*triangle)
        value = round(value*127)/127
        fade_in = min(1, t/attack)
        fade_out = min(1, max(0, (duration-t)/(.045 if key == 'G' else .022)))
        envelope = (.5-.5*math.cos(math.pi*fade_in))*(.5-.5*math.cos(math.pi*fade_out))
        envelope *= .55+.45*math.exp(-t/.075)
        samples.append(value*envelope)
    return samples


def sound(key, notes, state):
    data = [0.] * round(RATE*1.5)
    for start, frequency, duration, gain in notes:
        offset = round(start*RATE)
        for i, value in enumerate(pulse_voice(key, frequency, duration)):
            data[offset+i] += value*gain
    # Preserve the dry handheld feel; only the soft set gets a quiet, short reflection.
    if key == 'G':
        dry = data[:]
        delay = round(.072*RATE)
        for i in range(len(data)-delay):
            data[i+delay] += .07*dry[i]
    mean = sum(data)/len(data)
    data = [x-mean for x in data]
    fade = round(.012*RATE)
    for i in range(fade):
        gain = .5-.5*math.cos(math.pi*i/(fade-1))
        data[i] *= gain
        data[-i-1] *= gain
    rms = math.sqrt(sum(x*x for x in data)/len(data))
    target = 10**(-22/20)*{'running': .87, 'question': 1., 'done': .95}[state]
    gain = min(target/rms, .34/max(abs(x) for x in data))
    return [x*gain for x in data]


def main():
    manifest = {'style': '8-bit-style original pulse/triangle chord synthesis; 16-bit PCM export',
                'format': '16000 Hz mono PCM16 WAV', 'order': list(STATES),
                'onsets_seconds': [.45, 3.05, 5.65], 'palettes': {}}
    for key, (name, *_rest) in PALETTES.items():
        preview = [0.] * round(8.2*RATE)
        sounds = []
        for index, state in enumerate(STATES):
            samples = sound(key, PALETTES[key][3][state], state)
            sounds.append(save(f'{key}-{state}.wav', samples))
            offset = round((.45+2.6*index)*RATE)
            preview[offset:offset+len(samples)] = samples
        manifest['palettes'][key] = {'name': name, 'sounds': sounds, 'preview': save(f'{key}-preview.wav', preview)}
        print(f'PASS {key}: all three PCM files and preview, levels and boundaries checked')
    (ROOT/'manifest-chiptune.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf8')


if __name__ == '__main__':
    main()
