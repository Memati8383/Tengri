#pragma once
#include <windows.h>

namespace app
{
    void Init(HWND hwnd, float corner_radius);
    void Frame();       // build the whole UI for this frame
    bool WantsQuit();

    // Kapatma isteği (çarpı düğmesi ve Alt+F4 / WM_CLOSE). Tepsi açıksa pencereyi
    // gizler, kapalıysa çıkışa döner; hangisi olduğunu uygulama kendisi karar
    // verir, çağıran yalnızca niyeti iletir.
    void RequestClose();
    void Shutdown();    // join background scan/clean workers before teardown

    bool TrayEnabled(); // read by main.cpp, which owns the tray icon

    // Sistem tepsisi sağ tık menüsü. Menü hiçbir durumu kendi içinde tutmaz:
    // işaretli düğmelerin değerini, erişilebilir sayfaları ve seçilen komutun
    // ne yapacağını buradan sorar. Ayarlar ekranıyla tepsi menüsü aynı
    // anahtarları gördüğü için ikisi zamanla birbirinden kopamaz.
    enum TrayCmd
    {
        // Yalnızca tray.cpp'de karşılık bulur: pencereyi geri getirme yolu
        // tepsinin kendi out-parametresi üzerinden yürür.
        TrayCmdOpen     = 1,
        TrayCmdUndo     = 2,
        TrayCmdToasts   = 3,
        TrayCmdQuiet    = 4,
        TrayCmdStartup  = 5,
        TrayCmdSettings = 6,
        // Yalnızca tray.cpp'de karşılık bulur: kabuğa WM_QUIT göndermek
        // uygulama tarafındaki kapanış sırasından ayrı bir yol açardı.
        TrayCmdExit     = 7,
        // "Sayfaya git" alt menüsü bu tabanın üstüne sekme indeksini ekler;
        // indeksler arayüzdeki sekme sırasıyla aynıdır.
        TrayCmdGoto     = 0x100
    };

    bool        TrayCanUndo();          // geri yüklenebilir bir yedek var mı
    bool        TrayNotificationsOn();  // Windows bildirimleri açık mı
    bool        TrayQuietOn();          // yalnızca hatalar modu
    bool        TrayStartupOn();        // HKCU Run girdisi var mı
    int         TrayPageCount();
    const char* TrayPageLabel(int index);            // UTF-8
    void        HandleTrayCommand(int command);

    // Windows bildiriminden gelen eylem. command notify::Command değerlerinden
    // biridir, target ise o sayfanın gideceği sekme (TargetNone ise yok).
    // main.cpp tek örnek kilidi üzerinden iletir.
    //
    // Geri al burada doğrudan çalışmaz: kullanıcıdan onay ister, onay ancak
    // uygulama penceresinde verildikten sonra yazma yapılır.
    void HandleNotifyCommand(int command, int target);
}
