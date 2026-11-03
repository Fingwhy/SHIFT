// =====================================================================
// SHIFT - 吟唱对决 · 可试玩原型
// 编译：双击 build.bat
//
// 操作：
//   空格    开始吟唱
//   A S D   输入三系元素（火 / 冰 / 雷）
//   Shift   延长吟唱 + 短暂闪避（每场 3 次）
//   Enter   提前释放（用当前已输入的招式）
//
// 规则：
//   敌人先起手，起手键会亮起。你和它同时吟唱。
//   谁先完成谁先出手，先出手的打断对方。
// =====================================================================

#include "raylib.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

void DisableIME(void *windowHandle);   // 实现在 ime_fix.c
void DisableDpiScaling(void);           // 同上

// ---------------------------------------------------------------------
// 界面文字
// ---------------------------------------------------------------------
#define TXT_HINT    "空格开始吟唱   A S D 输入元素   Shift 延长并闪避   Enter 提前释放"
#define TXT_PLAYER  "你"
#define TXT_CASTING "吟唱中"
#define TXT_INPUT   "已输入"
#define TXT_SHIFT   "闪避次数"
#define TXT_WIN     "胜利"
#define TXT_LOSE    "你倒下了"
#define TXT_NEXT    "按 Enter 继续"
#define TXT_BEGIN   "按空格开始"
#define TXT_INTERRUPT "打断！"
#define TXT_FAIL    "吟唱失败"
#define TXT_DODGE   "闪避成功"
#define TXT_LAUNCH  "出手"
#define TXT_SPELLS  "可释放：A 火球 / AA 火柱 / AAA 炎爆 / S 冰锥 / SS 冰墙"
#define TXT_SPELLS2 "SSS 暴雪 / D 电弧 / DD 雷击 / DDD 雷暴 / ASD 三系融合"

// ---------------------------------------------------------------------
// 法术表：指令越长，伤害越高
// ---------------------------------------------------------------------
typedef struct {
    const char *name;
    unsigned char seq[4];   // 0=火 1=冰 2=雷
    int len;
    int damage;
} Spell;

static const Spell SPELLS[] = {
    { "火球",   { 0 },          1,  8 },
    { "火柱",   { 0, 0 },       2, 15 },
    { "炎爆",   { 0, 0, 0 },    3, 27 },
    { "冰锥",   { 1 },          1,  8 },
    { "冰墙",   { 1, 1 },       2, 15 },
    { "暴雪",   { 1, 1, 1 },    3, 27 },
    { "电弧",   { 2 },          1,  8 },
    { "雷击",   { 2, 2 },       2, 15 },
    { "雷暴",   { 2, 2, 2 },    3, 27 },
    { "三系融合", { 0, 1, 2 },  3, 36 },
};
#define SPELL_COUNT ((int)(sizeof(SPELLS) / sizeof(SPELLS[0])))

// ---------------------------------------------------------------------
// 敌人技能：起手键、吟唱时长、伤害
// ---------------------------------------------------------------------
typedef struct {
    const char *keyName;    // 起手键，玩家只能看到这个
    int element;
    float castTime;
    int damage;
} EnemySpell;

typedef struct {
    const char *name;
    int maxHP;
    const EnemySpell *spells;
    int spellCount;
} EnemyDef;

static const EnemySpell S_LESSON[] = {
    { "A", 0, 3.2f, 9 },
};

static const EnemySpell S_FROST[] = {
    { "S", 1, 3.0f, 12 },
    { "D", 2, 3.4f, 15 },
};

static const EnemySpell S_LORD[] = {
    { "A", 0, 2.9f, 14 },
    { "S", 1, 2.9f, 14 },
    { "D", 2, 3.6f, 20 },
};

static const EnemyDef ENEMIES[] = {
    { "见习法师",   40, S_LESSON, 1 },
    { "冰霜卫士",   70, S_FROST,  2 },
    { "元素之主",  110, S_LORD,   3 },
};
#define ENEMY_COUNT ((int)(sizeof(ENEMIES) / sizeof(ENEMIES[0])))

// ---------------------------------------------------------------------
// 字体字符收集
// ---------------------------------------------------------------------
static const char *UI_TEXT[] = {
    TXT_HINT, TXT_PLAYER, TXT_CASTING, TXT_INPUT, TXT_SHIFT,
    TXT_WIN, TXT_LOSE, TXT_NEXT, TXT_BEGIN, TXT_INTERRUPT,
    TXT_FAIL, TXT_DODGE, TXT_LAUNCH, TXT_SPELLS, TXT_SPELLS2
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
    for (int i = 0; i < SPELL_COUNT; i++)
        n += Utf8ToCodepoints(SPELLS[i].name, out + n, max - n);
    for (int i = 0; i < ENEMY_COUNT; i++)
        n += Utf8ToCodepoints(ENEMIES[i].name, out + n, max - n);
    return n;
}

// ---------------------------------------------------------------------
// 匹配：输入序列必须和某个招式的指令完全一致
// ---------------------------------------------------------------------
static int MatchSpell(const unsigned char *buf, int len) {
    if (len == 0) return -1;
    for (int i = 0; i < SPELL_COUNT; i++) {
        if (SPELLS[i].len != len) continue;
        int ok = 1;
        for (int j = 0; j < len; j++)
            if (SPELLS[i].seq[j] != buf[j]) ok = 0;
        if (ok) return i;
    }
    return -1;
}

int main(int argc, char **argv) {
    int shotMode = (argc > 1 && strcmp(argv[1], "--shot") == 0);
    int frame = 0;

    DisableDpiScaling();
    InitWindow(960, 640, "SHIFT");
    DisableIME(GetWindowHandle());
    SetTargetFPS(60);

    int cps[1024];
    int n = CollectGlyphs(cps, 1024);
    Font font = LoadFontEx("assets/font.ttf", 48, cps, n);
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);

    int enemyIdx = 0;
    int enemyHP = ENEMIES[0].maxHP;
    int playerHP = 70;
    const int playerMaxHP = 70;

    int patternIdx = 0;
    int enemyCasting = 0;
    float enemyTimer = 0.0f, enemyMaxTimer = 1.0f;

    int playerCasting = 0;
    float playerTimer = 0.0f, playerMaxTimer = 1.0f;
    unsigned char inputBuf[6];
    int inputLen = 0;
    int shiftUsed = 0;
    float invuln = 0.0f;

    float roundGap = 0.0f;          // 回合间隔
    int started = 0;                // 是否已开始第一回合
    const char *message = TXT_BEGIN;
    Color msgColor = (Color){ 200, 202, 210, 255 };
    int gameState = 0;              // 0=战斗中 1=胜利 2=失败

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (invuln > 0) invuln -= dt;

        // 截图模式：直接摆好一个战斗中的状态，用于验证排版
        frame++;
        if (shotMode) {
            if (frame == 2) {
                started = 1; enemyCasting = 1;
                enemyTimer = 2.2f; enemyMaxTimer = 3.2f;
            }
            if (frame == 6) {
                playerCasting = 1; playerTimer = 1.4f; playerMaxTimer = 2.0f;
                inputBuf[0] = 0; inputBuf[1] = 0; inputLen = 2;
                message = TXT_CASTING;
            }
        }

        // ---------------- 逻辑 ----------------
        if (gameState == 0) {
            if (!started) {
                if (IsKeyPressed(KEY_SPACE)) {
                    started = 1;
                    enemyCasting = 1;
                    enemyMaxTimer = ENEMIES[enemyIdx].spells[patternIdx].castTime;
                    enemyTimer = enemyMaxTimer;
                    message = TXT_CASTING;
                    msgColor = (Color){ 150, 152, 160, 255 };
                }
            }
            else if (roundGap > 0) {
                roundGap -= dt;
                if (roundGap <= 0) {
                    // 开新回合：敌人重新起手
                    const EnemyDef *e = &ENEMIES[enemyIdx];
                    enemyCasting = 1;
                    enemyMaxTimer = e->spells[patternIdx].castTime;
                    enemyTimer = enemyMaxTimer;
                    playerCasting = 0;
                    inputLen = 0;
                    shiftUsed = 0;
                    message = TXT_CASTING;
                    msgColor = (Color){ 150, 152, 160, 255 };
                }
            }
            else {
                const EnemyDef *e = &ENEMIES[enemyIdx];

                // Shift：随时可用。闪避立刻生效，正在吟唱时额外延长窗口
                if (IsKeyPressed(KEY_LEFT_SHIFT) && shiftUsed < 3) {
                    float add = 0.60f * powf(0.70f, (float)shiftUsed);
                    shiftUsed++;
                    invuln = 0.35f;
                    if (playerCasting) {
                        playerTimer += add;
                        playerMaxTimer += add;
                    }
                }

                // --- 玩家输入 ---
                if (!playerCasting && IsKeyPressed(KEY_SPACE)) {
                    playerCasting = 1;
                    playerTimer = 2.0f;
                    playerMaxTimer = 2.0f;
                    inputLen = 0;
                }

                if (playerCasting) {
                    if (IsKeyPressed(KEY_A) && inputLen < 6) inputBuf[inputLen++] = 0;
                    if (IsKeyPressed(KEY_S) && inputLen < 6) inputBuf[inputLen++] = 1;
                    if (IsKeyPressed(KEY_D) && inputLen < 6) inputBuf[inputLen++] = 2;

                    // Enter：提前释放
                    if (IsKeyPressed(KEY_ENTER)) {
                        int id = MatchSpell(inputBuf, inputLen);
                        if (id >= 0) {
                            enemyHP -= SPELLS[id].damage;
                            message = SPELLS[id].name;
                            msgColor = (Color){ 250, 199, 117, 255 };
                            if (enemyCasting) {   // 抢在敌人前面 → 打断
                                enemyCasting = 0;
                                message = TextFormat("%s  %s", SPELLS[id].name, TXT_INTERRUPT);
                            }
                        } else {
                            message = TXT_FAIL;
                            msgColor = (Color){ 224, 75, 74, 255 };
                        }
                        playerCasting = 0;
                        roundGap = 1.0f;
                    }

                    // 吟唱条走完 → 自动释放
                    playerTimer -= dt;
                    if (playerTimer <= 0.0f && playerCasting) {
                        int id = MatchSpell(inputBuf, inputLen);
                        if (id >= 0) {
                            enemyHP -= SPELLS[id].damage;
                            message = TextFormat("%s  %s", SPELLS[id].name, TXT_INTERRUPT);
                            msgColor = (Color){ 250, 199, 117, 255 };
                        } else {
                            message = TXT_FAIL;
                            msgColor = (Color){ 224, 75, 74, 255 };
                        }
                        enemyCasting = 0;
                        playerCasting = 0;
                        roundGap = 1.0f;
                    }
                }

                // --- 敌人推进 ---
                if (enemyCasting) {
                    enemyTimer -= dt;
                    if (enemyTimer <= 0.0f) {
                        enemyCasting = 0;
                        int dmg = e->spells[patternIdx].damage;
                        if (invuln > 0) {
                            message = TXT_DODGE;
                            msgColor = (Color){ 93, 202, 165, 255 };
                        } else {
                            playerHP -= dmg;
                            message = TextFormat("%s %d", TXT_LAUNCH, dmg);
                            msgColor = (Color){ 224, 75, 74, 255 };
                        }
                        patternIdx = (patternIdx + 1) % e->spellCount;
                        playerCasting = 0;
                        roundGap = 1.0f;
                    }
                }
            }

            // --- 胜负判定 ---
            if (enemyHP <= 0) {
                enemyIdx++;
                if (enemyIdx >= ENEMY_COUNT) {
                    gameState = 1;
                    message = TXT_WIN;
                } else {
                    enemyHP = ENEMIES[enemyIdx].maxHP;
                    patternIdx = 0;
                    playerHP = playerMaxHP;
                    started = 0;
                    roundGap = 0;
                    enemyCasting = 0;
                    playerCasting = 0;
                    message = TXT_BEGIN;
                    msgColor = (Color){ 200, 202, 210, 255 };
                }
            }
            if (playerHP <= 0) { playerHP = 0; gameState = 2; message = TXT_LOSE; }
        }
        else {
            if (IsKeyPressed(KEY_ENTER)) {
                enemyIdx = 0;
                enemyHP = ENEMIES[0].maxHP;
                playerHP = playerMaxHP;
                patternIdx = 0;
                started = 0;
                roundGap = 0;
                enemyCasting = 0;
                playerCasting = 0;
                inputLen = 0;
                shiftUsed = 0;
                gameState = 0;
                message = TXT_BEGIN;
                msgColor = (Color){ 200, 202, 210, 255 };
            }
        }

        // ---------------- 绘制 ----------------
        BeginDrawing();
        ClearBackground((Color){ 14, 15, 19, 255 });

        const EnemyDef *en = &ENEMIES[enemyIdx];

        // 敌人名字与血条
        DrawTextEx(font, en->name, (Vector2){ 40, 24 }, 26, 1, (Color){ 220, 130, 120, 255 });
        DrawRectangle(40, 62, 880, 18, (Color){ 34, 36, 44, 255 });
        DrawRectangle(40, 62, (int)(880.0f * enemyHP / en->maxHP), 18, (Color){ 190, 70, 70, 255 });

        // 敌人吟唱条 + 起手键
        if (enemyCasting) {
            const EnemySpell *es = &en->spells[patternIdx];
            DrawTextEx(font, es->keyName, (Vector2){ 40, 100 }, 56, 1, (Color){ 250, 199, 117, 255 });
            float r = enemyTimer / enemyMaxTimer;
            if (r < 0) r = 0;
            DrawRectangle(140, 116, 780, 22, (Color){ 34, 36, 44, 255 });
            DrawRectangle(140, 116, (int)(780 * r), 22, (Color){ 180, 100, 70, 255 });
        } else {
            DrawTextEx(font, "—", (Vector2){ 40, 100 }, 40, 1, (Color){ 60, 62, 70, 255 });
        }

        // 分隔
        DrawRectangle(40, 176, 880, 1, (Color){ 40, 42, 50, 255 });

        // 玩家血条
        DrawTextEx(font, TXT_PLAYER, (Vector2){ 40, 196 }, 22, 1, (Color){ 133, 183, 235, 255 });
        DrawRectangle(40, 230, 880, 18, (Color){ 34, 36, 44, 255 });
        DrawRectangle(40, 230, (int)(880.0f * playerHP / playerMaxHP), 18, (Color){ 70, 150, 110, 255 });

        // 玩家吟唱条
        if (playerCasting) {
            float r = playerTimer / playerMaxTimer;
            if (r < 0) r = 0;
            Color bc = (r > 0.3f) ? (Color){ 133, 183, 235, 255 } : (Color){ 224, 75, 74, 255 };
            DrawRectangle(40, 268, 880, 20, (Color){ 34, 36, 44, 255 });
            DrawRectangle(40, 268, (int)(880 * r), 20, bc);
            if (invuln > 0)
                DrawRectangle(40, 268, (int)(880 * r), 20, (Color){ 93, 202, 165, 120 });
        }

        // 已输入序列
        DrawTextEx(font, TXT_INPUT, (Vector2){ 40, 312 }, 20, 1, (Color){ 140, 142, 150, 255 });
        for (int i = 0; i < inputLen; i++) {
            float cx = 40 + i * 56;
            Color fill = (inputBuf[i] == 0) ? (Color){ 113, 43, 19, 255 }
                       : (inputBuf[i] == 1) ? (Color){ 12, 68, 124, 255 }
                       : (Color){ 133, 96, 12, 255 };
            DrawRectangleRounded((Rectangle){ cx, 340, 48, 48 }, 0.2f, 4, fill);
            const char *t = (inputBuf[i] == 0) ? "A" : (inputBuf[i] == 1) ? "S" : "D";
            DrawTextEx(font, t, (Vector2){ cx + 16, 350 }, 26, 1, RAYWHITE);
        }

        // Shift 次数
        DrawTextEx(font, TXT_SHIFT, (Vector2){ 40, 412 }, 20, 1, (Color){ 140, 142, 150, 255 });
        for (int i = 0; i < 3; i++) {
            Color c = (i < 3 - shiftUsed) ? (Color){ 133, 183, 235, 255 } : (Color){ 45, 47, 55, 255 };
            DrawRectangleRounded((Rectangle){ 40.0f + i * 46, 440, 36, 16 }, 0.4f, 4, c);
        }

        // 提示与信息
        DrawTextEx(font, TXT_HINT, (Vector2){ 40, 486 }, 17, 1, (Color){ 120, 122, 130, 255 });
        DrawTextEx(font, TXT_SPELLS,  (Vector2){ 40, 510 }, 15, 1, (Color){ 96, 98, 106, 255 });
        DrawTextEx(font, TXT_SPELLS2, (Vector2){ 40, 530 }, 15, 1, (Color){ 96, 98, 106, 255 });

        DrawTextEx(font, message, (Vector2){ 40, 556 }, 34, 1, msgColor);

        if (gameState != 0) {
            DrawTextEx(font, TXT_NEXT, (Vector2){ 40, 604 }, 18, 1, (Color){ 140, 142, 150, 255 });
        }

        EndDrawing();

        if (shotMode && frame == 20) { TakeScreenshot("game_shot.png"); break; }
    }

    UnloadFont(font);
    CloseWindow();
    return 0;
}
