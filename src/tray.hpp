#pragma once
#include <windows.h>

// Kabuk tray ikonu. Sahip olduğu tek şey kendi NOTIFYICONDATA yapısı ve özel mesaj
// kanalıdır; pencerenin sahibi main.cpp'de kalır. Geri getirme isteği bir bayrak olarak
// döner, Çıkış ise WM_QUIT gönderir; böylece süreç nasıl sonlandırılırsa sonlandırılsın
// kapanış, pencerenin kendi kapat düğmesiyle aynı yoldan geçer.
namespace tray
{
    // İkon tıklandığında pencereye gönderilir. Ana döngü, örtülmüş veya gizliyken kare
    // atladığı için bir sonraki tam kareyi beklemek yerine hemen uyanabilsin.
    extern UINT kRestoreMsg;

    // instance zorunludur: marka simgesi exe'nin KAYNAĞINDA (RT_GROUP_ICON 101)
    // durur ve LoadImage yalnızca doğru modülle adresleyebilir. hInstance NULL
    // verildiğinde kimlik bir SİSTEM kaynağı sayılır; 101 numaralı bir sistem
    // simgesi olmadığı için çağrı çöp döner ve tepside sarı bir ünlem üçgeni
    // belirir.
    bool Init(HWND hwnd, HINSTANCE instance);

    // Kabuk ikonu reddederse false döner (Explorer yok, politika kısıtlı). Çağıran, geri
    // getirilemeyecek bir pencereyi gizlemek yerine sıradan küçültme davranışını korur.
    bool IsAvailable();
    void SetEnabled(bool enabled);

    // Explorer yeniden başladığında ikonu yok eder; her karede NIM_ADD göndermek
    // gereksiz yük olurdu, bu yüzden ekleme yalnızca saniyede bir yeniden denenir.
    void Tick();
    void Shutdown();

    // WndProc sonuçları buradan geçirilir; mesaj tüketildiyse true döner ve
    // `restore` geri getirme isteğini bildirir.
    bool HandleMessage(UINT msg, LPARAM lparam, bool* restore);
}