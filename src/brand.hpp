#pragma once

// Ürünün kimlik bilgileri tek bir yerde toplanır.
//
// Ad önceden 14 ayrı yere dağılmıştı: pencere sınıfı, bildirim balonu metni,
// uygulama verisi klasörü, başlangıç kaydı, geçici dosya ön eki, arayüzdeki dört
// metin, iki dil kaydı, derleme çıktısı ve benzeri. Yeniden adlandırmak her birini
// tek tek aramayı ve hiçbirini kaçırmamayı gerektiriyordu. Artık ürünü adlandıran
// her şey burada olduğu için isim değişikliği tek dosyalık bir işlemdir.
//
// Buradaki her değer derleme sisteminde yazanlarla da örtüşmek zorundadır: sürüm
// kaynağı sürüm ve yayıncı metinlerini okur, derleme betiği çıktı adını buradan
// alır. Biri değişirse diğerleri de birlikte değiştirilmelidir.

namespace brand
{
    // ---- adlandırma --------------------------------------------------------
    // Görünen ad Türkçe yazımdır: noktalı büyük İ (U+0130). Kaynak dosya /utf-8
    // ile derlendiği için dar karakter hâli de UTF-8 bayt olarak doğru gider ve
    // ImGui'ın Segoe UI yazı tipinde glif bulunur.
    //
    // Disk ve registry adları aşağıda ASCII kaldı: dosya sistemi ve kayıt
    // defteri için noktalı İ taşımak yalnızca sorun çıkarır.
    constexpr const wchar_t* kName         = L"TENGRİ";     // görünen ad, pencere başlığı
    constexpr const char*    kNameA        = "TENGRİ";      // dar karakter kullanan çağrılar için
    constexpr const wchar_t* kWindowClass  = L"TENGRI_MainWindow";
    constexpr const wchar_t* kTrayTip      = L"TENGRİ - System Optimizer";
    constexpr const wchar_t* kTrayOpen     = L"Open TENGRİ";
    constexpr const wchar_t* kMsgTitle     = L"TENGRİ";     // pencere başlıklarında kullanılan ad

    // ---- disk ve registry kimliği -----------------------------------------
    // Bu adlar, daha önceki sürümlerin yazdığı kayıtları da kapsasın diye
    // değiştirilmez; yazma sonrası değerler geri okunur, arayüz de kalıcı olmayan
    // bir durumu etkinmiş gibi göstermez.
    constexpr const wchar_t* kAppDataFolder = L"TENGRI";     // %APPDATA%\TENGRI\*
    constexpr const char*    kAppDataFolderA = "TENGRI";     // dar hâli, std::string yolları için
    constexpr const wchar_t* kRunValueName  = L"TENGRI";     // HKCU\...\Run içindeki değer adı
    constexpr const char*    kTempPrefix    = "tengri_";     // %TEMP%\tengri_<pid>_<rastgele>.reg
    constexpr const wchar_t* kTempPrefixW   = L"tengri_";
    constexpr const char*    kPingPayload   = "tengri-latency-probe";

    // Makine parmak izine karıştırılan tuz. Tuz olmadan özet yalnızca makineyi
    // tanımlar, ürünü değil; aynı formülü kullanan bütün araçlar aynı değerde
    // anlaşırdı. Değiştirilmesi daha önce verilmiş anahtarları geçersiz kılacağı
    // için sürümlenmiş biçimde tutulur.
    constexpr const char*    kHwIdSalt      = "tengri.hwid.v1";

    // ---- bağlantılar --------------------------------------------------------
    // Hakkında sayfasında düğme olarak gösterilir. Uygulamanın kendi çözümlemesi
    // gerektirmesinler diye doğrudan adres olarak, kabuk çağrısıyla açılırlar.
    constexpr const char*    kInstagramUrl  = "https://instagram.com/ferit22901";
    constexpr const char*    kGitHubUser    = "Memati8383";
    constexpr const char*    kGitHubUrl     = "https://github.com/Memati8383";
    constexpr const char*    kRepoSlug      = "Memati8383/Tengri";
    constexpr const char*    kRepoUrl       = "https://github.com/Memati8383/Tengri";

    // Geniş karakter hâlleri, Unicode kabuk çağrısı için gereklidir. İkisini de
    // yan yana tanımlamak, dar hâlin unutulup geride kalmasını engeller.
    constexpr const wchar_t* kInstagramUrlW = L"https://instagram.com/ferit22901";
    constexpr const wchar_t* kGitHubUrlW    = L"https://github.com/Memati8383";
    constexpr const wchar_t* kRepoUrlW      = L"https://github.com/Memati8383/Tengri";

    // Tıklamanın nereye gideceği her zaman anlaşılsın diye düğmelerin altında
    // metin olarak gösterilirler.
    constexpr const char*    kInstagramHandle = "@ferit22901";
    constexpr const char*    kGitHubHandle    = "Memati8383";

    // ---- sürüm --------------------------------------------------------------
    // Aynı sayılar kaynak dosyanın sürüm bilgisinde de bulunur; hakkında kutusu
    // sürümü ikinci bir yere yazmak yerine çalışan dosyadan geri okuduğu için iki
    // değer birbirinden ayrılamaz.
    constexpr const char*    kVersionMajor  = "1";
    constexpr const char*    kVersionMinor  = "1";
    constexpr const char*    kVersionPatch  = "0";
    constexpr const char*    kVersion       = "1.1.0";
    constexpr const char*    kPublisher     = "TENGRİ Project";
    constexpr const char*    kDescription   = "TENGRİ - System Optimizer";

    // ---- derleme -----------------------------------------------------------
    // Derleme betiği (çıktı adı) ve CMake yapılandırması (proje ve hedef adı)
    // tarafından okunur.
    constexpr const char*    kBuildProject  = "TENGRI";

    // Simge kaynağının numarası. Uygulama simgesi için kullanılan kimlik Windows
    // tarafından tanımlı bir sabit olmadığından hem kaynak dosyasında hem de
    // burada bildirilmelidir; aksi hâlde çağrı var olmayan bir kaynağı gösterir.
    constexpr int            kIconId        = 101;
}
