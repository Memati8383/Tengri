#pragma once
#include <windows.h>

namespace app
{
    void Init(HWND hwnd, float corner_radius);
    void Frame();       // build the whole UI for this frame
    bool WantsQuit();
    void Shutdown();    // join background scan/clean workers before teardown

    bool TrayEnabled(); // read by main.cpp, which owns the tray icon
}
