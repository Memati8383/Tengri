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

        // Dört kapsamın Windows'taki gerçek konumları. Sıra, Scope enum'uyla
        // birebir aynı; başka yerde elle eşleme yapmamak için indekslenir.
        // (HKEY sabitleri reinterpret-cast içerdiği için constexpr olamaz.)
        const ScopeDef kScopes[4] = {
            { HKEY_CURRENT_USER,  L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",     false, false },
            { HKEY_CURRENT_USER,  L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce", false, true  },
            { HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",     true,  false },
            { HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce", true,  true  },
        };

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
            }
            return L"";
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
                    out.push_back(std::move(e));
                }
            }

            ::RegCloseKey(key);
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
            case Scope::UserRun:        return L"HKCU\\Run";
            case Scope::UserRunOnce:    return L"HKCU\\RunOnce";
            case Scope::MachineRun:     return L"HKLM\\Run";
            case Scope::MachineRunOnce: return L"HKLM\\RunOnce";
        }
        return L"?";
    }

    std::vector<Entry> Enumerate()
    {
        std::vector<Entry> out;
        out.reserve(32);

        for (int s = 0; s < 4; ++s)
        {
            const ScopeDef& def = kScopes[s];
            const Scope scope = static_cast<Scope>(s);

            // Canlı girdiler.
            EnumerateKey(def.root, def.subkey, scope, /*liveMarker=*/true, out);

            // Yedekteki (devre dışı) girdiler. Yedek yalnız HKCU altında olduğu
            // için her kapsamın yedeği aynı kökten okunur.
            EnumerateKey(HKEY_CURRENT_USER, ScopeBackupSubkey(scope), scope,
                         /*liveMarker=*/false, out);
        }

        return out;
    }

    bool SetEnabled(const Entry& e, bool enable)
    {
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
        const ScopeDef& def = kScopes[static_cast<int>(e.scope)];
        if (def.isMachine && !IsCurrentProcessElevated()) return false;

        bool ok1 = DeleteValue(def.root, def.subkey, e.name);
        bool ok2 = DeleteValue(HKEY_CURRENT_USER, ScopeBackupSubkey(e.scope), e.name);
        return ok1 && ok2;
    }
}
