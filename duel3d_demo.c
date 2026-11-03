// =====================================================================
// SHIFT - 2.5D 对决场景验证
//
// 目的：验证「3D 空间 + 2D 立绘 + 屏幕空间 HUD」这条路线
//   - 世界是真的 3D（透视相机、地面、柱子、法术按 z 轴飞来）
//   - 角色是 2D 图，用 billboard 贴成永远面向相机的纸片
//   - 所有需要被读懂的信息（条子、按键）画在屏幕空间，不跟着相机转
//
// 编译：gcc duel3d_demo.c ime_fix.c -o duel3d_demo.exe -I vendor/raylib/include
//       -L vendor/raylib/lib -lraylib -lopengl32 -lgdi32 -lwinmm -limm32 -static-libgcc -lm
// =====================================================================

#include "raylib.h"
#include "raymath.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

void DisableIME(void *windowHandle);
void DisableDpiScaling(void);

// ---------------------------------------------------------------------
// 界面文字
// ---------------------------------------------------------------------
#define TXT_ENEMY   "元素之主"
#define TXT_CAST    "敌方吟唱"
#define TXT_INPUT   "已输入"
#define TXT_SHIFT   "闪避"
#define TXT_HINT    "空格开始   A S D 输入   Shift 延长并闪避   Enter 提前释放"
#define TXT_KEY     "A"
#define TXT_TIP     "世界是 3D，人是 2D 纸片，条子在屏幕上"

static const char *UI_TEXT[] = {
    TXT_ENEMY, TXT_CAST, TXT_INPUT, TXT_SHIFT, TXT_HINT, TXT_KEY, TXT_TIP
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
// 用代码画一张「法师立绘」：不需要美术，也能看出是个站着的人
// ---------------------------------------------------------------------
static void SlantedBar(Image *img, int x, int y0, int y1, int w, float slope, Color c) {
    for (int y = y0; y < y1; y++)
        ImageDrawRectangle(img, x + (int)(slope * (float)(y - y0)), y, w, 1, c);
}

static Texture2D MakeMage(Color robe, Color glow, Color dark) {
    int w = 128, h = 256;
    Image img = GenImageColor(w, h, BLANK);

    // 法杖：细杆 + 顶端的光
    SlantedBar(&img, 96, 70, 250, 4, 0.02f, (Color){ 56, 26, 38, 255 });
    ImageDrawCircle(&img, 99, 62, 9, glow);
    ImageDrawCircle(&img, 99, 62, 4, (Color){ 255, 240, 210, 255 });

    // 左右手臂：从肩往下斜出去
    SlantedBar(&img, 24, 96, 168, 11, 0.22f, (Color){ 56, 26, 38, 255 });
    SlantedBar(&img, 96, 96, 168, 11, -0.22f, (Color){ 56, 26, 38, 255 });

    // 袍子：肩窄、腰收、下摆张开
    for (int y = 86; y < h; y++) {
        float t = (float)(y - 86) / (float)(h - 86);
        float hw = (t < 0.45f) ? (24.0f + t * 14.0f) : (30.2f + (t - 0.45f) * 58.0f);
        ImageDrawRectangle(&img, w / 2 - (int)hw, y, (int)hw * 2, 1, robe);
    }
    // 下摆一道稍亮的边，读得出布料轮廓
    ImageDrawRectangle(&img, 8, h - 6, 112, 4, Fade(glow, 0.35f));

    // 兜帽：盖住肩，脸是暗的，只有两点眼光
    ImageDrawCircle(&img, w / 2, 54, 30, robe);
    ImageDrawRectangle(&img, 30, 74, 68, 22, robe);
    ImageDrawCircle(&img, w / 2, 52, 17, dark);
    ImageDrawCircle(&img, w / 2 - 7, 51, 3, glow);
    ImageDrawCircle(&img, w / 2 + 7, 51, 3, glow);

    Texture2D tex = LoadTextureFromImage(img);
    SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
    UnloadImage(img);
    return tex;
}

int main(int argc, char **argv) {
    int shotMode = (argc > 1 && strcmp(argv[1], "--shot") == 0);
    int frame = 0;

    DisableDpiScaling();
    InitWindow(960, 640, "SHIFT - 2.5D");
    DisableIME(GetWindowHandle());
    SetTargetFPS(60);

    int cps[1024];
    int nc = CollectGlyphs(cps, 1024);
    Font font = LoadFontEx("assets/font.ttf", 48, cps, nc);
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);

    Texture2D mage = MakeMage((Color){ 74, 34, 48, 255 }, (Color){ 250, 199, 117, 255 },
                              (Color){ 40, 20, 30, 255 });
    Texture2D glowTex = LoadTextureFromImage(
        GenImageGradientRadial(64, 64, 0.0f, WHITE, BLANK));
    SetTextureFilter(glowTex, TEXTURE_FILTER_BILINEAR);

    // 屏幕暗角：把注意力压到中间，也让 2D 立绘的边不那么"贴"
    Texture2D vignette = LoadTextureFromImage(
        GenImageGradientRadial(256, 256, 0.55f, BLANK, (Color){ 0, 0, 0, 190 }));

    // 相机：站在玩家肩后，略微俯视，看向场地深处
    Camera3D cam = { 0 };
    cam.position = (Vector3){ 0.0f, 2.45f, 4.6f };
    cam.target   = (Vector3){ 0.0f, 1.55f, -9.0f };
    cam.up       = (Vector3){ 0.0f, 1.0f, 0.0f };
    cam.fovy     = 52.0f;
    cam.projection = CAMERA_PERSPECTIVE;

    const Vector3 enemyPos = { 0.0f, 1.60f, -9.0f };

    float t = shotMode ? 1.27f : 0.0f;

    while (!WindowShouldClose()) {
        float dt = shotMode ? 0.0f : GetFrameTime();
        t += dt;
        frame++;

        // 相机轻微呼吸，静止画面也能感到空间是活的
        cam.position.y = 2.45f + sinf(t * 0.7f) * 0.06f;
        cam.position.z = 4.60f + sinf(t * 0.5f) * 0.15f;

        // 敌方吟唱进度 0..1
        float castP = fmodf(t, 3.0f) / 3.0f;

        // 法术飞行：沿 z 轴朝相机飞过来，越近越大；给一点横向弧线，不挡住敌人
        float u = fmodf(t, 2.2f) / 2.2f;
        Vector3 bolt = { sinf(u * 3.0f) * 0.85f,
                         1.55f + sinf(u * 6.0f) * 0.15f,
                         -9.0f + u * 13.0f };
        float boltSize = 0.20f + u * 0.26f;

        BeginDrawing();
        ClearBackground((Color){ 10, 11, 15, 255 });

        // ================= 3D 世界 =================
        BeginMode3D(cam);

        DrawPlane((Vector3){ 0.0f, 0.0f, -6.0f }, (Vector2){ 26.0f, 26.0f },
                  (Color){ 20, 22, 28, 255 });
        DrawGrid(26, 1.0f);

        // 两侧柱子：纯粹用来读深度，越远越暗（假雾）
        for (int i = 0; i < 4; i++) {
            float z = -2.0f - i * 3.0f;
            float fade = 1.0f - (float)i * 0.20f;
            Color pc = { (unsigned char)(38 * fade), (unsigned char)(40 * fade),
                         (unsigned char)(50 * fade), 255 };
            DrawCylinder((Vector3){ -5.2f, 0.0f, z }, 0.30f, 0.30f, 5.0f, 8, pc);
            DrawCylinder((Vector3){  5.2f, 0.0f, z }, 0.30f, 0.30f, 5.0f, 8, pc);
        }

        // 敌人：2D 立绘 + billboard。size 是世界单位，不是像素
        DrawBillboardPro(cam, mage, (Rectangle){ 0, 0, (float)mage.width, (float)mage.height },
                         enemyPos, (Vector3){ 0, 1, 0 }, (Vector2){ 1.70f, 3.40f },
                         (Vector2){ 0.5f, 0.5f }, 0.0f, WHITE);

        // 脚下的软阴影：把纸片钉在地上的关键
        DrawCircle3D((Vector3){ 0.0f, 0.02f, -9.0f }, 0.75f, (Vector3){ 1, 0, 0 }, 90.0f,
                     (Color){ 0, 0, 0, 110 });
        for (int i = 1; i <= 6; i++)
            DrawCircle3D((Vector3){ 0.0f, 0.02f, -9.0f }, 0.75f - i * 0.10f,
                         (Vector3){ 1, 0, 0 }, 90.0f, (Color){ 0, 0, 0, 30 });

        // 吟唱进度 = 一个从脚下收拢的环。3D 里可读，且不占 HUD
        float ringR = 2.6f - castP * 1.8f;
        DrawCircle3D((Vector3){ 0.0f, 0.05f, -9.0f }, ringR,
                     (Vector3){ 1, 0, 0 }, 90.0f, (Color){ 214, 120, 80, 200 });
        DrawCircle3D((Vector3){ 0.0f, 0.05f, -9.0f }, ringR + 0.06f,
                     (Vector3){ 1, 0, 0 }, 90.0f, (Color){ 250, 199, 117, 90 });

        // 飞行中的法术：核心球 + 加法混合的光晕 + 拖尾
        BeginBlendMode(BLEND_ADDITIVE);
        for (int i = 1; i <= 7; i++) {
            float k = (float)i / 7.0f;
            float uk = u - k * 0.05f;
            if (uk < 0.0f) uk = 0.0f;
            Vector3 p = { sinf(uk * 3.0f) * 0.85f,
                          1.55f + sinf(uk * 6.0f) * 0.15f,
                          -9.0f + uk * 13.0f };
            float s = boltSize * (1.0f - k * 0.75f) * 2.6f;
            DrawBillboardPro(cam, glowTex, (Rectangle){ 0, 0, 64, 64 }, p,
                             (Vector3){ 0, 1, 0 }, (Vector2){ s, s }, (Vector2){ 0.5f, 0.5f },
                             0.0f, Fade((Color){ 230, 120, 70, 255 }, 0.30f * (1.0f - k)));
        }
        DrawSphereEx(bolt, boltSize, 8, 12, (Color){ 255, 214, 160, 255 });
        DrawBillboardPro(cam, glowTex, (Rectangle){ 0, 0, 64, 64 }, bolt,
                         (Vector3){ 0, 1, 0 }, (Vector2){ boltSize * 5.0f, boltSize * 5.0f },
                         (Vector2){ 0.5f, 0.5f }, 0.0f, (Color){ 255, 170, 110, 160 });
        EndBlendMode();

        EndMode3D();

        DrawTexturePro(vignette, (Rectangle){ 0, 0, 256, 256 },
                       (Rectangle){ 0, 0, 960, 640 }, (Vector2){ 0, 0 }, 0.0f, WHITE);

        // ================= 屏幕空间 HUD =================
        // 这些是玩家要读的信息，必须固定在屏幕上，不参与 3D 变换
        DrawTextEx(font, TXT_ENEMY, (Vector2){ 40, 24 }, 26, 1, (Color){ 220, 130, 120, 255 });
        DrawRectangle(40, 62, 380, 14, (Color){ 30, 32, 40, 255 });
        DrawRectangle(40, 62, 268, 14, (Color){ 190, 70, 70, 255 });

        DrawTextEx(font, TXT_CAST, (Vector2){ 40, 92 }, 18, 1, (Color){ 130, 132, 140, 255 });
        DrawTextEx(font, TXT_KEY, (Vector2){ 128, 82 }, 32, 1, (Color){ 250, 199, 117, 255 });
        DrawRectangle(160, 96, 260, 10, (Color){ 30, 32, 40, 255 });
        DrawRectangle(160, 96, (int)(260 * castP), 10, (Color){ 180, 100, 70, 255 });

        // 玩家吟唱条
        DrawRectangle(40, 528, 880, 16, (Color){ 30, 32, 40, 255 });
        DrawRectangle(40, 528, (int)(880 * 0.62f), 16, (Color){ 133, 183, 235, 255 });

        // 已输入序列
        DrawTextEx(font, TXT_INPUT, (Vector2){ 40, 556 }, 16, 1, (Color){ 120, 122, 130, 255 });
        for (int i = 0; i < 3; i++) {
            Color fill = (i == 0) ? (Color){ 113, 43, 19, 255 }
                       : (i == 1) ? (Color){ 12, 68, 124, 255 }
                       : (Color){ 30, 32, 40, 255 };
            DrawRectangleRounded((Rectangle){ 118.0f + i * 40, 550, 34, 34 }, 0.2f, 4, fill);
        }

        // 闪避次数
        DrawTextEx(font, TXT_SHIFT, (Vector2){ 780, 556 }, 16, 1, (Color){ 120, 122, 130, 255 });
        for (int i = 0; i < 3; i++) {
            Color c = (i < 2) ? (Color){ 133, 183, 235, 255 } : (Color){ 45, 47, 55, 255 };
            DrawRectangleRounded((Rectangle){ 850.0f + i * 26, 558, 20, 12 }, 0.4f, 4, c);
        }

        DrawTextEx(font, TXT_HINT, (Vector2){ 40, 604 }, 15, 1, (Color){ 96, 98, 106, 255 });
        DrawTextEx(font, TXT_TIP, (Vector2){ 620, 604 }, 15, 1, (Color){ 70, 110, 140, 255 });

        EndDrawing();

        if (shotMode && frame == 3) { TakeScreenshot("duel3d_shot.png"); break; }
    }

    UnloadTexture(glowTex);
    UnloadTexture(mage);
    UnloadFont(font);
    CloseWindow();
    return 0;
}
