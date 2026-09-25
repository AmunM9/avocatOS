/*
 * Synthesized system sounds. The board has a speaker but no audio files:
 * each sound is a short list of sine notes with a soft attack and release
 * (no clicks between notes), rendered on the fly at 16 kHz.
 */
#include <math.h>
#include <string.h>
#include "avo_core.h"

#define SINE_BITS 8
#define SINE_N (1 << SINE_BITS)
#define ATTACK_MS 4
#define RELEASE_MS 8
#define DECAY_END 0.45f         /* level at the end of a note, before release */
#define CLICK_GAIN_DIV 3        /* the UI tick is quieter than the rest       */
#define MAX_GAIN 29000

static int16_t s_sine[SINE_N];
static bool s_sine_ready;

/* Note names used below (Hz). */
enum { A5 = 880, B5 = 988, C6 = 1047, D6 = 1175, E6 = 1319, G6 = 1568, A6 = 1760, C7 = 2093, G5 = 784 };

static const avo_note_t CLICK[] = { { 3200, 12 } };
static const avo_note_t NOTIFY[] = { { E6, 90 }, { 0, 25 }, { A6, 170 } };
static const avo_note_t CHARGE[] = { { C6, 90 }, { G6, 170 } };
static const avo_note_t ALARM[] = { { C6, 120 }, { E6, 120 }, { G6, 120 }, { C7, 240 }, { 0, 520 } };
static const avo_note_t TIMER[] = { { A5, 150 }, { 0, 70 }, { A5, 150 }, { 0, 70 }, { E6, 320 }, { 0, 700 } };
static const avo_note_t RING[] = { { G5, 180 }, { B5, 180 }, { D6, 320 }, { 0, 1200 } };

static const struct {
    const avo_note_t *notes;
    uint8_t count;
    bool loop;
} SOUNDS[AVO_SOUND_COUNT] = {
    [AVO_SOUND_CLICK] = { CLICK, 1, false },
    [AVO_SOUND_NOTIFY] = { NOTIFY, 3, false },
    [AVO_SOUND_CHARGE] = { CHARGE, 2, false },
    [AVO_SOUND_ALARM] = { ALARM, 5, true },
    [AVO_SOUND_TIMER] = { TIMER, 6, true },
    [AVO_SOUND_RING] = { RING, 4, true },
};

static void sine_init(void)
{
    if (s_sine_ready) {
        return;
    }
    for (int i = 0; i < SINE_N; i++) {
        s_sine[i] = (int16_t)lrintf(32767.0f * sinf(2.0f * 3.14159265f * (float)i / SINE_N));
    }
    s_sine_ready = true;
}

static uint32_t ms_to_samples(uint32_t ms) { return ms * AVO_SYNTH_RATE / 1000; }

bool avo_sound_loops(avo_sound_t id)
{
    return id < AVO_SOUND_COUNT && SOUNDS[id].loop;
}

uint32_t avo_sound_duration_ms(avo_sound_t id)
{
    if (id >= AVO_SOUND_COUNT) {
        return 0;
    }
    uint32_t ms = 0;
    for (uint8_t i = 0; i < SOUNDS[id].count; i++) {
        ms += SOUNDS[id].notes[i].ms;
    }
    return ms;
}

void avo_synth_start(avo_synth_t *s, avo_sound_t id, uint8_t volume)
{
    sine_init();
    memset(s, 0, sizeof *s);
    if (id >= AVO_SOUND_COUNT) {
        return;
    }
    s->notes = SOUNDS[id].notes;
    s->count = SOUNDS[id].count;
    s->loop = SOUNDS[id].loop;
    float v = (float)(volume > 100 ? 100 : volume) / 100.0f;
    s->gain = (int32_t)(MAX_GAIN * v * v); /* loudness grows roughly with the square */
    if (id == AVO_SOUND_CLICK) {
        s->gain /= CLICK_GAIN_DIV;
    }
}

bool avo_synth_done(const avo_synth_t *s)
{
    return s->notes == NULL || s->idx >= s->count;
}

/* Envelope 0..1 at sample `pos` of a note lasting `len` samples. */
static float envelope(uint32_t pos, uint32_t len)
{
    uint32_t atk = ms_to_samples(ATTACK_MS), rel = ms_to_samples(RELEASE_MS);
    if (len < atk + rel) {
        atk = rel = len / 2;
    }
    if (pos < atk) {
        return (float)pos / (float)atk;
    }
    float body = 1.0f - (1.0f - DECAY_END) * (float)(pos - atk) / (float)(len - atk);
    if (pos + rel >= len) {
        return body * (float)(len - pos) / (float)rel;
    }
    return body;
}

size_t avo_synth_render(avo_synth_t *s, int16_t *out, size_t frames)
{
    size_t i = 0;
    while (i < frames && !avo_synth_done(s)) {
        const avo_note_t *n = &s->notes[s->idx];
        uint32_t len = ms_to_samples(n->ms);
        if (s->pos >= len) {
            s->pos = 0;
            s->phase = 0;
            if (++s->idx >= s->count && s->loop) {
                s->idx = 0;
            }
            continue;
        }
        int16_t v = 0;
        if (n->hz && s->gain) {
            uint32_t idx = (s->phase >> 16) & (SINE_N - 1);
            v = (int16_t)((float)s_sine[idx] * envelope(s->pos, len) * (float)s->gain / 32767.0f);
            s->phase += (uint32_t)(((uint64_t)n->hz * SINE_N << 16) / AVO_SYNTH_RATE);
        }
        out[i++] = v;
        s->pos++;
    }
    if (i < frames) {
        memset(out + i, 0, (frames - i) * sizeof *out);
    }
    return i;
}
