#pragma once
#include <string>
#include <vector>

// Windows bildirimleri (WinRT toast).
//
// Uygulama içi toasterlar (ui::Notify) ekranın köşesinde çizilir ve yalnızca
// uygulama açıkken görünür. Buradaki bildirimler ise gerçek Windows bildirimidir:
// Eylem Merkezi'ne düşer, düğme taşır, uygulama kapalıyken de gelir.
//
// Kurgu:
//   - Uygulama kimliği (AppUserModelID) Windows'un bildirimleri hangi uygulamaya
//     ait edeceğini bu kimlikle bilir. Kimliksiz toast'lar gruplanmaz.
//   - Bunun için Başlat menüsünde bir kısayol ve HKCU'da bir kayıt gerekir; ikisi
//     de bir kez yazılır ve bir daha dokunulmaz.
//   - Düğmeler uygulamayı YENİDEN başlatarak tetikler. Bunun tek bir pencereyle
//     sonuçlanması için main.cpp tek örnek kilidi ve WM_COPYDATA ile komut
//     yönlendirmesi yapar; ikinci örnek açılmaz, komutu ilk örneğe iletir.
namespace notify
{
    enum class Level
    {
        Success, Info, Warning, Error
    };

    // Toast düğmesi. Kimlik, XML'de arguments olarak taşınır; ana süreçte
    // karşılık gelen iş yapılır.
    enum class Action
    {
        None,        // düğme yok
        View,        // uygulamayı aç, hedef sayfaya git
        Undo,        // geri al ÖNCE onay ister
        Retry,       // işi yeniden dene
        Download,    // duyurulan sürümü indirmeye başla
        Dismiss,     // sistem düğmesi: yalnızca kapatır
    };

    // "Sonuçları gör" nereye götürür. app.cpp'teki sekme sırasıyla birebir aynı
    // olmalıdır; değerler yanlışsa kullanıcı başka bir sayfa açar.
    enum Target
    {
        TargetNone = -1,
        TargetDashboard = 0,
        TargetCleaner,
        TargetTweaks,
        TargetNetwork,
        TargetSystem,
        TargetSettings,
    };

    struct Button
    {
        Action      action;
        const char* label;   // zaten dile çevrilmiş metin (L(...) sonucu)
    };

    // ---- tercihler -----------------------------------------------------------
    // Üç anahtar ayrı ayrı tutulur çünkü biri diğer ikisini doğrudan kontrol
    // ediyor: sessiz mod açıkken ne uygulama içi ne Windows bildirimi gider,
    // yalnızca hata ve uyarılar geçer.
    extern bool toastsEnabled;   // Windows bildirimleri (varsayılan açık)
    extern bool quietMode;       // sadece hatalar

    // Sessiz mod, uygulama içi bildirimler için de geçerlidir.
    bool AllowInApp(Level lvl);

    // ---- kurulum -------------------------------------------------------------
    // AUMID kaydı, Başlat menüsü kısayolu ve süreç kimliği. Ana iş parçacığından
    // çağrılmalıdır (WinRT başlatma burada yapılır).
    //
    // Mevcut kısayolu ve kaydı bulursa hiçbir şey yazmaz; yalnızca eksik olanı
    // tamamlar. Dosya geçersizse sıfırdan yazar.
    void Init();

    void Shutdown();

    // Tercihleri diske yazar. İki anahtar da Init'te okunmuş olduğundan,
    // ayar sayfasında değiştikleri anda çağrılması yeterlidir.
    void Save();

    // ---- gönderim ------------------------------------------------------------
    // Windows bildirimi gönderir. Ayar kapalıysa, sessiz modda ve bu düzey
    // susturuluyorsa hiçbir şey yapmaz; çağıran taraf bunu bilmeye ihtiyaç
    // duymaz. Başarısız olursa sessizce geçer — bir bildirim gönderilememesi
    // iş akışını bozmamalıdır.
    void Post(Level lvl, const char* title, const char* body,
              Target target = TargetNone,
              const std::vector<Button>& buttons = {});

    // "Yetki alınıyor / işlem sürüyor" gibi süreç bildirimi. İşi başlatmadan
    // önce çağrılır, sonucun ardından Post ile gider.
    void PostProgress(const char* title, const char* body);

    // ---- komut yönlendirme ---------------------------------------------------
    // Toast'tan gelen eylem kodu. main.cpp komut satırından okur, tek örnek
    // kilidi üzerinden çalışan örneğe WM_COPYDATA ile iletir ve uygulama
    // app::HandleNotifyCommand içinde karşılar.
    enum Command
    {
        CmdView  = 1,   // uygulamayı aç, hedef sayfaya git
        CmdUndo  = 2,   // geri al ÖNCE onay ister
        CmdRetry = 3,   // işi yeniden dene
        CmdDownload = 4, // güncellemeyi indirmeye başla
    };

    // Başlatma komutunun ön eki. Ayrıştırma burada yaşar çünkü XML üretimiyle
    // aynı dosyada; böylece üretici ile okuyucu aynı biçimi paylaşır.
    constexpr const wchar_t* kLaunchPrefix = L"cmd:";

    // "cmd:2:-1" -> CmdUndo. Biçim tanınmazsa 0 döner; çağıran bunu "görev
    // yok" diye yorumlar. Hedef sekme istenirse ikinci alan onun numarasıdır,
    // yoksa -1.
    int ParseLaunch(const wchar_t* args, int* target);

    // Kısayolun yazılması gerçekten gerekiyorduysa. Ayar hata verirse de
    // çağrılır; çağıran yalnızca log için kullanır.
    extern bool g_installedShortcut;
}