#include "raylib.h"
#include "raymath.h"              // 矩阵运算：平移、旋转、相乘
#include <math.h>
#include <string.h>
#include <stdlib.h>

// ---------------------------------------------------------------------
// 把一张纹理做成一块 3D 平面（两个三角形构成一个四边形）
// 做成 Mesh 而不是用 2D 贴图，是为了让它能被任意旋转、接受透视
// 注意顶点顺序必须是逆时针，否则会被背面剔除掉，什么都看不见
// ---------------------------------------------------------------------
static Model MakeCardModel(Texture2D tex, float w, float h) {
    float hw = w * 0.5f, hh = h * 0.5f;

    float *verts = (float *)malloc(12 * sizeof(float));
    verts[0] = -hw; verts[1] = -hh; verts[2] = 0.0f;   // 左下
    verts[3] =  hw; verts[4] = -hh; verts[5] = 0.0f;   // 右下
    verts[6] =  hw; verts[7] =  hh; verts[8] = 0.0f;   // 右上
    verts[9] = -hw; verts[10] = hh; verts[11] = 0.0f;  // 左上

    float *uvs = (float *)malloc(8 * sizeof(float));
    uvs[0] = 0.0f; uvs[1] = 1.0f;                      // 左下角取图片左下
    uvs[2] = 1.0f; uvs[3] = 1.0f;                      // 右下角取图片右下
    uvs[4] = 1.0f; uvs[5] = 0.0f;                      // 右上角取图片右上
    uvs[6] = 0.0f; uvs[7] = 0.0f;                      // 左上角取图片左上

    float *normals = (float *)malloc(12 * sizeof(float));
    for (int i = 0; i < 4; i++) {                      // 四个顶点法线全部朝 +Z
        normals[i * 3 + 0] = 0.0f;
        normals[i * 3 + 1] = 0.0f;
        normals[i * 3 + 2] = 1.0f;
    }

    unsigned short *indices = (unsigned short *)malloc(6 * sizeof(unsigned short));
    indices[0] = 0; indices[1] = 1; indices[2] = 2;    // 第一个三角形
    indices[3] = 0; indices[4] = 2; indices[5] = 3;    // 第二个三角形

    Mesh mesh = { 0 };
    mesh.vertexCount = 4;
    mesh.triangleCount = 2;
    mesh.vertices = verts;
    mesh.texcoords = uvs;
    mesh.normals = normals;
    mesh.indices = indices;

    UploadMesh(&mesh, false);                          // 把顶点数据推上 GPU

    Model model = LoadModelFromMesh(mesh);             // 包成 Model，才能挂材质和纹理
    model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = tex;
    return model;
}

// 生成一张纯色卡面（正式项目里这里是 LoadTexture("card.png")）
static Texture2D MakeCardTexture(int w, int h, Color body, Color border) {
    Image img = GenImageColor(w, h, body);
    ImageDrawRectangleLines(&img, (Rectangle){ 0, 0, (float)w, (float)h }, 6, border);
    ImageDrawCircle(&img, w / 2, (int)(h * 0.42f), w / 5, border);
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    return tex;
}

int main(int argc, char **argv) {
    int shotMode = (argc > 1 && strcmp(argv[1], "--shot") == 0);

    InitWindow(960, 600, "SHIFT - 3D 卡牌");
    SetTargetFPS(60);

    // 透视相机：位置、看向哪里、上方向、视野角度
    Camera3D cam = { 0 };
    cam.position = (Vector3){ 0.0f, 2.2f, 6.0f };
    cam.target   = (Vector3){ 0.0f, 1.4f, 0.0f };
    cam.up       = (Vector3){ 0.0f, 1.0f, 0.0f };
    cam.fovy     = 45.0f;
    cam.projection = CAMERA_PERSPECTIVE;

    Texture2D cards[3];
    cards[0] = MakeCardTexture(300, 420, (Color){ 30, 58, 108, 255 }, (Color){ 190, 205, 230, 255 });
    cards[1] = MakeCardTexture(300, 420, (Color){ 108, 34, 40, 255 }, (Color){ 235, 200, 200, 255 });
    cards[2] = MakeCardTexture(300, 420, (Color){ 28, 84, 64, 255 }, (Color){ 195, 230, 210, 255 });

    Model models[3];
    for (int i = 0; i < 3; i++) models[i] = MakeCardModel(cards[i], 1.5f, 2.1f);

    float camAngle = 0.0f;     // 相机绕场景转的角度
    float t = 0.0f;            // 总时间，用来做漂浮动画
    int frame = 0;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        t += dt;

        camAngle += GetMouseDelta().x * 0.006f;            // 鼠标左右拖 → 相机绕场景转
        cam.position.x = sinf(camAngle) * 6.0f;            // 极坐标算相机位置
        cam.position.z = cosf(camAngle) * 6.0f;

        BeginDrawing();
        ClearBackground((Color){ 14, 15, 19, 255 });

        BeginMode3D(cam);                                   // 进入 3D 坐标系

        DrawGrid(20, 1.0f);                                 // 地面网格，提供空间感

        for (int i = 0; i < 3; i++) {
            float x = (i - 1) * 1.85f;                      // 三张卡沿 X 轴排开
            float bob = sinf(t * 1.6f + i * 0.9f) * 0.09f;  // 上下轻微漂浮，错开相位

            Vector3 pos = { x, 1.5f + bob, 0.0f };
            Vector3 rot = { -6.0f, (1 - i) * 26.0f, (i - 1) * 2.5f };  // 排成扇形，微微朝内

            Matrix transform = MatrixMultiply(              // 先旋转，再平移
                MatrixTranslate(pos.x, pos.y, pos.z),
                MatrixRotateXYZ((Vector3){ rot.x * DEG2RAD, rot.y * DEG2RAD, rot.z * DEG2RAD })
            );
            DrawMesh(models[i].meshes[0], models[i].materials[0], transform);
        }

        EndMode3D();                                        // 退出 3D 坐标系

        DrawText("Drag mouse to orbit", 20, 20, 20, (Color){ 130, 132, 140, 255 });

        EndDrawing();

        if (shotMode && ++frame == 40) { TakeScreenshot("card3d_shot.png"); break; }
    }

    for (int i = 0; i < 3; i++) {
        UnloadModel(models[i]);
        UnloadTexture(cards[i]);
    }
    CloseWindow();
    return 0;
}
