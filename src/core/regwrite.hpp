#pragma once

// regtext.hpp'in çözdüğü kayıtları registry'ye yazan tek yer.
//
// Neden ayrı başlık: çözümleme saf ve sınanabilir kalsın diye. Testler
// ParseRegText'i dosya üzerinde çalıştırır, Apply'i hiç çağırmaz — çağırılsa
// gerçek registry'ye dokunurdu. Yazma işinin tek bir yerde durması, "bu
// uygulamada registry'ye kim yazıyor" sorusunun tek bir cevabı olması demek.
//
// reg.exe `import` çağrısı buradan kaldırıldı. İki sebebi vardı:
//   1) Doğrulanan dosya ile içe aktarılan dosya aynı yolun iki ayrı açılımıydı;
//      aralarında aynı kullanıcının içeriği değiştirmesi mümkündü. Artık
//      çözülen baytlar doğrudan yazılıyor, dosya bir daha açılmıyor.
//   2) reg.exe'nin allowlist kavramı yok; kapsamı yalnızca bizim denetimimiz
//      belirliyordu.

#include "regtext.hpp"

#include <windows.h>

#include <vector>

namespace regwrite
{
    struct Result
    {
        int written = 0;    // değer yazıldı
        int removed = 0;    // değer silindi (yalnızca derleme zamanı gövdeleri)
        int absent = 0;     // silinecek değer zaten yoktu: hedef durum, hata değil
        int failed  = 0;    // anahtar açılamadı ya da yazma reddedildi
    };

    inline Result Apply(const std::vector<regtext::Record>& recs)
    {
        Result r;
        for (const regtext::Record& rec : recs)
        {
            HKEY root = nullptr;
            std::wstring sub;
            if (!regtext::SplitRoot(rec.key, &root, &sub)) { ++r.failed; continue; }

            HKEY k = nullptr;
            // KEY_SET_VALUE yeter: ne okuma ne silme istemiyoruz. Anahtar yoksa
            // oluşturulur — reg.exe import'un davranışı da buydu, ayarların
            // birçoğu kendi alt anahtarını ilk uygulamada oluşturuyor.
            if (::RegCreateKeyExW(root, sub.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                                  KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS)
            {
                ++r.failed;
                continue;
            }

            if (rec.remove)
            {
                const LONG code = ::RegDeleteValueW(k, rec.name.c_str());
                if (code == ERROR_SUCCESS)            ++r.removed;
                else if (code == ERROR_FILE_NOT_FOUND) ++r.absent;
                else                                   ++r.failed;
            }
            else
            {
                const LONG code = ::RegSetValueExW(k, rec.name.c_str(), 0, rec.type,
                                                   rec.data.empty() ? nullptr : rec.data.data(),
                                                   static_cast<DWORD>(rec.data.size()));
                if (code == ERROR_SUCCESS) ++r.written;
                else                       ++r.failed;
            }
            ::RegCloseKey(k);
        }
        return r;
    }
}
