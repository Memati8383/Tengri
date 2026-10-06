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

#include <cstdio>
#include <cstring>
#include <string>

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
    CheckEq(tweakCount, 56, "toplam tweak sayisi (7 kategori x 8 satir)");

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

    // 0-3 doğrudan registry yazan kategoriler; 4-6 .reg paketleri.
    Check(!tweaks::IsRegPack(0), "kategori 0 regpack degil");
    Check(!tweaks::IsRegPack(3), "kategori 3 regpack degil");
    Check(tweaks::IsRegPack(4), "kategori 4 regpack");
    Check(tweaks::IsRegPack(5), "kategori 5 regpack");
    Check(tweaks::IsRegPack(6), "kategori 6 regpack");
    Check(!tweaks::IsRegPack(7), "kategori 7 (yok) regpack degil");

    // IsShared belgelenmiş paylaşımlı değerleri işaretlemeli:
    //   SystemResponsiveness -> Perf[5], Game[5], FiveM[1]
    //   DisablePagingExecutive -> Perf[7], Games[5]
    //   GameDVR_FSEBehaviorMode -> Game[1], Games[1]
    Check(tweaks::IsShared(0, 5), "Perf[5] paylasimli (SystemResponsiveness)");
    Check(tweaks::IsShared(0, 7), "Perf[7] paylasimli (DisablePagingExecutive)");
    Check(tweaks::IsShared(1, 1), "Game[1] paylasimli (GameDVR_FSEBehaviorMode)");
    Check(tweaks::IsShared(1, 5), "Game[5] paylasimli");
    Check(tweaks::IsShared(4, 5), "Games[5] paylasimli");
    Check(tweaks::IsShared(5, 1), "FiveM[1] paylasimli");

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

    for (int cat = 4; cat <= 6; ++cat)
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
}

int main()
{
    std::printf("TENGRI saf mantik testleri\n\n");

    Test_LangTables();
    Test_RamPresets();
    Test_TweakContracts();
    Test_RegPackBodies();
    Test_ElevateCodec();

    std::printf("\n%d gecti, %d kaldi\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}