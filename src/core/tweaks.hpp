#pragma once

namespace tweaks
{
    bool Read(int category, int index);
    bool Apply(int category, int index, bool enable);

    // 4-6. kategoriler .reg gövdesi olarak gelir; birini uygulamak tek reg.exe geçişi demektir.
    bool IsRegPack(int category);

    // Bazı registry değerleri birden çok anahtar tarafından sahiplenilir
    // (SystemResponsiveness: Performans/Oyun/FiveM, TcpNoDelay: Oyun/Ağ). Kapalı olan bir
    // kardeşin yazdığı geri alma değerine yeniden dayatılmasın dipler, açık olan her paydaş
    // en son yeniden yazılır.
    bool IsShared(int category, int index);

    // `states` kategorideki her anahtarın istenen değeridir; `mask` (null olabilir) yazımı
    // yalnızca kullanıcının gerçekten dokunduğu anahtarlarla sınırlar, böylece başka bir
    // aracın ayarladığı değer üzerine hiçbir zaman geri alma yazılmaz.
    bool ApplyCategory(int category, const bool* states, const bool* mask, int count);
}