#pragma once

// Registry ayar gövdeleri, metin olarak gömülü. Diskten hiçbir şey okunmaz ve
// diske hiçbir şey yazılmaz: gövde bellekte çözümlenip RegSetValueExW ile
// uygulanıyor (regtext.hpp + regwrite.hpp).
namespace regpack
{
    // Gömülü bir gövdeyi çözüp registry'ye uygular. Kötü biçim ya da yazma
    // hatası varsa false.
    bool Import(const char* text);

    // Birkaç gövdeyi tek metinde birleştirir, böylece bir kategorinin tamamı tek
    // çözümleme/yazma geçişinde olur.
    bool ImportMany(const char* const* bodies, int count);

    // 4 (Oyunlar), 5 (FiveM), 6 (Gecikme), 7 (Valorant), 8 (CS2) ve 9 (Fortnite)
    // kategorilerinin gömülü gövdeleri.
    // enable = true ise açma, false ise geri alma gövdesini döndürür.
    const char* Body(int category, int index, bool enable);

    // Seçilen boyuttan çalışma anında üretilir; gb <= 0 Windows varsayılanına döner.
    bool ApplyRamProfile(int gb);
}