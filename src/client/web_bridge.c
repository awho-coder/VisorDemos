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

// web_bridge.c -- bridge between the web page (touch buttons) and the viewer.
// Web build only: the page calls visor_cmd() and reads visor_state(),
// visor_players() and visor_kills().

#ifdef __EMSCRIPTEN__

#include "client.h"
#include <emscripten.h>

void visor_cmd(const char *text);
const char *visor_state(void);
const char *visor_players(void);
const char *visor_kills(void);

// runs console text, e.g. "chase_next" or "demo_step +"
EMSCRIPTEN_KEEPALIVE void visor_cmd(const char *text)
{
    if (!text || !*text)
        return;
    Cbuf_AddText(&cmd_buffer, text);
    Cbuf_AddText(&cmd_buffer, "\n");
}

// appends s as the body of a JSON string; keeps the output valid JSON and
// UTF-8 (bytes >= 0x80 are Latin-1/colors in Q2 names: replaced by '?')
static size_t json_escape(char *dst, size_t size, const char *s)
{
    size_t n = 0;

    for (; *s && n + 7 < size; s++) {
        unsigned char c = *s;
        if (c == '"' || c == '\\') {
            dst[n++] = '\\';
            dst[n++] = c;
        } else if (c < 0x20 || c == 0x7f) {
            n += snprintf(dst + n, size - n, "\\u%04x", c);
        } else if (c >= 0x80) {
            dst[n++] = '?';
        } else {
            dst[n++] = c;
        }
    }
    dst[n] = 0;
    return n;
}

// JSON text that grows as needed; one per list, valid until the next call
typedef struct {
    char    *p;
    size_t  len, size;
} jbuf_t;

static void jb_reset(jbuf_t *b)
{
    b->len = 0;
    if (!b->p) {
        b->size = 4096;
        b->p = Z_Malloc(b->size);
    }
    b->p[0] = 0;
}

static void jb_printf(jbuf_t *b, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(b->p + b->len, b->size - b->len, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if (b->len + n + 1 > b->size) {
        while (b->len + n + 1 > b->size)
            b->size *= 2;
        b->p = Z_Realloc(b->p, b->size);
        va_start(ap, fmt);
        vsnprintf(b->p + b->len, b->size - b->len, fmt, ap);
        va_end(ap);
    }
    b->len += n;
}

// "text" as a JSON string (or null)
static void jb_string(jbuf_t *b, const char *s)
{
    char tmp[300];

    if (!s) {
        jb_printf(b, "null");
        return;
    }
    json_escape(tmp, sizeof(tmp), s);
    jb_printf(b, "\"%s\"", tmp);
}

// entity number of the player on screen (what chase_player takes), 0 if none
static int viewed_slot(const char *name)
{
    int num = CL_ChaseTarget();

    if (num)
        return num;
    if (!name)
        return 0;
    for (int i = 0; i < cl.maxclients && i < MAX_CLIENTS; i++) {
        if (!strcmp(cl.clientinfo[i].name, name))
            return i + 1;
    }
    return 0;
}

// state as JSON in a static buffer, valid until the next call
EMSCRIPTEN_KEEPALIVE const char *visor_state(void)
{
    static char buf[1536];
    char follow[300], clip[96];
    int frame = 0;
    float progress = 0;
    bool paused = false;
    double t, len;
    int64_t real_len;
    const char *name;
    viewkind_t kind;
    bool first;

    if (!CL_DemoPosition(&frame, &progress, &paused)) {
        strcpy(buf, "{\"ready\":false}");
        return buf;
    }

    // same clock as the kills' times; "len" is exact once the demo is
    // indexed, else an estimate from how far along the file we are
    t = CL_FeedDemoTime() / 1000.0;
    real_len = CL_FeedLength();
    if (real_len > 0)
        len = real_len / 1000.0;
    else
        len = progress > 0.001f ? t / progress : 0;
    if (len > 0 && len < t)
        len = t;

    // CL_ViewedPlayer (not CL_ChaseTargetName) because in a MVD the followed
    // player comes through the local MVD server, not through our chase camera
    name = CL_ViewedPlayer(&kind);
    first = kind == VIEW_FOLLOW ? CL_ChaseFirstPerson()
          : name && !CL_MvdThirdPerson();
    if (name) {
        follow[0] = '"';
        size_t n = json_escape(follow + 1, sizeof(follow) - 2, name);
        follow[n + 1] = '"';
        follow[n + 2] = 0;
    } else {
        strcpy(follow, "null");
    }

    snprintf(clip, sizeof(clip), "{\"active\":%s,\"progress\":%.3f}",
             CL_CaptureActive() ? "true" : "false", CL_CaptureProgress());

    snprintf(buf, sizeof(buf),
             "{\"ready\":true,\"t\":%.2f,\"len\":%.1f,\"progress\":%.4f,"
             "\"indexed\":%s,\"paused\":%s,\"speed\":%.3g,"
             "\"follow\":%s,\"followSlot\":%d,\"first\":%s,\"freecam\":%s,"
             "\"names\":%s,\"xray\":%s,\"killfeed\":%d,\"chat\":%d,\"trails\":%d,"
             "\"clip\":%s}",
             t, len, progress,
             CL_FeedIndexed() ? "true" : "false",
             paused ? "true" : "false", CL_DemoSpeed(),
             follow, viewed_slot(name),
             first ? "true" : "false",
             CL_FreecamActive() ? "true" : "false",
             Cvar_VariableInteger("visor_names") ? "true" : "false",
             Cvar_VariableInteger("xray") ? "true" : "false",
             Cvar_VariableInteger("visor_killfeed"),
             Cvar_VariableInteger("visor_chat"),
             Cvar_VariableInteger("visor_trails"),
             clip);
    return buf;
}

// players of the demo: [{"slot":4,"name":"x","team":"allies","kills":9,"deaths":3}]
// slot = what chase_player takes; team "spec" when it isn't one of the sides
EMSCRIPTEN_KEEPALIVE const char *visor_players(void)
{
    static jbuf_t b;
    bool any = false;

    jb_reset(&b);
    jb_printf(&b, "[");
    if (cls.state == ca_active) {
        for (int i = 0; i < cl.maxclients && i < MAX_CLIENTS; i++) {
            const char *name = cl.clientinfo[i].name;
            const char *team;
            int frags, deaths;

            if (!name[0])
                continue;
            team = CL_FeedPlayerTeam(name);
            CL_FeedCounts(name, &frags, &deaths);
            jb_printf(&b, "%s{\"slot\":%d,\"name\":", any ? "," : "", i + 1);
            jb_string(&b, name);
            jb_printf(&b, ",\"team\":\"%s\",\"kills\":%d,\"deaths\":%d}",
                      team[0] ? team : "spec", frags, deaths);
            any = true;
        }
    }
    jb_printf(&b, "]");
    return b.p;
}

// every death in the index, by time:
// [{"t":245.1,"killer":"a","victim":"b","weapon":"thompson","hs":false,"tk":false,"kteam":"allies","vteam":"axis"}]
EMSCRIPTEN_KEEPALIVE const char *visor_kills(void)
{
    static jbuf_t b;
    feedkill_t k;

    jb_reset(&b);
    jb_printf(&b, "[");
    for (int i = 0; CL_FeedGetKill(i, &k); i++) {
        jb_printf(&b, "%s{\"t\":%.2f,\"killer\":", i ? "," : "", k.time);
        jb_string(&b, k.killer);
        jb_printf(&b, ",\"victim\":");
        jb_string(&b, k.victim);
        jb_printf(&b, ",\"weapon\":");
        jb_string(&b, k.weapon);
        jb_printf(&b, ",\"hs\":%s,\"tk\":%s,\"kteam\":\"%s\",\"vteam\":\"%s\"}",
                  k.headshot ? "true" : "false", k.teamkill ? "true" : "false",
                  k.kteam, k.vteam);
    }
    jb_printf(&b, "]");
    return b.p;
}

// after a camera path / keyframe file is written: the page saves the
// browser's copy of the folder (IDBFS)
EM_JS(void, web_saved_js, (void), {
    if (Module['visorGuardar'])
        Module['visorGuardar']();
});

void CL_WebSaved(void)
{
    web_saved_js();
}

#endif // __EMSCRIPTEN__
