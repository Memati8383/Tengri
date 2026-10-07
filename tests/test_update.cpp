// update modulu testleri.
//
// Amaç: sürüm denetiminin DOĞRULAMA kısmını germek. En yeni sürümün nerede
// arandığı ve indirilen baytlara neden güvenilebildiği burada karar buluyor;
// bu ikisi yanlışsa güncelleme bir saldırı yüzeyine dönüşür.
//
// Yan-etki sözleşmesi (test_pure / test_license ile aynı):
//   - AĞ YOK. CheckNow/StageNow hiç çağrılmıyor: hiçbir sunucuya istek
//     gitmiyor, dosya inmiyor, süreç başlamıyor. Loopback de dahil.
//   - REGISTRY YOK. LoadPrefs/SavePrefs/MarkChecked çağrılmıyor; yazdıkları
//     anahtar (HKCU\Software\TENGRI\Update) testte hiç açılmıyor. Salt-okuma
//     de yapılmıyor ki test, kullanıcının "son denetim" zamanını bozmasın.
//   - DOSYA: yalnızca kendi yarattığımız geçici dosya (GetTempPath altında,
//     benzersiz adla) yazılır/okunur ve sonunda silinir. Kullanıcı verisine,
//     uygulama klasörüne ve çalışan exe'ye dokunulmaz.
//   - Doğrulanan mantık: sürüm sıralaması, bildirim ayrıştırması, sunucu
//     allowlist'i, SHA-256 çekirdeği, yol türetme, 24 saat hesabı, durum
//     makinesinin kapalı/güvensiz yolları.
//
// Test edilmeyen, bilinçli: HTTP yanıtının kendisi ve takasın (MoveFile ->
// CopyFile -> CreateProcess) gerçek dosya sistemiyle çalışması. Takası test
// etmek, testi yapan exe'nin kendi adını değiştirmeyi gerektirirdi. Onun
// yerine Apply'ın RED yol deneniyor: indirme yokken hiçbir dosyaya dokunmadan
// geri çevirmesi beklenir.

#include "../src/core/update.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <windows.h>

static int g_fail = 0;
static int g_pass = 0;

static void Check(bool ok, const char* what)
{
    if (ok) ++g_pass;
    else { ++g_fail; std::printf("  BASARISIZ: %s\n", what); }
}

static void CheckCmp(int got, int want, const char* what)
{
    if (got == want) ++g_pass;
    else { ++g_fail; std::printf("  BASARISIZ: %s (beklenen %d, gelen %d)\n", what, want, got); }
}

static void CheckFail(update::Fail got, update::Fail want, const char* what)
{
    if (got == want) ++g_pass;
    else { ++g_fail; std::printf("  BASARISIZ: %s (beklenen %d, gelen %d)\n",
                                 what, (int)want, (int)got); }
}

// Geçerli bir bildirimin gövdesi: alanları tek tek bozmak için parçaları sabit.
static const char* kHash =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
static const char* kUrl =
    "https://github.com/Memati8383/Tengri/releases/download/v1.3.0/TENGRI.exe";

static std::string Good(int size = 1681920)
{
    std::string j = "{\"version\":\"1.3.0\",\"sha256\":\"";
    j += kHash;
    j += "\",\"size\":";
    j += std::to_string(size);
    j += ",\"url\":\"";
    j += kUrl;
    j += "\",\"notes\":\"## Release\"}";
    return j;
}

// ---------------------------------------------------------------------------
// Sürüm sıralaması
// ---------------------------------------------------------------------------
static void Test_CompareVersions()
{
    std::printf("surum siralamasi\n");

    // Asıl tuzak bu satır: sözlük sırasıyla "1.10.0" < "1.2.0" olurdu ('1' < '2')
    // ve uygulama onuncu sürümünü hiç görmezdi.
    CheckCmp(update::CompareVersions("1.10.0", "1.2.0"),   1, "1.10.0 > 1.2.0");
    CheckCmp(update::CompareVersions("1.2.0",  "1.10.0"), -1, "1.2.0 < 1.10.0");
    CheckCmp(update::CompareVersions("1.9.0",  "1.10.0"), -1, "1.9.0 < 1.10.0");
    CheckCmp(update::CompareVersions("2.0.0",  "1.99.99"), 1, "buyuk ana surum");
    CheckCmp(update::CompareVersions("1.2.3",  "1.2.3"),   0, "esit surum");

    // Eksik bileşen sıfır sayılır: "1.2" ile "1.2.0" aynı sürüm. Yanlış
    // yorumlanırsa ya gereksiz güncelleme önerilir ya da gerçek biri atlanır.
    CheckCmp(update::CompareVersions("1.2", "1.2.0"),   0, "eksik bileşen sıfırdır");
    CheckCmp(update::CompareVersions("1.2.1", "1.2"),   1, "tek bileşen eksik, öndeki büyük");
    CheckCmp(update::CompareVersions("1.2.0.1", "1.2.0"), 1, "dördüncü bileşen");

    // Başındaki 'v' etiketi sürümün parçası değil, GitHub'ın biçimi.
    CheckCmp(update::CompareVersions("v1.3.0", "1.3.0"), 0, "v one eki anlam tasimaz");
    CheckCmp(update::CompareVersions("V1.3.0", "1.2.9"), 1, "buyuk V de taninir");

    // Ön-sürüm kuyruğu yok sayılır: "1.3.0-rc1" -> 1.3.0. Karar bilinçli;
    // releases/latest ön-sürüm döndürmez ama dönse de beta'ya güncellenmeyiz.
    CheckCmp(update::CompareVersions("1.3.0-rc1", "1.2.9"), 1, "rc oncesi buyuk surum");
    CheckCmp(update::CompareVersions("1.2.0-rc1", "1.2.0"), 0, "rc etiketi yok sayilir");

    // Çöp girdiler çökertmemeli, makul bir sonuca varmalı.
    CheckCmp(update::CompareVersions("", ""), 0, "iki bos esit");
    CheckCmp(update::CompareVersions("", "0.0.0"), 0, "bos = sıfırlar");
    CheckCmp(update::CompareVersions("abc", "1.0.0"), -1, "sayisiz metin sıfırdır");
    CheckCmp(update::CompareVersions("1..2", "1.0.2"), 0, "cift nokta bos bilesen");
    CheckCmp(update::CompareVersions("1.2.3.4.5.6.7.8.9.9.9", "1.2.3"), 1, "uzun dizi");

    // Taşma denemesi: 23 hane unsigned long long'u taşırır. Amaç doğru sayı
    // değil, çökmemek.
    CheckCmp(update::CompareVersions("99999999999999999999999", "1"), 1, "tasmada cokmez");
}

// ---------------------------------------------------------------------------
// Özet biçimi
// ---------------------------------------------------------------------------
static void Test_IsHex64()
{
    std::printf("ozet bicimi\n");

    Check(update::IsHex64(kHash), "64 onaltilik kabul");
    Check(update::IsHex64(std::string(64, 'A')), "buyuk harf de kabul");
    Check(!update::IsHex64(""), "bos red");
    Check(!update::IsHex64(std::string(63, 'a')), "63 karakter red");
    Check(!update::IsHex64(std::string(65, 'a')), "65 karakter red");
    Check(!update::IsHex64(std::string(63, 'a') + "g"), "64 ama 'g' iceriyor red");
    Check(!update::IsHex64(std::string(63, 'a') + "-"), "64 ama tire iceriyor red");
    Check(!update::IsHex64(std::string(63, 'a') + " "), "64 ama bosluk red");
}

// ---------------------------------------------------------------------------
// Sunucu allowlist'i
// ---------------------------------------------------------------------------
static void Test_HostAllowed()
{
    std::printf("sunucu allowlist\n");

    Check(update::HostAllowed(kUrl), "github.com kabul");
    Check(update::HostAllowed("https://objects.githubusercontent.com/x/y/z"),
          "objects.githubusercontent.com kabul");
    Check(update::HostAllowed("https://release-assets.githubusercontent.com/x"),
          "release-assets.githubusercontent.com kabul");
    Check(update::HostAllowed("HTTPS://GitHub.com/x"), "buyuk kucuk harf fark etmez");
    Check(update::HostAllowed("https://github.com"), "yol olmadan da kabul");
    Check(update::HostAllowed("https://raw.githubusercontent.com/x"),
          "raw alt alani kuyrukla kabul");

    // Düz HTTP reddedilir: bildirimin bütünlüğü TLS'e bağlıdır.
    Check(!update::HostAllowed("http://github.com/x"), "http reddedilir");

    // Sunucu adı taklitleri. Nokta ZORUNLU: "evil-githubusercontent.com" kuyruğu
    // birebir aynı metni içerir ama bizim alt alanımız değildir.
    Check(!update::HostAllowed("https://evil-githubusercontent.com/x"), "tek parca taklit red");
    Check(!update::HostAllowed("https://github.com.evil.net/x"), "eklenmis domain red");
    Check(!update::HostAllowed("https://evil.com/githubusercontent.com/x"), "yoldaki domain red");
    Check(!update::HostAllowed("https://sub.githubusercontent.com.evil.net/x"),
          "kuyrukta nokta sonrasi taklit red");
    Check(!update::HostAllowed("https://githubusercontent.com"), "kuyruksuz uyaniklik red");

    // Kullanıcı adı bölümü hilesi: "https://github.com@evil.net/". Ayrıştırma
    // '@'ı host'un parçası sayarsa sunucu github.com sanılır.
    Check(!update::HostAllowed("https://github.com@evil.net/x"), "kullanici adi hilesi red");

    Check(!update::HostAllowed(""), "bos adres red");
    Check(!update::HostAllowed("https:///x"), "hostsuz adres red");
    Check(!update::HostAllowed("ftp://github.com/x"), "ftp red");
    Check(!update::HostAllowed("github.com/x"), "semasiz red");
    Check(!update::HostAllowed("https:/github.com/x"), "tek slash red");
    Check(!update::HostAllowed("https://api.github.com/x"), "api alt alani listeye gerek yok");
    // Port host'un parçası değildir: "github.com:22"in sunucusu hâlâ
    // github.com'dur. Bağlantı zaten kurulmayacağı için reddetmek gerekmiyor.
    Check(update::HostAllowed("https://github.com:22/x"), "portlunun host kismi kabul");
}

// ---------------------------------------------------------------------------
// Bildirim (latest.json) ayrıştırması - geçerli
// ---------------------------------------------------------------------------
static void Test_ParseManifest_Good()
{
    std::printf("bildirim ayristirmasi - gecerli\n");

    update::Latest m;
    update::Fail why = update::Fail::None;
    Check(update::ParseManifest(Good(), &m, &why), "gecerli bildirim okunur");
    CheckFail(why, update::Fail::None, "neden yok");
    Check(m.version == "1.3.0", "surum dogru");
    Check(m.sha256 == kHash, "ozet dogru");
    Check(m.size == 1681920ull, "boyut dogru");
    Check(m.url == kUrl, "adres dogru");
    Check(m.notes == "## Release", "not alani okundu");

    // Alan sırası serbest ve tanımsız alanlar atlanır: bildirime yarın yeni bir
    // alan eklenirse eski uygulama patlamamalı, yok saymalı.
    update::Latest m2;
    {
        const std::string j = std::string(
            "{\"size\":2048,\"unknown\":{\"deep\":[1,2,{\"b\":3}]},\"n\":null,\"t\":true,"
            "\"version\":\"1.3.1\",\"url\":\"https://github.com/a/b\","
            "\"sha256\":\"") + kHash + "\"}";
        Check(update::ParseManifest(j, &m2, &why), "tanimsiz alanlar atlanır");
        Check(m2.version == "1.3.1", "alan sirasi serbest");
        Check(m2.size == 2048ull, "sayi alani");
    }

    // Büyük harfli özet küçüğe çevrilir; karşılaştırma küçük harf üzerinden
    // yapılır. Sadece özetin kendisi büyütülüyor: anahtar adları da büyürse
    // alan tanınmaz olur ve test yanlış yerde geçer.
    update::Latest m3;
    {
        std::string buyuk = kHash;
        for (auto& c : buyuk) c = (char)toupper((unsigned char)c);
        const std::string j = std::string("{\"version\":\"1.3.0\",\"size\":1681920,\"url\":\"") +
            kUrl + "\",\"sha256\":\"" + buyuk + "\"}";
        Check(update::ParseManifest(j, &m3, &why), "buyuk harf ozet kabul");
        Check(m3.sha256 == kHash, "ozet kucuge cevrildi");
    }

    // CI düz ve girintili yazıyor: boşluk/yeni satır toleransı.
    update::Latest m4;
    const std::string girintili = std::string(
        "\n{\n  \"version\" : \"2.0.0\",\n  \"size\": 1024,\n"
        "  \"url\": \"https://github.com/x\",\n  \"sha256\": \"") + kHash + "\"\n}\n";
    Check(update::ParseManifest(girintili, &m4, &why), "izgiarli bicim okunur");
    Check(m4.version == "2.0.0" && m4.size == 1024ull, "izgiarli bicimde degerler");

    // Aynı ad iki kez gelirse sonuncusu kazanır (JSON yazarlarının ortak davranışı).
    update::Latest m5;
    const std::string iki = std::string("{\"version\":\"1.0.0\",\"version\":\"1.4.0\",") +
        "\"size\":1024,\"url\":\"https://github.com/x\",\"sha256\":\"" + kHash + "\"}";
    Check(update::ParseManifest(iki, &m5, &why), "cift alan okunur");
    Check(m5.version == "1.4.0", "son yazilan kazanir");

    // Bozuk olmayan ama boş not: alan yok sayılır, sürüm yine kullanılır.
    update::Latest m6;
    const std::string notsuz = std::string("{\"version\":\"1.3.0\",\"size\":1024,") +
        "\"url\":\"https://github.com/x\",\"sha256\":\"" + kHash + "\"}";
    Check(update::ParseManifest(notsuz, &m6, &why), "notesuz bildirim kabul");
    Check(m6.notes.empty(), "not bos kalarak okunur");
}

// ---------------------------------------------------------------------------
// Bildirim ayrıştırması - dize tuzakları
// ---------------------------------------------------------------------------
static void Test_ParseManifest_StringTraps()
{
    std::printf("bildirim ayristirmasi - dize tuzaklari\n");

    // En önemli test. Sürüm notu JSON'un İÇİNE gömülü metindir ve içinde
    // tırnak, küme parantezi ve "version": gibi görünen diziler var. Dize içi
    // ayrıştırılırsa not'taki sahte sürüm gerçek sürüm olur.
    update::Latest m;
    update::Fail why = update::Fail::None;
    const std::string tuzak = std::string("{\"version\":\"1.3.0\",\"size\":1681920,\"url\":\"") +
        kUrl + "\",\"sha256\":\"" + kHash + "\","
        "\"notes\":\"## TENGR\\u0130\\n\\nAyar **iki** yeni ekran: \\\"version\\\": "
        "\\\"9.9.9\\\" diye yazan bir not, { ve } karakterleri, ka\\u00e7\\u0131\\u015fl\\u0131 "
        "\\\\\\\\ ters bolu ve satir sonu\\n bitti.\"}";
    Check(update::ParseManifest(tuzak, &m, &why), "tuzakli not ayristirilir");
    Check(m.version == "1.3.0", "not icindeki sahte surum aldatmadi");
    Check(m.notes.find("9.9.9") != std::string::npos, "not metni ic iceriyor");
    Check(m.notes.find("\"version\"") != std::string::npos, "not icindeki tirnak korundu");
    Check(m.notes.find('\n') != std::string::npos, "satir sonu cozuldu");
    Check(m.notes.find('\\') != std::string::npos, "ters bolu cozuldu");
    Check(m.notes.find("{") != std::string::npos && m.notes.find("}") != std::string::npos,
          "kume parantezleri metin olarak kaldi");
    // TENGRİ: \u0130 = İ = C4 B0? (U+0130 -> C4 B0)
    Check(m.notes.find("\xC4\xB0") != std::string::npos, "Türkçe İ dogru cozuldu");

    // Unicode kaçışları ve vekil ikilisi.
    update::Latest u;
    const std::string uni = std::string("{\"version\":\"1.3.0\",\"size\":1681920,\"url\":\"") +
        kUrl + "\",\"sha256\":\"" + kHash +
        "\",\"notes\":\"\\u00e9 \\u2022 \\u4e2d \\ud83d\\ude00\"}";
    Check(update::ParseManifest(uni, &u, &why), "unicode kacislari okunur");
    // é = C3 A9, • = E2 80 A2, 中 = E4 B8 AD, grinning face = F0 9F 98 80
    Check(u.notes == "\xC3\xA9 \xE2\x80\xA2 \xE4\xB8\xAD \xF0\x9F\x98\x80",
          "utf-8'e dogru cevrildi");

    // Yetim alt vekil: tanımsız karakter. Bildirim bozuk sayılmaz (yerine
    // bilinmeyen işaret konur) ama çökmez; buradaki asıl soru ikinci bir
    // bilinemeyen kaçışın reddi.
    const std::string yetim = std::string("{\"version\":\"1.3.0\",\"size\":1681920,\"url\":\"") +
        kUrl + "\",\"sha256\":\"" + kHash + "\",\"notes\":\"\\uDC00\"}";
    Check(update::ParseManifest(yetim, &u, &why), "yetim vekil cokertmez");
    Check(u.notes == "\xEF\xBF\xBD", "yetim vekil bilinmeyen isarete donustu");

    const std::string bilinmeyen = std::string("{\"version\":\"1.3.0\",\"size\":1681920,\"url\":\"") +
        kUrl + "\",\"sha256\":\"" + kHash + "\",\"notes\":\"a\\qb\"}";
    Check(!update::ParseManifest(bilinmeyen, &u, &why), "bilinmeyen kacis reddedilir");
    CheckFail(why, update::Fail::BadManifest, "bilinmeyen kacis -> BadManifest");

    const std::string kapanmayan = std::string("{\"version\":\"1.3.0\",\"size\":1681920,\"url\":\"") +
        kUrl + "\",\"sha256\":\"" + kHash + "\",\"notes\":\"acik";
    Check(!update::ParseManifest(kapanmayan, &u, &why), "kapanmayan dize red");
}

// ---------------------------------------------------------------------------
// Bildirim ayrıştırması - bozuk girdi
// ---------------------------------------------------------------------------
static void Test_ParseManifest_Bad()
{
    std::printf("bildirim ayristirmasi - bozuk girdi\n");

    update::Latest m;
    update::Fail why = update::Fail::None;

    auto bad = [&](const std::string& j, const char* what)
    {
        why = update::Fail::None;
        Check(!update::ParseManifest(j, &m, &why), what);
        CheckFail(why, update::Fail::BadManifest, what);
    };

    // Ağır ama olası: yanıt bir HTML hata sayfası ya da boş dönebilir.
    bad("", "bos yanit red");
    bad("<html>502 Bad Gateway</html>", "html yanit red");
    bad("version=1.3.0", "duz metin red");
    bad("[1,2,3]", "dizi koku red");
    bad("null", "null koku red");
    bad("{", "tek kume red");
    bad("{\"version\"", "yani yarim alan red");
    bad("{\"version\":}", "değersiz alan red");
    bad("{\"version\":1.0,,}", "cift virgul red");
    bad("{\"version\":1.0}", "alan değeri sayı (Str beklenir) red");

    // BOM: CI BOM'suz yazıyor; baştaki üç bayt ilk anahtarı okunamaz yapar.
    bad(std::string("\xEF\xBB\xBF") + Good(), "BOM'lu bildirim red");

    // Zorunlu alanların yokluğu teker teker.
    {
        std::string j = Good();
        j = j.replace(j.find("\"version\":\"1.3.0\","), 20, "");
        bad(j, "surum yok red");
    }
    {
        std::string j = std::string("{\"version\":\"1.3.0\",\"size\":1681920,\"url\":\"") + kUrl + "\"}";
        bad(j, "ozet yok red");
    }
    {
        std::string j = std::string("{\"version\":\"1.3.0\",\"size\":1681920,\"sha256\":\"") + kHash + "\"}";
        bad(j, "adres yok red");
    }
    {
        std::string j = std::string("{\"version\":\"1.3.0\",\"url\":\"") + kUrl +
                        "\",\"sha256\":\"" + kHash + "\"}";
        bad(j, "boyut yok red");
    }

    // Var ama anlamsız.
    bad(std::string("{\"version\":\"\",\"size\":1681920,\"url\":\"") + kUrl +
        "\",\"sha256\":\"" + kHash + "\"}", "bos surum red");
    bad(std::string("{\"version\":\"1.3.0\",\"size\":1681920,\"url\":\"") + kUrl +
        "\",\"sha256\":\"deadbeef\"}", "kisa ozet red");
    bad(std::string("{\"version\":\"1.3.0\",\"size\":1681920,\"url\":\"") + kUrl +
        "\",\"sha256\":\"" + std::string(63, 'z') + "a\"}", "onaltilik olmayan ozet red");
    bad(std::string("{\"version\":\"1.3.0\",\"size\":0,\"url\":\"") + kUrl +
        "\",\"sha256\":\"" + kHash + "\"}", "sifir boyut red");
    bad(std::string("{\"version\":\"1.3.0\",\"size\":\"1681920\",\"url\":\"") + kUrl +
        "\",\"sha256\":\"" + kHash + "\"}", "boyut dize olarak verilemez");
    bad(std::string("{\"version\":\"1.3.0\",\"size\":512,\"url\":\"") + kUrl +
        "\",\"sha256\":\"" + kHash + "\"}", "1 KB altindaki boyut red");
    bad(std::string("{\"version\":\"1.3.0\",\"size\":300000000,\"url\":\"") + kUrl +
        "\",\"sha256\":\"" + kHash + "\"}", "200 MB ustundeki boyut red");

    // Sunucu tanınmıyor: neden BadManifest değil TrustHost. "Bildirim bozuk" ile
    // "bizi başka bir yere çekiyor" arayüzde farklı anlatılır.
    why = update::Fail::None;
    Check(!update::ParseManifest(std::string("{\"version\":\"1.3.0\",\"size\":1681920,\"url\":\"") +
        "http://github.com/x\",\"sha256\":\"" + kHash + "\"}", &m, &why), "http indirime red");
    CheckFail(why, update::Fail::TrustHost, "http adres -> TrustHost");
    why = update::Fail::None;
    Check(!update::ParseManifest(std::string("{\"version\":\"1.3.0\",\"size\":1681920,\"url\":\"") +
        "https://evil.net/x\",\"sha256\":\"" + kHash + "\"}", &m, &why), "taninmayan sunucu red");
    CheckFail(why, update::Fail::TrustHost, "taninmayan sunucu -> TrustHost");

    // İç içe 80 katman: ayrıştırıcı yığın tüketimini durdurur.
    {
        std::string derin;
        for (int i = 0; i < 80; ++i) derin += "{\"a\":";
        derin += "1";
        for (int i = 0; i < 80; ++i) derin += "}";
        derin = std::string("{\"version\":\"1.3.0\",\"size\":1681920,\"url\":\"") + kUrl +
                "\",\"sha256\":\"" + kHash + "\",\"nest\":" + derin + "}";
        Check(!update::ParseManifest(derin, &m, &why), "asiri derinlik red");
    }

    // Sonuç alanı başarısızlıkta temiz kalmalı: yarım ayrışmış bir bildirim
    // arayüzde "yeni sürüm var" olarak görünebilir.
    why = update::Fail::None;
    m.version = "9.9.9";
    Check(!update::ParseManifest("{\"version\":\"1.0.0\"", &m, &why), "bozuk girdi red");
    Check(m.version.empty(), "red edildikten sonra alan temizlenir");
}

// ---------------------------------------------------------------------------
// SHA-256
// ---------------------------------------------------------------------------
static void Test_Sha256()
{
    std::printf("sha-256\n");

    // Bilinen sınama vektörleri. BCrypt'in kendisini değil, bizim sarmalayıcıyı
    // (akış, onaltılık dizim, boş girdi) doğruluyor.
    const char* empty = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    const char* abc   = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    const char* hello = "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824";

    Check(update::Sha256Hex("", 0) == empty, "bos girdi ozeti");
    Check(update::Sha256Hex("abc", 3) == abc, "abc ozeti");
    Check(update::Sha256Hex("hello", 5) == hello, "hello ozeti");
    Check(update::Sha256Hex("abc", 3).size() == 64, "ozet 64 karakter");
    Check(update::IsHex64(update::Sha256Hex("abc", 3)), "ozet onaltilik biciminde");
    Check(update::Sha256Hex("abc", 3) != update::Sha256Hex("abd", 3), "tek bayt farki");

    // Uzunluk verildiği için gömülü nul içeriğe dahil olmalı; C dizisi gibi
    // kesse iki farklı girdi aynı özeti verirdi.
    Check(update::Sha256Hex("a\0b", 3) != update::Sha256Hex("a", 1), "nul bayti icerikte");

    // 1 MB'lık tek blok: modül dosyayı 1 MB'lık dilimlerle dolaşıyor.
    {
        const std::string big(1024 * 1024, 'x');
        Check(update::IsHex64(update::Sha256Hex(big.data(), big.size())), "1 MB blok ozetlendi");
    }

    // Dosya yolu ile bellek yolu AYNI çekirdeği paylaşmalı: indirme dosyadan
    // doğrulanıyor, arada sapma olursa takas yanlış karar verir.
    {
        wchar_t tmp[MAX_PATH] = {};
        ::GetTempPathW(ARRAYSIZE(tmp), tmp);
        wchar_t name[MAX_PATH] = {};
        ::GetTempFileNameW(tmp, L"tgr", 0, name);
        const std::wstring path = name;

        HANDLE f = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE)
        {
            DWORD put = 0;
            ::WriteFile(f, "abc", 3, &put, nullptr);
            ::CloseHandle(f);

            std::string hex;
            Check(update::Sha256File(path, &hex), "dosya ozeti cikarildi");
            Check(hex == abc, "dosya ozeti bellek ozetiyle ayni");

            // Boş dosya: okunabilir ama özeti "empty" vektörüdür. İndirme
            // sonunda sıfır bayt yazılmışsa bu yolla yakalanır.
            ::SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
            f = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                              TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            ::CloseHandle(f);
            Check(update::Sha256File(path, &hex) && hex == empty, "bos dosya ozeti");

            ::SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
            ::DeleteFileW(path.c_str());
            Check(!::DeleteFileW(path.c_str()), "gecici dosya silindi (kalinti yok)");
        }
        else
        {
            Check(false, "gecici dosya olusturulamadi");
        }

        std::string yok;
        Check(!update::Sha256File(L"C:\\tengri-olmeyen-9182\\TENGRI.bin", &yok), "olmeyen dosya red");
        Check(yok.empty(), "olmeyen dosyada bos sonuc");
    }
}

// ---------------------------------------------------------------------------
// Zamanlama ve yol türetme
// ---------------------------------------------------------------------------
static void Test_TimingAndPaths()
{
    std::printf("zamanlama ve yollar\n");

    const unsigned long long D = 24ull * 3600ull;
    Check(update::IsDue(0, 1000, D), "hice denenmemis -> gerekiyor");
    Check(!update::IsDue(1000, 1000 + D - 1, D), "aralik dolmadi");
    Check(update::IsDue(1000, 1000 + D, D), "aralik tam doldu");
    Check(update::IsDue(1000, 1000 + D + 1, D), "aralik gecti");
    // Saat geri alınırsa "gelecekte denenmiş" olur; denetimi bir daha hiç
    // çalıştırmamalı değil, hemen çalıştırmalı.
    Check(update::IsDue(500000, 1000, D), "saat geri alinmis -> gerekiyor");
    Check(update::IsDue(1000, 1000, 0), "sifir aralik her zaman dolu");

    const update::SwapPlan p = update::PlanFor(L"C:\\Yayin\\TENGRI.exe");
    Check(p.running == L"C:\\Yayin\\TENGRI.exe", "calisan yol aynen tasindi");
    Check(p.stale == L"C:\\Yayin\\TENGRI.exe.old", "kenara alinan ad turetildi");

    // Boş yol: plan üretilmez; çağıran bunu "yapamayız" sayar.
    const update::SwapPlan e = update::PlanFor(L"");
    Check(e.running.empty() && e.stale.empty(), "bos yoldan plan uretilmez");

    // Boşluklu ve noktalı adlar: türetme sona ekler, bölemez.
    Check(update::PlanFor(L"D:\\Program Files\\TENGRI 1.2.0\\TENGRI.exe").stale
              == L"D:\\Program Files\\TENGRI 1.2.0\\TENGRI.exe.old",
          "bosluKlu yolda ek ayni");

    // Kenara alınmış ad üst üste biner ama çakışmaz: ikinci takas birincinin
    // kalıntısının üzerine yazar (ReplaceExisting), dosya adı sabit.
    Check(update::PlanFor(L"C:\\x\\TENGRI.exe.old").stale == L"C:\\x\\TENGRI.exe.old.old",
          "ust uste binen kenar adi");
}

// ---------------------------------------------------------------------------
// Modül durumu: yalnızca yerel işaretçiler, ne ağ ne registry
// ---------------------------------------------------------------------------
static void Test_StateDefaults()
{
    std::printf("durum varsayilanlari\n");

    // LoadPrefs çağrılmadığı için son denetim zamanı 0'dır. Ayar kapalıysa
    // hiçbir zaman "denetle" denmemeli: anahtarın gerçekten işi durdurduğunun
    // testi ağa çıkmadan ancak buradan yapılabilir.
    update::enabled = false;
    Check(!update::DueForCheck(), "kapali ayarda denetim istenmez");

    update::enabled = true;
    Check(update::DueForCheck(), "acik ayarda, hic denenmemisse istenir");

    Check(update::Get() == update::State::Idle, "ilkin durum bos");
    Check(update::StageGet() == update::DlState::Idle, "ilkin indirme bos");
    Check(update::StagedBytes() == 0, "inmis bayt yok");
    Check(update::StagedTotal() == 0, "beklenen bayt yok");
    Check(update::StagedFile().empty(), "dosya yolu yok");
    Check(update::Snapshot().version.empty(), "uzak surum henuz yok");
    Check(update::Reason() == update::Fail::None, "hata nedeni yok");

    // Idle durumda sıfırlama sessiz geçmeli: arayüz "vazgeç"e iki kez
    // basılabilir ve ikincisi çökme ya da yarım dosya bırakmamalı.
    update::ResetStage();
    update::ResetStage();
    Check(update::StageGet() == update::DlState::Idle, "bos sifirlama durumu bozmaz");
    Check(update::StagedFile().empty(), "bos sifirlama yol uretmez");

    // Apply yalnızca doğrulanmış indirmenin ardından dosyaya dokunur. İndirme
    // yokken reddedilmeli; reddedilmezse test kendi exe'sinin adını
    // değiştirirdi - sözleşme ihlali.
    const update::SwapPlan p = update::PlanFor(L"C:\\tengri-olmeyen-9182\\TENGRI.exe");
    Check(!update::Apply(p), "indirme olmadan takas reddedilir");
    CheckFail(update::Reason(), update::Fail::Io, "red nedeni: hazirlik yok");

    // Redden sonra da hiçbir şey yazılmamış olmalı: hedef klasör yok.
    DWORD attrs = ::GetFileAttributesW(L"C:\\tengri-olmeyen-9182");
    Check(attrs == INVALID_FILE_ATTRIBUTES, "red takasta klasor olusmadi");

    // Boş planla da denenebilir ve yine reddedilir.
    Check(!update::Apply(update::SwapPlan()), "bos planla takas reddedilir");
}

int main()
{
    std::printf("=== update testleri ===\n");

    Test_CompareVersions();
    Test_IsHex64();
    Test_HostAllowed();
    Test_ParseManifest_Good();
    Test_ParseManifest_StringTraps();
    Test_ParseManifest_Bad();
    Test_Sha256();
    Test_TimingAndPaths();
    Test_StateDefaults();

    std::printf("--- %d denetim, %d hata ---\n", g_pass, g_fail);
    if (g_fail) { std::printf("GUNCELLEME TESTLERI BASARISIZ\n"); return 1; }
    return 0;
}
