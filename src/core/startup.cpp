#include "startup.hpp"

#include <windows.h>
#include <shlwapi.h>

#include <algorithm>
#include <cwctype>

#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Version.lib")

namespace startup
{
    namespace
    {
        struct ScopeDef
        {
            HKEY         root;
            const wchar_t* subkey;
            bool         isMachine;
            bool         isOnce;
        };

        // Dokuz kapsamın Windows'taki gerçek konumları. Sıra, Scope enum'uyla
        // birebir aynı; başka yerde elle eşleme yapmamak için indekslenir.
        // (HKEY sabitleri reinterpret-cast içerdiği için constexpr olamaz.)
        //
        // Startup klasörleri ve Winlogon kök anahtarı KENDİLERİ DE girdi
        // değildir: Startup'ta her dosya, Winlogon'da ise Shell/Userinit gibi
        // belirli değerler ayrı girdilerdir. Kök dizinler burada yalnızca
        // taranacak yol olarak tutuluyor.
        // On kapsamın Windows'taki gerçek konumları. Sıra, Scope enum'uyla
        // birebir aynı; başka yerde elle eşleme yapmamak için indekslenir.
        //
        // Registry dışı iki kapsamın (Startup klasörleri) kök dizini burada
        // saklanmaz: bu dizinler ortam değişkenine bağlı, yani çalışma zamanında
        // çözülmesi gerekiyor. Onlar için ayrı bir çözümleyici var.
        //
        // (HKEY sabitleri reinterpret-cast içerdiği için constexpr olamaz.)
        const ScopeDef kScopes[8] = {
            { HKEY_CURRENT_USER,  L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",     false, false },
            { HKEY_CURRENT_USER,  L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce", false, true  },
            { HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",     true,  false },
            { HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce", true,  true  },

            { HKEY_CURRENT_USER,  L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer\\Run", false, false },
            { HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer\\Run", true,  false },

            // Winlogon'ın GPO'luk ve eski yolu da var; ikisi de Shell/Userinit
            // ele geçirmesinde kullanılır, bu yüzden kapsama alındı.
            { HKEY_CURRENT_USER,  L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon", false, false },
            { HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon", true,  false },
        };

        // Winlogon'da oturumun kendisi olan değerler. Bunlar "başlangıç öğesi"
        // değildir; ama değiştirilmiş olmaları bir saldırı işaretidir, bu yüzden
        // ayrıca listelenip standarttan sapmaları işaretleniyor. Kapatılamazlar.
        const wchar_t* const kWinlogonCritical[] = { L"Shell", L"Userinit" };

        bool IsListedWinlogonValue(const std::wstring& name)
        {
            for (const wchar_t* v : kWinlogonCritical)
                if (_wcsicmp(name.c_str(), v) == 0) return true;
            return false;
        }

        // Yedek anahtarı yalnız HKCU altında tutulur: HKLM'e yedek yazmak her
        // seferinde UAC isterdi; aynı makinede birden fazla kullanıcı varsa her
        // birinin kendi yedek listesi olması zaten mantıklı.
        const wchar_t* ScopeBackupSubkey(Scope s)
        {
            switch (s)
            {
                case Scope::UserRun:        return L"Software\\TENGRI\\StartupDisabled\\UserRun";
                case Scope::UserRunOnce:    return L"Software\\TENGRI\\StartupDisabled\\UserRunOnce";
                case Scope::MachineRun:     return L"Software\\TENGRI\\StartupDisabled\\MachineRun";
                case Scope::MachineRunOnce: return L"Software\\TENGRI\\StartupDisabled\\MachineRunOnce";
                case Scope::PolicyExplorerRun:      return L"Software\\TENGRI\\StartupDisabled\\PolicyExplorerRun";
                case Scope::PolicyExplorerRunMachine: return L"Software\\TENGRI\\StartupDisabled\\PolicyExplorerRunMachine";
            }
            return L"";
        }

        bool IsWinlogonScope(Scope s)
        {
            return s == Scope::WinlogonUser || s == Scope::WinlogonMachine;
        }

        bool IsFolderScope(Scope s)
        {
            return s == Scope::StartupFolderUser || s == Scope::StartupFolderCommon;
        }

        // Startup klasörü kapsamlarının gerçek yolu. Ortam değişkenine bağlı
        // olduğu için çalışma zamanında çözülür; değişken yoksa boş döner ve
        // kapsam tarama dışında kalır. Uydurma bir yol taramak, girdileri
        // yanlış yere yazmaktan iyidir.
        std::wstring FolderPath(Scope s)
        {
            wchar_t base[MAX_PATH] = {};
            if (s == Scope::StartupFolderUser)
            {
                if (::GetEnvironmentVariableW(L"APPDATA", base, MAX_PATH) == 0) return {};
            }
            else
            {
                if (::GetEnvironmentVariableW(L"PROGRAMDATA", base, MAX_PATH) == 0) return {};
            }
            return std::wstring(base) + L"\\Microsoft\\Windows\\Start Menu\\Programs\\Startup";
        }

        bool IsCurrentProcessElevated()
        {
            HANDLE token = nullptr;
            if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
            TOKEN_ELEVATION elev = {};
            DWORD size = sizeof(elev);
            BOOL ok = ::GetTokenInformation(token, TokenElevation, &elev, size, &size);
            ::CloseHandle(token);
            return ok && elev.TokenIsElevated;
        }

        // "C:\Path\with spaces\app.exe" --flag  →  C:\Path\with spaces\app.exe
        //
        // Tırnaklı yolu olduğu gibi al; tırnaksız yollarda ilk .exe uzantısına
        // kadar oku, yoksa ilk boşluğa kadar al. Hiçbir ayrıştırma kusursuz
        // değildir ama bu üç kural yaygın girdilerin büyük çoğunluğunu kapsar.
        std::wstring ParseExePath(const std::wstring& command)
        {
            if (command.empty()) return {};

            // Baştaki boşlukları atla.
            size_t i = 0;
            while (i < command.size() && std::iswspace(command[i])) ++i;
            if (i >= command.size()) return {};

            if (command[i] == L'"')
            {
                size_t end = command.find(L'"', i + 1);
                if (end == std::wstring::npos) return command.substr(i + 1);
                return command.substr(i + 1, end - i - 1);
            }

            // .exe sınırını ara (büyük/küçük harf duyarsız).
            std::wstring lower = command.substr(i);
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](wchar_t c) { return (wchar_t)std::towlower(c); });
            size_t extPos = lower.find(L".exe");
            if (extPos != std::wstring::npos)
                return command.substr(i, extPos + 4);

            // En kötü durum: boşluğa kadar al.
            size_t sp = command.find_first_of(L" \t", i);
            if (sp == std::wstring::npos) return command.substr(i);
            return command.substr(i, sp - i);
        }

        std::wstring ReadCompanyName(const std::wstring& path)
        {
            if (path.empty()) return {};
            DWORD dummy = 0;
            const DWORD size = ::GetFileVersionInfoSizeW(path.c_str(), &dummy);
            if (size == 0) return {};

            std::vector<unsigned char> buf(size);
            if (!::GetFileVersionInfoW(path.c_str(), 0, size, buf.data())) return {};

            // Dil/kod sayfası tablosundan ilk girişi al; çoğu imzalı exe için
            // yalnız bir çeviri bloğu vardır.
            struct LangCp { WORD lang; WORD cp; } *tr = nullptr;
            UINT trLen = 0;
            if (!::VerQueryValueW(buf.data(), L"\\VarFileInfo\\Translation",
                                  reinterpret_cast<void**>(&tr), &trLen) || trLen < sizeof(LangCp))
                return {};

            wchar_t sub[64] = {};
            ::swprintf_s(sub, L"\\StringFileInfo\\%04x%04x\\CompanyName", tr[0].lang, tr[0].cp);
            wchar_t* value = nullptr;
            UINT valueLen = 0;
            if (!::VerQueryValueW(buf.data(), sub, reinterpret_cast<void**>(&value), &valueLen) || valueLen == 0)
                return {};
            return std::wstring(value, valueLen - 1);
        }

        // Bir yolun son bileşenini verilen dosya adıyla karşılaştırır,
        // büyük/küçük harf duyarsız. Shell/Userinit karşılaştırması için:
        // Windows bunları mutlak yol olarak yazar ama kurulum yerine göre
        // değişir (D:\Windows\...), yalnız adı güvenilir.
        bool EndsWithExe(const std::wstring& value, const wchar_t* exeName)
        {
            std::wstring lower = value;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](wchar_t c) { return (wchar_t)std::towlower(c); });

            std::wstring needle(exeName);
            std::transform(needle.begin(), needle.end(), needle.begin(),
                           [](wchar_t c) { return (wchar_t)std::towlower(c); });

            if (lower.size() < needle.size()) return false;
            return lower.compare(lower.size() - needle.size(), needle.size(), needle) == 0;
        }

        // Winlogon\Shell standart değeri explorer.exe'dir. Userinit ise
        // "userinit.exe," ile BİTMEK ZORUNDADIR: Windows sonuna virgül
        // yazar ve oraya eklenen ikinci yol (virüsün ShellInjection
        // yöntemi) sapma olarak görünür. Bu ayrım önemli, çünkü
        // Userinit'in tamamını reddetmek her makinede yanlış alarm üretirdi.
        bool IsWinlogonAbnormal(const std::wstring& name, const std::wstring& value)
        {
            if (_wcsicmp(name.c_str(), L"Shell") == 0)
                return !EndsWithExe(value, L"explorer.exe");

            if (_wcsicmp(name.c_str(), L"Userinit") == 0)
            {
                if (!EndsWithExe(value, L"userinit.exe,") && !EndsWithExe(value, L"userinit.exe"))
                    return true;
                // Varsayılan tek yol; ek yol yok.
                std::wstring trimmed = value;
                while (!trimmed.empty() && (trimmed.back() == L' ' || trimmed.back() == L','))
                    trimmed.pop_back();
                return trimmed.find(L',') != std::wstring::npos;
            }
            return false;
        }

        Impact GuessImpact(const std::wstring& path)
        {
            if (path.empty()) return Impact::Unknown;

            HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            if (h == INVALID_HANDLE_VALUE) return Impact::Unknown;

            LARGE_INTEGER sz = {};
            BOOL ok = ::GetFileSizeEx(h, &sz);
            ::CloseHandle(h);
            if (!ok) return Impact::Unknown;

            // Çok kaba bir sezgisel: büyük ikili dosyalar yüklenirken daha fazla
            // iş yapma eğilimindedir. Gerçek etki için ETW/Prefetch gerekir;
            // şimdilik görsel bir ipucu için yeter.
            const double mb = static_cast<double>(sz.QuadPart) / (1024.0 * 1024.0);
            if (mb < 2.0)   return Impact::Low;
            if (mb < 20.0)  return Impact::Medium;
            return Impact::High;
        }

        bool IsTengriEntry(const std::wstring& name, const std::wstring& command)
        {
            if (_wcsicmp(name.c_str(), L"TENGRI") == 0) return true;
            // Komut uygulamanın kendi yoluyla eşleşiyorsa.
            wchar_t self[MAX_PATH] = {};
            if (::GetModuleFileNameW(nullptr, self, MAX_PATH))
            {
                std::wstring selfPath = self;
                std::wstring lowerCmd = command;
                std::wstring lowerSelf = selfPath;
                std::transform(lowerCmd.begin(), lowerCmd.end(), lowerCmd.begin(),
                               [](wchar_t c) { return (wchar_t)std::towlower(c); });
                std::transform(lowerSelf.begin(), lowerSelf.end(), lowerSelf.begin(),
                               [](wchar_t c) { return (wchar_t)std::towlower(c); });
                if (lowerCmd.find(lowerSelf) != std::wstring::npos) return true;
            }
            return false;
        }

        void EnumerateKey(HKEY root, const wchar_t* subkey, Scope scope, bool liveMarker,
                          std::vector<Entry>& out)
        {
            // Winlogon'da yalnızca Shell/Userinit listelenir. Anahtarda
            // Taskman, GINA, ReportFault gibi kırk değer daha vardır ve
            // hiçbiri başlangıç öğesi değildir.
            const bool winlogon = IsWinlogonScope(scope);

            HKEY key = nullptr;
            if (::RegOpenKeyExW(root, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) return;

            for (DWORD i = 0; ; ++i)
            {
                wchar_t name[16384] = {};
                DWORD nameLen = _countof(name);
                DWORD type = 0;
                std::vector<unsigned char> data(32768);
                DWORD dataLen = static_cast<DWORD>(data.size());

                LONG r = ::RegEnumValueW(key, i, name, &nameLen, nullptr, &type,
                                         data.data(), &dataLen);
                if (r == ERROR_NO_MORE_ITEMS) break;
                if (r == ERROR_MORE_DATA) continue;
                if (r != ERROR_SUCCESS) break;
                if (type != REG_SZ && type != REG_EXPAND_SZ) continue;
                if (winlogon && !IsListedWinlogonValue(name)) continue;

                // Veri NUL-sonlu değilse elle sonlandır.
                if (dataLen >= 2)
                {
                    const wchar_t* raw = reinterpret_cast<const wchar_t*>(data.data());
                    size_t chars = dataLen / sizeof(wchar_t);
                    while (chars > 0 && raw[chars - 1] == L'\0') --chars;

                    Entry e = {};
                    e.scope = scope;
                    e.name = std::wstring(name, nameLen);
                    e.command = std::wstring(raw, chars);
                    e.resolvedPath = ParseExePath(e.command);
                    e.publisher = ReadCompanyName(e.resolvedPath);
                    e.enabled = liveMarker;
                    e.isTengri = IsTengriEntry(e.name, e.command);
                    e.impact = GuessImpact(e.resolvedPath);
                    e.abnormal = IsWinlogonAbnormal(e.name, e.command);
                    // Sapmış bir Winlogon değeri her şeyden büyüktür: oturumu
                    // ele geçirir. Dosya boyutu tahmininin üstüne konur.
                    if (e.abnormal) e.impact = Impact::High;
                    out.push_back(std::move(e));
                }
            }

            ::RegCloseKey(key);
        }

        // Devre dışı bırakılan Startup dosyalarının tutulduğu klasör. Registry
        // yedeklerinin karşılığı burada bir dosyadır: girdi silinmez, taşınır.
        std::wstring FolderDisabledDir()
        {
            wchar_t appdata[MAX_PATH] = {};
            if (::GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH) == 0) return {};
            return std::wstring(appdata) + L"\\TENGRI\\startup-disabled";
        }

        void EnumerateFolder(Scope scope, const std::wstring& dir, bool liveMarker,
                             std::vector<Entry>& out)
        {
            if (dir.empty()) return;

            WIN32_FIND_DATAW fd = {};
            const std::wstring pattern = dir + L"\\*";
            HANDLE find = ::FindFirstFileW(pattern.c_str(), &fd);
            if (find == INVALID_HANDLE_VALUE) return;

            do
            {
                // Klasörler başlangıç öğesi değildir; alt klasörlerin
                // içeriği Windows tarafından taranmaz.
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

                Entry e = {};
                e.scope = scope;
                e.name = fd.cFileName;
                e.command = dir + L"\\" + fd.cFileName;
                e.resolvedPath = e.command;
                // Kısayol (.lnk) bir PE dosyası değildir; VERSIONINFO okuması
                // boş döner ve arayüz yolu gösterir. Doğru davranış bu.
                e.publisher = ReadCompanyName(e.resolvedPath);
                e.enabled = liveMarker;
                e.isTengri = IsTengriEntry(e.name, e.command);
                e.impact = GuessImpact(e.resolvedPath);
                e.abnormal = false;
                out.push_back(std::move(e));
            } while (::FindNextFileW(find, &fd));

            ::FindClose(find);
        }

        bool WriteValue(HKEY root, const wchar_t* subkey, const std::wstring& name,
                        const std::wstring& data)
        {
            HKEY key = nullptr;
            if (::RegCreateKeyExW(root, subkey, 0, nullptr, 0, KEY_SET_VALUE,
                                  nullptr, &key, nullptr) != ERROR_SUCCESS)
                return false;

            const DWORD bytes = static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t));
            LONG r = ::RegSetValueExW(key, name.c_str(), 0, REG_SZ,
                                      reinterpret_cast<const BYTE*>(data.c_str()), bytes);
            ::RegCloseKey(key);
            return r == ERROR_SUCCESS;
        }

        bool DeleteValue(HKEY root, const wchar_t* subkey, const std::wstring& name)
        {
            HKEY key = nullptr;
            if (::RegOpenKeyExW(root, subkey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
                return true;  // yoksa zaten silinmiş sayılır
            LONG r = ::RegDeleteValueW(key, name.c_str());
            ::RegCloseKey(key);
            return r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND;
        }

        bool ReadValue(HKEY root, const wchar_t* subkey, const std::wstring& name,
                       std::wstring& out)
        {
            HKEY key = nullptr;
            if (::RegOpenKeyExW(root, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS)
                return false;

            DWORD type = 0;
            DWORD bytes = 0;
            LONG r = ::RegQueryValueExW(key, name.c_str(), nullptr, &type, nullptr, &bytes);
            if (r != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
            {
                ::RegCloseKey(key);
                return false;
            }

            std::vector<unsigned char> buf(bytes + sizeof(wchar_t));
            r = ::RegQueryValueExW(key, name.c_str(), nullptr, &type, buf.data(), &bytes);
            ::RegCloseKey(key);
            if (r != ERROR_SUCCESS) return false;

            const wchar_t* raw = reinterpret_cast<const wchar_t*>(buf.data());
            size_t chars = bytes / sizeof(wchar_t);
            while (chars > 0 && raw[chars - 1] == L'\0') --chars;
            out.assign(raw, chars);
            return true;
        }
    }

    const wchar_t* ScopeLabel(Scope s)
    {
        switch (s)
        {
            case Scope::UserRun:                return L"HKCU\\Run";
            case Scope::UserRunOnce:            return L"HKCU\\RunOnce";
            case Scope::MachineRun:             return L"HKLM\\Run";
            case Scope::MachineRunOnce:         return L"HKLM\\RunOnce";
            case Scope::PolicyExplorerRun:      return L"HKCU\\Policy\\Explorer\\Run";
            case Scope::PolicyExplorerRunMachine: return L"HKLM\\Policy\\Explorer\\Run";
            case Scope::StartupFolderUser:      return L"Startup (user)";
            case Scope::StartupFolderCommon:    return L"Startup (all users)";
            case Scope::WinlogonUser:           return L"HKCU\\Winlogon";
            case Scope::WinlogonMachine:        return L"HKLM\\Winlogon";
        }
        return L"?";
    }

    bool IsToggleable(Scope s)
    {
        // Winlogon'un Shell/Userinit değerleri oturumun kendisidir. Kapatmak
        // oturumu açılmaz yapar ve düzeltmek başka bir oturum açmayı gerektirir;
        // bu yüzden yalnızca gösterilirler.
        return !IsWinlogonScope(s);
    }

    bool NeedsElevation(Scope s)
    {
        if (IsFolderScope(s))     return s == Scope::StartupFolderCommon;
        if (IsWinlogonScope(s))   return s == Scope::WinlogonMachine;
        return kScopes[static_cast<int>(s)].isMachine;
    }

    std::vector<Entry> Enumerate()
    {
        std::vector<Entry> out;
        out.reserve(32);

        for (int s = 0; s < 8; ++s)
        {
            const ScopeDef& def = kScopes[s];
            const Scope scope = static_cast<Scope>(s);

            // Canlı girdiler.
            EnumerateKey(def.root, def.subkey, scope, /*liveMarker=*/true, out);

            // Winlogon'un yedeği yoktur (kapsam salt-okunur); boş dönen bir
            // alt anahtarı okumak, olmayan bir yolu aramaktan başka anlam
            // taşımaz.
            const wchar_t* backupSub = ScopeBackupSubkey(scope);
            if (*backupSub)
                EnumerateKey(HKEY_CURRENT_USER, backupSub, scope, /*liveMarker=*/false, out);
        }

        // Startup klasörleri. Ortak klasör önce taranır: ortak bir girdi
        // kullanıcı girdisinden daha fazla etkilidir ve listede üstte görünür.
        for (int s = 6; s <= 7; ++s)
        {
            const Scope scope = static_cast<Scope>(s);
            const std::wstring dir = FolderPath(scope);
            if (dir.empty()) continue;

            EnumerateFolder(scope, dir, /*liveMarker=*/true, out);

            // Devre dışı bırakılmış dosyalar tek bir klasörde toplanır; adı
            // değil, tam yolu saklanır ki geri alırken doğru klasöre dönsün.
            const std::wstring disabled = FolderDisabledDir();
            if (!disabled.empty()) EnumerateFolder(scope, disabled, /*liveMarker=*/false, out);
        }

        return out;
    }

    bool SetEnabledFolder(const Entry& e, bool enable)
    {
        const std::wstring src = FolderPath(e.scope);
        const std::wstring disabledDir = FolderDisabledDir();
        if (src.empty() || disabledDir.empty()) return false;

        if (enable)
        {
            // Devre dışı klasöründen geri alınan dosya: hedef yol, saklanan tam
            // yoldur. Enable edilen girdi kayıttan okunduğu için komutu
            // hazır gelir.
            if (::MoveFileExW(e.resolvedPath.c_str(), e.command.c_str(), MOVEFILE_REPLACE_EXISTING))
                return true;
            return false;
        }

        // Kapatma: dosyayı silmek yerine taşı. Taşıma başarısızsa (dosya
        // kilitli, klasör salt-okunur) girdi yerinde kalır; kullanıcı hiçbir
        // şey kaybetmez.
        if (!::CreateDirectoryW(disabledDir.c_str(), nullptr) &&
            ::GetLastError() != ERROR_ALREADY_EXISTS)
            return false;

        // Aynı adda bir dosya zaten yedekteyse üstüne yazılmaz: eski girdi
        // kaybolur ve geri alma tek bir dosyayı değil, yanlış dosyayı döndürür.
        const std::wstring dest = disabledDir + L"\\" + e.name;
        if (::GetFileAttributesW(dest.c_str()) != INVALID_FILE_ATTRIBUTES) return false;

        return ::MoveFileExW(e.command.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
    }

    bool SetEnabled(const Entry& e, bool enable)
    {
        // Kapatılabilir olmayan kapsam: sessizce yazma başlamaz. Arayüz
        // düğmeleri kapalı gösterdiği için buraya normalde düşülmez; kapı
        // yine de koyulur, çünkü kapsam listesi elle tutuluyor ve yeni bir
        // kapsam eklenirken buradaki şart unutulabilir.
        if (!IsToggleable(e.scope)) return false;

        if (IsFolderScope(e.scope)) return SetEnabledFolder(e, enable);

        const ScopeDef& def = kScopes[static_cast<int>(e.scope)];

        // HKLM yazmak için elevate gerekir. Çağırana haber ver.
        if (def.isMachine && !IsCurrentProcessElevated()) return false;

        const wchar_t* backupSub = ScopeBackupSubkey(e.scope);

        if (enable)
        {
            // Yedek anahtardan canlıya taşı. Komut ya girdinin kendisinde ya da
            // (eski bir yedek sürümünden gelebilir) yedek anahtarda saklıdır.
            std::wstring cmd = e.command;
            if (cmd.empty())
                ReadValue(HKEY_CURRENT_USER, backupSub, e.name, cmd);
            if (cmd.empty()) return false;

            if (!WriteValue(def.root, def.subkey, e.name, cmd)) return false;
            DeleteValue(HKEY_CURRENT_USER, backupSub, e.name);
            return true;
        }
        else
        {
            // Canlıdan oku; yedeğe kopyala; canlıdan sil.
            std::wstring cmd = e.command;
            if (cmd.empty() && !ReadValue(def.root, def.subkey, e.name, cmd))
                return false;

            if (!WriteValue(HKEY_CURRENT_USER, backupSub, e.name, cmd)) return false;
            if (!DeleteValue(def.root, def.subkey, e.name))
            {
                // Yedek yazıldı ama canlı silinemedi: yedeği de kaldır ki
                // enumerate'te hem canlı hem yedekte görünüp kullanıcıyı
                // şaşırtmasın.
                DeleteValue(HKEY_CURRENT_USER, backupSub, e.name);
                return false;
            }
            return true;
        }
    }

    bool Remove(const Entry& e)
    {
        // Winlogon kalıcı olarak silinmez: oturum açma zincirinin parçasıdır
        // ve geri almanın tek yolu başka bir oturum açmaktır. Buna izin vermek
        // "temizlik" değil, kalıcı hasar verir.
        if (!IsToggleable(e.scope)) return false;

        if (IsFolderScope(e.scope))
        {
            if (NeedsElevation(e.scope) && !IsCurrentProcessElevated()) return false;
            // Kaldırma, girdiyi kalıcı siler: önce canlı konumdan, sonra
            // yedekten. Canlı yoksa yalnız yedek silinir.
            bool ok = true;
            if (!e.command.empty()) ok = ::DeleteFileW(e.command.c_str()) || !e.enabled;
            ok = ::DeleteFileW(e.resolvedPath.c_str()) && ok;
            return ok;
        }

        const ScopeDef& def = kScopes[static_cast<int>(e.scope)];
        if (def.isMachine && !IsCurrentProcessElevated()) return false;

        bool ok1 = DeleteValue(def.root, def.subkey, e.name);
        bool ok2 = DeleteValue(HKEY_CURRENT_USER, ScopeBackupSubkey(e.scope), e.name);
        return ok1 && ok2;
    }
}
