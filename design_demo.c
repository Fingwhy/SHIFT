// =====================================================================
// SHIFT · 设计演示（对应 DESIGN.md v2）
//
// 这不是游戏本体，是把设计稿里的机制跑到屏幕上、让你能用手检验的东西。
// 演示的每一项都能在设计文档里找到出处，凡是文档里"待定"的地方，
// 都在界面上标出来，可以用按键切换体感（F1 生命模式 / F2 位移层级）。
//
// 已按设计实现：
//   · 半实时：敌人先起手，你随时插入，双方同时推进，先完成者出手
//   · 提前释放：威力取决于"指令完整度"，不取决于吟唱时长
//   · Shift：延长吟唱（0.60→0.42→0.29）+ 瞬间位移，每场 3 次
//   · 敌人只露起手键、固定模式、超过 2 秒条子闪烁（在憋大的）
//   · 三场短局，死了从第 1 场重来，记录「最远抵达」
//
// 修掉了 shift_game.c 里 4 个与设计不符的问题：
//   1. 两套各自 -= dt 的计时器 → 改为绝对完成时刻比较，结算不再取决于代码行号
//   2. Shift 次数每回合重置 → 改为每场 3 次
//   3. Enter 无条件打断敌人 → 改为统一走时间裁决，慢就是慢
//   4. 敌人出招序号只在吟唱走完时推进 → 起手即推进，不会被锁在第一招
//
// 操作：A S D 输入指令 / 空格起手 / Shift 延长并位移 / Enter 提前释放
//       F1 生命模式 / F2 位移层级 / R 重开一局
//
// 编译：gcc design_demo.c ime_fix.c -o design_demo.exe -I vendor/raylib/include
//       -L vendor/raylib/lib -lraylib -lopengl32 -lgdi32 -lwinmm -limm32 -static-libgcc -lm
// =====================================================================

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

void DisableIME(void *windowHandle);
void DisableDpiScaling(void);

#define SW 960
#define SH 640
#define PI2 6.28318530718f

// ---------------- 设计文档「关键参数」 ----------------
#define PLAYER_WINDOW   2.0f            // 玩家基础吟唱窗口
#define STAGGER         1.0f            // 硬直时间
#define TIE_TOLERANCE   (3.0f / 60.0f)  // 同时按容忍 3 帧，固定，不随任何条件变化
#define CHARGE_HINT     2.0f            // 敌人吟唱超过这个时长，条子闪烁
#define BOLT_SPEED      15.0f           // 法术飞行速度（世界单位/秒）
// 输入节奏：每接受一个指令，下一次至少要等这么久
// 这个数不是拍脑袋——文档里「0.7 秒 A / 1.4 秒 A→S / 2.4 秒 A→S→Shift→A」
// 反推出来就是每个符号 0.5~0.7 秒。没有它，玩家 0.6 秒就能糊出终局法术，
// 「快 vs 强」的取舍立刻消失，敌人永远没机会出手。
#define INPUT_BEAT      0.55f

// ---------------------------------------------------------------------
// 界面文字
// ---------------------------------------------------------------------
#define TXT_TITLE   "SHIFT · 设计演示"
#define TXT_STAGE1  "第"
#define TXT_STAGE2  "场"
#define TXT_OF      " / 3"
#define TXT_BEST    "最远抵达"
#define TXT_CASTE   "敌方吟唱"
#define TXT_CASTP   "你的窗口"
#define TXT_INPUT   "指令"
#define TXT_SHIFT   "位移"
#define TXT_NOW     "此刻释放"
#define TXT_NONE    "尚无招式"
#define TXT_FP      "预计：你先出手"
#define TXT_FE      "预计：敌方先出手"
#define TXT_GAP     "相差"
#define TXT_SEC     "秒"
#define TXT_INT     "打断！"
#define TXT_FIZ     "吟唱失败"
#define TXT_DODGE   "闪避成功"
#define TXT_HIT     "被击中"
#define TXT_CLASH   "对撞"
#define TXT_WAIT    "起手键已亮，你可以选择何时开口"
#define TXT_WIN     "三场全通"
#define TXT_LOSE    "你倒下了"
#define TXT_RETRY   "按 Enter 从第 1 场重来"
#define TXT_HP1     "F1 生命：三段"
#define TXT_HP0     "F1 生命：一击"
#define TXT_SH1     "F2 位移：L1 抽象闪避"
#define TXT_SH2     "F2 位移：L2 三站位"
#define TXT_HINT    "A S D 输入  空格起手  Shift 延长并位移  Enter 提前释放  R 重开"
#define TXT_PASS    "通过"
#define TXT_TIER1   "基础"
#define TXT_TIER2   "复合"
#define TXT_TIER3   "高级"
#define TXT_TIER4   "终局"
#define TXT_YOU     "你"
#define TXT_ENEMY   "敌方"

static const char *UI_TEXT[] = {
    TXT_TITLE, TXT_STAGE1, TXT_STAGE2, TXT_OF, TXT_BEST, TXT_CASTE, TXT_CASTP,
    TXT_INPUT, TXT_SHIFT, TXT_NOW, TXT_NONE, TXT_FP, TXT_FE, TXT_GAP, TXT_SEC,
    TXT_INT, TXT_FIZ, TXT_DODGE, TXT_HIT, TXT_CLASH, TXT_WAIT, TXT_WIN, TXT_LOSE,
    TXT_RETRY, TXT_HP1, TXT_HP0, TXT_SH1, TXT_SH2, TXT_HINT, TXT_PASS,
    TXT_TIER1, TXT_TIER2, TXT_TIER3, TXT_TIER4, TXT_YOU, TXT_ENEMY,
    // 敌人与法术名
    "见习法师", "冰霜卫士", "元素之主",
    "火球", "火柱", "炎爆", "焚天",
    "冰锥", "冰墙", "暴雪", "永冬",
    "电弧", "雷击", "雷暴", "天罚",
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

static int CollectGlyphs(int *out, int max) {
    int n = 0;
    for (int c = 32; c < 127; c++) out[n++] = c;
    for (int i = 0; i < UI_TEXT_COUNT; i++)
        n += Utf8ToCodepoints(UI_TEXT[i], out + n, max - n);
    return n;
}

// ---------------------------------------------------------------------
// 元素与法术
//
// 文档原则：所有法术一开始就能用，门槛是"指令长度"，不是解锁。
// 所以这里不做招式表匹配，只按长度分档 —— 长度本身就是威力。
// ---------------------------------------------------------------------
enum { EL_FIRE = 0, EL_ICE = 1, EL_BOLT = 2, EL_SHIFT = 3 };

static const char *KEY_NAME[4] = { "A", "S", "D", "SH" };
static const Color ELEM_COLOR[4] = {
    { 214,  96,  64, 255 },
    {  86, 150, 214, 255 },
    { 222, 186,  86, 255 },
    { 150, 190, 220, 255 },
};
static const char *SPELL_NAME[3][4] = {
    { "火球", "火柱", "炎爆", "焚天" },
    { "冰锥", "冰墙", "暴雪", "永冬" },
    { "电弧", "雷击", "雷暴", "天罚" },
};
static const char *TIER_NAME[4] = { TXT_TIER1, TXT_TIER2, TXT_TIER3, TXT_TIER4 };
static const int   TIER_DMG[4]  = { 1, 2, 3, 4 };
static const float SHIFT_EXTEND[3] = { 0.60f, 0.42f, 0.29f };   // 逐次递减

// 敌招：起手键 + 吟唱时长 + 伤害。起手即推进，固定循环，可被读
typedef struct { int key; float cast; int dmg; } ESpell;
typedef struct {
    const char *name;
    int hp;
    const ESpell *spells;
    int count;
} EDef;

static const ESpell P_LESSON[] = { { EL_FIRE, 4.00f, 1 }, { EL_FIRE, 4.00f, 1 }, { EL_BOLT, 4.00f, 2 } };
static const ESpell P_FROST[]  = { { EL_ICE, 3.60f, 1 }, { EL_BOLT, 3.60f, 2 }, { EL_ICE, 3.60f, 1 } };
static const ESpell P_LORD[]   = { { EL_FIRE, 3.00f, 2 }, { EL_ICE, 3.00f, 1 }, { EL_BOLT, 3.00f, 3 } };

static const EDef ENEMIES[3] = {
    { "见习法师", 4, P_LESSON, 3 },
    { "冰霜卫士", 6, P_FROST,  3 },
    { "元素之主", 7, P_LORD,   3 },
};

// ---------------------------------------------------------------------
// 程序化生成法师立绘（沿用 duel3d 的思路，补了边缘光和杖顶的光）
// ---------------------------------------------------------------------
static void SlantedBar(Image *img, int x, int y0, int y1, int w, float slope, Color c) {
    for (int y = y0; y < y1; y++)
        ImageDrawRectangle(img, x + (int)(slope * (float)(y - y0)), y, w, 1, c);
}

static Color LerpCol(Color a, Color b, float t) {
    if (t < 0) t = 0; if (t > 1) t = 1;
    return (Color){ (unsigned char)(a.r + (b.r - a.r) * t),
                    (unsigned char)(a.g + (b.g - a.g) * t),
                    (unsigned char)(a.b + (b.b - a.b) * t), 255 };
}

static Texture2D MakeMage(Color robeTop, Color robeBot, Color glow) {
    int w = 128, h = 256;
    Image img = GenImageColor(w, h, BLANK);

    // 法杖：细杆 + 顶端一颗会亮的光
    SlantedBar(&img, 98, 74, 250, 4, 0.02f, (Color){ 44, 22, 30, 255 });
    ImageDrawCircle(&img, 101, 66, 9, Fade(glow, 0.40f));
    ImageDrawCircle(&img, 101, 66, 5, glow);
    ImageDrawCircle(&img, 101, 66, 2, (Color){ 255, 250, 236, 255 });

    // 肩：先铺一层，比袍子亮，读得出"人"的宽度
    for (int y = 84; y < 108; y++) {
        float hw = 26.0f - (float)(y - 84) * 0.15f;
        ImageDrawRectangle(&img, w / 2 - (int)hw, y, (int)hw * 2, 1,
                           LerpCol(robeTop, robeBot, 0.06f));
    }

    // 手臂：从肩斜出去，略暗，压出体积
    SlantedBar(&img, 20, 98, 176, 10, 0.20f, LerpCol(robeTop, robeBot, 0.35f));
    SlantedBar(&img, 98, 98, 176, 10, -0.20f, LerpCol(robeTop, robeBot, 0.35f));

    // 长袍：肩窄、腰收、下摆张开，整体带自上而下的明暗渐变
    for (int y = 96; y < h; y++) {
        float t = (float)(y - 96) / (float)(h - 96);
        float hw = (t < 0.30f) ? (24.0f + t * 16.0f) : (28.8f + (t - 0.30f) * 62.0f);
        Color body = LerpCol(robeTop, robeBot, t * 0.85f);
        ImageDrawRectangle(&img, w / 2 - (int)hw, y, (int)hw * 2, 1, body);
        // 右缘亮边：没有光照也能读出布料轮廓
        ImageDrawRectangle(&img, w / 2 + (int)hw - 2, y, 2, 1, Fade(glow, 0.35f - t * 0.18f));
        // 左缘暗边，进一步压出圆柱感
        ImageDrawRectangle(&img, w / 2 - (int)hw, y, 2, 1, (Color){ 22, 10, 16, 200 });
    }
    // 中线一道褶皱
    for (int y = 100; y < h; y++)
        ImageDrawRectangle(&img, w / 2 + 2, y, 3, 1, Fade(robeBot, 0.45f));
    // 腰带
    ImageDrawRectangle(&img, 40, 148, 48, 7, (Color){ 30, 14, 22, 255 });
    ImageDrawRectangle(&img, 40, 155, 48, 2, Fade(glow, 0.30f));
    // 下摆
    ImageDrawRectangle(&img, 4, h - 5, 120, 4, Fade(glow, 0.35f));

    // 兜帽：先宽后窄，脸是暗的，只有两点眼光
    ImageDrawCircle(&img, w / 2, 54, 31, LerpCol(robeTop, robeBot, 0.10f));
    ImageDrawRectangle(&img, 33, 70, 62, 28, LerpCol(robeTop, robeBot, 0.10f));
    ImageDrawCircle(&img, w / 2, 52, 18, (Color){ 26, 12, 20, 255 });
    ImageDrawCircle(&img, w / 2 - 7, 51, 3, glow);
    ImageDrawCircle(&img, w / 2 + 7, 51, 3, glow);

    Texture2D tex = LoadTextureFromImage(img);
    SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
    UnloadImage(img);
    return tex;
}

// ---------------------------------------------------------------------
// 程序生成一枚发光的环。用贴图而不是 DrawCircle3D：
// 圆环要永远正对相机，billboard 天然满足，DrawCircle3D 靠旋转去凑很容易翻车
// ---------------------------------------------------------------------
static Texture2D MakeRing(int size, float band) {
    Image img = GenImageColor(size, size, BLANK);
    float c = size * 0.5f;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            float dx = (float)x - c, dy = (float)y - c;
            float d = sqrtf(dx * dx + dy * dy) / c;
            float a = 1.0f - fabsf(d - 0.76f) / band;
            if (a < 0) a = 0; if (a > 1) a = 1;
            a *= a;
            if (a > 0.004f)
                ImageDrawPixel(&img, x, y, (Color){ 255, 255, 255, (unsigned char)(a * 255) });
        }
    }
    Texture2D t = LoadTextureFromImage(img);
    SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    UnloadImage(img);
    return t;
}

// ---------------------------------------------------------------------
// 游戏状态
// ---------------------------------------------------------------------
enum { PH_FIGHT = 0, PH_STAGGER, PH_INTERMISSION, PH_VICTORY, PH_DEAD };

typedef struct {
    int active;
    Vector3 from, to;
    float t0, dur;
    int dmg, side, lane;       // side: 0 玩家→敌, 1 敌→玩家
} Bolt;

typedef struct {
    int stage;                 // 0..2
    int enemyHP, playerHP;
    int patternIdx;

    int eActive, eSpell;
    float eStart, eEnd;

    int pActive, nInputs;
    unsigned char inputs[8];
    float pStart, pEnd;
    float inputLock;           // 下一次能接受指令的时刻（节奏）
    unsigned char pending;     // 按早了，缓冲一格
    int hasPending;

    int shiftsLeft, shiftUsed;
    int lane;
    float invulnUntil;

    int phase;
    float phaseEnd;

    int hpMode;                // 1 = 三段生命, 0 = 一击必杀
    int shiftMode;             // 2 = 三站位, 1 = 抽象闪避
    int bestStage;

    float enemyHit;            // 敌人受击抖动
    float shake, flashR, flashC;
    float ghost[3];
    const char *toast;
    Color toastCol;
    float toastT;

    Bolt bolts[8];
} Game;

static int gAuto = 0;   // --auto 试玩模式：把裁决过程打到 stdout

static void Toast(Game *g, const char *s, Color c) {
    g->toast = s; g->toastCol = c; g->toastT = 1.3f;
}

static void StartBattle(Game *g, int stage) {
    g->stage = stage;
    g->enemyHP = ENEMIES[stage].hp;
    g->playerHP = g->hpMode ? 6 : 1;
    g->patternIdx = 0;
    g->eActive = 0;
    g->pActive = 0;
    g->nInputs = 0;
    g->shiftsLeft = 3;
    g->shiftUsed = 0;
    g->lane = 1;
    if (stage + 1 > g->bestStage) g->bestStage = stage + 1;
    for (int i = 0; i < 8; i++) g->bolts[i].active = 0;
}

static void StartRound(Game *g, float now) {
    const EDef *d = &ENEMIES[g->stage];
    g->eSpell = g->patternIdx;
    g->eActive = 1;
    g->eStart = now;
    g->eEnd = now + d->spells[g->eSpell].cast;
    g->patternIdx = (g->patternIdx + 1) % d->count;   // 起手即推进，防止锁死在第一招
    g->pActive = 0;
    g->nInputs = 0;
    g->hasPending = 0;
    g->inputLock = 0;
    g->phase = PH_FIGHT;
}

// 指令入列：受 0.55 秒节奏限制；按早了缓冲一格，等节拍到了自动进
static void AcceptInput(Game *g, int sym, float now) {
    if (now < g->inputLock) {
        if (!g->hasPending) { g->pending = (unsigned char)sym; g->hasPending = 1; }
        return;
    }
    if (g->nInputs < 8) g->inputs[g->nInputs++] = (unsigned char)sym;
    g->inputLock = now + INPUT_BEAT;
}

// 指令 → 招式（按长度分档）
static int ResolveTier(int n) {
    if (n <= 0) return -1;
    if (n == 1) return 0;
    if (n <= 3) return 1;
    if (n <= 5) return 2;
    return 3;
}

static int ResolveElement(const unsigned char *in, int n) {
    for (int i = 0; i < n; i++) if (in[i] != EL_SHIFT) return in[i];
    return -1;
}

// 三个站位（世界坐标）。位移就是在这三条线之间横跳
static const float LANE_X[3] = { -2.55f, 0.0f, 2.55f };
static const float LANE_Z = -2.2f;
static const float LANE_Y = 1.10f;   // 落点印记的悬空高度

static void SpawnBolt(Game *g, int side, int dmg, int lane, Vector3 from, Vector3 to, float now) {
    for (int i = 0; i < 8; i++) {
        if (g->bolts[i].active) continue;
        Bolt *b = &g->bolts[i];
        b->active = 1; b->side = side; b->dmg = dmg; b->lane = lane;
        b->from = from; b->to = to; b->t0 = now;
        b->dur = Vector3Distance(from, to) / BOLT_SPEED;
        return;
    }
}

// ---------------------------------------------------------------------
// 三种结算。全部由"谁的完成时刻更早"驱动，函数本身不判断先后——
// 谁先完成谁就被调用，这是整套系统的唯一裁决口径。
// ---------------------------------------------------------------------
static void EndRound(Game *g, float now) {
    g->phase = PH_STAGGER;
    g->phaseEnd = now + STAGGER;
}

static void PlayerStrike(Game *g, float now) {
    int tr = ResolveTier(g->nInputs);
    int el = ResolveElement(g->inputs, g->nInputs);

    if (tr < 0 || el < 0) {
        // 没有形成招式 → 白放一次，敌人继续吟唱，不算打断
        Toast(g, TXT_FIZ, (Color){ 224, 120, 110, 255 });
        g->pActive = 0;
        return;
    }

    if (gAuto) printf("[%6.2f] 玩家先出手  %s·%s(%d) %s\n", now, TIER_NAME[tr],
                      SPELL_NAME[el][tr], TIER_DMG[tr], g->eActive ? "→ 打断敌方" : "");
    if (g->eActive) {                     // 先完成者生效，对方在这里被打断
        g->eActive = 0;
        Toast(g, TXT_INT, (Color){ 250, 199, 117, 255 });
    } else {
        Toast(g, SPELL_NAME[el][tr], ELEM_COLOR[el]);
    }

    SpawnBolt(g, 0, TIER_DMG[tr], -1, (Vector3){ LANE_X[g->lane], LANE_Y, LANE_Z + 0.4f },
              (Vector3){ 0.0f, 1.75f, -9.0f }, now);
    g->pActive = 0;
    EndRound(g, now);
}

static void EnemyStrike(Game *g, float now) {
    const EDef *d = &ENEMIES[g->stage];
    int dm = d->spells[g->eSpell].dmg;

    if (gAuto) printf("[%6.2f] 敌方先出手  %d 伤 打站位 %d %s\n", now, dm, g->lane,
                      g->pActive ? "→ 打断我方" : "");
    if (g->pActive) {                     // 后完成 → 被打断
        g->pActive = 0;
        Toast(g, TXT_HIT, (Color){ 224, 96, 90, 255 });
    }
    SpawnBolt(g, 1, dm, g->lane, (Vector3){ 0.45f, 1.95f, -9.0f },
              (Vector3){ LANE_X[g->lane], LANE_Y, LANE_Z }, now);
    g->eActive = 0;
    EndRound(g, now);
}

static void Clash(Game *g, float now) {
    // 文档未定：这里选"对撞"，双方各吃一半伤害
    int tr = ResolveTier(g->nInputs);
    int el = ResolveElement(g->inputs, g->nInputs);
    if (tr >= 0 && el >= 0) {
        int dm = TIER_DMG[tr] / 2; if (dm < 1) dm = 1;
        g->enemyHP -= dm;
        g->enemyHit = 0.3f;
    }
    int ed = (ENEMIES[g->stage].spells[g->eSpell].dmg + 1) / 2;
    g->playerHP -= ed;
    g->shake = 0.5f;
    g->flashR = 0.5f;
    Toast(g, TXT_CLASH, (Color){ 250, 199, 117, 255 });
    g->eActive = 0;
    g->pActive = 0;
    EndRound(g, now);
}

// ---------------------------------------------------------------------
int main(int argc, char **argv) {
    int shotMode = (argc > 1 && strcmp(argv[1], "--shot") == 0);
    int autoMode = (argc > 1 && strcmp(argv[1], "--auto") == 0);
    int autoDodge = (autoMode && argc > 2 && strcmp(argv[2], "dodge") == 0);
    gAuto = autoMode;
    int frame = 0;

    if (autoMode) SetConfigFlags(FLAG_MSAA_4X_HINT);
    else          SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    DisableDpiScaling();
    InitWindow(SW, SH, "SHIFT · 设计演示");
    DisableIME(GetWindowHandle());
    SetTargetFPS(60);

    int cps[2048];
    int nc = CollectGlyphs(cps, 2048);
    Font font = LoadFontEx("assets/font.ttf", 64, cps, nc);
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);

    Texture2D mage = MakeMage((Color){ 96, 46, 66, 255 }, (Color){ 40, 18, 30, 255 },
                              (Color){ 250, 190, 110, 255 });
    Texture2D glow = LoadTextureFromImage(GenImageGradientRadial(64, 64, 0.0f, WHITE, BLANK));
    Texture2D ring = MakeRing(128, 0.10f);
    SetTextureFilter(glow, TEXTURE_FILTER_BILINEAR);
    Texture2D vignette = LoadTextureFromImage(
        GenImageGradientRadial(256, 256, 0.52f, BLANK, (Color){ 0, 0, 0, 205 }));

    Game g = { 0 };
    g.hpMode = 1;
    g.shiftMode = 2;
    g.bestStage = 1;
    StartBattle(&g, 0);

    Camera3D cam = { 0 };
    cam.up = (Vector3){ 0, 1, 0 };
    cam.fovy = 50.0f;
    cam.projection = CAMERA_PERSPECTIVE;

    const Vector3 enemyPos = { 0.0f, 1.60f, -9.0f };

    float now = 0.0f;
    float t = 0.0f;
    int autoRound = 0;

    // 开局先起手
    StartRound(&g, now);

    while (!WindowShouldClose()) {
        float dt = autoMode ? (1.0f / 60.0f) : (shotMode ? 0.0f : GetFrameTime());
        if (dt > 0.05f) dt = 0.05f;
        now += dt;
        t += dt;
        frame++;

        // ---------------- 截图模式：摆一个"双方都在吟唱"的状态 ----------------
        if (shotMode && frame == 1) {
            StartRound(&g, now);
            g.eStart = now - 2.25f;  g.eEnd = now + 1.75f;
            g.pActive = 1; g.pStart = now - 1.1f; g.pEnd = now + 0.9f;
            g.inputs[0] = EL_FIRE; g.inputs[1] = EL_ICE; g.inputs[2] = EL_SHIFT; g.nInputs = 3;
            g.shiftsLeft = 2;
            SpawnBolt(&g, 1, 2, 1, (Vector3){ 0.45f, 1.95f, -9.0f }, (Vector3){ LANE_X[1], LANE_Y, LANE_Z }, now - 0.35f);
        }

        // ---------------- 输入 ----------------
        int alive = (g.phase == PH_FIGHT || g.phase == PH_STAGGER);
        if (!shotMode && alive) {
            int shiftPressed = IsKeyPressed(KEY_LEFT_SHIFT) || IsKeyPressed(KEY_RIGHT_SHIFT);

            // Shift：延长 + 位移，两件事同时发生，代价是一次额度
            if (shiftPressed && g.shiftsLeft > 0) {
                g.shiftsLeft--;
                if (g.pActive) {
                    g.pEnd += SHIFT_EXTEND[g.shiftUsed];   // 延长立刻生效
                    g.shiftUsed++;
                    AcceptInput(&g, EL_SHIFT, now);        // 但符号仍按节拍走
                }
                if (g.shiftMode == 2) {
                    g.ghost[g.lane] = 0.32f;
                    g.lane = (g.lane + 1) % 3;
                } else {
                    g.invulnUntil = now + 0.45f;
                }
            }

            // 起手：空格或任一元素键
            if (!g.pActive && g.phase == PH_FIGHT) {
                int startKey = -1;
                if (IsKeyPressed(KEY_SPACE)) startKey = 9;
                if (IsKeyPressed(KEY_A)) startKey = EL_FIRE;
                if (IsKeyPressed(KEY_S)) startKey = EL_ICE;
                if (IsKeyPressed(KEY_D)) startKey = EL_BOLT;
                if (startKey >= 0) {
                    g.pActive = 1;
                    g.pStart = now;
                    g.pEnd = now + PLAYER_WINDOW;
                    g.nInputs = 0;
                    g.hasPending = 0;
                    g.inputLock = now;
                    if (startKey < 3) AcceptInput(&g, startKey, now);
                }
            } else if (g.pActive && g.phase == PH_FIGHT) {
                if (IsKeyPressed(KEY_A)) AcceptInput(&g, EL_FIRE, now);
                if (IsKeyPressed(KEY_S)) AcceptInput(&g, EL_ICE, now);
                if (IsKeyPressed(KEY_D)) AcceptInput(&g, EL_BOLT, now);

                // 提前释放：只是把完成时刻提前到"现在"，之后一切照常交给时间裁决
                if (IsKeyPressed(KEY_ENTER)) g.pEnd = now;
            }
        }
        if (!shotMode) {
            if (IsKeyPressed(KEY_F1)) {
                g.hpMode = !g.hpMode;
                g.playerHP = g.hpMode ? 6 : 1;
                if (g.playerHP > 6) g.playerHP = 6;
            }
            if (IsKeyPressed(KEY_F2)) g.shiftMode = (g.shiftMode == 2) ? 1 : 2;
            if (IsKeyPressed(KEY_R)) {
                StartBattle(&g, 0);
                StartRound(&g, now);
            }
            if ((g.phase == PH_VICTORY || g.phase == PH_DEAD) && IsKeyPressed(KEY_ENTER)) {
                StartBattle(&g, 0);
                StartRound(&g, now);
            }
        }

        // 自动试玩附加：有法术飞来就侧移（验证 L2 位移能不能真的躲掉）
        if (autoDodge && g.phase == PH_STAGGER && g.shiftsLeft > 0) {
            for (int i = 0; i < 8; i++) {
                Bolt *b = &g.bolts[i];
                if (!b->active || b->side != 1) continue;
                if (now - b->t0 > 0.10f && now - b->t0 < b->dur * 0.75f) {
                    g.shiftsLeft--;
                    g.ghost[g.lane] = 0.32f;
                    g.lane = (g.lane + 1) % 3;
                    printf("[%6.2f] 位移到站位 %d\n", now, g.lane);
                    break;
                }
            }
        }

        // ---------------- 自动试玩：三局一轮，第 1/4/7 回合憋大招，其余抢攻 ----------------
        if (autoMode && g.phase == PH_FIGHT) {
            if (!g.pActive && now - g.eStart > 0.30f) {
                g.pActive = 1; g.pStart = now; g.pEnd = now + PLAYER_WINDOW;
                g.nInputs = 0; g.hasPending = 0; g.inputLock = now;
                autoRound++;
                AcceptInput(&g, EL_FIRE, now);
            } else if (g.pActive && now >= g.inputLock) {
                if (autoRound % 3 == 1) {          // 憋招：先用两次 Shift 换时间，再硬堆指令
                    if (g.shiftsLeft > 0 && g.shiftUsed < 2) {
                        g.shiftsLeft--;
                        g.pEnd += SHIFT_EXTEND[g.shiftUsed];
                        g.shiftUsed++;
                        AcceptInput(&g, EL_SHIFT, now);
                    } else if (g.nInputs < 8) {
                        AcceptInput(&g, (int)(g.nInputs % 3), now);
                    }
                } else {                            // 抢攻：四个指令就走
                    if (g.nInputs >= 4) g.pEnd = now;
                    else AcceptInput(&g, (int)(g.nInputs % 3), now);
                }
            }
        }

        // 缓冲的指令到点自动进列
        if (!g.pActive) {
            g.hasPending = 0;
        } else if (g.hasPending && now >= g.inputLock) {
            g.hasPending = 0;
            if (g.nInputs < 8) g.inputs[g.nInputs++] = g.pending;
            g.inputLock = now + INPUT_BEAT;
        }

        // ---------------- 时间裁决 ----------------
        // 核心：不比"谁先减到 0"，只比两个绝对完成时刻。结算顺序不再依赖代码书写顺序。
        if (g.phase == PH_FIGHT) {
            int pr = g.pActive && now >= g.pEnd;
            int er = g.eActive && now >= g.eEnd;

            if (pr && er) {
                float d = g.pEnd - g.eEnd;
                if (fabsf(d) <= TIE_TOLERANCE) Clash(&g, now);
                else if (d < 0)                PlayerStrike(&g, now);
                else                           EnemyStrike(&g, now);
            } else if (pr) {
                PlayerStrike(&g, now);
            } else if (er) {
                EnemyStrike(&g, now);
            }
        }

        // ---------------- 飞行中的法术 ----------------
        for (int i = 0; i < 8; i++) {
            Bolt *b = &g.bolts[i];
            if (!b->active) continue;
            if (now - b->t0 < b->dur) continue;
            b->active = 0;

            if (b->side == 1) {
                int dodged = 0;
                if (g.shiftMode == 2 && g.lane != b->lane) dodged = 1;      // 站位不同 → 打空
                if (g.shiftMode == 1 && now < g.invulnUntil) dodged = 1;
                if (gAuto) printf("[%6.2f] %s  站位 %d / 落点 %d  血 %d\n", now,
                                  dodged ? "闪避成功" : "被命中", g.lane, b->lane, g.playerHP);
                if (dodged) {
                    Toast(&g, TXT_DODGE, (Color){ 110, 210, 190, 255 });
                    g.flashC = 0.35f;
                } else {
                    g.playerHP -= b->dmg;
                    g.shake = 0.55f;
                    g.flashR = 0.6f;
                }
            } else {
                g.enemyHP -= b->dmg;
                g.enemyHit = 0.35f;
                if (gAuto) printf("[%6.2f] 我方命中  敌血 %d\n", now, g.enemyHP);
            }
        }

        // ---------------- 阶段推进 ----------------
        if (g.phase == PH_STAGGER && now >= g.phaseEnd) StartRound(&g, now);
        if (g.phase == PH_INTERMISSION && now >= g.phaseEnd) {
            // 注意：过关时 g.stage 已经 +1 了，这里直接用，别再 +1
            StartBattle(&g, g.stage);
            StartRound(&g, now);
            Toast(&g, ENEMIES[g.stage].name, (Color){ 220, 130, 120, 255 });
        }

        if (g.playerHP <= 0 && g.phase != PH_DEAD && g.phase != PH_VICTORY) {
            g.playerHP = 0;
            g.phase = PH_DEAD;
            Toast(&g, TXT_LOSE, (Color){ 224, 75, 74, 255 });
        } else if (g.enemyHP <= 0 && (g.phase == PH_FIGHT || g.phase == PH_STAGGER)) {
            g.enemyHP = 0;
            g.stage++;
            if (g.stage >= 3) {
                g.phase = PH_VICTORY;
                Toast(&g, TXT_WIN, (Color){ 250, 199, 117, 255 });
            } else {
                g.phase = PH_INTERMISSION;
                g.phaseEnd = now + 1.6f;
                g.eActive = 0; g.pActive = 0;
                for (int i = 0; i < 8; i++) g.bolts[i].active = 0;
            }
        }

        if (autoMode) {
            if (g.phase == PH_VICTORY || g.phase == PH_DEAD || now > 150.0f) break;
            continue;
        }

        // ---------------- 特效计时 ----------------
        if (g.shake > 0) g.shake -= dt * 2.2f;
        if (g.flashR > 0) g.flashR -= dt * 2.6f;
        if (g.flashC > 0) g.flashC -= dt * 3.0f;
        if (g.toastT > 0) g.toastT -= dt;
        if (g.enemyHit > 0) g.enemyHit -= dt;
        for (int i = 0; i < 3; i++) if (g.ghost[i] > 0) g.ghost[i] -= dt;

        // ---------------- 相机 ----------------
        float camX = LANE_X[g.lane] * 0.92f;
        float shx = 0.0f, shy = 0.0f;
        if (g.shake > 0) {
            shx = sinf(t * 91.0f) * g.shake * 0.28f;
            shy = cosf(t * 73.0f) * g.shake * 0.20f;
        }
        cam.position = (Vector3){ camX + shx, 2.42f + sinf(t * 0.9f) * 0.03f + shy, 4.35f };
        cam.target = (Vector3){ camX * 0.42f, 1.62f + shy * 0.5f, -9.0f };

        // ================= 绘制 =================
        BeginDrawing();
        ClearBackground((Color){ 9, 10, 14, 255 });

        BeginMode3D(cam);

        // 地面
        DrawPlane((Vector3){ 0, 0, -3 }, (Vector2){ 40, 40 }, (Color){ 15, 16, 21, 255 });
        for (int i = -14; i <= 14; i++) {
            float fade = powf(1.0f - fabsf((float)i) / 15.0f, 1.5f);
            Color c = Fade((Color){ 44, 50, 66, 255 }, fade * 0.5f);
            DrawLine3D((Vector3){ (float)i, 0.002f, -16 }, (Vector3){ (float)i, 0.002f, 12 }, c);
            DrawLine3D((Vector3){ -14, 0.002f, (float)i }, (Vector3){ 14, 0.002f, (float)i }, c);
        }

        // 两侧柱子：纯粹用来读深度，越远越暗（假雾）
        for (int i = 0; i < 4; i++) {
            float z = -1.5f - i * 3.1f;
            float fade = 1.0f - (float)i * 0.19f;
            Color pc = { (unsigned char)(37 * fade), (unsigned char)(39 * fade),
                         (unsigned char)(50 * fade), 255 };
            DrawCylinder((Vector3){ -6.2f, 0.0f, z }, 0.24f, 0.24f, 5.4f, 8, pc);
            DrawCylinder((Vector3){  6.2f, 0.0f, z }, 0.24f, 0.24f, 5.4f, 8, pc);
        }

        // 落点符印：敌人锁定了你哪个站位，就在那里浮一枚红印。
        // 这是 3D 里唯一"必须存在"的元素 —— 它把"我该往哪躲"直接画在空间里
        rlDisableDepthMask();
        BeginBlendMode(BLEND_ADDITIVE);
        for (int i = 0; i < 8; i++) {
            Bolt *b = &g.bolts[i];
            if (!b->active || b->side != 1) continue;
            float u = (now - b->t0) / b->dur;
            if (u < 0) u = 0; if (u > 1) u = 1;
            float pul = 0.58f + 0.42f * sinf(t * 15.0f);
            Vector3 mp = { LANE_X[b->lane], LANE_Y, LANE_Z };

            // 朝相机的两圈环，越接近命中越收紧
            for (int r = 0; r < 2; r++) {
                float sz = (1.25f - r * 0.28f) * (1.0f - u * 0.28f);
                DrawBillboardPro(cam, ring, (Rectangle){ 0, 0, 128, 128 }, mp,
                                 (Vector3){ 0, 1, 0 }, (Vector2){ sz, sz },
                                 (Vector2){ 0.5f, 0.5f }, 0.0f,
                                 Fade((Color){ 240, 96, 78, 255 }, (0.85f - r * 0.30f) * pul));
            }

            // 空中符印 + 一道竖光
            DrawBillboardPro(cam, glow, (Rectangle){ 0, 0, 64, 64 }, mp, (Vector3){ 0, 1, 0 },
                             (Vector2){ 1.5f, 1.5f }, (Vector2){ 0.5f, 0.5f }, 0.0f,
                             Fade((Color){ 240, 100, 80, 255 }, 0.80f * pul));
            DrawLine3D((Vector3){ mp.x, mp.y - 0.80f, mp.z },
                       (Vector3){ mp.x, mp.y + 0.80f, mp.z },
                       Fade((Color){ 250, 150, 110, 255 }, 0.55f * pul));
        }

        // 位移残影：从旧站位炸开的三圈环，朝相机平面
        for (int i = 0; i < 3; i++) {
            if (g.ghost[i] <= 0) continue;
            float k = g.ghost[i] / 0.32f;
            for (int r = 0; r < 3; r++) {
                float sz = 0.9f + (1.0f - k) * 1.3f + r * 0.22f;
                DrawBillboardPro(cam, ring, (Rectangle){ 0, 0, 128, 128 },
                                 (Vector3){ LANE_X[i], LANE_Y, LANE_Z + 0.3f },
                                 (Vector3){ 0, 1, 0 }, (Vector2){ sz, sz },
                                 (Vector2){ 0.5f, 0.5f }, 0.0f,
                                 Fade((Color){ 133, 183, 235, 255 }, 0.34f * k));
            }
        }
        EndBlendMode();
        rlEnableDepthMask();

        // 敌方吟唱：脚下一圈往里收 + 超过 2 秒开始闪（暗示"在憋大的"）
        if (g.eActive) {
            float el = now - g.eStart;
            float p = el / (g.eEnd - g.eStart);
            if (p < 0) p = 0; if (p > 1) p = 1;
            int charging = (el > CHARGE_HINT);
            float blink = charging ? (0.55f + 0.45f * sinf(t * 12.0f)) : 1.0f;
            Color rc = ELEM_COLOR[ENEMIES[g.stage].spells[g.eSpell].key];
            rlDisableDepthMask();
            BeginBlendMode(BLEND_ADDITIVE);
            float rr = 2.2f - p * 1.4f;
            for (int r = 0; r < 3; r++)
                DrawCircle3D((Vector3){ 0, 0.02f, -9.0f }, rr + r * 0.05f,
                             (Vector3){ 1, 0, 0 }, 90.0f, Fade(rc, 0.22f * blink));
            EndBlendMode();
            rlEnableDepthMask();
        }

        // 敌人
        {
            float bob = sinf(t * 1.2f) * 0.05f;
            float hitk = (g.enemyHit > 0) ? g.enemyHit : 0.0f;
            Vector3 ep = { enemyPos.x + sinf(t * 37.0f) * hitk * 0.12f,
                           enemyPos.y + bob, enemyPos.z };
            DrawCircle3D((Vector3){ 0, 0.015f, -9.0f }, 0.8f, (Vector3){ 1, 0, 0 }, 90.0f,
                         (Color){ 0, 0, 0, 120 });
            Color tint = (g.enemyHit > 0) ? (Color){ 255, 190, 190, 255 } : WHITE;
            DrawBillboardPro(cam, mage, (Rectangle){ 0, 0, (float)mage.width, (float)mage.height },
                             ep, (Vector3){ 0, 1, 0 }, (Vector2){ 1.75f, 3.50f },
                             (Vector2){ 0.5f, 0.5f }, 0.0f, tint);

            // 杖顶的光随吟唱变亮
            if (g.eActive) {
                float p = (now - g.eStart) / (g.eEnd - g.eStart);
                Vector3 dir = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
                Vector3 right = Vector3Normalize(Vector3CrossProduct((Vector3){ 0, 1, 0 }, dir));
                Vector3 tip = Vector3Add(ep, Vector3Add(Vector3Scale(right, 0.46f),
                                                        (Vector3){ 0, 0.80f, 0 }));
                rlDisableDepthMask();
                BeginBlendMode(BLEND_ADDITIVE);
                DrawBillboardPro(cam, glow, (Rectangle){ 0, 0, 64, 64 }, tip,
                                 (Vector3){ 0, 1, 0 }, (Vector2){ 0.5f + p * 1.6f, 0.5f + p * 1.6f },
                                 (Vector2){ 0.5f, 0.5f }, 0.0f,
                                 Fade(ELEM_COLOR[ENEMIES[g.stage].spells[g.eSpell].key], 0.35f + p * 0.4f));
                EndBlendMode();
                rlEnableDepthMask();
            }
        }

        // 玩家吟唱：站位上浮一枚蓝色印记，越接近完成越亮、越大。
        // 它和敌方红色落点印共用同一个空间位置——他瞄你的时候，两个印会重叠
        if (g.pActive) {
            float p = (now - g.pStart) / (g.pEnd - g.pStart);
            if (p < 0) p = 0; if (p > 1) p = 1;
            Vector3 sp = { LANE_X[g.lane], LANE_Y, LANE_Z };
            rlDisableDepthMask();
            BeginBlendMode(BLEND_ADDITIVE);
            for (int r = 0; r < 2; r++)
                DrawBillboardPro(cam, ring, (Rectangle){ 0, 0, 128, 128 }, sp,
                                 (Vector3){ 0, 1, 0 },
                                 (Vector2){ 1.05f - p * 0.42f - r * 0.24f, 1.05f - p * 0.42f - r * 0.24f },
                                 (Vector2){ 0.5f, 0.5f }, 0.0f,
                                 Fade((Color){ 133, 183, 235, 255 }, 0.45f - r * 0.15f));
            DrawBillboardPro(cam, glow, (Rectangle){ 0, 0, 64, 64 }, sp, (Vector3){ 0, 1, 0 },
                             (Vector2){ 0.9f + p * 1.5f, 0.9f + p * 1.5f },
                             (Vector2){ 0.5f, 0.5f }, 0.0f,
                             Fade((Color){ 133, 183, 235, 255 }, 0.16f + p * 0.26f));
            EndBlendMode();
            rlEnableDepthMask();
        }

        // 飞行中的法术
        rlDisableDepthMask();
        BeginBlendMode(BLEND_ADDITIVE);
        for (int i = 0; i < 8; i++) {
            Bolt *b = &g.bolts[i];
            if (!b->active) continue;
            float u = (now - b->t0) / b->dur;
            if (u < 0) u = 0; if (u > 1) u = 1;
            Color c = (b->side == 1) ? (Color){ 240, 120, 80, 255 } : (Color){ 150, 200, 240, 255 };
            for (int k = 0; k < 8; k++) {
                float uk = u - (float)k * 0.045f;
                if (uk < 0) uk = 0;
                Vector3 p = Vector3Lerp(b->from, b->to, uk);
                float s = (0.75f - k * 0.075f);
                DrawBillboardPro(cam, glow, (Rectangle){ 0, 0, 64, 64 }, p, (Vector3){ 0, 1, 0 },
                                 (Vector2){ s, s }, (Vector2){ 0.5f, 0.5f }, 0.0f,
                                 Fade(c, 0.35f * (1.0f - (float)k / 8.0f)));
            }
            Vector3 p = Vector3Lerp(b->from, b->to, u);
            DrawSphereEx(p, 0.11f, 8, 12, Fade(c, 0.9f));
            DrawBillboardPro(cam, glow, (Rectangle){ 0, 0, 64, 64 }, p, (Vector3){ 0, 1, 0 },
                             (Vector2){ 1.5f, 1.5f }, (Vector2){ 0.5f, 0.5f }, 0.0f, Fade(c, 0.8f));
        }
        EndBlendMode();
        rlEnableDepthMask();

        EndMode3D();

        // ================= 屏幕后期 =================
        DrawTexturePro(vignette, (Rectangle){ 0, 0, 256, 256 },
                       (Rectangle){ 0, 0, SW, SH }, (Vector2){ 0, 0 }, 0.0f, WHITE);
        if (g.flashR > 0)
            DrawRectangle(0, 0, SW, SH, (Color){ 180, 40, 40, (unsigned char)(g.flashR * 90) });
        if (g.flashC > 0)
            DrawRectangle(0, 0, SW, SH, (Color){ 80, 200, 200, (unsigned char)(g.flashC * 55) });

        // ================= HUD =================
        const EDef *ed = &ENEMIES[g.stage];

        // 顶部：敌人
        DrawTextEx(font, ed->name, (Vector2){ 40, 18 }, 26, 1, (Color){ 224, 140, 128, 255 });

        char sbuf[64];
        snprintf(sbuf, sizeof(sbuf), "%s %d %s%s", TXT_STAGE1, g.stage + 1, TXT_STAGE2, TXT_OF);
        DrawTextEx(font, sbuf, (Vector2){ 40, 52 }, 15, 1, (Color){ 110, 114, 128, 255 });

        snprintf(sbuf, sizeof(sbuf), "%s %d", TXT_BEST, g.bestStage);
        DrawTextEx(font, sbuf, (Vector2){ 210, 52 }, 15, 1, (Color){ 110, 114, 128, 255 });

        // 敌人血格
        for (int i = 0; i < ed->hp; i++) {
            Color c = (i < g.enemyHP) ? (Color){ 190, 70, 70, 255 } : (Color){ 40, 42, 50, 255 };
            DrawRectangle(360 + i * 22, 54, 16, 12, c);
        }


        // 敌方吟唱条 + 起手键
        if (g.eActive) {
            int key = ed->spells[g.eSpell].key;
            float el = now - g.eStart, tot = g.eEnd - g.eStart;
            float p = el / tot; if (p < 0) p = 0; if (p > 1) p = 1;
            int charging = (el > CHARGE_HINT);
            float blink = charging ? (0.55f + 0.45f * sinf(t * 12.0f)) : 1.0f;

            DrawRectangleRounded((Rectangle){ 40, 82, 46, 46 }, 0.22f, 6,
                                 Fade(ELEM_COLOR[key], 0.26f));
            DrawRectangleRoundedLines((Rectangle){ 40, 82, 46, 46 }, 0.22f, 6,
                                      Fade(ELEM_COLOR[key], 0.75f));
            Vector2 ks = MeasureTextEx(font, KEY_NAME[key], 30, 1);
            DrawTextEx(font, KEY_NAME[key], (Vector2){ 63 - ks.x / 2, 89 }, 30, 1, ELEM_COLOR[key]);

            DrawRectangle(98, 98, 448, 12, (Color){ 32, 34, 42, 255 });
            DrawRectangle(98, 98, (int)(448 * p), 12, Fade(ELEM_COLOR[key], blink));
            // 超过 2 秒的那一段画一道刻线：过了这里就是"在憋大的"
            DrawRectangle(98 + (int)(448 * (CHARGE_HINT / tot)), 94, 2, 20,
                          (Color){ 240, 200, 140, 170 });

            snprintf(sbuf, sizeof(sbuf), "%s %.1f%s", TXT_CASTE, g.eEnd - now, TXT_SEC);
            DrawTextEx(font, sbuf, (Vector2){ 98, 115 }, 14, 1, (Color){ 120, 124, 136, 255 });
        } else {
            DrawTextEx(font, TXT_WAIT, (Vector2){ 98, 100 }, 14, 1, (Color){ 84, 88, 100, 255 });
        }

        // 预计先后：把"谁先完成"直接写成一句话
        if (g.eActive && g.pActive) {
            float d = g.pEnd - g.eEnd;
            const char *who = (d < 0) ? TXT_FP : TXT_FE;
            Color c = (d < 0) ? (Color){ 133, 183, 235, 255 } : (Color){ 224, 110, 100, 255 };
            snprintf(sbuf, sizeof(sbuf), "%s · %s %.1f%s", who, TXT_GAP, fabsf(d), TXT_SEC);
            DrawTextEx(font, sbuf, (Vector2){ 600, 96 }, 17, 1, c);
        }

        // 底部：玩家
        if (g.pActive) {
            float p = (now - g.pStart) / (g.pEnd - g.pStart);
            if (p < 0) p = 0; if (p > 1) p = 1;
            Color bc = (p > 0.72f) ? (Color){ 224, 110, 90, 255 } : (Color){ 133, 183, 235, 255 };
            DrawRectangle(40, 470, 460, 12, (Color){ 32, 34, 42, 255 });
            DrawRectangle(40, 470, (int)(460 * p), 12, bc);
            snprintf(sbuf, sizeof(sbuf), "%s %.1f%s", TXT_CASTP, g.pEnd - now, TXT_SEC);
            DrawTextEx(font, sbuf, (Vector2){ 40, 486 }, 14, 1, (Color){ 120, 124, 136, 255 });
        } else {
            DrawRectangle(40, 470, 460, 12, (Color){ 26, 28, 34, 255 });
            DrawTextEx(font, TXT_WAIT, (Vector2){ 40, 486 }, 14, 1, (Color){ 84, 88, 100, 255 });
        }

        // 已输入指令
        DrawTextEx(font, TXT_INPUT, (Vector2){ 520, 466 }, 14, 1, (Color){ 110, 114, 128, 255 });
        for (int i = 0; i < g.nInputs && i < 8; i++) {
            int k = g.inputs[i];
            Color c = ELEM_COLOR[k];
            DrawRectangleRounded((Rectangle){ 520.0f + i * 34, 486, 28, 28 }, 0.25f, 4, Fade(c, 0.22f));
            DrawRectangleRoundedLines((Rectangle){ 520.0f + i * 34, 486, 28, 28 }, 0.25f, 4, Fade(c, 0.9f));
            Vector2 ks = MeasureTextEx(font, KEY_NAME[k], 15, 1);
            DrawTextEx(font, KEY_NAME[k], (Vector2){ 534.0f + i * 34 - ks.x / 2, 493 }, 15, 1, c);
        }

        // 此刻释放会得到什么 —— 这是"快 vs 强"的全部可见性
        {
            int tr = ResolveTier(g.nInputs);
            int el = ResolveElement(g.inputs, g.nInputs);
            DrawTextEx(font, TXT_NOW, (Vector2){ 40, 514 }, 14, 1, (Color){ 110, 114, 128, 255 });
            if (tr < 0 || el < 0) {
                DrawTextEx(font, TXT_NONE, (Vector2){ 118, 510 }, 18, 1, (Color){ 86, 90, 102, 255 });
            } else {
                snprintf(sbuf, sizeof(sbuf), "%s · %s · %d", SPELL_NAME[el][tr], TIER_NAME[tr],
                         TIER_DMG[tr]);
                DrawTextEx(font, sbuf, (Vector2){ 118, 510 }, 18, 1, ELEM_COLOR[el]);
            }
        }

        // 位移额度 / 模式
        DrawTextEx(font, TXT_SHIFT, (Vector2){ 40, 546 }, 14, 1, (Color){ 110, 114, 128, 255 });
        for (int i = 0; i < 3; i++) {
            Color c = (i < g.shiftsLeft) ? (Color){ 133, 183, 235, 255 } : (Color){ 42, 44, 52, 255 };
            DrawRectangleRounded((Rectangle){ 88.0f + i * 24, 548, 18, 10 }, 0.4f, 4, c);
        }
        DrawTextEx(font, g.hpMode ? TXT_HP1 : TXT_HP0, (Vector2){ 180, 544 }, 13, 1,
                   (Color){ 96, 100, 112, 255 });
        DrawTextEx(font, g.shiftMode == 2 ? TXT_SH2 : TXT_SH1, (Vector2){ 330, 544 }, 13, 1,
                   (Color){ 96, 100, 112, 255 });

        // 玩家血格
        int pmax = g.hpMode ? 6 : 1;
        for (int i = 0; i < pmax; i++) {
            Color c = (i < g.playerHP) ? (Color){ 110, 200, 165, 255 } : (Color){ 40, 42, 50, 255 };
            DrawRectangle(700 + i * 20, 545, 15, 11, c);
        }
        DrawTextEx(font, TXT_YOU, (Vector2){ 700, 520 }, 14, 1, (Color){ 130, 180, 160, 255 });

        // 站位指示（屏幕右下，和 3D 里的三条线对应）
        for (int i = 0; i < 3; i++) {
            int cur = (i == g.lane);
            DrawRectangleRounded((Rectangle){ 840.0f + i * 30, 545, 22, 11 }, 0.4f, 4,
                                 cur ? (Color){ 133, 183, 235, 255 } : (Color){ 44, 46, 54, 255 });
        }

        // 结算提示
        if (g.toastT > 0) {
            float a = (g.toastT > 1.0f) ? 1.0f : g.toastT;
            Vector2 ts = MeasureTextEx(font, g.toast, 34, 1);
            DrawTextEx(font, g.toast, (Vector2){ 480 - ts.x / 2, 300 }, 34, 1, Fade(g.toastCol, a));
        }

        if (g.phase == PH_VICTORY || g.phase == PH_DEAD) {
            DrawRectangle(0, 0, SW, SH, (Color){ 6, 7, 10, 190 });
            Vector2 s1 = MeasureTextEx(font, g.phase == PH_VICTORY ? TXT_WIN : TXT_LOSE, 46, 2);
            DrawTextEx(font, g.phase == PH_VICTORY ? TXT_WIN : TXT_LOSE,
                       (Vector2){ 480 - s1.x / 2, 260 }, 46, 2,
                       g.phase == PH_VICTORY ? (Color){ 250, 199, 117, 255 }
                                             : (Color){ 224, 90, 84, 255 });
            snprintf(sbuf, sizeof(sbuf), "%s %d %s", TXT_BEST, g.bestStage, TXT_STAGE2);
            Vector2 s2 = MeasureTextEx(font, sbuf, 20, 1);
            DrawTextEx(font, sbuf, (Vector2){ 480 - s2.x / 2, 322 }, 20, 1,
                       (Color){ 160, 164, 176, 255 });
            Vector2 s3 = MeasureTextEx(font, TXT_RETRY, 18, 1);
            DrawTextEx(font, TXT_RETRY, (Vector2){ 480 - s3.x / 2, 356 }, 18, 1,
                       (Color){ 110, 114, 128, 255 });
        }

        DrawTextEx(font, TXT_HINT, (Vector2){ 40, 596 }, 13, 1, (Color){ 84, 88, 100, 255 });
        DrawTextEx(font, TXT_TITLE, (Vector2){ 740, 596 }, 13, 1, (Color){ 62, 66, 78, 255 });

        EndDrawing();

        if (shotMode && frame == 4) { TakeScreenshot("design_shot.png"); break; }
    }

    if (autoMode)
        printf("\n[结束] 阶段 %d  最远 %d  我方血 %d  敌方血 %d\n", g.phase, g.bestStage,
               g.playerHP, g.enemyHP);

    UnloadTexture(vignette);
    UnloadTexture(ring);
    UnloadTexture(glow);
    UnloadTexture(mage);
    UnloadFont(font);
    CloseWindow();
    return 0;
}
