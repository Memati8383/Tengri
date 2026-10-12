#pragma once

// Yedek (.reg) dosyalarinin içeriğini denetleyen saf işlevler.
//
// Neden başlıkta ve backup.cpp'de değil: bu denetim, yükseltilmiş sürecin
// yönetici olarak içe aktaracağı dosyanın kapsamını belirliyor. Denetimin
// kendisi registry'ye ve üretici tabloya dokunmadan sınanabilsin diye burada
// duruyor — backup.cpp sadece hangi anahtarların beklendiğini bilir,
// karşılaştırmayı buraya verir.
//
// Tehdit modeli: yedek klasörü kullanıcının yazabildiği bir yerde duruyor, yani
// aynı kullanıcıda koşan herhangi bir program oraya kendi .reg dosyasını
// bırakabilir. Dosyaların kökenini kanıtlayan güvenilir bir kayıt yok (bizim
// tuttuğumuz bir liste de aynı program tarafından değiştirilebilirdi), bu yüzden
// etki alanı sabitleştiriliyor: ad, tabloda gerçek bir (kategori,sıra) çiftine
// karşılık gelmeli ve dosyanın İÇİNDEKİ her anahtar satırı o çiftin
// anahtarlarından birinin kendisi ya da ALT anahtarı olmalı. Böylece yabancı bir
// dosya en fazla uygulamanın zaten yedeklediği alt ağaca dokunabilir; IFEO,
// hizmet ya da Run gibi rastgele bir HKLM anahtarına yazamaz.
//
// "Alt anahtar" kuralı hoşgörü değil, zorunluluk: reg.exe bir anahtarı dışa
// aktarırken alt anahtarlarını da aynı dosyaya yazar. Ölçüldü — bu makinedeki 54
// yedek anahtarının 12'si alt anahtar satırı üretiyor (ör. Services\nvlddmkm),
// en geniş dışa aktarım HKLM\SYSTEM\CurrentControlSet\Control için 30 MB.
// Yalnızca tam anahtar satırını kabul eden bir denetim, bu meşru yedeklerin
// tamamını reddeder ve geri alma sessizce hiçbir şey yapmazdı.

#include <windows.h>

#include <cstring>
#include <string>
#include <vector>

namespace regtext
{
    // Doğrulama içeriği belleğe okur, bu yüzden bir tavan var. 64 MB, ölçülen en
    // büyük meşru dışa aktarımın (30 MB) iki katından fazla; altında keyfi bir
    // sayı değil, yukarıdaki ölçüm var.
    constexpr unsigned long long kMaxFileBytes = 64ull << 20;

    // Dışa aktarma dosyalarımızın geçerli adları "<cat>-<idx>.reg" ve aynı
    // ayarın birden fazla anahtarı olduğunda "<cat>-<idx>-<tekrar>.reg". Rakam
    // ve tire dışı hiçbir şey kabul edilmez; ad üzerinden yol zorlama da böylece
    // mümkün olmaz. Uzantı büyük/küçük harf duyarsız.
    //
    // <tekrar> yalnızca çakışmayı önleyen bir etikettir: (cat,idx) çiftinin Kaç
    // anahtarı varsa o kadar dosya olur ve hepsi aynı ada düşmesin diye sayılır.
    // Doğrulama için anlamı yok — içerik, çiftin TÜM anahtarlarına karşı
    // denetleniyor. Eski yedekler (etiketsiz ad) kabul edilmeye devam eder.
    inline bool ParseBackupName(const wchar_t* name, int* cat, int* idx)
    {
        if (!name || !cat || !idx) return false;
        const size_t len = ::wcslen(name);
        if (len <= 4) return false;
        if (_wcsicmp(name + len - 4, L".reg") != 0) return false;

        const std::wstring stem(name, len - 4);
        int nums[3] = { 0, 0, 0 };
        int groups = 0;
        size_t i = 0;
        for (;;)
        {
            int v = 0;
            bool any = false;
            while (i < stem.size() && stem[i] != L'-')
            {
                if (stem[i] < L'0' || stem[i] > L'9') return false;
                v = v * 10 + (stem[i] - L'0');
                if (v > 100000) return false;      // taşma da burada kesilir
                any = true;
                ++i;
            }
            if (!any) return false;                // boş grup: "-12", "3--1"
            if (groups >= 3) return false;         // en fazla üç sayı grubu
            nums[groups++] = v;
            if (i == stem.size()) break;
            ++i;                                   // ayraç tire
            if (i == stem.size()) return false;    // tire ile bitiyor
        }
        if (groups < 2) return false;              // "3" tek başına yedek adı değil

        *cat = nums[0]; *idx = nums[1];
        return true;
    }

    // reg.exe dışa aktarımları UTF-16LE (BOM'lu) ya da BOM'suz UTF-8/ANSI
    // olabilir; satırları inceleyebilmek için ikisi de geniş dizeye çevrilir.
    //
    // UTF-16'da dönüşüm yapılmıyor, baytlar olduğu gibi wchar_t'a alınıyor:
    // MultiByteToWideChar'ın desteklediği kod sayfaları CP_ACP/CP_OEMCP/CP_UTF7/
    // CP_UTF8'dir; 1200 bu listede yok (testte ölçüldü — dönüşüm başarısız
    // oluyordu). BOM'suz UTF-16'yi ayırt etmenin güvenilir bir yolu yok, o
    // yüzden BOM'suz veri UTF-8 sayılıyor ve geçersiz bayt çifti reddediliyor
    // (MB_ERR_INVALID_CHARS): uydurma kodlamalı bir dosyayı "sorun yok" diye
    // geçmek istemeyiz.
    inline bool ReadRegText(const std::wstring& path, std::wstring* out)
    {
        if (out) out->clear();
        if (path.empty() || !out) return false;

        HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;

        LARGE_INTEGER sz = {};
        if (!::GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 ||
            static_cast<unsigned long long>(sz.QuadPart) > kMaxFileBytes)
        {
            ::CloseHandle(h);
            return false;
        }

        std::vector<unsigned char> bytes(static_cast<size_t>(sz.QuadPart));
        DWORD done = 0;
        const bool read = ::ReadFile(h, bytes.data(),
                                     static_cast<DWORD>(bytes.size()), &done, nullptr) != FALSE
                          && done == bytes.size();
        ::CloseHandle(h);
        if (!read) return false;

        const bool utf16 = bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE;
        size_t skip = utf16 ? 2 : 0;
        if (!utf16 && bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
            skip = 3;                       // UTF-8 BOM: gövde zaten UTF-8
        if (bytes.size() == skip) return false;

        const unsigned char* body = bytes.data() + skip;
        const size_t bodyLen = bytes.size() - skip;

        if (utf16)
        {
            if (bodyLen < sizeof(wchar_t) || bodyLen % sizeof(wchar_t) != 0) return false;
            const size_t n = bodyLen / sizeof(wchar_t);
            out->assign(n, L'\0');
            ::memcpy(&(*out)[0], body, bodyLen);
            return true;
        }

        const char* src = reinterpret_cast<const char*>(body);
        const int need = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                               src, static_cast<int>(bodyLen), nullptr, 0);
        if (need <= 0) return false;        // boş ya da geçersiz kodlamalı gövde

        out->assign(static_cast<size_t>(need), L'\0');
        if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                  src, static_cast<int>(bodyLen), &(*out)[0], need) != need)
        {
            out->clear();
            return false;
        }
        return true;
    }

    // key, root'un kendisi ya da ALT anahtarı mı? Registry yolları büyük/küçük
    // harf duyarsız, burada da öyle. "Harold" gibi bir kardeş anahtarın "Harry"
    // ile önek çakışmasına izin verilmez: sınırı ya dizinin sonu ya da bisl
    // ayırıcısı belirler.
    inline bool KeyInSubtree(const std::wstring& key, const std::wstring& root)
    {
        if (key.size() < root.size()) return false;
        if (_wcsnicmp(key.c_str(), root.c_str(), root.size()) != 0) return false;
        if (key.size() == root.size()) return true;
        return key[root.size()] == L'\\';
    }

    // Bir .reg dosyasında anahtar satırı köşeli ayraçlar arasında durur:
    //   [HKEY_LOCAL_MACHINE\Software\...]   anahtar
    //   [-HKEY_LOCAL_MACHINE\...]           anahtarı SİL
    //   "BirDeger"=dword:00000001           anahtar satırı değil
    //   hex(..),\  + sonraki satır          değerin devamı, boşlukla başlar
    //
    // Dosya ancak ve ancak TÜM ayraç satırları beklenen anahtarların (ya da alt
    // anahtarlarının) içindeyse kabul edilir. Silme biçimi reddedilir: dışa
    // aktarım onu hiç üretmez ve geri alma sırasında bir anahtarı yok etmek,
    // değerleri geri getirmek değildir.
    inline bool ContentMatchesKeys(const std::wstring& text,
                                   const std::vector<std::wstring>& expected)
    {
        const wchar_t* const kHeader = L"Windows Registry Editor Version 5.00";
        if (expected.empty()) return false;

        size_t pos = 0;
        bool headerSeen = false;
        bool anyKey = false;

        while (pos < text.size())
        {
            size_t e = text.find(L'\n', pos);
            const bool last = (e == std::wstring::npos);
            if (last) e = text.size();

            std::wstring line = text.substr(pos, e - pos);
            pos = last ? text.size() : e + 1;
            if (!line.empty() && line.back() == L'\r') line.pop_back();
            if (line.empty()) continue;

            if (!headerSeen)
            {
                if (_wcsicmp(line.c_str(), kHeader) != 0) return false;
                headerSeen = true;
                continue;
            }

            if (line.front() != L'[') continue;      // değer satırı ya da devamı
            if (line.back() != L']') return false;   // bozuk ayraç

            std::wstring key = line.substr(1, line.size() - 2);
            if (key.empty()) return false;
            if (key.front() == L'-') return false;   // anahtarı silme biçimi

            bool ok = false;
            for (const std::wstring& root : expected)
                if (KeyInSubtree(key, root)) { ok = true; break; }
            if (!ok) return false;
            anyKey = true;
        }

        return headerSeen && anyKey;
    }
}
