// Yeni eklenen modüllerin testleri: restore, startup, services, shader temizleyici.
//
// test_pure.cpp ile aynı sözleşme: registry YAZMAZ, dosya SİLMEZ, hizmet durumu
// DEĞİŞTİRMEZ, geri yükleme noktası OLUŞTURMAZ. Yalnızca salt-okunur yüzeyler ve
// yetki/istemeden önce reddeden güvenlik kapıları denenir. Bu yüzden bu test
// yükseltilmemiş bir süreçte de, yönetici olarak da aynı sonucu vermelidir:
//   - CreatePoint çağrılmaz (yükseltilmişse gerçek nokta yaratırdı).
//   - SetEnabled/Remove çağrılmaz (HKLM/UserRun yazardı).
//   - ApplyGameProfile/RestoreGameProfile/SetStartType(beyaz-liste-içi) çağrılmaz.
//   - CleanShader çağrılmaz (dosya silerdi).
// SetStartType yalnızca BEYAZ-LİSTE-DIŞI adla çağrılır: FindPolicy ilk kapı olduğu
// için SCM'ye hiç dokunmadan false döner — bu, kritik hizmetlerin kapatılamayacağını
// kanıtlayan anlamlı bir güvenlik testidir ve yükseltilmiş durumda bile yan etkisizdir.

#include "../src/core/restore.hpp"
#include "../src/core/startup.hpp"
#include "../src/core/services.hpp"
#include "../src/core/cleaner.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_fail = 0;
static int g_pass = 0;

static void Check(bool ok, const char* what)
{
    if (ok) ++g_pass;
    else { ++g_fail; std::printf("  BASARISIZ: %s\n", what); }
}

static void CheckEq(long long got, long long want, const char* what)
{
    if (got == want) ++g_pass;
    else { ++g_fail; std::printf("  BASARISIZ: %s (beklenen %lld, gelen %lld)\n", what, want, got); }
}

// --------------------------------------------------------------------------- restore

static void Test_Restore()
{
    std::printf("geri yukleme noktasi (salt-okunur)\n");

    // ResultText: her durum dolu ve birbirinden farkli bir metin vermeli. Ayni
    // metin iki duruma dusurse log/bildirim teshis edilemez hale gelir.
    const restore::Result tum[] = {
        restore::Result::Created, restore::Result::Disabled,
        restore::Result::NotElevated, restore::Result::ServiceUnavailable,
        restore::Result::Throttled, restore::Result::Failed,
    };
    int bos = 0;
    for (const auto& r : tum)
    {
        const wchar_t* t = restore::ResultText(r);
        if (!t || !*t) ++bos;
    }
    CheckEq(bos, 0, "tum Result() durumlari dolu metin");

    // Ikili farklilik.
    int cakisma = 0;
    for (size_t i = 0; i < 6; ++i)
        for (size_t j = i + 1; j < 6; ++j)
            if (std::wcscmp(restore::ResultText(tum[i]), restore::ResultText(tum[j])) == 0)
                ++cakisma;
    CheckEq(cakisma, 0, "durum metinleri birbirinden farkli");

    // Cagirmadan once son olusturma damgasi sifir olmali (surec icinde hic
    // CreatePoint cagrilmadi).
    CheckEq((long long)restore::LastCreatedTick(), 0, "CreatePoint oncesi damga 0");

    // IsProtectionEnabled salt-okunur; cagirmak cokmemeli ve tutarli olmali.
    const bool ilk = restore::IsProtectionEnabled();
    const bool ikinci = restore::IsProtectionEnabled();
    CheckEq((long long)ilk, (long long)ikinci, "IsProtectionEnabled tutarli");
}

// --------------------------------------------------------------------------- services

// Beyaz liste disinda bir adle SetStartType cagirmak, SCM'ye dokunmadan false
// dondurur (FindPolicy ilk kapidir). Kritik Windows hizmetleri buradadir; bunlardan
// birinin yanlislikla listelenmesi, oyun profilinin sistemi bozmasina yol acarardi.
static void Test_Services_Whitelist()
{
    std::printf("hizmet beyaz listesi (guvenlik kapisi)\n");

    const services::Policy* pol = nullptr;
    int count = 0;
    services::GetPolicy(&pol, &count);
    Check(count > 0, "beyaz liste bos degil");

    // Her girdi: ad dolu, hedef yalnizca Manual ya da Disabled (profil hicbir
    // zaman bir hizmetin baslangic onceligini YUKSELTMEMELI).
    int adBos = 0, hedefGecersiz = 0;
    for (int i = 0; i < count; ++i)
    {
        if (!pol[i].serviceName || !*pol[i].serviceName) ++adBos;
        if (pol[i].gameProfile != services::StartType::Manual &&
            pol[i].gameProfile != services::StartType::Disabled)
            ++hedefGecersiz;
    }
    CheckEq(adBos, 0, "beyaz liste adlari dolu");
    CheckEq(hedefGecersiz, 0, "oyun profili yalnizca Manual/Disabled hedefler");

    // Kritik hizmetler beyaz listede OLMAMALI.
    const wchar_t* kritik[] = {
        L"RpcSs", L"Dhcp", L"Winmgmt", L"BFE", L"mpssvc",
        L"LanmanServer", L"LanmanWorkstation", L"SchedSvc", L"Schedule",
        L"RpcLocator", L"DcomLaunch", L"EventLog", L"LSM", L"termservice",
    };
    int kritikListede = 0;
    for (const wchar_t* k : kritik)
        for (int i = 0; i < count; ++i)
            if (std::wcscmp(pol[i].serviceName, k) == 0) ++kritikListede;
    CheckEq(kritikListede, 0, "kritik hizmetler beyaz listede degil");

    // Beyaz liste disi ad -> reddedilir, SCM'ye dokunulmaz. Yukseltilmis süreçte
    // de ayni sonucu vermeli: FindPolicy, yetki denetinden once calisir.
    Check(!services::SetStartType(L"RpcSs", services::StartType::Disabled), "RpcSs reddedildi");
    Check(!services::SetStartType(L"Winmgmt", services::StartType::Disabled), "Winmgmt reddedildi");
    Check(!services::SetStartType(L"", services::StartType::Disabled), "bos ad reddedildi");
    Check(!services::SetStartType(L"nonexistent_svc_xyz", services::StartType::Disabled), "listesi disi ad reddedildi");

    // StartTypeLabel tum degerler dolu.
    const services::StartType turler[] = {
        services::StartType::Boot, services::StartType::System,
        services::StartType::Automatic, services::StartType::AutomaticDelayed,
        services::StartType::Manual, services::StartType::Disabled,
        services::StartType::Unknown,
    };
    int etiketBos = 0;
    for (const auto& t : turler)
    {
        const wchar_t* s = services::StartTypeLabel(t);
        if (!s || !*s) ++etiketBos;
    }
    CheckEq(etiketBos, 0, "StartTypeLabel tum degerlerde dolu");
}

// Query() salt-okunur SCM sorgusu. Cokmemeli; size beyaz listeyle birebir olmali
// ve her Entry.policy beyaz liste icine isaret etmeli.
static void Test_Services_Query()
{
    std::printf("hizmet sorgusu (salt-okunur)\n");

    const services::Policy* pol = nullptr;
    int count = 0;
    services::GetPolicy(&pol, &count);

    std::vector<services::Entry> e = services::Query();
    CheckEq((long long)e.size(), (long long)count, "Query boyutu beyaz listeye esit");

    int hataliIsaretci = 0, adUyumsuz = 0;
    for (const services::Entry& en : e)
    {
        if (!en.policy) { ++hataliIsaretci; continue; }
        // policy isaretcisi beyaz liste dizisi icinde mi?
        const intptr_t off = en.policy - pol;
        if (off < 0 || off >= count) { ++hataliIsaretci; continue; }
        if (std::wcscmp(en.serviceName.c_str(), en.policy->serviceName) != 0) ++adUyumsuz;
    }
    CheckEq(hataliIsaretci, 0, "her Entry.policy beyaz liste icinde");
    CheckEq(adUyumsuz, 0, "Entry adi policy adiyla ayni");
}

// --------------------------------------------------------------------------- startup

static void Test_Startup()
{
    std::printf("baslangic yoneticisi (salt-okunur)\n");

    // ScopeLabel: 10 kapsam dolu ve ikili farkli. Ayni etiket iki kapsama
    // düşerse kullanıcı hangi anahtara dokunduğunu anlamaz.
    const startup::Scope kapsamlar[] = {
        startup::Scope::UserRun, startup::Scope::UserRunOnce,
        startup::Scope::MachineRun, startup::Scope::MachineRunOnce,
        startup::Scope::PolicyExplorerRun, startup::Scope::PolicyExplorerRunMachine,
        startup::Scope::StartupFolderUser, startup::Scope::StartupFolderCommon,
        startup::Scope::WinlogonUser, startup::Scope::WinlogonMachine,
    };
    int cakisma = 0, bos = 0;
    for (auto s : kapsamlar)
    {
        const wchar_t* lbl = startup::ScopeLabel(s);
        if (!lbl || !*lbl) ++bos;
    }
    for (int i = 0; i < 10; ++i)
        for (int j = i + 1; j < 10; ++j)
            if (std::wcscmp(startup::ScopeLabel(kapsamlar[i]),
                            startup::ScopeLabel(kapsamlar[j])) == 0) ++cakisma;
    CheckEq(bos, 0, "ScopeLabel tum kapsamlarda dolu");
    CheckEq(cakisma, 0, "ScopeLabel kapsamlari ayirt ediyor");

    // Kapatilabilirlik kapisi: Winlogon'da oturumun kendisi olan degerler
    // kapatilamaz. Kapi acilirsa bir tiklama oturumu acilmayaz yapar ve
    // geri almak baska bir oturum gerektirir.
    Check(startup::IsToggleable(startup::Scope::UserRun), "Run kapatilabilir");
    Check(startup::IsToggleable(startup::Scope::PolicyExplorerRunMachine),
          "Policy Explorer Run kapatilabilir");
    Check(startup::IsToggleable(startup::Scope::StartupFolderUser),
          "kullanici baslangic klasoru kapatilabilir");
    Check(startup::IsToggleable(startup::Scope::StartupFolderCommon),
          "ortak baslangic klasoru kapatilabilir");
    Check(!startup::IsToggleable(startup::Scope::WinlogonUser),  "HKCU Winlogon salt-okunur");
    Check(!startup::IsToggleable(startup::Scope::WinlogonMachine), "HKLM Winlogon salt-okunur");

    // Yetki kapisi: HKLM ve ortak Startup klasoru yonetici ister, kullanici
    // tarafi ve salt-okunur Winlogon istemez.
    Check(startup::NeedsElevation(startup::Scope::MachineRun), "HKLM Run yetki ister");
    Check(startup::NeedsElevation(startup::Scope::PolicyExplorerRunMachine), "HKLM Policy Run yetki ister");
    Check(startup::NeedsElevation(startup::Scope::StartupFolderCommon), "ortak Startup klasoru yetki ister");
    Check(!startup::NeedsElevation(startup::Scope::StartupFolderUser), "kullanici Startup klasoru yetki istemez");
    Check(!startup::NeedsElevation(startup::Scope::WinlogonUser), "HKCU Winlogon yetki istemez");

    // Enumerate() salt-okunur tarama: bu makinedeki gercek Run/RunOnce anahtarlarini
    // okur, hicbir sey yazmaz/silmez.
    std::vector<startup::Entry> entries = startup::Enumerate();

    int adBos = 0, kapsamGecersiz = 0, etkiGecersiz = 0, tirnakKalin = 0;
    int winlogonSizinti = 0, abnormalHafif = 0;
    for (const startup::Entry& en : entries)
    {
        if (en.name.empty()) ++adBos;
        const int sk = (int)en.scope;
        if (sk < 0 || sk >= (int)startup::Scope::Count) ++kapsamGecersiz;
        const int im = (int)en.impact;
        if (im < 0 || im > 3) ++etkiGecersiz;

        // Kapatilamayan kapsamda yalnizca Shell/Userinit gorunur: Winlogon
        // anahtarinda "Taskman", "GINA" gibi baslangic olmayan kirk deger daha
        // vardir ve hepsini gostermek listeyi kullanilamaz yapar.
        if (!startup::IsToggleable((startup::Scope)sk) &&
            en.name != L"Shell" && en.name != L"Userinit")
            ++winlogonSizinti;

        // Sapmali bir Winlogon degeri her zaman yuksek etkilidir: oturumu
        // ele gecirir. Etki tahmininin altinda kalmasi sessizce hafif gosterir.
        if (en.abnormal && im != (int)startup::Impact::High) ++abnormalHafif;

        // resolvedPath, ParseExePath'in tırnakları soyulmus hali olmali; kenarda
        // tırnak kalirsaysa ayristirma bozuk demektir.
        if (!en.resolvedPath.empty() &&
            (en.resolvedPath.front() == L'"' || en.resolvedPath.back() == L'"'))
            ++tirnakKalin;
    }
    CheckEq(adBos, 0, "her girinin adi dolu");
    CheckEq(kapsamGecersiz, 0, "kapsam degerleri gecerli aralikta");
    CheckEq(etkiGecersiz, 0, "etki degerleri gecerli aralikta");
    CheckEq(tirnakKalin, 0, "ayristirilmis yolda tırnak kalmadi");
    CheckEq(winlogonSizinti, 0, "Winlogon kapsaminda yalnizca Shell/Userinit listeleniyor");
    CheckEq(abnormalHafif, 0, "sapan deger daima yuksek etkili isaretleniyor");

    // TENGRI kendi kaydini tanimali: eger HKCU Run'da TENGRI degeri varsa ve
    // bu surec uygulamanin kendisi ise isTengri dogru olur; aksi halde en azindan
    // isaretlemeli. Yanlis-pozitif yerine tutarliliga bakiyoruz: isTengri=true olan
    // bir girinin adinda ya da komutunda TENGRI gecebilir.
    int tengriTutarsiz = 0;
    for (const startup::Entry& en : entries)
    {
        if (!en.isTengri) continue;
        const bool adUyuyor = en.name.find(L"TENGRI") != std::wstring::npos;
        const bool komutUyuyor = en.command.find(L"TENGRI") != std::wstring::npos ||
                                 en.command.find(L"ENGR") != std::wstring::npos;
        if (!adUyuyor && !komutUyuyor) ++tengriTutarsiz;
    }
    CheckEq(tengriTutarsiz, 0, "isTengri girileri TENGR ieriyor");
}

// --------------------------------------------------------------------------- shader

static void Test_ShaderScan()
{
    std::printf("shader cache taramasi (salt-okunur)\n");

    // Bos maske: hicbir yol taranmamali.
    cleaner::ScanResult bos = cleaner::ScanShader(0);
    CheckEq((long long)(bos.sizeMB == 0.0), 1, "bos maske -> 0 MB");
    CheckEq(bos.fileCount, 0, "bos maske -> 0 dosya");

    // Tekil maske negatif olmamali; salt okur, silme yok.
    cleaner::ScanResult d3d = cleaner::ScanShader(cleaner::Shader_D3D);
    Check(d3d.sizeMB >= 0.0, "D3D tarama boyutu negatif degil");
    Check(d3d.fileCount >= 0, "D3D tarama dosya sayisi negatif degil");

    // Birlesik maske, parcalarini icermeli (yollar ayrık, boyut toplanir):
    // Shader_All, Shader_D3D'yi de kapsadigindan dosya sayisi >= olmali.
    cleaner::ScanResult all = cleaner::ScanShader(cleaner::Shader_All);
    Check(all.fileCount >= d3d.fileCount, "Shader_All dosyalari >= D3D");
    Check(all.sizeMB >= d3d.sizeMB - 0.001, "Shader_All boyutu >= D3D");

    // Eski Scan(8) / Shader_Default yolu calisiyor ve birlesik maskeyle ayni
    // hedefi gostermeli (Default = D3D|NVIDIA|AMD|Intel; Steam hariç).
    cleaner::ScanResult def = cleaner::Scan(8);
    Check(def.fileCount >= 0 && def.sizeMB >= 0.0, "Scan(8) gecerli deger dondu");

    // Yanlis bir kategori numarasi cokmemeli (temizleyici siniri astiginda 0 doner).
    cleaner::ScanResult asiri = cleaner::Scan(999);
    CheckEq(asiri.fileCount, 0, "Scan(999) -> 0 dosya (sinir disi)");
}

int main()
{
    std::printf("TENGRI yeni modullerin testleri\n\n");

    Test_Restore();
    Test_Services_Whitelist();
    Test_Services_Query();
    Test_Startup();
    Test_ShaderScan();

    std::printf("\n%d gecti, %d kaldi\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
