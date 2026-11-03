#include "raylib.h"
#include <string.h>
#include <stdio.h>

// 实现在 ime_fix.c，避免 windows.h 与 raylib.h 的名字冲突
void DisableIME(void *windowHandle);

// ---------------------------------------------------------------------
// 界面文字集中定义
// ---------------------------------------------------------------------
#define TXT_TITLE   "吟唱"
#define TXT_IDLE    "按空格开始吟唱"
#define TXT_CASTING "吟唱中"
#define TXT_INPUT   "已输入"
#define TXT_HP      "生命"
#define TXT_MP      "魔力"
#define TXT_SPELLS  "法术"
#define TXT_HIT     "命中"
#define TXT_MISS    "失败"
#define TXT_ENEMY   "敌人"
#define TXT_WIN     "敌人倒下"
#define TXT_LOSE    "你倒下了"
#define TXT_SP1     "火球术   J"
#define TXT_SP2     "冰锥     J + K"
#define TXT_SP3     "雷击     J → Shift → J"
#define TXT_EXT     "延长 0.5 秒  消耗 5 魔力"

static const char *UI_TEXT[] = {
    TXT_TITLE, TXT_IDLE, TXT_CASTING, TXT_INPUT, TXT_HP, TXT_MP, TXT_SPELLS,
    TXT_HIT, TXT_MISS, TXT_ENEMY, TXT_WIN, TXT_LOSE, TXT_SP1, TXT_SP2, TXT_SP3, TXT_EXT
};
#define UI_TEXT_COUNT ((int)(sizeof(UI_TEXT) / sizeof(UI_TEXT[0])))

static int Utf8ToCodepoints(const char *text, int *out, int max) {
    int n = 0;
    const unsigned char *p = (const unsigned char *)text;
    while (*p && n < max) {
        int cp = 0, len = 1;
        if (*p < 0x80) { cp = *p; len = 1; }
        else if ((*p >> 5) == 0x6) { cp = *p & 0x1F; len = 2; }
        else if ((*p >> 4) == 0xE) { cp = *p & 0x0F; len = 3; }
        else { cp = *p & 0x07; len = 4; }
        for (int i = 1; i < len; i++) cp = (cp << 6) | (p[i] & 0x3F);
        out[n++] = cp;
        p += len;
    }
    return n;
}

static int CollectUIGlyphs(int *out, int max) {
    int n = 0;
    for (int c = 32; c < 127; c++) out[n++] = c;
    for (int i = 0; i < UI_TEXT_COUNT; i++)
        n += Utf8ToCodepoints(UI_TEXT[i], out + n, max - n);
    return n;
}

// ---------------------------------------------------------------------
// 输入历史
// ---------------------------------------------------------------------
#define HIST 256

typedef struct { unsigned char j, k, shift; } RawFrame;
static RawFrame g_hist[HIST];
static int g_histLen = 0;

enum { EV_J = 1, EV_JK, EV_SHIFT };

typedef struct { int type; int frame; } Event;

static int ExtractEvents(Event *ev, int max) {
    int n = 0;
    for (int f = 0; f < g_histLen && n < max; f++) {
        int jUp = g_hist[f].j && (f == 0 || !g_hist[f - 1].j);
        int kUp = g_hist[f].k && (f == 0 || !g_hist[f - 1].k);
        int shUp = g_hist[f].shift && (f == 0 || !g_hist[f - 1].shift);

        if (jUp && kUp) {
            ev[n].type = EV_JK; ev[n].frame = f; n++;
        } else if (jUp) {
            int kNear = 0;
            for (int g = f; g <= f + 3 && g < g_histLen; g++) if (g_hist[g].k) kNear = 1;
            for (int g = f - 3; g < f; g++) if (g >= 0 && g_hist[g].k) kNear = 1;
            ev[n].type = kNear ? EV_JK : EV_J; ev[n].frame = f; n++;
        } else if (kUp) {
            int jNear = 0;
            for (int g = f; g <= f + 3 && g < g_histLen; g++) if (g_hist[g].j) jNear = 1;
            for (int g = f - 3; g < f; g++) if (g >= 0 && g_hist[g].j) jNear = 1;
            if (jNear) { ev[n].type = EV_JK; ev[n].frame = f; n++; }
        }

        if (shUp && n < max) { ev[n].type = EV_SHIFT; ev[n].frame = f; n++; }
    }
    return n;
}

// ---------------------------------------------------------------------
// 法术表
// 注意：全部法术一开局就可用。新手的限制不是"没学会"，而是"来不及搓"
// ---------------------------------------------------------------------
typedef struct {
    const char *name;
    int damage;
    int pattern[4];
    int count;
} Spell;

static const Spell SPELLS[] = {
    { "火球术", 9,  { EV_J },                          1 },
    { "冰锥",   15, { EV_JK },                         1 },
    { "雷击",   24, { EV_J, EV_SHIFT, EV_J },          3 },
};
#define SPELL_COUNT ((int)(sizeof(SPELLS) / sizeof(SPELLS[0])))

static int MatchSpell(Event *ev, int n) {
    if (n == 0) return -1;
    for (int s = 0; s < SPELL_COUNT; s++) {
        if (SPELLS[s].count != n) continue;
        int ok = 1;
        for (int i = 0; i < n; i++) if (ev[i].type != SPELLS[s].pattern[i]) ok = 0;
        if (ok) return s;
    }
    return -1;
}

// ---------------------------------------------------------------------
enum { PH_IDLE = 0, PH_CAST, PH_RESULT };

int main(int argc, char **argv) {
    int shotMode = (argc > 1 && strcmp(argv[1], "--shot") == 0);

    InitWindow(960, 640, "SHIFT - Casting");
    DisableIME(GetWindowHandle());
    SetTargetFPS(60);

    int cps[512];
    int n = CollectUIGlyphs(cps, 512);
    Font font = LoadFontEx("assets/font.ttf", 40, cps, n);
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);

    int phase = PH_IDLE;
    float timer = 0.0f, maxTime = 2.0f;
    float resultTimer = 0.0f;
    int playerHP = 60, playerMaxHP = 60;
    int playerMP = 60, playerMaxMP = 60;
    int enemyHP = 100, enemyMaxHP = 100;

    const char *resultLine1 = "";
    const char *resultLine2 = "";
    int frame = 0;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        frame++;

        RawFrame cur;
        if (shotMode) {
            cur.j     = (frame >= 12 && frame <= 16) || (frame >= 30 && frame <= 34);
            cur.k     = 0;
            cur.shift = (frame >= 20 && frame <= 24);
        } else {
            cur.j     = IsKeyDown(KEY_J);
            cur.k     = IsKeyDown(KEY_K);
            cur.shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        }

        if (phase == PH_IDLE) {
            if (shotMode ? (frame == 5) : IsKeyPressed(KEY_SPACE)) {
                if (enemyHP > 0 && playerHP > 0) {
                    phase = PH_CAST;
                    g_histLen = 0;
                    timer = 2.0f;
                    maxTime = 2.0f;
                }
            }
        }
        else if (phase == PH_CAST) {
            if (g_histLen < HIST) g_hist[g_histLen++] = cur;

            // Shift 延长：每次消耗 5 魔力，换 0.5 秒
            int shUp = cur.shift && (g_histLen < 2 || !g_hist[g_histLen - 2].shift);
            if (shUp && playerMP >= 5) {
                playerMP -= 5;
                timer += 0.5f;
                maxTime += 0.5f;
            }

            timer -= dt;
            if (timer <= 0.0f) {
                timer = 0.0f;

                Event ev[32];
                int ec = ExtractEvents(ev, 32);
                int sid = MatchSpell(ev, ec);

                if (sid >= 0) {
                    enemyHP -= SPELLS[sid].damage;
                    if (enemyHP < 0) enemyHP = 0;
                    resultLine1 = SPELLS[sid].name;
                    resultLine2 = TXT_HIT;
                } else {
                    resultLine1 = "";
                    resultLine2 = TXT_MISS;
                }

                // 敌人反击
                if (enemyHP > 0) {
                    playerHP -= 6;
                    if (playerHP < 0) playerHP = 0;
                }
                // 魔力缓慢回复
                playerMP += 8;
                if (playerMP > playerMaxMP) playerMP = playerMaxMP;

                phase = PH_RESULT;
                resultTimer = 1.1f;
            }
        }
        else if (phase == PH_RESULT) {
            resultTimer -= dt;
            if (resultTimer <= 0.0f) phase = PH_IDLE;
        }

        // ---------- 绘制 ----------
        BeginDrawing();
        ClearBackground((Color){ 14, 15, 19, 255 });

        // 血条
        DrawTextEx(font, TXT_HP, (Vector2){ 40, 30 }, 18, 1, (Color){ 150, 152, 160, 255 });
        DrawRectangle(40, 56, 300, 16, (Color){ 34, 36, 44, 255 });
        DrawRectangle(40, 56, 300 * playerHP / playerMaxHP, 16, (Color){ 70, 150, 110, 255 });

        DrawTextEx(font, TXT_MP, (Vector2){ 40, 84 }, 18, 1, (Color){ 150, 152, 160, 255 });
        DrawRectangle(40, 110, 300, 16, (Color){ 34, 36, 44, 255 });
        DrawRectangle(40, 110, 300 * playerMP / playerMaxMP, 16, (Color){ 80, 120, 200, 255 });

        DrawTextEx(font, TXT_ENEMY, (Vector2){ 620, 30 }, 18, 1, (Color){ 150, 152, 160, 255 });
        DrawRectangle(620, 56, 300, 16, (Color){ 34, 36, 44, 255 });
        DrawRectangle(620, 56, 300 * enemyHP / enemyMaxHP, 16, (Color){ 190, 70, 70, 255 });

        // 吟唱区
        if (phase == PH_IDLE) {
            DrawTextEx(font, TXT_IDLE, (Vector2){ 40, 180 }, 26, 1, (Color){ 170, 172, 180, 255 });
        }

        if (phase == PH_CAST) {
            DrawTextEx(font, TXT_CASTING, (Vector2){ 40, 170 }, 20, 1, (Color){ 150, 152, 160, 255 });
            float ratio = (maxTime > 0) ? timer / maxTime : 0.0f;
            Color barColor = (ratio > 0.3f) ? (Color){ 133, 183, 235, 255 } : (Color){ 224, 75, 74, 255 };
            DrawRectangle(40, 200, 820, 16, (Color){ 34, 36, 44, 255 });
            DrawRectangle(40, 200, (int)(820 * ratio), 16, barColor);

            char tb[32];
            snprintf(tb, sizeof(tb), "%.2f", timer);
            DrawTextEx(font, tb, (Vector2){ 876, 192 }, 22, 1, barColor);

            DrawTextEx(font, TXT_INPUT, (Vector2){ 40, 240 }, 20, 1, (Color){ 150, 152, 160, 255 });
            Event ev[32];
            int ec = ExtractEvents(ev, 32);
            for (int i = 0; i < ec && i < 10; i++) {
                float cx = 40 + i * 78.0f;
                Color fill = (ev[i].type == EV_SHIFT) ? (Color){ 12, 68, 124, 255 } : (Color){ 113, 43, 19, 255 };
                Color line = (ev[i].type == EV_SHIFT) ? (Color){ 133, 183, 235, 255 } : (Color){ 240, 153, 123, 255 };
                DrawRectangleRounded((Rectangle){ cx, 268, 70, 48 }, 0.2f, 4, fill);
                DrawRectangleRoundedLines((Rectangle){ cx, 268, 70, 48 }, 0.2f, 4, line);

                const char *t = (ev[i].type == EV_J) ? "J" : (ev[i].type == EV_JK) ? "JK" : "SH";
                Vector2 sz = MeasureTextEx(font, t, 22, 1);
                DrawTextEx(font, t, (Vector2){ cx + 35 - sz.x / 2, 280 }, 22, 1, RAYWHITE);
            }

            DrawTextEx(font, TXT_EXT, (Vector2){ 40, 340 }, 18, 1, (Color){ 120, 140, 170, 255 });
        }

        if (phase == PH_RESULT) {
            DrawTextEx(font, resultLine1, (Vector2){ 40, 186 }, 38, 1, (Color){ 250, 199, 117, 255 });
            DrawTextEx(font, resultLine2, (Vector2){ 40, 236 }, 26, 1, (Color){ 200, 202, 210, 255 });
        }

        // 法术表
        DrawTextEx(font, TXT_SPELLS, (Vector2){ 40, 400 }, 20, 1, (Color){ 150, 152, 160, 255 });
        DrawTextEx(font, TXT_SP1, (Vector2){ 40, 432 }, 20, 1, (Color){ 180, 182, 190, 255 });
        DrawTextEx(font, TXT_SP2, (Vector2){ 40, 462 }, 20, 1, (Color){ 180, 182, 190, 255 });
        DrawTextEx(font, TXT_SP3, (Vector2){ 40, 492 }, 20, 1, (Color){ 180, 182, 190, 255 });

        if (enemyHP <= 0) DrawTextEx(font, TXT_WIN, (Vector2){ 620, 460 }, 30, 1, (Color){ 250, 199, 117, 255 });
        if (playerHP <= 0) DrawTextEx(font, TXT_LOSE, (Vector2){ 620, 500 }, 30, 1, (Color){ 224, 75, 74, 255 });

        EndDrawing();

        if (shotMode && frame == 60)  { TakeScreenshot("cast_shot.png"); }
        if (shotMode && frame == 165) { TakeScreenshot("cast_result.png"); break; }
    }

    UnloadFont(font);
    CloseWindow();
    return 0;
}
