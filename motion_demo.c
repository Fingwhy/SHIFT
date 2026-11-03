#include "raylib.h"
#include <string.h>
#include <stdio.h>

#define BUF 64          // 环形缓冲区深度（帧）
#define WINDOW 24       // 招式识别窗口（帧），约 0.4 秒

// ---------------------------------------------------------------------
// 界面上所有文字，集中定义
// 加载字体时直接从这份清单提取汉字，漏字的可能性降到零
// ---------------------------------------------------------------------
#define TXT_TITLE   "搓招输入系统"
#define TXT_HINT    "方向键模拟摇杆   J 键出拳"
#define TXT_DIR     "当前方向"
#define TXT_HIST    "最近 20 帧输入历史"
#define TXT_RESULT  "判定结果"
#define TXT_MOVES   "指令表"
#define TXT_HADOU   "波动拳"
#define TXT_SHORYU  "升龙拳"
#define TXT_TATSU   "旋风腿"
#define TXT_PUNCH   "站立拳"
#define TXT_IDLE    "等待输入"
#define TXT_LIST1   "波动拳 2 - 3 - 6 + P"
#define TXT_LIST2   "升龙拳 6 - 2 - 3 + P"
#define TXT_LIST3   "旋风腿 2 - 1 - 4 + P"

static const char *UI_TEXT[] = {
    TXT_TITLE, TXT_HINT, TXT_DIR, TXT_HIST, TXT_RESULT, TXT_MOVES,
    TXT_HADOU, TXT_SHORYU, TXT_TATSU, TXT_PUNCH, TXT_IDLE,
    TXT_LIST1, TXT_LIST2, TXT_LIST3
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
    for (int c = 32; c < 127; c++) out[n++] = c;              // 全部可见 ASCII
    for (int i = 0; i < UI_TEXT_COUNT; i++) {                 // 逐个字符串提取汉字
        n += Utf8ToCodepoints(UI_TEXT[i], out + n, max - n);
    }
    return n;
}

// ---------------------------------------------------------------------
// 输入历史：每一帧压一条记录，两个字段就够
// ---------------------------------------------------------------------
typedef struct {
    unsigned char dir;      // 1-9 小键盘方向，5 = 中立
    unsigned char btn;      // 是否按了攻击键
} InputEvent;

static InputEvent g_buf[BUF];
static int g_head = 0;
static int g_count = 0;

static void PushInput(unsigned char dir, unsigned char btn) {
    g_buf[g_head].dir = dir;
    g_buf[g_head].btn = btn;
    g_head = (g_head + 1) % BUF;
    if (g_count < BUF) g_count++;
}

// 取往回数第 i 条记录（0 = 最新）
static InputEvent GetPast(int i) {
    int idx = (g_head - 1 - i + BUF * 2) % BUF;
    return g_buf[idx];
}

// ---------------------------------------------------------------------
// 核心：倒序扫描，看最近若干帧里能否按顺序凑出 seq
// 这就是「子序列匹配」——允许中间插噪音，只要顺序对
// ---------------------------------------------------------------------
static int MatchMotion(const unsigned char *seq, int len) {
    int k = len - 1;                        // 从指令的最后一个节点开始找
    int limit = (g_count < WINDOW) ? g_count : WINDOW;

    for (int i = 0; i < limit && k >= 0; i++) {
        if (GetPast(i).dir == seq[k]) k--;  // 命中一个节点，往前推一格
    }
    return (k < 0);                         // 全部节点都找到了
}

// ---------------------------------------------------------------------
// 读键盘，合成小键盘方向记号
// 小键盘布局：  7 8 9
//               4 5 6
//               1 2 3
// ---------------------------------------------------------------------
static unsigned char ReadStick(void) {
    int x = 0, y = 0;
    if (IsKeyDown(KEY_RIGHT)) x = 1;
    if (IsKeyDown(KEY_LEFT))  x = -1;
    if (IsKeyDown(KEY_UP))    y = 1;
    if (IsKeyDown(KEY_DOWN))  y = -1;
    return (unsigned char)(5 + x + y * 3);
}

int main(int argc, char **argv) {
    int shotMode = (argc > 1 && strcmp(argv[1], "--shot") == 0);

    InitWindow(900, 520, "SHIFT - Motion Input");
    SetTargetFPS(60);

    int cps[512];
    int n = CollectUIGlyphs(cps, 512);
    Font font = LoadFontEx("assets/font.ttf", 40, cps, n);
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);

    // 三条指令表
    static const unsigned char M_HADOU[]  = { 2, 3, 6 };   // 波动拳：下、下前、前
    static const unsigned char M_SHORYU[] = { 6, 2, 3 };   // 升龙拳：前、下、下前
    static const unsigned char M_TATSU[]  = { 2, 1, 4 };   // 旋风腿：下、下后、后

    const char *moveName = TXT_IDLE;
    unsigned char prevBtn = 0;
    unsigned char curDir = 5;
    int frame = 0;

    while (!WindowShouldClose()) {
        unsigned char dir, btn;

        if (shotMode) {                     // 截图模式：注入一段 236+P，方便自动验证
            dir = (frame < 6) ? 5 : (frame < 10) ? 2 : (frame < 14) ? 3 : (frame < 18) ? 6 : 5;
            btn = (frame == 18) ? 1 : 0;
        } else {                            // 正常模式：读键盘
            dir = ReadStick();
            btn = IsKeyDown(KEY_J) ? 1 : 0;
        }

        PushInput(dir, btn);
        curDir = dir;

        // 按键的上升沿：只在"刚按下"的那一帧判定
        if (btn && !prevBtn) {
            if (MatchMotion(M_SHORYU, 3))      moveName = TXT_SHORYU;
            else if (MatchMotion(M_HADOU, 3))  moveName = TXT_HADOU;
            else if (MatchMotion(M_TATSU, 3))  moveName = TXT_TATSU;
            else                               moveName = TXT_PUNCH;
        }
        prevBtn = btn;

        BeginDrawing();
        ClearBackground((Color){ 16, 17, 21, 255 });

        DrawTextEx(font, TXT_TITLE, (Vector2){ 40, 24 }, 30, 1, RAYWHITE);
        DrawTextEx(font, TXT_HINT, (Vector2){ 40, 64 }, 20, 1, (Color){ 140, 142, 150, 255 });

        // 当前方向，用大号数字显示
        char dirText[8];
        snprintf(dirText, sizeof(dirText), "%d", curDir);
        DrawTextEx(font, TXT_DIR, (Vector2){ 40, 110 }, 20, 1, (Color){ 150, 152, 160, 255 });
        DrawTextEx(font, dirText, (Vector2){ 40, 134 }, 52, 1, (Color){ 133, 183, 235, 255 });

        // 输入历史条
        DrawTextEx(font, TXT_HIST, (Vector2){ 140, 110 }, 20, 1, (Color){ 150, 152, 160, 255 });
        int cells = (g_count < 20) ? g_count : 20;
        for (int i = 0; i < cells; i++) {
            InputEvent e = GetPast(cells - 1 - i);   // 最左边是最旧的
            float cx = 140 + i * 37.0f;
            Color fill = e.btn ? (Color){ 113, 43, 19, 255 } : (Color){ 12, 68, 124, 255 };
            Color line = e.btn ? (Color){ 240, 153, 123, 255 } : (Color){ 133, 183, 235, 255 };
            DrawRectangleRounded((Rectangle){ cx, 140, 32, 40 }, 0.2f, 4, fill);
            DrawRectangleRoundedLines((Rectangle){ cx, 140, 32, 40 }, 0.2f, 4, line);

            char t[4];
            if (e.btn) snprintf(t, sizeof(t), "P");
            else       snprintf(t, sizeof(t), "%d", e.dir);
            Vector2 sz = MeasureTextEx(font, t, 20, 1);
            DrawTextEx(font, t, (Vector2){ cx + 16 - sz.x / 2, 150 }, 20, 1, RAYWHITE);
        }

        // 判定结果
        DrawTextEx(font, TXT_RESULT, (Vector2){ 40, 240 }, 20, 1, (Color){ 150, 152, 160, 255 });
        DrawTextEx(font, moveName, (Vector2){ 40, 268 }, 44, 1, (Color){ 250, 199, 117, 255 });

        // 指令表
        DrawTextEx(font, TXT_MOVES, (Vector2){ 40, 360 }, 20, 1, (Color){ 150, 152, 160, 255 });
        DrawTextEx(font, TXT_LIST1, (Vector2){ 40, 388 }, 20, 1, (Color){ 180, 182, 190, 255 });
        DrawTextEx(font, TXT_LIST2, (Vector2){ 40, 416 }, 20, 1, (Color){ 180, 182, 190, 255 });
        DrawTextEx(font, TXT_LIST3, (Vector2){ 40, 444 }, 20, 1, (Color){ 180, 182, 190, 255 });

        EndDrawing();

        if (shotMode && ++frame == 26) { TakeScreenshot("motion_shot.png"); break; }
    }

    UnloadFont(font);
    CloseWindow();
    return 0;
}
