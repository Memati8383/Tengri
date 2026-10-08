// Oyun profili gövdeleri için gidiş-dönüş sondası.
//
// Ne yapar, katman katman:
//
//   1. AYARLA.  regpack::Body(cat, i, true)  -> regpack::Import
//   2. OKU.      Her gövdedeki [HKEY...] ve "ad"=satırlarını ayrıştırıp gerçekten
//                var olduklarını ve doğru değeri taşıdıklarını RegQueryValueExW
//                ile SORAR. reg.exe'nin "başarılı" demesi yeterli değil: yanlış
//                anahtara yazılmış bir gövde de başarıyla içe aktarılır.
//   3. GERİ AL.  regpack::Body(cat, i, false) -> regpack::Import
//   4. DOĞRULA. Geri alma gövdesinin yaptığını doğrula: değer SİLİNMİŞ olmalı
//                (=-), ya da belgelenmiş varsayılana DÖNMÜŞ olmalı.
//   5. TIKANMA. Sonda geri alınan değerlerin YEDEĞİ geri yüklenir. Kullanıcının
//                sisteminde bu anahtarlardan biri daha önce özel ayarlanmışsa,
//                3. adım onu belgelenmiş varsayılana düşürürdü; bu yüzden
//                yedek alınır ve en sonda aynen geri konur. Sondan çıktıktan
//                sonra sistem, başlamadan önceki hâline birebir döner.
//
// Neden ayrı bir araç ve neden build.bat'ın parçası değil:
//   - HKLM'ye yazar, yani YÖNETİCİ yetkisi ister; build.bat yetkisiz çalışır.
//   - Gerçek sistem registry'sine dokunur. test_pure.exe bilinçli olarak
//     hiçbir şey yazmaz ("registry yazmaz, dosya silmez, ağa çıkmaz"); bu araç
//     o sözü bilerek ihlal eder, o yüzden opt-in ve elle çağrılır.
//
// Oyunun kurulu olması gerekmez: IFEO anahtarları exe'yi ADIYLA tanır.
// Windows, karşılığı diskte bulunamayan bir anahtarı da okur, yalnızca hiçbir
// süreçle eşleştirmez. Bu yüzden Valorant/ Fortnite/ CS2 kurulu olmadan
// yazma-doğrulama-silme zinciri baştan sona denenebilir.
//
// Kullanım:  probe_game_tweaks.exe            (sadece oyun profilleri, 7..9)
//            probe_game_tweaks.exe --all      (4..9, mevcut tüm regpack'ler)
//            probe_game_tweaks.exe --cat 8    (tek kategori)
//            probe_game_tweaks.exe --keep     (geri alma YAPMA, incelemek için)
//
// Çıkış kodu: 0 = hepsi geçti, 1 = en az bir kontrol kaldı.

#include "../src/core/regpack.hpp"
#include "../src/core/tweaks.hpp"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{
    int g_fail = 0;
    int g_pass = 0;
    bool g_keep = false;   // --keep: geri alma yapma, sistemi olduğu gibi bırak

    void Check(bool ok, const char* what)
    {
        if (ok) ++g_pass;
        else { ++g_fail; std::printf("    BASARISIZ: %s\n", what); }
    }

    void CheckEq(unsigned long long got, unsigned long long want, const char* what)
    {
        if (got == want) ++g_pass;
        else { ++g_fail; std::printf("    BASARISIZ: %s (beklenen %llu, gelen %llu)\n", what, want, got); }
    }

    // Bir .reg gövdesinin tek satırı. Anahtar ya da değer.
    struct Entry
    {
        std::wstring path;    // tam anahtar yolu, HKEY_ ile başlar
        std::wstring name;    // değer adı
        std::wstring value;   // ham "değer" kısmı (tür öneki dahil), boş ise silme
    };

    std::wstring Widen(const char* s)
    {
        if (!s || !*s) return std::wstring();
        const int n = ::MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
        if (n <= 1) return std::wstring();
        std::wstring w;
        w.resize(n - 1);
        ::MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
        return w;
    }

    // Gövdeyi satırlara bölüp Entry listesine çevirir. .reg sözdiziminin bu
    // projedeki gövdelerde kullandığı alt kümesini kapsar: [HKEY...] başlıkları,
    // "ad"=dword:xxxxxxxx, "ad"="metin", ve silme için "ad"=-.
    std::vector<Entry> ParseBody(const char* body)
    {
        std::vector<Entry> out;
        if (!body) return out;

        const char* p = body;
        std::wstring cur;
        while (*p)
        {
            const char* eol = std::strchr(p, '\n');
            std::string line(p, eol ? (size_t)(eol - p) : std::strlen(p));

            // Baştaki \r'yi ve indentasyonu at.
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
                line.pop_back();

            if (line.size() > 2 && line[0] == '[' && line[line.size() - 1] == ']')
            {
                cur = Widen(line.substr(1, line.size() - 2).c_str());
            }
            else if (line.size() > 2 && line[0] == '"' && !cur.empty())
            {
                const size_t close = line.find('"', 1);
                if (close != std::string::npos && close + 1 < line.size() && line[close + 1] == '=')
                {
                    Entry en;
                    en.path  = cur;
                    en.name  = Widen(line.substr(1, close - 1).c_str());
                    en.value = Widen(line.substr(close + 2).c_str());
                    out.push_back(en);
                }
            }

            if (!eol) break;
            p = eol + 1;
        }
        return out;
    }

    // HKEY_CURRENT_USER -> HKEY_LOCAL_MACHINE adreslemesi.
    HKEY RootOf(const std::wstring& path)
    {
        if (path.rfind(L"HKEY_LOCAL_MACHINE", 0) == 0) return HKEY_LOCAL_MACHINE;
        if (path.rfind(L"HKEY_CURRENT_USER", 0)  == 0) return HKEY_CURRENT_USER;
        return nullptr;
    }

    std::wstring SubKeyOf(const std::wstring& path)
    {
        const size_t i = path.find(L'\\');
        return i == std::wstring::npos ? std::wstring() : path.substr(i + 1);
    }

    // Bir değeri okur. dword:0x00000001 / "metin" / (silinmişse) ayrıştırılır.
    // dword: biçimi için "0x" öneki yoktur: sekiz onaltılık hane doğrudan gelir.
    bool ReadEntry(const Entry& e, std::wstring& out, unsigned long& outDword, bool& isDword)
    {
        out.clear();
        outDword = 0;
        isDword  = false;

        const HKEY root = RootOf(e.path);
        const std::wstring sub = SubKeyOf(e.path);
        if (!root || sub.empty()) return false;

        HKEY k;
        if (::RegOpenKeyExW(root, sub.c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS) return false;

        DWORD type = 0, sz = 0;
        LONG r = ::RegQueryValueExW(k, e.name.c_str(), nullptr, &type, nullptr, &sz);
        if (r != ERROR_SUCCESS) { ::RegCloseKey(k); return false; }

        if (type == REG_DWORD)
        {
            DWORD v = 0;
            sz = sizeof(v);
            r = ::RegQueryValueExW(k, e.name.c_str(), nullptr, &type, (LPBYTE)&v, &sz);
            ::RegCloseKey(k);
            if (r != ERROR_SUCCESS) return false;
            outDword = v;
            isDword  = true;
            return true;
        }

        std::vector<wchar_t> buf(sz / sizeof(wchar_t) + 1, 0);
        r = ::RegQueryValueExW(k, e.name.c_str(), nullptr, &type, (LPBYTE)buf.data(), &sz);
        ::RegCloseKey(k);
        if (r != ERROR_SUCCESS) return false;
        out.assign(buf.data());
        return true;
    }

    unsigned long ParseDword(const std::wstring& v)
    {
        if (v.rfind(L"dword:", 0) != 0) return 0;
        return ::wcstoul(v.c_str() + 6, nullptr, 16);
    }

    std::wstring StripQuotes(const std::wstring& v)
    {
        if (v.size() >= 2 && v.front() == L'"' && v.back() == L'"')
            return v.substr(1, v.size() - 2);
        return v;
    }

    // Geliş bölüm: yol -> (ad -> değer metni). Silinmiş değerler "yok" işaretlenir.
    typedef std::map<std::wstring, std::wstring> Snapshot;

    Snapshot Capture(const std::vector<Entry>& entries)
    {
        Snapshot s;
        for (const Entry& e : entries)
        {
            const std::wstring k = e.path + L"\\" + e.name;
            std::wstring sv;
            unsigned long dv = 0;
            bool isD = false;
            if (ReadEntry(e, sv, dv, isD))
            {
                wchar_t buf[64];
                if (isD) ::swprintf_s(buf, L"dword:%08lx", dv);
                else     ::wcscpy_s(buf, sv.c_str());
                s[k] = buf;
            }
            else
            {
                s[k] = L"<yok>";   // anahtar ya da değer yok
            }
        }
        return s;
    }

    // Yedeği geri yükler. <yok> işaretli olanlar silinir; kalanlar eski değerine
    // döner. Katı bir işlem değil: anahtar hiç yoksa RegDeleteValue başarısız olur
    // ve bu normaldir, o değer zaten yoktu.
    void Restore(const Snapshot& snap)
    {
        for (const auto& kv : snap)
        {
            const std::wstring path = kv.first.substr(0, kv.first.rfind(L'\\'));
            const std::wstring name = kv.first.substr(kv.first.rfind(L'\\') + 1);
            const HKEY root = RootOf(path);
            const std::wstring sub = SubKeyOf(path);
            if (!root || sub.empty()) continue;

            HKEY k;
            if (::RegOpenKeyExW(root, sub.c_str(), 0, KEY_WRITE, &k) != ERROR_SUCCESS) continue;

            if (kv.second == L"<yok>")
            {
                ::RegDeleteValueW(k, name.c_str());
            }
            else if (kv.second.rfind(L"dword:", 0) == 0)
            {
                const DWORD v = (DWORD)::wcstoul(kv.second.c_str() + 6, nullptr, 16);
                ::RegSetValueExW(k, name.c_str(), 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
            }
            else
            {
                const std::wstring s = StripQuotes(kv.second);
                const DWORD cb = (DWORD)((s.size() + 1) * sizeof(wchar_t));
                ::RegSetValueExW(k, name.c_str(), 0, REG_SZ, (const BYTE*)s.c_str(), cb);
            }
            ::RegCloseKey(k);
        }
    }

    const char* CatName(int c)
    {
        switch (c)
        {
        case 4: return "Oyunlar";
        case 5: return "FiveM";
        case 6: return "Gecikme";
        case 7: return "Valorant";
        case 8: return "CS2";
        case 9: return "Fortnite";
        }
        return "?";
    }

    void ProbeCategory(int cat)
    {
        std::printf("\n== %d (%s) ==\n", cat, CatName(cat));

        for (int i = 0; i < 8; ++i)
        {
            const char* on  = regpack::Body(cat, i, true);
            const char* off = regpack::Body(cat, i, false);

            std::vector<Entry> entsOn;
            for (const Entry& e : ParseBody(on)) entsOn.push_back(e);

            if (entsOn.empty())
            {
                Check(false, "acma govdesi ayristirilamadi");
                continue;
            }

            // Geri alma gövdesi, açma gövdesinin dokunduğu değerlerin TAMAMINI
            // ele almalı. Eksik bir geri alma, kullanıcı anahtarı kapattığında
            // sistemde yarım uygulanmış bir ayar bırakır.
            std::vector<Entry> entsOff;
            for (const Entry& e : ParseBody(off)) entsOff.push_back(e);

            std::map<std::wstring, int> offSeen;
            for (const Entry& e : entsOff) ++offSeen[e.path + L"\\" + e.name];

            int eksikGeriAlma = 0;
            for (const Entry& e : entsOn)
                if (!offSeen[e.path + L"\\" + e.name]) ++eksikGeriAlma;
            CheckEq(eksikGeriAlma, 0, "her degerin geri alimi var");

            // --- 1. TIKANMA ---
            const Snapshot before = Capture(entsOn);

            // --- 2. AYARLA ---
            if (!regpack::Import(on))
            {
                Check(false, "regpack::Import(ac) basarisiz");
                continue;
            }
            Check(true, "regpack::Import(ac) tamam");

            // --- 3. OKU: gerçekten yazıldı mı ---
            int yanlis = 0, yazilmadi = 0;
            for (const Entry& e : entsOn)
            {
                std::wstring sv;
                unsigned long dv = 0;
                bool isD = false;
                if (!ReadEntry(e, sv, dv, isD)) { ++yazilmadi; continue; }

                const std::wstring want = e.value;
                bool ok = false;
                if (want.rfind(L"dword:", 0) == 0)
                    ok = isD && dv == ParseDword(want);
                else
                    ok = !isD && sv == StripQuotes(want);
                if (!ok) ++yanlis;
            }
            CheckEq(yazilmadi, 0, "her deger anahtarda var");
            CheckEq(yanlis, 0, "her deger beklenen degeri tasiyor");

            // --- 4. GERİ AL ---
            bool offOk = regpack::Import(off);
            Check(offOk, "regpack::Import(kapalı) tamam");

            // --- 5. GERİ ALMAYI DOĞRULA ---
            // Kapalı gövdenin yaptığı her şey sadece "değeri kaldır" ya da
            // "belgelenmiş varsayılana dön" olabilir; ikisini de kabul ediyoruz
            // ama hiçbir şey yapmadığını kabul etmiyoruz.
            int etkisiz = 0;
            for (const Entry& e : entsOff)
            {
                if (e.value == L"-") continue;   // silme: yokluğu ayrıca denetle
                std::wstring sv;
                unsigned long dv = 0;
                bool isD = false;
                const bool ok = ReadEntry(e, sv, dv, isD);
                bool dogru = false;
                if (e.value.rfind(L"dword:", 0) == 0)
                    dogru = ok && isD && dv == ParseDword(e.value);
                else
                    dogru = ok && !isD && sv == StripQuotes(e.value);
                if (!dogru) ++etkisiz;
            }
            CheckEq(etkisiz, 0, "kapali govde beklenen varsayilani yukledi");

            // Silme işaretleri gerçekten değeri kaldırmış olmalı.
            int silinmeyen = 0;
            for (const Entry& e : entsOff)
            {
                if (e.value != L"-") continue;
                std::wstring sv;
                unsigned long dv = 0;
                bool isD = false;
                if (ReadEntry(e, sv, dv, isD)) ++silinmeyen;
            }
            CheckEq(silinmeyen, 0, "kapali govde degeri sildi");

            // --- 6. TIKANMA: kullanıcının halini aynen geri koy ---
            if (!g_keep)
            {
                Restore(before);
                std::printf("    (%d deger geri yuklendi)\n", (int)before.size());
            }
            else
            {
                std::printf("    (--keep: ayar ACIK birakildi, cat %d satir %d)\n", cat, i);
            }
        }
    }

    bool IsElevated()
    {
        HANDLE t = nullptr;
        if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &t)) return false;
        TOKEN_ELEVATION e = {};
        DWORD need = 0;
        const BOOL ok = ::GetTokenInformation(t, TokenElevation, &e, sizeof(e), &need);
        ::CloseHandle(t);
        return ok && e.TokenIsElevated != 0;
    }
}

int wmain(int argc, wchar_t** argv)
{
    int  from = 7, to = 9;
    bool all = false;
    int  one = -1;

    for (int i = 1; i < argc; ++i)
    {
        if (!::wcscmp(argv[i], L"--all"))   { all = true; from = 4; }
        else if (!::wcscmp(argv[i], L"--keep")) g_keep = true;
        else if (!::wcscmp(argv[i], L"--cat") && i + 1 < argc) one = ::_wtoi(argv[++i]);
    }
    if (one >= 0) { from = to = one; }

    std::printf("Oyun profili gidis-donus sondasi\n");
    std::printf("Kapsam: %d..%d   Mod: %s\n", from, to, g_keep ? "KEEP (geri alma yok)" : "geri alinir");

    if (!IsElevated())
    {
        std::printf("\n[!] Yonetici yetkisi gerekli: HKLM'ye yaziliyor.\n");
        std::printf("    PowerShell: Start-Process -Verb RunAs -FilePath .\\probe_game_tweaks.exe\n");
        return 1;
    }

    for (int c = from; c <= to; ++c) ProbeCategory(c);

    std::printf("\n---------------------------------------------\n");
    std::printf("%d gecti, %d kaldi\n", g_pass, g_fail);
    if (g_keep) std::printf("UYARI: --keep kullanildi; bazi ayarlar ACIK birakildi.\n");
    return g_fail ? 1 : 0;
}
