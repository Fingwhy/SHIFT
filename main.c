// =====================================================================
// SHIFT —— 一个最小可操作的 raylib 程序
// 编译：双击 build.bat
// 功能：方向键移动卡牌，鼠标点击切换颜色，屏幕显示点击次数
// =====================================================================

#include "raylib.h"                     // 引入 raylib 头文件，窗口、绘图、字体、输入的函数都在这里声明
#include <string.h>                     // 引入标准库字符串函数，本文件用到 strcmp 比较命令行参数

// ---------------------------------------------------------------------
// 工具函数：把一段 UTF-8 字符串拆成 Unicode 码点数组
// 为什么需要它：raylib 加载字体时必须指定"要预烘焙哪些字符"。
// 中文字体有上万个字，全部加载会让字体纹理大到爆，所以只挑真正要显示的字。
// ---------------------------------------------------------------------
static int Utf8ToCodepoints(const char *text, int *out, int max) {  // text=输入字符串，out=输出数组，max=数组容量
    int n = 0;                                          // 已写入的码点数量，同时当作数组下标用
    const unsigned char *p = (const unsigned char *)text;  // 用无符号指针逐字节读，避免符号扩展出错

    while (*p && n < max) {                             // 还没到字符串结尾，数组也还没写满，就继续
        int cp = 0, len = 1;                            // cp=解出来的码点，len=这个字符占几个字节

        if (*p < 0x80) {                                // 最高位是 0，说明是单字节字符（纯 ASCII）
            cp = *p;                                    // 直接就是码点本身
            len = 1;                                    // 占 1 个字节
        }
        else if ((*p >> 5) == 0x6) {                    // 前三位是 110，说明是双字节字符
            cp = *p & 0x1F;                             // 取低 5 位，作为码点的高位
            len = 2;                                    // 占 2 个字节
        }
        else if ((*p >> 4) == 0xE) {                    // 前四位是 1110，说明是三字节字符（汉字都在这里）
            cp = *p & 0x0F;                             // 取低 4 位，作为码点的高位
            len = 3;                                    // 占 3 个字节
        }
        else {                                          // 剩下的就是四字节字符（emoji 之类）
            cp = *p & 0x07;                             // 取低 3 位
            len = 4;                                    // 占 4 个字节
        }

        for (int i = 1; i < len; i++)                   // 把后面跟着的"延续字节"逐个拼进来
            cp = (cp << 6) | (p[i] & 0x3F);             // 已有码点左移 6 位，再或上当前字节的低 6 位

        out[n++] = cp;                                  // 把这个码点存进输出数组，计数加一
        p += len;                                       // 指针跳过这个字符占的全部字节
    }

    return n;                                           // 返回一共解出多少个码点
}

// ---------------------------------------------------------------------
// 程序入口
// ---------------------------------------------------------------------
int main(int argc, char **argv) {                       // argc=参数个数，argv=参数字符串数组
    int shotMode = (argc > 1 && strcmp(argv[1], "--shot") == 0);  // 带 --shot 参数时进入截图模式（用于自动验证画面）

    InitWindow(960, 600, "SHIFT");                      // 创建 960×600 的窗口，标题为 SHIFT；到这步才有 OpenGL 上下文
    SetTargetFPS(60);                                   // 把主循环限制在每秒 60 帧，不让显卡空转跑满

    int cps[512];                                       // 存放要预烘焙的字符码点
    int n = 0;                                          // 当前已收集的码点数量
    for (int c = 32; c < 127; c++) cps[n++] = c;        // 先把全部可见 ASCII（空格到 ~）加进去，数字和英文靠它们

    n += Utf8ToCodepoints("方向键移动卡牌点击次数", cps + n, 512 - n);  // 再把界面上会显示的中文字符补进去

    Font font = LoadFontEx("assets/font.ttf", 40, cps, n);  // 从 ttf 里只烘焙上面列出的字，基础字号 40
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);  // 开双线性过滤，字体被放大缩小时边缘不会出锯齿

    Rectangle card = { 400, 220, 160, 220 };            // 卡牌矩形：左上角在 (400,220)，宽 160，高 220
    Color cardColor = (Color){ 38, 92, 160, 255 };      // 卡牌颜色：R38 G92 B160，第四个 255 是完全不透明
    int clicks = 0;                                     // 点击计数器，初始为 0
    int frame = 0;                                      // 帧计数器，只有截图模式会用到

    while (!WindowShouldClose()) {                      // 主循环：窗口没被关掉就一直转，这就是游戏的心脏
        float dt = GetFrameTime();                      // 取上一帧耗时（秒），通常约 0.0167，乘它能让速度不受帧率影响

        if (IsKeyDown(KEY_RIGHT)) card.x += 420 * dt;   // 右方向键按住，每秒向右移动 420 像素
        if (IsKeyDown(KEY_LEFT))  card.x -= 420 * dt;   // 左方向键按住，每秒向左移动 420 像素
        if (IsKeyDown(KEY_UP))    card.y -= 420 * dt;   // 上方向键按住，每秒上移（屏幕 y 轴朝下，所以是减）
        if (IsKeyDown(KEY_DOWN))  card.y += 420 * dt;   // 下方向键按住，每秒下移

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&  // 鼠标左键"刚按下"的那一帧，不是一直按着为真
            CheckCollisionPointRec(GetMousePosition(), card)) {  // 并且鼠标位置落在卡牌矩形内部
            clicks++;                                   // 点击次数加一
            cardColor = (clicks % 2)                    // 判断点击次数是奇数还是偶数
                ? (Color){ 190, 62, 58, 255 }           // 奇数：换成红色
                : (Color){ 38, 92, 160, 255 };          // 偶数：换回蓝色
        }

        BeginDrawing();                                 // 开始绘制这一帧，后面的绘制命令都先写进缓冲区
        ClearBackground((Color){ 16, 17, 21, 255 });    // 用深灰黑铺满整个屏幕，相当于清屏

        DrawRectangleRec(card, cardColor);              // 画实心矩形，就是那张卡牌的底面
        DrawRectangleLinesEx(card, 2, (Color){ 226, 228, 236, 255 });  // 沿矩形画 2 像素宽的边框，让边缘清楚
        DrawCircle((int)(card.x + 80),                  // 圆心 x：卡牌左边缘往右 80 像素，正好是水平中线
                   (int)(card.y + 80),                  // 圆心 y：卡牌上边缘往下 80 像素
                   26,                                  // 半径 26 像素
                   (Color){ 240, 242, 248, 255 });      // 白色，当作卡面图案的占位符

        DrawTextEx(font, "SHIFT", (Vector2){ 40, 32 },  // 在 (40,32) 处画标题
                   40,                                 // 字号 40
                   2,                                  // 字间距 2 像素
                   RAYWHITE);                           // 近白色

        DrawTextEx(font, "方向键移动卡牌", (Vector2){ 40, 88 },  // 标题下方画一行操作提示
                   24,                                 // 字号 24
                   1,                                  // 字间距 1
                   (Color){ 150, 152, 160, 255 });     // 灰色，不抢视线

        const char *counter = TextFormat("点击次数 %i", clicks);  // 把数字格式化进字符串（返回静态缓冲区，别连着调两次）
        DrawTextEx(font, counter, (Vector2){ 40, 530 },  // 在屏幕左下角画计数器
                   26,                                 // 字号 26
                   1,                                  // 字间距 1
                   (Color){ 200, 202, 210, 255 });     // 浅灰色

        EndDrawing();                                   // 结束这一帧：提交缓冲区给 GPU，并交换前后缓冲显示出来

        if (shotMode && ++frame == 20) {                // 截图模式下，帧计数器加到 20 时
            TakeScreenshot("shot.png");                 // 把当前画面存成 png 文件
            break;                                      // 直接跳出主循环，方便自动验证
        }
    }

    UnloadFont(font);                                   // 释放字体占用的显存和内存
    CloseWindow();                                      // 关闭窗口，销毁 OpenGL 上下文
    return 0;                                           // 返回 0 表示正常退出
}
