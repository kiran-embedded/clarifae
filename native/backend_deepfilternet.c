/*
 * backend_deepfilternet.c — OPTIONAL "Maximum clarity" engine (mode 3). EXPERIMENTAL.
 *
 * Builds ONLY when CLARIFAE_WITH_DF is defined (default OFF). Requires libdf
 * (DeepFilterNet's Rust C-FFI, MIT/Apache-2.0) cross-compiled for Android, plus
 * an exported DeepFilterNet model at:
 *     /data/adb/clarifae/models/df/   (set DF_MODEL_PATH to the .tar.gz)
 *
 * The C API below mirrors the symbols exported by the official deep-filter
 * LADSPA plugin / the `deep_filter` capi crate. Exact names can differ between
 * libdf builds — adjust the extern block and the CMake link if your build
 * differs. DeepFilterNet is full-band 48 kHz; non-48k / multi-channel input is
 * resampled and down-mixed to mono, enhanced, then fanned back out.
 *
 * If libdf or the model is missing, create() returns NULL and the dispatcher
 * falls back to rnnoise.
 */
#ifdef CLARIFAE_WITH_DF

#include "backend.h"
#include "ringbuf.h"
#include "resampler.h"
#include "log.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

/* ---- libdf C FFI (provided by the DeepFilterNet capi build) ---- */
typedef struct DFState DFState;
extern DFState*  df_create(const char* path, float atten_lim_db);
extern ptrdiff_t df_get_frame_length(DFState* st);
extern float     df_process_frame(DFState* st, float* input, float* output);
extern void      df_set_atten_lim(DFState* st, float lim_db);
extern void      df_set_post_filter_beta(DFState* st, float beta);
extern void      df_free(DFState* st);

#ifndef DF_MODEL_PATH
#define DF_MODEL_PATH "/data/adb/clarifae/models/df/DeepFilterNet3_onnx.tar.gz"
#endif
#define DF_RATE 48000

typedef struct {
    DFState* df;
    int      frame;          /* df_get_frame_length() */
    int      channels;
    int      sample_rate;
    float    atten_db;
    resampler_t* up;         /* session -> 48k */
    resampler_t* down;       /* 48k -> session */
    ringbuf_t in48, out48, outq;
    float    scratch[8192];
    float*   fin;            /* frame buffers */
    float*   fout;
} df_state_t;

static void* df_be_create(int sample_rate, int channels, char* err, int errlen) {
    df_state_t* s = (df_state_t*)calloc(1, sizeof(*s));
    if (!s) { snprintf(err, errlen, "oom"); return NULL; }
    s->channels = channels < 1 ? 1 : channels;
    s->sample_rate = sample_rate;
    s->atten_db = 100.0f;   /* DF treats large limit as "no cap" */

    s->df = df_create(DF_MODEL_PATH, s->atten_db);
    if (!s->df) { snprintf(err, errlen, "df_create failed (model %s)", DF_MODEL_PATH); goto fail; }
    s->frame = (int)df_get_frame_length(s->df);
    if (s->frame <= 0 || s->frame > 4096) { snprintf(err, errlen, "bad df frame %d", s->frame); goto fail; }

    s->fin  = (float*)calloc((size_t)s->frame, sizeof(float));
    s->fout = (float*)calloc((size_t)s->frame, sizeof(float));
    s->up   = rs_create(sample_rate, DF_RATE);
    s->down = rs_create(DF_RATE, sample_rate);
    if (!s->fin || !s->fout || !s->up || !s->down ||
        rb_init(&s->in48, 16384) || rb_init(&s->out48, 16384) || rb_init(&s->outq, 16384)) {
        snprintf(err, errlen, "oom buffers"); goto fail;
    }
    CLARIFAE_LOGI("deepfilternet backend ready (%d Hz, frame=%d)", sample_rate, s->frame);
    return s;
fail:
    if (s->df) df_free(s->df);
    free(s->fin); free(s->fout);
    rs_destroy(s->up); rs_destroy(s->down);
    rb_free(&s->in48); rb_free(&s->out48); rb_free(&s->outq);
    free(s);
    return NULL;
}

static int df_be_process(void* st, float* inout, int frames) {
    df_state_t* s = (df_state_t*)st;
    if (!s || frames <= 0) return 0;
    int ch = s->channels;
    int off = 0;
    while (off < frames) {
        int n = frames - off; if (n > 4096) n = 4096;
        float mono[4096];
        for (int i = 0; i < n; i++) {
            float acc = 0.0f;
            for (int c = 0; c < ch; c++) acc += inout[(size_t)(off + i) * ch + c];
            mono[i] = acc / (float)ch / 32768.0f;   /* DF expects ~[-1,1] */
        }
        int outn = 0;
        rs_process(s->up, mono, n, s->scratch, 8192, &outn);
        rb_write(&s->in48, s->scratch, outn);

        while (rb_avail(&s->in48) >= s->frame) {
            rb_read(&s->in48, s->fin, s->frame);
            df_process_frame(s->df, s->fin, s->fout);
            rb_write(&s->out48, s->fout, s->frame);
        }
        while (rb_avail(&s->out48) >= s->frame) {
            rb_read(&s->out48, s->scratch, s->frame);
            int dn = 0;
            rs_process(s->down, s->scratch, s->frame, s->scratch + 4096, 4096, &dn);
            rb_write(&s->outq, s->scratch + 4096, dn);
        }
        off += n;
    }
    for (int i = 0; i < frames; i++) {
        float v;
        if (rb_read(&s->outq, &v, 1) == 1) {
            v *= 32768.0f;
            if (v < -32768.0f) v = -32768.0f; else if (v > 32767.0f) v = 32767.0f;
            for (int c = 0; c < ch; c++) inout[(size_t)i * ch + c] = v;
        }
    }
    return 0;
}

static void df_be_set_params(void* st, float strength, float atten_db, float vad,
                             int highpass, int autogain) {
    (void)strength; (void)vad; (void)highpass; (void)autogain;
    df_state_t* s = (df_state_t*)st;
    if (!s || !s->df) return;
    /* map 0 dB -> no suppression cap is awkward; treat 0 as "max clarity". */
    float lim = (atten_db <= 0.0f) ? 100.0f : atten_db;
    s->atten_db = lim;
    df_set_atten_lim(s->df, lim);
}

static void df_be_reset(void* st) {
    df_state_t* s = (df_state_t*)st;
    if (!s) return;
    rb_reset(&s->in48); rb_reset(&s->out48); rb_reset(&s->outq);
    rs_reset(s->up); rs_reset(s->down);
}

static void df_be_destroy(void* st) {
    df_state_t* s = (df_state_t*)st;
    if (!s) return;
    if (s->df) df_free(s->df);
    free(s->fin); free(s->fout);
    rs_destroy(s->up); rs_destroy(s->down);
    rb_free(&s->in48); rb_free(&s->out48); rb_free(&s->outq);
    free(s);
}

static const clarifae_backend_t kDf = {
    .name = "deepfilternet",
    .create = df_be_create,
    .process = df_be_process,
    .set_params = df_be_set_params,
    .reset = df_be_reset,
    .destroy = df_be_destroy,
};

const clarifae_backend_t* clarifae_backend_deepfilternet(void) { return &kDf; }

#endif /* CLARIFAE_WITH_DF */
