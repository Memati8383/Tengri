#pragma once
// Disk analiz motoru — arayüzden bağımsız, salt Windows API.
//
// İki tarama yolu var:
//   1. MFT: yönetici yetkisiyle \\.\C: ham bölümünden $FILE kayıtları doğrudan
//      okunur. WizTree'nin hız sırrı budur: milyonlarca dosya, dizin gezmeden,
//      tek sıralı okumayla ağaca dönüşür. Yalnız NTFS bölümlerde çalışır.
//   2. Walk: paralel FindFirstFile gezisi. Yetki gerektirmez; çıkarılabilir,
//      ağ ve tek-klasör hedeflerinde tek yoldur. NTFS'te MFT açılamazsa buna
//      düşülür.
//
// Sonuç her iki yolda da aynı düğüm dizisidir (parent-index'li düz ağaç), böylece
// arayüz motor seçimiyle ilgilenmez. Dizin düğümlerinin alloc/size alanları
// ALT AĞAÇ TOPLAMIdır (yapraklardan yukarı toplanır); dosyalarda kendi boyutudur.
//
// İş parçacığı sözleşmesi: tarama kendi işçilerinde koşar; düğüm havuzu tek
// mutex ile korunur. Arayüz okurken Lock()/Unlock() arasında `const` erişimleri
// kullanır. Progress sayaçları atomiktir, kilitsiz sorulabilir.
//
// Test sözleşmesi: ParseMftRecord, DecodeDataRuns ve LayoutTreemap saf
// fonksiyonlardır; sahte kayıt tamponlarıyla birim test edilebilirler. Birim
// testler gerçek diskten okuma YAPMAZ.

#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace disk
{
    enum class State    { Idle, Scanning, Done, Failed, Cancelled };
    enum class Engine   { Unknown, Mft, Walk };
    enum class DupState { Idle, Running, Done, Failed, Cancelled };

    // Düğüm başına 56 bayt (assert testi aşağıda). size/alloc'un 8 hizası
    // yüzünden 12..15 boşlukta duruyordu; oraya mtime, 44'e owner, 52'ye
    // bayraklar yazıldı: alan eklemek boyutu 64'e kaçırmıyor.
    enum NodeFlag : uint16_t
    {
        kFlagCompressed  = 1u << 0,   // $DATA sıkıştırılmış (LZX/XPRESS)
        kFlagSparse      = 1u << 1,   // seyrek dosya
        kFlagEncrypted   = 1u << 2,   // EFS
        kFlagReparse     = 1u << 3,   // symlink / junction / reparse point
        kFlagOffline     = 1u << 4,   // içeriği diskte değil (tiering)
        kFlagAds         = 1u << 5,   // adlı $DATA akışı var
        kFlagHardLink    = 1u << 6,   // kayıtta birden çok $FILE_NAME
        kFlagSizeUnknown = 1u << 7,   // boyut hiçbir kaynaktan okunamadı
    };

    struct Node
    {
        int      parent     = -1;
        int      firstChild = -1;   // ekleme sırası ters; arayüz görünüre göre sıralar
        int      nextSib    = -1;
        uint32_t mtime      = 0;    // değişim zamanı, unix saniye (0 = bilinmiyor)
        uint64_t size       = 0;    // dosyada mantıksal bayt; dizinde alt ağaç toplamı
        uint64_t alloc      = 0;    // diskte kapladığı; dizinde alt ağaç toplamı
        uint32_t nameOff    = 0;    // UTF-8 havuz indeksi
        uint32_t nameLen    = 0;
        int32_t  ext        = -1;   // uzantı tablosu indeksi (yalnız dosyalar)
        uint32_t owner      = 0;    // 1-bazlı Scanner::Owners() indeksi; 0 = bilinmiyor
        uint8_t  dir        = 0;
        uint8_t  protectedDir = 0;  // sistem ağacı: silme teklif edilmez
        uint8_t  orphan     = 0;    // MFT'te ebeveyni bulunamadı, köke asıldı
        uint8_t  pad        = 0;
        uint16_t flags      = 0;    // NodeFlag bitleri
        uint16_t pad16      = 0;
    };
    static_assert(sizeof(Node) == 56, "disk::Node 56 bayt olmalı (canli agacin boyu)");

    struct ExtStat { std::string ext; uint64_t alloc = 0; uint64_t files = 0; };

    // $Secure:$SDS sözlük kaydı. Node.owner bunun 1-bazlı indeksidir.
    // LookupAccountSid AĞ SORUSU gönderebilir ve saniyelerce sürebilir: ad asla
    // UI iş parçacığında çözülmaz, işçi kuyruğa atılan id'yi arka planda çözer.
    struct Owner
    {
        uint32_t    secId     = 0;    // $SI +0x34'te okunan security id
        std::string sid;              // ham "S-1-5-..." — her zaman dolu
        std::string name;             // "MAKINE\Kullanıcı"; boş = henüz çözülmedi
        bool        attempted = false;// çözüm denendi ve sonuç alınamadı
        bool        queued    = false;// işçiye bir kez gönderildi
    };

    enum OwnerState { kOwnerNone = 0, kOwnerResolved, kOwnerPending, kOwnerFailed };

    struct DupGroup
    {
        uint64_t         size   = 0;   // dosya başına mantıksal bayt
        uint64_t         wasted = 0;   // (n-1) * size
        std::vector<int> nodes;        // düğüm indeksleri; [0] "tutulan" dosya
    };

    struct Progress
    {
        State        state  = State::Idle;
        Engine       engine = Engine::Unknown;
        uint64_t     items  = 0;
        uint64_t     allocBytes = 0;
        uint64_t     bytesRead  = 0;   // MFT: ilerleme yoklaması
        uint64_t     bytesTotal = 0;   // 0 = toplam bilinmiyor
        std::wstring current;          // walk: şu an gezilen klasör
    };

    struct Source
    {
        std::wstring path;    // "C:\" veya UNC ("\\srv\paylasim")
        std::wstring label;   // "C: SSD" gibi gösterim etiketi
        bool         ntfs     = false;
        bool         volume   = false; // harfli sürücü kökü mü (MFT adayı)
    };

    // Bilinen sürücüleri listeler (sabit + çıkarılabilir + ağ + RAM disk).
    std::vector<Source> ListSources();

    class Scanner
    {
    public:
        Scanner() = default;
        ~Scanner();                       // işçileri join eder
        Scanner(const Scanner&) = delete;

        // Hedefi tarar. Meşgulse false döner. Sürücü kökü + NTFS'te MFT denenir,
        // açılamazsa walk'a düşülür. Yeni tarama eski ağacın yerini alır.
        bool Start(const std::wstring& target);
        void Cancel();                    // iptal et ve işçileri bekle
        void Join();                      // kapanmadan önce (UI thread'inden)

        State    GetState()  const { return state_.load(); }
        Engine   UsedEngine() const { return engine_.load(); }
        bool     Busy()      const { return busy_.load(); }
        // Ne $DATA'sında ne de $FILE_NAME bağlarında boyut taşıyan dosyalar
        // (ölçülen C:'de 9.103): ağaçta 0 görünür, arayüz "bilinmiyor" der.
        uint64_t UnknownSizeFiles() const { return unknownSize_.load(); }
        void     GetProgress(Progress& out) const;   // anlık kopya

        // ----- ağaç okuma (çağıran Lock()/Unlock() arasında tutmalı) -----
        void Lock() const;
        void Unlock() const;
        const std::vector<Node>& Nodes() const { return nodes_; }
        const std::vector<char>& Pool()  const { return pool_; }
        const std::vector<ExtStat>& Exts() const { return exts_; }
        const std::vector<Owner>& Owners() const { return owners_; }   // Lock() altında
        // Düğümün sahibi: etiket çözülmüşse "MAKINE\Kullanıcı", değilse ham SID;
        // hiç yoksa boş. Görülen id ilk fırsatta arka plan çözüm kuyruğuna atılır,
        // böylece imleç kutusu API cevabını beklemez.
        OwnerState OwnerOfLocked(int node, std::string& label);
        int  Root() const { return root_; }
        std::wstring RootPathLocked() const;          // Lock() altında çağrılmalı
        std::string  NodeNameLocked(int n) const;     // Lock() altında
        bool BuildPathLocked(int node, std::wstring& out) const;  // Lock() altında
        bool BuildPath(int node, std::wstring& out) const;        // kendi kilidiyle

        // ----- duplicate tespiti (tarama bittikten sonra) -----
        bool     StartDuplicates();
        void     CancelDuplicates();      // iptal + bekle
        DupState GetDupState() const { return dupState_.load(); }
        uint32_t DupPhase()    const { return dupPhase_.load(); }   // 1 grup, 2 örnek hash, 3 tam hash
        // Faz içi ilerleme: yol/adres sayacı. Faz değişince sıfırdan dolar.
        uint64_t DupDone()     const { return dupDone_.load(); }
        uint64_t DupTotal()    const { return dupTotal_.load(); }
        const std::vector<DupGroup>& Dups() const { return dups_; } // Lock() altında

        // ----- önbellek -----
        bool SaveCache(const std::wstring& path) const;   //Done/okunmuş ağacı yazar
        bool LoadCache(const std::wstring& path);         // ağacı değiştirir, Done yapar
        static std::wstring DefaultCachePath();           // %LOCALAPPDATA%\TENGRI\diskscan.tgds

        // Başta "TGDS" imzası olan ama sürümü bizden farklı (v1/v2) dosyayı
        // SİLER ve true döner. Yabancı imzaya, okunamayan/bozuk gövdeye ve
        // dosyasızlığa dokunulmaz: LoadCache'in sessiz reddi yeter.
        static bool PurgeIncompatibleCache(const std::wstring& path);

        // MFT okuma paralelliği: 1 = tek iş parçacığı. Sonraki değerler $MFT'yi
        // dilimlere böler; her dilim kendi kolundan okur. Ayar bir tarama
        // BAŞLADIĞINDA okunur, çalışan taramayı değiştirmez.
        // Ölçülen (C:, 1.270.087.680 B $MFT, 12 çekirdek/NVMe, iki tur):
        // 1 = 80,1-81,9 s (ters sıralı kontrolde 106,4 s), 2 = 22,1-23,6 s,
        // 4 = 10,6-10,8 s, 8 = 1,5-1,7 s. Tek iş parçacıklı overlapped prefetch
        // denendi ve seriden hızlı değildi (85,5-93,9 s), yolu silindi.
        void SetMftThreads(int n) { mftThreads_ = n < 1 ? 1 : (n > 8 ? 8 : n); }
        int  MftThreads() const   { return mftThreads_; }

        // ----- eylemler (arayüzün kendi STA işçi thread'inden çağrılır) -----
        struct DeleteResult { int attempted = 0; bool aborted = false; std::wstring error; };
        static bool DeletePaths(const std::vector<std::wstring>& paths, bool recycle, DeleteResult& res);
        static void ShowInExplorer(const std::wstring& path);
        static void OpenPath(const std::wstring& path);   // kabuğun varsayılan eylemi

    private:
        void RunMft(const std::wstring& target, const std::wstring& volumeRoot);
        void RunWalk(const std::wstring& target);
        void RunDup();
        void RunOwnerWorker();            // SID -> ad, mtx_ DIŞINDA (ağ sorusu olabilir)
        void EnsureOwnerWorker();         // mtx_ tutulurken
        void StopOwnerWorker();
        int  AddNode(int parent, const std::string& name, bool dir,
                     uint64_t size, uint64_t alloc,
                     uint32_t mtime = 0, uint32_t owner = 0, uint16_t flags = 0);   // mtx_ tutulurken
        void InternExtLocked(int nodeIdx);
        void Finish(State s);
        bool MftVolumeAvailable(const std::wstring& volumeRoot, HANDLE& outHandle);

    private:
        mutable std::mutex mtx_;

        std::vector<Node> nodes_;
        std::vector<char> pool_;                 // UTF-8 ad havuzu
        std::vector<ExtStat> exts_;
        std::vector<Owner> owners_;              // $SDS sözlüğü; mtx_ altında
        std::vector<uint32_t> ownerPending_;     // çözüm kuyruğu; mtx_ altında
        // Tablo her değiştiğinde artar: işçi API çağrısına çıkmadan önceki tabloya
        // ait indeksi geri yazmasın (yeni tarama/önbellek yüklemesi arada olabilir).
        uint64_t ownerGen_ = 0;                  // mtx_ altında
        std::condition_variable ownerCv_;        // mtx_ ile beklenir
        bool               ownerStop_ = false;   // mtx_ altında
        std::thread        ownerWorker_;         // SID -> ad (tek işçi)
        int          root_ = -1;
        std::wstring rootPath_;                  // "C:\" veya hedef klasör/UNC
        uint64_t     clusterBytes_ = 4096;       // walk: disk kapladığı tahmini
        std::wstring currentDir_;                // walk: Lock() altında gösterim
        int          mftThreads_  = 1;           // MFT dilim iş parçacığı sayısı

        std::atomic<State>    state_{ State::Idle };
        std::atomic<Engine>   engine_{ Engine::Unknown };
        std::atomic<bool>     busy_{ false };
        std::atomic<bool>     cancel_{ false };
        std::atomic<uint64_t> items_{ 0 };
        std::atomic<uint64_t> allocBytes_{ 0 };
        std::atomic<uint64_t> mftRead_{ 0 };
        std::atomic<uint64_t> mftTotal_{ 0 };
        std::atomic<uint64_t> unknownSize_{ 0 };   // kFlagSizeUnknown taşıyan dosya sayısı

        std::thread              worker_;        // RunMft/RunWalk şemsiye işçisi
        std::vector<std::thread> walkWorkers_;
        // walk iş kuyruğu kilidi: Cancel() buradan beklemedekileri uyandırır.
        std::mutex               qMtx_;
        std::condition_variable  qCv_;

        std::thread              dupWorker_;
        std::atomic<DupState>    dupState_{ DupState::Idle };
        std::atomic<bool>        dupCancel_{ false };
        std::atomic<uint32_t>    dupPhase_{ 0 };
        std::atomic<uint64_t>    dupDone_{ 0 };
        std::atomic<uint64_t>    dupTotal_{ 0 };
        std::vector<DupGroup>    dups_;          // Lock() altında
    };

    // =================================================================== saf yardımcılar

    struct MftFileInfo
    {
        bool         inUse   = false;
        bool         isDir   = false;
        bool         hasName = false;   // kayıtta kullanılabilir $FILENAME var
        uint64_t     parent  = 0;       // ebeveyn FRN'sinin alt 48 biti (kayıt indeksi)
        uint64_t     size    = 0;       // mantıksal bayt
        uint64_t     alloc   = 0;       // diskte kapladığı
        uint32_t     mtime   = 0;       // $STANDARD_INFORMATION değişim zamanı, unix saniye
        uint32_t     owner   = 0;       // $SI +0x34 security id (RunMft sonunda tablo indeksine çevrilir)
        uint32_t     attrs   = 0;       // FILE_ATTRIBUTE maskesi ($SI +0x20, yoksa $FN +0x38)
        uint8_t      links   = 0;       // kullanılabilir $FILE_NAME sayısı (2+ = hard link)
        uint8_t      nameNs  = 0;       // seçilen bağın namespace'i: 0 POSIX, 1 Win32, 2 DOS, 3 Win32+DOS
        uint8_t      ads     = 0;       // görülen adlı $DATA akışı sayısı (ADS)
        bool         dataOk  = false;   // boyut adsız $DATA'dan geldi (uzantı dahil)
        bool         sizeUnknown = false; // ne $DATA ne $FILE_NAME boyut verdi
        uint64_t     baseRef = 0;       // taban FRN'nin alt 48 biti; 0 = taban kayıt
        std::wstring name;              // Win32 adı, UTF-16
    };

    // Tek FILE kaydını çözer. Silinmiş kayıtlarda inUse=false ile true döner;
    // bozuk/sihirli olmayan tampondan false döner. Boyut zinciri: adsız $DATA
    // (resident: değer uzunluğu, non-resident: tahsis +0x28 / gerçek +0x30) →
    // $FILE_NAME bağları arasında en küçük sıfır-olmayan → 0. $STANDARD_INFORMATION
    // boyut TAŞIMAZ, yok sayılır. FRN ve boyut alanlarının üst 16 biti bu
    // makinede çöp taşıyabiliyor: hepsi alt 48 bite maskelenir.
    // Hard-link'li kayıtta birden çok $FILE_NAME olur; ad için DOS 8.3 en sonda,
    // boyut taşıyan, yoksa Win32 bağ seçilir (diğer bağlar ağaçta görünmez).
    // out.owner: $SI +0x34 security id. +0x30'daki "owner id" alanı bu hacimde
    // pratikte boş (ölçülen: 719.699 kullanımda kaydın 5'inde sıfır dışı), bu
    // yüzden anahtar olarak +0x34 kullanılır.
    bool ParseMftRecord(const uint8_t* rec, uint32_t recLen, MftFileInfo& out);

    // $Secure:$SDS akışından sahip sözlüğü kurar: owners (sıralı, 0-bazlı) ve
    // security id -> 1-bazlı indeks haritası (0 = sahip yok). Girdi, taban
    // kaydın uzantılarındaki adlandırılmış $DATA parçalarından VCN sırasında
    // birleştirilmiş akıştır. Girdi başı uzunluk alanıyla YÜRÜNÜLEZmez (ölçülen:
    // +0x10 alanı gerçek adımla 3.544 farkın yalnızca 737'sinde birebir);
    // bunun yerine her 8'li ofste yapısal öz-denetim yapılır: +0x08 = girdinin
    // kendi akış ofseti, +0x04 = security id, +0x14 = yerdeğiştirmeli SD başlığı
    // (rev 1), SD+0x04 = owner SID ofseti. Geçersiz/çakışan girdiler atlanır.
    size_t BuildSdsOwners(const std::vector<uint8_t>& sds, std::vector<Owner>& owners,
                          std::unordered_map<uint32_t, uint32_t>& idToIndex);

    // Non-resident $DATA tahsis alanının bozuk yüksek 32 bitini ayıklar: sıkıştırıl-
    // mamış/seyrek olmayan dosyada tahsis real ile real+clusBytes arasındadır;
    // dışına taşan değer küme katına yuvarlanmış gerçek boyuta çevrilir. real'den
    // küçük değer (sıkıştırılmış/seyrek) olduğu gibi korunur.
    uint64_t PlausibleAlloc(uint64_t real, uint64_t raw, uint64_t clusBytes);

    // $MFT bloğundan kayıt adımını ölçer (0x40 = "FILE" olmalı; aday adım
    // katlarındaki imza + attrsOff yoklamasından geçer). Ölçülemezse 0.
    // $Boot +0x40 yeni yapılarda kayıt boyunu vermediği için tek kaynak budur;
    // hiç kullanılmamış ( sıfır ) kayıtlar doğru adımı ezmameli: yoklama oranla
    // yapılır.
    uint32_t MeasureRecStride(const uint8_t* buf, uint32_t span);

    // Non-resident öznitelik başlığında koşu listesinin bayt ofsetini bulur.
    // Aday alanlar +0x20 (ölçülen) ve +0x10 (klasik belge); geçerli ofset +
    // makul koşu başlık baytı vereni kazanır, hiçbiri vermezse out=0x40 ve
    // dönen değer 0x40 < attrLen'dir. attr en az 0x40 bayt okunabilir olmalı.
    bool NonResidentRunsOffset(const uint8_t* attr, uint32_t attrLen, uint32_t& out);

    // Squarified treemap: sizes>0 girdileri azalan boyutla (w x h) alanına dizer.
    // out[i] girdi indeksine karşı gelir. Toplam oran bire normalize edilir.
    struct Tile { double x, y, w, h; };
    void LayoutTreemap(const std::vector<uint64_t>& sizes, double w, double h,
                       std::vector<Tile>& out);

    struct DataRun { uint64_t lcn; uint64_t len; };   // len küme sayısı
    // NTFS data run dizisini çözer; lcn'ler birbirine göre mutlak kümeye yığılır.
    // absolute: 0 uzunluklu sıkıştırma işaret koşusu görüldüyse true (desteklenmez).
    bool DecodeDataRuns(const uint8_t* p, uint32_t len, bool& absolute,
                        std::vector<DataRun>& out);

    // Sabit uzunluklu mantıksal okuma dilimi. `logical` = $MFT akışındaki bayt
    // ofseti, `bytes` = dolu bayt (son dilim kısadır), pieces = dilimi diskte
    // dolduran bitişik küme blokları. Koşu sınırında parçalar TEK tamponda
    // birleştiği için kayıt adımı kümeden büyük olsa da hiçbir kayıt ortada
    // bölünmez; kayıt indeksi mantıksal ofsetten türer, disk konumundan değil.
    struct MftPiece { uint64_t phys = 0; uint32_t bufOff = 0; uint32_t bytes = 0; };
    struct MftChunk { uint64_t logical = 0; uint32_t bytes = 0;
                      std::vector<MftPiece> pieces; };
    bool PlanMftChunks(const std::vector<DataRun>& runs, uint64_t clusBytes,
                       uint32_t clusPerChunk, std::vector<MftChunk>& out);

    // MFT dilim sayısının arayüz varsayılanı: mantıksal çekirdek sayısı, 1–8'e
    // sıkıştırılmış. Motor bunu kendisi seçmez; ayarı çağıran (UI) kullanır.
    int  DefaultMftThreadCount();

    std::string  Utf16ToUtf8(const wchar_t* s, int cch);
    std::wstring Utf8ToUtf16(const char* s, size_t len);

    // FILETIME (1601'den itibaren 100 ns) → unix saniye. 0 ve 1970 öncesi → 0;
    // 2106'dan büyük değer 0xFFFFFFFF'a doyurulur (uint32 alan).
    uint32_t UnixSeconds(uint64_t filetime);

    // Win32 FILE_ATTRIBUTE maskesini (ölçülen kaynak: $FILE_NAME +0x38, walk'ta
    // WIN32_FIND_DATAW.dwFileAttributes) NodeFlag bitlerine çevirir.
    uint16_t NodeFlags(uint32_t fileAttrs, uint8_t adsStreams, uint8_t links, bool sizeUnknown);
}
