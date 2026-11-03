/*
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

//
// http_web.c -- file downloads for the browser build (replaces http.c/libcurl)
//
// When a server needs files we don't have (map, models, sounds, skins...),
// the precache check in download.c queues them here and the page downloads
// them with fetch() (Module.q2dl, with the browser cache), writing each one
// into the game directory. Files the page doesn't know about fall back to the
// normal UDP download from the game server.
//
//   Module.q2dl.conoce(ruta)          true if the page can download it
//   Module.q2dl.bajar(ruta, destino)  Promise<status>: 200 = written to destino
//

#include "client.h"
#include <emscripten.h>

EM_JS(int, web_dl_available, (void), {
    return Module['q2dl'] ? 1 : 0;
});

EM_JS(int, web_dl_known, (const char *path), {
    var d = Module['q2dl'];
    return d && d.conoce(UTF8ToString(path)) ? 1 : 0;
});

EM_JS(void, web_dl_start, (int id, int gen, const char *path, const char *dest), {
    var d = Module['q2dl'];
    var listo = function (st) { (d.listos = d.listos || []).push([id, gen, st]); };
    d.bajar(UTF8ToString(path), UTF8ToString(dest)).then(listo, function (e) {
        console.warn(e);
        listo(0);
    });
});

// next finished download of this connection (gen), 0 if none
EM_JS(int, web_dl_done, (int gen, int *status), {
    var d = Module['q2dl'];
    while (d && d.listos && d.listos.length) {
        var x = d.listos.shift();
        if (x[1] === gen) {
            HEAP32[status >> 2] = x[2];
            return x[0];
        }
    }
    return 0;
});

static cvar_t   *cl_http_downloads;
static bool     web_active;
static int      web_gen = 1;    // bumped on disconnect: late answers are ignored

void HTTP_Init(void)
{
    cl_http_downloads = Cvar_Get("cl_http_downloads", "1", 0);
}

void HTTP_Shutdown(void)
{
    HTTP_CleanupDownloads();
}

/*
===============
HTTP_SetServer

The download URL the server announces is ignored: browsers can only fetch
from servers that allow it (CORS), so the page decides where files come from.
===============
*/
void HTTP_SetServer(const char *url)
{
    web_active = false;

    if (NET_IsLocalAddress(&cls.serverAddress))
        return;
    if (allow_download->integer == -1 || !cl_http_downloads->integer)
        return;
    if (!web_dl_available())
        return;

    web_active = true;
    Com_DPrintf("[HTTP] Downloads through the browser\n");
}

/*
===============
HTTP_QueueDownload

Called from the precache check. Q_ERR(ENOSYS) makes the caller use UDP.
Every queued file starts at once (the page limits how many run together),
so a UDP download never picks up an entry meant for HTTP.
===============
*/
int HTTP_QueueDownload(const char *path, dltype_t type)
{
    char dest[MAX_OSPATH];
    dlqueue_t *q;
    int ret;

    if (!web_active || !web_dl_known(path))
        return Q_ERR(ENOSYS);

    if (Q_snprintf(dest, sizeof(dest), "%s/%s", fs_gamedir, path) >= sizeof(dest))
        return Q_ERR(ENAMETOOLONG);

    ret = CL_QueueDownload(path, type);
    if (ret)
        return ret;

    q = LIST_LAST(dlqueue_t, &cls.download.queue, entry);
    q->state = DL_RUNNING;
    web_dl_start((int)(intptr_t)q, web_gen, q->path, dest);
    return Q_ERR_SUCCESS;
}

// in game, after files arrived: look again for the ones that were missing and
// reload every player (a teammate's new team or class may be ready now)
static void reload_players(void)
{
    const char *s;
    int i;

    R_ForgetMissingImages();
    for (i = 0; i < MAX_CLIENTS; i++) {
        s = cl.configstrings[cl.csr.playerskins + i];
        if (s[0])
            CL_LoadClientinfo(&cl.clientinfo[i], s);
    }
}

// the page couldn't get it: ask the game server for it over UDP instead
// (not in game: a UDP download would compete with the game)
static void fallback_udp(dlqueue_t *q)
{
    char path[MAX_QPATH];
    dltype_t type = q->type;

    if (cls.state == ca_active) {
        CL_FinishDownload(q);
        return;
    }

    Q_strlcpy(path, q->path, sizeof(path));
    List_Remove(&q->entry);
    Z_Free(q);
    cls.download.pending--;

    if (CL_QueueDownload(path, type) == Q_ERR_SUCCESS)
        CL_StartNextDownload();
}

void HTTP_RunDownloads(void)
{
    dlqueue_t *q;
    int id, status;
    bool finished = false;

    if (!web_active)
        return;

    while ((id = web_dl_done(web_gen, &status))) {
        q = (dlqueue_t *)(intptr_t)id;
        if (status == 200) {
            CL_FinishDownload(q);
            Com_Printf("[HTTP] %s [%d remaining file%s]\n", q->path,
                       cls.download.pending, cls.download.pending == 1 ? "" : "s");
        } else {
            Com_Printf("[HTTP] %s [error %d, trying UDP]\n", q->path, status);
            fallback_udp(q);
        }
        finished = true;
    }

    if (!finished)
        return;

    if (cls.state == ca_active && !cls.download.pending)
        reload_players();

    // see if we have more to download
    CL_RequestNextDownload();
}

void HTTP_CleanupDownloads(void)
{
    web_gen++;
}
