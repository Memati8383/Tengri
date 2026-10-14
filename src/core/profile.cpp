#include "profile.hpp"
#include "lang.hpp"
#include "regtext.hpp"
#include "tweakinfo.hpp"

#include <cwctype>
#include <string>
#include <vector>

namespace
{
    // Karsilastirma anahtari: anahtar adi buyuk/kucuk harf duyarsiz (registry
    // de oyle), deger adi duyarlidir. Yedek kapisi da ayni kurali kullanir;
    // ikisi farkli davranirsa dosya arayuzde "bilinen" gorunur, geri yuklemede
    // reddedilir.
    std::wstring PairId(const std::wstring& key, const std::wstring& name)
    {
        std::wstring k = key;
        for (wchar_t& c : k) c = ::towlower(c);
        // Ayraç olarak \1: iki alan yan yana yazıldığında sınırın nerede
        // bittiği okuyucuda belirsiz kalmasın.
        return k + L'\1' + name;
    }
}

namespace profile
{
    bool Analyze(const std::wstring& file, Analysis* out)
    {
        if (!out) return false;
        *out = Analysis{};

        std::wstring text;
        if (!regtext::ReadRegText(file, &text)) return false;

        // Birinci gecis: guvenilir mod, yani yalnizca bicim denetimi. Bizim
        // bicimimizde olmayan bir dosya iceriğe bakilmadan reddedilir; reg.exe
        // disa aktarimi ayni basligi tasiyor ama alt agac ve silme satirlari
        // iceriyor, bu cozumleme onlari tanimiyor.
        std::vector<regtext::Record> recs;
        if (!regtext::ParseRegText(text, nullptr, &recs)) return false;
        out->parsed = true;

        // Tablosuz derlemede (test paketleri) hicbir cift bilinmiyor: burada
        // basari sansli bir dosyayi "gecerli profil" diye gecistirir.
        if (tweakinfo::EntryCount() <= 0) return false;

        // Ikinci gecis: ayni metin, tum tablonun izin listesiyle. Red => dosya
        // ya baska bir aractan geldi ya da ici elle degistirildi.
        std::vector<regtext::Allowed> allowed;
        tweakinfo::AllAllowed(&allowed);
        std::vector<regtext::Record> gated;
        if (!regtext::ParseRegText(text, &allowed, &gated))
        {
            out->foreign = true;
            return false;
        }

        std::vector<std::wstring> present;
        for (const regtext::Record& r : gated)
            present.push_back(PairId(r.key, r.name));

        // Bir ayar yalnizca DOKUNDUGU TUM ciftler dosyada varsa acilir. Kismi
        // eslesme acsaydi, ayarin yazmadigi bir deger eski haliyle kalir ve
        // "profil uygulandi" sozu yalan olurdu.
        constexpr int kTweakCatCount = 10;
        constexpr int kTweakRows     = 8;
        for (int c = 0; c < kTweakCatCount; ++c)
        {
            for (int i = 0; i < kTweakRows; ++i)
            {
                int have = 0, total = 0;
                tweakinfo::Item it = {};
                for (int k = 0; tweakinfo::Get(c, i, k, &it); ++k)
                {
                    const std::wstring full = regtext::ExpandKey(it.key);
                    for (const wchar_t* const* v = it.valueNames; v && *v; ++v)
                    {
                        ++total;
                        const std::wstring id = PairId(full, *v);
                        for (const std::wstring& p : present)
                            if (p == id) { ++have; break; }
                    }
                }
                // total == 0: guc plani / hizmet gibi komut araciyla degisen ayar.
                if (total == 0) continue;
                if (have == total)      out->on.push_back({ c, i });
                else if (have > 0)      out->partial.push_back({ c, i });
            }
        }
        return true;
    }
}
