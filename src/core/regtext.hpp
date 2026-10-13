#pragma once

// Yedek (.reg) dosyalarının içeriğini okuyup yazan saf işlevler.
//
// Neden başlıkta ve backup.cpp'de değil: bu denetim, yükseltilmiş sürecin
// yönetici olarak registry'ye yazacağı kayıtların kapsamını belirliyor. Denetimin
// kendisi registry'ye ve üretici tabloya dokunmadan sınanabilsin diye burada
// duruyor — backup.cpp yalnızca hangi (anahtar,değer) çiftlerinin beklendiğini
// bilir, çözümlemeyi buraya verir.
//
// Tehdit modeli: yedek klasörü kullanıcının yazabildiği bir yerde duruyor, yani
// aynı kullanıcıda koşan herhangi bir program oraya kendi dosyasını
// bırakabilir. Dosyaların kökenini kanıtlayan güvenilir bir kayıt yok (bizim
// tuttuğumuz bir liste de aynı program tarafından değiştirilebilirdi), bu yüzden
// etki alanı sabitleştiriliyor: geri yüklenecek HER kayıt, adının karşılık
// geldiği (kategori,sıra) çiftinin üretilmiş tablosunda bir (anahtar,değer)
// çifti olarak bulunmalı. Kapsam değerin ADINA kadar daraltılıyor; anahtar
// adında daraltmak yetmiyordu, çünkü yükseltme anahtarda değil değer adında
// duruyor (HKLM\SYSTEM\CurrentControlSet\Control meşru bir yedek anahtarı,
// ancak içindeki Session Manager\BootExecute SYSTEM kodekoşumu demekti).
//
// Bu biçimi yalnızca kendi dışa aktarımımız üretiyor: reg.exe devreden çıktı,
// yani bir anahtarın alt anahtarlarını topluca dosyaya yazan hiçbir yol kalmadı.
// Alt ağaç hoşgörüsü de bu yüzden kaldırıldı; artık tam anahtar eşleşmesi
// isteniyor ve hoşgörü meşru bir yedeği bozmadan daraltılabiliyor.

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace regtext
{
    // Doğrulama içeriği belleğe okur, bu yüzden bir tavan var. Değer düzeyinde
    // yedek bir avuç satırdır: ölçülen en büyük dışa aktarım birkaç kilobayt.
    // 4 MB, bu biçimin doğal boyutunun çok üstünde ama belleği sınırsız
    // ayırmaktan koruyacak kadar küçük.
    constexpr unsigned long long kMaxFileBytes = 4ull << 20;

    // Tek bir registry değerinin dışa aktarılabilecek en büyük boyu. Ayarların
    // yazdığı değerler dword/kısa dize; bundan büyük bir değer ancak üçüncü bir
    // programın oraya koyduğu bir şey olur ve geri alınacak "eski ayar" sayılmaz.
    constexpr size_t kMaxValueBytes = 64u << 10;

    // Geri yüklenebilir tipler. Ayarların dokunduğu anahtalarda görülenler
    // bunlar; REG_SECURE_NOTIFICATION(9), REG_FULL_RESOURCE_DESCRIPTOR(15) ve
    // REG_LINK(12) gibi tipler hiçbir yedeğin içinde üretilemez ve reddedilir.
    inline bool TypeRestorable(DWORD t)
    {
        switch (t)
        {
        case REG_NONE: case REG_SZ: case REG_EXPAND_SZ: case REG_BINARY:
        case REG_DWORD: case REG_DWORD_BIG_ENDIAN: case REG_MULTI_SZ:
        case REG_RESOURCE_LIST: case REG_RESOURCE_REQUIREMENTS_LIST:
        case REG_QWORD:
            return true;
        }
        return false;
    }

    // Üretilmiş tablodaki tek bir izin: "<anahtar> altında <değer> adı".
    struct Allowed { const wchar_t* key; const wchar_t* name; };

    // Çözümlenmiş tek bir kayıt. remove=true ise "değeri sil" demek; bu biçim
    // yalnızca derleme zamanı gövdelerinde (regpack.cpp) var, dışa aktarım hiç
    // üretmez çünkü yedeklenen değer ya vardır ya hiç yoktur.
    struct Record
    {
        std::wstring key;                       // HKEY_LOCAL_MACHINE\... tam ad
        std::wstring name;                      // değer adı; boş = varsayılan değer
        DWORD        type = 0;
        std::vector<unsigned char> data;
        bool         remove = false;
    };

    // Dışa aktarma dosyalarımızın geçerli adları "<cat>-<idx>.reg" ve eski
    // sürümlerin "<cat>-<idx>-<tekrar>.reg" biçimi. Rakam ve tire dışı hiçbir
    // şey kabul edilmez; ad üzerinden yol zorlama da böylece mümkün olmaz.
    // Uzantı büyük/küçük harf duyarsız.
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

    // "HKLM\..." kısaltmasını dosyada yazan tam ada çevirir. Üretilmiş tablo
    // kısaltmayı yazıyor (kaynak kodda öyle görünüyor), dosya ise reg.exe
    // biçiminde tam adı taşımak zorunda.
    inline std::wstring ExpandKey(const wchar_t* abbrev)
    {
        std::wstring p(abbrev ? abbrev : L"");
        if (p.rfind(L"HKCU\\", 0) == 0)       p.replace(0, 4, L"HKEY_CURRENT_USER");
        else if (p.rfind(L"HKLM\\", 0) == 0)  p.replace(0, 4, L"HKEY_LOCAL_MACHINE");
        return p;
    }

    // Tam anahtar adını RegSetValueExW'nin istediği (kök, geri kalan) ikilisine
    // ayırır. Yalnızca iki kök kabul edilir; HKEY_USERS, "HKEY_CLASSES_ROOT" ya
    // da kökün kendisi ("HKEY_LOCAL_MACHINE" tek başına) reddedilir.
    inline bool SplitRoot(const std::wstring& key, HKEY* root, std::wstring* sub)
    {
        static const wchar_t kHkcu[] = L"HKEY_CURRENT_USER\\";       // 18 karakter
        static const wchar_t kHklm[] = L"HKEY_LOCAL_MACHINE\\";      // 19 karakter
        if (!root || !sub) return false;
        const size_t cu = sizeof(kHkcu) / sizeof(wchar_t) - 1;
        const size_t lm = sizeof(kHklm) / sizeof(wchar_t) - 1;
        if (key.size() > cu && _wcsnicmp(key.c_str(), kHkcu, cu) == 0)
        { *root = HKEY_CURRENT_USER;   *sub = key.substr(cu); return true; }
        if (key.size() > lm && _wcsnicmp(key.c_str(), kHklm, lm) == 0)
        { *root = HKEY_LOCAL_MACHINE;  *sub = key.substr(lm); return true; }
        return false;
    }

    namespace detail
    {
        inline int HexNibble(wchar_t c)
        {
            if (c >= L'0' && c <= L'9') return c - L'0';
            if (c >= L'a' && c <= L'f') return c - L'a' + 10;
            if (c >= L'A' && c <= L'F') return c - L'A' + 10;
            return -1;
        }

        // "aa,bb,cc" kümesini baytlara çevirir. Virgülle ayrılmış her küme bir
        // ya da iki onaltılık hanedir; boş küme, harf hatası ve üç hane reddedilir.
        inline bool ParseBytes(const std::wstring& s, std::vector<unsigned char>* out)
        {
            out->clear();
            size_t i = 0;
            bool anyTok = false;
            while (i <= s.size())
            {
                size_t j = i;
                while (j < s.size() && s[j] != L',') ++j;
                std::wstring tok = s.substr(i, j - i);
                // Boşluk ve satır içi ayraçlar toleransla atılır; başka bir
                // karakter tanım hatasıdır.
                std::wstring clean;
                for (wchar_t c : tok)
                {
                    if (c == L' ' || c == L'\t') continue;
                    clean.push_back(c);
                }
                if (!clean.empty())
                {
                    if (clean.size() > 2) return false;
                    int v = 0;
                    for (wchar_t c : clean)
                    {
                        const int n = HexNibble(c);
                        if (n < 0) return false;
                        v = v * 16 + n;
                    }
                    out->push_back(static_cast<unsigned char>(v));
                    anyTok = true;
                }
                if (j >= s.size()) break;
                i = j + 1;                          // virgülü atla
            }
            return anyTok;                          // hiç bayt yoksa geçersiz
        }

        // `from` konumundaki tırnaklı diziyi çözer; kapanış tırnağından SONRAKI
        // konumu yazar. Kaçışlar: \n \t \" \\. Kapanış tırnak yoksa false.
        inline bool UnquoteAt(const std::wstring& s, size_t from, std::wstring* out, size_t* next)
        {
            if (out) out->clear();
            if (from >= s.size() || s[from] != L'"') return false;
            std::wstring acc;
            for (size_t i = from + 1; i < s.size(); ++i)
            {
                if (s[i] == L'"')
                {
                    if (out) *out = acc;
                    if (next) *next = i + 1;
                    return true;
                }
                if (s[i] == L'\\')
                {
                    if (i + 1 >= s.size()) return false;
                    ++i;
                    switch (s[i])
                    {
                    case L'n':  acc.push_back(L'\n'); break;
                    case L't':  acc.push_back(L'\t'); break;
                    case L'"':  acc.push_back(L'"');  break;
                    case L'\\': acc.push_back(L'\\'); break;
                    default: return false;           // bilinmeyen kaçış
                    }
                    continue;
                }
                acc.push_back(s[i]);
            }
            return false;                             // kapanış tırnağı yok
        }
    }

    // .reg metnini kayıtlara çözer.
    //
    //   allowed == nullptr -> güvenilir mod: derleme zamanı gövdeleri (regpack).
    //       Orada metnin kaynağı zaten EXE'nin içinde, kapsamı denetlemenin
    //       anlamı yok; yalnızca biçim denetleniyor.
    //   allowed != nullptr -> disa aktarım modu: HER kaydın (anahtar,değer)
    //       çifti listede bulunmak zorunda; biri bile yoksa tüm dosya reddedilir.
    //       Anahtar adı büyük/küçük harf duyarsız (registry de öyle), değer adı
    //       duyarlüdür.
    //
    // Kabul edilen satır biçimleri (üretici de yalnızca bunları yazıyor):
    //   [HKEY_LOCAL_MACHINE\...\Control]     anahtar bölümü
    //   "Ad"=dword:00000001                  REG_DWORD
    //   "Ad"=hex(7):61,00,00,00,\            tip numaralı bayt kümesi (devam satırı olur)
    //   "Ad"=hex:01,02                       REG_NONE
    //   "Ad"="metin"                         REG_SZ
    //   "Ad"=-                               değeri sil (yalnızca güvenilir mod)
    //   @=...                                varsayılan değer (adı boş)
    //
    // Reddedilenler: başlık satırı yoksa, bölüm dışı değer satırıysa, anahtar
    // kökü HKCU/HKLM değilse, [-HKEY...] silme biçimiyle karşılaşırsa, tip
    // TypeRestorable dışındaysa, tırnak kaçışı bozuksa.
    namespace detail
    {
        inline bool ParseInto(const std::wstring& text,
                              const std::vector<Allowed>* allowed,
                              std::vector<Record>* out)
        {
            out->clear();

        const wchar_t* const kHeader = L"Windows Registry Editor Version 5.00";

        // Satırları önce devam zincirleriyle birleştir: sonu '\' ile biten satır
        // bir sonraki satırla birleştirilir (hex dizileri uzun olabilir).
        std::vector<std::wstring> lines;
        {
            size_t pos = 0;
            while (pos <= text.size())
            {
                size_t e = text.find(L'\n', pos);
                const bool last = (e == std::wstring::npos);
                std::wstring line = text.substr(pos, last ? std::wstring::npos : e - pos);
                pos = last ? text.size() + 1 : e + 1;
                if (!line.empty() && line.back() == L'\r') line.pop_back();
                if (last && line.empty()) break;
                lines.push_back(line);
            }
        }
        std::vector<std::wstring> joined;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            std::wstring cur = lines[i];
            while (!cur.empty() && cur.back() == L'\\' && i + 1 < lines.size())
            {
                cur.pop_back();
                std::wstring next = lines[++i];
                size_t s = 0;
                while (s < next.size() && (next[s] == L' ' || next[s] == L'\t')) ++s;
                cur += next.substr(s);
            }
            if (!cur.empty()) joined.push_back(cur);
        }

        std::wstring section;
        bool headerSeen = false;
        bool any = false;

        for (const std::wstring& line : joined)
        {
            if (!headerSeen)
            {
                if (_wcsicmp(line.c_str(), kHeader) != 0) return false;
                headerSeen = true;
                continue;
            }

            const wchar_t head = line.front();

            if (head == L'[')
            {
                if (line.back() != L']') return false;
                section = line.substr(1, line.size() - 2);
                if (section.empty() || section.front() == L'-') return false;  // silme biçimi
                HKEY probe = nullptr; std::wstring sub;
                if (!SplitRoot(section, &probe, &sub)) return false;
                continue;
            }

            // Değer satırı: "@=..." ya da "<tırnaklı ad>=<biçim>".
            std::wstring name;
            size_t rhsStart = 0;
            if (head == L'@')
            {
                if (line.size() < 2 || line[1] != L'=') return false;
                rhsStart = 2;
            }
            else if (head == L'"')
            {
                size_t next = 0;
                if (!detail::UnquoteAt(line, 0, &name, &next)) return false;
                if (next >= line.size() || line[next] != L'=') return false;
                rhsStart = next + 1;
            }
            else return false;                        // tanınmayan satır

            if (section.empty()) return false;        // bölüm dışı değer satırı
            const std::wstring rhs = line.substr(rhsStart);
            if (rhs.empty()) return false;

            Record rec;
            rec.key  = section;
            rec.name = name;

            if (rhs == L"-")
            {
                if (allowed) return false;            // yedek hiçbir zaman silme üretmez
                rec.remove = true;
                out->push_back(rec);
                any = true;
                continue;
            }

            if (rhs.size() >= 7 && _wcsnicmp(rhs.c_str(), L"dword:", 6) == 0)
            {
                const std::wstring hx = rhs.substr(6);
                if (hx.size() != 8) return false;
                unsigned v = 0;
                for (wchar_t c : hx)
                {
                    const int n = detail::HexNibble(c);
                    if (n < 0) return false;
                    v = v * 16 + static_cast<unsigned>(n);
                }
                rec.type = REG_DWORD;
                rec.data.assign(4, 0);
                for (int i = 0; i < 4; ++i) rec.data[i] = static_cast<unsigned char>((v >> (8 * i)) & 0xFF);
            }
            else if (rhs[0] == L'"')
            {
                std::wstring s;
                size_t next = 0;
                if (!detail::UnquoteAt(rhs, 0, &s, &next)) return false;
                if (next != rhs.size()) return false;          // tırnaktan sonra artık var
                if ((s.size() + 1) * sizeof(wchar_t) > kMaxValueBytes) return false;
                rec.type = REG_SZ;
                rec.data.assign((s.size() + 1) * sizeof(wchar_t), 0);
                if (!s.empty())
                    ::memcpy(rec.data.data(), s.c_str(), s.size() * sizeof(wchar_t));
            }
            else if (_wcsnicmp(rhs.c_str(), L"hex", 3) == 0)
            {
                DWORD type = REG_NONE;
                size_t colon = 3;
                if (rhs.size() > 4 && rhs[3] == L'(')
                {
                    const size_t close = rhs.find(L')', 4);
                    if (close == std::wstring::npos || close == 4) return false;
                    unsigned t = 0;
                    for (size_t i = 4; i < close; ++i)
                    {
                        const int n = detail::HexNibble(rhs[i]);
                        if (n < 0) return false;
                        t = t * 16 + static_cast<unsigned>(n);
                    }
                    type = t;
                    colon = close + 1;
                }
                if (colon >= rhs.size() || rhs[colon] != L':') return false;

                // Sifir uzunluklu bir deger meşru: disa aktarim onu "hex(N):"
                // olarak (bos kumeyle) yaziyor ve okuyucu da geri alinabilir
                // olmali. Virgul iceren bos kumeler ("hex:aa,,bb") ParseBytes
                // tarafindan reddedilmeye devam ediyor.
                const std::wstring set = rhs.substr(colon + 1);
                bool blank = true;
                for (wchar_t c : set)
                    if (c != L' ' && c != L'\t') { blank = false; break; }
                if (!blank && !detail::ParseBytes(set, &rec.data)) return false;
                if (!TypeRestorable(type)) return false;
                if (rec.data.size() > kMaxValueBytes) return false;
                rec.type = type;
            }
            else return false;

            if (allowed)
            {
                bool found = false;
                for (const Allowed& a : *allowed)
                {
                    const std::wstring k = ExpandKey(a.key);
                    if (_wcsicmp(k.c_str(), rec.key.c_str()) == 0 && rec.name == a.name)
                    { found = true; break; }
                }
                if (!found) return false;
            }

            out->push_back(rec);
            any = true;
        }

        return headerSeen && any;
        }
    }

    // Disa aktarim geri yuklendiginde kismi gecer bir dosya kabul edilemez:
    // ParseInto bir red aninda geri donse bile o ana kadarki kayitlari birakirdi.
    // Cagiranin "false ise hicbir sey yazilmadi" sozu verebilmesi icin kismi
    // sonuc burada siliniyor.
    inline bool ParseRegText(const std::wstring& text,
                             const std::vector<Allowed>* allowed,
                             std::vector<Record>* out)
    {
        if (!out) return false;
        std::vector<Record> recs;
        if (!detail::ParseInto(text, allowed, &recs)) { out->clear(); return false; }
        out->swap(recs);
        return true;
    }

    // Canlı bir değeri tek satıra serileştirir. Ad tırnak taşıyorsa yazmayız:
    // üretilmiş tabloda böyle bir ad yok, olsaydı da satırı bozulmuş olurdu.
    inline bool SerializeValue(const wchar_t* name, DWORD type,
                               const unsigned char* data, DWORD len,
                               std::wstring* out)
    {
        if (!name || !out) return false;
        if (!data && len) return false;
        if (len > kMaxValueBytes) return false;
        if (!TypeRestorable(type)) return false;
        if (wcschr(name, L'"')) return false;

        std::wstring s = L"\"";
        s += name;
        s += L"\"=";

        if (type == REG_DWORD && len == 4)
        {
            unsigned v = 0;
            for (int i = 3; i >= 0; --i) v = (v << 8) | data[i];
            wchar_t buf[16];
            ::swprintf_s(buf, L"dword:%08x", v);
            s += buf;
        }
        else
        {
            wchar_t buf[16];
            if (type == REG_NONE) s += L"hex:";
            else { ::swprintf_s(buf, L"hex(%x):", type); s += buf; }
            for (DWORD i = 0; i < len; ++i)
            {
                ::swprintf_s(buf, L"%02x", data[i]);
                s += buf;
                if (i + 1 < len) s += L',';
            }
        }
        s += L"\r\n";
        *out += s;
        return true;
    }
}
