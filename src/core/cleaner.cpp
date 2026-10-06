#include "cleaner.hpp"
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <vector>

namespace cleaner
{
    namespace
    {
        std::wstring Env(const wchar_t* var)
        {
            wchar_t buf[MAX_PATH];
            DWORD n = GetEnvironmentVariableW(var, buf, MAX_PATH);
            return n > 0 ? std::wstring(buf, n) : L"";
        }

        // Önbellek klasörlerinin içindeki junction ve sembolik bağlantılar
        // istediğimiz yere işaret edebilir; sıkıştırma araçlarının bıraktığı "_"
        // bağlantıları ise doğrudan üst klasöre geri döner. Bunların içine
        // girildiğinde sınırlı bir tarama sınırsız bir gezintiye dönüşür, bu
        // yüzden dolu bir yaprak sayılır, içi asla taranmaz.
        bool IsReparse(const WIN32_FIND_DATAW& fd)
        {
            return (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        }

        bool IsDotDir(const wchar_t* name)
        {
            return name[0] == L'.' &&
                   (name[1] == 0 || (name[1] == L'.' && name[2] == 0));
        }

        void AddFile(const WIN32_FIND_DATAW& fd, double& mb, int& count)
        {
            ULARGE_INTEGER sz;
            sz.LowPart  = fd.nFileSizeLow;
            sz.HighPart = fd.nFileSizeHigh;
            mb += (double)sz.QuadPart / (1024.0 * 1024.0);
            count++;
        }

        void ScanDir(const std::wstring& path, double& mb, int& count, bool recurse = true)
        {
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW((path + L"\\*").c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE) return;
            do {
                if (IsDotDir(fd.cFileName)) continue;
                std::wstring full = path + L"\\" + fd.cFileName;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                {
                    if (recurse && !IsReparse(fd)) ScanDir(full, mb, count, true);
                }
                else
                {
                    AddFile(fd, mb, count);
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }

        void ScanPattern(const std::wstring& dir, const wchar_t* pattern, double& mb, int& count)
        {
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW((dir + L"\\" + pattern).c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE) return;
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                    AddFile(fd, mb, count);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }

        int CleanDir(const std::wstring& path, bool recurse = true)
        {
            int deleted = 0;
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW((path + L"\\*").c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE) return 0;
            do {
                if (IsDotDir(fd.cFileName)) continue;
                std::wstring full = path + L"\\" + fd.cFileName;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                {
                    if (recurse && !IsReparse(fd)) deleted += CleanDir(full, true);
                    // Yalnızca boş bir yaprağı kaldırır; yerinde duran bir bağlantı
                    // hiçbir zaman boşalmış bir klasör sanılmaz.
                    if (!IsReparse(fd)) RemoveDirectoryW(full.c_str());
                }
                else
                {
                    SetFileAttributesW(full.c_str(), FILE_ATTRIBUTE_NORMAL);
                    if (DeleteFileW(full.c_str())) deleted++;
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
            return deleted;
        }

        int CleanPattern(const std::wstring& dir, const wchar_t* pattern)
        {
            int deleted = 0;
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW((dir + L"\\" + pattern).c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE) return 0;
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                {
                    std::wstring full = dir + L"\\" + fd.cFileName;
                    SetFileAttributesW(full.c_str(), FILE_ATTRIBUTE_NORMAL);
                    if (DeleteFileW(full.c_str())) deleted++;
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
            return deleted;
        }

        // Firefox önbelleğini <profil>\cache2 içinde tutar; oturum bilgileri,
        // yer imleri, eklentiler ve ayarlar ise bir üst dizinde durur. Bu yüzden
        // profil kökü asla temizleme fonksiyonuna verilmez, her profilin yalnızca
        // "cache2" adlı alt klasörü tek tek hedeflenir.
        void ForEachFirefoxCache(const std::wstring& local, bool scan, double* mb, int* count)
        {
            const std::wstring profiles = local + L"\\Mozilla\\Firefox\\Profiles";
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW((profiles + L"\\*").c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE) return;
            do {
                if (IsDotDir(fd.cFileName)) continue;
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || IsReparse(fd)) continue;
                const std::wstring cache = profiles + L"\\" + fd.cFileName + L"\\cache2";
                if (scan && mb && count) ScanDir(cache, *mb, *count);
                else if (!scan)            CleanDir(cache);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }

        // Windows Update, SoftwareDistribution\Download içindeki dosyaları açık
        // tuttuğu için düz silme önbelleğin çoğunu yerinde bırakıyor. Bu
        // dosyaları elinde tutan iki hizmet (wuauserv ve BITS) işlem boyunca
        // durdurulur, sonra yeniden başlatılır. Her iki komut da yetki eksikliği
        // ya da hizmetin zaten devre dışı olması gibi durumlarda sessizce
        // başarısız olabilir; bu yüzden yalnızca biz durdurduysak yeniden
        // başlatırız.
        bool RunCmd(const wchar_t* cmd)
        {
            STARTUPINFOW si = { sizeof(si) };
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi = {};
            wchar_t buf[128];
            wcscpy_s(buf, cmd);
            if (!CreateProcessW(nullptr, buf, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
                return false;

            // Zaman aşımında alt süreç sonlandırılır: aksi hâlde çıkış kodu hâlâ
            // "çalışıyor" olarak kalır ve komut gözetimsiz çalışmaya devam eder.
            const DWORD wait = WaitForSingleObject(pi.hProcess, 15000);
            if (wait == WAIT_TIMEOUT)
            {
                ::TerminateProcess(pi.hProcess, 1);
                ::WaitForSingleObject(pi.hProcess, 1000);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                return false;
            }

            DWORD code = 1;
            GetExitCodeProcess(pi.hProcess, &code);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return code == 0;
        }

        // Olay günlükleri yalnızca eklenebilir ve günlük tutan servis onları açık
        // tutar; rapora sayılırlar ama bilerek silinmezler.
        constexpr bool kDeleteEventLogs = false;

        struct Roots
        {
            std::wstring temp, local, win;
        };

        Roots Resolve()
        {
            Roots r;
            r.temp  = Env(L"TEMP");
            r.local = Env(L"LOCALAPPDATA");
            r.win   = Env(L"WINDIR");
            return r;
        }
    }

    ScanResult Scan(int cat)
    {
        ScanResult r;
        const Roots env = Resolve();

        switch (cat)
        {
        case 0:
            if (!env.temp.empty()) ScanDir(env.temp, r.sizeMB, r.fileCount);
            if (!env.win.empty())  ScanDir(env.win + L"\\Temp", r.sizeMB, r.fileCount);
            break;
        case 1:
            if (!env.local.empty())
            {
                ScanDir(env.local + L"\\Google\\Chrome\\User Data\\Default\\Cache", r.sizeMB, r.fileCount);
                ScanDir(env.local + L"\\Google\\Chrome\\User Data\\Default\\Code Cache", r.sizeMB, r.fileCount);
                ScanDir(env.local + L"\\Microsoft\\Edge\\User Data\\Default\\Cache", r.sizeMB, r.fileCount);
                ScanDir(env.local + L"\\Microsoft\\Edge\\User Data\\Default\\Code Cache", r.sizeMB, r.fileCount);
                ForEachFirefoxCache(env.local, true, &r.sizeMB, &r.fileCount);
                ScanDir(env.local + L"\\Opera Software\\Opera Stable\\Cache", r.sizeMB, r.fileCount);
                ScanDir(env.local + L"\\BraveSoftware\\Brave-Browser\\User Data\\Default\\Cache", r.sizeMB, r.fileCount);
            }
            break;
        case 2:
            if (!env.win.empty()) ScanDir(env.win + L"\\SoftwareDistribution\\Download", r.sizeMB, r.fileCount);
            break;
        case 3:
        {
            SHQUERYRBINFO info = { sizeof(info) };
            if (SUCCEEDED(SHQueryRecycleBinW(nullptr, &info)))
            {
                r.sizeMB    = (double)info.i64Size / (1024.0 * 1024.0);
                r.fileCount = (int)info.i64NumItems;
            }
            break;
        }
        case 4:
            if (!env.win.empty()) ScanDir(env.win + L"\\Prefetch", r.sizeMB, r.fileCount);
            break;
        case 5:
            if (!env.win.empty())
            {
                ScanDir(env.win + L"\\Logs", r.sizeMB, r.fileCount);
                ScanDir(env.win + L"\\System32\\LogFiles", r.sizeMB, r.fileCount);
                if (kDeleteEventLogs)
                    ScanPattern(env.win + L"\\System32\\winevt\\Logs", L"*.evtx", r.sizeMB, r.fileCount);
            }
            break;
        case 6:
            if (!env.local.empty())
                ScanPattern(env.local + L"\\Microsoft\\Windows\\Explorer", L"thumbcache_*.db", r.sizeMB, r.fileCount);
            break;
        case 7:
            if (!env.local.empty()) ScanDir(env.local + L"\\CrashDumps", r.sizeMB, r.fileCount);
            if (!env.win.empty())
            {
                ScanDir(env.win + L"\\Minidump", r.sizeMB, r.fileCount);
                ScanPattern(env.win, L"MEMORY.DMP", r.sizeMB, r.fileCount);
            }
            break;
        case 8:
            if (!env.local.empty())
            {
                ScanDir(env.local + L"\\D3DSCache", r.sizeMB, r.fileCount);
                ScanDir(env.local + L"\\NVIDIA\\DXCache", r.sizeMB, r.fileCount);
                ScanDir(env.local + L"\\NVIDIA\\GLCache", r.sizeMB, r.fileCount);
                ScanDir(env.local + L"\\AMD\\DXCache", r.sizeMB, r.fileCount);
            }
            break;
        case 9:
            if (!env.win.empty()) ScanDir(env.win + L"\\SoftwareDistribution\\DeliveryOptimization", r.sizeMB, r.fileCount);
            break;
        }
        return r;
    }

    double Clean(int cat)
    {
        double before = Scan(cat).sizeMB;
        const Roots env = Resolve();

        switch (cat)
        {
        case 0:
            if (!env.temp.empty()) CleanDir(env.temp);
            if (!env.win.empty())  CleanDir(env.win + L"\\Temp");
            break;
        case 1:
            if (!env.local.empty())
            {
                CleanDir(env.local + L"\\Google\\Chrome\\User Data\\Default\\Cache");
                CleanDir(env.local + L"\\Google\\Chrome\\User Data\\Default\\Code Cache");
                CleanDir(env.local + L"\\Microsoft\\Edge\\User Data\\Default\\Cache");
                CleanDir(env.local + L"\\Microsoft\\Edge\\User Data\\Default\\Code Cache");
                ForEachFirefoxCache(env.local, false, nullptr, nullptr);
                CleanDir(env.local + L"\\Opera Software\\Opera Stable\\Cache");
                CleanDir(env.local + L"\\BraveSoftware\\Brave-Browser\\User Data\\Default\\Cache");
            }
            break;
        case 2:
            if (!env.win.empty())
            {
                const bool wasBITS  = RunCmd(L"net stop BITS /y");
                const bool wasWUA   = RunCmd(L"net stop wuauserv /y");
                CleanDir(env.win + L"\\SoftwareDistribution\\Download");
                if (wasWUA) RunCmd(L"net start wuauserv");
                if (wasBITS) RunCmd(L"net start BITS");
            }
            break;
        case 3:
            SHEmptyRecycleBinW(nullptr, nullptr, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
            return before;
        case 4:
            if (!env.win.empty()) CleanDir(env.win + L"\\Prefetch");
            break;
        case 5:
            if (!env.win.empty())
            {
                CleanDir(env.win + L"\\Logs");
                CleanDir(env.win + L"\\System32\\LogFiles");
                if (kDeleteEventLogs) CleanPattern(env.win + L"\\System32\\winevt\\Logs", L"*.evtx");
            }
            break;
        case 6:
            if (!env.local.empty())
                CleanPattern(env.local + L"\\Microsoft\\Windows\\Explorer", L"thumbcache_*.db");
            break;
        case 7:
            if (!env.local.empty()) CleanDir(env.local + L"\\CrashDumps");
            if (!env.win.empty())
            {
                CleanDir(env.win + L"\\Minidump");
                CleanPattern(env.win, L"MEMORY.DMP");
            }
            break;
        case 8:
            if (!env.local.empty())
            {
                CleanDir(env.local + L"\\D3DSCache");
                CleanDir(env.local + L"\\NVIDIA\\DXCache");
                CleanDir(env.local + L"\\NVIDIA\\GLCache");
                CleanDir(env.local + L"\\AMD\\DXCache");
            }
            break;
        case 9:
            if (!env.win.empty()) CleanDir(env.win + L"\\SoftwareDistribution\\DeliveryOptimization");
            break;
        }

        double after = Scan(cat).sizeMB;
        return before > after ? before - after : 0.0;
    }
}
