#pragma once

// Alt süreçlere giden çalıştırılabilir yolu.
//
// CreateProcessW, lpApplicationName NULL bırakıldığında adı şu sırayla arar:
// uygulamanın kendi klasörü → geçerli klasör → System32 → Windows klasörü →
// PATH. İlk iki adım kullanıcı-yazılabilir klasörlerdir; portable kurulum
// zaten %USERPROFILE% altında koşuyor. Yani komut satırına yalın `reg.exe`
// yazmak, o klasöre bırakılmış bir `reg.exe`yi gereksinim varsa YÖNETİCİ
// olarak çalıştırmak demektir. Tam yol verildiğinde arama sırası hiç devreye
// girmez ve bu sınıf yerleştirme saldırısı kapanır.
//
// LoadLibraryW için de aynı kural geçerli: yalnızca ad verilirse arama
// uygulamanın klasöründen başlar.

#include <windows.h>

#include <string>

namespace syspath
{
    // Çözümleyemiyorsa boş dize döner. Çağıran bu durumda komutu denememeli;
    // "bir şey yapmadık" her zaman "yanlış programı çalıştırdık"tan iyidir.
    inline std::wstring System32()
    {
        static const std::wstring dir = []
        {
            wchar_t buf[MAX_PATH] = {};
            const DWORD n = ::GetSystemDirectoryW(buf, MAX_PATH);
            if (n == 0 || n >= MAX_PATH) return std::wstring();
            return std::wstring(buf, n);
        }();
        return dir;
    }

    // C:\Windows\System32\<name>. Çağıran komut satırına eklerken tırnağa
    // ihtiyaç duyarsa (boşluk içerebilir) kendisi ekler; burada ham yol dönüyor
    // çünkü LoadLibraryW tırnak kabul etmez.
    inline std::wstring Tool(const wchar_t* name)
    {
        const std::wstring dir = System32();
        if (dir.empty() || !name || !*name) return std::wstring();
        return dir + L"\\" + name;
    }

    // CreateProcessW'in lpCommandLine alanı için: tırnaklı program yolu +
    // argümanlar. name bulunamazsa boş döner.
    inline std::wstring CommandLine(const wchar_t* name, const std::wstring& args)
    {
        const std::wstring tool = Tool(name);
        if (tool.empty()) return std::wstring();
        std::wstring cmd = L"\"" + tool + L"\"";
        if (!args.empty()) { cmd += L' '; cmd += args; }
        return cmd;
    }
}
