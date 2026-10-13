// Masaüstünün üstünde yüzen medya adası. Kurgu için island.hpp.
//
// Üç parçadan oluşur:
//   1. Medya durumu   - SMTC (Windows'un medya oturumu bilgisi) bir arka plan
//                       iş parçacığında dinlenir; komut kuyruğu ve paylaşılan
//                       durum kilitlerle korunur.
//   2. Çizim          - ana iş parçacığında, UYGULAMANIN ImGui bağlamının
//                       shared data'sı ve paylaşılan D3D11 cihazı üzerinde;
//                       ama kendi karesi YOK (bkz. DrawFrame üstündeki not):
//                       ada yalnızca kendi ImDrawList'ini çizer.
//   3. Tercihler     - %APPDATA%\TENGRI\island.dat
#include "island.hpp"
#include "brand.hpp"
#include "lang.hpp"
#include "../gui/font_data.hpp"

// windows.h'nin min/max makroları std::min/std::max ile çakışır ve
// initializer-list sürümünü bozar. Bu dosyada makrolar hiç kullanılmıyor.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// sample_data.hpp kendi windows.h'ını çektiği için burada, NOMINMAX tanımlandıktan
// sonra gelir. Dosyanın üstündeki gruplara eklense min/max makroları tüm çeviri
// birimine sızar ve aşağıdaki std::max({...}) çağrıları bozulurdu.
#include "sample_data.hpp"
#include <windows.h>
#include <dwmapi.h>
#include <d3d11.h>
#include "../gui/theme.hpp"   // NOMINMAX tanimindan SONRA gelir: windows.h makrolari std::min/max'i bozar
#include <mmdeviceapi.h>
// IAudioSessionManager2 / IAudioSessionControl2 audiopolicy.h'te,
// ISimpleAudioVolume ise audioclient.h'te tanımlı.
#include <audiopolicy.h>
#include <audioclient.h>
#include <wincodec.h>
#include <winhttp.h>
#include <wrl/client.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>    // strtoul
#include <cstring>
#include <cwctype>    // towlower
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace island
{
    bool enabled     = true;    // ilk açılışta ada açıktır
    // KAPALI varsayılan: ada her pencerenin üzerinde yüzer.
    bool desktopOnly = false;
    bool lyrics      = true;    // senkronize sözler paneli
    int  theme       = 0;
    bool visible     = true;    // INSERT ile açılıp kapanır, hatırlanır

    int         ThemeCount() { return 6; }
    const char* ThemeName(int i)
    {
        static const char* const names[] =
        {
            "Black", "Spotify", "Aurora", "Sunset", "Ocean", "Cover",
        };
        if (i < 0 || i >= ThemeCount()) return names[0];
        return names[i];
    }

    namespace
    {
        using steady = std::chrono::steady_clock;
        using Microsoft::WRL::ComPtr;
        using namespace winrt::Windows::Media::Control;

        // =====================================================================
        //  Tercihler
        // =====================================================================

        std::wstring PrefPath()
        {
            wchar_t dir[MAX_PATH] = {};
            if (!::GetEnvironmentVariableW(L"APPDATA", dir, MAX_PATH)) return {};
            const std::wstring root = std::wstring(dir) + L"\\" + brand::kAppDataFolder;
            ::CreateDirectoryW(root.c_str(), nullptr);
            return root + L"\\island.dat";
        }

        void LoadPrefs()
        {
            enabled     = true;    // ilk açılışta ada açıktır
            desktopOnly = false;
            lyrics      = true;
            theme       = 0;
            visible     = true;
            const std::wstring p = PrefPath();
            if (p.empty()) return;
            HANDLE f = ::CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f == INVALID_HANDLE_VALUE) return;   // ilk çalıştırma: varsayılanlar

            unsigned char buf[5] = {};
            DWORD n = 0;
            const bool ok = ::ReadFile(f, buf, 5, &n, nullptr) && n == 5;
            ::CloseHandle(f);
            if (!ok) return;

            enabled     = buf[0] != '0';
            desktopOnly = buf[1] != '0';
            lyrics      = buf[2] != '0';
            theme       = (int)(buf[3] % (unsigned char)ThemeCount());
            visible     = buf[4] != '0';
        }

        // =====================================================================
        //  Metin / URL / JSON / LRC yardımcıları
        // =====================================================================

        std::string ToUtf8(const wchar_t* w)
        {
            if (!w || !*w) return {};
            int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
            std::string s(n > 0 ? n - 1 : 0, 0);
            if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
            return s;
        }

        std::wstring UrlEnc(const std::string& s)
        {
            std::wstring o;
            for (unsigned char c : s)
            {
                if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (wchar_t)c;
                else { wchar_t t[4]; swprintf_s(t, 4, L"%%%02X", c); o += t; }
            }
            return o;
        }

        void AppendUtf8(std::string& o, unsigned cp)
        {
            if (cp < 0x80) o += (char)cp;
            else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 63)); }
            else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 63)); o += (char)(0x80 | (cp & 63)); }
            else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 63)); o += (char)(0x80 | ((cp >> 6) & 63)); o += (char)(0x80 | (cp & 63)); }
        }

        std::string JsonUnescape(const std::string& j, size_t i)
        {
            std::string o;
            while (i < j.size())
            {
                char c = j[i++];
                if (c == '"') break;
                if (c != '\\') { o += c; continue; }
                if (i >= j.size()) break;
                char e = j[i++];
                switch (e)
                {
                case 'n': o += '\n'; break;
                case 't': o += '\t'; break;
                case 'r': break;
                case 'u': {
                    unsigned cp = (unsigned)strtoul(j.substr(i, 4).c_str(), nullptr, 16); i += 4;
                    if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= j.size() && j[i] == '\\' && j[i + 1] == 'u')
                    {
                        unsigned lo = (unsigned)strtoul(j.substr(i + 2, 4).c_str(), nullptr, 16); i += 6;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    AppendUtf8(o, cp); break;
                }
                default: o += e;
                }
            }
            return o;
        }

        bool ExtractField(const std::string& j, const std::string& key, std::string& out)
        {
            const std::string pat = "\"" + key + "\":";
            size_t p = 0;
            while ((p = j.find(pat, p)) != std::string::npos)
            {
                p += pat.size();
                while (p < j.size() && j[p] == ' ') p++;
                if (p < j.size() && j[p] == '"') { out = JsonUnescape(j, p + 1); if (!out.empty()) return true; }
            }
            return false;
        }

        struct LyricLine { double t; std::string s; };

        std::vector<LyricLine> ParseLrc(const std::string& s)
        {
            std::vector<LyricLine> out;
            size_t i = 0;
            while (i < s.size())
            {
                size_t e = s.find('\n', i);
                if (e == std::string::npos) e = s.size();
                std::string line = s.substr(i, e - i);
                i = e + 1;
                if (!line.empty() && line.back() == '\r') line.pop_back();

                std::vector<double> ts;
                size_t p = 0;
                bool bad = false;
                while (p < line.size() && line[p] == '[')
                {
                    size_t q = line.find(']', p);
                    if (q == std::string::npos) { bad = true; break; }
                    int mm = 0; double ss = 0;
                    if (sscanf_s(line.c_str() + p + 1, "%d:%lf", &mm, &ss) == 2) ts.push_back(mm * 60.0 + ss);
                    else { bad = true; break; }
                    p = q + 1;
                }
                if (bad || ts.empty()) continue;
                while (p < line.size() && line[p] == ' ') p++;
                for (double t : ts) out.push_back({ t, line.substr(p) });
            }
            std::sort(out.begin(), out.end(),
                      [](const LyricLine& a, const LyricLine& b) { return a.t < b.t; });
            return out;
        }

        // Sadece ürün adı çıkarılan temiz başlık: arama ikinci denemesi için.
        std::string CleanTitle(std::string t)
        {
            size_t p = t.find(" - ");
            if (p != std::string::npos) t = t.substr(0, p);
            p = t.find(" (");
            if (p != std::string::npos) t = t.substr(0, p);
            return t;
        }

        // =====================================================================
        //  Söz servisi (lrclib.net)
        // =====================================================================

        std::string HttpGet(const std::wstring& path)
        {
            std::string out;
            HINTERNET s = WinHttpOpen(L"TENGRI/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                      nullptr, nullptr, 0);
            if (!s) return out;
            WinHttpSetTimeouts(s, 5000, 5000, 8000, 8000);

            HINTERNET c = WinHttpConnect(s, L"lrclib.net", INTERNET_DEFAULT_HTTPS_PORT, 0);
            HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path.c_str(), nullptr,
                                                 WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                 WINHTTP_FLAG_SECURE)
                            : nullptr;
            if (r && WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
                && WinHttpReceiveResponse(r, nullptr))
            {
                DWORD code = 0, sz = sizeof(code);
                WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    nullptr, &code, &sz, nullptr);
                if (code == 200)
                {
                    for (;;)
                    {
                        DWORD n = 0;
                        if (!WinHttpQueryDataAvailable(r, &n) || !n) break;
                        std::string buf(n, 0);
                        DWORD rd = 0;
                        if (!WinHttpReadData(r, buf.data(), n, &rd)) break;
                        out.append(buf.data(), rd);
                    }
                }
            }
            if (r) WinHttpCloseHandle(r);
            if (c) WinHttpCloseHandle(c);
            WinHttpCloseHandle(s);
            return out;
        }

        // =====================================================================
        //  Paylaşılan medya durumu
        // =====================================================================

        struct Shared
        {
            std::mutex m;
            bool connected = false, hasTrack = false, playing = false, shuffle = false;
            int  repeat = 0;                 // 0 yok, 1 parça, 2 liste
            std::string title, artist, album;
            double dur = 0, posBase = 0;
            steady::time_point posT = steady::now();
            uint64_t trackId = 0;
            std::vector<uint8_t> cover;
            int   coverVer = 0;
            float dom[3] = { 0.2f, 0.2f, 0.2f };
            float volume = -1.f;
            std::shared_ptr<const std::vector<LyricLine>> lyr;
            int lyrState = 0;                // 0 yükleniyor, 1 hazır, 2 yok
        } G;

        enum CmdType { C_PLAY, C_NEXT, C_PREV, C_SEEK, C_SHUFFLE, C_REPEAT, C_VOL };
        struct Cmd { CmdType t; double v; };

        std::mutex              g_cm;
        std::vector<Cmd>        g_cmds;
        std::condition_variable g_cv;
        std::thread             g_worker;
        std::atomic<bool>       g_run{ false };

        void Push(CmdType t, double v = 0)
        {
            // Örnek veri kipinde dinleyici yok: kuyruk boşuna dolmasın, gerçek
            // oynatıcıya tek komut gitmesin.
            if (sample::Active()) return;
            { std::lock_guard<std::mutex> l(g_cm); g_cmds.push_back({ t, v }); }
            g_cv.notify_one();
        }

        void LyricsThread(uint64_t id, std::string title, std::string artist,
                          std::string album, double dur)
        {
            if (!lyrics) return;   // ayar sonradan kapatıldıysa hiç sorma

            std::string synced;
            std::string body = HttpGet(L"/api/get?artist_name=" + UrlEnc(artist) +
                                       L"&track_name=" + UrlEnc(title) +
                                       L"&album_name=" + UrlEnc(album) +
                                       L"&duration=" + std::to_wstring((int)(dur + 0.5)));
            if (!body.empty()) ExtractField(body, "syncedLyrics", synced);

            if (synced.empty())
            {
                body = HttpGet(L"/api/search?track_name=" + UrlEnc(title) + L"&artist_name=" + UrlEnc(artist));
                if (!body.empty()) ExtractField(body, "syncedLyrics", synced);
            }
            if (synced.empty())
            {
                const std::string ct = CleanTitle(title);
                body = HttpGet(L"/api/search?track_name=" + UrlEnc(ct) + L"&artist_name=" + UrlEnc(artist));
                if (!body.empty()) ExtractField(body, "syncedLyrics", synced);
            }

            auto lines = std::make_shared<std::vector<LyricLine>>(ParseLrc(synced));
            std::lock_guard<std::mutex> l(G.m);
            // Arada parça değiştiyse bu sonuç artık kimseye ait değil.
            if (G.trackId == id) { G.lyr = lines; G.lyrState = lines->empty() ? 2 : 1; }
        }

        // =====================================================================
        //  Uygulama sesi: yalnızca oynatıcının kendi oturumu
        // =====================================================================

        bool IsPlayerPid(DWORD pid)
        {
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (!h) return false;
            wchar_t buf[MAX_PATH];
            DWORD n = MAX_PATH;
            bool ok = false;
            if (QueryFullProcessImageNameW(h, 0, buf, &n))
            {
                std::wstring p(buf, n);
                for (auto& c : p) c = (wchar_t)towlower(c);
                ok = p.size() >= 11 && p.compare(p.size() - 11, 11, L"spotify.exe") == 0;
            }
            CloseHandle(h);
            return ok;
        }

        void ForEachPlayerVolume(const std::function<void(ISimpleAudioVolume*)>& fn)
        {
            ComPtr<IMMDeviceEnumerator> en;
            if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en))))
                return;
            ComPtr<IMMDevice> dev;
            if (FAILED(en->GetDefaultAudioEndpoint(eRender, ERole::eMultimedia, &dev))) return;
            ComPtr<IAudioSessionManager2> mgr;
            if (FAILED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, (void**)mgr.GetAddressOf())))
                return;
            ComPtr<IAudioSessionEnumerator> se;
            if (FAILED(mgr->GetSessionEnumerator(&se))) return;

            int n = 0;
            se->GetCount(&n);
            for (int i = 0; i < n; i++)
            {
                ComPtr<IAudioSessionControl> c;
                if (FAILED(se->GetSession(i, &c))) continue;
                ComPtr<IAudioSessionControl2> c2;
                if (FAILED(c.As(&c2))) continue;
                DWORD pid = 0;
                c2->GetProcessId(&pid);
                if (!IsPlayerPid(pid)) continue;
                ComPtr<ISimpleAudioVolume> v;
                if (SUCCEEDED(c.As(&v))) fn(v.Get());
            }
        }

        float GetPlayerVolume()
        {
            float r = -1.f;
            ForEachPlayerVolume([&](ISimpleAudioVolume* v)
            {
                float x;
                if (r < 0 && SUCCEEDED(v->GetMasterVolume(&x))) r = x;
            });
            return r;
        }

        void SetPlayerVolume(float x)
        {
            ForEachPlayerVolume([&](ISimpleAudioVolume* v) { v->SetMasterVolume(std::clamp(x, 0.f, 1.f), nullptr); });
        }

        // =====================================================================
        //  Kapak resmi: SMTC küçük resmi -> WIC -> 128x128 RGBA
        // =====================================================================

        bool LoadCover(GlobalSystemMediaTransportControlsSessionMediaProperties const& p,
                       std::vector<uint8_t>& out, float dom[3])
        {
            auto ref = p.Thumbnail();
            if (!ref) return false;
            auto stream = ref.OpenReadAsync().get();
            const uint32_t size = (uint32_t)stream.Size();
            if (!size) return false;

            winrt::Windows::Storage::Streams::DataReader rd(stream.GetInputStreamAt(0));
            rd.LoadAsync(size).get();
            std::vector<uint8_t> bytes(size);
            rd.ReadBytes(bytes);

            ComPtr<IWICImagingFactory> f;
            if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f))))
                return false;
            ComPtr<IWICStream> st;
            f->CreateStream(&st);
            st->InitializeFromMemory(bytes.data(), size);
            ComPtr<IWICBitmapDecoder> dec;
            if (FAILED(f->CreateDecoderFromStream(st.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &dec)))
                return false;
            ComPtr<IWICBitmapFrameDecode> fr;
            if (FAILED(dec->GetFrame(0, &fr))) return false;
            ComPtr<IWICBitmapScaler> sc;
            f->CreateBitmapScaler(&sc);
            sc->Initialize(fr.Get(), 128, 128, WICBitmapInterpolationModeFant);
            ComPtr<IWICFormatConverter> conv;
            f->CreateFormatConverter(&conv);
            conv->Initialize(sc.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                             WICBitmapPaletteTypeCustom);

            out.resize(128 * 128 * 4);
            if (FAILED(conv->CopyPixels(nullptr, 128 * 4, (UINT)out.size(), out.data()))) return false;

            // Baskın renk: doygunluğu yüksek piksellere daha yüksek ağırlık. Düz
            // ortalama, çoğu albüm kapağında griye kayıyor.
            double r = 0, g = 0, b = 0, wsum = 0;
            for (size_t i = 0; i < out.size(); i += 4)
            {
                int mx = std::max({ out[i], out[i + 1], out[i + 2] });
                int mn = std::min({ out[i], out[i + 1], out[i + 2] });
                double w = (mx - mn) + 16.0;
                r += out[i] * w; g += out[i + 1] * w; b += out[i + 2] * w; wsum += w;
            }
            dom[0] = (float)(r / wsum / 255.0);
            dom[1] = (float)(g / wsum / 255.0);
            dom[2] = (float)(b / wsum / 255.0);
            return true;
        }

        // =====================================================================
        //  Arka plan iş parçacığı: SMTC dinleme + komutlar
        // =====================================================================

        // Örnek veri kipinde ada tamamen sabit bir örnek parçayla çizilir: ne SMTC oturumu
        // okunur ne de lrclib.net'e tek bir istek gider. Gerekçe diğer örnek
        // alanlarla aynı: çalınan parça o makinede kimin ne dinlediğini söyler,
        // yani yayınlanan görselde duramaz. Adanın düğmeleri de gerçek oynatıcıyı
        // sürdüğü için bu kipte hiç komut göndermez — sahte etikete dokunan gerçek
        // eylem olmaz (bkz. sample_data.hpp, startup.cpp).
        void SeedSample()
        {
            std::lock_guard<std::mutex> l(G.m);
            G.connected = true;
            G.hasTrack  = true;
            G.playing   = true;
            G.shuffle   = false;
            G.repeat    = 0;
            G.title     = "Sample Track";
            G.artist    = "Demo Artist";
            G.album     = "Demo Album";
            G.dur       = 214.0;
            G.posBase   = 96.0;
            G.posT      = steady::now();
            G.volume    = 0.70f;
            G.trackId   = 1;
            G.lyr       = std::make_shared<std::vector<LyricLine>>(std::vector<LyricLine>{
                { 88.0,  "Sample line one" },
                { 94.5,  "Sample line two" },
                { 101.0, "Sample line three" },
            });
            G.lyrState = 1;
        }

        void Worker()
        {
            if (sample::Active())
            {
                SeedSample();
                // Konum akışı UI'da posBase + geçen süre ile hesaplandığı için
                // ilerleme çubuğu burada da canlı durur; tek gereken bekleme.
                while (g_run)
                {
                    std::unique_lock<std::mutex> lk(g_cm);
                    g_cv.wait_for(lk, std::chrono::milliseconds(250), [] { return !g_run; });
                }
                return;
            }

            winrt::init_apartment(winrt::apartment_type::multi_threaded);

            GlobalSystemMediaTransportControlsSessionManager mgr{ nullptr };
            try { mgr = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get(); }
            catch (...) { return; }

            std::string lastKey;
            bool haveCover = false;
            int  coverTries = 0, tick = 0;

            while (g_run)
            {
                std::vector<Cmd> local;
                {
                    std::unique_lock<std::mutex> lk(g_cm);
                    g_cv.wait_for(lk, std::chrono::milliseconds(250),
                                  [] { return !g_cmds.empty() || !g_run; });
                    local.swap(g_cmds);
                }
                if (!g_run) break;

                try
                {
                    GlobalSystemMediaTransportControlsSession ses{ nullptr };
                    for (auto s : mgr.GetSessions())
                    {
                        std::wstring id = s.SourceAppUserModelId().c_str();
                        for (auto& ch : id) ch = (wchar_t)towlower(ch);
                        if (id.find(L"spotify") != std::wstring::npos) { ses = s; break; }
                    }

                    bool volCmd = false;
                    for (auto& c : local)
                    {
                        if (c.t == C_VOL) { SetPlayerVolume((float)c.v); volCmd = true; continue; }
                        if (!ses) continue;
                        switch (c.t)
                        {
                        case C_PLAY:    ses.TryTogglePlayPauseAsync(); break;
                        case C_NEXT:    ses.TrySkipNextAsync(); break;
                        case C_PREV:    ses.TrySkipPreviousAsync(); break;
                        case C_SEEK:    ses.TryChangePlaybackPositionAsync((int64_t)(c.v * 1e7)); break;
                        case C_SHUFFLE: ses.TryChangeShuffleActiveAsync(c.v > 0.5); break;
                        case C_REPEAT:  ses.TryChangeAutoRepeatModeAsync(
                                            (winrt::Windows::Media::MediaPlaybackAutoRepeatMode)(int)c.v); break;
                        default: break;
                        }
                    }
                    if (!ses)
                    {
                        std::lock_guard<std::mutex> l(G.m);
                        G.connected = false;
                        G.playing = false;
                        continue;
                    }

                    auto props = ses.TryGetMediaPropertiesAsync().get();
                    auto info  = ses.GetPlaybackInfo();
                    auto tl    = ses.GetTimelineProperties();

                    const std::string title  = ToUtf8(props.Title().c_str());
                    const std::string artist = ToUtf8(props.Artist().c_str());
                    const std::string album  = ToUtf8(props.AlbumTitle().c_str());

                    const bool playing = info.PlaybackStatus() ==
                                         GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
                    bool shuffle = false;
                    if (auto sh = info.IsShuffleActive()) shuffle = sh.Value();
                    int rep = 0;
                    if (auto r = info.AutoRepeatMode()) rep = (int)r.Value();

                    double dur = (tl.EndTime() - tl.StartTime()).count() / 1e7;
                    double pos = tl.Position().count() / 1e7;
                    if (playing)
                    {
                        const double el = std::chrono::duration<double>(
                            winrt::clock::now() - tl.LastUpdatedTime()).count();
                        if (el > 0 && el < 3600) pos += el;
                    }
                    if (dur > 0) pos = std::min(pos, dur);

                    const bool hasTrack = !title.empty();
                    const std::string key = title + "\x1f" + artist + "\x1f" + album;
                    const bool changed = hasTrack && key != lastKey;

                    uint64_t tid = 0;
                    if (changed)
                    {
                        lastKey = key;
                        haveCover = false;
                        coverTries = 0;
                        std::lock_guard<std::mutex> l(G.m);
                        tid = ++G.trackId;
                        G.lyr.reset();
                        G.lyrState = 0;
                        G.cover.clear();
                        G.coverVer++;
                        std::thread(LyricsThread, tid, title, artist, album, dur).detach();
                    }
                    if (hasTrack && !haveCover && coverTries < 12)
                    {
                        coverTries++;
                        std::vector<uint8_t> px;
                        float dom[3];
                        if (LoadCover(props, px, dom))
                        {
                            haveCover = true;
                            std::lock_guard<std::mutex> l(G.m);
                            G.cover = std::move(px);
                            G.dom[0] = dom[0]; G.dom[1] = dom[1]; G.dom[2] = dom[2];
                            G.coverVer++;
                        }
                    }

                    float vol = -2.f;
                    if (volCmd || (++tick % 4) == 0) vol = GetPlayerVolume();

                    std::lock_guard<std::mutex> l(G.m);
                    G.connected = true;
                    G.hasTrack = hasTrack;
                    G.playing = playing;
                    G.shuffle = shuffle;
                    G.repeat = rep;
                    if (hasTrack) { G.title = title; G.artist = artist; G.album = album; }
                    G.dur = dur;
                    G.posBase = pos;
                    G.posT = steady::now();
                    if (vol > -1.5f) G.volume = vol;
                }
                catch (...) {}   // oturum kapanmış olabilir; bir sonraki turda yeniden bakar
            }
        }

        // =====================================================================
        //  Çizim yardımcıları
        // =====================================================================

        float g_scale = 1.f;
        inline float S(float v) { return v * g_scale; }
        inline float Clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
        inline float Smooth(float x) { x = Clamp01(x); return x * x * (3 - 2 * x); }
        inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

        ImVec4 Lerp4(ImVec4 a, ImVec4 b, float t)
        {
            return ImVec4(Lerp(a.x, b.x, t), Lerp(a.y, b.y, t), Lerp(a.z, b.z, t), 1.f);
        }
        ImU32 U32(ImVec4 c) { return ImGui::ColorConvertFloat4ToU32(c); }
        ImU32 Col(float r, float g, float b, float a)
        {
            return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), (int)(Clamp01(a) * 255));
        }
        ImU32 Wh(float a) { return Col(1, 1, 1, a); }
        ImU32 WithA(ImVec4 c, float a) { return Col(c.x, c.y, c.z, a); }

        // Yaylı hareket. Kısa görsel geçişlerde (genişleme, açılış) sabit
        // hızlanmadan çok daha iyi durur: değer hedefe doğru salınır, hedefi
        // aşmaz.
        struct Spring
        {
            float x = 0, v = 0;
            void Step(float target, float dt, float k, float d)
            {
                const int n = std::max(1, (int)std::ceil(dt / 0.004f));
                const float h = dt / n;
                for (int i = 0; i < n; i++)
                {
                    const float a = (target - x) * k - v * d;
                    v += a * h;
                    x += v * h;
                }
            }
        };

        struct ThemeDef { ImVec4 top, bot, acc; };
        const ThemeDef kThemes[] =
        {
            { { 0.00f, 0.00f, 0.00f, 1 }, { 0.00f, 0.00f, 0.00f, 1 }, { 1.00f, 1.00f, 1.00f, 1 } },  // Black
            { { 0.05f, 0.22f, 0.12f, 1 }, { 0.01f, 0.04f, 0.02f, 1 }, { 0.12f, 0.84f, 0.38f, 1 } },  // Spotify
            { { 0.17f, 0.09f, 0.40f, 1 }, { 0.02f, 0.07f, 0.15f, 1 }, { 0.47f, 1.00f, 0.80f, 1 } },  // Aurora
            { { 0.40f, 0.12f, 0.17f, 1 }, { 0.08f, 0.02f, 0.08f, 1 }, { 1.00f, 0.63f, 0.35f, 1 } },  // Sunset
            { { 0.04f, 0.20f, 0.38f, 1 }, { 0.01f, 0.04f, 0.10f, 1 }, { 0.35f, 0.78f, 1.00f, 1 } },  // Ocean
            { { 0.00f, 0.00f, 0.00f, 1 }, { 0.00f, 0.00f, 0.00f, 1 }, { 1.00f, 1.00f, 1.00f, 1 } },  // Cover (dinamik)
        };
        constexpr int kThemeCover = 5;

        // Şarkı değişiminin toplam süresi: yarısı eskisini siler, yarısı
        // yenisini getirir.
        constexpr float kXfDur = 0.42f;

        ImFont* g_font = nullptr;
        ImFont* g_bold = nullptr;

        // Kutu: dikey gradyanı dilim dilim doldurup sonra üstüne ince bir çerçeve
        // vurmak. Tek AddRectFilled ile yapılsa köşelerde bant oluşuyor.
        void DrawIsland(ImDrawList* d, float x, float y, float w, float h, float r, ImVec4 top, ImVec4 bot)
        {
            r = std::min(r, std::min(w, h) * 0.5f);
            d->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), U32(Lerp4(top, bot, 0.5f)), r);

            if (std::fabs(top.x - bot.x) + std::fabs(top.y - bot.y) + std::fabs(top.z - bot.z) > 0.01f)
            {
                const int n = std::max(2, (int)(h / 2.f));
                for (int i = 0; i < n; i++)
                {
                    const float y0 = y + h * i / n;
                    const float sh = h / n + 0.6f;
                    const float yc = (y0 - y) + sh * 0.5f;
                    float inset = 0;
                    if (yc < r)      { const float dd = r - yc;      inset = r - std::sqrt(std::max(0.f, r * r - dd * dd)); }
                    else if (yc > h - r) { const float dd = yc - (h - r); inset = r - std::sqrt(std::max(0.f, r * r - dd * dd)); }
                    inset += 0.8f;
                    d->AddRectFilled(ImVec2(x + inset, y0), ImVec2(x + w - inset, y0 + sh),
                                     U32(Lerp4(top, bot, (i + 0.5f) / n)));
                }
            }
            d->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), Wh(0.10f), r, 0, 1.f);
        }

        // Taşan metin: genişliği aşarsa sola doğru kayar, sonuna gelince durur.
        void Marquee(ImDrawList* d, ImFont* f, float size, ImVec2 p, float maxw, ImU32 col,
                     const std::string& s, double t)
        {
            const ImVec2 ts = f->CalcTextSizeA(size, FLT_MAX, 0.f, s.c_str());
            d->PushClipRect(p, ImVec2(p.x + maxw, p.y + size * 1.5f), true);
            if (ts.x <= maxw) d->AddText(f, size, p, col, s.c_str());
            else
            {
                const float gap = S(40), cyc = ts.x + gap, pause = S(50);
                const float ph = std::fmod((float)t * S(30), cyc + pause);
                const float off = std::max(0.f, ph - pause);
                d->AddText(f, size, ImVec2(p.x - off, p.y), col, s.c_str());
                d->AddText(f, size, ImVec2(p.x - off + cyc, p.y), col, s.c_str());
            }
            d->PopClipRect();
        }

        void TextC(ImDrawList* d, ImFont* f, float size, float cx, float y, float maxw, ImU32 col, const char* s)
        {
            ImVec2 ts = f->CalcTextSizeA(size, FLT_MAX, 0.f, s);
            if (ts.x > maxw)
            {
                size *= std::max(0.7f, maxw / ts.x);
                ts = f->CalcTextSizeA(size, FLT_MAX, 0.f, s);
            }
            d->PushClipRect(ImVec2(cx - maxw * 0.5f, y - S(4)), ImVec2(cx + maxw * 0.5f, y + size * 1.6f), true);
            const float x = ts.x > maxw ? cx - maxw * 0.5f : cx - ts.x * 0.5f;
            d->AddText(f, size, ImVec2(x, y), col, s);
            d->PopClipRect();
        }

        // ---- ikonlar: yazı tipi değil, doğrudan çizim ------------------------
        // Adanın yazı tipi atlası küçük; ikonları vektör çizmek bir glyph
        // dosyasına bağımlılık getirmiyor ve her ölçekte net kalıyor.

        void IcoPlay(ImDrawList* d, ImVec2 c, float s, ImU32 col)
        {
            d->AddTriangleFilled(ImVec2(c.x - 0.55f * s, c.y - 0.85f * s),
                                 ImVec2(c.x - 0.55f * s, c.y + 0.85f * s),
                                 ImVec2(c.x + 0.95f * s, c.y), col);
        }
        void IcoPause(ImDrawList* d, ImVec2 c, float s, ImU32 col)
        {
            d->AddRectFilled(ImVec2(c.x - 0.7f * s, c.y - 0.85f * s), ImVec2(c.x - 0.2f * s, c.y + 0.85f * s), col, s * 0.12f);
            d->AddRectFilled(ImVec2(c.x + 0.2f * s, c.y - 0.85f * s), ImVec2(c.x + 0.7f * s, c.y + 0.85f * s), col, s * 0.12f);
        }
        void IcoNext(ImDrawList* d, ImVec2 c, float s, ImU32 col, bool prev)
        {
            const float k = prev ? -1.f : 1.f;
            d->AddTriangleFilled(ImVec2(c.x - 0.75f * s * k, c.y - 0.85f * s),
                                 ImVec2(c.x - 0.75f * s * k, c.y + 0.85f * s),
                                 ImVec2(c.x + 0.55f * s * k, c.y), col);
            const float a = c.x + 0.65f * s * k, b = c.x + 0.92f * s * k;
            d->AddRectFilled(ImVec2(std::min(a, b), c.y - 0.85f * s),
                             ImVec2(std::max(a, b), c.y + 0.85f * s), col, s * 0.08f);
        }
        void IcoShuffle(ImDrawList* d, ImVec2 c, float s, ImU32 col)
        {
            const ImVec2 a[4] = { { c.x - s, c.y + 0.6f * s }, { c.x - 0.2f * s, c.y + 0.6f * s },
                                  { c.x + 0.3f * s, c.y - 0.6f * s }, { c.x + 0.7f * s, c.y - 0.6f * s } };
            const ImVec2 b[4] = { { c.x - s, c.y - 0.6f * s }, { c.x - 0.2f * s, c.y - 0.6f * s },
                                  { c.x + 0.3f * s, c.y + 0.6f * s }, { c.x + 0.7f * s, c.y + 0.6f * s } };
            d->AddPolyline(a, 4, col, 0, s * 0.2f);
            d->AddPolyline(b, 4, col, 0, s * 0.2f);
            d->AddTriangleFilled(ImVec2(c.x + 0.65f * s, c.y - 1.0f * s), ImVec2(c.x + 0.65f * s, c.y - 0.2f * s),
                                 ImVec2(c.x + 1.2f * s, c.y - 0.6f * s), col);
            d->AddTriangleFilled(ImVec2(c.x + 0.65f * s, c.y + 0.2f * s), ImVec2(c.x + 0.65f * s, c.y + 1.0f * s),
                                 ImVec2(c.x + 1.2f * s, c.y + 0.6f * s), col);
        }
        void IcoRepeat(ImDrawList* d, ImVec2 c, float s, ImU32 col, int mode)
        {
            d->AddRect(ImVec2(c.x - s, c.y - 0.55f * s), ImVec2(c.x + s, c.y + 0.55f * s), col, s * 0.45f, 0, s * 0.2f);
            d->AddTriangleFilled(ImVec2(c.x + 0.15f * s, c.y - 1.0f * s), ImVec2(c.x + 0.15f * s, c.y - 0.1f * s),
                                 ImVec2(c.x + 0.8f * s, c.y - 0.55f * s), col);
            if (mode == 1) d->AddText(g_bold, s * 1.0f, ImVec2(c.x - s * 0.18f, c.y - s * 0.45f), col, "1");
        }
        void IcoSpeaker(ImDrawList* d, ImVec2 c, float s, ImU32 col, float vol)
        {
            d->AddRectFilled(ImVec2(c.x - s, c.y - 0.35f * s), ImVec2(c.x - 0.4f * s, c.y + 0.35f * s), col, s * 0.1f);
            const ImVec2 poly[4] = { { c.x - 0.45f * s, c.y - 0.35f * s }, { c.x + 0.2f * s, c.y - 0.9f * s },
                                     { c.x + 0.2f * s, c.y + 0.9f * s }, { c.x - 0.45f * s, c.y + 0.35f * s } };
            d->AddConvexPolyFilled(poly, 4, col);
            if (vol > 0.02f) { d->PathArcTo(ImVec2(c.x + 0.1f * s, c.y), 0.65f * s, -0.9f, 0.9f, 8); d->PathStroke(col, 0, s * 0.2f); }
            if (vol > 0.5f)  { d->PathArcTo(ImVec2(c.x + 0.1f * s, c.y), 1.10f * s, -0.9f, 0.9f, 10); d->PathStroke(col, 0, s * 0.2f); }
        }
        void IcoLyrics(ImDrawList* d, ImVec2 c, float s, ImU32 col)
        {
            d->AddLine(ImVec2(c.x - s * 0.8f, c.y - 0.7f * s), ImVec2(c.x + s * 0.8f, c.y - 0.7f * s), col, s * 0.22f);
            d->AddLine(ImVec2(c.x - s * 0.8f, c.y),          ImVec2(c.x + s * 0.25f, c.y), col, s * 0.22f);
            d->AddLine(ImVec2(c.x - s * 0.8f, c.y + 0.7f * s), ImVec2(c.x + s * 0.55f, c.y + 0.7f * s), col, s * 0.22f);
        }

        // =====================================================================
        //  Çizim durumu
        // =====================================================================

        struct UI
        {
            bool expanded = false, shown = false, deskOK = true;
            bool clickThrough = true, inited = false;
            bool desktopModeOn = false;     // masaüstü modu anahtarı
            int  panel = 0;                 // 0 yok, 1 sözler, 2 yardım
            float vis = 0, eq = 0, lyrPos = 0, uiVol = 0.5f, dragVal = 0;
            Spring sw, sh;
            double lastInside = 0, lastDesk = 0, lastVolSend = 0, lastVolCmd = -10, lastTop = 0;
            int   theme = 0, drag = 0;      // drag: 1 ilerleme, 2 ses
            ImVec4 cTop{ 0, 0, 0, 1 }, cBot{ 0, 0, 0, 1 }, cAcc{ 1, 1, 1, 1 };
            bool prevDown = false, ignoreClick = false;
            ID3D11ShaderResourceView* cover = nullptr;
            int coverVer = -1;
            uint64_t lyrTid = 0;

            // Şarkı değişiminin kimlik geçişi: eski kapak/başlık solar, yeni
            // belirir. Kontroller (düğmeler, ilerleme, ses) geçişe girmez;
            // %50 üst üste binen iki metin okunmaz olduğu için geçiş iki
            // aşamalıdır.
            float xf = 0;
            ID3D11ShaderResourceView* coverOld = nullptr;
            std::string pTitle, pArtist, pAlbum, oTitle, oArtist, oAlbum;
            bool pHas = false;
            uint64_t tid = 0;
        } g_u;

        ID3D11Device*           g_dev  = nullptr;   // == g_sharedDev (paylaşılan)
        ID3D11DeviceContext*    g_ctx  = nullptr;   // == g_sharedCtx (paylaşılan)
        IDXGISwapChain*         g_sc   = nullptr;   // ada ait
        ID3D11RenderTargetView* g_rtv  = nullptr;   // ada ait
        HWND                    g_hwnd = nullptr;
        // UYGULAMANIN cihazı ve bağlamı (bkz. SetSharedD3D). Sahibi değiliz:
        // serbest bırakılmaz, yalnızca kendi swap zincirimizi bunun üstünde
        // açarız. NULL ise ada hiç kurulmaz.
        ID3D11Device*           g_sharedDev = nullptr;
        ID3D11DeviceContext*    g_sharedCtx = nullptr;
        int   g_W = 0, g_H = 0, g_winX = 0, g_winY = 0;
        float g_wheel = 0.f;
        LARGE_INTEGER g_freq{};

        // Ön plan penceresi masaüstü mü: sınıf adı kontrolü tek yol. UWP
        // başlatıcılar "ApplicationFrameWindow" kullanır; onları da saymamak
        // için kabul edilen sınıflar bilerek dar tutulur.
        bool OnDesktop()
        {
            HWND fg = GetForegroundWindow();
            if (!fg || fg == g_hwnd) return true;
            wchar_t cls[64] = {};
            GetClassNameW(fg, cls, 64);
            return !wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") ||
                   !wcscmp(cls, L"Shell_TrayWnd") || !wcscmp(cls, L"Shell_SecondaryTrayWnd");
        }

        void SetClickThrough(bool on)
        {
            if (g_u.clickThrough == on) return;
            g_u.clickThrough = on;
            LONG_PTR ex = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
            SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE,
                              on ? (ex | WS_EX_TRANSPARENT) : (ex & ~(LONG_PTR)WS_EX_TRANSPARENT));
        }

        

        // =====================================================================
        //  Tek kare
        // =====================================================================

        // Ada, paylasilan baglamda IKINCI bir ImGui karesi ACMAZ.
        //
        // Neden: NewFrame() her pencere icin "WasActive = Active;" okuyup
        // Active'i false'a cekerken ActiveIdIsAlive canliligini de sifirlar.
        // Ada ana pencerenin pencerelerini Begin() etmedigi icin adanin
        // karesinden sonra Active false kalir; uygulamanin bir sonraki
        // NewFrame'inde WasActive false olur, FindHoveredWindowEx pencereyi
        // atlar, g.HoveredWindow hicbir karede dolmaz ve arayuzun hicbir
        // yerine tiklanamaz (olculdu: rWas=0, hovWin=(yok), itemHov=0;
        // surukleme calisiyordu cunku HandleDrag yalnizca IsMouseClicked
        // okuyor). Ayni karede ikinci kez kosan NewFrame ayni zamanda
        // FrameCount'i ikiye katlar, HoveredIdPreviousFrame'i sifirler ve
        // basili tutulan ogeleri bir kare icinde dusurur.
        //
        // Ada zaten hic widget kullanmiyor, yalnizca ImDrawList ciziyor:
        // kendi listesini baglamin shared data'siyla kuruyor ve kendi
        // ImDrawData'sini dogrudan DX11 backend'ine veriyor. Backend verisi
        // (vertex/index buffer, shader, yazi tipi dokusu) ayni cihazda
        // ana pencereninkiyle paylasildigi icin cizim sorunsuz.
        ImDrawList* g_dlist = nullptr;
        ImDrawData  g_ddata;

        void DrawFrame(float dt, double now)
        {
            UI& u = g_u;

            // ---- görünürlük ----
            if (now - u.lastDesk > 0.1) { u.deskOK = OnDesktop(); u.lastDesk = now; }
            const bool want = visible && (!u.desktopModeOn || u.deskOK);
            u.vis += ((want ? 1.f : 0.f) - u.vis) * (1.f - std::exp(-11.f * dt));

            if (u.vis < 0.003f && !want)
            {
                if (u.shown) { ShowWindow(g_hwnd, SW_HIDE); u.shown = false; u.expanded = false; u.panel = 0; }
                SetClickThrough(true);
                return;
            }
            if (!u.shown) { ShowWindow(g_hwnd, SW_SHOWNOACTIVATE); u.shown = true; }

            // Başka bir her zaman üstte pencere (bildirim balonu, tam ekran oyun)
            // adanın önüne geçebiliyor. İki saniyede bir geri getirmek, gözle
            // fark edilmeyen bir aralıkta yapılan bir iş.
            if (now - u.lastTop > 2.0)
            {
                SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                u.lastTop = now;
            }

            // ---- paylaşılan durumun kopyası ----
            bool conn, has, playing, shuffle, wantLyr;
            int repeat, lyrState, newCoverVer = u.coverVer;
            std::string title, artist, album;
            double dur, posBase;
            steady::time_point posT;
            float vol, dom[3];
            uint64_t tid;
            std::shared_ptr<const std::vector<LyricLine>> lyr;
            std::vector<uint8_t> px;

            {
                std::lock_guard<std::mutex> l(G.m);
                conn = G.connected; has = G.hasTrack; playing = G.playing;
                shuffle = G.shuffle; repeat = G.repeat;
                title = G.title; artist = G.artist; album = G.album;
                dur = G.dur; posBase = G.posBase; posT = G.posT;
                vol = G.volume; tid = G.trackId; lyr = G.lyr; lyrState = G.lyrState;
                std::memcpy(dom, G.dom, sizeof(dom));
                if (G.coverVer != u.coverVer) { px = G.cover; newCoverVer = G.coverVer; }
            }
            wantLyr = lyrics;

            // ---- şarkı değişimi: kimlik bloğunun iki aşamalı geçişi ----
            // Eskiyi ayırmak kapağın sürücüye yüklenmesinden ÖNCE olmalı: worker
            // parça değişimi ile yeni kapağı aynı turda yayımlıyorsa aşağıdaki
            // yükleme bloğu u.cover'ı zaten yenisine çevirmiş olur.
            if (u.xf > 0.f) u.xf = std::max(0.f, u.xf - (float)dt / kXfDur);
            if (tid != u.tid)
            {
                // u.tid == 0: ilk parça, sönecek bir şey yok. has == false:
                // oynatıcı kapandı, orada zaten ayrı bir durum çiziliyor.
                if (u.tid != 0 && has && u.pHas)
                {
                    if (u.coverOld) { u.coverOld->Release(); u.coverOld = nullptr; }
                    u.coverOld = u.cover;
                    u.cover = nullptr;
                    u.oTitle = u.pTitle; u.oArtist = u.pArtist; u.oAlbum = u.pAlbum;
                    u.xf = 1.f;
                }
                u.tid = tid;
            }
            u.pTitle = title; u.pArtist = artist; u.pAlbum = album; u.pHas = has;

            // xf 1→0: ilk yarıda eskisi görünür, ikinci yarıda yenisi.
            const float aOld = u.xf >= 0.5f ? Smooth((u.xf - 0.5f) * 2.f) : 0.f;
            const float aNew = u.xf >= 0.5f ? 0.f : Smooth((0.5f - u.xf) * 2.f);

            // Kapak dokusu sürücüye yalnızca burada, yani sunumdan önce
            // yüklenir. D3D11 nesneleri oluşturulduğu için başka bir yerde
            // güvenli değil.
            if (newCoverVer != u.coverVer)
            {
                u.coverVer = newCoverVer;
                if (u.cover) { u.cover->Release(); u.cover = nullptr; }
                if (px.size() == 128 * 128 * 4)
                {
                    D3D11_TEXTURE2D_DESC td{};
                    td.Width = 128; td.Height = 128; td.MipLevels = 1; td.ArraySize = 1;
                    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                    td.SampleDesc.Count = 1;
                    td.Usage = D3D11_USAGE_IMMUTABLE;
                    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

                    D3D11_SUBRESOURCE_DATA sd{ px.data(), 128 * 4, 0 };
                    ID3D11Texture2D* tex = nullptr;
                    if (SUCCEEDED(g_dev->CreateTexture2D(&td, &sd, &tex)))
                    {
                        g_dev->CreateShaderResourceView(tex, nullptr, &u.cover);
                        tex->Release();
                    }
                }
            }

            double pos = posBase;
            if (playing) pos += std::chrono::duration<double>(steady::now() - posT).count();
            if (dur > 0) pos = std::min(pos, dur);
            pos = std::max(0.0, pos);

            // ---- tema renkleri (yumuşak geçiş) ----
            ImVec4 tt, tb, ta;
            if (u.theme == kThemeCover)
            {
                const ImVec4 dc(dom[0], dom[1], dom[2], 1);
                tt = ImVec4(dc.x * 0.62f, dc.y * 0.62f, dc.z * 0.62f, 1);
                tb = ImVec4(dc.x * 0.10f, dc.y * 0.10f, dc.z * 0.10f, 1);
                ta = Lerp4(dc, ImVec4(1, 1, 1, 1), 0.55f);
            }
            else { tt = kThemes[u.theme].top; tb = kThemes[u.theme].bot; ta = kThemes[u.theme].acc; }

            const float k = 1.f - std::exp(-8.f * dt);
            u.cTop = Lerp4(u.cTop, tt, k);
            u.cBot = Lerp4(u.cBot, tb, k);
            u.cAcc = Lerp4(u.cAcc, ta, k);

            // ---- boyutlar / yaylar ----
            const float cw = has ? S(176) : S(116);
            const float ch = S(34);
            const float ew = S(420);
            const float baseH = S(196), extraH = S(96), topM = S(10);
            if (!u.inited) { u.sw.x = cw; u.sh.x = ch; u.inited = true; }

            const float tw = u.expanded ? ew : cw;
            const float th = u.expanded ? baseH + (u.panel ? extraH : 0.f) : ch;
            u.sw.Step(tw, dt, 380.f, 27.f);
            u.sh.Step(th, dt, 300.f, 26.f);

            const float w = std::max(cw * 0.9f, u.sw.x);
            const float h = std::max(ch * 0.9f, u.sh.x);
            const float sl = Smooth(u.vis);
            const float x0 = (g_W - w) * 0.5f;
            const float y0 = topM - (1.f - sl) * (h + topM + S(6));

            // ---- fare / hover ----
            POINT pt;
            GetCursorPos(&pt);
            const float mxp = (float)(pt.x - g_winX);
            const float myp = (float)(pt.y - g_winY);
            const float pad = S(8);
            const bool inside = u.vis > 0.9f && mxp >= x0 - pad && mxp <= x0 + w + pad &&
                                myp >= y0 - pad && myp <= y0 + h + pad;
            if (inside) { u.expanded = true; u.lastInside = now; }
            else if (u.expanded && u.drag == 0 && now - u.lastInside > 0.30) { u.expanded = false; u.panel = 0; }
            SetClickThrough(!(inside || u.drag));

            const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            // Tıklamayı yutan pencere: açılış karesinde kullanıcının tıklaması
            // (ya da arkasındaki pencerenin tıklaması) düğmelere sızmasın.
            if (down && !u.prevDown && !inside) u.ignoreClick = true;
            if (!down) u.ignoreClick = false;
            // Basılı bırakma karesinde kenar kaybolsun diye prevDown'dan ÖNCE
            // okunur: ada kendi tıklamasını kendi durumundan türetir.
            const bool clicked = down && !u.prevDown && !u.ignoreClick;
            u.prevDown = down;

            // Ada tıklanamaz olduğu için tekerlek mesajı düğmelere gelmiyor;
            // tekerleği doğrudan burada okuyup ses olarak kullanıyoruz.
            if (inside && u.expanded && g_wheel != 0.f && conn)
            {
                u.uiVol = Clamp01(u.uiVol + g_wheel);
                Push(C_VOL, u.uiVol);
                u.lastVolCmd = now;
            }
            g_wheel = 0.f;

            // ---- kendi cizim listesi ----
            // ImGui::GetBackgroundDrawList() yerine kendi listemiz: listeyi
            // kurmak kare acmak degil, yalnizca paylasilan shared data'yi
            // (yazi tipi, AA bayragi, doku referansi) okuyor.
            if (!g_dlist)
                g_dlist = IM_NEW(ImDrawList)(ImGui::GetDrawListSharedData());
            g_dlist->_OwnerName = "##Island";
            g_dlist->_ResetForNewFrame();
            g_dlist->PushTexture(ImGui::GetIO().Fonts->TexRef);
            g_dlist->PushClipRect(ImVec2(0.f, 0.f), ImVec2((float)g_W, (float)g_H), false);

            ImDrawList* d = g_dlist;
            const float p = Clamp01((w - cw) / (ew - cw));
            const float aMain = Smooth((p - 0.55f) / 0.45f);
            const float aMini = 1.f - Smooth(p / 0.35f);
            const float pe = Smooth(Clamp01((h - baseH) / extraH) * 1.1f);

            const ImVec4 black(0, 0, 0, 1);
            const float rad = Lerp(ch * 0.5f, S(32), Smooth(p));
            DrawIsland(d, x0, y0, w, h, rad, Lerp4(black, u.cTop, p), Lerp4(black, u.cBot, p));
            d->PushClipRect(ImVec2(x0, y0), ImVec2(x0 + w, y0 + h), true);

            u.eq += ((playing ? 1.f : 0.f) - u.eq) * (1.f - std::exp(-8.f * dt));
            const ImVec4 acc = u.cAcc;

            // ================== KÜÇÜK ==================
            if (aMini > 0.02f)
            {
                if (has)
                {
                    const ImVec2 a(x0 + S(5), y0 + (h - S(24)) * 0.5f);
                    ID3D11ShaderResourceView* tx = aOld > 0.f ? u.coverOld : u.cover;
                    const float ia = aOld > 0.f ? aOld : aNew;
                    if (tx)
                        d->AddImageRounded((ImTextureID)(intptr_t)tx, a, ImVec2(a.x + S(24), a.y + S(24)),
                                           ImVec2(0, 0), ImVec2(1, 1), Wh(aMini * ia), S(8));
                    else
                        d->AddRectFilled(a, ImVec2(a.x + S(24), a.y + S(24)), Wh(0.12f * aMini * ia), S(8));

                    const float bx = x0 + w - S(14) - 4 * S(3) - 3 * S(2.5f);
                    for (int i = 0; i < 4; i++)
                    {
                        const float amp = 0.5f + 0.5f * std::sin((float)now * (5.f + i * 1.7f) + i * 1.3f);
                        const float bh = S(3.5f) + u.eq * S(11) * (0.25f + 0.75f * amp);
                        const float cx = bx + i * (S(3) + S(2.5f));
                        const float cy = y0 + h * 0.5f;
                        d->AddRectFilled(ImVec2(cx, cy - bh * 0.5f), ImVec2(cx + S(3), cy + bh * 0.5f),
                                         WithA(acc, aMini), S(1.5f));
                    }
                }
                else
                {
                    TextC(d, g_bold, S(13), x0 + w * 0.5f, y0 + (h - S(13) * 1.3f) * 0.5f, w,
                          Wh(0.7f * aMini), brand::kNameA);
                }
            }

            // ================== GENİŞ ==================
            if (aMain > 0.02f)
            {
                const float ox = x0, oy = y0, A = aMain;
                const bool ui = A > 0.92f && u.expanded;
                // Konum ve tıklama karenin kendisinden gelir (bkz. DrawFrame
                // girişi); ImGui'nin fare durumu ana pencerenindir.
                const ImVec2 mp(mxp, myp);

                auto HitC = [&](ImVec2 c, float r)
                {
                    const float dx = mp.x - c.x, dy = mp.y - c.y;
                    return dx * dx + dy * dy <= r * r;
                };
                auto HitR = [&](ImVec2 a, ImVec2 b)
                {
                    return mp.x >= a.x && mp.x <= b.x && mp.y >= a.y && mp.y <= b.y;
                };

                const bool en = conn && has;
                const float ca = en ? 1.f : 0.35f;

                // kapak
                const ImVec2 cv0(ox + S(18), oy + S(18)), cv1(cv0.x + S(72), cv0.y + S(72));
                ID3D11ShaderResourceView* cvTex = aOld > 0.f ? u.coverOld : u.cover;
                const float ia = aOld > 0.f ? aOld : aNew;
                if (cvTex)
                    d->AddImageRounded((ImTextureID)(intptr_t)cvTex, cv0, cv1, ImVec2(0, 0), ImVec2(1, 1), Wh(A * ia), S(14));
                else
                    d->AddRectFilled(cv0, cv1, Wh(0.10f * A * ia), S(14));

                // başlıklar
                const float tx = ox + S(104), tmax = S(200);
                const bool oldIds = aOld > 0.f;
                const std::string t1 = oldIds ? u.oTitle : (has ? title : brand::kNameA);
                const std::string t2 = oldIds ? u.oArtist : (has ? artist : L(IslandOpenPlayer));
                Marquee(d, g_bold, S(17), ImVec2(tx, oy + S(20)), tmax, Wh(A * ia), t1, now);
                Marquee(d, g_font, S(14), ImVec2(tx, oy + S(43)), tmax, Wh(0.62f * A * ia), t2, now);
                const std::string t3 = oldIds ? u.oAlbum : ((!conn && has) ? L(IslandPlayerClosed) : album);
                d->PushClipRect(ImVec2(tx, oy + S(62)), ImVec2(tx + tmax, oy + S(82)), true);
                d->AddText(g_font, S(12), ImVec2(tx, oy + S(63)), Wh(0.38f * A * ia), t3.c_str());
                d->PopClipRect();

                // sağ üst düğmeler: tema / sözler / yardım
                const float by = oy + S(30), br = S(12);
                const ImVec2 cTheme(ox + ew - S(30), by), cLyr(ox + ew - S(60), by), cHelp(ox + ew - S(90), by);
                const bool hT = ui && HitC(cTheme, br + S(3));
                const bool hL = ui && HitC(cLyr, br + S(3));
                const bool hH = ui && HitC(cHelp, br + S(3));
                d->AddCircleFilled(cTheme, S(7.5f), WithA(acc, A), 24);
                d->AddCircle(cTheme, S(10.5f), Wh((hT ? 0.8f : 0.3f) * A), 24, S(1.5f));
                IcoLyrics(d, cLyr, S(7), (u.panel == 1) ? WithA(acc, A) : Wh((hL ? 0.95f : 0.5f) * A));
                d->AddCircle(cHelp, S(9.5f), (u.panel == 2) ? WithA(acc, A) : Wh((hH ? 0.95f : 0.5f) * A), 24, S(1.4f));
                d->AddText(g_bold, S(13), ImVec2(cHelp.x - S(3.6f), cHelp.y - S(8.2f)),
                           (u.panel == 2) ? WithA(acc, A) : Wh((hH ? 0.95f : 0.5f) * A), "?");
                if (hT && clicked) { u.theme = (u.theme + 1) % ThemeCount(); theme = u.theme; Save(); }
                if (hL && clicked) { u.panel = (u.panel == 1) ? 0 : 1; if (!wantLyr) u.panel = 0; }
                if (hH && clicked) u.panel = (u.panel == 2) ? 0 : 2;

                // ilerleme
                const float bx0 = ox + S(60), bx1 = ox + ew - S(60), byy = oy + S(112);
                const double shown = (u.drag == 1) ? u.dragVal * dur : pos;
                const bool hP = ui && en && dur > 0 && HitR(ImVec2(bx0 - S(8), byy - S(11)), ImVec2(bx1 + S(8), byy + S(11)));
                if (hP && clicked) u.drag = 1;
                if (u.drag == 1)
                {
                    u.dragVal = Clamp01((mp.x - bx0) / (bx1 - bx0));
                    if (!down)
                    {
                        const double target = u.dragVal * dur;
                        Push(C_SEEK, target);
                        { std::lock_guard<std::mutex> l(G.m); G.posBase = target; G.posT = steady::now(); }
                        u.drag = 0;
                    }
                }
                const float frac = dur > 0 ? (float)(shown / dur) : 0.f;
                const float bt = (hP || u.drag == 1) ? S(7) : S(4.5f);
                d->AddRectFilled(ImVec2(bx0, byy - bt * 0.5f), ImVec2(bx1, byy + bt * 0.5f), Wh(0.18f * A), bt * 0.5f);
                d->AddRectFilled(ImVec2(bx0, byy - bt * 0.5f), ImVec2(bx0 + (bx1 - bx0) * frac, byy + bt * 0.5f),
                                 WithA(acc, A * 0.95f), bt * 0.5f);

                char tb1[16], tb2[16];
                const int ps = (int)shown, ds = (int)dur;
                snprintf(tb1, sizeof(tb1), "%d:%02d", ps / 60, ps % 60);
                snprintf(tb2, sizeof(tb2), "%d:%02d", ds / 60, ds % 60);
                d->AddText(g_font, S(12), ImVec2(ox + S(18), byy - S(8)), Wh(0.55f * A), tb1);
                const ImVec2 t2s = g_font->CalcTextSizeA(S(12), FLT_MAX, 0.f, tb2);
                d->AddText(g_font, S(12), ImVec2(ox + ew - S(18) - t2s.x, byy - S(8)), Wh(0.55f * A), tb2);

                // oynatma düğmeleri
                const float cy = oy + S(146), mid = ox + ew * 0.5f;
                const ImVec2 cPlay(mid, cy), cPrev(mid - S(62), cy), cNext(mid + S(62), cy);
                const ImVec2 cShuf(mid - S(124), cy), cRep(mid + S(124), cy);
                const bool hPl = ui && en && HitC(cPlay, S(24));
                const bool hPr = ui && en && HitC(cPrev, S(20));
                const bool hNx = ui && en && HitC(cNext, S(20));
                const bool hSh = ui && en && HitC(cShuf, S(18));
                const bool hRp = ui && en && HitC(cRep, S(18));
                const float pr = S(20) + (hPl ? S(1.5f) : 0.f);
                d->AddCircleFilled(cPlay, pr, Wh(A * ca), 32);
                if (playing) IcoPause(d, cPlay, S(8.5f), Col(0, 0, 0, A));
                else         IcoPlay(d, cPlay, S(8.5f), Col(0, 0, 0, A));
                IcoNext(d, cPrev, S(8), Wh((hPr ? 1.f : 0.85f) * A * ca), true);
                IcoNext(d, cNext, S(8), Wh((hNx ? 1.f : 0.85f) * A * ca), false);
                IcoShuffle(d, cShuf, S(8), shuffle ? WithA(acc, A * ca) : Wh((hSh ? 0.8f : 0.42f) * A * ca));
                IcoRepeat(d, cRep, S(8), repeat ? WithA(acc, A * ca) : Wh((hRp ? 0.8f : 0.42f) * A * ca), repeat);
                if (clicked)
                {
                    if (hPl)
                    {
                        Push(C_PLAY);
                        std::lock_guard<std::mutex> l(G.m);
                        G.posBase = pos; G.posT = steady::now(); G.playing = !G.playing;
                    }
                    if (hPr) Push(C_PREV);
                    if (hNx) Push(C_NEXT);
                    if (hSh) { Push(C_SHUFFLE, shuffle ? 0 : 1); std::lock_guard<std::mutex> l(G.m); G.shuffle = !G.shuffle; }
                    if (hRp)
                    {
                        const int nm = repeat == 0 ? 2 : (repeat == 2 ? 1 : 0);
                        Push(C_REPEAT, nm);
                        std::lock_guard<std::mutex> l(G.m);
                        G.repeat = nm;
                    }
                }

                // ses
                const float vy = oy + S(178), vx0 = ox + S(48), vx1 = ox + ew - S(100);
                if (u.drag != 2 && now - u.lastVolCmd > 1.2 && vol >= 0.f) u.uiVol = vol;
                const bool hV = ui && conn && HitR(ImVec2(vx0 - S(8), vy - S(11)), ImVec2(vx1 + S(8), vy + S(11)));
                if (hV && clicked) u.drag = 2;
                if (u.drag == 2)
                {
                    u.uiVol = Clamp01((mp.x - vx0) / (vx1 - vx0));
                    u.lastVolCmd = now;
                    if (now - u.lastVolSend > 0.05) { Push(C_VOL, u.uiVol); u.lastVolSend = now; }
                    if (!down) { Push(C_VOL, u.uiVol); u.drag = 0; }
                }
                IcoSpeaker(d, ImVec2(ox + S(28), vy), S(7), Wh(0.6f * A), u.uiVol);
                const float vt = (hV || u.drag == 2) ? S(6) : S(4);
                d->AddRectFilled(ImVec2(vx0, vy - vt * 0.5f), ImVec2(vx1, vy + vt * 0.5f), Wh(0.18f * A), vt * 0.5f);
                d->AddRectFilled(ImVec2(vx0, vy - vt * 0.5f), ImVec2(vx0 + (vx1 - vx0) * u.uiVol, vy + vt * 0.5f),
                                 Wh(0.85f * A), vt * 0.5f);

                const ImVec2 bs = g_bold->CalcTextSizeA(S(11), FLT_MAX, 0.f, brand::kNameA);
                d->AddText(g_bold, S(11), ImVec2(ox + ew - S(18) - bs.x, vy - S(7)), WithA(acc, 0.55f * A), brand::kNameA);

                // ---- alt panel: sözler / yardım ----
                if (pe > 0.02f)
                {
                    const float py0 = oy + baseH - S(2), ap = pe * A;
                    d->AddLine(ImVec2(ox + S(18), py0), ImVec2(ox + ew - S(18), py0), Wh(0.08f * ap), 1.f);

                    if (u.panel == 1)
                    {
                        const float cxm = ox + ew * 0.5f, cyl = py0 + S(46), maxw = ew - S(40);
                        if (lyrState == 1 && lyr && !lyr->empty())
                        {
                            int idx = -1;
                            for (int i = 0; i < (int)lyr->size(); i++)
                            {
                                if ((*lyr)[i].t <= pos + 0.12) idx = i; else break;
                            }
                            if (u.lyrTid != tid) { u.lyrPos = (float)idx; u.lyrTid = tid; }
                            u.lyrPos += ((float)idx - u.lyrPos) * (1.f - std::exp(-9.f * dt));

                            d->PushClipRect(ImVec2(ox, py0 + S(4)), ImVec2(ox + ew, oy + h - S(8)), true);
                            const int n = (int)lyr->size();
                            for (int i = std::max(0, idx - 3); i <= std::min(n - 1, idx + 3); i++)
                            {
                                const float dd = i - u.lyrPos;
                                if (std::fabs(dd) > 2.3f) continue;
                                const float yy = cyl + dd * S(27) - S(9);
                                const std::string& s = (*lyr)[i].s;
                                const float a2 = (1.f - std::min(1.f, std::fabs(dd)) * 0.72f) * ap;
                                TextC(d, std::fabs(dd) < 0.5f ? g_bold : g_font, S(Lerp(17.f, 14.f, std::min(1.f, std::fabs(dd)))),
                                      cxm, yy, maxw, (std::fabs(dd) < 0.5f) ? Wh(a2) : Wh(a2 * 0.8f),
                                      s.empty() ? "..." : s.c_str());
                            }
                            d->PopClipRect();
                        }
                        else if (lyrState == 0 && has)
                            TextC(d, g_font, S(14), cxm, cyl - S(8), maxw, Wh(0.5f * ap), L(IslandLyricsLoading));
                        else
                            TextC(d, g_font, S(14), cxm, cyl - S(8), maxw, Wh(0.5f * ap), L(IslandNoLyrics));
                    }
                    else if (u.panel == 2)
                    {
                        d->AddText(g_font, S(13), ImVec2(ox + S(18), py0 + S(10)), Wh(0.75f * ap), L(IslandHintToggle));
                        d->AddText(g_font, S(13), ImVec2(ox + S(18), py0 + S(30)), Wh(0.55f * ap), L(IslandHintHover));

                        const ImVec2 a1(ox + S(18), py0 + S(56)), b1(a1.x + S(200), a1.y + S(26));
                        const ImVec2 a2(ox + S(228), py0 + S(56)), b2(a2.x + S(76), a2.y + S(26));
                        const bool h1 = ui && pe > 0.9f && HitR(a1, b1);
                        const bool h2 = ui && pe > 0.9f && HitR(a2, b2);
                        d->AddRectFilled(a1, b1, Wh((h1 ? 0.20f : 0.10f) * ap), S(13));
                        d->AddRectFilled(a2, b2, Wh((h2 ? 0.20f : 0.10f) * ap), S(13));

                        // L() bir işlev çağrısı, bir dize sabiti değil: bu yüzden
                        // bitişiklik değil, açık birleştirme.
                        std::string s1 = std::string(L(IslandDesktopMode)) + " " +
                                         (u.desktopModeOn ? L(IslandOn) : L(IslandOff));
                        TextC(d, g_font, S(13), (a1.x + b1.x) * 0.5f, a1.y + S(4), S(190), Wh(0.9f * ap), s1.c_str());
                        TextC(d, g_font, S(13), (a2.x + b2.x) * 0.5f, a2.y + S(4), S(70), Wh(0.9f * ap), L(Close));
                        if (clicked && h1)
                        {
                            u.desktopModeOn = !u.desktopModeOn;
                            desktopOnly = u.desktopModeOn;
                            Save();
                        }
                        // Kapat düğmesi uygulamayı değil adayı kapatır: VOID çalışmaya devam eder.
    if (clicked && h2) { island::visible = false; island::Save(); }
                    }
                }
            }
            d->PopClipRect();

            // ---- ciz ----
            // Backend yalnizca DisplayPos/DisplaySize/FramebufferScale ve
            // CmdLists okur; girdi durumuna hic bakmaz. Doku listesi NULL
            // birakiliyor: yazi tipi atlasini bu turda ana pencerenin karesi
            // zaten GPU'ya yukluyor (ada cizimi uygulama karesinden SONRA).
            g_ddata.Clear();
            g_ddata.Valid            = true;
            g_ddata.FrameCount       = ImGui::GetFrameCount();
            g_ddata.AddDrawList(g_dlist);
            g_ddata.DisplayPos       = ImVec2(0.f, 0.f);
            g_ddata.DisplaySize      = ImVec2((float)g_W, (float)g_H);
            g_ddata.FramebufferScale = ImVec2(1.f, 1.f);
            g_ddata.OwnerViewport    = ImGui::GetMainViewport();
            g_ddata.Textures         = nullptr;

            const float clear[4] = { 0, 0, 0, 0 };
            g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
            g_ctx->ClearRenderTargetView(g_rtv, clear);
            ImGui_ImplDX11_RenderDrawData(&g_ddata);
            // Dikey eşitleme YOK: ana pencere bu turda zaten bir kare bekledi.
            // Ada da beklerse tur iki beklemeyle ~30 kareye düşer; ada karesi
            // döngünün kendisi tarafından (uygulama karesi ya da Sleep(16))
            // zaten ~60'ta tutuluyor.
            g_sc->Present(0, 0);
        }

        // =====================================================================
        //  Pencere / D3D
        // =====================================================================

        LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
        {
            // Bu pencere kendi ImGui bağlamını kullanır; uygulamanın bağlamı
            // o an etkin olduğu için düğme/klavye olayları burada BİLEREK
            // aktarılmaz. Ada tamamen fare konumuna bakar (bkz. DrawFrame).
            switch (m)
            {
            case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
            case WM_MOUSEWHEEL:    g_wheel += GET_WHEEL_DELTA_WPARAM(w) / 120.f * 0.05f; return 0;
            case WM_DESTROY:       return 0;
            }
            return DefWindowProcW(h, m, w, l);
        }

        bool CreateD3D(HWND hwnd)
        {
            // Kendi cihazımız YOK. Swap zinciri, uygulamanın cihazı üzerinde
            // açılır: imgui_impl_dx11'in vertex/index buffer'ları bu cihazın
            // belleğinde durur, ikinci bir cihazda RenderDrawData geçersiz
            // adreslere yazar.
            if (!g_dev || !g_ctx) return false;

            DXGI_SWAP_CHAIN_DESC sd{};
            sd.BufferCount = 2;
            sd.BufferDesc.Width = g_W;
            sd.BufferDesc.Height = g_H;
            sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            sd.BufferDesc.RefreshRate = { 60, 1 };
            sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            sd.OutputWindow = hwnd;
            sd.SampleDesc.Count = 1;
            sd.Windowed = TRUE;
            sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

            HRESULT hr = E_FAIL;
            {
                // Swap zinciri cihazın kendi factory'sinden açılır. DXGI 1.0
                // CreateSwapChain kullanılır (bu SDK'da IDXGIFactory2 yok):
                // cihaz -> IDXGIDevice -> IDXGIAdapter -> IDXGIFactory.
                ComPtr<IDXGIDevice>  dxgiDev;
                ComPtr<IDXGIAdapter> adapter;
                ComPtr<IDXGIFactory> factory;
                hr = g_dev->QueryInterface(IID_PPV_ARGS(&dxgiDev));
                if (SUCCEEDED(hr)) hr = dxgiDev->GetAdapter(&adapter);
                if (SUCCEEDED(hr)) hr = adapter->GetParent(IID_PPV_ARGS(&factory));
                if (SUCCEEDED(hr)) hr = factory->CreateSwapChain(g_dev, &sd, &g_sc);
            }
            if (FAILED(hr)) return false;

            ID3D11Texture2D* bb = nullptr;
            g_sc->GetBuffer(0, IID_PPV_ARGS(&bb));
            if (bb) { g_dev->CreateRenderTargetView(bb, nullptr, &g_rtv); bb->Release(); }
            return g_rtv != nullptr;
        }

        // Adı CreateWindow olamaz: windows.h bunu makro olarak tanımlar
// (CreateWindowW'ye açılır) ve bildirim geçersizleşir.
        bool CreateIslandWindow()
        {
            // Yüzen ada, birincil monitörün üst ortasına oturur. Dört birimlik
            // aralık (mini genişlik, geniş hâl genişlik) pencere boyutunu
            // kolaylaştırır: boyut değişince pencere taşınmıyor, yalnızca
            // sunum hedefi yeniden oluşuyor.
            g_W = (int)S(460);
            g_H = (int)S(340);
            g_winX = (GetSystemMetrics(SM_CXSCREEN) - g_W) / 2;
            g_winY = 0;

            const wchar_t* cls = brand::kWindowClass;
            const std::wstring islandClass = std::wstring(cls) + L"_Island";

            HINSTANCE hi = GetModuleHandleW(nullptr);
            HICON icon = LoadIconW(nullptr, IDI_APPLICATION);

            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.style = CS_CLASSDC;
            wc.lpfnWndProc = WndProc;
            wc.hInstance = hi;
            wc.hIcon = icon;
            wc.hCursor = nullptr;   // fare yalnızca adanın üstündeyken görünür
            wc.lpszClassName = islandClass.c_str();
            RegisterClassExW(&wc);

            g_hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT |
                                     WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                     islandClass.c_str(), brand::kName, WS_POPUP,
                                     g_winX, g_winY, g_W, g_H, nullptr, nullptr, hi, nullptr);
            if (!g_hwnd) { UnregisterClassW(islandClass.c_str(), hi); return false; }

            SetLayeredWindowAttributes(g_hwnd, 0, 255, LWA_ALPHA);
            MARGINS mg{ -1, -1, -1, -1 };
            DwmExtendFrameIntoClientArea(g_hwnd, &mg);
            return CreateD3D(g_hwnd);
        }

        void DestroyIslandWindow()
        {
            if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
            if (g_sc)  { g_sc->Release();  g_sc = nullptr; }
            // g_dev / g_ctx UYGULAMANIN cihazıdır; serbest bırakılmaz.
            if (g_hwnd)
            {
                const std::wstring islandClass = std::wstring(brand::kWindowClass) + L"_Island";
                ::DestroyWindow(g_hwnd);
                g_hwnd = nullptr;
                UnregisterClassW(islandClass.c_str(), GetModuleHandleW(nullptr));
            }
        }

        bool g_built = false;
        LARGE_INTEGER g_last{};

        void Build()
        {
            if (g_built) return;
            if (!g_dev || !g_ctx) return;          // cihaz henüz paylaşılmadı
            if (!CreateIslandWindow()) return;

            // İkinci bir ImGui bağlamı YOK. Adanın karesi uygulamanın bağlamında
            // çizilir; yalnızca kendi penceresine ait ölçü ve zamanı o kare
            // boyunca IO'ya yazılır (bkz. DrawFrame).
            g_font = ::theme::fonts.regular ? ::theme::fonts.regular : ImGui::GetIO().Fonts->AddFontDefault();
            g_bold = ::theme::fonts.bold ? ::theme::fonts.bold : g_font;

            // Tercih kopyaları burada DEĞİL, Frame()'in kare başında alınır:
            // ayar sayfası pencere bir kez kurulduktan sonra da değişebilir.
            g_u = UI();

            g_run = true;
            g_worker = std::thread(Worker);

            QueryPerformanceFrequency(&g_freq);
            QueryPerformanceCounter(&g_last);
            g_built = true;
        }

        void Teardown()
        {
            if (!g_built) return;

            g_run = false;
            g_cv.notify_all();
            if (g_worker.joinable()) g_worker.join();

            // Paylaşılan bağlama, cihaza ve backend'lere dokunulmaz: onlar ana
            // pencerenin sahibidir. Burada yalnızca ADAYA ait olan serbest
            // kalır (swap zinciri, hedef görüntü, kapak dokusu, pencere).
            if (g_u.cover) { g_u.cover->Release(); g_u.cover = nullptr; }
            if (g_u.coverOld) { g_u.coverOld->Release(); g_u.coverOld = nullptr; }
            if (g_dlist) { IM_DELETE(g_dlist); g_dlist = nullptr; }
            g_ddata.Clear();
            DestroyIslandWindow();
            g_built = false;
        }

        bool g_prevInsert = false;
    }

    // =====================================================================
    //  Dış yüzey
    // =====================================================================

    void SetSharedD3D(ID3D11Device* device, ID3D11DeviceContext* context)
    {
        g_sharedDev = device;
        g_sharedCtx = context;
        g_dev = device;
        g_ctx = context;
    }

    void Startup(float dpi_scale)
    {
        g_scale = dpi_scale > 0.f ? dpi_scale : 1.f;
        LoadPrefs();
        if (enabled) Build();
    }

    void Shutdown()
    {
        Teardown();
    }

    void Save()
    {
        const std::wstring p = PrefPath();
        if (p.empty()) return;
        HANDLE f = ::CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) return;
        const char buf[5] =
        {
            enabled ? '1' : '0',
            desktopOnly ? '1' : '0',
            lyrics ? '1' : '0',
            (char)('0' + (theme % ThemeCount())),
            visible ? '1' : '0',
        };
        DWORD n = 0;
        ::WriteFile(f, buf, 5, &n, nullptr);
        ::CloseHandle(f);
    }

    void Frame()
    {
        // Ayar anahtarı: pencere kurulur / yıkılır. Uygulamanın kendi karesi
        // atlandığında da buraya gelinir, yoksa tepsideyken ada donup kalır.
        if (enabled && !g_built)  Build();
        if (!enabled && g_built)  Teardown();
        if (!g_built) return;

        // Ayar sayfası tercihleri doğrudan bu değişkenlere yazar. Adanın kendi
        // kopyası (g_u) olmadan tema ve masaüstü modu seçimi yalnızca pencere
        // kurulurken okunur, yani uygulama içinden yapılan değişiklik adaya
        // hiç ulaşmazdı. Ada yüzeyindeki düğmeler aynı değişkenleri geri
        // yazdığı için bu köprü çift yönlüdür: kim yazdıysa o kazanır.
        g_u.theme         = theme;
        g_u.desktopModeOn = desktopOnly;

        // INSERT: göster/gizle. Uygulamanın kendi kısayolu olmadığı için
        // çakışma yok; yine de kare başında basılı tutulup bırakılması tek
        // bir geçiş sayılır.
        const bool ins = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
        if (ins && !g_prevInsert) { visible = !visible; Save(); }
        g_prevInsert = ins;

        LARGE_INTEGER nowq;
        QueryPerformanceCounter(&nowq);
        const double now = (double)nowq.QuadPart / g_freq.QuadPart;
        float dt = (float)((nowq.QuadPart - g_last.QuadPart) / (double)g_freq.QuadPart);
        g_last = nowq;
        dt = std::min(dt, 0.033f);

        DrawFrame(dt, now);
    }
}
