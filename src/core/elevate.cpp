#include "elevate.hpp"
#include "tweaks.hpp"
#include "regpack.hpp"
#include "ram.hpp"
#include "network.hpp"
#include "backup.hpp"

#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cwchar>

namespace elevate
{
    namespace
    {
        // Tam yol olmadan yeniden başlatma komutu kurulamıyor; uygulama PATH'ten
        // çağrılmış olabilir, bu yüzden modül yolundan okunur.
        std::wstring g_exePath;

        // Komut satırı uzunluğu sınırlı. Bekleyen iş 56 anahtar + 3 seçim, onaltılık
        // kodlamayla 56 baytın katı; 8 kategori x 8 satır = 64 bayt + 3 bayt başlık.
        // 512 karakter rahatça yeter, üstüne kısa bir bayrak da sığar.
        constexpr int kMaxLoad = 512;

        void AppendHex(std::wstring& s, unsigned v)
        {
            static const wchar_t* d = L"0123456789ABCDEF";
            s += d[(v >> 4) & 0xF];
            s += d[v & 0xF];
        }

        bool ReadHex(char c, unsigned& out)
        {
            if (c >= '0' && c <= '9')      { out = (unsigned)(c - '0');      return true; }
            if (c >= 'a' && c <= 'f')      { out = (unsigned)(c - 'a' + 10); return true; }
            if (c >= 'A' && c <= 'F')      { out = (unsigned)(c - 'A' + 10); return true; }
            return false;
        }
    }

    void CacheExecutablePath()
    {
        if (!g_exePath.empty()) return;
        wchar_t buf[MAX_PATH] = {};
        const DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
        if (n > 0 && n < MAX_PATH) g_exePath.assign(buf, n);
    }

    bool IsElevated()
    {
        HANDLE token = nullptr;
        if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token))
            return false;
        TOKEN_ELEVATION el = {};
        DWORD needed = 0;
        const BOOL ok = ::GetTokenInformation(token, TokenElevation, &el, sizeof(el), &needed);
        ::CloseHandle(token);
        return ok && el.TokenIsElevated != 0;
    }

    // Bekleyen işi onaltılık bayt dizisine kodla:
    //   [ram] [dns] [startup] [kategoriSayisi] ( [kategori] [durumMaskesi] )*
    // FF, "bu iş yok" demektir. Her kategorinin 8 satırı bir bit maskesinde sığar.
    std::wstring EncodeCommandLine(const Pending& p)
    {
        // Her tekil seçim iki bayt yer: once "var/yok", sonra değer. Tek bayt
        // yeterli görünüyor ama 0xFF hem "yok" hem olası bir değer olurdu; iki
        // bayt bu belirsizliği tamamen kaldırır.
        auto putOpt = [](std::wstring& s, int v) {
            if (v < 0) { AppendHex(s, 0); AppendHex(s, 0); }
            else       { AppendHex(s, 1); AppendHex(s, (unsigned)(v & 0xFF)); }
        };

        std::wstring body;
        putOpt(body, p.ramPreset);
        putOpt(body, p.dnsProvider);
        putOpt(body, p.startupToggle);

        // Geri alma klasörü adı: önce uzunluk, sonra adın ASCII baytları.
        // Uzunluk konulmazsa çözücü nerede biteceğini bilemez.
        const std::string& r = p.restoreFrom;
        AppendHex(body, (unsigned)(r.size() > 255 ? 255 : r.size()));
        for (size_t i = 0; i < r.size() && i < 255; ++i)
        {
            // AppendHex zaten iki haneyi (yüksek ve alçak) birlikte yazıyor; ayrı ayrı
            // çağırmak karakteri iki bayta böler ve çözücü yanlış hizalıyor.
            AppendHex(body, (unsigned)(unsigned char)r[i]);
        }

        unsigned n = 0;
        for (int c = 0; c < 8; ++c) if (p.anyChange[c]) ++n;
        AppendHex(body, n);
        for (int c = 0; c < 8; ++c)
        {
            if (!p.anyChange[c]) continue;
            AppendHex(body, (unsigned)c);
            unsigned mask = 0;
            for (int i = 0; i < 8; ++i) if (p.categories[c][i]) mask |= 1u << i;
            AppendHex(body, mask);
        }

        return std::wstring(L"--apply=") + body;
    }

    bool DecodeCommandLine(int argc, wchar_t** argv, Pending& out)
    {
        std::wstring load;
        for (int i = 1; i < argc; ++i)
        {
            const wchar_t* a = argv[i];
            const wchar_t tag[] = L"--apply=";
            if (std::wcsncmp(a, tag, 8) != 0) continue;
            load.assign(a + 8);
            break;
        }
        if (load.empty()) return false;
        if ((int)load.size() > kMaxLoad * 2) return false;

        // Hex bayt dizisine çevir.
        std::vector<unsigned> bytes;
        for (size_t i = 0; i + 1 < load.size(); i += 2)
        {
            unsigned hi, lo;
            if (!ReadHex((char)load[i], hi) || !ReadHex((char)load[i + 1], lo)) return false;
            bytes.push_back((hi << 4) | lo);
        }
        size_t p = 0;
        if (bytes.size() < 8) return false;   // 3 seçim x 2 bayt + geri alma uzunluğu + kategori sayısı

        // Her seçim: varlık baytı (0 = yok, 1 = var), sonra değer.
        auto getOpt = [&](int& dst) -> bool {
            if (p + 1 >= bytes.size()) return false;
            const unsigned var = bytes[p++];
            const unsigned val = bytes[p++];
            dst = var ? (int)val : -1;
            return true;
        };
        if (!getOpt(out.ramPreset))     return false;
        if (!getOpt(out.dnsProvider))   return false;
        if (!getOpt(out.startupToggle)) return false;

        // Geri alma klasörü adı: uzunluk baytı, sonra ASCII baytlar.
        out.restoreFrom.clear();
        if (p >= bytes.size()) return false;
        const unsigned rlen = bytes[p++];
        for (unsigned i = 0; i < rlen; ++i)
        {
            if (p >= bytes.size()) return false;
            // Yalnizca basit ASCII kabul edilir. Komut satirindan gelen veri
            // kullanici girdisi sayilmaz; yine de sinir disi bir bayt reddedilir.
            const unsigned ch = bytes[p++];
            if (ch < 0x20 || ch > 0x7E) return false;
            out.restoreFrom.push_back((char)ch);
        }

        if (p >= bytes.size()) return true;
        const unsigned n = bytes[p++];
        for (unsigned k = 0; k < n; ++k)
        {
            if (p + 1 >= bytes.size()) return false;
            const unsigned c = bytes[p++];
            const unsigned mask = bytes[p++];
            if (c >= 8) return false;
            out.anyChange[c] = true;
            for (int i = 0; i < 8; ++i) if (mask & (1u << i)) out.categories[c][i] = true;
        }
        return true;
    }

    // Bekleyen işleri bu süreçte uygular (süreç yükseltilmiş olduğunda çağrılır).
    static bool ApplyHere(const Pending& p)
    {
        bool any = false;

        // Geri alma isteniyorsa ayarlar uygulanmaz; yedeklenen eski degerler geri
        // ice aktarilir. Yolu burada kuruyoruz: komut satirinda yalnizca klasor adi
        // tasindi, kullanici girdisi bu yola giremez.
        if (!p.restoreFrom.empty())
        {
            const std::wstring root = backup::RootDirectory();
            if (root.empty()) return false;

            // Klasor adi ASCII; darlaltma acikca yaziliyor.
            std::wstring ad;
            for (char ch : p.restoreFrom)
                ad.push_back(ch < 128 ? (wchar_t)ch : L'?');

            return backup::Restore(root + L"\\" + ad);
        }

        for (int c = 0; c < 8; ++c)
        {
            if (!p.anyChange[c]) continue;
            any = true;
            bool states[8] = {}, mask[8] = {};
            for (int i = 0; i < 8; ++i) { states[i] = p.categories[c][i]; mask[i] = true; }

            if (tweaks::IsRegPack(c))
            {
                tweaks::ApplyCategory(c, states, mask, 8);
            }
            else
            {
                for (int i = 0; i < 8; ++i)
                    if (mask[i]) tweaks::Apply(c, i, states[i]);
            }
        }

        // Paylaşımlı registry değerleri: açık olan kardeş son yeniden yazılır.
        for (int c = 0; c < 8; ++c)
            for (int i = 0; i < 8; ++i)
                if (p.categories[c][i] && tweaks::IsShared(c, i))
                    tweaks::Apply(c, i, true);

        if (p.ramPreset >= 0) { ram::Apply(p.ramPreset); any = true; }
        if (p.dnsProvider >= 0) { network::SetDns(p.dnsProvider); any = true; }
        if (p.startupToggle >= 0)
        {
            CacheExecutablePath();
            network::SetRunAtStartup(g_exePath.c_str(), p.startupToggle != 0);
            any = true;
        }
        return any;
    }

    Result ApplyOrDelegate(const Pending& p)
    {
        // Zaten yükseltilmişse işi burada yap; arayüz bildirimini kendi akışında gösterir.
        if (IsElevated())
            return ApplyHere(p) ? Result::Applied : Result::Failed;

        CacheExecutablePath();
        if (g_exePath.empty()) return Result::Failed;

        const std::wstring args = EncodeCommandLine(p);

        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.fMask        = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
        sei.lpVerb       = L"runas";
        sei.lpFile       = g_exePath.c_str();
        sei.lpParameters = args.c_str();
        sei.nShow        = SW_HIDE;

        // "runas" UAC sorusunu gösterir; reddedilirse ERROR_CANCELLED döner.
        if (!::ShellExecuteExW(&sei))
            return Result::Failed;

        if (sei.hProcess)
        {
            ::CloseHandle(sei.hProcess);
            // Yeni yükseltilmiş süreç işi yapıp çıkacak; buradaki arayüz kapanmalı ki
            // aynı iş iki pencerede de görünmesin.
            ::ExitProcess(0);
        }
        return Result::IsInherited;
    }
}