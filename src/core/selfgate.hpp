#pragma once

// "Bu surec bizim exemizin kendisi mi?" sorusunun tek cevabi.
//
// WM_COPYDATA ayni oturumdaki her surecin acik kanali: UIPI yalnizca daha Dusuk
// bileskelik (integrity) seviyesindeki gonderenleri engeller, ayni seviyedekileri
// engellemez. Bildirim komutlarini (sekme degistir, pencereyi one getir, yazma isini
// tekrarla, indirme baslat) yalnizca kendimizden kabul etmek icin gonderen
// surecin kimligi gerekiyor.
//
// Uc kanit bir arada aranir: surecin tam exe yolu bizinkile birebir ayni, kullanici
// SID'i bizinkine esit, ve ikisi de basariyla okunabilmis. Yol tek basina yetmez:
// exe klasoru ayni kullanici tarafindan yazilabilir, dolayisiyla ayni yolda
// hazirlanmis bir dosya yol karsilastirmasini gecerdigi gibi kullanici karsilastirmasi
// da farkli bir hesabta calisan kopyayi ayirt eder.
//
// Bu denet ayricalik siniri DEGIL: saldirgan zaten bizim exemizi kendi yetkimizle
// calistirabilir. Sagladigi, rastgele bir surecin arayuzumuze komut firlatamamasi ve
// gereksiz UAC kapisi acilammasi. Ayricalik siniri olarak UAC (guvenli masaustu)
// duruyor.
#include <windows.h>

#include <vector>

namespace selfgate
{
    // Token'daki kullanici SID'i. GetTokenInformation TokenUser icin tampon
    // boyutunu once soruyor; SID, döndürülen tamponun icinde kaliyor, o yuzden
    // tampon cagiran yasamali.
    inline PSID TokenUserSid(HANDLE token, std::vector<BYTE>& keep)
    {
        DWORD need = 0;
        if (!::GetTokenInformation(token, TokenUser, nullptr, 0, &need) && need == 0)
            return nullptr;
        keep.resize(need);
        if (!::GetTokenInformation(token, TokenUser, keep.data(), need, &need))
            return nullptr;
        return reinterpret_cast<PTOKEN_USER>(keep.data())->User.Sid;
    }

    inline bool SameUserAsCurrent(HANDLE proc)
    {
        HANDLE theirs = nullptr, ours = nullptr;
        if (!::OpenProcessToken(proc, TOKEN_QUERY, &theirs)) return false;
        if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &ours))
        {
            ::CloseHandle(theirs);
            return false;
        }
        std::vector<BYTE> a, b;
        const PSID sa = TokenUserSid(theirs, a);
        const PSID sb = TokenUserSid(ours, b);
        const bool same = sa && sb && ::EqualSid(sa, sb) != FALSE;
        ::CloseHandle(theirs);
        ::CloseHandle(ours);
        return same;
    }

    inline bool SameImageAsCurrent(HANDLE proc)
    {
        wchar_t other[MAX_PATH * 4] = {};
        DWORD   n = static_cast<DWORD>(sizeof(other) / sizeof(other[0]) - 1);
        if (!::QueryFullProcessImageNameW(proc, 0, other, &n)) return false;

        wchar_t self[MAX_PATH * 4] = {};
        const DWORD k = ::GetModuleFileNameW(nullptr, self,
                            static_cast<DWORD>(sizeof(self) / sizeof(self[0]) - 1));
        return k > 0 && k < static_cast<DWORD>(sizeof(self) / sizeof(self[0]) - 1)
               && _wcsicmp(self, other) == 0;
    }

    // pid 0 reddedilir: gonderen belli degil. Kendi surecimiz her zaman kabul,
    // cunku kendimiz icin ayri kanit aramanin sonucu degismez.
    inline bool IsSelf(DWORD pid)
    {
        if (pid == 0) return false;
        if (pid == ::GetCurrentProcessId()) return true;

        HANDLE proc = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!proc) return false;
        const bool ours = SameImageAsCurrent(proc) && SameUserAsCurrent(proc);
        ::CloseHandle(proc);
        return ours;
    }
}
