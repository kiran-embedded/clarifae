/*
 * backend_rnnoise.c — Clarifae's shipping engine (mode 1, "Low latency").
 *
 * Wraps Xiph RNNoise (BSD-3-Clause). RNNoise is a compact RNN denoiser that runs
 * strictly at 48 kHz on 10 ms (480-sample) mono frames, with samples expressed
 * as floats in the int16 range. This backend adapts an arbitrary capture stream
 * to that contract:
 *
 *   deinterleave -> [optional high-pass] -> resample to 48k -> 480-frame buffer
 *     -> rnnoise_process_frame -> wet/dry mix bounded by the attenuation limit
 *     -> [optional auto-gain] -> resample back to session rate -> reinterleave
 *
 * The 480-frame buffering plus resampling introduces a small fixed latency
 * (~10 ms + resampler). Until the pipeline primes, output falls back to the dry
 * input so there are no clicks.
 *
 * Also hosts clarifae_backend_for_mode() (the dispatcher), since rnnoise is the
 * one backend always linked in.
 */
#include "backend.h"
#include "ringbuf.h"
#include "resampler.h"
#include "log.h"

#include "rnnoise.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define RNN_RATE      48000
#define MAX_FRAME     480     /* RNNoise frame size (10 ms @ 48 kHz) */
#define RING_CAP      16384
#define IN_STEP       256     /* session-rate samples resampled per inner step */
#define SCRATCH_CAP   4096

typedef struct {
    DenoiseState* st;
    resampler_t*  up;     /* session_rate -> 48000 */
    resampler_t*  down;   /* 48000 -> session_rate */
    ringbuf_t     in48;   /* 48k samples awaiting framing */
    ringbuf_t     out48;  /* 48k processed samples awaiting downsample */
    ringbuf_t     outq;   /* session-rate processed samples awaiting output */
    float         hp_x1;  /* high-pass history */
    float         hp_y1;
    float         agc_g;  /* auto-gain smoothed gain */
    float         frame_in[MAX_FRAME];
    float         frame_out[MAX_FRAME];
} rnn_chan_t;

typedef struct {
    int          channels;
    int          sample_rate;
    int          frame;       /* rnnoise_get_frame_size() */
    rnn_chan_t*  ch;

    /* params */
    float strength;   /* 0..1 wet amount */
    float atten_lin;  /* min retained level when suppressing = 10^(-dB/20) */
    float vad;        /* 0..1 voice-activity gate */
    int   highpass;
    int   autogain;

    /* shared scratch (audio thread only) */
    float scratch48[SCRATCH_CAP];
    float scratchSr[SCRATCH_CAP];
    float dch[8192];          /* one channel's deinterleaved session samples */
} rnn_state_t;

static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static void chan_free(rnn_chan_t* c) {
    if (c->st)   rnnoise_destroy(c->st);
    if (c->up)   rs_destroy(c->up);
    if (c->down) rs_destroy(c->down);
    rb_free(&c->in48);
    rb_free(&c->out48);
    rb_free(&c->outq);
    memset(c, 0, sizeof(*c));
}

static void* rnn_create(int sample_rate, int channels, char* err, int errlen) {
    if (channels < 1) channels = 1;
    if (sample_rate < 8000 || sample_rate > 192000) {
        snprintf(err, errlen, "unsupported sample rate %d", sample_rate);
        return NULL;
    }

    rnn_state_t* s = (rnn_state_t*)calloc(1, sizeof(*s));
    if (!s) { snprintf(err, errlen, "oom state"); return NULL; }

    s->channels    = channels;
    s->sample_rate = sample_rate;
    s->frame       = rnnoise_get_frame_size();
    if (s->frame <= 0 || s->frame > MAX_FRAME) s->frame = MAX_FRAME;

    s->strength = 0.7f;
    s->atten_lin = powf(10.0f, -24.0f / 20.0f);
    s->vad = 0.5f;
    s->highpass = 1;
    s->autogain = 0;

    s->ch = (rnn_chan_t*)calloc((size_t)channels, sizeof(rnn_chan_t));
    if (!s->ch) { free(s); snprintf(err, errlen, "oom chans"); return NULL; }

    for (int i = 0; i < channels; i++) {
        rnn_chan_t* c = &s->ch[i];
        c->st = rnnoise_create(NULL);
        c->up = rs_create(sample_rate, RNN_RATE);
        c->down = rs_create(RNN_RATE, sample_rate);
        c->agc_g = 1.0f;
        if (!c->st || !c->up || !c->down ||
            rb_init(&c->in48, RING_CAP) || rb_init(&c->out48, RING_CAP) ||
            rb_init(&c->outq, RING_CAP)) {
            snprintf(err, errlen, "oom channel %d", i);
            for (int j = 0; j <= i; j++) chan_free(&s->ch[j]);
            free(s->ch);
            free(s);
            return NULL;
        }
    }

    CLARIFAE_LOGI("rnnoise backend: %d Hz, %d ch, frame=%d", sample_rate, channels, s->frame);
    return s;
}

static void rnn_set_params(void* st, float strength, float atten_db, float vad,
                           int highpass, int autogain) {
    rnn_state_t* s = (rnn_state_t*)st;
    if (!s) return;
    s->strength  = clampf(strength, 0.0f, 1.0f);
    s->atten_lin = powf(10.0f, -clampf(atten_db, 0.0f, 60.0f) / 20.0f);
    s->vad       = clampf(vad, 0.0f, 1.0f);
    s->highpass  = highpass ? 1 : 0;
    s->autogain  = autogain ? 1 : 0;
}

static void rnn_reset(void* st) {
    rnn_state_t* s = (rnn_state_t*)st;
    if (!s) return;
    for (int i = 0; i < s->channels; i++) {
        rnn_chan_t* c = &s->ch[i];
        rb_reset(&c->in48);
        rb_reset(&c->out48);
        rb_reset(&c->outq);
        rs_reset(c->up);
        rs_reset(c->down);
        c->hp_x1 = c->hp_y1 = 0.0f;
        c->agc_g = 1.0f;
        if (c->st) rnnoise_init(c->st, NULL);
    }
}

static void rnn_destroy(void* st) {
    rnn_state_t* s = (rnn_state_t*)st;
    if (!s) return;
    for (int i = 0; i < s->channels; i++) chan_free(&s->ch[i]);
    free(s->ch);
    free(s);
}

/* Process one 480-sample 48 kHz frame in place (frame_in -> frame_out -> mix). */
static void process_frame(rnn_state_t* s, rnn_chan_t* c) {
    float vadprob = rnnoise_process_frame(c->st, c->frame_out, c->frame_in);

    float w = s->strength;
    float amax = s->atten_lin;       /* retained fraction floor */
    for (int i = 0; i < s->frame; i++) {
        float dry = c->frame_in[i];
        float wet = c->frame_out[i];
        float mixed = dry + (wet - dry) * w;       /* wet/dry blend */
        /* bound suppression depth: never remove more than (1-amax)*|dry| */
        float removed = dry - mixed;
        float maxrm = (1.0f - amax) * fabsf(dry);
        if (removed >  maxrm) removed =  maxrm;
        if (removed < -maxrm) removed = -maxrm;
        c->frame_out[i] = dry - removed;
    }

    if (s->autogain) {
        float acc = 0.0f;
        for (int i = 0; i < s->frame; i++) acc += c->frame_out[i] * c->frame_out[i];
        float rms = sqrtf(acc / (float)s->frame);
        float desired;
        if (vadprob > s->vad && rms > 1.0f) {
            desired = clampf(4000.0f / rms, 1.0f, 4.0f);   /* boost speech only */
            c->agc_g += (desired - c->agc_g) * 0.05f;       /* slow attack */
        } else {
            c->agc_g += (1.0f - c->agc_g) * 0.02f;          /* relax in silence */
        }
        for (int i = 0; i < s->frame; i++)
            c->frame_out[i] = clampf(c->frame_out[i] * c->agc_g, -32768.0f, 32767.0f);
    }
}

static int rnn_process(void* st, float* inout, int frames) {
    rnn_state_t* s = (rnn_state_t*)st;
    if (!s || frames <= 0) return 0;
    int chn = s->channels;

    for (int ci = 0; ci < chn; ci++) {
        rnn_chan_t* c = &s->ch[ci];

        /* 1. deinterleave this channel (chunked to bound dch[]) */
        int off = 0;
        while (off < frames) {
            int n = frames - off;
            if (n > (int)(sizeof(s->dch) / sizeof(float))) n = (int)(sizeof(s->dch) / sizeof(float));
            for (int i = 0; i < n; i++) s->dch[i] = inout[(size_t)(off + i) * chn + ci];

            /* 2. optional high-pass (DC / rumble removal) at session rate */
            if (s->highpass) {
                const float a = 0.995f;
                for (int i = 0; i < n; i++) {
                    float x = s->dch[i];
                    float y = a * (c->hp_y1 + x - c->hp_x1);
                    c->hp_x1 = x;
                    c->hp_y1 = y;
                    s->dch[i] = y;
                }
            }

            /* 3. resample session-rate -> 48k in IN_STEP sub-blocks, buffer it */
            int p = 0;
            while (p < n) {
                int m = n - p;
                if (m > IN_STEP) m = IN_STEP;
                int outn = 0;
                rs_process(c->up, s->dch + p, m, s->scratch48, SCRATCH_CAP, &outn);
                rb_write(&c->in48, s->scratch48, outn);
                p += m;

                /* 4. drain whole 480-frames -> denoise -> stage at 48k */
                while (rb_avail(&c->in48) >= s->frame) {
                    rb_read(&c->in48, c->frame_in, s->frame);
                    process_frame(s, c);
                    rb_write(&c->out48, c->frame_out, s->frame);
                }

                /* 5. resample processed 48k -> session rate -> output queue */
                while (rb_avail(&c->out48) >= s->frame) {
                    rb_read(&c->out48, s->scratch48, s->frame);
                    int dn = 0;
                    rs_process(c->down, s->scratch48, s->frame, s->scratchSr, SCRATCH_CAP, &dn);
                    rb_write(&c->outq, s->scratchSr, dn);
                }
            }
            off += n;
        }

        /* 6. emit `frames` output samples; dry passthrough on under-run (prime) */
        for (int i = 0; i < frames; i++) {
            float v;
            if (rb_read(&c->outq, &v, 1) == 1) {
                inout[(size_t)i * chn + ci] = clampf(v, -32768.0f, 32767.0f);
            }
            /* else: leave the existing (dry) sample in place — startup priming */
        }
    }
    return 0;
}

static const clarifae_backend_t kRnnoise = {
    .name       = "rnnoise",
    .create     = rnn_create,
    .process    = rnn_process,
    .set_params = rnn_set_params,
    .reset      = rnn_reset,
    .destroy    = rnn_destroy,
};

const clarifae_backend_t* clarifae_backend_rnnoise(void) { return &kRnnoise; }

/* ---- dispatcher (here because rnnoise is always linked) ---- */
const clarifae_backend_t* clarifae_backend_for_mode(int mode) {
    if (mode == 2 && clarifae_backend_dtln) {
        const clarifae_backend_t* b = clarifae_backend_dtln();
        if (b) return b;
        CLARIFAE_LOGW("DTLN requested but unavailable; using rnnoise");
    }
    if (mode == 3 && clarifae_backend_deepfilternet) {
        const clarifae_backend_t* b = clarifae_backend_deepfilternet();
        if (b) return b;
        CLARIFAE_LOGW("DeepFilterNet requested but unavailable; using rnnoise");
    }
    return clarifae_backend_rnnoise();
}
