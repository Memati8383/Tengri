#pragma once
#include <windows.h>

namespace app
{
    void Init(HWND hwnd, float corner_radius);
    void Frame();       // build the whole UI for this frame
    bool WantsQuit();
    void Shutdown();    // join background scan/clean workers before teardown

    bool TrayEnabled(); // read by main.cpp, which owns the tray icon

    // Windows bildiriminden gelen eylem. command notify::Command değerlerinden
    // biridir, target ise o sayfanın gideceği sekme (TargetNone ise yok).
    // main.cpp tek örnek kilidi üzerinden iletir.
    //
    // Geri al burada doğrudan çalışmaz: kullanıcıdan onay ister, onay ancak
    // uygulama penceresinde verildikten sonra yazma yapılır.
    void HandleNotifyCommand(int command, int target);
}
