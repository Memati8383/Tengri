#include "license.hpp"
#include "sysinfo.hpp"
#include "lang.hpp"
#include "brand.hpp"
#include <windows.h>
#include <future>
#include <chrono>
#include <thread>
#include <fstream>
#include <ctime>
#include <cctype>
#include <cstdlib>

namespace license
{
    namespace
    {
        struct Result
        {
            bool        ok = false;
            std::string error;
            Info        info;
        };

        std::future<Result> g_task;
        Status              g_status = Status::Idle;
        std::string         g_error;
        Info                g_info;

        // Tire ve boşlukları atıp harfleri büyük harfe çevirir. Anahtar
        // içinde izin verilmeyen bir karakter varsa boş dize döner.
        std::string Normalize(const std::string& key)
        {
            std::string s;
            for (char c : key)
            {
                if (std::isalnum((unsigned char)c))
                    s += (char)std::toupper((unsigned char)c);
                else if (c != '-' && c != ' ')
                    return {};
            }
            return s;
        }

        std::string Pretty(const std::string& n)
        {
            std::string s;
            for (size_t i = 0; i < n.size(); ++i)
            {
                if (i && i % 4 == 0)
                    s += '-';
                s += n[i];
            }
            return s;
        }

        Result Validate(std::string key, std::string hwid)
        {
            // Gerçek bir sunucuya gidiş-dönüş süresini taklit eden gecikme;
            // arayüzün yükleniyor durumunu görebilmesi için gerekli.
            std::this_thread::sleep_for(std::chrono::milliseconds(1400));

            // ------------------------------------------------------------------
            // DEMO DOĞRULAMA. Bu blok yalnızca ekranı göstermek içindir; hiçbir
            // ağ isteği yapılmaz, anahtar sunucuya gönderilmez ve gerçek bir
            // kontrol uygulanmaz. Yayına çıkarmadan önce burayı kendi doğrulama
            // sunucunuza yapacağınız çağrıyla değiştirin: anahtarı ve donanım
            // kimliğini HTTPS üzerinden gönderip imzalı yanıtı doğrulayın.
            // Doğrulama yalnızca istemcide çalışıyorsa lisans koruması sayılmaz.
            // ------------------------------------------------------------------
            Result r;
            std::string n = Normalize(key);
            if (n.empty())
            {
                r.error = lang::Get(S::LicenseKeyError);
                return r;
            }
            while (n.size() < 16) n += 'X';
            if (n.size() > 16) n = n.substr(0, 16);

            r.ok        = true;
            r.info.key  = Pretty(n);
            r.info.user = sys::UserName();
            r.info.hwid = hwid;
            r.info.lifetime = (n.rfind("LIFE", 0) == 0);

            if (!r.info.lifetime)
            {
                // Tarih, sistemin kendi dil bilgisine göre biçimlenir; böylece
                // arayüzün geri kalanı Türkçe olurken sonuçta İngilizce bir ay
                // kısaltması çıkmaz.
                const time_t t = time(nullptr) + 30 * 86400;
                tm tmv = {};
                if (localtime_s(&tmv, &t) != 0) return r;

                SYSTEMTIME st = {};
                st.wYear  = (WORD)(tmv.tm_year + 1900);
                st.wMonth = (WORD)(tmv.tm_mon + 1);
                st.wDay   = (WORD)tmv.tm_mday;

                wchar_t date[64] = {};
                // Kullanılan SDK sürümünde tarih biçimlendirme çağrısı yedinci
                // parametre olarak takvim bilgisini de bekliyor.
                if (GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &st,
                                    nullptr, date, ARRAYSIZE(date), nullptr) > 0)
                {
                    char out[64];
                    WideCharToMultiByte(CP_UTF8, 0, date, -1, out, sizeof(out), nullptr, nullptr);
                    r.info.expires = out;
                }
            }
            return r;
        }

        std::string StoragePath()
        {
            char*  env = nullptr;
            size_t len = 0;
            std::string dir = (_dupenv_s(&env, &len, "APPDATA") == 0 && env) ? env : ".";
            free(env);
            if (dir.empty() || dir == ".") dir = ".";
            dir += "\\";
            dir += brand::kAppDataFolderA;
            CreateDirectoryA(dir.c_str(), nullptr);
            return dir + "\\license.dat";
        }
    }

    void Activate(const std::string& key)
    {
        if (g_status == Status::Checking)
            return;
        g_status = Status::Checking;
        g_error.clear();
        g_task = std::async(std::launch::async, Validate, key, sys::Hwid());
    }

    Status Poll()
    {
        if (g_status == Status::Checking && g_task.valid() &&
            g_task.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            Result r = g_task.get();
            if (r.ok)
            {
                g_status = Status::Valid;
                g_info   = r.info;
            }
            else
            {
                g_status = Status::Invalid;
                g_error  = r.error;
            }
        }
        return g_status;
    }

    void Logout()
    {
        g_status = Status::Idle;
        g_info   = {};
    }

    const std::string& Error()   { return g_error; }
    const Info&        Current() { return g_info; }

    // Anahtar "XXXX-XXXX-XXXX-XXXX" biçiminde 19 karakterdir; son grup 15.
    // konumdan başlar. 15'ten itibaren 4 karakter almak dizinin sonunu aşıp
    // bozuk bir kuyruk üretiyordu, bu yüzden yalnızca 3 karakter alınır.
    std::string Mask(const std::string& key)
    {
        constexpr size_t kPretty = 19;
        if (key.size() != kPretty)
            return key;
        static const char* kDot = "\xE2\x80\xA2"; // U+2022
        std::string out = key.substr(0, 4);
        out += '-';
        for (int i = 0; i < 4; ++i) out += kDot;
        out += '-';
        for (int i = 0; i < 4; ++i) out += kDot;
        out += '-';
        out += key.substr(15, 3);
        return out;
    }

    std::string LoadSaved()
    {
        std::ifstream f(StoragePath());
        std::string s;
        std::getline(f, s);
        return s;
    }

    void Save(const std::string& key)
    {
        std::ofstream f(StoragePath(), std::ios::trunc);
        f << key;
    }

    void ForgetSaved() { DeleteFileA(StoragePath().c_str()); }
}
