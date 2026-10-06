$ErrorActionPreference = 'Stop'

# NOT: Onceki denemede bir yardimci fonksiyon kullanildi ve icindeki Write-Output,
# fonksiyonun donus degerine karisti; sonuc dosyanin 1. satirina yazildi. Bu yuzden
# burada fonksiyon YOK: her degisiklik dogrudan bir degiskene yazilir.
function Kaldir([string]$ad, [string]$pattern, [string]$text) {
    $yeni = [regex]::Replace($text, $pattern, '', 'Singleline')
    if($yeni -eq $text){
        [Console]::Error.WriteLine("  UYARI: '$ad' bulunamadi")
        [Console]::Error.WriteLine($text) | Out-Null
        return $text
    }
    [Console]::Error.WriteLine("  silindi: $ad")
    return ,$yeni
}

# Temiz kopyayi GitHub'dan cek (push edilmis, bozulmamis surum).
$gh = "C:\Program Files\GitHub CLI\gh.exe"
$r = & $gh api repos/Memati8383/Memati8383/contents/README.md --jq '.content'
$t = [System.Text.Encoding]::UTF8.GetString([Convert]::FromBase64String((($r -join '').Trim())))
[System.IO.File]::WriteAllText("$env:TEMP\pr.md", $t, (New-Object System.Text.UTF8Encoding $false))
$baslangic = ($t -split "`n").Count
[Console]::Error.WriteLine("temiz surum cekildi: $baslangic satir")

$t = Kaldir 'TikTok rozeti' '(?m)^[ \t]*<a href="https://www\.tiktok\.com/@baysecurity0".*?</a>\r?\n' $t
$t = Kaldir 'profil goruntulenme sayaci' '(?m)^<p align="center">\r?\n[ \t]*<img src="https://komarev\.com/ghpvc/.*?</p>\r?\n\r?\n' $t
$t = Kaldir 'eski heroku streak (tekrar)' '(?m)^<div align="center">\r?\n[ \t]*<img src="https://github-readme-streak-stats\.herokuapp\.com/.*?</div>\r?\n\r?\n' $t
$t = Kaldir 'react-dark activity graph' '(?m)^<div align="center">\r?\n[ \t]*<img src="https://github-readme-activity-graph\.vercel\.app/graph\?username=Memati8383&theme=react-dark.*?</div>\r?\n\r?\n' $t
$t = Kaldir 'activity graph (402, bozuk)' '(?m)^<p align="center">\r?\n[ \t]*<img height="280em" src="https://github-readme-activity-graph\.vercel\.app/.*?</p>\r?\n' $t
$t = Kaldir 'sondaki baglantisiz </div>' '(?s)\r?\n</div>\s*$' $t

# Trophy alt metni: baska bir isim yaziyor.
$e = 'alt="Emre G' + [char]0x00F6 + 'ksu''s GitHub Trophies"'
if($t.Contains($e)){
    $t = $t.Replace($e, 'alt="Memati8383 GitHub trophies"')
    [Console]::Error.WriteLine('  duzeltildi: trophy alt metni')
}

# Art arda bos satirlari tekilestir.
$t = [regex]::Replace($t, '(\r?\n){3,}', "`n`n")

[System.IO.File]::WriteAllText("$env:TEMP\pr.md", $t, (New-Object System.Text.UTF8Encoding $false))
[Console]::Error.WriteLine(("satir: {0} -> {1}" -f $baslangic, ($t -split "`n").Count))