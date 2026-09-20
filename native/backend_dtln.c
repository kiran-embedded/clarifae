/*
 * backend_dtln.c — OPTIONAL "Balanced" engine (mode 2). EXPERIMENTAL.
 *
 * Builds ONLY when CLARIFAE_WITH_DTLN is defined (default OFF). Requires the
 * TensorFlow Lite C API (libtensorflowlite_c.so + tensorflow/lite/c/c_api.h)
 * and the two pretrained DTLN models at:
 *     /data/adb/clarifae/models/dtln/model_1.tflite   (spectral mask, stage 1)
 *     /data/adb/clarifae/models/dtln/model_2.tflite   (time-domain, stage 2)
 * obtained via scripts/fetch_models.sh (breizhn/DTLN pretrained_model/).
 *
 * Implements the DTLN real-time recipe (cf. real_time_processing_tf_lite.py):
 * 16 kHz mono, block_len=512, block_shift=128, overlap-add, LSTM state carried
 * between the two interpreters. Non-16k / multi-channel streams are resampled
 * and down-mixed to mono, enhanced, then copied to every output channel.
 *
 * If the runtime or models are absent, create() returns NULL so the dispatcher
 * transparently falls back to rnnoise.
 */
#ifdef CLARIFAE_WITH_DTLN

#include "backend.h"
#include "ringbuf.h"
#include "resampler.h"
#include "log.h"

#include "tensorflow/lite/c/c_api.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define DTLN_RATE   16000
#define BLOCK_LEN   512
#define BLOCK_SHIFT 128
#define NBINS       (BLOCK_LEN / 2 + 1)   /* 257 */
#define M1_PATH "/data/adb/clarifae/models/dtln/model_1.tflite"
#define M2_PATH "/data/adb/clarifae/models/dtln/model_2.tflite"

/* ---- small radix-2 complex FFT (size 512) ---- */
static void fft512(float* re, float* im, int inv) {
    const int n = BLOCK_LEN;
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t;
                     t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = 2.0 * M_PI / len * (inv ? 1.0 : -1.0);
        float wr = (float)cos(ang), wi = (float)sin(ang);
        for (int i = 0; i < n; i += len) {
            float cr = 1.0f, ci = 0.0f;
            for (int k = 0; k < len / 2; k++) {
                float ur = re[i + k], ui = im[i + k];
                float vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci;
                float vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
                re[i + k] = ur + vr; im[i + k] = ui + vi;
                re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
                float ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr; cr = ncr;
            }
        }
    }
    if (inv) for (int i = 0; i < n; i++) { re[i] /= n; im[i] /= n; }
}

typedef struct {
    TfLiteModel* m1; TfLiteModel* m2;
    TfLiteInterpreter* i1; TfLiteInterpreter* i2;
    TfLiteInterpreterOptions* o1; TfLiteInterpreterOptions* o2;
    int mag_in1, st_in1, mask_out1, st_out1;  /* tensor indices for stage 1 */
    int blk_in2, st_in2, blk_out2, st_out2;   /* tensor indices for stage 2 */
    int st1_len, st2_len;

    float in_block[BLOCK_LEN];
    float out_block[BLOCK_LEN];
    float* st1; float* st2;

    resampler_t* up;     /* session -> 16k */
    resampler_t* down;   /* 16k -> session */
    ringbuf_t in16, out16, outq;

    int channels;
    int sample_rate;
    float scratch[8192];
} dtln_state_t;

/* find the input tensor whose last dim == want (mag=257), the other is state */
static void classify_inputs(TfLiteInterpreter* it, int want, int* idx_match, int* idx_other) {
    int n = TfLiteInterpreterGetInputTensorCount(it);
    *idx_match = 0; *idx_other = (n > 1) ? 1 : 0;
    for (int i = 0; i < n; i++) {
        const TfLiteTensor* t = TfLiteInterpreterGetInputTensor(it, i);
        int nd = TfLiteTensorNumDims(t);
        int last = TfLiteTensorDim(t, nd - 1);
        if (last == want) *idx_match = i; else *idx_other = i;
    }
}
static void classify_outputs(TfLiteInterpreter* it, int want, int* idx_match, int* idx_other) {
    int n = TfLiteInterpreterGetOutputTensorCount(it);
    *idx_match = 0; *idx_other = (n > 1) ? 1 : 0;
    for (int i = 0; i < n; i++) {
        const TfLiteTensor* t = TfLiteInterpreterGetOutputTensor(it, i);
        int nd = TfLiteTensorNumDims(t);
        int last = TfLiteTensorDim(t, nd - 1);
        if (last == want) *idx_match = i; else *idx_other = i;
    }
}

static TfLiteInterpreter* make_interp(const char* path, TfLiteModel** pm,
                                      TfLiteInterpreterOptions** po) {
    TfLiteModel* m = TfLiteModelCreateFromFile(path);
    if (!m) return NULL;
    TfLiteInterpreterOptions* o = TfLiteInterpreterOptionsCreate();
    TfLiteInterpreterOptionsSetNumThreads(o, 1);
    TfLiteInterpreter* it = TfLiteInterpreterCreate(m, o);
    if (!it || TfLiteInterpreterAllocateTensors(it) != kTfLiteOk) {
        if (it) TfLiteInterpreterDelete(it);
        TfLiteInterpreterOptionsDelete(o);
        TfLiteModelDelete(m);
        return NULL;
    }
    *pm = m; *po = o;
    return it;
}

static void* dtln_create(int sample_rate, int channels, char* err, int errlen) {
    dtln_state_t* s = (dtln_state_t*)calloc(1, sizeof(*s));
    if (!s) { snprintf(err, errlen, "oom"); return NULL; }
    s->channels = channels < 1 ? 1 : channels;
    s->sample_rate = sample_rate;

    s->i1 = make_interp(M1_PATH, &s->m1, &s->o1);
    s->i2 = make_interp(M2_PATH, &s->m2, &s->o2);
    if (!s->i1 || !s->i2) { snprintf(err, errlen, "tflite model load failed"); goto fail; }

    classify_inputs(s->i1, NBINS, &s->mag_in1, &s->st_in1);
    classify_outputs(s->i1, NBINS, &s->mask_out1, &s->st_out1);
    classify_inputs(s->i2, BLOCK_LEN, &s->blk_in2, &s->st_in2);
    classify_outputs(s->i2, BLOCK_LEN, &s->blk_out2, &s->st_out2);

    s->st1_len = (int)(TfLiteTensorByteSize(TfLiteInterpreterGetInputTensor(s->i1, s->st_in1)) / sizeof(float));
    s->st2_len = (int)(TfLiteTensorByteSize(TfLiteInterpreterGetInputTensor(s->i2, s->st_in2)) / sizeof(float));
    s->st1 = (float*)calloc((size_t)s->st1_len, sizeof(float));
    s->st2 = (float*)calloc((size_t)s->st2_len, sizeof(float));

    s->up = rs_create(sample_rate, DTLN_RATE);
    s->down = rs_create(DTLN_RATE, sample_rate);
    if (!s->st1 || !s->st2 || !s->up || !s->down ||
        rb_init(&s->in16, 16384) || rb_init(&s->out16, 16384) || rb_init(&s->outq, 16384)) {
        snprintf(err, errlen, "oom buffers"); goto fail;
    }
    CLARIFAE_LOGI("dtln backend ready (%d Hz)", sample_rate);
    return s;
fail:
    if (s->i1) TfLiteInterpreterDelete(s->i1);
    if (s->i2) TfLiteInterpreterDelete(s->i2);
    if (s->o1) TfLiteInterpreterOptionsDelete(s->o1);
    if (s->o2) TfLiteInterpreterOptionsDelete(s->o2);
    if (s->m1) TfLiteModelDelete(s->m1);
    if (s->m2) TfLiteModelDelete(s->m2);
    free(s->st1); free(s->st2);
    rs_destroy(s->up); rs_destroy(s->down);
    rb_free(&s->in16); rb_free(&s->out16); rb_free(&s->outq);
    free(s);
    return NULL;
}

static void dtln_block(dtln_state_t* s) {
    /* rfft of in_block */
    float re[BLOCK_LEN], im[BLOCK_LEN];
    for (int i = 0; i < BLOCK_LEN; i++) { re[i] = s->in_block[i] / 32768.0f; im[i] = 0.0f; }
    fft512(re, im, 0);
    float mag[NBINS], ph[NBINS];
    for (int i = 0; i < NBINS; i++) { mag[i] = hypotf(re[i], im[i]); ph[i] = atan2f(im[i], re[i]); }

    /* stage 1: mask */
    TfLiteTensorCopyFromBuffer(TfLiteInterpreterGetInputTensor(s->i1, s->mag_in1), mag, sizeof(mag));
    TfLiteTensorCopyFromBuffer(TfLiteInterpreterGetInputTensor(s->i1, s->st_in1), s->st1,
                               (size_t)s->st1_len * sizeof(float));
    TfLiteInterpreterInvoke(s->i1);
    float mask[NBINS];
    TfLiteTensorCopyToBuffer(TfLiteInterpreterGetOutputTensor(s->i1, s->mask_out1), mask, sizeof(mask));
    TfLiteTensorCopyToBuffer(TfLiteInterpreterGetOutputTensor(s->i1, s->st_out1), s->st1,
                             (size_t)s->st1_len * sizeof(float));

    /* apply mask, irfft */
    for (int i = 0; i < NBINS; i++) { float m = mag[i] * mask[i]; re[i] = m * cosf(ph[i]); im[i] = m * sinf(ph[i]); }
    for (int i = 1; i < BLOCK_LEN / 2; i++) { re[BLOCK_LEN - i] = re[i]; im[BLOCK_LEN - i] = -im[i]; }
    fft512(re, im, 1);
    float est[BLOCK_LEN];
    for (int i = 0; i < BLOCK_LEN; i++) est[i] = re[i] * 32768.0f;

    /* stage 2: time-domain refinement */
    TfLiteTensorCopyFromBuffer(TfLiteInterpreterGetInputTensor(s->i2, s->blk_in2), est, sizeof(est));
    TfLiteTensorCopyFromBuffer(TfLiteInterpreterGetInputTensor(s->i2, s->st_in2), s->st2,
                               (size_t)s->st2_len * sizeof(float));
    TfLiteInterpreterInvoke(s->i2);
    float outb[BLOCK_LEN];
    TfLiteTensorCopyToBuffer(TfLiteInterpreterGetOutputTensor(s->i2, s->blk_out2), outb, sizeof(outb));
    TfLiteTensorCopyToBuffer(TfLiteInterpreterGetOutputTensor(s->i2, s->st_out2), s->st2,
                             (size_t)s->st2_len * sizeof(float));

    /* overlap-add */
    memmove(s->out_block, s->out_block + BLOCK_SHIFT, (BLOCK_LEN - BLOCK_SHIFT) * sizeof(float));
    memset(s->out_block + BLOCK_LEN - BLOCK_SHIFT, 0, BLOCK_SHIFT * sizeof(float));
    for (int i = 0; i < BLOCK_LEN; i++) s->out_block[i] += outb[i];
    /* the first BLOCK_SHIFT samples are now finalized */
    rb_write(&s->out16, s->out_block, BLOCK_SHIFT);
}

static int dtln_process(void* st, float* inout, int frames) {
    dtln_state_t* s = (dtln_state_t*)st;
    if (!s || frames <= 0) return 0;
    int ch = s->channels;

    /* down-mix to mono session-rate */
    for (int i = 0; i < frames; i++) {
        float acc = 0.0f;
        for (int c = 0; c < ch; c++) acc += inout[(size_t)i * ch + c];
        s->scratch[i % 8192] = acc / (float)ch;   /* chunked below */
    }
    /* process in mono chunks to bound scratch */
    int off = 0;
    while (off < frames) {
        int n = frames - off; if (n > 4096) n = 4096;
        float mono[4096];
        for (int i = 0; i < n; i++) {
            float acc = 0.0f;
            for (int c = 0; c < ch; c++) acc += inout[(size_t)(off + i) * ch + c];
            mono[i] = acc / (float)ch;
        }
        int outn = 0;
        rs_process(s->up, mono, n, s->scratch, 8192, &outn);
        rb_write(&s->in16, s->scratch, outn);

        while (rb_avail(&s->in16) >= BLOCK_SHIFT) {
            memmove(s->in_block, s->in_block + BLOCK_SHIFT, (BLOCK_LEN - BLOCK_SHIFT) * sizeof(float));
            rb_read(&s->in16, s->in_block + BLOCK_LEN - BLOCK_SHIFT, BLOCK_SHIFT);
            dtln_block(s);
        }
        while (rb_avail(&s->out16) >= BLOCK_SHIFT) {
            float tmp[BLOCK_SHIFT];
            rb_read(&s->out16, tmp, BLOCK_SHIFT);
            int dn = 0;
            rs_process(s->down, tmp, BLOCK_SHIFT, s->scratch, 8192, &dn);
            rb_write(&s->outq, s->scratch, dn);
        }
        off += n;
    }

    for (int i = 0; i < frames; i++) {
        float v;
        if (rb_read(&s->outq, &v, 1) == 1) {
            if (v < -32768.0f) v = -32768.0f; else if (v > 32767.0f) v = 32767.0f;
            for (int c = 0; c < ch; c++) inout[(size_t)i * ch + c] = v;
        }
    }
    return 0;
}

static void dtln_set_params(void* st, float strength, float atten_db, float vad,
                            int highpass, int autogain) {
    (void)st; (void)strength; (void)atten_db; (void)vad; (void)highpass; (void)autogain;
    /* DTLN exposes no live knobs in this build; wet/dry could be added here. */
}

static void dtln_reset(void* st) {
    dtln_state_t* s = (dtln_state_t*)st;
    if (!s) return;
    memset(s->in_block, 0, sizeof(s->in_block));
    memset(s->out_block, 0, sizeof(s->out_block));
    if (s->st1) memset(s->st1, 0, (size_t)s->st1_len * sizeof(float));
    if (s->st2) memset(s->st2, 0, (size_t)s->st2_len * sizeof(float));
    rb_reset(&s->in16); rb_reset(&s->out16); rb_reset(&s->outq);
    rs_reset(s->up); rs_reset(s->down);
}

static void dtln_destroy(void* st) {
    dtln_state_t* s = (dtln_state_t*)st;
    if (!s) return;
    if (s->i1) TfLiteInterpreterDelete(s->i1);
    if (s->i2) TfLiteInterpreterDelete(s->i2);
    if (s->o1) TfLiteInterpreterOptionsDelete(s->o1);
    if (s->o2) TfLiteInterpreterOptionsDelete(s->o2);
    if (s->m1) TfLiteModelDelete(s->m1);
    if (s->m2) TfLiteModelDelete(s->m2);
    free(s->st1); free(s->st2);
    rs_destroy(s->up); rs_destroy(s->down);
    rb_free(&s->in16); rb_free(&s->out16); rb_free(&s->outq);
    free(s);
}

static const clarifae_backend_t kDtln = {
    .name = "dtln",
    .create = dtln_create,
    .process = dtln_process,
    .set_params = dtln_set_params,
    .reset = dtln_reset,
    .destroy = dtln_destroy,
};

const clarifae_backend_t* clarifae_backend_dtln(void) { return &kDtln; }

#endif /* CLARIFAE_WITH_DTLN */
