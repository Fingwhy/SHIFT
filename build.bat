@echo off
cd /d "%~dp0"

gcc shift_game.c ime_fix.c -o shift_game.exe ^
    -I vendor/raylib/include ^
    -L vendor/raylib/lib ^
    -lraylib -lopengl32 -lgdi32 -lwinmm -limm32 ^
    -static-libgcc -lm

if errorlevel 1 (
    echo.
    echo [BUILD FAILED] see errors above
    pause
    exit /b 1
)

echo [BUILD OK] launching...
shift_game.exe
