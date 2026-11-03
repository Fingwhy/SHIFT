// ---------------------------------------------------------------------
// 独立的 Win32 辅助文件
// 这里只引用 windows.h，不引用 raylib.h，避免两者的名字冲突：
//   Rectangle / CloseWindow / ShowCursor 在两边都有定义
// 对外只暴露一个函数：DisableIME(void *raylibWindowHandle)
// ---------------------------------------------------------------------
#include <windows.h>
#include <imm.h>

static WNDPROC g_oldWndProc = NULL;

static LRESULT CALLBACK GameWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_INPUTLANGCHANGEREQUEST) return 0;   // 吞掉输入法切换请求
    return CallWindowProc(g_oldWndProc, hwnd, msg, wp, lp);
}

void DisableIME(void *windowHandle) {
    HWND hwnd = (HWND)windowHandle;
    if (!hwnd) return;

    ImmAssociateContext(hwnd, NULL);                   // 让窗口不绑定任何输入法

    g_oldWndProc = (WNDPROC)SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)GameWndProc);
}

// ---------------------------------------------------------------------
// 关掉系统的 DPI 缩放
// 否则在 150% 缩放下，framebuffer 会变成 1.5 倍，但渲染视口还是原尺寸，
// 结果就是画面只占窗口左上角，右边和下边全是黑的。
// 必须在 InitWindow 之前调用，晚了系统会拒绝。
// ---------------------------------------------------------------------
void DisableDpiScaling(void) {
    typedef BOOL (WINAPI *SetCtxFn)(HANDLE);
    HMODULE user32 = LoadLibraryA("user32.dll");
    if (!user32) return;

    SetCtxFn setCtx = (SetCtxFn)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
    if (setCtx) setCtx((HANDLE)-1);        // DPI_AWARENESS_CONTEXT_UNAWARE
}

