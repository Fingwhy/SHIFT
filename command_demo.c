#include "raylib.h"
#include <string.h>
#include <stdio.h>

// 实现在 ime_fix.c 里（那个文件才引用 windows.h，避免和 raylib 的名字冲突）
void DisableIME(void *windowHandle);

// ---------------------------------------------------------------------
// 界面文字集中定义，字体字符集自动提取
// ---------------------------------------------------------------------
#define TXT_TITLE   "指令输入系统"
#define TXT_IDLE    "按空格键开始输入指令"
#define TXT_INPUT   "输入中"
#define TXT_USED    "已输入"
#define TXT_RESULT  "结算结果"
#define TXT_NONE    "无动作"
#define TXT_RULES   "规则"
#define TXT_R1      "空格开始，2 秒输入窗口"
#define TXT_R2      "窗口内只按 J 为普通攻击"
#define TXT_R3      "J 之后按 Shift 再按 J 为重攻击"
#define TXT_R4      "J 和 K 同时按为旋转攻击"
#define TXT_R5      "按 Shift 后窗口延长 1 秒"
#define TXT_LIGHT       "普通攻击"
#define TXT_HEAVY       "重攻击"
#define TXT_SPIN        "旋转攻击"
#define TXT_SPIN_HEAVY  "旋转重攻击"
#define TXT_EXTEND      "已延长 1 秒"

static const char *UI_TEXT[] = {
    TXT_TITLE, TXT_IDLE, TXT_INPUT, TXT_USED, TXT_RESULT, TXT_NONE, TXT_RULES,
    TXT_R1, TXT_R2, TXT_R3, TXT_R4, TXT_R5,
    TXT_LIGHT, TXT_HEAVY, TXT_SPIN, TXT_SPIN_HEAVY, TXT_EXTEND
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
// 每帧的原始按键状态，整段存下来，最后统一分析
// ---------------------------------------------------------------------
#define HIST 256

typedef struct {
    unsigned char j, k, shift;
} RawFrame;

static RawFrame g_hist[HIST];
static int g_histLen = 0;

enum { EV_J = 1, EV_JK, EV_SHIFT };

typedef struct {
    int type;
    int frame;
} Event;

// ---------------------------------------------------------------------
// 从原始帧历史里提取"事件序列"
// 同时按的判定：J 按下时，前后 3 帧内 K 有按下 → 合并成 JK
// ---------------------------------------------------------------------
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
// 结算：看最后一个攻击事件，它前面是不是 [同类攻击, Shift]
// ---------------------------------------------------------------------
static const char *Resolve(void) {
    Event ev[32];
    int n = ExtractEvents(ev, 32);

    int last = -1;
    for (int i = n - 1; i >= 0; i--) {
        if (ev[i].type == EV_J || ev[i].type == EV_JK) { last = i; break; }
    }
    if (last < 0) return TXT_NONE;

    int charged = 0;
    if (last >= 2 && ev[last - 1].type == EV_SHIFT && ev[last - 2].type == ev[last].type) {
        charged = 1;
    }

    if (ev[last].type == EV_J) return charged ? TXT_HEAVY : TXT_LIGHT;
    return charged ? TXT_SPIN_HEAVY : TXT_SPIN;
}

// ---------------------------------------------------------------------
// 状态机
// ---------------------------------------------------------------------
enum { PH_IDLE = 0, PH_INPUT, PH_RESULT };

int main(int argc, char **argv) {
    int shotMode = (argc > 1 && strcmp(argv[1], "--shot") == 0);

    InitWindow(900, 560, "SHIFT - Command Input");
    DisableIME(GetWindowHandle());   // 必须紧跟 InitWindow，否则 Shift 会被输入法吃掉
    SetTargetFPS(60);

    int cps[512];
    int n = CollectUIGlyphs(cps, 512);
    Font font = LoadFontEx("assets/font.ttf", 40, cps, n);
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);

    int phase = PH_IDLE;
    float timer = 0.0f;
    float maxTime = 1.0f;        // 当前窗口的总时长，用于画进度条
    int extended = 0;            // 是否已经享受过延长
    const char *resultText = TXT_NONE;
    int frame = 0;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        frame++;

        // ---------- 读取按键 ----------
        RawFrame cur;
        if (shotMode) {          // 截图模式：脚本注入 J → Shift → J
            cur.j     = (frame >= 10 && frame <= 14) || (frame >= 26 && frame <= 30);
            cur.k     = 0;
            cur.shift = (frame >= 18 && frame <= 21);
        } else {
            cur.j     = IsKeyDown(KEY_J);
            cur.k     = IsKeyDown(KEY_K);
            cur.shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        }

        // ---------- 状态机 ----------
        if (phase == PH_IDLE) {
            if (shotMode ? (frame == 5) : IsKeyPressed(KEY_SPACE)) {
                phase = PH_INPUT;
                g_histLen = 0;
                extended = 0;
                timer = 2.0f;
                maxTime = 2.0f;
                resultText = TXT_NONE;
            }
        }
        else if (phase == PH_INPUT) {
            if (g_histLen < HIST) g_hist[g_histLen++] = cur;

            // 已经打出攻击、且按下 Shift → 延长一次
            int shiftUp = cur.shift && (g_histLen < 2 || !g_hist[g_histLen - 2].shift);
            if (!extended && shiftUp) {
                int hasAttack = 0;
                for (int i = 0; i < g_histLen; i++) if (g_hist[i].j || g_hist[i].k) { hasAttack = 1; break; }
                if (hasAttack) {
                    timer += 1.0f;
                    maxTime += 1.0f;
                    extended = 1;
                }
            }

            timer -= dt;
            if (timer <= 0.0f) {
                timer = 0.0f;
                resultText = Resolve();
                phase = PH_RESULT;
            }
        }
        else if (phase == PH_RESULT) {
            if (shotMode ? (frame == 60) : IsKeyPressed(KEY_SPACE)) phase = PH_IDLE;
        }

        // ---------- 绘制 ----------
        BeginDrawing();
        ClearBackground((Color){ 16, 17, 21, 255 });

        DrawTextEx(font, TXT_TITLE, (Vector2){ 40, 24 }, 30, 1, RAYWHITE);

        if (phase == PH_IDLE) {
            DrawTextEx(font, TXT_IDLE, (Vector2){ 40, 90 }, 24, 1, (Color){ 150, 152, 160, 255 });
        }

        if (phase != PH_IDLE) {
            // 倒计时条
            DrawTextEx(font, TXT_INPUT, (Vector2){ 40, 90 }, 20, 1, (Color){ 150, 152, 160, 255 });
            float ratio = (maxTime > 0) ? timer / maxTime : 0.0f;
            if (ratio < 0) ratio = 0;
            Color barColor = (ratio > 0.3f) ? (Color){ 133, 183, 235, 255 } : (Color){ 224, 75, 74, 255 };
            DrawRectangle(40, 120, 600, 14, (Color){ 34, 36, 44, 255 });
            DrawRectangle(40, 120, (int)(600 * ratio), 14, barColor);

            char tbuf[32];
            snprintf(tbuf, sizeof(tbuf), "%.2f", timer);
            DrawTextEx(font, tbuf, (Vector2){ 660, 112 }, 24, 1, barColor);

            if (extended)
                DrawTextEx(font, TXT_EXTEND, (Vector2){ 40, 146 }, 18, 1, (Color){ 250, 199, 117, 255 });

            // 已输入的事件序列
            DrawTextEx(font, TXT_USED, (Vector2){ 40, 186 }, 20, 1, (Color){ 150, 152, 160, 255 });
            Event ev[32];
            int ec = ExtractEvents(ev, 32);
            for (int i = 0; i < ec && i < 10; i++) {
                float cx = 40 + i * 74.0f;
                Color fill, line;
                if (ev[i].type == EV_SHIFT) { fill = (Color){ 12, 68, 124, 255 }; line = (Color){ 133, 183, 235, 255 }; }
                else                        { fill = (Color){ 113, 43, 19, 255 }; line = (Color){ 240, 153, 123, 255 }; }
                DrawRectangleRounded((Rectangle){ cx, 216, 66, 46 }, 0.2f, 4, fill);
                DrawRectangleRoundedLines((Rectangle){ cx, 216, 66, 46 }, 0.2f, 4, line);

                const char *t = (ev[i].type == EV_J) ? "J" : (ev[i].type == EV_JK) ? "JK" : "SH";
                Vector2 sz = MeasureTextEx(font, t, 22, 1);
                DrawTextEx(font, t, (Vector2){ cx + 33 - sz.x / 2, 228 }, 22, 1, RAYWHITE);
            }
        }

        if (phase == PH_RESULT) {
            DrawTextEx(font, TXT_RESULT, (Vector2){ 40, 300 }, 20, 1, (Color){ 150, 152, 160, 255 });
            DrawTextEx(font, resultText, (Vector2){ 40, 328 }, 46, 1, (Color){ 250, 199, 117, 255 });
        }

        DrawTextEx(font, TXT_RULES, (Vector2){ 40, 396 }, 20, 1, (Color){ 150, 152, 160, 255 });
        DrawTextEx(font, TXT_R1, (Vector2){ 40, 424 }, 18, 1, (Color){ 170, 172, 180, 255 });
        DrawTextEx(font, TXT_R2, (Vector2){ 40, 450 }, 18, 1, (Color){ 170, 172, 180, 255 });
        DrawTextEx(font, TXT_R3, (Vector2){ 40, 476 }, 18, 1, (Color){ 170, 172, 180, 255 });
        DrawTextEx(font, TXT_R4, (Vector2){ 40, 502 }, 18, 1, (Color){ 170, 172, 180, 255 });
        DrawTextEx(font, TXT_R5, (Vector2){ 400, 424 }, 18, 1, (Color){ 170, 172, 180, 255 });

        EndDrawing();

        if (shotMode && frame == 195) { TakeScreenshot("command_shot.png"); break; }
    }

    UnloadFont(font);
    CloseWindow();
    return 0;
}
