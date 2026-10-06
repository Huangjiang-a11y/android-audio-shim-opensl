#include <jni.h>
#include <dlfcn.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#define MAX_INPUT_STREAMS   4
#define INPUT_QUEUE_BUFFERS 2
#define MAX_OUTPUT_STREAMS  16
#define QUEUE_BUFFERS       4

// ============ INPUT (OpenSL ES recorder) ============
typedef struct {
    SLObjectItf  engineObj;
    SLEngineItf  engine;
    SLObjectItf  recorderObj;
    SLRecordItf  record;
    SLAndroidSimpleBufferQueueItf bq;
    int32_t      channels;
    int32_t      frameSize;
    int16_t*     bufs[INPUT_QUEUE_BUFFERS];
    int32_t      bufBytes;
    int32_t      readBuf;
    pthread_cond_t  cond;
    pthread_mutex_t mutex;
    int          available;   // number of completed buffers ready to read
    int          stopping;    // set by stop()/close() to wake blocked readers
} InputCtx;

// ============ OUTPUT (OpenSL ES player) ============
typedef struct {
    SLObjectItf  engineObj;
    SLEngineItf  engine;
    SLObjectItf  mixObj;
    SLObjectItf  playerObj;
    SLPlayItf    play;
    SLAndroidSimpleBufferQueueItf bq;
    int32_t      channels;
    int32_t      frameSize;
    int16_t*     bufs[QUEUE_BUFFERS];
    int32_t      bufBytes;
    int32_t      writeBuf;
    float        gain;        /* linear gain, 1.0f = unity */
} OutputCtx;

static InputCtx*  g_inputs[MAX_INPUT_STREAMS];
static OutputCtx* g_outputs[MAX_OUTPUT_STREAMS];

static void recorderCallback(SLAndroidSimpleBufferQueueItf bq, void* ctx) {
    InputCtx* in = (InputCtx*)ctx;
    pthread_mutex_lock(&in->mutex);
    if (in->available < INPUT_QUEUE_BUFFERS) in->available++;
    pthread_cond_signal(&in->cond);
    pthread_mutex_unlock(&in->mutex);
}

JNIEXPORT jint JNICALL
Java_de_maxhenkel_shim_NativeAudio_open(JNIEnv* e, jclass c, jint rate, jint channels, jint frames) {
    int slot = -1;
    for (int i = 0; i < MAX_INPUT_STREAMS; i++)
        if (!g_inputs[i]) { slot = i; break; }
    if (slot < 0) return -400;

    InputCtx* ctx = (InputCtx*)calloc(1, sizeof(InputCtx));
    if (!ctx) return -401;

    ctx->channels  = channels > 0 ? channels : 1;
    ctx->frameSize = ctx->channels * 2;
    ctx->bufBytes  = (frames > 0 ? frames : 960) * ctx->frameSize;

    for (int i = 0; i < INPUT_QUEUE_BUFFERS; i++) {
        ctx->bufs[i] = (int16_t*)calloc(1, ctx->bufBytes);
        if (!ctx->bufs[i]) { free(ctx); return -402; }
    }

    pthread_cond_init(&ctx->cond, NULL);
    pthread_mutex_init(&ctx->mutex, NULL);

    jint rc = -500;

    if (slCreateEngine(&ctx->engineObj, 0, NULL, 0, NULL, NULL) != SL_RESULT_SUCCESS) { rc = -403; goto fail; }
    if ((*ctx->engineObj)->Realize(ctx->engineObj, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS) { rc = -404; goto fail; }
    if ((*ctx->engineObj)->GetInterface(ctx->engineObj, SL_IID_ENGINE, &ctx->engine) != SL_RESULT_SUCCESS) { rc = -405; goto fail; }

    {
        SLDataLocator_IODevice locator = {
            SL_DATALOCATOR_IODEVICE,
            SL_IODEVICE_AUDIOINPUT,
            SL_DEFAULTDEVICEID_AUDIOINPUT,
            NULL
        };
        SLDataFormat_PCM pcmFmt = {
            SL_DATAFORMAT_PCM,
            (SLuint32)ctx->channels,
            (SLuint32)(rate * 1000),
            SL_PCMSAMPLEFORMAT_FIXED_16,
            SL_PCMSAMPLEFORMAT_FIXED_16,
            ctx->channels == 2
                ? (SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT)
                : SL_SPEAKER_FRONT_CENTER,
            SL_BYTEORDER_LITTLEENDIAN
        };
        SLDataLocator_AndroidSimpleBufferQueue bqLoc = {
            SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE,
            INPUT_QUEUE_BUFFERS
        };
        /* FIX: mic (IODevice) is the SOURCE and carries NO format;
         *      the buffer queue is the SINK and carries the PCM format. */
        SLDataSource source = { &locator, NULL };
        SLDataSink   sink   = { &bqLoc,   &pcmFmt };
        const SLInterfaceID ids[1] = { SL_IID_ANDROIDSIMPLEBUFFERQUEUE };
        const SLboolean reqs[1] = { SL_BOOLEAN_TRUE };

        SLresult cr = (*ctx->engine)->CreateAudioRecorder(ctx->engine, &ctx->recorderObj,
                &source, &sink, 1, ids, reqs);
        if (cr != SL_RESULT_SUCCESS) { rc = -406000 - (jint)cr; goto fail; }
    }

    if ((*ctx->recorderObj)->Realize(ctx->recorderObj, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS) { rc = -407; goto fail; }
    if ((*ctx->recorderObj)->GetInterface(ctx->recorderObj,
            SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &ctx->bq) != SL_RESULT_SUCCESS) { rc = -408; goto fail; }
    if ((*ctx->recorderObj)->GetInterface(ctx->recorderObj,
            SL_IID_RECORD, &ctx->record) != SL_RESULT_SUCCESS) { rc = -409; goto fail; }

    if ((*ctx->bq)->RegisterCallback(ctx->bq, recorderCallback, ctx) != SL_RESULT_SUCCESS) { rc = -410; goto fail; }

    g_inputs[slot] = ctx;
    return slot;

fail:
    if (ctx->recorderObj) (*ctx->recorderObj)->Destroy(ctx->recorderObj);
    if (ctx->engineObj) (*ctx->engineObj)->Destroy(ctx->engineObj);
    for (int i = 0; i < INPUT_QUEUE_BUFFERS; i++) free(ctx->bufs[i]);
    pthread_cond_destroy(&ctx->cond);
    pthread_mutex_destroy(&ctx->mutex);
    free(ctx);
    (void)e;
    return rc;
}

JNIEXPORT jint JNICALL
Java_de_maxhenkel_shim_NativeAudio_start(JNIEnv* e, jclass c, jint h) {
    if (h < 0 || h >= MAX_INPUT_STREAMS || !g_inputs[h]) return -1;
    InputCtx* ctx = g_inputs[h];

    pthread_mutex_lock(&ctx->mutex);
    ctx->stopping  = 0;
    ctx->available = 0;
    pthread_mutex_unlock(&ctx->mutex);

    for (int i = 0; i < INPUT_QUEUE_BUFFERS; i++) {
        (*ctx->bq)->Enqueue(ctx->bq, ctx->bufs[i], (SLuint32)ctx->bufBytes);
    }

    return (jint)(*ctx->record)->SetRecordState(ctx->record, SL_RECORDSTATE_RECORDING);
}

JNIEXPORT jint JNICALL
Java_de_maxhenkel_shim_NativeAudio_stop(JNIEnv* e, jclass c, jint h) {
    if (h < 0 || h >= MAX_INPUT_STREAMS || !g_inputs[h]) return -1;
    InputCtx* ctx = g_inputs[h];
    SLresult r = (*ctx->record)->SetRecordState(ctx->record, SL_RECORDSTATE_STOPPED);

    /* Wake any thread blocked in read() so it can hit EOF instead of hanging */
    pthread_mutex_lock(&ctx->mutex);
    ctx->stopping = 1;
    pthread_cond_broadcast(&ctx->cond);
    pthread_mutex_unlock(&ctx->mutex);

    return (jint)r;
}

JNIEXPORT jint JNICALL
Java_de_maxhenkel_shim_NativeAudio_close(JNIEnv* e, jclass c, jint h) {
    if (h < 0 || h >= MAX_INPUT_STREAMS || !g_inputs[h]) return 0;
    InputCtx* ctx = g_inputs[h];

    /* Wake blocked readers first, then tear down */
    pthread_mutex_lock(&ctx->mutex);
    ctx->stopping = 1;
    pthread_cond_broadcast(&ctx->cond);
    pthread_mutex_unlock(&ctx->mutex);

    /* Give a woken reader a moment to bail out before destroying objects,
     * so it cannot touch the recorder after it is gone. */
    usleep(20000);

    (*ctx->recorderObj)->Destroy(ctx->recorderObj);
    (*ctx->engineObj)->Destroy(ctx->engineObj);
    for (int i = 0; i < INPUT_QUEUE_BUFFERS; i++) free(ctx->bufs[i]);
    pthread_cond_destroy(&ctx->cond);
    pthread_mutex_destroy(&ctx->mutex);
    free(ctx);
    g_inputs[h] = NULL;
    return 0;
}

JNIEXPORT jint JNICALL
Java_de_maxhenkel_shim_NativeAudio_read(JNIEnv* env, jclass c, jint h, jbyteArray buf, jint off, jint len) {
    if (h < 0 || h >= MAX_INPUT_STREAMS || !g_inputs[h]) return -1;
    InputCtx* ctx = g_inputs[h];

    pthread_mutex_lock(&ctx->mutex);
    while (ctx->available <= 0) {
        if (ctx->stopping) {
            /* Stream ended: no more data will ever arrive */
            pthread_mutex_unlock(&ctx->mutex);
            return 0;
        }
        pthread_cond_wait(&ctx->cond, &ctx->mutex);
    }
    ctx->available--;
    pthread_mutex_unlock(&ctx->mutex);

    jbyte* d = (*env)->GetByteArrayElements(env, buf, NULL);
    if (!d) return -2;

    int copyLen = len < ctx->bufBytes ? len : ctx->bufBytes;
    memcpy(d + off, ctx->bufs[ctx->readBuf % INPUT_QUEUE_BUFFERS], copyLen);
    (*env)->ReleaseByteArrayElements(env, buf, d, 0);

    (*ctx->bq)->Enqueue(ctx->bq,
        ctx->bufs[ctx->readBuf % INPUT_QUEUE_BUFFERS],
        (SLuint32)ctx->bufBytes);
    ctx->readBuf++;

    return copyLen;
}

JNIEXPORT jint JNICALL
Java_de_maxhenkel_shim_NativeAudio_available(JNIEnv* env, jclass c, jint h) {
    if (h < 0 || h >= MAX_INPUT_STREAMS || !g_inputs[h]) return 0;
    InputCtx* ctx = g_inputs[h];
    int n;
    pthread_mutex_lock(&ctx->mutex);
    n = ctx->available * ctx->bufBytes;
    pthread_mutex_unlock(&ctx->mutex);
    return (jint)n;
}

// ============ OUTPUT (unchanged OpenSL ES player) ============

JNIEXPORT jint JNICALL
Java_de_maxhenkel_shim_NativeAudioOutput_open(JNIEnv* e, jclass c, jint rate, jint channels, jint frames) {
    int slot = -1;
    for (int i = 0; i < MAX_OUTPUT_STREAMS; i++)
        if (!g_outputs[i]) { slot = i; break; }
    if (slot < 0) return -200;

    OutputCtx* ctx = (OutputCtx*)calloc(1, sizeof(OutputCtx));
    if (!ctx) return -201;
    ctx->gain      = 1.0f;
    ctx->channels  = channels > 0 ? channels : 2;
    ctx->frameSize = ctx->channels * 2;
    ctx->bufBytes  = (frames > 0 ? frames : 960) * ctx->frameSize;
    for (int i = 0; i < QUEUE_BUFFERS; i++) {
        ctx->bufs[i] = (int16_t*)calloc(1, ctx->bufBytes);
        if (!ctx->bufs[i]) { free(ctx); return -202; }
    }

    if (slCreateEngine(&ctx->engineObj, 0, NULL, 0, NULL, NULL) != SL_RESULT_SUCCESS) goto fail;
    if ((*ctx->engineObj)->Realize(ctx->engineObj, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS) goto fail;
    if ((*ctx->engineObj)->GetInterface(ctx->engineObj, SL_IID_ENGINE, &ctx->engine) != SL_RESULT_SUCCESS) goto fail;

    if ((*ctx->engine)->CreateOutputMix(ctx->engine, &ctx->mixObj, 0, NULL, NULL) != SL_RESULT_SUCCESS) goto fail;
    if ((*ctx->mixObj)->Realize(ctx->mixObj, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS) goto fail;

    {
        SLDataLocator_AndroidSimpleBufferQueue bqLoc = {
            SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, QUEUE_BUFFERS
        };
        SLDataFormat_PCM pcmFmt = {
            SL_DATAFORMAT_PCM,
            (SLuint32)ctx->channels,
            (SLuint32)(rate * 1000),
            SL_PCMSAMPLEFORMAT_FIXED_16,
            SL_PCMSAMPLEFORMAT_FIXED_16,
            ctx->channels == 2
                ? (SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT)
                : SL_SPEAKER_FRONT_CENTER,
            SL_BYTEORDER_LITTLEENDIAN
        };
        SLDataSource audioSrc = { &bqLoc, &pcmFmt };
        SLDataLocator_OutputMix outLoc = { SL_DATALOCATOR_OUTPUTMIX, ctx->mixObj };
        SLDataSink audioSnk = { &outLoc, NULL };
        const SLInterfaceID ids[1]  = { SL_IID_ANDROIDSIMPLEBUFFERQUEUE };
        const SLboolean     reqs[1] = { SL_BOOLEAN_TRUE };

        if ((*ctx->engine)->CreateAudioPlayer(ctx->engine, &ctx->playerObj,
                &audioSrc, &audioSnk, 1, ids, reqs) != SL_RESULT_SUCCESS) goto fail;
    }

    if ((*ctx->playerObj)->Realize(ctx->playerObj, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS) goto fail;
    if ((*ctx->playerObj)->GetInterface(ctx->playerObj, SL_IID_PLAY, &ctx->play) != SL_RESULT_SUCCESS) goto fail;
    if ((*ctx->playerObj)->GetInterface(ctx->playerObj, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &ctx->bq) != SL_RESULT_SUCCESS) goto fail;

    g_outputs[slot] = ctx;
    return slot;

fail:
    if (ctx->playerObj) (*ctx->playerObj)->Destroy(ctx->playerObj);
    if (ctx->mixObj)    (*ctx->mixObj)->Destroy(ctx->mixObj);
    if (ctx->engineObj) (*ctx->engineObj)->Destroy(ctx->engineObj);
    for (int i = 0; i < QUEUE_BUFFERS; i++) free(ctx->bufs[i]);
    free(ctx);
    return -300;
}

JNIEXPORT jint JNICALL
Java_de_maxhenkel_shim_NativeAudioOutput_start(JNIEnv* e, jclass c, jint h) {
    if (h < 0 || h >= MAX_OUTPUT_STREAMS || !g_outputs[h]) return -1;
    return (jint)(*g_outputs[h]->play)->SetPlayState(g_outputs[h]->play, SL_PLAYSTATE_PLAYING);
}

JNIEXPORT jint JNICALL
Java_de_maxhenkel_shim_NativeAudioOutput_stop(JNIEnv* e, jclass c, jint h) {
    if (h < 0 || h >= MAX_OUTPUT_STREAMS || !g_outputs[h]) return -1;
    return (jint)(*g_outputs[h]->play)->SetPlayState(g_outputs[h]->play, SL_PLAYSTATE_STOPPED);
}

JNIEXPORT jint JNICALL
Java_de_maxhenkel_shim_NativeAudioOutput_close(JNIEnv* e, jclass c, jint h) {
    if (h < 0 || h >= MAX_OUTPUT_STREAMS || !g_outputs[h]) return 0;
    OutputCtx* ctx = g_outputs[h];
    (*ctx->playerObj)->Destroy(ctx->playerObj);
    (*ctx->mixObj)->Destroy(ctx->mixObj);
    (*ctx->engineObj)->Destroy(ctx->engineObj);
    for (int i = 0; i < QUEUE_BUFFERS; i++) free(ctx->bufs[i]);
    free(ctx);
    g_outputs[h] = NULL;
    return 0;
}

JNIEXPORT jint JNICALL
Java_de_maxhenkel_shim_NativeAudioOutput_write2(JNIEnv* env, jclass c,
                                                 jint h, jbyteArray buf,
                                                 jint off, jint len, jint frameSize) {
    if (h < 0 || h >= MAX_OUTPUT_STREAMS || !g_outputs[h]) return -1;
    OutputCtx* ctx = g_outputs[h];
    if (len <= 0) return 0;
    int copyLen = len < ctx->bufBytes ? len : ctx->bufBytes;
    int idx = ctx->writeBuf % QUEUE_BUFFERS;
    jbyte* d = (*env)->GetByteArrayElements(env, buf, NULL);
    if (!d) return -2;
    memcpy(ctx->bufs[idx], d + off, copyLen);
    (*env)->ReleaseByteArrayElements(env, buf, d, JNI_ABORT);

    /* Apply software gain (MASTER_GAIN) with saturation, in place. */
    if (ctx->gain != 1.0f) {
        int16_t* samples = (int16_t*)ctx->bufs[idx];
        int      count   = copyLen / 2;
        float    g       = ctx->gain;
        for (int i = 0; i < count; i++) {
            int32_t v = (int32_t)(samples[i] * g);
            if      (v >  32767) v =  32767;
            else if (v < -32768) v = -32768;
            samples[i] = (int16_t)v;
        }
    }

    SLresult r = (*ctx->bq)->Enqueue(ctx->bq, ctx->bufs[idx], (SLuint32)copyLen);
    if (r != SL_RESULT_SUCCESS) return -3;
    ctx->writeBuf++;
    return copyLen;
}

JNIEXPORT jint JNICALL
Java_de_maxhenkel_shim_NativeAudioOutput_setGain(JNIEnv* env, jclass c, jint h, jfloat gain) {
    if (h < 0 || h >= MAX_OUTPUT_STREAMS || !g_outputs[h]) return -1;
    g_outputs[h]->gain = gain;
    return 0;
}
