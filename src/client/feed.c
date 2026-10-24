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

// feed.c -- kill feed and chat panel for demo playback
//
// The server prints every death and chat line, and demos keep those prints.
// Deaths are turned into short "killer  verb  victim" lines (top right) and
// chat goes to its own panel (bottom left). Both run on demo time, so they
// stay up while paused, last longer in slow motion and come out the same in
// video clips rendered at a fixed frame rate.
//
// When a DM2 demo opens, demo.c reads it to the end once (the same way a
// seek does) and every print lands here: the whole list of deaths and chat
// is known, so after any jump the panels show exactly what was said and who
// died in the seconds before, the timeline marks the frags of the watched
// player and frag_next / frag_prev jump between them. MVD demos and demos
// that can't be read ahead fill the list as they play.
//
// D-Day sends an obituary in pieces: victim, " was sniped by ", killer, tail
// ("'s rifle - ** HEADSHOT **\n"). Mods that send one whole line are parsed
// by looking for the players' names in it.

#include "client.h"

#define FEED_NAME       32
#define FEED_TEXT       160
#define MAX_PIECES      8
#define MAX_LINES       16

#define FRAG_LEAD       2000    // a jump to a frag lands this long before it, ms

typedef enum {
    TEAM_NONE,
    TEAM_ALLIES,
    TEAM_AXIS
} team_t;

typedef struct {
    int64_t time;               // demo time, ms
    int     frame;              // demo frame, for seeking (1/10 s)
    float   pos;                // demo position 0-1, for the timeline
    char    killer[FEED_NAME];  // empty: died on their own
    char    victim[FEED_NAME];
    char    label[FEED_NAME];   // "sniped", "grenade", "suicide"...
    team_t  kteam, vteam;
    bool    headshot;
    bool    teamkill;
} kill_t;

typedef struct {
    int64_t time;
    bool    server;             // a server message, not chat
    char    tag[8];             // "usa", "grm"... (D-Day team chat)
    char    name[FEED_NAME];
    team_t  team;
    char    text[FEED_TEXT];
} chat_t;

// both lists are sorted by time
static kill_t   *kills;
static int      num_kills, max_kills;
static chat_t   *chats;
static int      num_chats, max_chats;

static bool     indexing;       // demo.c is reading the demo ahead
static bool     indexed;        // ...and finished: the lists are complete

// obituary pieces of the current server frame
static struct {
    int     frame;
    int     count;
    char    text[MAX_PIECES][MAX_STRING_CHARS];
} acc;

static cvar_t   *visor_killfeed;
static cvar_t   *visor_chat;
static cvar_t   *clip_killfeed;
static cvar_t   *clip_chat;
static cvar_t   *visor_killfeed_time;
static cvar_t   *visor_killfeed_lines;
static cvar_t   *visor_chat_time;
static cvar_t   *visor_chat_lines;
static cvar_t   *visor_feed_size;

// same clock as the camera paths: follows the demo through seeks. It works
// out to the number of the server frame on screen, in ms.
static int64_t demo_time(void)
{
    return cl.time + (int64_t)cl.serverdelta * CL_FRAMETIME;
}

// when a print arrives: the last server frame parsed (also while seeking)
static int64_t print_time(void)
{
    return (int64_t)cl.frame.number * CL_FRAMETIME;
}

static bool watching_demo(void)
{
    if (cls.state != ca_active)
        return false;
#if USE_MVD_CLIENT
    if (sv_running->integer == ss_broadcast)
        return true;
#endif
    return cls.demo.playback;
}

// where in the demo we are right now, also in the middle of a seek
static void demo_position(int *frame, float *pos)
{
    *frame = 0;
    *pos = 0;
    if (cls.demo.playback) {
        int64_t at = FS_Tell(cls.demo.playback);
        *frame = cls.demo.frames_read;
        if (cls.demo.file_size && at > cls.demo.file_offset)
            *pos = Q_clipf((float)(at - cls.demo.file_offset) / cls.demo.file_size, 0, 1);
    } else {
        CL_DemoPosition(frame, pos, NULL);
    }
}

static void flush_pieces(void);

void CL_FeedClear(void)
{
    Z_Freep(&kills);
    Z_Freep(&chats);
    num_kills = max_kills = 0;
    num_chats = max_chats = 0;
    indexing = indexed = false;
    acc.count = 0;
}

void CL_FeedIndexBegin(void)
{
    indexing = true;
}

void CL_FeedIndexEnd(void)
{
    flush_pieces();
    indexing = false;
    indexed = true;
    Com_DPrintf("Feed index: %d deaths, %d chat lines\n", num_kills, num_chats);
}

bool CL_FeedIndexed(void)
{
    return indexed;
}

/*
===============================================================================

PLAYERS

Names and models come straight from the configstrings ("name\model/skin"):
while the demo is read ahead those are up to date, the client's player info
isn't.

===============================================================================
*/

static const char *player_string(int i)
{
    if (i < 0 || i >= cl.maxclients || i >= MAX_CLIENTS)
        return "";
    return cl.configstrings[cl.csr.playerskins + i];
}

static size_t player_name_len(const char *s)
{
    const char *p = strchr(s, '\\');
    return p ? p - s : strlen(s);
}

static int find_player(const char *name)
{
    size_t len = strlen(name);

    for (int i = 0; i < MAX_CLIENTS && i < cl.maxclients; i++) {
        const char *s = player_string(i);
        if (len && player_name_len(s) == len && !strncmp(s, name, len))
            return i;
    }
    return -1;
}

// D-Day teams by model folder (usa/class_infantry...)
static team_t player_team(const char *name)
{
    static const char *const allies[] = { "usa", "gbr", "rus", "pol", "aus", "fra" };
    static const char *const axis[] = { "grm", "ita", "jpn" };
    char model[MAX_QPATH];
    const char *s, *p;
    int i = find_player(name);

    if (i < 0)
        return TEAM_NONE;
    s = player_string(i);
    p = strchr(s, '\\');
    if (!p)
        return TEAM_NONE;
    Q_strlcpy(model, p + 1, sizeof(model));
    if ((p = strchr(model, '/')))
        model[p - model] = 0;

    for (int k = 0; k < q_countof(allies); k++)
        if (!Q_stricmp(model, allies[k]))
            return TEAM_ALLIES;
    for (int k = 0; k < q_countof(axis); k++)
        if (!Q_stricmp(model, axis[k]))
            return TEAM_AXIS;
    return TEAM_NONE;
}

// longest player name that s starts with (raw names, as the server sends them)
static size_t name_prefix(const char *s)
{
    size_t best = 0;

    for (int i = 0; i < MAX_CLIENTS && i < cl.maxclients; i++) {
        const char *n = player_string(i);
        size_t len = player_name_len(n);
        if (len > best && !strncmp(s, n, len))
            best = len;
    }
    return best;
}

/*
===============================================================================

PARSING

===============================================================================
*/

// printable copy for our colored drawing: no control characters, no high
// bit (that would ignore the color), spaces trimmed at both ends
static void clean_copy(char *dst, size_t size, const char *src, size_t len)
{
    size_t n = 0;

    while (len && *src && n + 1 < size) {
        int c = *src++ & 127;
        len--;
        if (c == '\n' || c == '\r' || c == '\t')
            c = ' ';
        if (c < 32 && c != 0x10 && c != 0x11)
            continue;   // D-Day puts a \x02 in front of some names
        if (c == ' ' && !n)
            continue;
        dst[n++] = c;
    }
    while (n && dst[n - 1] == ' ')
        n--;
    dst[n] = 0;
}

static const char *short_weapon(const char *w)
{
    if (!Q_stricmp(w, "handgrenade"))
        return "grenade";
    if (!Q_stricmp(w, "explosive attack"))
        return "explosive";
    return w;
}

// tail "'s light machine gun - ** HEADSHOT **" -> "light machine gun"
static void tail_weapon(char *dst, size_t size, const char *tail)
{
    const char *end;

    *dst = 0;
    if (strncmp(tail, "'s ", 3))
        return;
    tail += 3;
    for (end = tail; *end && *end != '\n' && *end != ')' &&
         strncmp(end, " - ", 3) && strncmp(end, " (", 2); end++)
        ;
    clean_copy(dst, size, tail, end - tail);
}

static void make_label(char *dst, size_t size, const char *mid, const char *weapon)
{
    size_t len = strlen(mid);

    // "was sniped by" -> "sniped"; "was killed by X's airstrike" -> "airstrike"
    if (!strncmp(mid, "was ", 4) && len > 7 && !strcmp(mid + len - 3, " by")) {
        if (*weapon && !strcmp(mid, "was killed by")) {
            Q_strlcpy(dst, short_weapon(weapon), size);
            return;
        }
        clean_copy(dst, size, mid + 4, len - 7);
        return;
    }
    if (Q_stristr(mid, "wounds")) {
        Q_strlcpy(dst, "wounds", size);
        return;
    }
    if (Q_stristr(mid, "fell to")) {
        Q_strlcpy(dst, "fell", size);
        return;
    }
    // "did not survive", "didn't see", "ate"... say the weapon instead
    if (*weapon) {
        Q_strlcpy(dst, short_weapon(weapon), size);
        return;
    }
    clean_copy(dst, size, mid, len);
}

// Lists stay sorted. Something older than the newest entry means we went back
// in a demo that isn't indexed: what follows it is heard again, drop it.
#define PUSH(list, num, max, t) do {                                    \
        while (num && list[num - 1].time > t)                           \
            num--;                                                      \
        if (num == max) {                                               \
            max = max ? max * 2 : 256;                                  \
            list = Z_Realloc(list, max * sizeof(list[0]));              \
        }                                                               \
        memset(&list[num], 0, sizeof(list[0]));                         \
        list[num].time = t;                                             \
    } while (0)

static kill_t *new_kill(void)
{
    int64_t t = print_time();
    kill_t *k;

    PUSH(kills, num_kills, max_kills, t);
    k = &kills[num_kills++];
    demo_position(&k->frame, &k->pos);
    return k;
}

static chat_t *new_chat(void)
{
    int64_t t = print_time();

    PUSH(chats, num_chats, max_chats, t);
    return &chats[num_chats++];
}

static bool add_kill(const char *victim, const char *mid, const char *killer, const char *tail)
{
    char v[FEED_NAME], m[MAX_STRING_CHARS], kname[FEED_NAME], weapon[FEED_NAME];
    kill_t *k;

    clean_copy(v, sizeof(v), victim, SIZE_MAX);
    clean_copy(m, sizeof(m), mid, SIZE_MAX);
    clean_copy(kname, sizeof(kname), killer, SIZE_MAX);
    if (!*v || !*m || !*kname)
        return false;

    // what follows the killer's name has to look like the end of an obituary
    if (*tail && *tail != '\n' && strncmp(tail, "'s ", 3) &&
        strncmp(tail, " - ", 3) && strncmp(tail, " (", 2) && *tail != ')')
        return false;

    tail_weapon(weapon, sizeof(weapon), tail);

    k = new_kill();
    Q_strlcpy(k->victim, v, sizeof(k->victim));
    Q_strlcpy(k->killer, kname, sizeof(k->killer));
    make_label(k->label, sizeof(k->label), m, weapon);
    k->vteam = player_team(v);
    k->kteam = player_team(kname);
    k->headshot = Q_stristr(tail, "HEADSHOT");
    k->teamkill = Q_stristr(tail, "Friendly fire") ||
        (k->kteam && k->kteam == k->vteam && strcmp(v, kname));
    return true;
}

// "X killed himself." and the like: a death with no killer
static bool add_death(const char *victim, const char *tail)
{
    static const char *const marks[] = {
        "himself", "herself", "itself", "death", "suicide", "drown", "squish",
        "sank", "melt", "lava", "cratered", "smaller gun", "pin back",
        "saw the light", "found a way out", "blew up"
    };
    char v[FEED_NAME], t[FEED_NAME];
    kill_t *k;
    int i;

    for (i = 0; i < q_countof(marks); i++)
        if (Q_stristr(tail, marks[i]))
            break;
    if (i == q_countof(marks))
        return false;

    clean_copy(v, sizeof(v), victim, SIZE_MAX);
    clean_copy(t, sizeof(t), tail, SIZE_MAX);
    if (!*v || !*t)
        return false;

    k = new_kill();
    Q_strlcpy(k->victim, v, sizeof(k->victim));
    k->vteam = player_team(v);
    if (Q_stristr(t, "killed himself") || Q_stristr(t, "suicide"))
        Q_strlcpy(k->label, "suicide", sizeof(k->label));
    else if (Q_stristr(t, "blew himself up"))
        Q_strlcpy(k->label, "blew up", sizeof(k->label));
    else if (Q_stristr(t, "fell to"))
        Q_strlcpy(k->label, "fell", sizeof(k->label));
    else {
        size_t len = strlen(t);
        if (len && t[len - 1] == '.')
            t[len - 1] = 0;
        Q_strlcpy(k->label, t, sizeof(k->label));
    }
    return true;
}

// a whole obituary in one line: look for the two players' names in it
static bool parse_line(const char *s)
{
    char mid[MAX_STRING_CHARS];
    size_t vlen = name_prefix(s), klen;
    const char *p;

    if (!vlen || s[vlen] != ' ')
        return false;

    for (p = s + vlen + 1; *p; p++) {
        if (p[-1] != ' ' || !(klen = name_prefix(p)))
            continue;
        Q_strlcpy(mid, s + vlen, min(p - (s + vlen) + 1, sizeof(mid)));
        if (add_kill(va("%.*s", (int)vlen, s), mid, va("%.*s", (int)klen, p), p + klen))
            return true;
    }
    return add_death(va("%.*s", (int)vlen, s), s + vlen);
}

static void add_server_message(const char *s)
{
    char text[FEED_TEXT];
    chat_t *c;

    clean_copy(text, sizeof(text), s, SIZE_MAX);
    if (!*text)
        return;
    c = new_chat();
    c->server = true;
    Q_strlcpy(c->text, text, sizeof(c->text));
}

// obituary pieces collected, a full line is ready
static void flush_pieces(void)
{
    char line[MAX_STRING_CHARS];
    char (*f)[MAX_STRING_CHARS] = acc.text;
    bool done = false;
    int i, n = acc.count;

    acc.count = 0;
    if (!n)
        return;

    if (n == 4)
        done = add_kill(f[0], f[1], f[2], f[3]);
    else if (n == 2)
        done = add_death(f[0], f[1]);

    if (done)
        return;

    line[0] = 0;
    for (i = 0; i < n; i++)
        Q_strlcat(line, f[i], sizeof(line));
    if (!parse_line(line))
        add_server_message(line);
}

// "usa +=EC=+ZeRo: na" or "[MR]+LolPaoAsi+: hola"
static void add_chat(const char *s)
{
    char raw[MAX_STRING_CHARS];
    const char *p = s;
    size_t len;
    chat_t *c = new_chat();

    // D-Day team chat starts with the team: "usa ", "grm "...
    if (strlen(s) > 4 && Q_isalpha(s[0]) && Q_isalpha(s[1]) && Q_isalpha(s[2]) &&
        s[3] == ' ' && (len = name_prefix(s + 4)) && !strncmp(s + 4 + len, ": ", 2)) {
        clean_copy(c->tag, sizeof(c->tag), s, 3);
        p = s + 4;
    }

    len = name_prefix(p);
    if (!len || strncmp(p + len, ": ", 2)) {
        // unknown name: split at the first ": " anyway
        const char *colon = strstr(p, ": ");
        len = colon && colon - p < FEED_NAME ? colon - p : 0;
    }

    if (len) {
        Q_strlcpy(raw, p, min(len + 1, sizeof(raw)));
        clean_copy(c->name, sizeof(c->name), raw, SIZE_MAX);
        c->team = player_team(raw);
        p += len + 2;
    }
    clean_copy(c->text, sizeof(c->text), p, SIZE_MAX);
}

/*
===============
CL_FeedPrint

Every svc_print of a demo goes through here, also the ones skipped over by
a seek (the console only gets the ones played).
===============
*/
void CL_FeedPrint(int level, const char *s)
{
    if (!watching_demo())
        return;

    // the list is complete already
    if (indexed && !indexing)
        return;

    if (level == PRINT_CHAT) {
        flush_pieces();
        add_chat(s);
        return;
    }

    if (level != PRINT_MEDIUM && level != PRINT_HIGH)
        return;

    // pieces of one obituary come in the same frame
    if (acc.count && acc.frame != cl.frame.number)
        flush_pieces();

    if (level == PRINT_HIGH) {
        flush_pieces();
        add_server_message(s);
        return;
    }

    acc.frame = cl.frame.number;
    if (acc.count < MAX_PIECES)
        Q_strlcpy(acc.text[acc.count++], s, sizeof(acc.text[0]));

    if (strchr(s, '\n') || acc.count == MAX_PIECES)
        flush_pieces();
}

/*
===============================================================================

FRAGS OF THE WATCHED PLAYER

===============================================================================
*/

// the player whose frags count: the one watched or followed (NULL = anyone)
static const char *frag_player(void)
{
    viewkind_t kind;
    return CL_ViewedPlayer(&kind);
}

static bool is_frag(const kill_t *k, const char *name)
{
    if (!*k->killer || k->teamkill)
        return false;
    return !name || !strcmp(k->killer, name);
}

/*
===============
CL_FeedFragMarks

Timeline positions (0-1) of the watched player's frags. Returns how many,
none without a watched player.
===============
*/
int CL_FeedFragMarks(float *pos, int max)
{
    const char *name = frag_player();
    int n = 0;

    if (!name || !watching_demo())
        return 0;

    for (int i = 0; i < num_kills && n < max; i++)
        if (is_frag(&kills[i], name))
            pos[n++] = kills[i].pos;
    return n;
}

// frag_next / frag_prev: jump to just before the next / previous frag
static void jump_to_frag(int dir)
{
    const char *name = frag_player();
    int64_t now = demo_time();
    const kill_t *k = NULL;
    int i, num = 0, which = 0;

    if (!watching_demo()) {
        Com_Printf("Abre un demo primero.\n");
        return;
    }

    for (i = 0; i < num_kills; i++) {
        const kill_t *t = &kills[i];
        int64_t at = t->time - FRAG_LEAD;

        if (!is_frag(t, name))
            continue;
        num++;
        if (dir > 0 && !k && at > now + 500) {
            k = t;
            which = num;
        } else if (dir < 0 && at < now - 500) {
            k = t;
            which = num;
        }
    }

    if (!k) {
        if (!num)
            SCR_CenterPrint(name ? va("%s no tiene frags", name) : "No hay frags", false);
        else
            SCR_CenterPrint(dir > 0 ? "No hay mas frags" : "No hay frags antes", false);
        return;
    }

    int frame = max(k->frame - FRAG_LEAD / 100, 0);
    const char *what = name ? "Frag" : "Muerte";    // nobody watched: any death
    Cmd_ExecuteString(&cmd_buffer, va("seek %d.%d", frame / 10, frame % 10));
    if (indexed)
        SCR_CenterPrint(va("%s %d de %d", what, which, num), false);
    else
        SCR_CenterPrint(va("%s %d", what, which), false);
}

static void Frag_Next_f(void)
{
    jump_to_frag(1);
}

static void Frag_Prev_f(void)
{
    jump_to_frag(-1);
}

/*
===============================================================================

DRAWING

===============================================================================
*/

#define COLOR_ALLIES    MakeColor(120, 175, 255, 255)
#define COLOR_AXIS      MakeColor(255, 115, 95, 255)
#define COLOR_NONE      MakeColor(235, 235, 235, 255)
#define COLOR_LABEL     MakeColor(185, 185, 185, 255)
#define COLOR_HEADSHOT  MakeColor(255, 180, 40, 255)
#define COLOR_TEAMKILL  MakeColor(255, 70, 70, 255)
#define COLOR_SERVER    MakeColor(160, 160, 160, 255)
#define COLOR_TAG       MakeColor(150, 150, 150, 255)
#define COLOR_BG        MakeColor(0, 0, 0, 120)
#define COLOR_BG_MINE   MakeColor(130, 95, 0, 150)

static uint32_t team_color(team_t t)
{
    return t == TEAM_ALLIES ? COLOR_ALLIES : t == TEAM_AXIS ? COLOR_AXIS : COLOR_NONE;
}

static uint32_t with_alpha(uint32_t color, float alpha)
{
    color_t c = { .u32 = color };
    c.u8[3] = Q_rint(c.u8[3] * alpha);
    return c.u32;
}

// 1 while fresh, fades out over the last half second; 0 once gone or if it
// hasn't happened yet
static float fade(int64_t time, int64_t now, float seconds)
{
    int64_t age = now - time;
    int64_t life = Q_rint(seconds * 1000);

    if (age < 0 || age >= life)
        return 0;
    if (life - age < 500)
        return (life - age) / 500.0f;
    return 1;
}

// index of the first entry newer than now (entries before it have happened)
static int kills_until(int64_t now)
{
    int l = 0, r = num_kills;
    while (l < r) {
        int m = (l + r) / 2;
        if (kills[m].time <= now) l = m + 1; else r = m;
    }
    return l;
}

static int chats_until(int64_t now)
{
    int l = 0, r = num_chats;
    while (l < r) {
        int m = (l + r) / 2;
        if (chats[m].time <= now) l = m + 1; else r = m;
    }
    return l;
}

static int draw_text(int x, int y, int flags, const char *s, size_t len,
                     uint32_t color, float alpha, qhandle_t font)
{
    R_SetColor(with_alpha(color, alpha));
    return R_DrawString(x, y, flags, len, s, font);
}

bool CL_FeedKillsOn(void)
{
    return (CL_CaptureActive() ? clip_killfeed : visor_killfeed)->integer;
}

bool CL_FeedChatOn(void)
{
    return (CL_CaptureActive() ? clip_chat : visor_chat)->integer;
}

// our panels replace the console lines at the top while watching a demo
bool CL_FeedHidesNotify(void)
{
    return watching_demo() && (visor_killfeed->integer || visor_chat->integer);
}

static void draw_kills(qhandle_t font, int right, int y)
{
    const char *watched = frag_player();
    int64_t now = demo_time();
    int i, lines = 0, max_lines = Cvar_ClampInteger(visor_killfeed_lines, 1, MAX_LINES);
    const kill_t *list[MAX_LINES];
    int only_mine = (CL_CaptureActive() ? clip_killfeed : visor_killfeed)->integer == 2;
    float time = Cvar_ClampValue(visor_killfeed_time, 1, 60);

    // newest ones that are still up, oldest first
    for (i = kills_until(now) - 1; i >= 0 && lines < max_lines; i--) {
        const kill_t *k = &kills[i];
        if (!fade(k->time, now, time))
            break;
        if (only_mine && watched && strcmp(k->killer, watched) && strcmp(k->victim, watched))
            continue;
        list[lines++] = k;
    }

    for (i = lines - 1; i >= 0; i--) {
        const kill_t *k = list[i];
        float a = fade(k->time, now, time);
        bool mine = watched && (!strcmp(k->killer, watched) || !strcmp(k->victim, watched));
        // [killer  ]label[ HS][ TK]  victim
        size_t len = strlen(k->label) + 2 + strlen(k->victim);
        int x;

        if (*k->killer)
            len += strlen(k->killer) + 2;
        if (k->headshot)
            len += 3;
        if (k->teamkill)
            len += 3;

        x = right - CONCHAR_WIDTH - len * CONCHAR_WIDTH;
        R_DrawFill32(x - 3, y - 1, len * CONCHAR_WIDTH + 6, CONCHAR_HEIGHT + 2,
                     with_alpha(mine ? COLOR_BG_MINE : COLOR_BG, a));

        if (*k->killer)
            x = draw_text(x, y, 0, k->killer, SIZE_MAX, team_color(k->kteam), a, font) +
                CONCHAR_WIDTH * 2;
        x = draw_text(x, y, 0, k->label, SIZE_MAX,
                      k->teamkill ? COLOR_TEAMKILL : COLOR_LABEL, a, font);
        if (k->headshot)
            x = draw_text(x, y, 0, " HS", SIZE_MAX, COLOR_HEADSHOT, a, font);
        if (k->teamkill)
            x = draw_text(x, y, 0, " TK", SIZE_MAX, COLOR_TEAMKILL, a, font);
        draw_text(x + CONCHAR_WIDTH * 2, y, 0, k->victim, SIZE_MAX, team_color(k->vteam), a, font);

        y += CONCHAR_HEIGHT + 3;
    }
}

// one chat entry, wrapped to cols characters; returns how many rows it
// takes. Drawn with its last row at y_bottom when draw is set.
static int draw_chat(const chat_t *c, int x, int y_bottom, int cols, float a,
                     qhandle_t font, bool draw)
{
    const char *text = c->text;
    size_t width = cols, len = strlen(text), head = 0, first;
    uint32_t color = c->server ? COLOR_SERVER : COLOR_NONE;
    int rows, y;

    if (!c->server) {
        if (*c->tag)
            head += strlen(c->tag) + 3;     // "(usa) "
        if (*c->name)
            head += strlen(c->name) + 2;    // "name: "
    }
    if (head + 8 > width)
        head = 0;   // no room for the name, just the text
    first = width - head;

    rows = 1 + (len > first ? (len - first + width - 1) / width : 0);
    if (!draw)
        return rows;

    y = y_bottom - (rows - 1) * CONCHAR_HEIGHT;

    // see-through band behind each row, like the kill feed
    for (int r = 0; r < rows; r++) {
        size_t n = r ? min(width, len - first - (r - 1) * width) : head + min(first, len);
        R_DrawFill32(x - 3, y + r * CONCHAR_HEIGHT - 1, n * CONCHAR_WIDTH + 6,
                     CONCHAR_HEIGHT + 1, with_alpha(COLOR_BG, a * 0.8f));
    }

    if (head) {
        if (*c->tag) {
            x = draw_text(x, y, UI_DROPSHADOW, "(", 1, COLOR_TAG, a, font);
            x = draw_text(x, y, UI_DROPSHADOW, c->tag, SIZE_MAX, COLOR_TAG, a, font);
            x = draw_text(x, y, UI_DROPSHADOW, ") ", 2, COLOR_TAG, a, font);
        }
        if (*c->name) {
            x = draw_text(x, y, UI_DROPSHADOW, c->name, SIZE_MAX, team_color(c->team), a, font);
            x = draw_text(x, y, UI_DROPSHADOW, ": ", 2, COLOR_NONE, a, font);
        }
        x -= head * CONCHAR_WIDTH;
    }

    draw_text(x + head * CONCHAR_WIDTH, y, UI_DROPSHADOW, text, first, color, a, font);
    text += min(first, len);
    while (*text) {
        y += CONCHAR_HEIGHT;
        draw_text(x, y, UI_DROPSHADOW, text, width, color, a, font);
        text += min(width, strlen(text));
    }
    return rows;
}

static void draw_chats(qhandle_t font, int left, int w, int y_bottom)
{
    int64_t now = demo_time();
    int i, rows = 0, max_rows = Cvar_ClampInteger(visor_chat_lines, 1, MAX_LINES * 2);
    int cols = max(w / 2 / CONCHAR_WIDTH, 20);
    int with_server = (CL_CaptureActive() ? clip_chat : visor_chat)->integer == 2;
    float time = Cvar_ClampValue(visor_chat_time, 1, 120);
    int x = left + CONCHAR_WIDTH;

    // newest at the bottom, older ones above
    for (i = chats_until(now) - 1; i >= 0; i--) {
        const chat_t *c = &chats[i];
        float a = fade(c->time, now, time);
        int n;

        if (!a)
            break;
        if (c->server && !with_server)
            continue;
        n = draw_chat(c, x, 0, cols, a, font, false);
        if (rows + n > max_rows)
            break;
        draw_chat(c, x, y_bottom - rows * CONCHAR_HEIGHT, cols, a, font, true);
        rows += n;
    }
}

/*
===============
CL_FeedDraw

scale: HUD scale (larger = smaller characters). game_hud: the game's own
status bar is on screen, keep clear of it. bottom_rows: rows taken at the
bottom by the viewer's timeline.
===============
*/
void CL_FeedDraw(qhandle_t font, float scale, bool game_hud, int bottom_rows)
{
    int x = 0, y = 0, w = r_config.width, h = r_config.height;
    float size = Cvar_ClampValue(visor_feed_size, 0.5f, 3);
    int top, bottom;

    if (!watching_demo())
        return;

    // in a clip, stay inside the part of the window that is recorded
    CL_CaptureRect(&x, &y, &w, &h);

    // D-Day keeps kills and points at the top right, the health at the
    // bottom left. These gaps are in HUD rows, the HUD doesn't change size.
    top = game_hud ? 12 : 2;
    bottom = (game_hud ? 10 : 3) + bottom_rows;

    scale /= size;
    R_SetScale(scale);
    x = Q_rint(x * scale);
    y = Q_rint(y * scale);
    w = Q_rint(w * scale);
    h = Q_rint(h * scale);

    if (CL_FeedKillsOn())
        draw_kills(font, x + w, y + Q_rint(CONCHAR_HEIGHT * top / size));

    if (CL_FeedChatOn())
        draw_chats(font, x, w, y + h - Q_rint(CONCHAR_HEIGHT * bottom / size));

    R_ClearColor();
    R_SetScale(1.0f);
}

static const cmdreg_t c_feed[] = {
    { "frag_next", Frag_Next_f },
    { "frag_prev", Frag_Prev_f },
    { NULL }
};

void CL_InitFeed(void)
{
    visor_killfeed = Cvar_Get("visor_killfeed", "1", CVAR_ARCHIVE);
    visor_chat = Cvar_Get("visor_chat", "1", CVAR_ARCHIVE);
    clip_killfeed = Cvar_Get("clip_killfeed", "1", CVAR_ARCHIVE);
    clip_chat = Cvar_Get("clip_chat", "0", CVAR_ARCHIVE);
    visor_killfeed_time = Cvar_Get("visor_killfeed_time", "6", CVAR_ARCHIVE);
    visor_killfeed_lines = Cvar_Get("visor_killfeed_lines", "5", CVAR_ARCHIVE);
    visor_chat_time = Cvar_Get("visor_chat_time", "10", CVAR_ARCHIVE);
    visor_chat_lines = Cvar_Get("visor_chat_lines", "6", CVAR_ARCHIVE);
    visor_feed_size = Cvar_Get("visor_feed_size", "1", CVAR_ARCHIVE);

    Cmd_Register(c_feed);
}
