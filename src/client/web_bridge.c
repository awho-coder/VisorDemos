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
// Web build only: the page calls visor_cmd() and reads visor_state().

#ifdef __EMSCRIPTEN__

#include "client.h"
#include <emscripten.h>

void visor_cmd(const char *text);
const char *visor_state(void);

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

// state as JSON in a static buffer, valid until the next call
EMSCRIPTEN_KEEPALIVE const char *visor_state(void)
{
    static char buf[1024];
    char follow[300];
    int frame = 0;
    float progress = 0;
    bool paused = false;
    double t, len;
    const char *name;

    if (!CL_DemoPosition(&frame, &progress, &paused)) {
        strcpy(buf, "{\"ready\":false}");
        return buf;
    }

    t = frame * (double)CL_DemoFrameMs() / 1000.0;
    len = progress > 0.001f ? t / progress : 0;

    name = CL_ChaseTargetName();
    if (name) {
        follow[0] = '"';
        size_t n = json_escape(follow + 1, sizeof(follow) - 2, name);
        follow[n + 1] = '"';
        follow[n + 2] = 0;
    } else {
        strcpy(follow, "null");
    }

    snprintf(buf, sizeof(buf),
             "{\"ready\":true,\"t\":%.2f,\"len\":%.1f,\"progress\":%.4f,"
             "\"paused\":%s,\"speed\":%.3g,\"follow\":%s,\"first\":%s,"
             "\"names\":%s,\"xray\":%s,\"killfeed\":%d}",
             t, len, progress,
             CL_DemoPaused() ? "true" : "false", CL_DemoSpeed(), follow,
             CL_ChaseFirstPerson() ? "true" : "false",
             Cvar_VariableInteger("visor_names") ? "true" : "false",
             Cvar_VariableInteger("xray") ? "true" : "false",
             Cvar_VariableInteger("visor_killfeed"));
    return buf;
}

#endif // __EMSCRIPTEN__
