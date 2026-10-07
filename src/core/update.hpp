#pragma once

// Otomatik sürüm denetimi.
//
// Akış tek bir dosyaya dayanır: sürüm yayınlandığında CI, EXE'nin SHA-256'sını ve
// boyutunu hesaplayıp `latest.json` adında ikinci bir dosyayı da sürüme asetler.
// Uygulama yalnızca o dosyayı okur. Hash böylece kaynakta yazılı duran bir sabit
// değil, derlenmiş dosyadan SONRA üretilen bir değer olur; bozuk inen bir exe ile
// ona uymayan bir hash'i el ele tutuşturmanın iki ayrı hata gerektirmesi bundan.
//
// Adres `https://github.com/<repo>/releases/latest/download/latest.json`. GitHub
// `latest`i yayımlanmış ve ön-sürüm olmayan en yeni etikete yönlendirir, dolayısıyla
// tek istek hem "en yeni kararlı sürüm"ü hem de onun bildirimini getiriyor; sürüm
// listesi ayrıştırmak gerekmiyor.
//
// Neden indirmeyi tarayıcıya bırakmıyoruz? Kullanıcının indirdiği dosyanın beklenen
// hash ile eşleştiğini gösteremeyiz; doğrulamanın anlamı için akışı elimizde tutuyoruz.
//
// Politika: SUAL SOR, SONRA İNDİR. Ne denetim kendi kendine exe indirir ne de
// indirme onaysız başlar. Denetim birkaç KB'lik JSON okur; indirme yalnızca
// kullanıcı istediğinde başlar.
//
// Yetki: takas yazılabilir bir klasör ister. İzin vermiyorsa UAC İSTEMİ YÜKSELMEZ —
// dosya olduğu yerde bırakılır ve yolu söylenir. Bir güncellemeyi yerleştirmek
// için yükseltilmiş çalışmak, uygulamanın geri kalanındaki "yetki yalnızca işi
// yaparken sorulur" kuralını bozardı.
//
// Güven sınırının dürüst tarifi: hash'i yayınlayan ile exe'yi yayınlayan aynı yer.
// Bu, bozuk aktarıma ve vekil (proxy) hilesine karşı korur; depoyu ele geçirmiş
// birine karşı korumaz. Asimetrik imza (minisign) ayrı bir aşama olarak duruyor.
#include <string>

namespace update
{
    // Bildirimdeki alanlar. `notes` ayrıştırılır ama arayüzde gösterilmez; dosyanın
    // insan tarafından da okunabilir kalması için orada duruyor.
    struct Latest
    {
        std::string version;               // "1.3.0" — baştaki 'v' atılır
        std::string sha256;                // 64 karakterlik onaltılık, küçük harf
        std::string url;                   // exe'nin indirme adresi
        std::string notes;
        unsigned long long size = 0;       // bayt
    };

    enum class State { Idle, Checking, Current, Available, Failed };

    // Hata nedeni. Metin değil NEDEN saklanır: arka plan iş parçacığı hangi dilde
    // çalışıyorsa o dilin metni yapışıp kalırdı; neden çizim anında L() ile çözülür.
    enum class Fail
    {
        None,
        Network,       // bağlanılamadı, DNS ya da zaman aşımı
        Http,          // beklenmedik durum kodu
        NotFound,      // 404: o sürümde latest.json yok (ör. eski bir yayın)
        BadManifest,   // JSON bozuk ya da zorunlu alan eksik
        TrustHost,     // indirme adresi tanınmayan bir sunucuya gidiyor
        Io,            // dosya yazılamadı/okunamadı
        HashMismatch,  // inen dosya beklenen özetle uyuşmuyor
        NotWritable,   // klasör izin vermedi; dosya geçici yerde duruyor
    };

    enum class DlState { Idle, Running, Ready, Failed };

    // ---- tercihler -----------------------------------------------------------
    // Varsayılan AÇIK. Kayıt hiç yoksa da açıktır; kapalı olması kullanıcının
    // açıkça kapattığı anlamına gelir.
    extern bool enabled;

    void LoadPrefs();
    void SavePrefs();

    // Son denetimin Unix saniyesi (0 = hiç denenmemiş). Kalıcılığın gerekçesi:
    // pencere bir gün açık kalsa bile aynı güne bir daha istek atılmaması.
    unsigned long long LastCheckUtc();
    void               MarkChecked(unsigned long long nowUtc);

    // Son denetimin üzerinden kaç saat geçti (0 = hiç ya da bu saat içinde).
    // Arayüz "kaç saat önce" yazarken kendi saatini kurmasın: denetimin
    // aralığıyla aynı saat kaynağı kullanılır, böylece rozet ile "yeniden
    // denetle" birbirini tutar.
    unsigned long long HoursSinceLastCheck();

    // ---- saf mantık ----------------------------------------------------------
    // Ağa, diske veya registry'ye dokunmayan yüzey; testler bu bölümü hedefler.

    // "1.10.0" > "1.2.0" olmalı. Sözlük sırasıyla karşılaştırırsak '1' < '2'
    // olduğu için ters çıkar ve uygulama kendi onlarca sürümünü hiç görmez.
    // Dönüş: a<b -1, eşit 0, a>b 1. Eksik bileşen sıfır sayılır ("1.2" == "1.2.0"),
    // baştaki 'v' ve '-' sonrası etiket (rc, beta) yok sayılır.
    int CompareVersions(const std::string& a, const std::string& b);

    bool IsHex64(const std::string& s);

    // Bildirimi ayrıştırır. Zorunlu alanlardan biri yoksa, özet/boyut anlamsızsa
    // ya da indirme adresi tanınmıyorsa false döner ve `why`'ye neden yazılır.
    bool ParseManifest(const std::string& json, Latest* out, Fail* why);

    // Adresin sunucusu tanınıyor mu? Bildirim GitHub'dan geldiği için onda yazılı
    // olan indirme adresi de GitHub dışına taşıyorsa ya bozuk ya da ele
    // geçirilmiş demektir; indirmeden önce bu sorulur.
    bool HostAllowed(const std::string& url);

    // Aralık doldu mu. `now < last` tek başına "henüz dolmadı" demek değildir:
    // saat geri alınırsa denetim bir daha hiç gelmezdi.
    bool IsDue(unsigned long long lastUtc, unsigned long long nowUtc,
               unsigned long long intervalSec);

    // Onaltılık özet. Bellekteki blok testlerde, dosya sürümde kullanılır; ikisi
    // aynı çekirdeği paylaşır.
    std::string Sha256Hex(const void* data, size_t len);
    bool        Sha256File(const std::wstring& path, std::string* outHex);

    // Takas için gereken iki yol: çalışılan exe ve onun kenara alınmış hâli.
    // Adların türetilme biçimi tek yerde dursun diye burada; takasın doğruluğu
    // üç ayrı yerin aynı adı üretmesine değil tek bir üreticiye bağlı olsun.
    //
    // Yazılabilirlik burada SORGULANMAZ: deneme-yanılma için kullanıcı klasörüne
    // dosya yazmak gerekirdi. Asıl karar takas anında, taşıma çağrısının kendi
    // hatasıyla verilir (Apply).
    struct SwapPlan
    {
        std::wstring running;   // şu an çalışılan dosya
        std::wstring stale;     // kenara alınan eski dosya
    };

    SwapPlan PlanFor(const std::wstring& runningPath);

    // ---- denetim -------------------------------------------------------------
    // Yeni bir denetim başlatır; biri sürüyorsa bir şey yapmaz. Sonuç Poll() ile
    // toplanır.
    void CheckNow();

    // Ayarsa, kimse meşgulse ve son denetimden yeterince geçtiyse true.
    bool DueForCheck();

    // İş parçacığının sonucunu toplar ve güncel durumu döner. Çizim tarafı her
    // karede çağırabilir.
    State Poll();
    State Get();

    // Sonuçların kopyası. Kopya olarak dönüyor çünkü arayüz her karede okuyor ve
    // bir "yeniden denetle" bu sırada iş parçacığını yazdırabilirdi; referans
    // döndürsek yarış iki tarafın da suçu olmayan bir okuma olurdu.
    Latest Snapshot();
    Fail   Reason();

    // Uygulama açılırken bir kez: tercihleri ve son denetim zamanını okur, önceki
    // takastan kalmış olabilecek kenara alınmış dosyayı anımsar. Çalışan exe'nin
    // kendi adı da kenara alınmış olabilir ve o sırada Windows tarafından kilitli
    // kalır; silinememesi normaldir.
    void Startup();

    // Kapanırken: süren bir iş varsa iptal işaretini koyar ve iş parçacığını kısa
    // süre bekler. Beklemenin gerekçesi, yarım kalmış bir WinHTTP çağrısının süreç
    // kapanırken çökmüş bir yığın üzerinde kalabilmesi. Süre dolarsa iş parçacığı
    // kendi hâline bırakılır: yalnızca bu modülün süreç boyunca yaşayan
    // statiklerine dokunur, o yüzden geç tamamlanması zararsızdır.
    void Shutdown();

    // ---- indirme ve takas ----------------------------------------------------
    // Kullanıcı onayından sonra çağrılır. Adrelin sunucusu HostAllowed dışıysa
    // hiçbir istek yapılmaz ve neden Fail::TrustHost olur.
    void StageNow();
    DlState StagePoll();
    DlState StageGet();
    unsigned long long StagedBytes();
    unsigned long long StagedTotal();

    // İnen dosyanın yolu. Yalnızca DlState::Ready ise anlamlıdır: doğrulanmamış
    // bir bayt kullanıcıya gösterilmez. Takas başarısız olursa dosya burada
    // kalmaya devam eder ve kullanıcıya söylenen yol da budur.
    // Kopya dönüyor; iş parçacığı indirme bitince bu alanı yazıyor olabilir.
    std::wstring StagedFile();
    void ResetStage();

    // Süren indirmeyi iptal eder: true dönerse iş parçacığı yarım dosyayı siler
    // ve durum kendi kendine Idle'a döner. false = indirme sürmüyor; arayüz
    // düğmeyi o durumda göstermemeli. Ayrı bir vazgeçiş olmasının sebebi
    // ResetStage'in akışla yarışması değil, yarım dosyayı silmeden bırakması.
    bool StageCancel();

    // Yeni örneğin "kapalı pencereyi açma" komutu. Tek örnek kilidi kapanırken
    // serbest bırakılır ama kapanış ile yeni örneğin doğuşu arasında yarış vardır:
    // yeni örnek hâlâ çalışan bir örnekle karşılaşırsa komutu ona gönderip kendi
    // çıkardı ve güncellenmiş uygulama hiç açılmamış olurdu. Bu öneki gören örnek
    // kilidi devralmayı kısa süre BEKLER; eski süreç öldüğünde mutex kendini
    // bırakır. Değer tek yerde dursun diye burada, hem yazıcısı hem okuyucusu.
    constexpr const wchar_t* kRelaunchArg = L"--tengri-relaunch";

    // İndirilen dosyayı yerleştirir: çalışan exe kenara alınır, yenisi aynı ada
    // yazılır, yeni süreç başlatılır ve BU SÜREÇ DOĞRUDAN KAPATILIR. Yani true
    // geri dönmez; dönüş olursa başarısızlık demektir ve Reason() nedeni taşır.
    //
    // Kapanış burada, çağırana bırakılmıyor: Apply'dan sonraki her ek iş (DirectX
    // nesneleri, bildirim kapanışı) yeni örneğin tek örnek kilidiyle yarışına
    // zaman kazandırır. Aynı kalıbı yetki yeninden başlatması da kullanıyor.
    // Fail::NotWritable ise dosya StagedFile() yolunda durur ve uygulama açık kalır.
    bool Apply(const SwapPlan& plan);

    // Takas sonrası kalan kenara alınmış dosya; silinmeyi bekliyor. Boşsa
    // bekleme yok.
    std::wstring StalePath();
}
