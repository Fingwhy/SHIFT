#define RAYGUI_IMPLEMENTATION          // 告诉 raygui：在本文件里生成实现代码（整个项目只能有一个文件这么写）
#include "raylib.h"                    // raylib 主库，窗口 / 绘图 / 音频都在这里
#include "raygui.h"                    // raygui，即时模式 GUI，提供按钮、滑块、输入框
#include <math.h>                      // sinf / expf，用来合成音效
#include <stdlib.h>                    // malloc / free
#include <string.h>                    // strcmp

// ---------------------------------------------------------------------
// 界面上所有会出现的文字，集中放在这里
// 目的：加载字体时直接从这份清单提取汉字，永远不会漏字
// ---------------------------------------------------------------------
static const char *UI_TEXT[] = {
    "音频与 UI 验证",                    // 标题
    "播放音效",                          // 按钮 1
    "静音", "取消静音",                  // 按钮 2，两种状态
    "音量",                              // 滑块左边的标签
    "程序合成音效 + 流式背景音乐",       // 说明第一行
    "点按任意处试听",                    // 说明第二行
    "音频设备 ", "在线", "离线",         // 状态行
    "音乐时长 ", " 秒",                  // 状态行
};
#define UI_TEXT_COUNT ((int)(sizeof(UI_TEXT) / sizeof(UI_TEXT[0])))

// UTF-8 转码点，和中文字体加载配套用
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

// 把 UI_TEXT 里所有字符串的字符收集成码点数组，供 LoadFontEx 使用
static int CollectUIGlyphs(int *out, int max) {
    int n = 0;
    for (int c = 32; c < 127; c++) out[n++] = c;          // 先收全部可见 ASCII：数字、字母、百分号都靠它
    for (int i = 0; i < UI_TEXT_COUNT; i++) {             // 再逐个字符串提取中文
        n += Utf8ToCodepoints(UI_TEXT[i], out + n, max - n);
    }
    return n;
}

// ---------------------------------------------------------------------
// 用数学合成了一个"卡牌翻面"音效，不依赖任何素材文件
// 原理：一个频率下滑的正弦波，乘上指数衰减的包络
// 这跟你用 DAC + 定时器发一个短促提示音，是同一件事
// ---------------------------------------------------------------------
static Sound MakeClickSound(void) {
    int sr = 44100;                        // 采样率 44.1kHz
    int n = (int)(sr * 0.12f);             // 时长 0.12 秒
    short *buf = (short *)malloc(n * sizeof(short));

    for (int i = 0; i < n; i++) {
        float t = (float)i / sr;                       // 当前时刻（秒）
        float env = expf(-t * 30.0f);                  // 指数衰减包络，声音迅速消失
        float f = 900.0f - 600.0f * (t / 0.12f);       // 频率从 900Hz 滑到 300Hz，像"啪"一声
        buf[i] = (short)(sinf(2 * PI * f * t) * env * 12000);
    }

    Wave w = {0};
    w.frameCount = n;
    w.sampleRate = sr;
    w.sampleSize = 16;
    w.channels = 1;
    w.data = buf;

    Sound snd = LoadSoundFromWave(w);   // 转成声卡能直接播放的音频缓冲
    free(buf);                          // raylib 已经拷贝过了，原始缓冲可以释放
    return snd;
}

int main(int argc, char **argv) {
    int shotMode = (argc > 1 && strcmp(argv[1], "--shot") == 0);

    InitWindow(900, 560, "SHIFT - 音频与 UI");
    InitAudioDevice();                    // 打开音频设备，这行之后才有声音
    SetTargetFPS(60);

    int cps[1024];
    int n = CollectUIGlyphs(cps, 1024);       // 自动收集所有要显示的字符
    Font font = LoadFontEx("assets/font.ttf", 40, cps, n);
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);

    GuiSetFont(font);                        // 让 raygui 也用这套中文字体
    GuiSetStyle(DEFAULT, TEXT_SIZE, 20);     // raygui 默认字号只有 10，中文会糊，调大到 20

    Sound click = MakeClickSound();                 // 合成的音效
    Music bgm = LoadMusicStream("assets/bgm.wav");  // 流式加载背景音乐
    PlayMusicStream(bgm);                           // 开始播放

    float vol = 0.7f;                        // 音量，0.0 到 1.0
    bool muted = false;                      // 是否静音
    int frame = 0;

    while (!WindowShouldClose()) {           // 主循环
        UpdateMusicStream(bgm);              // 关键：音乐必须每帧喂数据，不调就断流
        SetMusicVolume(bgm, muted ? 0.0f : vol);

        if (GuiButton((Rectangle){ 40, 60, 190, 46 }, UI_TEXT[1])) PlaySound(click);
        if (GuiButton((Rectangle){ 40, 126, 190, 46 }, muted ? UI_TEXT[3] : UI_TEXT[2])) muted = !muted;

        const char *volText = TextFormat("%i%%", (int)(vol * 100));
        GuiSlider((Rectangle){ 40, 206, 280, 26 }, UI_TEXT[4], volText, &vol, 0.0f, 1.0f);

        BeginDrawing();
        ClearBackground((Color){ 16, 17, 21, 255 });

        DrawTextEx(font, UI_TEXT[0], (Vector2){ 40, 16 }, 30, 1, RAYWHITE);
        DrawTextEx(font, UI_TEXT[5], (Vector2){ 40, 270 }, 22, 1, (Color){ 150, 152, 160, 255 });
        DrawTextEx(font, UI_TEXT[6], (Vector2){ 40, 306 }, 22, 1, (Color){ 150, 152, 160, 255 });

        const char *devText = TextFormat("%s%s", UI_TEXT[7],
                                         IsAudioDeviceReady() ? UI_TEXT[8] : UI_TEXT[9]);
        DrawTextEx(font, devText, (Vector2){ 40, 380 }, 22, 1, (Color){ 200, 202, 210, 255 });

        const char *lenText = TextFormat("%s%.1f%s", UI_TEXT[10],
                                         GetMusicTimeLength(bgm), UI_TEXT[11]);
        DrawTextEx(font, lenText, (Vector2){ 40, 416 }, 22, 1, (Color){ 200, 202, 210, 255 });

        EndDrawing();

        if (shotMode && ++frame == 30) { TakeScreenshot("audio_shot.png"); break; }
    }

    UnloadMusicStream(bgm);
    UnloadSound(click);
    UnloadFont(font);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
