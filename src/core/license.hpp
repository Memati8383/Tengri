#pragma once
#include <string>

namespace license
{
    // Anahtarları etkinleştirme. license.cpp içindeki doğrulama yereldir ve yalnızca
    // bir demodur; kendi doğrulama sunucunu kurarsan Validate() gövdesini değiştirmen yeterli.
    enum class Status { Idle, Checking, Valid, Invalid };

    // Plan ve bitiş metin olarak değil veri olarak tutulur. İfadeyi burada üretmek,
    // anahtar doğrulanırken etkin olan dili metne döndürür ve daha sonra dil değişirse
    // ekranda eski ifade kalır. `lifetime` seçimi çizim anında yapılır.
    struct Info
    {
        std::string key;
        std::string user;
        std::string hwid;
        bool        lifetime = false;   // true: süresiz, false: `expires` geçerli
        std::string expires;            // yalnızca !lifetime için
    };

    void Activate(const std::string& key);
    void Logout();

    Status        Poll();               // arka plan doğrulamasının durumu
    const std::string& Error();
    const Info&        Current();

    // Anahtarı yalnızca ilk ve son birkaç karakteri görünür olacak biçimde maskeler.
    std::string Mask(const std::string& key);

    // "Beni hatırla" kalıcılığı: %APPDATA%\TENGRI\license.dat
    std::string LoadSaved();
    void Save(const std::string& key);
    void ForgetSaved();
}