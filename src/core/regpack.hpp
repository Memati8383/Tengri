#pragma once

// Registry ayar gövdeleri, metin olarak gömülü. Diskten hiçbir şey okunmaz: ayar
// gövdesini geçici bir dosyaya yazar, reg.exe'e verir, sonra siler.
namespace regpack
{
    // `text` verisini %TEMP% altında UTF-16LE .reg dosyası olarak yazar, içe aktarır ve siler.
    bool Import(const char* text);

    // Birkaç gövdeyi tek .reg içinde birleştirir, böylece bir kategorinin tamamı tek
    // içe aktarma maliyetinde olur.
    bool ImportMany(const char* const* bodies, int count);

    // 4 (Oyunlar), 5 (FiveM), 6 (Gecikme), 7 (Valorant), 8 (CS2) ve 9 (Fortnite)
    // kategorilerinin gömülü gövdeleri.
    // enable = true ise açma, false ise geri alma gövdesini döndürür.
    const char* Body(int category, int index, bool enable);

    // Seçilen boyuttan çalışma anında üretilir; gb <= 0 Windows varsayılanına döner.
    bool ApplyRamProfile(int gb);
}