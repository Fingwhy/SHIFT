# SHIFT

> GitHub Game Off 2026 参赛作品 ｜ 主题：SHIFT

**你和敌人同时吟唱，谁先按出来谁占便宜，但按得越早越弱。**

变强的是玩家的手和判断，不是角色的数值。

---

## 玩法

双方同时吟唱。敌人先起手，只露出一枚**起手键**——你看得到它属于哪一类，看不到具体是哪一招。你随时可以插入自己的吟唱，两边同时推进，**先完成者出手**。

吟唱途中你随时可以按 Enter **提前释放**，但放出来的是哪一级，取决于你已经输入了几个指令：

| 已输入 | 可释放 | 威力 |
|---|---|---|
| 1 步 | 基础法术 | 低 |
| 2~3 步 | 复合法术 | 中 |
| 4~5 步 | 高级法术 | 高 |

于是每一回合都是一次胆小鬼博弈：急着出手，双方都弱，拼谁先打中；憋大招，可能被对方打断，也可能一击定胜负。

**Shift** 一次按键同时做两件事——延长吟唱窗口，和瞬间位移躲开飞来的法术。**每场只有 3 次**，用完条子变灰。

没有法力、没有冷却、没有技能树。所有法术开局就能用，新手的限制不是"没学会"，是"来不及搓"。

一局三场（小怪 1 招 → 精英 2 招 → BOSS 3 招），死了从第一场重来，界面记录「最远抵达：第 N 场」。

## 操作

| 键 | 作用 |
|---|---|
| `A` `S` `D` | 输入三系元素（火 / 冰 / 雷） |
| `空格` | 起手吟唱 |
| `Shift` | 延长窗口 + 闪避位移（每场 3 次） |
| `Enter` | 提前释放当前已输入的招式 |
| `F1` / `F2` | 切换生命模式 / 位移层级（设计待定项的体感对照） |
| `R` | 重开一局 |

## 运行

无需安装依赖，raylib 5.5 已静态链接在 `vendor/`。

```bat
build_demo.bat
```

或手工：

```bat
gcc design_demo.c ime_fix.c -o design_demo.exe ^
    -I vendor/raylib/include -L vendor/raylib/lib ^
    -lraylib -lopengl32 -lgdi32 -lwinmm -limm32 -static-libgcc -lm
```

需要 MinGW-w64 gcc 与 OpenGL 驱动。运行时 `assets/` 目录必须和 exe 同级（字体与背景音乐在这里）。

## 仓库结构

```
design_demo.c        ← 当前主线：按 DESIGN.md v2 实现的设计演示
ime_fix.c            ← Win32 补丁：屏蔽输入法（否则 Shift 被吃掉）+ 关闭 DPI 缩放
DESIGN.md            ← 设计文档 v2（机制、参数、待定问题）
STATUS.md            ← 设计 vs 实现的一致性审计

assets/font.ttf      界面字体（Noto Sans SC 子集，OFL）
assets/bgm.wav       背景音乐
tools/make_font.py   重新生成字体子集
vendor/raylib/       raylib 5.5（zlib 许可）静态库 + 头文件

# 以下为实现过程中单独验证某个技术点的实验文件，保留作记录
fx3d_demo.c          3D 特效：菲涅尔 shader + 加法混合伪造 bloom
duel3d_demo.c        2.5D 构图：真 3D 场景 + 2D billboard 立绘 + 屏幕空间 HUD
cast_demo.c          吟唱窗口 + 按键事件提取（含 ±3 帧同时按合并）
combo_demo.c         环形缓冲区 + Shift 修饰强化
command_demo.c       2 秒指令窗口 + 事件序列结算
motion_demo.c        搓招子序列匹配
card3d_demo.c        3D mesh 卡牌 + 透视相机
audio_demo.c         raygui + 程序合成音效 + 流式 BGM
main.c               窗口 / 中文字体烘焙 / 输入基线
shift_game.c         第一版可玩循环（裁决层有已知缺陷，见 STATUS.md）
```

所有 demo 都支持 `--shot` 参数：摆好一帧画面自动截图后退出，用于无人值守验证排版。

## 素材许可

- **raylib 5.5** — zlib 许可，可自由分发。
- **assets/font.ttf** — Noto Sans SC 的子集（SIL Open Font License 1.1）。只保留了界面实际用到的 900 余字，从 17 MB 压到 290 KB。改动界面文字后若出现方块，运行 `python tools/make_font.py` 重新生成。
- **assets/bgm.wav** — 背景音乐。
- 其余图形与音效均由代码程序化生成，无外部素材。
