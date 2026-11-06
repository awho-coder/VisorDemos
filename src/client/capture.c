/*
Copyright (C) 2026 VisorDemos contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

// capture.c -- render demo clips straight to a video file
//
// While capturing, the engine runs on a fixed time step (1/clip_fps seconds
// per frame) instead of the wall clock, so the output is perfectly smooth no
// matter how long each frame takes to render and encode. Every rendered frame
// is read back from the GL back buffer before menus and console are drawn, and
// the software sound mixer is driven with exactly the samples that belong to
// that frame, so audio stays in sync.
//
// Clip workflow (DM2 demos): clip_in / clip_out mark a range, clip_export
// seeks to the start, records until the end and pauses again.
// capture_start / capture_stop record manually (also works for MVD playback).
//
// Web build: no FFmpeg. Everything above stays the same (marks, fixed time
// step, crop to the clip's format, mixed audio) but each frame (RGBA, scaled
// to clip_size) and the audio go to the page through Module.visorClip, which
// encodes them in the browser (WebCodecs). While the page says busy() the
// engine holds the clock: no time passes and no frame is taken.

#include "client.h"
#include "sound/sound.h"
#include "common/files.h"

static cvar_t   *clip_fps;
static cvar_t   *clip_format;
static cvar_t   *clip_quality;
static cvar_t   *clip_hud;
static cvar_t   *clip_volume;
static cvar_t   *clip_size;

static struct {
    int     in, out;            // demo frame marks, -1 if unset
    float   in_pos, out_pos;    // position in the demo file (0..1), for the timeline
} marks = { -1, -1 };

static bool export_whole;       // clip_export_all is opening the file

#if USE_AVCODEC || defined(__EMSCRIPTEN__)
#define CAPTURE_REAL    1
#endif

#if CAPTURE_REAL

#if USE_AVCODEC
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
#else
#include <emscripten.h>

#define WEB_MAX_CLIP    120     // seconds: longest clip in the browser

EM_JS(int, web_clip_available, (void), {
    var c = Module['visorClip'];
    return c && c.begin ? 1 : 0;
});

EM_JS(int, web_clip_begin, (int w, int h, int fps, int rate, int channels, const char *name), {
    try {
        return Module['visorClip'].begin({ width: w, height: h, fps: fps, sampleRate: rate,
                                           channels: channels, name: UTF8ToString(name) }) ? 1 : 0;
    } catch (e) {
        console.warn(e);
        return 0;
    }
});

EM_JS(void, web_clip_frame, (const void *ptr, int w, int h), {
    try { Module['visorClip'].frame(ptr, w, h); } catch (e) { console.warn(e); }
});

EM_JS(void, web_clip_audio, (const void *ptr, int frames, int channels), {
    try { Module['visorClip'].audio(ptr, frames, channels); } catch (e) { console.warn(e); }
});

EM_JS(int, web_clip_busy, (void), {
    try { return Module['visorClip'].busy() ? 1 : 0; } catch (e) { return 0; }
});

EM_JS(void, web_clip_end, (int ok), {
    try { Module['visorClip'].end(!!ok); } catch (e) { console.warn(e); }
});
#endif

typedef enum {
    CAP_IDLE,
    CAP_RUNNING,
} capstate_t;

static struct {
    capstate_t  state;
    bool        exporting;      // stop automatically at marks.out
    bool        whole;          // the whole demo, start to end (clip_export_all)
    bool        failed;         // an encoding error stopped the recording
    bool        cut;            // clip_stop before the end mark
    int         start_frame;    // demo frame where the export began
    int         stop_frame;

    // fixed time step
    int         fps;
    int64_t     ticks;          // frames issued
    int64_t     time_ms;        // ms issued so far
    int64_t     audio_target;   // samples that should exist after this frame
    int64_t     audio_painted;  // samples mixed so far
    float       game_frac;      // leftover game time when playing slow/fast

    // video
#if USE_AVCODEC
    AVFormatContext     *oc;
    AVCodecContext      *venc, *aenc;
    AVStream            *vst, *ast;
    struct SwsContext   *sws;
    AVFrame             *vframe, *aframe;
    AVPacket            *pkt;
#else
    bool                skip;           // the page is busy: this tick doesn't count
    byte                *out;           // frame handed to the page (width x height)
    int                 *cols;          // scaling table: x0, x1, weight per column
#endif
    byte                *rgba;
    int                 src_w, src_h;   // render size
    int                 crop_x, crop_y; // source region kept (top-left)
    int                 crop_w, crop_h;
    int                 width, height;  // encoded size (even)
    int64_t             vframes;

    // audio
    int                 rate;
    float               *abuf;          // AV: interleaved stereo, int16 scale
#if USE_AVCODEC
    int                 abuf_len, abuf_size;
    int64_t             apts;
#else
    int                 abuf_size;      // web: scratch for what goes to the page
#endif

    float               saved_volume;
    char                saved_clear[16];    // gl_clear before recording

    char                filename[MAX_OSPATH];
} cap;

bool CL_CaptureActive(void)
{
    return cap.state == CAP_RUNNING;
}

bool CL_CaptureHideHud(void)
{
    return CL_CaptureActive() && !clip_hud->integer;
}

// part of the window that ends up in the clip (cropped to the clip's aspect)
bool CL_CaptureRect(int *x, int *y, int *w, int *h)
{
    if (!CL_CaptureActive())
        return false;
    *x = cap.crop_x;
    *y = cap.crop_y;
    *w = cap.crop_w;
    *h = cap.crop_h;
    return true;
}

// Cvar_SetValue doesn't run change callbacks, the mixer needs them
static void set_cvar_now(const char *name, float value)
{
    cvar_t *var = Cvar_FindVar(name);

    if (!var)
        return;
    Cvar_SetValue(var, value, FROM_CODE);
    if (var->changed)
        var->changed(var);
}

static bool audio_enabled(void)
{
#if USE_AVCODEC
    return s_started == SS_DMA && cap.aenc;
#else
    return s_started == SS_DMA && cap.rate;
#endif
}

/*
===============
CL_CaptureFrameMsec

Called by the main loop once per frame while capturing; returns the fixed
step for this frame. Steps alternate (e.g. 16/17 ms at 60 fps) so the long
term rate is exact.
===============
*/
unsigned CL_CaptureFrameMsec(void)
{
    int64_t next;
    unsigned msec;
    float scaled;

#ifdef __EMSCRIPTEN__
    // the page has too much queued: time stands still, nothing is captured
    cap.skip = web_clip_busy();
    if (cap.skip)
        return 0;
#endif

    cap.ticks++;
    next = cap.ticks * 1000 / cap.fps;
    msec = next - cap.time_ms;
    cap.time_ms = next;

    if (cap.rate)
        cap.audio_target = cap.ticks * cap.rate / cap.fps;

    // slow motion / fast forward: the video keeps its frame rate, the game
    // advances by a scaled amount
    cap.game_frac += msec * CL_PlaySpeed();
    scaled = floorf(cap.game_frac);
    cap.game_frac -= scaled;

    return scaled;
}

/*
===============
CL_CaptureAudioSamples

How many samples the mixer must produce for the current frame.
===============
*/
int CL_CaptureAudioSamples(void)
{
    int64_t n = cap.audio_target - cap.audio_painted;
    return n > 0 ? n : 0;
}

#if USE_AVCODEC

/*
===============================================================================

ENCODING

===============================================================================
*/

static void log_averror(const char *what, int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(err, buf, sizeof(buf));
    Com_EPrintf("Captura: %s: %s\n", what, buf);
}

static int write_packets(AVCodecContext *enc, AVStream *st)
{
    int ret;

    while (1) {
        ret = avcodec_receive_packet(enc, cap.pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return 0;
        if (ret < 0)
            return ret;

        av_packet_rescale_ts(cap.pkt, enc->time_base, st->time_base);
        cap.pkt->stream_index = st->index;
        ret = av_interleaved_write_frame(cap.oc, cap.pkt);
        if (ret < 0)
            return ret;
    }
}

static int encode(AVCodecContext *enc, AVStream *st, AVFrame *frame)
{
    int ret = avcodec_send_frame(enc, frame);
    if (ret < 0)
        return ret;
    return write_packets(enc, st);
}

static const AVCodec *find_video_encoder(bool prores)
{
    // libx264 on Linux; Media Foundation (built into Windows) on Windows
    static const char *const h264[] = { "libx264", "h264_mf", "libopenh264", NULL };
    const AVCodec *codec;

    if (prores)
        return avcodec_find_encoder_by_name("prores_ks");

    for (int i = 0; h264[i]; i++) {
        codec = avcodec_find_encoder_by_name(h264[i]);
        if (codec)
            return codec;
    }

    return avcodec_find_encoder(AV_CODEC_ID_MPEG4);
}

// the wanted pixel format if the encoder takes it, else its first choice
static enum AVPixelFormat pick_pix_fmt(const AVCodec *codec, enum AVPixelFormat want)
{
    const enum AVPixelFormat *fmts = NULL;
    int n = 0;

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
    if (avcodec_get_supported_config(NULL, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0,
                                     (const void **)&fmts, &n) < 0)
        fmts = NULL;
#else
    fmts = codec->pix_fmts;
    while (fmts && fmts[n] != AV_PIX_FMT_NONE)
        n++;
#endif

    if (!fmts || !n)
        return want;
    for (int i = 0; i < n; i++)
        if (fmts[i] == want)
            return want;
    return fmts[0];
}

static bool open_video(bool prores)
{
    const AVCodec *codec = find_video_encoder(prores);
    AVCodecContext *c;
    int ret;

    if (!codec) {
        Com_EPrintf("Captura: no hay codificador de video disponible.\n");
        return false;
    }

    cap.vst = avformat_new_stream(cap.oc, NULL);
    c = cap.venc = avcodec_alloc_context3(codec);
    if (!cap.vst || !c)
        return false;

    c->width = cap.width;
    c->height = cap.height;
    c->time_base = (AVRational){ 1, cap.fps };
    c->framerate = (AVRational){ cap.fps, 1 };
    c->gop_size = cap.fps;   // a keyframe every second keeps editors snappy
    c->color_range = AVCOL_RANGE_MPEG;
    c->colorspace = AVCOL_SPC_BT709;
    c->color_primaries = AVCOL_PRI_BT709;
    c->color_trc = AVCOL_TRC_BT709;

    if (prores) {
        c->pix_fmt = pick_pix_fmt(codec, AV_PIX_FMT_YUV422P10LE);
        av_opt_set(c->priv_data, "profile", "hq", 0);
    } else {
        c->pix_fmt = pick_pix_fmt(codec, AV_PIX_FMT_YUV420P);
        if (!strcmp(codec->name, "libx264")) {
            av_opt_set(c->priv_data, "preset", "fast", 0);
            av_opt_set_double(c->priv_data, "crf", Cvar_ClampValue(clip_quality, 0, 51), 0);
        } else {
            // no CRF: pick a generous bitrate (about 31 Mbit/s at 1080p60)
            c->bit_rate = (int64_t)cap.width * cap.height * cap.fps / 4;
            c->rc_max_rate = c->bit_rate * 3 / 2;
            c->rc_buffer_size = c->bit_rate * 2;
        }
    }

    Com_DPrintf("Captura: codificador %s, formato %s\n", codec->name, av_get_pix_fmt_name(c->pix_fmt));

    if (cap.oc->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    ret = avcodec_open2(c, codec, NULL);
    if (ret < 0) {
        log_averror("abrir codificador de video", ret);
        return false;
    }

    avcodec_parameters_from_context(cap.vst->codecpar, c);
    cap.vst->time_base = c->time_base;
    cap.vst->avg_frame_rate = c->framerate;

    cap.vframe = av_frame_alloc();
    if (!cap.vframe)
        return false;
    cap.vframe->format = c->pix_fmt;
    cap.vframe->width = c->width;
    cap.vframe->height = c->height;
    cap.vframe->color_range = c->color_range;
    cap.vframe->colorspace = c->colorspace;
    cap.vframe->color_primaries = c->color_primaries;
    cap.vframe->color_trc = c->color_trc;
    if (av_frame_get_buffer(cap.vframe, 0) < 0)
        return false;

    cap.sws = sws_getContext(cap.crop_w, cap.crop_h, AV_PIX_FMT_RGBA,
                             cap.width, cap.height, c->pix_fmt,
                             SWS_BICUBIC, NULL, NULL, NULL);
    if (!cap.sws)
        return false;

    // full range RGB in, limited range BT.709 YUV out
    sws_setColorspaceDetails(cap.sws,
                             sws_getCoefficients(SWS_CS_ITU709), 1,
                             sws_getCoefficients(SWS_CS_ITU709), 0,
                             0, 1 << 16, 1 << 16);

    cap.rgba = Z_Malloc(cap.src_w * cap.src_h * 4);
    return true;
}

static bool open_audio(bool pcm)
{
    const AVCodec *codec;
    AVCodecContext *c;
    int ret;

    if (s_started != SS_DMA) {
        Com_WPrintf("Captura: el audio solo se graba con el sonido por software "
                    "(s_enable 1). El video se guardara sin sonido.\n");
        return true;
    }

    codec = avcodec_find_encoder(pcm ? AV_CODEC_ID_PCM_S16LE : AV_CODEC_ID_AAC);
    if (!codec) {
        Com_WPrintf("Captura: no hay codificador de audio, se guardara sin sonido.\n");
        return true;
    }

    cap.ast = avformat_new_stream(cap.oc, NULL);
    c = cap.aenc = avcodec_alloc_context3(codec);
    if (!cap.ast || !c)
        return false;

    cap.rate = S_GetSampleRate();
    c->sample_fmt = pcm ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_FLTP;
    c->sample_rate = cap.rate;
    c->time_base = (AVRational){ 1, cap.rate };
    av_channel_layout_default(&c->ch_layout, 2);
    if (!pcm)
        c->bit_rate = 320000;

    if (cap.oc->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    ret = avcodec_open2(c, codec, NULL);
    if (ret < 0) {
        log_averror("abrir codificador de audio", ret);
        return false;
    }

    avcodec_parameters_from_context(cap.ast->codecpar, c);
    cap.ast->time_base = c->time_base;

    cap.aframe = av_frame_alloc();
    if (!cap.aframe)
        return false;
    cap.aframe->format = c->sample_fmt;
    cap.aframe->sample_rate = c->sample_rate;
    av_channel_layout_copy(&cap.aframe->ch_layout, &c->ch_layout);
    cap.aframe->nb_samples = c->frame_size ? c->frame_size : 1024;
    if (av_frame_get_buffer(cap.aframe, 0) < 0)
        return false;

    cap.abuf_size = cap.rate;   // one second of stereo samples
    cap.abuf = Z_Malloc(cap.abuf_size * 2 * sizeof(float));
    return true;
}

// encode one encoder-sized chunk (or less, when flushing) from abuf
static int encode_audio_chunk(int count)
{
    AVFrame *f = cap.aframe;
    const float *in = cap.abuf;
    int ret;

    ret = av_frame_make_writable(f);
    if (ret < 0)
        return ret;

    f->nb_samples = count;
    if (cap.aenc->sample_fmt == AV_SAMPLE_FMT_FLTP) {
        float *l = (float *)f->data[0];
        float *r = (float *)f->data[1];
        for (int i = 0; i < count; i++) {
            l[i] = Q_clipf(in[i * 2 + 0] * (1.0f / 32768), -1, 1);
            r[i] = Q_clipf(in[i * 2 + 1] * (1.0f / 32768), -1, 1);
        }
    } else {
        int16_t *out = (int16_t *)f->data[0];
        for (int i = 0; i < count * 2; i++)
            out[i] = Q_clip_int16((int)in[i]);
    }

    f->pts = cap.apts;
    cap.apts += count;

    cap.abuf_len -= count;
    memmove(cap.abuf, cap.abuf + count * 2, cap.abuf_len * 2 * sizeof(float));

    return encode(cap.aenc, cap.ast, f);
}

/*
===============
CL_CaptureAudio

Receives freshly mixed samples from the sound mixer.
===============
*/
void CL_CaptureAudio(const float *samples, int count)
{
    int chunk;

    if (!CL_CaptureActive())
        return;

    cap.audio_painted += count;


    if (!audio_enabled())
        return;

    chunk = cap.aenc->frame_size ? cap.aenc->frame_size : 1024;

    while (count > 0) {
        int n = min(count, cap.abuf_size - cap.abuf_len);
        memcpy(cap.abuf + cap.abuf_len * 2, samples, n * 2 * sizeof(float));
        cap.abuf_len += n;
        samples += n * 2;
        count -= n;

        while (cap.abuf_len >= chunk) {
            int ret = encode_audio_chunk(chunk);
            if (ret < 0) {
                log_averror("codificar audio", ret);
                break;
            }
        }
    }
}

static void free_encoder(void)
{
    avcodec_free_context(&cap.venc);
    avcodec_free_context(&cap.aenc);
    av_frame_free(&cap.vframe);
    av_frame_free(&cap.aframe);
    av_packet_free(&cap.pkt);
    sws_freeContext(cap.sws);
    cap.sws = NULL;
    if (cap.oc) {
        if (!(cap.oc->oformat->flags & AVFMT_NOFILE))
            avio_closep(&cap.oc->pb);
        avformat_free_context(cap.oc);
        cap.oc = NULL;
    }
    Z_Freep(&cap.rgba);
    Z_Freep(&cap.abuf);
    cap.vst = cap.ast = NULL;
}

#else // !USE_AVCODEC: the page encodes

/*
===============
CL_CaptureAudio

Receives freshly mixed samples from the sound mixer; the page gets them as
interleaved stereo float32 in -1..1.
===============
*/
void CL_CaptureAudio(const float *samples, int count)
{
    if (!CL_CaptureActive())
        return;

    cap.audio_painted += count;

    if (!audio_enabled() || count <= 0 || cap.skip)
        return;

    if (count * 2 > cap.abuf_size) {
        cap.abuf_size = count * 2;
        cap.abuf = Z_Realloc(cap.abuf, cap.abuf_size * sizeof(float));
    }
    for (int i = 0; i < count * 2; i++)
        cap.abuf[i] = Q_clipf(samples[i] * (1.0f / 32768), -1, 1);
    web_clip_audio(cap.abuf, count, 2);
}

static void free_encoder(void)
{
    Z_Freep(&cap.rgba);
    Z_Freep(&cap.out);
    Z_Freep(&cap.cols);
    Z_Freep(&cap.abuf);
}

// the part of the window kept (cap.crop_*) to width x height, bilinear.
// GL rows are bottom-up, the page wants top-down.
static void scale_frame(void)
{
    const int w = cap.width, h = cap.height;
    const int *cols = cap.cols;

    for (int y = 0; y < h; y++) {
        float fy = (y + 0.5f) * cap.crop_h / h - 0.5f;
        int y0 = (int)floorf(fy), wy = (int)((fy - y0) * 256);
        int y1 = Q_clip(y0 + 1, 0, cap.crop_h - 1);
        y0 = Q_clip(y0, 0, cap.crop_h - 1);
        const byte *r0 = cap.rgba + ((size_t)(cap.src_h - 1 - (cap.crop_y + y0)) * cap.src_w + cap.crop_x) * 4;
        const byte *r1 = cap.rgba + ((size_t)(cap.src_h - 1 - (cap.crop_y + y1)) * cap.src_w + cap.crop_x) * 4;
        byte *dst = cap.out + (size_t)y * w * 4;

        for (int x = 0; x < w; x++, dst += 4) {
            const int x0 = cols[x * 3] * 4, x1 = cols[x * 3 + 1] * 4, wx = cols[x * 3 + 2];
            for (int c = 0; c < 3; c++) {
                int top = r0[x0 + c] * (256 - wx) + r0[x1 + c] * wx;
                int bot = r1[x0 + c] * (256 - wx) + r1[x1 + c] * wx;
                dst[c] = (top * (256 - wy) + bot * wy) >> 16;
            }
            dst[3] = 255;
        }
    }
}

static bool open_web(const char *name)
{
    cap.rgba = Z_Malloc((size_t)cap.src_w * cap.src_h * 4);
    cap.out = Z_Malloc((size_t)cap.width * cap.height * 4);
    cap.cols = Z_Malloc((size_t)cap.width * 3 * sizeof(int));
    for (int x = 0; x < cap.width; x++) {
        float fx = (x + 0.5f) * cap.crop_w / cap.width - 0.5f;
        int x0 = (int)floorf(fx);
        cap.cols[x * 3 + 2] = (int)((fx - x0) * 256);
        cap.cols[x * 3 + 1] = Q_clip(x0 + 1, 0, cap.crop_w - 1);
        cap.cols[x * 3] = Q_clip(x0, 0, cap.crop_w - 1);
    }

    if (s_started == SS_DMA) {
        cap.rate = S_GetSampleRate();
    } else {
        Com_WPrintf("Captura: sin sonido por software, el clip no tendra audio.\n");
    }

    if (!web_clip_begin(cap.width, cap.height, cap.fps, cap.rate, cap.rate ? 2 : 0,
                        COM_SkipPath(cap.filename))) {
        free_encoder();
        return false;
    }
    return true;
}

#endif // USE_AVCODEC (encoders)

// "1920x1080" -> 1920, 1080; empty means "same as the window"
static bool parse_size(const char *s, int *w, int *h)
{
    char *p;
    long x, y;

    x = strtol(s, &p, 10);
    if (p == s || (*p != 'x' && *p != 'X'))
        return false;
    y = strtol(p + 1, &p, 10);
    if (*p || x < 16 || y < 16 || x > 8192 || y > 8192)
        return false;

    *w = x;
    *h = y;
    return true;
}

static void make_filename(const char *name, const char *ext)
{
    char base[MAX_QPATH];
    char path[MAX_OSPATH];

    if (name && *name) {
        Q_strlcpy(base, name, sizeof(base));
    } else {
        // demo name plus the clip range, e.g. townwar_01m20s
        char *p;
        int start = marks.in;
        if (start < 0 && !CL_DemoPosition(&start, NULL, NULL))
            start = 0;

        Q_strlcpy(base, cls.servername[0] ? cls.servername : "clip", sizeof(base));
        p = strrchr(base, '.');
        if (p)
            *p = 0;
        for (p = base; *p; p++) {
            if (!Q_isalnum(*p) && *p != '-' && *p != '_')
                *p = '_';
        }
        if (export_whole)
            Q_strlcat(base, "_completo", sizeof(base));
        else
            Q_strlcat(base, va("_%02dm%02ds", start / 600, start / 10 % 60), sizeof(base));
    }

    // never overwrite an existing clip
    for (int i = 0; i < 1000; i++) {
        if (i)
            Q_snprintf(path, sizeof(path), "%s/videos/%s_%d.%s", fs_gamedir, base, i, ext);
        else
            Q_snprintf(path, sizeof(path), "%s/videos/%s.%s", fs_gamedir, base, ext);
        FILE *f = fopen(path, "rb");
        if (!f)
            break;
        fclose(f);
    }

    Q_strlcpy(cap.filename, path, sizeof(cap.filename));
}

static bool capture_open(const char *name)
{
    bool mov = !Q_stricmp(clip_format->string, "mov");
#if USE_AVCODEC
    int ret;
#endif

    cap.fps = Cvar_ClampInteger(clip_fps, 10, 240);
    cap.src_w = r_config.width;
    cap.src_h = r_config.height;

    if (!parse_size(clip_size->string, &cap.width, &cap.height)) {
        cap.width = cap.src_w;
        cap.height = cap.src_h;
    }
    cap.width &= ~1;
    cap.height &= ~1;

    if (cap.width < 16 || cap.height < 16 || cap.src_w < 16 || cap.src_h < 16) {
        Com_EPrintf("Captura: resolucion invalida.\n");
        return false;
    }

    // keep the output aspect ratio: crop the window in the middle
    if ((int64_t)cap.src_w * cap.height > (int64_t)cap.src_h * cap.width) {
        cap.crop_h = cap.src_h;
        cap.crop_w = (int64_t)cap.src_h * cap.width / cap.height;
    } else {
        cap.crop_w = cap.src_w;
        cap.crop_h = (int64_t)cap.src_w * cap.height / cap.width;
    }
    cap.crop_x = (cap.src_w - cap.crop_w) / 2;
    cap.crop_y = (cap.src_h - cap.crop_h) / 2;

    make_filename(name, mov ? "mov" : "mp4");
    FS_CreatePath(cap.filename);

#if !USE_AVCODEC
    return open_web(name);
#else
    ret = avformat_alloc_output_context2(&cap.oc, NULL, mov ? "mov" : "mp4", cap.filename);
    if (ret < 0 || !cap.oc) {
        log_averror("crear archivo", ret);
        return false;
    }

    cap.pkt = av_packet_alloc();
    if (!cap.pkt)
        goto fail;

    if (!open_video(mov))
        goto fail;
    if (!open_audio(mov))
        goto fail;

    ret = avio_open(&cap.oc->pb, cap.filename, AVIO_FLAG_WRITE);
    if (ret < 0) {
        log_averror("abrir archivo", ret);
        goto fail;
    }

    ret = avformat_write_header(cap.oc, NULL);
    if (ret < 0) {
        log_averror("escribir cabecera", ret);
        goto fail;
    }

    return true;

fail:
    free_encoder();
    return false;
#endif
}

#if !USE_AVCODEC
static void capture_close(void)
{
    web_clip_end(!cap.failed && !cap.cut);
    free_encoder();
}
#else
static void capture_close(void)
{
    int ret;

    // flush partial audio and both encoders
    if (audio_enabled() && cap.abuf_len > 0)
        encode_audio_chunk(cap.abuf_len);
    if (cap.aenc)
        encode(cap.aenc, cap.ast, NULL);
    if (cap.venc)
        encode(cap.venc, cap.vst, NULL);

    ret = av_write_trailer(cap.oc);
    if (ret < 0)
        log_averror("cerrar archivo", ret);

    free_encoder();
}
#endif

/*
===============================================================================

START / STOP

===============================================================================
*/

static bool capture_start(const char *name)
{
    if (CL_CaptureActive()) {
        Com_Printf("Ya se esta grabando un clip.\n");
        return false;
    }

    if (cls.state != ca_active) {
        Com_Printf("Abre un demo antes de grabar.\n");
        return false;
    }

    if (CL_CamPathRecording())
        Cmd_ExecuteString(&cmd_buffer, "campath_record");

    memset(&cap, 0, sizeof(cap));

    if (!capture_open(name))
        return false;

    // clip_volume "auto": the clip sounds like what you hear in the viewer.
    // "0.7" was the old default, much louder than most people listen.
    cap.saved_volume = Cvar_VariableValue("s_volume");
    if (!strcmp(clip_volume->string, "0.7"))
        Cvar_Set("clip_volume", "auto");
    if (!Q_stricmp(clip_volume->string, "auto") || !clip_volume->string[0])
        set_cvar_now("s_volume", cap.saved_volume);
    else
        set_cvar_now("s_volume", Cvar_ClampValue(clip_volume, 0, 1));

    // the device gets nothing while we mix for the file
    S_StopAllSounds();

    // make sure the demo runs: sv_paused is what holds a DM2, and it only
    // follows cl_paused when the pause state is checked again; a MVD file
    // has its own pause
    if (CL_DemoView())
        CL_DemoSetPaused(false);
    else {
        Cvar_Set("cl_paused", "0");
        CL_CheckForPause();
    }

    // clear the screen before each frame: where the camera sees nothing
    // (outside the map, inside a wall) the last frame would stay, with the
    // console and the editor panel drawn on it, and end up in the video
    Q_strlcpy(cap.saved_clear, Cvar_VariableString("gl_clear"), sizeof(cap.saved_clear));
    Cvar_Set("gl_clear", "1");

    cap.state = CAP_RUNNING;

    Com_Printf("Grabando %dx%d a %d fps en %s\n", cap.width, cap.height, cap.fps, cap.filename);
    return true;
}

static void capture_stop(void)
{
    double seconds;

    if (!CL_CaptureActive())
        return;

    cap.state = CAP_IDLE;
    seconds = (double)cap.vframes / cap.fps;

    capture_close();

    set_cvar_now("s_volume", cap.saved_volume);
    Cvar_Set("gl_clear", cap.saved_clear[0] ? cap.saved_clear : "0");
    S_StopAllSounds();

    if (cap.failed && !cap.vframes) {
        // nothing usable was written: don't leave an empty file behind
        remove(cap.filename);
#if USE_AVCODEC
        Com_EPrintf("No se pudo grabar el clip (fallo el codificador de video). "
                    "Prueba con otro formato: clip_format mov\n");
#else
        Com_EPrintf("No se pudo grabar el clip.\n");
#endif
        SCR_CenterPrint("No se pudo grabar el clip (ver consola)", false);
        return;
    }

    if (cap.failed) {
        Com_EPrintf("La grabacion se corto por un error; se guardo lo grabado "
                    "(%.1f s): %s\n", seconds, cap.filename);
        SCR_CenterPrint("Grabacion cortada por un error (ver consola)", false);
        return;
    }

    Com_Printf("Clip guardado (%.1f s): %s\n", seconds, cap.filename);
    SCR_CenterPrint(va("Clip guardado: %s", COM_SkipPath(cap.filename)), false);
}

static void capture_fail(void)
{
    cap.failed = true;
    capture_stop();
}

void CL_CaptureShutdown(void)
{
    if (CL_CaptureActive()) {
        if (!cap.whole)     // the whole demo: it ran to its end, which closes it
            Com_Printf("Grabacion interrumpida.\n");
        capture_stop();
    }
}

/*
===============
CL_CaptureFrame

Called right after the 3D view and HUD are drawn, before menus and console.
===============
*/
void CL_CaptureFrame(void)
{
#if USE_AVCODEC
    const uint8_t *src[4];
    int stride[4];
    int ret;
#endif

    if (!CL_CaptureActive())
        return;

#if !USE_AVCODEC
    // the page was busy this tick: the demo didn't move, nothing to take
    if (cap.skip)
        return;
#endif

    if (r_config.width != cap.src_w || r_config.height != cap.src_h) {
        Com_EPrintf("Captura: cambio el tamano de la ventana, se detiene la grabacion.\n");
        capture_fail();
        return;
    }

    if (!R_CaptureReadPixels(cap.rgba, cap.src_w, cap.src_h)) {
        capture_fail();
        return;
    }

#if !USE_AVCODEC
    scale_frame();
    web_clip_frame(cap.out, cap.width, cap.height);
    cap.vframes++;
#else
    // GL rows are bottom-up: start at the top row of the cropped region
    // and walk backwards
    src[0] = cap.rgba + (size_t)(cap.src_h - 1 - cap.crop_y) * cap.src_w * 4 + cap.crop_x * 4;
    stride[0] = -cap.src_w * 4;
    src[1] = src[2] = src[3] = NULL;
    stride[1] = stride[2] = stride[3] = 0;

    ret = av_frame_make_writable(cap.vframe);
    if (ret >= 0) {
        sws_scale(cap.sws, src, stride, 0, cap.crop_h, cap.vframe->data, cap.vframe->linesize);
        cap.vframe->pts = cap.vframes++;
        ret = encode(cap.venc, cap.vst, cap.vframe);
    }
    if (ret < 0) {
        log_averror("codificar video", ret);
        capture_fail();
        return;
    }
#endif

    if (cap.exporting) {
        int frame;
        if (!CL_DemoPosition(&frame, NULL, NULL) || CL_DemoAtEnd() || frame >= cap.stop_frame) {
            capture_stop();
            // a manual pause ("1" is the automatic one, which lets go by itself),
            // applied to the demo right away
            CL_DemoSetPaused(true);
        }
    }
}

/*
===============
CL_CaptureStatus

Text for the on-screen recording indicator (not part of the video).
===============
*/
bool CL_CaptureStatus(char *buf, size_t size)
{
    if (!CL_CaptureActive())
        return false;

    if (cap.whole) {
        float p = 0;
        CL_DemoPosition(NULL, &p, NULL);
        Q_snprintf(buf, size, "GRABANDO DEMO COMPLETO %d%%  (%.1f s)",
                   Q_rint(p * 100), (double)cap.vframes / cap.fps);
    } else if (cap.exporting && marks.out > marks.in) {
        int done = 0;
        CL_DemoPosition(&done, NULL, NULL);
        done -= marks.in;
        int total = cap.stop_frame - marks.in;
        Q_snprintf(buf, size, "GRABANDO %d%%  (%.1f s)",
                   Q_clip(done * 100 / max(total, 1), 0, 100),
                   (double)cap.vframes / cap.fps);
    } else {
        Q_snprintf(buf, size, "GRABANDO  %.1f s", (double)cap.vframes / cap.fps);
    }
    return true;
}

// how far along an export is, 0-1
float CL_CaptureProgress(void)
{
    int frame = 0;
    float p = 0;

    if (!CL_CaptureActive() || !cap.exporting)
        return 0;
    CL_DemoPosition(&frame, &p, NULL);
    if (cap.whole)
        return p;
    if (cap.stop_frame == INT_MAX || cap.stop_frame <= cap.start_frame)
        return 0;
    return Q_clipf((float)(frame - cap.start_frame) / (cap.stop_frame - cap.start_frame), 0, 1);
}

#else // CAPTURE_REAL

bool CL_CaptureActive(void) { return false; }
bool CL_CaptureHideHud(void) { return false; }
bool CL_CaptureRect(int *x, int *y, int *w, int *h) { return false; }
unsigned CL_CaptureFrameMsec(void) { return 0; }
int CL_CaptureAudioSamples(void) { return 0; }
void CL_CaptureAudio(const float *samples, int count) { }
void CL_CaptureFrame(void) { }
void CL_CaptureShutdown(void) { }
bool CL_CaptureStatus(char *buf, size_t size) { return false; }
float CL_CaptureProgress(void) { return 0; }

static bool capture_start(const char *name)
{
    Com_Printf("Esta version se compilo sin soporte de video (ffmpeg).\n");
    return false;
}

static void capture_stop(void) { }

#endif // !CAPTURE_REAL

/*
===============================================================================

COMMANDS

===============================================================================
*/

static void format_frame(char *buf, size_t size, int frame)
{
    Q_snprintf(buf, size, "%d:%02d.%d", frame / 600, frame / 10 % 60, frame % 10);
}

int CL_ClipMarkIn(void)  { return marks.in; }
int CL_ClipMarkOut(void) { return marks.out; }
float CL_ClipMarkPos(bool out) { return out ? marks.out_pos : marks.in_pos; }

void CL_ClipClearMarks(void)
{
    marks.in = marks.out = -1;
}

static bool need_demo(void)
{
    if (!CL_DemoPosition(NULL, NULL, NULL)) {
        Com_Printf("Las marcas de clip funcionan al reproducir un demo (.dm2 o .mvd2).\n");
        return false;
    }
    return true;
}

static void Clip_In_f(void)
{
    char buf[16];

    if (!need_demo())
        return;

    CL_DemoPosition(&marks.in, &marks.in_pos, NULL);
    if (marks.out >= 0 && marks.out <= marks.in)
        marks.out = -1;

    format_frame(buf, sizeof(buf), marks.in);
    SCR_CenterPrint(va("Inicio del clip: %s", buf), false);
}

static void Clip_Out_f(void)
{
    char buf[16];

    if (!need_demo())
        return;

    int now;

    CL_DemoPosition(&now, NULL, NULL);
    if (marks.in >= 0 && now <= marks.in) {
        Com_Printf("El fin del clip debe estar despues del inicio.\n");
        return;
    }

    CL_DemoPosition(&marks.out, &marks.out_pos, NULL);
    format_frame(buf, sizeof(buf), marks.out);
    SCR_CenterPrint(va("Fin del clip: %s", buf), false);
}

static void Clip_Clear_f(void)
{
    CL_ClipClearMarks();
    Com_Printf("Marcas de clip borradas.\n");
}

#ifdef __EMSCRIPTEN__
// the browser holds a clip in memory: at most WEB_MAX_CLIP seconds (frames
// are 1/10 s). Returns the frame to stop at.
static int web_stop_frame(int start, int stop)
{
    int limit = start + WEB_MAX_CLIP * 10;

    if (stop <= limit)
        return stop;
    Com_Printf("En el navegador un clip dura como maximo %d s: se recorta.\n", WEB_MAX_CLIP);
    SCR_CenterPrint(va("El clip se recorta a %d s", WEB_MAX_CLIP), false);
    return limit;
}

static bool web_can_record(void)
{
    if (web_clip_available())
        return true;
    Com_Printf("No se puede grabar clips en este navegador.\n");
    SCR_CenterPrint("No se puede grabar clips en este navegador", false);
    return false;
}
#else
#define web_can_record()    true
#endif

static void Clip_Export_f(void)
{
#if CAPTURE_REAL
    char name[MAX_QPATH];
    int start;

    if (!need_demo())
        return;

    // seeking runs another command and overwrites our arguments
    Q_strlcpy(name, Cmd_Argv(1), sizeof(name));

    if (CL_CaptureActive()) {
        // G again: stop here
        Cmd_ExecuteString(&cmd_buffer, "clip_stop");
        return;
    }

    int now;

    if (!web_can_record())
        return;

    CL_DemoPosition(&now, NULL, NULL);
    start = marks.in >= 0 ? marks.in : now;

    // jump to the start mark first
    if (start != now)
        Cmd_ExecuteString(&cmd_buffer, va("seek %d.%d", start / 10, start % 10));

    if (!capture_start(name))
        return;

    cap.exporting = true;
    cap.start_frame = start;
    cap.stop_frame = marks.out > start ? marks.out : INT_MAX;
#ifdef __EMSCRIPTEN__
    cap.stop_frame = web_stop_frame(start, cap.stop_frame);
#endif
#else
    capture_start(NULL);
#endif
}

// clip_export_all [name]: the whole demo to video, from the start to the
// end, without clip marks
static void Clip_Export_All_f(void)
{
#if CAPTURE_REAL
    char name[MAX_QPATH];
    bool ok;

    if (!need_demo() || !web_can_record())
        return;

    Q_strlcpy(name, Cmd_Argv(1), sizeof(name));

    if (CL_CaptureActive()) {
        Cmd_ExecuteString(&cmd_buffer, "clip_stop");
        return;
    }

    Cmd_ExecuteString(&cmd_buffer, "seek 0");

    export_whole = true;
    ok = capture_start(name);
    export_whole = false;
    if (!ok)
        return;

    cap.exporting = true;
    cap.whole = true;
    cap.stop_frame = INT_MAX;   // until the demo ends
#ifdef __EMSCRIPTEN__
    cap.stop_frame = web_stop_frame(0, cap.stop_frame);
#endif
#else
    capture_start(NULL);
#endif
}

// clip_stop: end the recording here (what was recorded is kept) and pause
static void Clip_Stop_f(void)
{
    if (!CL_CaptureActive()) {
        Com_Printf("No se esta grabando ningun clip.\n");
        return;
    }
    cap.cut = cap.exporting;
    capture_stop();
    CL_DemoSetPaused(true);
}

static void Capture_Start_f(void)
{
    capture_start(Cmd_Argv(1));
}

static void Capture_Stop_f(void)
{
    capture_stop();
}

static void Capture_Toggle_f(void)
{
    if (CL_CaptureActive())
        capture_stop();
    else
        capture_start(NULL);
}

static const cmdreg_t c_capture[] = {
    { "clip_in", Clip_In_f },
    { "clip_out", Clip_Out_f },
    { "clip_clear", Clip_Clear_f },
    { "clip_export", Clip_Export_f },
    { "clip_export_all", Clip_Export_All_f },
    { "clip_stop", Clip_Stop_f },
    { "capture_start", Capture_Start_f },
    { "capture_stop", Capture_Stop_f },
    { "capture_toggle", Capture_Toggle_f },
    { NULL }
};

void CL_InitCapture(void)
{
    clip_fps = Cvar_Get("clip_fps", "60", CVAR_ARCHIVE);
    clip_format = Cvar_Get("clip_format", "mp4", CVAR_ARCHIVE);
    clip_quality = Cvar_Get("clip_quality", "16", CVAR_ARCHIVE);
    clip_hud = Cvar_Get("clip_hud", "0", CVAR_ARCHIVE);
    clip_volume = Cvar_Get("clip_volume", "auto", CVAR_ARCHIVE);
    clip_size = Cvar_Get("clip_size", "1920x1080", CVAR_ARCHIVE);

    Cmd_Register(c_capture);
}
