#include "raylib.h"
#include <string.h>
#include <stdio.h>

// ---------------------------------------------------------------------
// 界面上所有文字，集中定义，字体字符集从这里自动提取
// ---------------------------------------------------------------------
#define TXT_TITLE   "组合键系统"
#define TXT_HINT    "J 单击    Shift 修饰    J + K 同时"
#define TXT_HIST    "动作历史"
#define TXT_NOW     "当前触发"
#define TXT_RULES   "规则"
#define TXT_R1      "J  普通攻击"
#define TXT_R2      "J → Shift → J  重攻击"
#define TXT_R3      "J + K  旋转攻击"
#define TXT_R4      "J + K → Shift → J + K  旋转重攻击"
#define TXT_LIGHT       "普通攻击"
#define TXT_HEAVY       "重攻击"
#define TXT_SPIN        "旋转攻击"
#define TXT_SPIN_HEAVY  "旋转重攻击"
#define TXT_IDLE        "等待输入"

static const char *UI_TEXT[] = {
    TXT_TITLE, TXT_HINT, TXT_HIST, TXT_NOW, TXT_RULES,
    TXT_R1, TXT_R2, TXT_R3, TXT_R4,
    TXT_LIGHT, TXT_HEAVY, TXT_SPIN, TXT_SPIN_HEAVY, TXT_IDLE
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
    for (int i = 0; i < UI_TEXT_COUNT; i++) {
        n += Utf8ToCodepoints(UI_TEXT[i], out + n, max - n);
    }
    return n;
}

// ---------------------------------------------------------------------
// 动作类型
// ---------------------------------------------------------------------
enum {
    ACT_J = 1,      // 单击 J
    ACT_JK,         // J + K 同时
    ACT_SHIFT       // 修饰键
};

typedef struct {
    int type;
    int frame;
} Action;

#define ACT_BUF 16
#define COMBO_WINDOW 60     // 连招记忆窗口（帧），1 秒
#define SIMUL_GAP 3         // 同时按的容忍帧数

static Action g_actions[ACT_BUF];
static int g_head = 0;
static int g_count = 0;

static void PushAction(int type, int frame) {
    g_actions[g_head].type = type;
    g_actions[g_head].frame = frame;
    g_head = (g_head + 1) % ACT_BUF;
    if (g_count < ACT_BUF) g_count++;
}

static Action GetPastAction(int i) {
    int idx = (g_head - 1 - i + ACT_BUF * 2) % ACT_BUF;
    return g_actions[idx];
}

// ---------------------------------------------------------------------
// 判断：最近是否有一次 Shift，且它之前紧挨着同样的基础动作
// 有 → 这次攻击是"强化版"
// ---------------------------------------------------------------------
static int HasShiftBefore(int base, int now) {
    for (int i = 0; i < g_count; i++) {
        Action a = GetPastAction(i);
        if (now - a.frame > COMBO_WINDOW) return 0;   // 太久远，记忆失效

        if (a.type == ACT_SHIFT) {
            for (int j = i + 1; j < g_count; j++) {   // 继续往回找基础动作
                Action b = GetPastAction(j);
                if (b.type == base) return 1;         // 找到了，强化成立
                if (b.type == ACT_SHIFT) break;       // 中间又隔了一个 Shift，打断
            }
            return 0;
        }
    }
    return 0;
}

static const char *g_lastMove = TXT_IDLE;
static int g_flash = 0;

// 真正出招：判定是普通版还是强化版
static void FireAttack(int base, int frame) {
    int charged = HasShiftBefore(base, frame);
    PushAction(base, frame);

    if (base == ACT_J) g_lastMove = charged ? TXT_HEAVY : TXT_LIGHT;
    else               g_lastMove = charged ? TXT_SPIN_HEAVY : TXT_SPIN;

    g_flash = 26;
}

int main(int argc, char **argv) {
    int shotMode = (argc > 1 && strcmp(argv[1], "--shot") == 0);

    InitWindow(900, 540, "SHIFT - Combo Keys");
    SetTargetFPS(60);

    int cps[512];
    int n = CollectUIGlyphs(cps, 512);
    Font font = LoadFontEx("assets/font.ttf", 40, cps, n);
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);

    int frame = 0;
    int pendFrame = -1;      // 等待合并的按键起始帧
    int pendKey = 0;         // 1 = J，2 = K

    while (!WindowShouldClose()) {
        // ---------- 输入 ----------
        if (shotMode) {
            // 截图模式：脚本注入 J → Shift → J
            if (frame == 5)  FireAttack(ACT_J, frame);
            if (frame == 12) PushAction(ACT_SHIFT, frame);
            if (frame == 20) FireAttack(ACT_J, frame);
        } else {
            // 修饰键：Shift 一按下就记录
            if (IsKeyPressed(KEY_LEFT_SHIFT) || IsKeyPressed(KEY_RIGHT_SHIFT)) {
                PushAction(ACT_SHIFT, frame);
            }

            // 攻击键：先挂起，等 SIMUL_GAP 帧，看要不要合并成 J+K
            if (pendFrame < 0) {
                if (IsKeyPressed(KEY_J)) { pendFrame = frame; pendKey = 1; }
                else if (IsKeyPressed(KEY_K)) { pendFrame = frame; pendKey = 2; }
            } else {
                int elapsed = frame - pendFrame;
                int otherPressed = (pendKey == 1) ? IsKeyPressed(KEY_K) : IsKeyPressed(KEY_J);

                if (otherPressed) {                              // 两个键都按到了 → 组合键
                    FireAttack(ACT_JK, frame);
                    pendFrame = -1;
                } else if (elapsed >= SIMUL_GAP) {               // 等了 3 帧没等到 → 单击
                    if (pendKey == 1) FireAttack(ACT_J, pendFrame);
                    pendFrame = -1;
                }
            }
        }

        if (g_flash > 0) g_flash--;

        // ---------- 绘制 ----------
        BeginDrawing();
        ClearBackground((Color){ 16, 17, 21, 255 });

        DrawTextEx(font, TXT_TITLE, (Vector2){ 40, 24 }, 30, 1, RAYWHITE);
        DrawTextEx(font, TXT_HINT, (Vector2){ 40, 64 }, 20, 1, (Color){ 140, 142, 150, 255 });

        // 动作历史
        DrawTextEx(font, TXT_HIST, (Vector2){ 40, 116 }, 20, 1, (Color){ 150, 152, 160, 255 });
        int cells = (g_count < 8) ? g_count : 8;
        for (int i = 0; i < cells; i++) {
            Action a = GetPastAction(cells - 1 - i);   // 最左是最旧
            float cx = 40 + i * 72.0f;
            Color fill, line;
            if (a.type == ACT_SHIFT)     { fill = (Color){ 12, 68, 124, 255 }; line = (Color){ 133, 183, 235, 255 }; }
            else                         { fill = (Color){ 113, 43, 19, 255 }; line = (Color){ 240, 153, 123, 255 }; }

            DrawRectangleRounded((Rectangle){ cx, 146, 64, 44 }, 0.2f, 4, fill);
            DrawRectangleRoundedLines((Rectangle){ cx, 146, 64, 44 }, 0.2f, 4, line);

            const char *t = (a.type == ACT_J) ? "J" : (a.type == ACT_JK) ? "JK" : "SH";
            Vector2 sz = MeasureTextEx(font, t, 22, 1);
            DrawTextEx(font, t, (Vector2){ cx + 32 - sz.x / 2, 157 }, 22, 1, RAYWHITE);
        }

        // 当前触发
        DrawTextEx(font, TXT_NOW, (Vector2){ 40, 230 }, 20, 1, (Color){ 150, 152, 160, 255 });
        Color moveColor = g_flash > 0 ? (Color){ 250, 199, 117, 255 } : (Color){ 120, 122, 130, 255 };
        DrawTextEx(font, g_lastMove, (Vector2){ 40, 258 }, 46, 1, moveColor);

        // 规则表
        DrawTextEx(font, TXT_RULES, (Vector2){ 40, 350 }, 20, 1, (Color){ 150, 152, 160, 255 });
        DrawTextEx(font, TXT_R1, (Vector2){ 40, 380 }, 20, 1, (Color){ 180, 182, 190, 255 });
        DrawTextEx(font, TXT_R2, (Vector2){ 40, 410 }, 20, 1, (Color){ 180, 182, 190, 255 });
        DrawTextEx(font, TXT_R3, (Vector2){ 40, 440 }, 20, 1, (Color){ 180, 182, 190, 255 });
        DrawTextEx(font, TXT_R4, (Vector2){ 40, 470 }, 20, 1, (Color){ 180, 182, 190, 255 });

        EndDrawing();

        frame++;
        if (shotMode && frame == 26) { TakeScreenshot("combo_shot.png"); break; }
    }

    UnloadFont(font);
    CloseWindow();
    return 0;
}
