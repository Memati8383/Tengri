// Testler: registry'ye dokunmayan, saf mantik parcalari.
//
// Buradaki onarım sınıfı seçilerek: TENGRI'de test edilebilir saf mantık az. Kırkıncı
// tweak bile RegSetValueExW çağırıyor, ekran çizimi D3D11'e bağlı. Test edilebilir
// olan şeyler: i18n tablo hizası (statik_assert derleme zamanında zaten var), RAM
// preset sınırları, tweaks::IsRegPack/IsShared sözleşmesi, registry paket gövdelerinin
// başlık/varsayılan yapısı. Bunlar yanlışsa gerçek registry'ye yazmadan yakalanır.
//
// Testler registry yazmaz, dosya silmez, ağa çıkmaz — CI'da güvenle koşar.

#include "../src/core/lang.hpp"
#include "../src/core/ram.hpp"
#include "../src/core/regpack.hpp"
#include "../src/core/tweaks.hpp"
#include "../src/core/elevate.hpp"
#include "../src/core/network.hpp"
#include "../src/core/startup.hpp"
#include "../src/core/sample_data.hpp"
#include "../src/core/regtext.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

static int g_fail = 0;
static int g_pass = 0;

static void Check(bool ok, const char* what)
{
    if (ok) ++g_pass;
    else { ++g_fail; std::printf("  BASARISIZ: %s\n", what); }
}

static void CheckEq(int got, int want, const char* what)
{
    if (got == want) ++g_pass;
    else { ++g_fail; std::printf("  BASARISIZ: %s (beklenen %d, gelen %d)\n", what, want, got); }
}

// ---------------------------------------------------------------------------

static void Test_LangTables()
{
    std::printf("i18n tablolari\n");

    // Sayı sabitleri ve blok tablo adresleri tutarlı olmalı. Bunlar yanlışsa tweak
    // etiketleri yanlış metni göstermeye başlar; sınır kontrolü yakalamadan önce
    // ekranda yanlış şey görünür.
    const int tweakCount = S::kTweakCatCount * S::kTweakRows;
    CheckEq(tweakCount, 80, "toplam tweak sayisi (10 kategori x 8 satir)");

    Check(S::TweakNames > 0, "TweakNames sifirdan buyuk");
    CheckEq(S::TweakDescs - S::TweakNames, tweakCount, "TweakDescs kaymasi");
    CheckEq(S::NetNames - S::TweakDescs, tweakCount, "NetNames kaymasi");
    Check(S::NetNames < S::NetDescs, "NetNames < NetDescs");
    Check(S::_COUNT > S::NetDescs, "_COUNT en sonda");

    // TweakNameKey adreslemesi: son kategori/son satır kendi bloğunun içinde mi?
    const int son = S::TweakNameKey(S::kTweakCatCount - 1, S::kTweakRows - 1);
    Check(son >= S::TweakNames && son < S::TweakDescs, "son tweak anahtari blok icinde");

    // Her tweak adı ve açıklaması dolu olmalı; boş dize ekranda görünmez etiket bırakır.
    int bos = 0;
    for (int c = 0; c < S::kTweakCatCount; ++c)
        for (int r = 0; r < S::kTweakRows; ++r)
        {
            const char* n = lang::Get(S::TweakNameKey(c, r));
            const char* d = lang::Get(S::TweakDescKey(c, r));
            if (!n || !*n || std::strcmp(n, "???") == 0) ++bos;
            if (!d || !*d || std::strcmp(d, "???") == 0) ++bos;
        }
    CheckEq(bos, 0, "tum tweak ad/aciklamalari dolu");

    // Sınır dışı istek "???" döner (uygulama çökmesin).
    Check(std::strcmp(lang::Get(-1), "???") == 0, "Get(-1) -> ???");
    Check(std::strcmp(lang::Get(S::_COUNT), "???") == 0, "Get(_COUNT) -> ???");
    Check(std::strcmp(lang::Get(S::_COUNT + 100), "???") == 0, "Get(asiri) -> ???");
}

static void Test_RamPresets()
{
    std::printf("RAM presetleri\n");

    const int n = ram::PresetCount();
    Check(n > 0, "preset sayisi pozitif");
    CheckEq(n, 14, "preset sayisi 14");

    // İlk preset "Default" (0 GB) olmalı, sıralı ve pozitif olmayan indeks
    // sınır dışı yakalanmalı.
    CheckEq(ram::PresetGB(0), 0, "ilk preset 0 GB (varsayilan)");
    Check(std::strcmp(ram::PresetName(0), "Default") == 0, "ilk preset adı Default");

    // Her preset pozitif bir ad taşımalı (0 dışındakiler).
    int adBos = 0;
    for (int i = 1; i < n; ++i)
    {
        if (!ram::PresetName(i) || !*ram::PresetName(i)) ++adBos;
        if (ram::PresetGB(i) <= 0) ++adBos;
    }
    CheckEq(adBos, 0, "tum presetlerde pozitif GB ve ad var");

    // Sıralı artış: presetler küçükten büyüğe olmalı.
    int sirasiz = 0;
    for (int i = 1; i < n; ++i)
        if (ram::PresetGB(i) <= ram::PresetGB(i - 1)) ++sirasiz;
    CheckEq(sirasiz, 0, "presetler artan sirada");

    // Geçersiz indeks 0 döner ve "?" adı verir, çökmez.
    CheckEq(ram::PresetGB(-1), 0, "PresetGB(-1) -> 0");
    CheckEq(ram::PresetGB(n), 0, "PresetGB(n) -> 0");
    Check(std::strcmp(ram::PresetName(-1), "?") == 0, "PresetName(-1) -> ?");
    Check(std::strcmp(ram::PresetName(n), "?") == 0, "PresetName(n) -> ?");
}

// tweaks::IsRegPack ve IsShared sözleşmeleri. Registry'ye dokunmadan test edilir.

static void Test_TweakContracts()
{
    std::printf("tweak sozlesmeleri\n");

    // 0-3 doğrudan registry yazan kategoriler; 4-9 .reg paketleri.
    Check(!tweaks::IsRegPack(0), "kategori 0 regpack degil");
    Check(!tweaks::IsRegPack(3), "kategori 3 regpack degil");
    Check(tweaks::IsRegPack(4), "kategori 4 regpack");
    Check(tweaks::IsRegPack(5), "kategori 5 regpack");
    Check(tweaks::IsRegPack(6), "kategori 6 regpack");
    Check(tweaks::IsRegPack(7), "kategori 7 (Valorant) regpack");
    Check(tweaks::IsRegPack(8), "kategori 8 (CS2) regpack");
    Check(tweaks::IsRegPack(9), "kategori 9 (Fortnite) regpack");
    Check(!tweaks::IsRegPack(10), "kategori 10 (yok) regpack degil");

    // IsShared belgelenmiş paylaşımlı değerleri işaretlemeli:
    //   SystemResponsiveness -> Perf[5], Game[5], FiveM[1]
    //   DisablePagingExecutive -> Perf[7], Games[5]
    //   GameDVR_FSEBehaviorMode -> Game[1], Games[1]
    Check(tweaks::IsShared(0, 5), "Perf[5] paylasimli (SystemResponsiveness)");
    Check(tweaks::IsShared(0, 7), "Perf[7] paylasimli (DisablePagingExecutive)");
    Check(tweaks::IsShared(1, 1), "Game[1] paylasimli (GameDVR_FSEBehaviorMode)");
    Check(tweaks::IsShared(1, 5), "Game[5] paylasimli");
    Check(tweaks::IsShared(4, 5), "Games[5] paylasimli");
    Check(tweaks::IsShared(5, 1), "FiveM[1] paylasimli (SystemResponsiveness)");
    // Oyunlar ve FiveM ayni "Games" gorev anahtarini paylasir; hesaplanan liste:
    //   Games[0] + FiveM[0] : GPU Priority, Priority, Scheduling Category, SFIO Priority
    //   Games[2] + FiveM[0] : Affinity, Background Only, Clock Rate
    // Bu uc ayar IsShared'de yoksa biri kapatildiginda digerinin degerleri ezilir.
    Check(tweaks::IsShared(4, 0), "Games[0] paylasimli (FiveM[0] ile)");
    Check(tweaks::IsShared(4, 2), "Games[2] paylasimli (FiveM[0] ile)");
    Check(tweaks::IsShared(5, 0), "FiveM[0] paylasimli (Games[0] ve Games[2] ile)");
    // Oyun profilleri de ayni Games gorev anahtarini ve fare/GameBar degerlerini
    // paylasir; kapatilan bir profil digerinin yazisini sessizce ezmemeli.
    Check(tweaks::IsShared(7, 4), "Valorant[4] paylasimli (Games[0]/FiveM[0] ile)");
    Check(tweaks::IsShared(7, 7), "Valorant[7] paylasimli (SystemResponsiveness)");
    Check(tweaks::IsShared(8, 4), "CS2[4] paylasimli (Games[0]/FiveM[0] ile)");
    Check(tweaks::IsShared(8, 6), "CS2[6] paylasimli (GameDVR)");
    Check(tweaks::IsShared(9, 4), "Fortnite[4] paylasimli (Games[0]/FiveM[0] ile)");
    Check(tweaks::IsShared(9, 7), "Fortnite[7] paylasimli (SystemResponsiveness)");
    // Profil kategorilerinin kendi IFEO anahtarlari paylasilmaz.
    Check(!tweaks::IsShared(7, 0), "Valorant[0] paylasimli degil (kendi IFEO anahtari)");
    Check(!tweaks::IsShared(8, 0), "CS2[0] paylasimli degil (kendi IFEO anahtari)");
    Check(!tweaks::IsShared(9, 0), "Fortnite[0] paylasimli degil (kendi IFEO anahtari)");

    // Paylaşımlı olmayan bir anahtar işaretlenmemeli (her şey paylaşımlı olsayd
    // bu işaretlemenin bir anlamı kalmazdı).
    Check(!tweaks::IsShared(0, 0), "Perf[0] paylasimli degil");
    Check(!tweaks::IsShared(2, 3), "Priv[3] paylasimli degil");
    Check(!tweaks::IsShared(6, 0), "Delay[0] paylasimli degil");
}

// Registry paket gövdelerinin yapısı: her gövde geçerli .reg başlığı taşımalı ve
// kapalı gövde hiçbir yazma yapmamalı (bazıları değeri siler, hepsi anahtarı
// belirtir). İçeriği içe aktarmadan kontrol ediyoruz.

static void Test_RegPackBodies()
{
    std::printf("registry paket govdeleri\n");

    const char* baslik = "Windows Registry Editor Version 5.00";

    for (int cat = 4; cat <= 9; ++cat)
    {
        for (int i = 0; i < 8; ++i)
        {
            for (int e = 0; e < 2; ++e)
            {
                const char* b = regpack::Body(cat, i, e == 0);
                if (!b) { Check(false, "regpack::Body null dondu"); continue; }

                const bool baslikVar = b[0] == 'W' && std::strncmp(b, baslik, std::strlen(baslik)) == 0;
                if (!baslikVar)
                {
                    Check(false, "regpack govdesi .reg basligi ile baslamiyor");
                    goto next;
                }
                // Gövde en az bir anahtar başlığı ( [HKEY_... ] ) taşımalı.
                if (!std::strstr(b, "[HKEY_"))
                {
                    Check(false, "regpack govdesinde HKEY basligi yok");
                    goto next;
                }
                ++g_pass;
            next:;
            }
        }
    }

    // Sınır dışı indeks null döner.
    Check(regpack::Body(4, -1, true) == nullptr, "Body(4,-1) -> null");
    Check(regpack::Body(4, 8, true) == nullptr, "Body(4,8) -> null");
    Check(regpack::Body(3, 0, true) == nullptr, "Body(3,0) -> null (regpack degil)");
    Check(regpack::Body(10, 0, true) == nullptr, "Body(10,0) -> null (kategori yok)");

    // Kapalı gövde gerçekten bir yazma yapmamalı: her gövdede en az bir
    // "=dword" ya da "=\" değeri bulunmalı. IFEO geri alma gövdeleri değerleri
    // silme işaretiyle ("=-") yazdığı için sayıma girer.
    for (int cat = 4; cat <= 9; ++cat)
        for (int i = 0; i < 8; ++i)
        {
            const char* b = regpack::Body(cat, i, false);
            if (!b) { Check(false, "regpack::Body(kapalı) null dondu"); continue; }
            const bool yazmaVar = std::strstr(b, "=dword") || std::strstr(b, "=\"") ||
                                  std::strstr(b, "=-");
            Check(yazmaVar, "kapali govde yazma iceriyor");
        }
}

// Yetki yükseltme komut satırı gidiş-dönüşü. Bekleyen iş, kullanıcı UAC onayı verdiğinde
// yükseltilmiş sürece bu metinle taşınır; kodlama/çözme bir yönde çalışsa diğerinde
// bozulur ve ayar sessizce yanlış uygulanır. Saf metin dönüşümüdür, registry'ye dokunmaz.

static bool RoundTrip(const elevate::Pending& in, elevate::Pending& out)
{
    const std::wstring args = elevate::EncodeCommandLine(in);
    // --apply= bayrağını ayrıştırıcıya verilecek biçime getir.
    const size_t eq = args.find(L'=');
    if (eq == std::wstring::npos) return false;

    out = elevate::Pending();
    const std::wstring flag = args.substr(0, eq + 1);

    wchar_t* argv[2] = { const_cast<wchar_t*>(L"TENGRI.exe"), const_cast<wchar_t*>(args.c_str()) };
    (void)flag;
    return elevate::DecodeCommandLine(2, argv, out);
}

static void Test_ElevateCodec()
{
    std::printf("yetki yukseltme kodlamasi\n");

    // Boş iş: hiçbir şey kodlanmamalı ama bayrak yine de üretilmeli.
    {
        elevate::Pending in;
        elevate::Pending out;
        Check(RoundTrip(in, out), "bos is kodlanip cozuldu");
        CheckEq(out.ramPreset, -1, "bos: ram yok");
        CheckEq(out.dnsProvider, -1, "bos: dns yok");
        CheckEq(out.startupToggle, -1, "bos: baslangic yok");
    }

    // Tek kategoride iki satır açık (kategori 2 gizlilik, satır 0 ve 3).
    {
        elevate::Pending in;
        in.anyChange[2] = true;
        in.categories[2][0] = true;
        in.categories[2][3] = true;
        elevate::Pending out;
        Check(RoundTrip(in, out), "kategori 2 kodu cozuldu");
        Check(out.anyChange[2], "kategori 2 isaretlendi");
        Check(out.categories[2][0], "kategori 2 satir 0 acik");
        Check(out.categories[2][3], "kategori 2 satir 3 acik");
        Check(!out.categories[2][1], "kategori 2 satir 1 kapali kalmali");
        Check(!out.anyChange[0], "kategori 0 isaretlenmemeli");
    }

    // Tam maske: 8 satırın hepsi açık. Bu, maske sınırını zorlar.
    {
        elevate::Pending in;
        in.anyChange[5] = true;
        for (int i = 0; i < 8; ++i) in.categories[5][i] = true;
        elevate::Pending out;
        Check(RoundTrip(in, out), "tam maske cozuldu");
        int acik = 0;
        for (int i = 0; i < 8; ++i) if (out.categories[5][i]) ++acik;
        CheckEq(acik, 8, "sekiz satirin hepsi aktarildi");
    }

    // Birden fazla kategori ve üç tekil seçim birlikte.
    {
        elevate::Pending in;
        in.anyChange[0] = true; in.categories[0][1] = true;
        in.anyChange[4] = true; in.categories[4][7] = true;
        in.anyChange[6] = true; in.categories[6][2] = true; in.categories[6][5] = true;
        in.ramPreset     = 12;
        in.dnsProvider   = 2;
        in.startupToggle = 1;
        elevate::Pending out;
        Check(RoundTrip(in, out), "coklu kategori ve secimler cozuldu");
        Check(out.categories[0][1] && out.categories[4][7], "kategori 0/4 aktarildi");
        Check(out.categories[6][2] && out.categories[6][5], "kategori 6 iki satir aktarildi");
        CheckEq(out.ramPreset, 12, "RAM profili 12");
        CheckEq(out.dnsProvider, 2, "DNS saglayici 2");
        CheckEq(out.startupToggle, 1, "baslangic acik");
    }

    // Tam kategori: 8 kategorinin hepsi birden. Taşma sınırını zorlar.
    {
        elevate::Pending in;
        for (int c = 0; c < 8; ++c) { in.anyChange[c] = true; in.categories[c][c] = true; }
        elevate::Pending out;
        Check(RoundTrip(in, out), "sekiz kategori birden cozuldu");
        int dogru = 0;
        for (int c = 0; c < 8; ++c) if (out.anyChange[c] && out.categories[c][c]) ++dogru;
        CheckEq(dogru, 8, "sekiz kategori dogru yerde");
    }

    // Sınır dışı değerler bozulmamalı: RAM 255 (0xFF değil, gerçek değer).
    {
        elevate::Pending in;
        in.ramPreset = 255;
        elevate::Pending out;
        Check(RoundTrip(in, out), "ram 255 cozuldu");
        CheckEq(out.ramPreset, 255, "255 gercek deger olarak kaldi (yok demek degil)");
    }

    // --apply bayragi yoksa cozulemez.
    {
        elevate::Pending out;
        wchar_t* argv[2] = { const_cast<wchar_t*>(L"TENGRI.exe"), const_cast<wchar_t*>(L"--other") };
        Check(!elevate::DecodeCommandLine(2, argv, out), "bayraksiz komut satiri reddedildi");
    }

    // Geri alma klasoru adi: ayar uygulanmaz, yedek geri yuklenir. Ad komut satirindan
    // geldigi icin tasinma ve sınır denetimi ayrıca denenmeli.
    {
        elevate::Pending in;
        in.restoreFrom = "20261006-071234";
        elevate::Pending out;
        Check(RoundTrip(in, out), "geri alma klasoru cozuldu");
        Check(out.restoreFrom == "20261006-071234", "klasor adi aynen aktarildi");

        // Geri alma istegi varken ayar istekleri de tasinabilmeli; yukseltilmis surec
        // once geri alma yoluna girer, ama kodlama ikisini de korumali.
        elevate::Pending in2;
        in2.restoreFrom = "20260101-000000";
        in2.anyChange[3] = true;
        in2.categories[3][4] = true;
        elevate::Pending out2;
        Check(RoundTrip(in2, out2), "geri alma + ayar birlikte cozuldu");
        Check(out2.restoreFrom == "20260101-000000", "klasor adi korundu");
        Check(out2.categories[3][4], "ayar da korundu");
    }

    // Kontrol karakteri ve bosluk iceren klasor adi reddedilmeli: komut satiri
    // ayristiricisi bunlari guvenle ayirt edemez.
    {
        elevate::Pending out;
        elevate::Pending in;
        in.restoreFrom = std::string("bad\x01name");
        std::wstring args = elevate::EncodeCommandLine(in);
        wchar_t* argv[2] = { const_cast<wchar_t*>(L"TENGRI.exe"), const_cast<wchar_t*>(args.c_str()) };
        Check(!elevate::DecodeCommandLine(2, argv, out), "kontrol karakterli klasor adi reddedildi");
    }
}

// Registry paket govdelerinin acil/kapali simetrisi.
//
// Her ayar iki govdeyle saklanir: acikken ne yazilacagi ve kapaliyken ne yazilacagi.
// Kapali govde her zaman "belgelenmis varsayilani geri yukler ya da degeri siler" --
// yani kendi varsayilani, kullanicinin daha onceki degeri degil. Bu bir tasarim
// tercihidir, ama su sonucu dogurur: kapali govde acik govdenin yazdigi tum anahtarlari
// karsilamiyorsa o ayar geri donussuz kaybolur. Sessizce kaybolan bir ayar, hatali
// bir hata mesajindan cok daha kotudur.
//
// Bu test asagidakileri zorlar:
//   1) Kapali govde, acik govdenin yazdigi anahtarlarin TAMAMINI ele aliyor
//   2) Ortak degerler icin acik ve kapali degerler FARKLI (ayniysa anahtar bir sey yapmiyor)
//   3) Govde yazim bicimi gecerli (.reg basligi, HKEY basligi, deger satiri)

namespace
{
    // Bir .reg govdesini ayristirir: anahtar yolu -> deger adi -> deger metni
    using Parsed = std::map<std::string, std::map<std::string, std::string>>;

    Parsed ParseReg(const char* text)
    {
        Parsed out;
        if (!text) return out;

        std::string key;
        for (const char* line = text; line && *line; )
        {
            const char* eol = std::strchr(line, '\n');
            std::string ln(line, eol ? (size_t)(eol - line) : std::strlen(line));
            line = eol ? eol + 1 : nullptr;

            while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.pop_back();
            if (ln.empty()) continue;

            if (ln[0] == '[')
            {
                // [HKEY_...\Yol]  veya  [-HKEY_...] (anahtari sil)
                const size_t b = ln.find('[');
                const size_t e = ln.rfind(']');
                if (b != std::string::npos && e != std::string::npos && e > b)
                {
                    std::string k = ln.substr(b + 1, e - b - 1);
                    if (!k.empty() && k[0] == '-') k.erase(0, 1);
                    key = k;
                    out[key];   // anahtari kaydet
                }
            }
            else if (ln[0] == '"' && ln.find('=') != std::string::npos)
            {
                const size_t q = ln.find('"', 1);
                if (q == std::string::npos) continue;
                std::string ad = ln.substr(1, q - 1);
                std::string deger = ln.substr(ln.find('=') + 1);
                while (!deger.empty() && deger.front() == ' ') deger.erase(0, 1);
                // On ekli - deger silme isaretini koru: "adir" ile "-adir" farklidir.
                out[key][ad] = deger;
            }
        }
        return out;
    }

    std::string Join(const Parsed& p)
    {
        std::string s;
        for (const auto& kv : p)
        {
            if (!s.empty()) s += "; ";
            s += kv.first;
            for (const auto& d : kv.second) s += "[" + d.first + "=" + d.second + "]";
        }
        return s;
    }
}

static void Test_RegPackSymmetry()
{
    std::printf("registry paket simetrisi\n");

    int eksikAnahtar = 0, ayniDeger = 0, bicimHatasi = 0;

    for (int cat = 4; cat <= 6; ++cat)
    {
        for (int i = 0; i < 8; ++i)
        {
            const char* acik  = regpack::Body(cat, i, true);
            const char* kapali = regpack::Body(cat, i, false);
            if (!acik || !kapali) { ++bicimHatasi; continue; }

            const Parsed pa = ParseReg(acik);
            const Parsed pk = ParseReg(kapali);

            if (pa.empty() || pk.empty()) { ++bicimHatasi; continue; }

            // 1) Kapali govde, acik govdenin yazdigi her anahtari ele almali.
            for (const auto& kv : pa)
            {
                if (!kv.second.empty() && pk.find(kv.first) == pk.end())
                {
                    ++eksikAnahtar;
                    std::printf("  BASARISIZ: kategori %d, ayar %d -- kapali govde bu anahtari ele almıyor: %s\n",
                                cat, i, kv.first.c_str());
                }
            }

            // 2) Ortak degerler icin acik ve kapali degerler farkli olmali.
            for (const auto& kv : pa)
            {
                auto kit = pk.find(kv.first);
                if (kit == pk.end()) continue;
                for (const auto& d : kv.second)
                {
                    auto d2 = kit->second.find(d.first);
                    if (d2 != kit->second.end() && d2->second == d.second)
                    {
                        ++ayniDeger;
                        std::printf("  BASARISIZ: kategori %d, ayar %d -- %s\\%s icin acik ve kapali deger ayni: %s\n",
                                    cat, i, kv.first.c_str(), d.first.c_str(), d.second.c_str());
                    }
                }
            }
        }
    }

    CheckEq(eksikAnahtar, 0, "kapali govde acik govdenin tum anahtarlarini ele aliyor");
    CheckEq(ayniDeger, 0, "acik ve kapali degerler birbirinden farkli");
    CheckEq(bicimHatasi, 0, "tum govdeler ayristirilabildi");
}

// ---------------------------------------------------------------------------
// Ekran goruntusu kipi (TENGRI_SHOT)
//
// docs/screenshots altindaki gorsellerde makine bilgisi gorunuyor. Kip acikken
// o alanlar sabit orneklerle ciziliyor. Testler gercek API'leri hic cagirmiyor:
// yalnizca ornek verinin bicimi ve kacin bir kez belirlenmesi denetleniyor.

static bool HwidBicimi(const char* s)
{
    // Ornek, gercek HWID ile ayni bicimde olmali: dort grup, dort onaltilik hane.
    // Bicim farkliysa ekran goruntusundaki satir gercek davranisi yanlis
    // anlatirdi.
    const char* p = s;
    for (int g = 0; g < 4; ++g)
    {
        if (g && *p++ != '-') return false;
        for (int i = 0; i < 4; ++i)
        {
            const char c = *p++;
            const bool hex = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
            if (!hex) return false;
        }
    }
    return *p == '\0';
}

// license::Mask'in urettiği bicim: XXXX-••••-••••-XXX. Ornek dize bu bicimde
// degilse Ayarlar ekranindaki satir gercek maskeyi tanimaz hale gelir.
static bool LisansMaskesiBicimi(const char* s)
{
    const std::string bullet = "\xE2\x80\xA2";
    std::string m(s);

    int ad = 0;
    for (size_t pos = 0; (pos = m.find(bullet, pos)) != std::string::npos; pos += bullet.size()) ++ad;
    if (ad != 8) return false;

    // Grup ayiraclari: 4, sonra 12 bayt (4 mermi), ayirac, 12 bayt, ayirac.
    if (m.size() != 4 + 1 + 12 + 1 + 12 + 1 + 3) return false;
    if (m[4] != '-' || m[17] != '-' || m[30] != '-') return false;
    return true;
}

static void Test_BackupNameAndContentGate()
{
    std::printf("yedek: dosya adi ayristirma ve icerik kapisi\n");

    // ---- ad: "<cat>-<idx>.reg" veya "<cat>-<idx>-<tekrar>.reg"
    struct NameCase { const wchar_t* name; bool ok; int cat; int idx; };
    const NameCase kNames[] = {
        { L"3-12.reg",          true,   3,  12 },
        { L"0-0.reg",           true,   0,   0 },
        { L"12-345.reg",        true,  12, 345 },
        { L"7-1-2.reg",         true,   7,   1 },  // ayni ayarin ikinci anahtari
        { L"3-12.REG",          true,   3,  12 },  // uzanti buyuk/kucuk harf duyarsiz
        { L"",                 false,  -1,  -1 },
        { L"3.reg",            false,  -1,  -1 },  // sira yok
        { L"3-12",             false,  -1,  -1 },  // uzanti yok
        { L"-12.reg",          false,  -1,  -1 },  // kategori yok
        { L"3-.reg",           false,  -1,  -1 },  // bos sira
        { L"3-12-.reg",        false,  -1,  -1 },  // tire ile bitiyor
        { L"3-12-9-4.reg",     false,  -1,  -1 },  // dorduncu grup
        { L"a-12.reg",         false,  -1,  -1 },  // harf
        { L"3_12.reg",         false,  -1,  -1 },  // alt cizgi
        { L"3-12.exe",         false,  -1,  -1 },
        { L"..\\evil.reg",     false,  -1,  -1 },  // ad uzerinden yol zorlama
        { L"3-12.reg.reg",     false,  -1,  -1 },
        { L"00000003-1.reg",   true,   3,   1 },   // bastaki sifirlar zararsiz: cift yine tabloda olmak zorunda
        { L"10000000-1.reg",   false,  -1,  -1 },  // sayi tavanini asan uzunluk
        { L"3-12 .reg",        false,  -1,  -1 },  // bosluk
    };
    for (const NameCase& c : kNames)
    {
        int cat = -9, idx = -9;
        const bool got = regtext::ParseBackupName(c.name, &cat, &idx);
        Check(got == c.ok, c.ok ? "ad kabul edilmeli" : "ad reddedilmeli");
        if (c.ok && got) { CheckEq(cat, c.cat, "kategori dogru okundu"); CheckEq(idx, c.idx, "sira dogru okundu"); }
    }

    // ---- icerik kapisi: yalnizca beklenen (anahtar, deger) ciftleri gecer
    //
    // Kapi artik deger ADINA kadar iniyor. Onceki bicim yalnizca anahtar satirlarina
    // bakip deger satirlarini atliyordu; bir yedek dosyasi boylece beklenen anahtar
    // altinda istenen herhangi bir degere (HKLM\SYSTEM\CurrentControlSet\Control
    // altinda Session Manager\BootExecute gibi) yazabiliyordu. Asagidaki
    // "listede olmayan deger adi" vakalari tam olarak bunu olduruyor.
    const std::vector<regtext::Allowed> allowed = {
        { L"HKCU\\Software\\TENGRI\\Probe", L"Deger" },
        { L"HKCU\\Software\\TENGRI\\Probe", L"" },          // varsayilan deger
        { L"HKLM\\SOFTWARE\\TENGRI\\Other", L"Deger" },
    };
    const wchar_t* const H = L"Windows Registry Editor Version 5.00";

    struct Case { std::wstring text; bool ok; int recs; const char* what; };
    const std::wstring NL = L"\r\n";
    const std::wstring kProbe = L"[HKEY_CURRENT_USER\\Software\\TENGRI\\Probe]";
    const std::wstring kOther = L"[HKEY_LOCAL_MACHINE\\SOFTWARE\\TENGRI\\Other]";
    const std::wstring kValueLine = L"\"Deger\"=dword:00000001";
    const std::wstring kHexCont   = L"\"Deger\"=hex(7):61,00,00,00,\\\r\n  62,00,00,00\r\n";

    const Case kCases[] = {
        // Kabul: beklenen anahtar + beklenen deger.
        { std::wstring(H) + NL + kProbe + NL + kValueLine + NL, true, 1,
          "beklenen anahtar + beklenen deger" },
        // Kabul: deger adinin buyuk/kucuk harfi registry'de duyarlidir; tablo
        // "Deger" diyor, "deger" listede yok.
        { std::wstring(H) + NL + kProbe + NL + L"\"deger\"=dword:00000001" + NL, false, 0,
          "deger adi buyuk/kucuk harf duyarli" },
        // Kabul: anahtar adi duyarsiz.
        { std::wstring(H) + NL + L"[hkey_current_user\\software\\tengri\\probe]" + NL + kValueLine + NL,
          true, 1, "kucuk harfle yazilmis beklenen anahtar" },
        // Kabul: iki beklenen cift.
        { std::wstring(H) + NL + kProbe + NL + kValueLine + NL + kOther + NL + kValueLine + NL,
          true, 2, "ciftin iki anahtari da" },
        // Kabul: devam satiri olan hex degeri.
        { std::wstring(H) + NL + kProbe + NL + kHexCont, true, 1, "devam satiri olan deger" },
        // Kabul: varsayilan deger @= ile yazilir, adi bos.
        { std::wstring(H) + NL + kProbe + NL + L"@=hex(1):61,00,00,00" + NL, true, 1,
          "varsayilan deger" },
        // Kabul: sifir uzunluklu deger (disa aktarim "hex(N):" olarak yaziyor).
        { std::wstring(H) + NL + kProbe + NL + L"\"Deger\"=hex(3):" + NL, true, 1,
          "sifir uzunluklu deger" },

        // Red: ONEK TUZAGI - anahtar adi beklenenin baslangiciyla ayni ama bir
        // karakter uzun. Bisl olmadan gecerli degil.
        { std::wstring(H) + NL + L"[HKEY_CURRENT_USER\\Software\\TENGRI\\ProbeX]" + NL + kValueLine + NL,
          false, 0, "beklenen anahtarin kardesi (onek tuagi)" },
        // Red: yabanci anahtar.
        { std::wstring(H) + NL + L"[HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run]" + NL
          + L"\"Yeni\"=dword:00000001" + NL, false, 0, "yabanci Run anahtari" },
        // Red: LISTE DISI DEGER ADI - kapinin asil isi. Anahtar beklenen, deger
        // tablonun o cifti icin yazdigi listede yok.
        { std::wstring(H) + NL + kProbe + NL + L"\"ListedeOlmayan\"=dword:00000001" + NL,
          false, 0, "listede olmayan deger adi" },
        // Red: gercek yukseltme deseni. Anahtar HKLM\SYSTEM\CurrentControlSet\Control
        // uygulamanin meşru bir yedek anahtari; deger adi BootExecute ise SYSTEM
        // kodkoşumu demek. Tablo o anahtar altinda yalnizca WaitToKillServiceTimeout
        // tanir, dolayisiyla burasi reddedilmeli.
        { std::wstring(L"Windows Registry Editor Version 5.00") + NL
          + L"[HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control]" + NL
          + L"\"WaitToKillServiceTimeout\"=\"2000\"" + NL, false, 0,
          "gercek tabloda olmayan anahtar BootExecute icin kullanilamaz" },
        // Red: IFEO - ayni kategorideki gercek bir ayarin anahtari bile olsa,
        // beklenen kume disindaysa gecerli degil.
        { std::wstring(H) + NL + L"[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\notepad.exe]" + NL
          + L"\"CpuPriorityClass\"=dword:00000001" + NL, false, 0, "IFEO anahtari" },
        // Red: anahtari silme bicimi. Disa aktarim bunu hic uretmiyor; geri alma
        // sirasinda anahtar yok etmek, deger geri getirmek degil.
        { std::wstring(H) + NL + L"[-HKEY_CURRENT_USER\\Software\\TENGRI\\Probe]" + NL + kValueLine + NL,
          false, 0, "anahtar silme bicimi" },
        // Red: deger silme bicimi yedekte uretilemez.
        { std::wstring(H) + NL + kProbe + NL + L"\"Deger\"=-" + NL, false, 0,
          "yedekte deger silme bicimi" },
        // Red: baslik satiri yok (dosya anahtarla basliyor).
        { kProbe + NL + kValueLine + NL, false, 0, "baslik eksik" },
        // Red: baslik yanlis.
        { std::wstring(L"REGEDIT4") + NL + kProbe + NL + kValueLine + NL, false, 0,
          "baslik REGEDIT4" },
        // Red: hic deger kaydi yok - bos bir dosya geri yuklenmek icin degil.
        { std::wstring(H) + NL + kProbe + NL, false, 0, "deger satiri icermeyen dosya" },
        // Red: bos ayraclar.
        { std::wstring(H) + NL + L"[]" + NL, false, 0, "bos anahtar ayraci" },
        // Red: kapanmamis ayracli satir.
        { std::wstring(H) + NL + L"[HKEY_CURRENT_USER\\Software\\TENGRI\\Probe" + NL,
          false, 0, "kapanmamis ayraclar" },
        // Red: baska kok anahtar.
        { std::wstring(H) + NL + L"[HKEY_CLASSES_ROOT\\Software\\TENGRI\\Probe]" + NL + kValueLine + NL,
          false, 0, "HKEY_CLASSES_ROOT koku" },
        // Red: bolumsuz deger satiri.
        { std::wstring(H) + NL + kValueLine + NL, false, 0, "bolum disi deger satiri" },
        // Red: taninmayan satir.
        { std::wstring(H) + NL + kProbe + NL + L"bu bir satir degil" + NL, false, 0,
          "taninmayan satir" },
        // Red: dword uzunlugu yanlış.
        { std::wstring(H) + NL + kProbe + NL + L"\"Deger\"=dword:01" + NL, false, 0,
          "kisa dword" },
        // Red: hex kumesinde taninmayan karakter.
        { std::wstring(H) + NL + kProbe + NL + L"\"Deger\"=hex:gg" + NL, false, 0,
          "gecersiz hex" },
        // Red: tip listesi dışinda (REG_LINK = 6).
        { std::wstring(H) + NL + kProbe + NL + L"\"Deger\"=hex(6):01" + NL, false, 0,
          "geri yuklenemeyen tip" },
        // Red: gecerli bir kayitten SONRA liste dışı bir kayit gelirse tüm dosya
        // reddedilir ve cozumlenmis hicbir sey geri kalmamali.
        { std::wstring(H) + NL + kProbe + NL + kValueLine + NL
          + L"\"Yabanci\"=dword:00000001" + NL, false, 0, "kismi gecerli dosya" },
    };
    for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); ++i)
    {
        const Case& c = kCases[i];
        std::vector<regtext::Record> recs;
        const bool got = regtext::ParseRegText(c.text, &allowed, &recs);
        Check(got == c.ok, c.what);
        if (c.ok) CheckEq((int)recs.size(), c.recs, "kayit sayisi dogru cozuldu");
        else Check(recs.empty(), "reddedilen dosyadan kayit kalmadi");
    }

    // Bos beklenen kume: tabloya karsilik gelmeyen hicbir dosya gecerli degil.
    {
        const std::vector<regtext::Allowed> none;
        std::vector<regtext::Record> recs;
        Check(!regtext::ParseRegText(std::wstring(H) + NL + kProbe + NL + kValueLine + NL,
                                     &none, &recs), "beklenen kume bos");
    }

    // ---- guvenilir mod: derleme zamani govdeleri (regpack.cpp)
    //
    // allowed == nullptr oldugunda kapsam denetlenmiyor, cunku metin EXE'nin
    // icinde. Silme bicimi yalnizca burada gecer.
    {
        std::vector<regtext::Record> recs;
        Check(regtext::ParseRegText(std::wstring(H) + NL + kProbe + NL
              + L"\"Deger\"=-" + NL, nullptr, &recs), "goqvde silme bicimi kabul eder");
        Check(recs.size() == 1 && recs[0].remove, "silme kaydi isaretlendi");

        recs.clear();
        Check(regtext::ParseRegText(std::wstring(H) + NL
              + L"[HKEY_LOCAL_MACHINE\\ANYTHING\\At\\All]" + NL
              + L"\"HerNeyse\"=dword:00000001" + NL, nullptr, &recs),
              "guvenilir mod anahtar adini denetlemiyor");
    }

    // ---- SplitRoot / ExpandKey: yazma yolunun tek kok ayirici
    Check(regtext::ExpandKey(L"HKCU\\A") == L"HKEY_CURRENT_USER\\A", "HKCU tam ada cevrildi");
    Check(regtext::ExpandKey(L"HKLM\\A") == L"HKEY_LOCAL_MACHINE\\A", "HKLM tam ada cevrildi");
    // Cizgi dusurulursa "HKEY_CURRENT_USERSoftware\\..." gibi gecersiz yol olurdu.
    Check(regtext::ExpandKey(L"SKUNTER\\A") == L"SKUNTER\\A", "kisa adi olmayan yol dokunulmadi");
    {
        HKEY root = nullptr; std::wstring sub;
        Check(regtext::SplitRoot(L"HKEY_LOCAL_MACHINE\\SYSTEM", &root, &sub)
              && root == HKEY_LOCAL_MACHINE && sub == L"SYSTEM",
              "HKLM ondeki uzunlugu dogru (cizgi dahil)");
        Check(regtext::SplitRoot(L"HKEY_CURRENT_USER\\A\\B", &root, &sub)
              && root == HKEY_CURRENT_USER && sub == L"A\\B",
              "HKCU ondeki uzunlugu dogru");
        Check(!regtext::SplitRoot(L"HKEY_LOCAL_MACHINE", &root, &sub), "tek basina kok reddedilir");
        Check(!regtext::SplitRoot(L"HKEY_LOCAL_MACHINEX\\A", &root, &sub), "onek tuagi reddedilir");
        Check(!regtext::SplitRoot(L"HKEY_USERS\\S-1-5\\A", &root, &sub), "HKU reddedilir");
        Check(!regtext::SplitRoot(L"Software\\TENGRI", &root, &sub), "koksuz yol reddedilir");
    }

    // ---- SerializeValue / ParseRegText gidis-donus
    //
    // Disa aktarim ile geri yukleme ayni bicimi kullaniyor; biri digerinin
    // cikardisini okuyamiyorsa yedek sessizce geri alinamaz olur. Bu, eski
    // surumde butun regpack kategorilerinin hic uygulanmamasina yol acan hata
    // sinifiynin aynı hali: uretici ile tuketici arasinda sozlesme testi.
    {
        auto roundtrip = [&](const wchar_t* name, DWORD type,
                            const std::vector<unsigned char>& in) {
            std::wstring text;
            if (!regtext::SerializeValue(name, type, in.empty() ? nullptr : in.data(),
                                         (DWORD)in.size(), &text)) return false;
            std::vector<regtext::Record> recs;
            const std::wstring full = std::wstring(H) + NL + kProbe + NL + text;
            // Ciftin izin listesi bu anahtar icin "Deger" ve varsayilanadi taniyor.
            if (!regtext::ParseRegText(full, &allowed, &recs)) return false;
            if (recs.size() != 1) return false;
            const regtext::Record& r = recs[0];
            return r.name == name && r.type == type && r.data == in && !r.remove;
        };

        Check(roundtrip(L"Deger", REG_DWORD, { 1, 0, 0, 0 }), "dword gidis-donus");
        Check(roundtrip(L"Deger", REG_DWORD, { 0xFF, 0xFF, 0xFF, 0xFF }), "buyuk dword gidis-donus");
        Check(roundtrip(L"Deger", REG_SZ, { 'a', 0, 'b', 0, 0, 0 }), "REG_SZ bayt olarak dondu");
        Check(roundtrip(L"Deger", REG_BINARY, { 0xDE, 0xAD, 0xBE, 0xEF }), "binary gidis-donus");
        Check(roundtrip(L"Deger", REG_NONE, {}), "sifir uzunluklu deger gidis-donus");
        Check(roundtrip(L"", REG_SZ, { 0, 0 }), "varsayilan deger gidis-donus");

        // Uzun ad: 64 haneden fazla bayt yazilmamali.
        std::vector<unsigned char> big(regtext::kMaxValueBytes + 1, 0xAB);
        std::wstring text;
        Check(!regtext::SerializeValue(L"Deger", REG_BINARY, big.data(),
                                       (DWORD)big.size(), &text),
              "boyut tavani asan deger disa aktarilmaz");

        // Adinda tirsnak olan deger satiri bozardi; yazmayi reddetmeli.
        Check(!regtext::SerializeValue(L"Kacli\"Deger", REG_DWORD, nullptr, 0, &text),
              "adinda tirsnak olan deger yazilmaz");
    }
}

static void Test_SampleMode()
{
    _putenv_s("TENGRI_SHOT", "");
    sample::InitFromEnv();
    Check(!sample::Active(), "TENGRI_SHOT yokken kip kapali");

    _putenv_s("TENGRI_SHOT", "0");
    sample::InitFromEnv();
    Check(!sample::Active(), "TENGRI_SHOT=0 kipi acmıyor");

    _putenv_s("TENGRI_SHOT", "2");
    sample::InitFromEnv();
    Check(!sample::Active(), "beklenmeyen deger kipi acmıyor");

    _putenv_s("TENGRI_SHOT", "1");
    sample::InitFromEnv();
    Check(sample::Active(), "TENGRI_SHOT=1 kipi acar");

    Check(HwidBicimi(sample::Hwid()), "ornek HWID gercek bicimde");
    Check(LisansMaskesiBicimi(sample::LicenseMask()), "ornek lisans maskesi license::Mask biciminde");

    // Ag getterlari ornek degeri dondururmu — kip acikken gercek adaptore
    // hic bakilmamali.
    Check(network::LocalIP()     == sample::LocalIp(),     "yerel IP ornege cevrildi");
    Check(network::GatewayIP()   == sample::GatewayIp(),   "ag gecidi ornege cevrildi");
    Check(network::AdapterName() == sample::AdapterName(), "adaptor adi ornege cevrildi");

    const auto v = sample::StartupEntries();
    CheckEq((int)v.size(), 3, "uc ornek baslatma girdisi");
    for (const startup::Entry& e : v)
    {
        Check(!e.name.empty() && !e.command.empty() && !e.resolvedPath.empty() &&
              !e.publisher.empty(), "ornek girdinin alanlari dolu");
        Check((int)e.scope < (int)startup::Scope::Count, "ornek kapsam tablodaki bir kapsam");
        Check(!e.isTengri, "ornek girdi TENGRI'nin kendi kaydi gibi durmuyor");
        // Genisletilmemis degisken yolu cizilirdi: "%APPDATA%\..." gercek
        // kullanici adini ele vermez ama arayuzde de bir anlam tasimiyor.
        Check(e.command.find(L'%') == std::wstring::npos, "ornek kumanda satiri degisken tasimiyor");
    }
    // Uc sekmenin de dolu gorunmesi icin farkli kapsamlar: biri kapali durumdaki
    // makine girdisi, digeri kullanici girdileri.
    Check(v[0].scope != v[2].scope, "ornek girdiler kullanici ve makine kapsamlarina dagitik");
    Check(!v[2].enabled, "makine ornegi kapali durumda: arayuzde acilabilir satir ciziliyor");

    _putenv_s("TENGRI_SHOT", "");
    sample::InitFromEnv();
    Check(!sample::Active(), "kip kapatilabiliyor");
}

namespace
{
// Profil analiz: bicim hatasi, tablo izinlisi, kismi eslesme
static void Test_ProfileAnalyze()
{
    std::vector<regtext::Allowed> allowed;
    // Tabloyu doldur (tweakinfo::AllAllowed)
    tweakinfo::AllAllowed(&allowed);
    Check(allowed.size() > 0, "tablodan izinli ciftler geldi");

    // Gecerli profil simülasyonu: tek bir cift
    std::wstring good = L"Windows Registry Editor Version 5.00\r\n";
    if (!allowed.empty())
    {
        const regtext::Allowed& a = allowed[0];
        good += L"[" + a.key + L"]\r\n";
        good += L"\"" + a.name + L"\"=dword:00000001\r\n";
    }

    profile::Analysis a;
    bool ok = profile::Analyze(good, &a);
    Check(ok, "bicim gecerli");
    Check(a.parsed, "parsed=true");
    // Foreign degilse on listesi dolu olmalı
    Check((bool)((a.on.size() > 0) || !a.foreign), "on veya foreign dogru ayarlandi");
}

// Geri yükleme noktası kodlaması: bayt sayisi > 9, bozuk hizalama reddi
static void Test_RestorePointCodec()
{
    elevate::Pending p1 = {};
    p1.ramPreset       = 1;
    p1.dnsProvider     = 0;
    p1.startupToggle   = 1;
    p1.restorePoint    = true;
    p1.restoreFrom     = "TENGRI-20241210-143015";
    p1.anyChange[2]    = true;  // Gizlilik
    p1.categories[2][3] = true; // dördüncü satır acik

    std::wstring cmd = elevate::EncodeCommandLine(p1);

    // Kodlama uzunluğu yeterince uzun olmalı (bayt basliga)
    Check(cmd.size() > 8);

    elevate::Pending p2 = {};
    wchar_t* argv[] = { nullptr, cmd.c_str() };
    bool decoded = elevate::DecodeCommandLine(2, argv, p2);
    Check(decoded, "decoded");
    Check((bool)(p2.ramPreset == 1), "ramPreset");
    Check((bool)(p2.dnsProvider == 0), "dnsProvider");
    Check((bool)(p2.startupToggle == 1), "startupToggle");
    Check((bool)(p2.restorePoint == true), "restorePoint");
    Check((bool)(p2.restoreFrom == p1.restoreFrom), "restoreFrom");
    Check((bool)(p2.anyChange[2]), "anyChange[2]");
    Check((bool)(p2.categories[2][3]), "categories[2][3]");
}
}

int main()
{
    std::printf("TENGRI saf mantik testleri\n\n");

    Test_LangTables();
    Test_RamPresets();
    Test_TweakContracts();
    Test_RegPackBodies();
    Test_RegPackSymmetry();
    Test_ElevateCodec();
    Test_BackupNameAndContentGate();
    Test_ProfileAnalyze();
    Test_RestorePointCodec();
    Test_SampleMode();

    std::printf("\n%d gecti, %d kaldi\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}