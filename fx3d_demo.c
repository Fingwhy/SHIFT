// =====================================================================
// SHIFT - 3D 特效演示
//
// 目标：用纯代码（无美术资源）做出一个能看很久的 3D 场景，验证三件事
//   1. 自定义 shader 能做菲涅尔边缘光 / 能量流动，不用贴图也有质感
//   2. 加法混合的 billboard 光晕可以伪造 bloom，成本极低
//   3. 线框几何 + 发光线条，是这个游戏想要的美术方向
//
// 场景构成：悬浮晶体（自定义 mesh）+ 轨道线框环 + 粒子涡流 + 地面能量阵
//
// 操作：拖拽鼠标旋转视角 / 滚轮缩放 / G 线框 / 1 2 3 换配色 / 空格 暂停自转
//
// 编译：gcc fx3d_demo.c ime_fix.c -o fx3d_demo.exe -I vendor/raylib/include
//       -L vendor/raylib/lib -lraylib -lopengl32 -lgdi32 -lwinmm -limm32 -static-libgcc -lm
// =====================================================================

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void DisableIME(void *windowHandle);
void DisableDpiScaling(void);

#define SW 960
#define SH 640
#define PI2 6.28318530718f

// ---------------------------------------------------------------------
// 界面文字（中文要预先收集字符，否则 raylib 默认字体画不出来）
// ---------------------------------------------------------------------
#define TXT_TITLE  "3D 特效演示"
#define TXT_SUB    "晶体 · 能量环 · 粒子涡流"
#define TXT_HINT   "拖拽 旋转视角    滚轮 缩放    G 线框    1 2 3 配色    空格 自转"
#define TXT_FPS    "FPS"
#define TXT_PAUSE  "自转 已暂停"

static const char *THEME_NAME[3] = { "裂隙 · 冰蓝", "余烬 · 赤金", "秘银 · 鎏金" };

static const char *UI_TEXT[] = {
    TXT_TITLE, TXT_SUB, TXT_HINT, TXT_FPS, TXT_PAUSE,
    "裂隙 · 冰蓝", "余烬 · 赤金", "秘银 · 鎏金"
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
// 自定义着色器：菲涅尔边缘光 + 竖向能量流动
//
// 里面几个 uniform 名字（mvp / matModel / matNormal / colDiffuse）是
// raylib 的约定，名字必须完全一致，否则每帧不会自动喂矩阵进来
// ---------------------------------------------------------------------
static const char *GEM_VS = R"(
#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec3 vNormal;
out vec3 vPos;
void main() {
    vec4 wp = matModel * vec4(vertexPosition, 1.0);
    vPos = wp.xyz;
    vNormal = normalize((matNormal * vec4(vertexNormal, 0.0)).xyz);
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

static const char *GEM_FS = R"(
#version 330
in vec3 vNormal;
in vec3 vPos;
uniform vec4 colDiffuse;
uniform vec3 rimColor;
uniform vec3 coreColor;
uniform vec3 camPos;
uniform float time;
uniform float rimPower;
out vec4 finalColor;
void main() {
    vec3 N = normalize(vNormal);
    vec3 V = normalize(camPos - vPos);
    // 视线越接近掠射角越亮 —— 也就是"边缘发光"的经典做法
    float fres = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), rimPower);

    // 竖向扫光：让表面看起来有能量在往上流，而不是一块死玻璃
    float flow = 0.5 + 0.5 * sin(vPos.y * 7.0 - time * 3.0);

    // 基础色压一点 + 边缘狠狠提亮，晶体才立得起来，不然整个是一块黑
    vec3 col = coreColor * 1.15 + rimColor * fres * 1.9;
    col *= 0.70 + 0.30 * flow;

    float alpha = 0.46 + 0.54 * fres;      // 正面半透，边缘不透明
    finalColor = vec4(col * colDiffuse.rgb, alpha * colDiffuse.a);
}
)";

// ---------------------------------------------------------------------
// 生成晶体：上下两个锥体拼成的多面宝石，手工三角面片
//
// 手工建面最容易翻车的是绕序（背面剔除后什么都看不见），
// 所以每个三角形算完法线后跟"向外"方向对一下，反了就换顶点顺序
// ---------------------------------------------------------------------
static int gVert = 0;

static void PushTri(float *v, float *n, Vector3 a, Vector3 b, Vector3 c) {
    Vector3 nr = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a),
                                                     Vector3Subtract(c, a)));
    Vector3 ctr = Vector3Scale(Vector3Add(Vector3Add(a, b), c), 1.0f / 3.0f);
    if (Vector3DotProduct(nr, ctr) < 0.0f) {   // 法线朝内 → 反过来
        Vector3 tmp = b; b = c; c = tmp;
        nr = Vector3Negate(nr);
    }
    Vector3 p[3] = { a, b, c };
    for (int i = 0; i < 3; i++) {
        v[gVert * 3 + 0] = p[i].x; v[gVert * 3 + 1] = p[i].y; v[gVert * 3 + 2] = p[i].z;
        n[gVert * 3 + 0] = nr.x;   n[gVert * 3 + 1] = nr.y;   n[gVert * 3 + 2] = nr.z;
        gVert++;
    }
}

static Mesh MakeGemMesh(int sides, float radius, float topH, float botH) {
    int tris = sides * 2, vc = tris * 3;
    float *verts = (float *)calloc(vc * 3, sizeof(float));
    float *norms = (float *)calloc(vc * 3, sizeof(float));
    gVert = 0;

    for (int i = 0; i < sides; i++) {
        float a0 = (float)i / sides * PI2;
        float a1 = (float)(i + 1) / sides * PI2;
        Vector3 p0  = { cosf(a0) * radius, 0.0f, sinf(a0) * radius };
        Vector3 p1  = { cosf(a1) * radius, 0.0f, sinf(a1) * radius };
        Vector3 top = { 0.0f,  topH, 0.0f };
        Vector3 bot = { 0.0f, -botH, 0.0f };
        PushTri(verts, norms, p0, p1, top);    // 上半锥
        PushTri(verts, norms, p1, p0, bot);    // 下半锥
    }

    Mesh mesh = { 0 };
    mesh.vertexCount   = vc;
    mesh.triangleCount = tris;
    mesh.vertices      = verts;
    mesh.normals       = norms;
    UploadMesh(&mesh, false);
    return mesh;
}

// ---------------------------------------------------------------------
// 光晕贴图：一张从中心到边缘渐隐的圆，加法混合叠出来的就是 bloom
// ---------------------------------------------------------------------
static Texture2D MakeGlow(void) {
    Texture2D t = LoadTextureFromImage(GenImageGradientRadial(64, 64, 0.0f, WHITE, BLANK));
    SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    return t;
}

// 在 3D 里画一个永远面向相机的光斑
static void DrawGlow(Camera3D cam, Texture2D glow, Vector3 p, float size, Color c) {
    DrawBillboardPro(cam, glow, (Rectangle){ 0, 0, 64, 64 }, p, (Vector3){ 0, 1, 0 },
                     (Vector2){ size, size }, (Vector2){ 0.5f, 0.5f }, 0.0f, c);
}

// ---------------------------------------------------------------------
// 能量环：一圈发光线。用线而不是圆环模型，是因为线能和加法混合
// 直接叠出细亮的高光，比实体环更像"能量"
// ---------------------------------------------------------------------
static void DrawEnergyRing(Vector3 center, float radius, Vector3 axis,
                           float spin, Color col, float bright) {
    Vector3 n = Vector3Normalize(axis);
    // 随便找一个和法线不平行的向量，叉出一个正交基
    Vector3 helper = (fabsf(n.y) < 0.9f) ? (Vector3){ 0, 1, 0 } : (Vector3){ 1, 0, 0 };
    Vector3 u = Vector3Normalize(Vector3CrossProduct(helper, n));
    Vector3 v = Vector3CrossProduct(n, u);

    const int seg = 96;
    for (int i = 0; i < seg; i++) {
        float a0 = spin + (float)i / seg * PI2;
        float a1 = spin + (float)(i + 1) / seg * PI2;
        Vector3 p0 = Vector3Add(center, Vector3Add(Vector3Scale(u, cosf(a0) * radius),
                                                   Vector3Scale(v, sinf(a0) * radius)));
        Vector3 p1 = Vector3Add(center, Vector3Add(Vector3Scale(u, cosf(a1) * radius),
                                                   Vector3Scale(v, sinf(a1) * radius)));
        // 沿环走一圈的明暗起伏，读起来像能量在环上跑
        float wave = 0.30f + 0.70f * powf(0.5f + 0.5f * sinf(a0 * 3.0f + spin * 6.0f), 2.0f);
        DrawLine3D(p0, p1, Fade(col, wave * bright));
    }
}

// ---------------------------------------------------------------------
// 粒子涡流
// ---------------------------------------------------------------------
#define SPARK_COUNT 260

typedef struct { float ang, rad, y, spd, size, tw; } Spark;

int main(int argc, char **argv) {
    int shotMode = (argc > 1 && strcmp(argv[1], "--shot") == 0);
    int frame = 0;

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    DisableDpiScaling();
    InitWindow(SW, SH, "SHIFT - 3D 特效演示");
    DisableIME(GetWindowHandle());
    SetTargetFPS(60);

    int cps[1024];
    int nc = CollectGlyphs(cps, 1024);
    Font font = LoadFontEx("assets/font.ttf", 64, cps, nc);
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);

    // ---- 配色 ----
    Vector3 coreCol[3] = { { 0.10f, 0.42f, 0.78f }, { 0.72f, 0.14f, 0.20f }, { 0.62f, 0.40f, 0.06f } };
    Vector3 rimCol[3]  = { { 0.62f, 0.94f, 1.00f }, { 1.00f, 0.72f, 0.42f }, { 1.00f, 0.92f, 0.62f } };
    int theme = 0;

    // ---- 晶体 ----
    Model gem = LoadModelFromMesh(MakeGemMesh(8, 0.95f, 1.55f, 1.05f));
    Model gemIn = LoadModelFromMesh(MakeGemMesh(8, 0.58f, 0.95f, 0.66f));

    Shader shader = LoadShaderFromMemory(GEM_VS, GEM_FS);
    int shValid = IsShaderValid(shader);
    if (shValid) {
        gem.materials[0].shader   = shader;
        gemIn.materials[0].shader = shader;
    }
    int locRim   = shValid ? GetShaderLocation(shader, "rimColor")  : -1;
    int locCore  = shValid ? GetShaderLocation(shader, "coreColor") : -1;
    int locCam   = shValid ? GetShaderLocation(shader, "camPos")    : -1;
    int locTime  = shValid ? GetShaderLocation(shader, "time")      : -1;
    int locRimP  = shValid ? GetShaderLocation(shader, "rimPower")  : -1;
    float rimPower = 2.2f;

    // ---- 轨道环（线框环，给场景一个"结构"）----
    Model torus = LoadModelFromMesh(GenMeshTorus(1.95f, 0.018f, 64, 8));

    Texture2D glow = MakeGlow();
    Texture2D vignette = LoadTextureFromImage(
        GenImageGradientRadial(256, 256, 0.52f, BLANK, (Color){ 0, 0, 0, 215 }));

    // ---- 粒子：三条螺旋臂，比纯随机撒点更有结构，也更像"涡流" ----
    Spark sparks[SPARK_COUNT];
    srand(7);
    for (int i = 0; i < SPARK_COUNT; i++) {
        float k = (float)i / (float)SPARK_COUNT;        // 0..1，决定半径
        int arm = i % 3;
        sparks[i].rad  = 1.02f + k * 1.95f;
        sparks[i].ang  = (float)arm / 3.0f * PI2 + k * 2.4f;
        sparks[i].y    = sinf(k * PI2 * 1.4f + arm * 2.1f) * 1.15f
                       + ((float)(rand() % 100) / 100.0f - 0.5f) * 0.35f;
        sparks[i].spd  = 0.34f / sparks[i].rad + 0.05f; // 越靠内转得越快
        sparks[i].size = 0.045f + (float)(rand() % 100) / 100.0f * 0.075f;
        sparks[i].tw   = (float)(rand() % 100) / 100.0f * PI2;
    }

    // ---- 相机 ----
    Camera3D cam = { 0 };
    cam.up = (Vector3){ 0, 1, 0 };
    cam.fovy = 46.0f;
    cam.projection = CAMERA_PERSPECTIVE;

    float yaw = 0.35f, pitch = 0.16f, dist = 7.4f;
    float t = shotMode ? 3.4f : 0.0f;
    float gemSpin = 0.0f;
    int autoSpin = 1, wire = 0;

    const Vector3 gemPos = { 0.0f, 1.85f, 0.0f };

    while (!WindowShouldClose()) {
        float dt = shotMode ? 0.0f : GetFrameTime();
        t += dt;

        // ---- 交互 ----
        if (!shotMode) {
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                Vector2 d = GetMouseDelta();
                yaw -= d.x * 0.006f;
                pitch += d.y * 0.004f;
            }
            float wheel = GetMouseWheelMove();
            if (wheel != 0.0f) dist -= wheel * 0.55f;
            if (IsKeyPressed(KEY_G)) wire = !wire;
            if (IsKeyPressed(KEY_SPACE)) autoSpin = !autoSpin;
            if (IsKeyPressed(KEY_ONE))   theme = 0;
            if (IsKeyPressed(KEY_TWO))   theme = 1;
            if (IsKeyPressed(KEY_THREE)) theme = 2;
        }
        if (pitch < -0.55f) pitch = -0.55f;
        if (pitch >  0.95f) pitch =  0.95f;
        if (dist < 3.4f) dist = 3.4f;
        if (dist > 13.0f) dist = 13.0f;
        if (autoSpin) { yaw += dt * 0.05f; gemSpin += dt * 0.35f; }

        cam.position = (Vector3){
            sinf(yaw) * cosf(pitch) * dist,
            1.9f + sinf(pitch) * dist,
            cosf(yaw) * cosf(pitch) * dist
        };
        cam.target = (Vector3){ 0.0f, 1.85f, 0.0f };

        Color themeLine = ColorFromNormalized((Vector4){ rimCol[theme].x, rimCol[theme].y, rimCol[theme].z, 1.0f });

        // ---- shader 参数 ----
        if (shValid) {
            SetShaderValue(shader, locRim,   &rimCol[theme],  SHADER_UNIFORM_VEC3);
            SetShaderValue(shader, locCore,  &coreCol[theme], SHADER_UNIFORM_VEC3);
            SetShaderValue(shader, locCam,   &cam.position,   SHADER_UNIFORM_VEC3);
            SetShaderValue(shader, locTime,  &t,              SHADER_UNIFORM_FLOAT);
            SetShaderValue(shader, locRimP,  &rimPower,       SHADER_UNIFORM_FLOAT);
        }

        BeginDrawing();
        ClearBackground((Color){ 8, 9, 13, 255 });

        BeginMode3D(cam);

        // ============ 地面 ============
        DrawPlane((Vector3){ 0, 0, 0 }, (Vector2){ 44, 44 }, (Color){ 12, 13, 18, 255 });

        // 自定义网格：越远越淡，比 DrawGrid 多一点"消失在地平线"的感觉
        for (int i = -18; i <= 18; i++) {
            float fade = powf(1.0f - fabsf((float)i) / 19.0f, 1.6f);
            Color c = Fade((Color){ 46, 52, 70, 255 }, fade * 0.55f);
            DrawLine3D((Vector3){ (float)i, 0.002f, -18 }, (Vector3){ (float)i, 0.002f, 18 }, c);
            DrawLine3D((Vector3){ -18, 0.002f, (float)i }, (Vector3){ 18, 0.002f, (float)i }, c);
        }

        // 地面能量阵：从中心一圈圈往外扩，扩到边上淡掉
        rlDisableDepthMask();
        BeginBlendMode(BLEND_ADDITIVE);
        for (int i = 0; i < 3; i++) {
            float phase = fmodf(t * 0.28f + (float)i / 3.0f, 1.0f);
            float r = 1.0f + phase * 7.5f;
            DrawEnergyRing((Vector3){ 0, 0.01f, 0 }, r, (Vector3){ 0, 1, 0 },
                           phase * 2.0f, themeLine, (1.0f - phase) * 0.55f);
        }
        // 中心脚池：几层同心圆叠出来的地面辉光
        for (int i = 0; i < 18; i++) {
            float k = (float)i / 18.0f;
            DrawCircle3D((Vector3){ 0, 0.008f, 0 }, 3.1f - k * 3.0f,
                         (Vector3){ 1, 0, 0 }, 90.0f, Fade(themeLine, 0.012f));
        }
        EndBlendMode();
        rlEnableDepthMask();

        // ============ 晶体本体 ============
        // 外层：缓慢自转 + 轻微上下浮动
        float bob = sinf(t * 1.1f) * 0.10f;
        Matrix outer = MatrixMultiply(
            MatrixTranslate(gemPos.x, gemPos.y + bob, gemPos.z),
            MatrixMultiply(
                MatrixRotateY(gemSpin),
                MatrixMultiply(MatrixRotateZ(sinf(t * 0.6f) * 0.10f),
                               MatrixRotateX(sinf(t * 0.45f) * 0.07f))
            )
        );

        BeginBlendMode(BLEND_ALPHA);
        DrawMesh(gem.meshes[0], gem.materials[0], outer);
        EndBlendMode();

        // 棱：线框再画一遍，边缘更锋利、也更"全息"
        // 发光通道必须关掉深度写入，否则先画的那张光斑会把它后面的光全挡掉，
        // 屏幕上出现一堆硬边方块 —— 这是透明渲染最经典的坑
        rlDisableDepthMask();
        BeginBlendMode(BLEND_ADDITIVE);
        rlEnableWireMode();
        Material wireMat = gem.materials[0];
        wireMat.maps[MATERIAL_MAP_DIFFUSE].color = Fade(themeLine, 0.30f);
        DrawMesh(gem.meshes[0], wireMat, outer);
        rlDisableWireMode();
        EndBlendMode();
        rlEnableDepthMask();

        // 内核：反向旋转的小晶体。关掉深度测试，让它像从晶体内部透出来
        Matrix inner = MatrixMultiply(
            MatrixTranslate(gemPos.x, gemPos.y + bob * 1.3f, gemPos.z),
            MatrixMultiply(MatrixRotateY(-gemSpin * 1.7f), MatrixRotateX(gemSpin * 0.9f))
        );
        rlDisableDepthTest();
        rlDisableDepthMask();
        BeginBlendMode(BLEND_ADDITIVE);
        Material innerMat = gemIn.materials[0];
        innerMat.maps[MATERIAL_MAP_DIFFUSE].color = (Color){ 255, 255, 255, 255 };
        DrawMesh(gemIn.meshes[0], innerMat, inner);

        // 内核的灯火：几层光斑，越靠内越亮
        float pulse = 0.85f + 0.15f * sinf(t * 2.6f);
        DrawGlow(cam, glow, (Vector3){ gemPos.x, gemPos.y + bob, gemPos.z }, 3.6f * pulse,
                 Fade(themeLine, 0.20f));
        DrawGlow(cam, glow, (Vector3){ gemPos.x, gemPos.y + bob, gemPos.z }, 1.6f * pulse,
                 Fade(themeLine, 0.34f));
        DrawGlow(cam, glow, (Vector3){ gemPos.x, gemPos.y + bob, gemPos.z }, 0.62f,
                 Fade((Color){ 255, 255, 255, 255 }, 0.65f));
        EndBlendMode();
        rlEnableDepthMask();
        rlEnableDepthTest();

        // ============ 轨道环 ============
        rlDisableDepthMask();
        BeginBlendMode(BLEND_ADDITIVE);
        rlEnableWireMode();
        for (int i = 0; i < 2; i++) {
            Matrix m = MatrixMultiply(
                MatrixTranslate(0, gemPos.y, 0),
                MatrixMultiply(
                    MatrixRotateX((i == 0 ? 1.0f : -0.6f) + sinf(t * 0.4f + i) * 0.12f),
                    MatrixMultiply(MatrixRotateZ(sinf(t * 0.3f + i * 2.0f) * 0.20f),
                                   MatrixRotateY(gemSpin * (i == 0 ? -0.8f : 0.55f)))
                )
            );
            Material tm = torus.materials[0];
            tm.maps[MATERIAL_MAP_DIFFUSE].color = Fade(themeLine, 0.30f);
            DrawMesh(torus.meshes[0], tm, m);
        }
        rlDisableWireMode();
        EndBlendMode();

        // 发光的细环，和线框环错开角度
        BeginBlendMode(BLEND_ADDITIVE);
        DrawEnergyRing((Vector3){ 0, gemPos.y, 0 }, 2.35f, (Vector3){ 0.35f, 1.0f, 0.15f },
                       t * 0.55f, themeLine, 0.75f);
        DrawEnergyRing((Vector3){ 0, gemPos.y, 0 }, 2.62f, (Vector3){ -0.55f, 0.55f, 1.0f },
                       -t * 0.40f, themeLine, 0.50f);
        EndBlendMode();

        // ============ 粒子涡流 ============
        // 晶体背后再铺一层大而暗的光，被晶体挡住后只剩一圈晕，等于免费 bloom
        DrawGlow(cam, glow, (Vector3){ 0, gemPos.y + 0.15f, -2.6f }, 7.4f,
                 Fade(themeLine, 0.085f));

        BeginBlendMode(BLEND_ADDITIVE);
        for (int i = 0; i < SPARK_COUNT; i++) {
            Spark *s = &sparks[i];
            s->ang += s->spd * dt;
            float wob = sinf(t * 1.6f + s->tw) * 0.22f;
            float r = s->rad + wob * 0.30f;
            float y = gemPos.y + s->y + sinf(t * 1.15f + s->tw * 1.7f) * 0.22f;

            Vector3 p = { cosf(s->ang) * r, y, sinf(s->ang) * r };
            // 拖尾：拿前一个角度的位置画一条短亮线，粒子就有了速度感
            float a2 = s->ang - 0.14f;
            Vector3 q = { cosf(a2) * r, y, sinf(a2) * r };
            float tw = 0.55f + 0.45f * sinf(t * 3.0f + s->tw * 2.3f);
            DrawLine3D(p, q, Fade(themeLine, 0.45f * tw));
            DrawGlow(cam, glow, p, s->size * 4.2f, Fade(themeLine, 0.32f * tw));
            DrawGlow(cam, glow, p, s->size * 1.7f,
                     Fade((Color){ 255, 255, 255, 255 }, 0.40f * tw));
        }
        EndBlendMode();
        rlEnableDepthMask();

        EndMode3D();

        // ============ 屏幕空间后期 ============
        DrawTexturePro(vignette, (Rectangle){ 0, 0, 256, 256 },
                       (Rectangle){ 0, 0, SW, SH }, (Vector2){ 0, 0 }, 0.0f, WHITE);

        // 顶部压一条渐变，让 HUD 文字不被 3D 抢走注意力
        DrawRectangleGradientV(0, 0, SW, 110, (Color){ 8, 9, 13, 200 }, (Color){ 8, 9, 13, 0 });

        // ============ HUD ============
        DrawTextEx(font, TXT_TITLE, (Vector2){ 40, 28 }, 30, 2, themeLine);
        DrawRectangle(40, 68, 64, 2, Fade(themeLine, 0.55f));
        DrawTextEx(font, TXT_SUB, (Vector2){ 40, 78 }, 16, 1, (Color){ 120, 126, 140, 255 });

        // 右上角：当前配色 / 状态
        DrawTextEx(font, THEME_NAME[theme],
                   (Vector2){ SW - 40 - MeasureTextEx(font, THEME_NAME[theme], 18, 1).x, 34 },
                   18, 1, Fade(themeLine, 0.85f));
        if (!autoSpin) {
            Vector2 m = MeasureTextEx(font, TXT_PAUSE, 14, 1);
            DrawTextEx(font, TXT_PAUSE, (Vector2){ SW - 40 - m.x, 62 }, 14, 1,
                       (Color){ 110, 114, 126, 255 });
        }

        char fps[32];
        sprintf(fps, "%d %s", GetFPS(), TXT_FPS);
        Vector2 fm = MeasureTextEx(font, fps, 14, 1);
        DrawTextEx(font, fps, (Vector2){ SW - 40 - fm.x, SH - 40 }, 14, 1,
                   (Color){ 84, 88, 100, 255 });

        DrawTextEx(font, TXT_HINT, (Vector2){ 40, SH - 44 }, 15, 1, (Color){ 96, 100, 112, 255 });

        EndDrawing();

        if (shotMode && ++frame == 5) { TakeScreenshot("fx3d_shot.png"); break; }
    }

    UnloadTexture(vignette);
    UnloadTexture(glow);
    UnloadModel(torus);
    UnloadModel(gemIn);
    UnloadModel(gem);
    if (shValid) UnloadShader(shader);
    UnloadFont(font);
    CloseWindow();
    return 0;
}
