#include "update.hpp"
#include "brand.hpp"
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <functional>
#include <mutex>
#include <vector>
#include <string>
#include <future>
#include <chrono>
#include <atomic>
#include <cctype>
#include <cstdlib>

// winhttp.lib ve bcrypt.lib derleme betiğinde de listeleniyor; pragma burada
// duruyor, çünkü CMake yapılandırması bağımlılıkları hedef dosyasından okuyor ve
// tek bir yerden iki sisteme birden ulaşsın.
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

namespace update
{
    bool enabled = true;

    namespace
    {
        // ---- sabitler --------------------------------------------------------
        // Denetim aralığı. Kısalırsa aynı oturumda defalarca istek atılır;
        // uzarsa bir gün açık kalan pencere ikinci denetimi görmez.
        constexpr unsigned long long kIntervalSec = 24ull * 3600ull;

        // Bildirim dosyasının üst sınırı. Zorunlu dört alan birkaç düzine bayt;
        // geri kalanı yayın notu. 1 MB'ı aşan yanıt ya bozuktur ya da başka bir
        // şeydir ve ikisinde de ayrıştırılmaz.
        constexpr size_t kManifestCap = 1u << 20;

        // İndirme üst sınırı. ParseManifest boyutu 200 MB ile zaten sınırlıyor;
        // burada aynı tavan yazılımda da duruyor ki uyanık bir sunucu akışı
        // sonlandırmayıp belleği/diski dolduramasın.
        constexpr unsigned long long kDownloadCap = 200ull * 1024ull * 1024ull;

        constexpr DWORD kChunk = 64u * 1024u;

        constexpr const wchar_t* kRegPath       = L"Software\\TENGRI\\Update";
        constexpr const wchar_t* kRegEnabled    = L"Enabled";
        constexpr const wchar_t* kRegLastCheck  = L"LastCheck";

        // ---- küçük yardımcılar -----------------------------------------------
        std::wstring W(const std::string& s)
        {
            if (s.empty()) return {};
            const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
            if (n <= 0) return {};
            std::wstring w(static_cast<size_t>(n), L'\0');
            ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
            return w;
        }

        std::wstring W(const char* s) { return W(std::string(s ? s : "")); }

        std::string A(const std::wstring& s)
        {
            if (s.empty()) return {};
            const int n = ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(),
                                                nullptr, 0, nullptr, nullptr);
            if (n <= 0) return {};
            std::string a(static_cast<size_t>(n), '\0');
            ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &a[0], n, nullptr, nullptr);
            return a;
        }

        std::string Lower(std::string s)
        {
            for (char& c : s) c = (char)std::tolower((unsigned char)c);
            return s;
        }

        unsigned long long NowUtc()
        {
            FILETIME ft = {};
            ::GetSystemTimeAsFileTime(&ft);
            const ULONGLONG t = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
            // FILETIME 1601'den itibaren 100 ns birimleriyle sayar.
            if (t < 116444736000000000ULL) return 0;
            return (unsigned long long)((t - 116444736000000000ULL) / 10000000ULL);
        }

        // Çalışan exe'nin tam yolu. Genişletilmiş ön ek (\\?\) bilinçli olarak
        // EKLENMİYOR: elde edilen yol zaten CreateProcess'in beklediği biçimde.
        std::wstring ExePathW()
        {
            std::wstring buf(32768, L'\0');
            const DWORD n = ::GetModuleFileNameW(nullptr, &buf[0], (DWORD)buf.size());
            if (n == 0 || n >= buf.size()) return {};
            buf.resize(n);
            return buf;
        }

        // ---- kayıt defteri ---------------------------------------------------
        // Tercihler HKCU altında: yazmak yetki gerektirmez ve hesap başına durur.
        // Okunamazsa varsayılanlar korunur (açık / hiç denenmemiş).
        void WriteDword(const wchar_t* name, DWORD v)
        {
            HKEY h = nullptr;
            if (::RegCreateKeyExW(HKEY_CURRENT_USER, kRegPath, 0, nullptr,
                                  REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &h,
                                  nullptr) != ERROR_SUCCESS)
                return;
            ::RegSetValueExW(h, name, 0, REG_DWORD,
                             reinterpret_cast<const BYTE*>(&v), sizeof(v));
            ::RegCloseKey(h);
        }

        void WriteQword(const wchar_t* name, unsigned long long v)
        {
            HKEY h = nullptr;
            if (::RegCreateKeyExW(HKEY_CURRENT_USER, kRegPath, 0, nullptr,
                                  REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &h,
                                  nullptr) != ERROR_SUCCESS)
                return;
            ::RegSetValueExW(h, name, 0, REG_QWORD,
                             reinterpret_cast<const BYTE*>(&v), sizeof(v));
            ::RegCloseKey(h);
        }

        bool ReadDword(const wchar_t* name, DWORD* out)
        {
            HKEY h = nullptr;
            if (::RegOpenKeyExW(HKEY_CURRENT_USER, kRegPath, 0, KEY_READ, &h) != ERROR_SUCCESS)
                return false;
            DWORD v = 0, type = 0, size = sizeof(v);
            const bool ok = ::RegQueryValueExW(h, name, nullptr, &type,
                                               reinterpret_cast<BYTE*>(&v), &size) == ERROR_SUCCESS
                         && type == REG_DWORD && size == sizeof(v);
            ::RegCloseKey(h);
            if (ok && out) *out = v;
            return ok;
        }

        bool ReadQword(const wchar_t* name, unsigned long long* out)
        {
            HKEY h = nullptr;
            if (::RegOpenKeyExW(HKEY_CURRENT_USER, kRegPath, 0, KEY_READ, &h) != ERROR_SUCCESS)
                return false;
            unsigned long long v = 0;
            DWORD type = 0, size = sizeof(v);
            const bool ok = ::RegQueryValueExW(h, name, nullptr, &type,
                                               reinterpret_cast<BYTE*>(&v), &size) == ERROR_SUCCESS
                         && type == REG_QWORD && size == sizeof(v);
            ::RegCloseKey(h);
            if (ok && out) *out = v;
            return ok;
        }

        // ---- JSON ------------------------------------------------------------
        // Tam bir ağaç değil, üst üste binmiş nesneleri atlayabilen bir gezi.
        // Yeterli: kendi yayınladığımız bildirim düz bir nesne ve arayüzün
        // ihtiyacı beş alan.
        //
        // Asıl dikkat noktası dize İÇİNİN ayrıştırılmaması. Yayın notlarında
        // tırnak, süslü parantez ve "version": gibi görünen metinler var; bunları
        // alan sanırsak yanlış değeri okuruz. O yüzden akış karakter karakter
        // ilerliyor ve her tırnakta dizenin sonuna kadar atlıyor.
        struct Scan
        {
            const char* p;
            size_t      n;
            size_t      i = 0;
            bool Eof() const { return i >= n; }
            char At() const { return p[i]; }
        };

        void SkipWs(Scan& s)
        {
            while (!s.Eof())
            {
                const char c = s.At();
                if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
                ++s.i;
            }
        }

        bool Hex4(Scan& s, unsigned* out)
        {
            unsigned v = 0;
            for (int k = 0; k < 4; ++k)
            {
                if (s.Eof()) return false;
                const char c = s.At();
                unsigned d;
                if      (c >= '0' && c <= '9') d = (unsigned)(c - '0');
                else if (c >= 'a' && c <= 'f') d = (unsigned)(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') d = (unsigned)(c - 'A' + 10);
                else return false;
                v = v * 16u + d;
                ++s.i;
            }
            *out = v;
            return true;
        }

        void AppendUtf8(std::string* out, unsigned cp)
        {
            if (cp < 0x80u)
            {
                out->push_back((char)cp);
            }
            else if (cp < 0x800u)
            {
                out->push_back((char)(0xC0u | (cp >> 6)));
                out->push_back((char)(0x80u | (cp & 0x3Fu)));
            }
            else if (cp < 0x10000u)
            {
                out->push_back((char)(0xE0u | (cp >> 12)));
                out->push_back((char)(0x80u | ((cp >> 6) & 0x3Fu)));
                out->push_back((char)(0x80u | (cp & 0x3Fu)));
            }
            else
            {
                out->push_back((char)(0xF0u | (cp >> 18)));
                out->push_back((char)(0x80u | ((cp >> 12) & 0x3Fu)));
                out->push_back((char)(0x80u | ((cp >> 6) & 0x3Fu)));
                out->push_back((char)(0x80u | (cp & 0x3Fu)));
            }
        }

        // Açılış tırnağı üzerinde çağrılır; kapanışa kadar okur, kaçışları çözer.
        // Kapanmayan dize ya da tanınmayan kaçış = bozuk bildirim.
        bool ReadString(Scan& s, std::string* out)
        {
            std::string v;
            ++s.i;                                  // açılış "
            while (!s.Eof())
            {
                const char c = s.At();
                ++s.i;
                if (c == '"') { if (out) *out = std::move(v); return true; }
                if (c != '\\') { v.push_back(c); continue; }

                if (s.Eof()) return false;
                const char e = s.At();
                ++s.i;
                switch (e)
                {
                case '"':  v.push_back('"');  break;
                case '\\': v.push_back('\\'); break;
                case '/':  v.push_back('/');  break;
                case 'b':  v.push_back('\b'); break;
                case 'f':  v.push_back('\f'); break;
                case 'n':  v.push_back('\n'); break;
                case 'r':  v.push_back('\r'); break;
                case 't':  v.push_back('\t'); break;
                case 'u':
                {
                    unsigned cp = 0;
                    if (!Hex4(s, &cp)) return false;
                    // Vekil ikilisi: U+10000 ve üstü iki \uXXXX olarak gelir.
                    // İkinci parça yoksa karakter tanımsız; biçimi bozmamak için
                    // yerine "bilinmeyen işaret" konur.
                    if (cp >= 0xD800u && cp <= 0xDBFFu)
                    {
                        const size_t save = s.i;
                        if (s.Eof() || s.At() != '\\') { AppendUtf8(&v, 0xFFFDu); break; }
                        ++s.i;
                        if (s.Eof() || s.At() != 'u') { s.i = save; AppendUtf8(&v, 0xFFFDu); break; }
                        ++s.i;
                        unsigned lo = 0;
                        if (!Hex4(s, &lo)) return false;
                        if (lo < 0xDC00u || lo > 0xDFFFu) { AppendUtf8(&v, 0xFFFDu); break; }
                        cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                    }
                    else if (cp >= 0xDC00u && cp <= 0xDFFFu)
                    {
                        AppendUtf8(&v, 0xFFFDu);   // yetim alt vekil
                        break;
                    }
                    AppendUtf8(&v, cp);
                    break;
                }
                default: return false;
                }
            }
            return false;
        }

        enum class Tok { Bad, Str, Num, Word, Object, Array };

        // Bir değeri okur (gerekirse metnini döndürür) ve tüketir. Derinlik
        // sınarı var: iç içe 64 nesne ayrıştırıcıyı yığını tüketerek durdurabilirdi.
        bool SkipValue(Scan& s, int depth, Tok* kind, std::string* text)
        {
            SkipWs(s);
            if (s.Eof()) return false;
            const char c = s.At();

            if (c == '"')
            {
                if (kind) *kind = Tok::Str;
                return ReadString(s, text);
            }
            if (c == '{' || c == '[')
            {
                if (kind) *kind = (c == '{') ? Tok::Object : Tok::Array;
                if (text) *text = std::string();
                if (depth >= 64) return false;
                const char close = (c == '{') ? '}' : ']';
                ++s.i;
                for (;;)
                {
                    SkipWs(s);
                    if (s.Eof()) return false;
                    if (s.At() == close) { ++s.i; return true; }
                    if (s.At() == ',' || s.At() == ':') { ++s.i; continue; }
                    if (c == '{')
                    {
                        if (s.At() != '"') return false;      // anahtar dize olmalı
                        if (!ReadString(s, nullptr)) return false;
                        SkipWs(s);
                        if (s.Eof() || s.At() != ':') return false;
                        ++s.i;
                    }
                    if (!SkipValue(s, depth + 1, nullptr, nullptr)) return false;
                }
            }
            if (c == '-' || (c >= '0' && c <= '9'))
            {
                const size_t b = s.i;
                while (!s.Eof())
                {
                    const char d = s.At();
                    if ((d >= '0' && d <= '9') || d == '-' || d == '+' ||
                        d == '.' || d == 'e' || d == 'E') ++s.i;
                    else break;
                }
                if (kind) *kind = Tok::Num;
                if (text) text->assign(s.p + b, s.i - b);
                return s.i > b;
            }
            // true / false / null: harf dizisi olarak atlanır.
            const size_t b = s.i;
            while (!s.Eof())
            {
                const char d = s.At();
                if ((d >= 'a' && d <= 'z') || (d >= 'A' && d <= 'Z')) ++s.i;
                else break;
            }
            if (s.i == b) return false;
            if (kind) *kind = Tok::Word;
            if (text) text->assign(s.p + b, s.i - b);
            return true;
        }

        // Üst düzey nesnenin alanlarını dolaşır; her alan için bir kez çağrılır.
        // Tanım dışı alanlar sessizce atlanır: bildirime yeni bir alan eklemek
        // eski bir uygulamayı bozmamalı.
        bool WalkObject(const std::string& json,
                        const std::function<void(const std::string&, Tok, const std::string&)>& fn)
        {
            Scan s{ json.data(), json.size(), 0 };
            SkipWs(s);
            if (s.Eof() || s.At() != '{') return false;
            ++s.i;
            for (;;)
            {
                SkipWs(s);
                if (s.Eof()) return false;
                if (s.At() == '}') return true;
                if (s.At() == ',') { ++s.i; continue; }
                if (s.At() != '"') return false;
                std::string key;
                if (!ReadString(s, &key)) return false;
                SkipWs(s);
                if (s.Eof() || s.At() != ':') return false;
                ++s.i;
                Tok kind = Tok::Bad;
                std::string val;
                if (!SkipValue(s, 0, &kind, &val)) return false;
                fn(key, kind, val);
            }
        }

        // ---- özet ------------------------------------------------------------
        struct Sha256
        {
            BCRYPT_ALG_HANDLE  alg = nullptr;
            BCRYPT_HASH_HANDLE h   = nullptr;
            std::vector<BYTE>  obj;

            ~Sha256() { Reset(); }
            Sha256() = default;
            Sha256(const Sha256&) = delete;
            Sha256& operator=(const Sha256&) = delete;

            void Reset()
            {
                if (h)   { ::BCryptDestroyHash(h); h = nullptr; }
                if (alg) { ::BCryptCloseAlgorithmProvider(alg, 0); alg = nullptr; }
            }
            bool Start()
            {
                if (::BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
                { alg = nullptr; return false; }
                DWORD len = 0, got = 0;
                if (::BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&len, sizeof(len),
                                        &got, 0) != 0 || len == 0)
                { Reset(); return false; }
                obj.assign(len, 0);
                if (::BCryptCreateHash(alg, &h, obj.data(), len, nullptr, 0, 0) != 0)
                { h = nullptr; Reset(); return false; }
                return true;
            }
            bool Add(const void* data, size_t len)
            {
                return len == 0 || ::BCryptHashData(h, (PUCHAR)data, (ULONG)len, 0) == 0;
            }
            std::string Finish()
            {
                DWORD len = 0, got = 0;
                if (::BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, (PUCHAR)&len, sizeof(len),
                                        &got, 0) != 0 || len == 0 || len > 64)
                { Reset(); return {}; }
                std::vector<BYTE> dig(len, 0);
                const bool ok = ::BCryptFinishHash(h, dig.data(), len, 0) == 0;
                Reset();
                if (!ok) return {};
                static const char* kHex = "0123456789abcdef";
                std::string out;
                out.reserve(dig.size() * 2);
                for (BYTE b : dig) { out.push_back(kHex[b >> 4]); out.push_back(kHex[b & 15]); }
                return out;
            }
        };

        // ---- WinHTTP ---------------------------------------------------------
        // Tek kullanım için açılıp kapanan bir istek. Kopyalanması yok: tanıkları
        // iki yerde tutmak, birinde kapanan tutamağın diğerinden kullanılması
        // demek olurdu.
        struct Http
        {
            HINTERNET session = nullptr;
            HINTERNET connect = nullptr;
            HINTERNET request = nullptr;
            DWORD     status  = 0;

            ~Http() { Close(); }
            Http() = default;
            Http(const Http&) = delete;
            Http& operator=(const Http&) = delete;

            void Close()
            {
                if (request) { ::WinHttpCloseHandle(request); request = nullptr; }
                if (connect) { ::WinHttpCloseHandle(connect); connect = nullptr; }
                if (session) { ::WinHttpCloseHandle(session); session = nullptr; }
            }

            bool Open(const std::string& url)
            {
                const std::wstring wurl = W(url);
                wchar_t host[256] = {}, path[2048] = {}, extra[1024] = {};
                URL_COMPONENTS u = {};
                u.dwStructSize      = sizeof(u);
                u.lpszHostName      = host;  u.dwHostNameLength     = ARRAYSIZE(host) - 1;
                u.lpszUrlPath       = path;  u.dwUrlPathLength      = ARRAYSIZE(path) - 1;
                u.lpszExtraInfo     = extra; u.dwExtraInfoLength    = ARRAYSIZE(extra) - 1;

                // ICU_REJECT_USERPWD: "https://github.com@baskent/" gibi bir adres
                // ayrıştırılırsa sunucu sanılır; kullanıcı adı bölümü reddedilir.
                if (!::WinHttpCrackUrl(wurl.c_str(), 0, ICU_REJECT_USERPWD, &u)) return false;
                if (u.nScheme != INTERNET_SCHEME_HTTPS) return false;
                if (u.dwHostNameLength == 0) return false;

                // Kullanıcı-kimliği dizesi sürümle birlikte kuruluyor: sabit
                // metin derlemede birleştirilemez çünkü sürüm tek kaynaktan
                // (brand.hpp) geliyor ve elle iki yerde tutulması ayrışırdı.
                const std::wstring ua = L"TENGRI/" + W(brand::kVersion) + L" (optimizer)";
                session = ::WinHttpOpen(ua.c_str(),
                                        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
                if (!session) return false;
                // Çözümleme 5 sn, bağlantı 10 sn, gönderme 15 sn, okuma 20 sn.
                // Sınırı aşan bir istek "çalışmıyor" değil "ağ yok" demektir;
                // arayüz ikinci kez denemeyi önerir.
                ::WinHttpSetTimeouts(session, 5000, 10000, 15000, 20000);

                connect = ::WinHttpConnect(session, host, u.nPort, 0);
                if (!connect) return false;

                std::wstring target = std::wstring(path) + extra;
                if (target.empty()) target = L"/";

                request = ::WinHttpOpenRequest(connect, L"GET", target.c_str(), nullptr,
                                               WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               WINHTTP_FLAG_SECURE);
                if (!request) return false;

                // Accept-Encoding bilerek yok: WinHTTP sıkıştırılmış yanıtı kendi
                // açmaz, biz de açamayız. Sıkıştırma açmayı eklemek, ayrıştırmanın
                // ilk baytta tökezlemesi demek olurdu.
                static const wchar_t* kHeaders = L"Accept: application/json\r\n";
                if (!::WinHttpSendRequest(request, kHeaders, (DWORD)-1,
                                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
                    return false;
                if (!::WinHttpReceiveResponse(request, nullptr)) return false;

                DWORD st = 0, sz = sizeof(st);
                if (!::WinHttpQueryHeaders(request,
                                           WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                           WINHTTP_HEADER_NAME_BY_INDEX, &st, &sz,
                                           WINHTTP_NO_HEADER_INDEX))
                    return false;
                status = st;
                return true;
            }

            // Yanıtın ULTIM adresi. GitHub sürüm indirmelerini başka bir alan adına
            // yönlendirir; yönlendirme sonrası da HTTPS ve tanınan sunucuda
            // bitmeden hiçbir bayt kabul edilmez.
            bool FinalUrlAllowed() const
            {
                wchar_t buf[4096] = {};
                DWORD len = sizeof(buf);
                if (!::WinHttpQueryOption(request, WINHTTP_OPTION_URL, buf, &len)) return false;
                return HostAllowed(A(std::wstring(buf)));
            }

            unsigned long long ContentLength() const
            {
                DWORD v = 0, sz = sizeof(v);
                if (!::WinHttpQueryHeaders(request,
                                           WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                                           WINHTTP_HEADER_NAME_BY_INDEX, &v, &sz,
                                           WINHTTP_NO_HEADER_INDEX))
                    return 0;
                return v;
            }

            bool ReadAll(std::string* out, size_t cap) const
            {
                out->clear();
                std::vector<char> buf(kChunk);
                for (;;)
                {
                    DWORD avail = 0;
                    if (!::WinHttpQueryDataAvailable(request, &avail)) return false;
                    if (avail == 0) return true;
                    const DWORD want = avail < kChunk ? avail : kChunk;
                    DWORD got = 0;
                    if (!::WinHttpReadData(request, buf.data(), want, &got)) return false;
                    if (got == 0) return true;
                    if (out->size() + got > cap) return false;
                    out->append(buf.data(), got);
                }
            }

            // Dosyaya yazar ve her blokta iptal işaretine bakar. İptal edilen
            // indirmede yarım dosyayı çağıran taraf siler; burada yarım bırakıp
            // dönmenin tek nedeni kullanıcıdan gelen vazgeçiştir.
            bool ReadTo(HANDLE file, unsigned long long cap,
                        std::atomic<unsigned long long>& done,
                        const std::atomic<bool>& abort,
                        unsigned long long* written) const
            {
                std::vector<char> buf(kChunk);
                unsigned long long total = 0;
                for (;;)
                {
                    if (abort.load(std::memory_order_relaxed)) { if (written) *written = total; return false; }
                    DWORD avail = 0;
                    if (!::WinHttpQueryDataAvailable(request, &avail)) { if (written) *written = total; return false; }
                    if (avail == 0) { if (written) *written = total; return true; }
                    const DWORD want = avail < kChunk ? avail : kChunk;
                    DWORD got = 0;
                    if (!::WinHttpReadData(request, buf.data(), want, &got)) { if (written) *written = total; return false; }
                    if (got == 0) { if (written) *written = total; return true; }
                    total += got;
                    if (total > cap) { if (written) *written = total; return false; }
                    done.store(total, std::memory_order_relaxed);
                    DWORD put = 0;
                    if (!::WriteFile(file, buf.data(), got, &put, nullptr) || put != got)
                        { if (written) *written = total; return false; }
                }
            }
        };

        // ---- ortak durum -----------------------------------------------------
        std::atomic<State>   g_state{ State::Idle };
        std::atomic<DlState> g_dl{ DlState::Idle };
        std::atomic<unsigned long long> g_bytes{ 0 }, g_total{ 0 };
        std::atomic<bool>    g_abort{ false };
        std::atomic<bool>    g_busy{ false };

        // Sonuçlar iş parçacığıyla paylaşılır: yazma biter, durum atomikle
        // işaretlenir, okuyan kilit altında kopya alır.
        std::mutex    g_mtx;
        Latest        g_latest;
        Fail          g_reason = Fail::None;
        std::wstring  g_stagePath;
        std::wstring  g_stale;
        // Arayüz her karede okuyor, denetim iş parçacığı yazıyor: atomik olmasının
        // sebebi bu. Kayıt değerinin tek başına yarım okunması değil, derleyicinin
        // önbelleğe alıp hiç tazelememesi daha kötü senaryoydu.
        std::atomic<unsigned long long> g_lastCheck{ 0 };

        std::future<void> g_task;
        // Kapanırken beklenen iş parçacığı hâlâ çalışıyorsa geleceği buraya
        // taşır; ~future beklerdi ve kapanışta bunu göze alamayız.
        std::future<void>* g_orphan = nullptr;
        ULONGLONG          g_lastStaleTry = 0;

        std::string ManifestUrl()
        {
            std::string u = "https://github.com/";
            u += brand::kRepoSlug;
            u += "/releases/latest/download/latest.json";
            return u;
        }

        // Tek bir bileşeni oluşturmayı dener; zaten varsa başarı sayılır.
        // CreateDirectoryW ÖZYİNELEMELİ DEĞİLDİR: üst klasör yoksa
        // ERROR_PATH_NOT_FOUND döner. Klasör hiç oluşmadıysa false.
        bool EnsureDir(const std::wstring& dir)
        {
            if (::CreateDirectoryW(dir.c_str(), nullptr)) return true;
            const DWORD e = ::GetLastError();
            return e == ERROR_ALREADY_EXISTS;
        }

        std::wstring StageRoot()
        {
            wchar_t tmp[MAX_PATH] = {};
            if (::GetTempPathW(ARRAYSIZE(tmp), tmp) == 0) return {};
            // Uygulama klasörü değil: indirilen dosya doğrulanmadan yere
            // konmamalı, ayrıca Program Files altında yazma yetki ister. Geçici
            // klasör her yazılabilir ve kullanıcı yolu bulabilir.
            const std::wstring parent = std::wstring(tmp) + brand::kAppDataFolder;
            const std::wstring root   = parent + L"\\Update";

            // İKİ SEVİYE OLUŞTURULUYOR. Tek çağrıda "%TEMP%\TENGRI\Update"
            // denemek, %TEMP%\TENGRI yoksa ERROR_PATH_NOT_FOUND ile sessizce
            // başarısız oluyordu; dönüş değeri denetlenmediği için de StageRoot
            // yine geçerli görünen bir yol döndürüyor, sonraki CreateFileW
            // başarısız olup kullanıcıya "Dosya hatası" diyordu. Bu, güncelleme
            // akışının TEMEL klasörü ilk kez kurulduğunda her zaman böyle
            // kırılıyordu: yani hiçbir kullanıcı güncelleme indiremiyordu.
            if (!EnsureDir(parent) || !EnsureDir(root)) return {};
            return root;
        }

        void SetReason(Fail f) { std::lock_guard<std::mutex> lk(g_mtx); g_reason = f; }

        // Denetimin gövdesi. İş parçacığında çalışır.
        void RunCheck()
        {
            Fail   why  = Fail::None;
            Latest got;
            State  next = State::Failed;

            do
            {
                Http h;
                if (!h.Open(ManifestUrl()))          { why = Fail::Network;   break; }
                if (h.status == 404)                 { why = Fail::NotFound;  break; }
                if (h.status != 200)                 { why = Fail::Http;      break; }
                if (!h.FinalUrlAllowed())            { why = Fail::TrustHost; break; }

                std::string body;
                if (!h.ReadAll(&body, kManifestCap)) { why = Fail::Network;   break; }
                if (!ParseManifest(body, &got, &why)) break;

                next = CompareVersions(got.version, brand::kVersion) > 0
                     ? State::Available
                     : State::Current;
            } while (false);

            {
                std::lock_guard<std::mutex> lk(g_mtx);
                g_latest = next == State::Available ? got : Latest();
                g_reason = next == State::Failed ? why : Fail::None;
            }
            MarkChecked(NowUtc());
            g_state.store(next, std::memory_order_release);
            g_busy.store(false, std::memory_order_release);
        }

        // İndirmenin gövdesi. İş parçacığında çalışır.
        void RunStage()
        {
            const Latest want = Snapshot();
            g_abort.store(false);
            g_bytes.store(0);
            g_total.store(want.size);
            g_dl.store(DlState::Running);

            Fail why = Fail::None;
            std::wstring path;
            bool ready = false;

            do
            {
                if (!HostAllowed(want.url)) { why = Fail::TrustHost; break; }

                const std::wstring root = StageRoot();
                if (root.empty()) { why = Fail::Io; break; }
                path = root + L"\\TENGRI.exe";

                // Kalıntı varsa atılır: yarım kalmış bir önceki indirme, bu
                // indirmenin sonu sanılmasın.
                ::DeleteFileW(path.c_str());

                HANDLE f = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (f == INVALID_HANDLE_VALUE) { why = Fail::Io; break; }

                Http h;
                unsigned long long written = 0;
                bool ok = h.Open(want.url) && h.status == 200 && h.FinalUrlAllowed();
                if (ok) ok = h.ReadTo(f, kDownloadCap, g_bytes, g_abort, &written);
                ::CloseHandle(f);

                if (!ok)
                {
                    ::DeleteFileW(path.c_str());
                    if (g_abort.load()) { path.clear(); break; }      // kullanıcı vazgeçti
                    why = h.status == 404 ? Fail::NotFound
                        : (h.status != 0 && h.status != 200) ? Fail::Http : Fail::Network;
                    break;
                }
                if (g_abort.load())
                {
                    ::DeleteFileW(path.c_str());
                    path.clear();
                    break;
                }

                // Boyut, özetten önce sorulur: eksik akış zaten uyuşmayacak ama
                // yarım dosyayı yediden geçirmektense burada reddetmek ucuz.
                if (written != want.size)
                {
                    ::DeleteFileW(path.c_str());
                    why = Fail::Io;
                    break;
                }

                std::string hex;
                if (!Sha256File(path, &hex))
                {
                    ::DeleteFileW(path.c_str());
                    why = Fail::Io;
                    break;
                }
                if (Lower(hex) != want.sha256)
                {
                    // En kritik an: doğrulama başarısızsa dosya yere konmaz.
                    ::DeleteFileW(path.c_str());
                    why = Fail::HashMismatch;
                    break;
                }
                ready = true;
            } while (false);

            {
                std::lock_guard<std::mutex> lk(g_mtx);
                g_stagePath = ready ? path : std::wstring();
                g_reason    = why;
            }
            g_dl.store(ready ? DlState::Ready : (why == Fail::None ? DlState::Idle : DlState::Failed),
                       std::memory_order_release);
            g_busy.store(false, std::memory_order_release);
        }

        void Launch(std::function<void()> job)
        {
            g_busy.store(true);
            try
            {
                g_task = std::async(std::launch::async, job);
            }
            catch (...)
            {
                // İş parçacığı açılamazsa (yetersiz kaynak) denetim yapılmamış
                // sayılır; durum Idle'da kalır ve bir sonraki kare dener.
                g_busy.store(false);
            }
        }
    }

    // ---- saf mantık ----------------------------------------------------------
    int CompareVersions(const std::string& a, const std::string& b)
    {
        auto Parts = [](const std::string& in)
        {
            std::vector<unsigned long long> v;
            size_t i = 0;
            if (i < in.size() && (in[i] == 'v' || in[i] == 'V')) ++i;
            while (true)
            {
                unsigned long long n = 0;
                bool any = false;
                while (i < in.size() && in[i] >= '0' && in[i] <= '9')
                {
                    n = n * 10ull + (unsigned long long)(in[i] - '0');
                    any = true;
                    ++i;
                }
                v.push_back(any ? n : 0ull);
                if (i >= in.size() || in[i] != '.') break;
                ++i;                                   // nokta: yeni bileşen
            }
            return v;
        };

        const std::vector<unsigned long long> x = Parts(a), y = Parts(b);
        const size_t n = x.size() > y.size() ? x.size() : y.size();
        for (size_t k = 0; k < n; ++k)
        {
            const unsigned long long xv = k < x.size() ? x[k] : 0ull;
            const unsigned long long yv = k < y.size() ? y[k] : 0ull;
            if (xv != yv) return xv > yv ? 1 : -1;
        }
        return 0;
    }

    bool IsHex64(const std::string& s)
    {
        if (s.size() != 64) return false;
        for (char c : s)
        {
            const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            if (!ok) return false;
        }
        return true;
    }

    bool HostAllowed(const std::string& url)
    {
        // Yalnızca HTTPS. Adres çubuğunda "güvenli" görünen bir sürüm
        // bildiriminin, düz HTTP'den inen bir exe ile değiş tokuşu bütün
        // doğrulama varsayımını boşaltırdı.
        const std::string s = Lower(url);
        if (s.compare(0, 8, "https://") != 0) return false;
        size_t j = 8;
        while (j < s.size() && s[j] != '/' && s[j] != ':' && s[j] != '?' && s[j] != '#') ++j;
        const std::string host = s.substr(8, j - 8);
        if (host.empty()) return false;

        if (host == "github.com") return true;
        // Sürüm varlıkları githubusercontent alt alanlarında duruyor ve alt alan
        // adları zamanla değişebiliyor (objects..., release-assets...). Kuyruğa
        // bakmak, tek tek ad saymaktan az kırılgan.
        static const std::string kTail = ".githubusercontent.com";
        return host.size() > kTail.size() &&
               host.compare(host.size() - kTail.size(), kTail.size(), kTail) == 0;
    }

    bool IsDue(unsigned long long lastUtc, unsigned long long nowUtc,
               unsigned long long intervalSec)
    {
        if (lastUtc == 0)   return true;               // ilk çalıştırma
        if (nowUtc < lastUtc) return true;             // saat geri alınmış
        return (nowUtc - lastUtc) >= intervalSec;
    }

    bool ParseManifest(const std::string& json, Latest* out, Fail* why)
    {
        if (out) *out = Latest();
        if (why) *why = Fail::None;

        Latest tmp;
        bool hasVersion = false, hasHash = false, hasUrl = false, hasSize = false;

        const bool walked = WalkObject(json,
            [&](const std::string& key, Tok kind, const std::string& val)
        {
            if      (key == "version" && kind == Tok::Str) { tmp.version = val; hasVersion = true; }
            else if (key == "sha256"  && kind == Tok::Str) { tmp.sha256  = Lower(val); hasHash = true; }
            else if (key == "url"     && kind == Tok::Str) { tmp.url     = val; hasUrl = true; }
            else if (key == "notes"   && kind == Tok::Str) { tmp.notes   = val; }
            else if (key == "size"    && kind == Tok::Num)
            {
                // Kesir/bilimsel gösterim kabul edilmez: yalnızca baştaki tamsayı
                // kısmı okunur. Derleyici boyutu tam sayı olarak yazıyor.
                unsigned long long n = 0;
                size_t d = 0;
                while (d < val.size() && val[d] >= '0' && val[d] <= '9')
                {
                    n = n * 10ull + (unsigned long long)(val[d] - '0');
                    ++d;
                }
                tmp.size = n;
                hasSize = d > 0;
            }
        });

        auto fail = [&](Fail f) { if (why) *why = f; return false; };

        if (!walked)                                    return fail(Fail::BadManifest);
        if (!hasVersion || tmp.version.empty())         return fail(Fail::BadManifest);
        if (!hasHash || !IsHex64(tmp.sha256))           return fail(Fail::BadManifest);
        if (!hasUrl   || tmp.url.empty())               return fail(Fail::BadManifest);
        if (!hasSize  || tmp.size == 0)                 return fail(Fail::BadManifest);
        // Boyut akıl sınırlarında mı? 1 KB'ın altı exe olamaz; 200 MB'ın üstü ya
        // yazım hatasıdır ya da indirmeye değmez bir şey.
        if (tmp.size < 1024ull || tmp.size > kDownloadCap) return fail(Fail::BadManifest);
        if (!HostAllowed(tmp.url))                      return fail(Fail::TrustHost);

        tmp.sha256 = Lower(tmp.sha256);
        if (out) *out = std::move(tmp);
        return true;
    }

    std::string Sha256Hex(const void* data, size_t len)
    {
        Sha256 s;
        if (!s.Start()) return {};
        if (len && !s.Add(data, len)) return {};
        return s.Finish();
    }

    bool Sha256File(const std::wstring& path, std::string* outHex)
    {
        if (outHex) outHex->clear();
        // FILE_SHARE_DELETE: kenara alınmış çalışan exe'yi de okuyabilmek için.
        HANDLE f = ::CreateFileW(path.c_str(), GENERIC_READ,
                                 FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) return false;

        Sha256 s;
        bool ok = s.Start();
        std::vector<BYTE> chunk(1u << 20);
        if (ok)
        {
            for (;;)
            {
                DWORD got = 0;
                if (!::ReadFile(f, chunk.data(), (DWORD)chunk.size(), &got, nullptr)) { ok = false; break; }
                if (got == 0) break;
                if (!s.Add(chunk.data(), got)) { ok = false; break; }
            }
        }
        ::CloseHandle(f);
        if (!ok) return false;
        const std::string hex = s.Finish();
        if (hex.empty()) return false;
        if (outHex) *outHex = hex;
        return true;
    }

    SwapPlan PlanFor(const std::wstring& runningPath)
    {
        SwapPlan p;
        p.running = runningPath;
        if (!p.running.empty()) p.stale = p.running + L".old";
        return p;
    }

    // ---- tercihler -----------------------------------------------------------
    void LoadPrefs()
    {
        DWORD v = 1;                                    // kayıt yoksa açık
        if (ReadDword(kRegEnabled, &v)) enabled = (v != 0);
        else enabled = true;

        unsigned long long last = 0;
        g_lastCheck = ReadQword(kRegLastCheck, &last) ? last : 0;
    }

    void SavePrefs()
    {
        WriteDword(kRegEnabled, enabled ? 1u : 0u);
    }

    unsigned long long LastCheckUtc() { return g_lastCheck.load(); }

    void MarkChecked(unsigned long long nowUtc)
    {
        g_lastCheck.store(nowUtc);
        WriteQword(kRegLastCheck, nowUtc);
    }

    unsigned long long HoursSinceLastCheck()
    {
        const unsigned long long last = g_lastCheck.load();
        if (last == 0) return 0;
        const unsigned long long now = NowUtc();
        // Saat geri alındıysa "gelecekte denetim" göstermek yerine "az önce"
        // denir; ikincisi daha az yalan söyler.
        if (now <= last) return 0;
        return (now - last) / 3600ULL;
    }

    // ---- denetim -------------------------------------------------------------
    bool DueForCheck()
    {
        if (!enabled) return false;
        if (g_busy.load()) return false;
        return IsDue(g_lastCheck.load(), NowUtc(), kIntervalSec);
    }

    void CheckNow()
    {
        if (g_busy.load()) return;
        g_state.store(State::Checking);
        Launch(RunCheck);
    }

    State Poll()
    {
        // Takas sonrası kalan kenara alınmış dosya silinmeyi bekler: onun adı
        // bizden önceki süreçte kilitlenmişti, bu yüzden sabırla denenir.
        // Kopya kilit altında alınıyor; Apply aynı alanı yazıyor olabilir.
        std::wstring stale;
        { std::lock_guard<std::mutex> lk(g_mtx); stale = g_stale; }
        if (!stale.empty())
        {
            const ULONGLONG now = ::GetTickCount64();
            if (now - g_lastStaleTry >= 30000)
            {
                g_lastStaleTry = now;
                if (::DeleteFileW(stale.c_str()))
                { std::lock_guard<std::mutex> lk(g_mtx); g_stale.clear(); }
            }
        }
        return g_state.load(std::memory_order_acquire);
    }

    State Get() { return g_state.load(std::memory_order_acquire); }

    Latest Snapshot()
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        return g_latest;
    }

    Fail Reason()
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        return g_reason;
    }

    std::wstring StalePath()
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        return g_stale;
    }

    void Startup()
    {
        LoadPrefs();
        // Kenara alınmış bir kalıntı varsa anımsanır ve Poll() üzerinden
        // silinmeye çalışılır. Silinememesi olağandır: dosya, kendisini kenara
        // alan süreç henüz yaşamıyor olabilir.
        const SwapPlan p = PlanFor(ExePathW());
        if (p.stale.empty()) return;
        WIN32_FIND_DATAW fd = {};
        const HANDLE h = ::FindFirstFileW(p.stale.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        ::FindClose(h);
        std::lock_guard<std::mutex> lk(g_mtx);
        g_stale = p.stale;
    }

    void Shutdown()
    {
        g_abort.store(true);
        if (g_task.valid())
        {
            if (g_task.wait_for(std::chrono::milliseconds(1500)) == std::future_status::timeout)
            {
                // Hâlâ çalışıyor: beklemek yerine geleceği sahipleniyoruz.
                // ~future iş parçacığının bitmesini beklerdi; process çıkışında
                // bunu göze alamayız. İş parçacığı yalnızca bu modülün süreç
                // boyunca yaşayan statiklerine dokunduğu için kendi hâlinde
                // bitmesi zararsızdır.
                if (!g_orphan) g_orphan = new std::future<void>();
                *g_orphan = std::move(g_task);
            }
            else
            {
                g_task.get();
            }
        }
        g_busy.store(false);
        g_state.store(State::Idle);
        g_dl.store(DlState::Idle);
    }

    // ---- indirme ve takas ----------------------------------------------------
    void StageNow()
    {
        if (g_busy.load()) return;
        if (g_dl.load() == DlState::Running) return;
        Launch(RunStage);
    }

    DlState StagePoll() { return g_dl.load(std::memory_order_acquire); }
    DlState StageGet() { return g_dl.load(std::memory_order_acquire); }

    unsigned long long StagedBytes() { return g_bytes.load(std::memory_order_relaxed); }
    unsigned long long StagedTotal() { return g_total.load(std::memory_order_relaxed); }

    std::wstring StagedFile()
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        return g_stagePath;
    }

    void ResetStage()
    {
        if (g_dl.load() == DlState::Running) return;    // sürerken bozulmaz
        g_abort.store(true);
        g_bytes.store(0);
        g_total.store(0);
        std::lock_guard<std::mutex> lk(g_mtx);
        g_stagePath.clear();
        g_reason = Fail::None;
        g_dl.store(DlState::Idle);
    }

    bool StageCancel()
    {
        if (g_dl.load() != DlState::Running) return false;
        // İş parçacığı iptal işaretini görüp yarım dosyayı kendi siler ve
        // Idle'a döner. Burada Idle'a çekmek, silinmemiş bir dosyanın yolu
        // bellekte yokken diskte kalmasına yol açardı.
        g_abort.store(true);
        return true;
    }

    bool Apply(const SwapPlan& plan)
    {
        const std::wstring staged = StagedFile();
        if (staged.empty() || plan.running.empty()) { SetReason(Fail::Io); return false; }
        if (g_dl.load() != DlState::Ready)          { SetReason(Fail::Io); return false; }

        // 1) Çalışan exe kenara alınır. Windows, çalışan bir görüntüyü aynı
        //    birimde taşımaya izin verir; kilitli olan içeriğidir, adı değil.
        //    Bu çağrının kendisi yazılabilirlik sınavıdır: ayrı bir yoklama için
        //    kullanıcı klasörüne dosya yazmak gerekirdi.
        if (!::MoveFileExW(plan.running.c_str(), plan.stale.c_str(), MOVEFILE_REPLACE_EXISTING))
        {
            const DWORD e = ::GetLastError();
            // Erişilemeyen klasörün hata kodu tek değil: ACL reddi, paylaşılan
            // kilit, salt-okunur ortam. Dördü de kullanıcıya "yazamıyoruz, dosya
            // geçici yerde duruyor" olarak aynı şekilde anlatılır.
            SetReason(e == ERROR_ACCESS_DENIED || e == ERROR_PRIVILEGE_NOT_HELD ||
                      e == ERROR_SHARING_VIOLATION || e == ERROR_WRITE_PROTECT
                      ? Fail::NotWritable : Fail::Io);
            return false;
        }

        // 2) Yenisi aynı ada yazılır. Bu adım patlarsa eski hâline döndürülür;
        //    kullanıcı uygulamasız kalmaz.
        if (!::CopyFileW(staged.c_str(), plan.running.c_str(), FALSE))
        {
            const DWORD e2 = ::GetLastError();
            ::MoveFileExW(plan.stale.c_str(), plan.running.c_str(), MOVEFILE_REPLACE_EXISTING);
            SetReason(e2 == ERROR_ACCESS_DENIED ? Fail::NotWritable : Fail::Io);
            return false;
        }

        ::DeleteFileW(staged.c_str());

        // 3) Yeni süreç başlatılır ve bu süreç kapanır. Kenara alınmış dosyanın
        //    yolu ÖNCE hatırlanıyor: kapanışın sonrası yok, bir daha buraya
        //    dönülmeyecek.
        {
            std::lock_guard<std::mutex> lk(g_mtx);
            g_stale       = plan.stale;
            g_stagePath.clear();
            g_reason      = Fail::None;
        }
        g_dl.store(DlState::Idle);

        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};
        // Önek yeni örneğe "kapalı pencereyi açma" yerine kilidi devralmayı
        // beklemesini söylüyor; yarışın tek güvenli çözümü bu.
        std::wstring cmd = L"\"" + plan.running + L"\" ";
        cmd += kRelaunchArg;
        if (!::CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE,
                              0, nullptr, nullptr, &si, &pi))
        {
            // Başlatılamadı: eski hâline geri dönülüyor, kullanıcı uygulamayla
            // baş başa kalmıyor çünkü pencere hâlâ açık.
            ::DeleteFileW(plan.running.c_str());
            ::MoveFileExW(plan.stale.c_str(), plan.running.c_str(), MOVEFILE_REPLACE_EXISTING);
            SetReason(Fail::Io);
            return false;
        }
        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);

        // Doğrudan kapanış: Apply'dan sonraki her ek iş, yeni örneğin tek örnek
        // kilidiyle yarışına zaman kazandırırdı.
        ::ExitProcess(0);
        return true;   //ulaşılamaz
    }
}
