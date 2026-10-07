#include "restore.hpp"

#include <windows.h>
#include <cstdint>
#include <cwchar>

// SRSetRestorePointW; srclient.dll içinde yaşar ve statik olarak her SDK'da
// bulunmaz. Yapı tanımları SRRestorePtAPI.h içindedir ama o başlık Windows SDK'nın
// bazı varyantlarında yoktur; bu yüzden ihtiyacımız olan iki yapı burada elle
// bildirilmiştir. Değerler Microsoft'un belgelerinden gelir ve değişmez.

namespace
{
    constexpr DWORD kBeginSystemChange = 100;  // RESTOREPOINTINFO.dwEventType
    constexpr DWORD kEndSystemChange   = 101;  // (kullanılmıyor — tek atış)
    constexpr DWORD kModifySettings    = 12;   // RESTOREPOINTINFO.dwRestorePtType

    // Microsoft'un tanımladığı yapılar; başlık yoksa elle duplike edilir.
    // Alanların sırası ve boyutu kritik: yanlış tanımlanırsa SRSetRestorePointW
    // sessizce ERROR_INVALID_DATA döner.
    #pragma pack(push, 8)
    struct RestorePointInfoW
    {
        DWORD   dwEventType;
        DWORD   dwRestorePtType;
        INT64   llSequenceNumber;
        WCHAR   szDescription[256];
    };

    struct StateMgrStatus
    {
        DWORD   nStatus;
        INT64   llSequenceNumber;
    };
    #pragma pack(pop)

    using SRSetRestorePointWFn = BOOL(WINAPI*)(RestorePointInfoW*, StateMgrStatus*);

    unsigned long long g_lastCreatedTick = 0;

    bool IsCurrentProcessElevated()
    {
        HANDLE tok = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
        TOKEN_ELEVATION e = {};
        DWORD sz = sizeof(e);
        const bool ok = GetTokenInformation(tok, TokenElevation, &e, sz, &sz) != 0;
        CloseHandle(tok);
        return ok && e.TokenIsElevated != 0;
    }
}

namespace restore
{
    const wchar_t* ResultText(Result r)
    {
        switch (r)
        {
        case Result::Created:            return L"restore point created";
        case Result::Disabled:           return L"system protection disabled";
        case Result::NotElevated:        return L"administrator rights required";
        case Result::ServiceUnavailable: return L"srclient.dll not available";
        case Result::Throttled:          return L"skipped: throttled by Windows (24h rule)";
        case Result::Failed:             return L"failed";
        }
        return L"unknown";
    }

    bool IsProtectionEnabled()
    {
        // C: için Sistem Koruma durumu, SPP (System Protection) kayıt
        // defterinde "RPSessionInterval" > 0 kontrolüyle anlaşılabilir.
        // Ancak bu kayıt yalnızca Pro/Server SKU'larda doğru dolu;
        // Home SKU'da varsayılan olarak 0 olabilir ve yine de nokta
        // oluşturulabilir. En güvenilir yöntem: SysRestore anahtarının
        // kendisi var mı ve DisableSR = 0 mu.
        HKEY k;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                          L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\SystemRestore",
                          0, KEY_READ, &k) != ERROR_SUCCESS)
            return true; // anahtar yoksa engel yok sayalım; CreatePoint zaten
                         // gerçek durumu döner.

        DWORD disabled = 0, sz = sizeof(disabled);
        RegQueryValueExW(k, L"DisableSR", nullptr, nullptr, (LPBYTE)&disabled, &sz);
        RegCloseKey(k);
        return disabled == 0;
    }

    unsigned long long LastCreatedTick() { return g_lastCreatedTick; }

    Result CreatePoint(const wchar_t* description)
    {
        if (!description || !*description) description = L"TENGRI";

        if (!IsCurrentProcessElevated())
            return Result::NotElevated;

        if (!IsProtectionEnabled())
            return Result::Disabled;

        // srclient.dll bazı Server SKU'larda ve Windows PE görüntülerinde yoktur.
        // Statik bağlama yerine dinamik yükleme kullanılıyor: olmasa bile uygulama
        // başlangıcı etkilenmez.
        HMODULE dll = LoadLibraryW(L"srclient.dll");
        if (!dll) return Result::ServiceUnavailable;

        auto fn = (SRSetRestorePointWFn)GetProcAddress(dll, "SRSetRestorePointW");
        if (!fn)
        {
            FreeLibrary(dll);
            return Result::ServiceUnavailable;
        }

        RestorePointInfoW info = {};
        info.dwEventType     = kBeginSystemChange;
        info.dwRestorePtType = kModifySettings;
        info.llSequenceNumber = 0;
        // wcsncpy_s hedef dizi tam boyutta geçersiz sonlandırıcı yazar; sığmayan
        // açıklamalar güvenli şekilde kesilir.
        wcsncpy_s(info.szDescription, _TRUNCATE, description, _TRUNCATE);

        StateMgrStatus status = {};
        const BOOL ok = fn(&info, &status);

        FreeLibrary(dll);

        if (ok && status.nStatus == ERROR_SUCCESS)
        {
            g_lastCreatedTick = GetTickCount64();
            return Result::Created;
        }

        // ERROR_SERVICE_DISABLED (1058) ve SR'ye özel 0xC000020B
        // (STATUS_RESTORE_DISABLED): sistem koruma kapatılmış.
        if (status.nStatus == ERROR_SERVICE_DISABLED)
            return Result::Disabled;

        // Windows 10 1803+ varsayılan olarak 24 saat kuralı uygular; yakın bir
        // nokta varsa yenisi oluşturulmaz. SRSetRestorePointW bu durumda
        // llSequenceNumber=0 ile başarı döndürür ama fiili bir kayıt yoktur.
        // Dönüş biraz muğlak; nStatus hatası yoksa throttle olarak kabul ediyoruz
        // ve kullanıcıya "atlandı" bildiriyoruz.
        if (ok && status.llSequenceNumber == 0)
            return Result::Throttled;

        return Result::Failed;
    }
}
