param([string[]]$Configs = @('1','2','4','8'), [switch]$SkipMeasure)
$ErrorActionPreference = 'Stop'

# Kontrol noktasi 1 dogrulamasini tek komutta kosur:
#   1) build.bat'in urettiği alti test cephesi (hepsi exit=0 olmali),
#   2) probe ile gercek $MFT olcumu (1/2/4/8 dilim) ve kapsam değişmezleri.
# Rapor: C:\Users\Public\tgengine\checkpoint1.txt  (Cikis: 0 = hepsi gecerli)
#
# Yonetici ister: probe ham bolumden `$MFT okur (olculen: GENERIC_READ ve
# yonetici olmadan her okuma ERROR_ACCESS_DENIED). UAC'i tek seferde gormek icin
# kendimizi bir kez yukseltip ayni betigi tekrar calistiriyoruz.
#
# Kullanim: powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_checkpoint1.ps1
#           ... -SkipMeasure      yalniz test cepheleri (hizli)
#           ... -Configs 1,8      belirli kosular

$root = Split-Path -Parent $PSScriptRoot
$id  = [Security.Principal.WindowsIdentity]::GetCurrent()
$pr  = New-Object Security.Principal.WindowsPrincipal $id
if (-not $pr.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $argl = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $PSCommandPath,
              '-Configs', ($Configs -join ','))
    if ($SkipMeasure) { $argl += '-SkipMeasure' }
    $up = Start-Process powershell -Verb RunAs -ArgumentList $argl -Wait -PassThru
    exit $up.ExitCode
}

$dir = 'C:\Users\Public\tgengine'
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$report = Join-Path $dir 'checkpoint1.txt'
Remove-Item -Force -ErrorAction SilentlyContinue $report

$fails = New-Object System.Collections.Generic.List[string]
function Say([string]$s) { Write-Output $s; Add-Content -Path $report -Value $s }
function Num([string]$txt, [string]$re) {
    $m = [regex]::Match($txt, $re)
    if ($m.Success) { return [int64]$m.Groups[1].Value }
    return -1
}
function Check([bool]$ok, [string]$name, [string]$detail) {
    if ($ok) { Say ("  OK    {0}  {1}" -f $name, $detail) }
    else     { Say ("  BOZUK {0}  {1}" -f $name, $detail); $fails.Add($name) }
}

Say "=== kontrol noktasi 1 dogrulamasi: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') ==="
Say ""

# ---------------------------------------------------------------- 1) test cepheleri
Say "-- test cepheleri (build\*.exe) --"
$suites = @(
    @{ n = 'test_pure';        re = '(\d+) gecti, (\d+) kaldi' },
    @{ n = 'test_modules';     re = '(\d+) gecti, (\d+) kaldi' },
    @{ n = 'test_license';     re = '(\d+) gecti, (\d+) kaldi' },
    @{ n = 'test_write_paths'; re = '(\d+) gecti, (\d+) kaldi' },
    @{ n = 'test_update';      re = '(\d+) denetim, (\d+) hata' },
    @{ n = 'test_disk';        re = '(\d+) basarili, (\d+) basarisiz' }
)
$grandChecks = 0; $grandBad = 0
foreach ($s in $suites) {
    $exe = Join-Path $root ('build\' + $s.n + '.exe')
    if (-not (Test-Path $exe)) { Check $false $s.n "exe yok: $exe (once build.bat)"; continue }
    $txt = (& $exe 2>&1) | Out-String
    $code = $LASTEXITCODE
    $m = [regex]::Match($txt, $s.re)
    if (-not $m.Success) { Check $false $s.n "ozet satiri bulunamadi, exit=$code"; continue }
    $ok = [int]$m.Groups[1].Value; $bad = [int]$m.Groups[2].Value
    $grandChecks += $ok; $grandBad += $bad
    Check ($code -eq 0 -and $bad -eq 0 -and $ok -gt 0) $s.n ("{0}/{1} gecerli, exit={2}" -f $ok, ($ok + $bad), $code)
}
Check ($grandBad -eq 0) 'test toplami' ("{0} denetim, {1} hata" -f $grandChecks, $grandBad)
Say ""

if ($SkipMeasure) {
    Say "-- olcum atlandi (-SkipMeasure) --"
} else {
    # ------------------------------------------------------------ 2) gercek $MFT olcumu
    $exe = Join-Path $root 'build\probe_mft_engine.exe'
    if (-not (Test-Path $exe)) {
        Check $false 'probe' "exe yok: $exe (_engine_probe_build.bat)"
    } else {
        Say '-- olcum: probe ile gercek `$MFT (dilim sayisina gore) --'
        $results = @{}
        foreach ($t in $Configs) {
            $out = Join-Path $dir ('cp1_t' + $t + '.txt')
            Remove-Item -Force -ErrorAction SilentlyContinue $out
            $started = Get-Date
            $p = Start-Process -FilePath $exe -ArgumentList @($out, $t) -Wait -PassThru
            $secs = [int]((Get-Date) - $started).TotalSeconds
            if (-not (Test-Path $out)) {
                Check ($p.ExitCode -eq 0) "t$t" "cikti dosyasi yok, exit=$($p.ExitCode)"
                continue
            }
            $txt = Get-Content $out -Raw
            $results[$t] = $txt

            $durum  = Num $txt 'durum=(\d+) motor='
            $motor  = Num $txt 'durum=\d+ motor=(\d+)'
            $sure   = Num $txt 'sure=(\d+) ms'
            $okunan = Num $txt 'okunan=(\d+) / toplam='
            $toplam = Num $txt 'okunan=\d+ / toplam=(\d+)'
            $dugum  = Num $txt 'dugum=(\d+) dosya='
            $ulasil = Num $txt 'kokten ulasilabilir=(\d+) /'
            $orphan = Num $txt 'orphan-bayrakli=(\d+)'
            $tablo  = Num $txt 'sahip: tablo=(\d+)'
            $sahips = Num $txt 'sahipsiz=(\d+) tablo-disi'
            $disi   = Num $txt 'tablo-disi-indeks=(\d+)'
            $sidN   = Num $txt 'sid-karsilastirilan=(\d+) eslesen='
            $sidOk  = Num $txt 'sid-karsilastirilan=\d+ eslesen=(\d+)'
            $farkli = Num $txt 'eslesen=\d+  farkli=(\d+)'
            $w32faz = Num $txt 'win32Fazla=(\d+)'

            Say ("  t{0}: sure={1} ms  bekle={2}s  dugum={3}  okunan={4}/{5}  tablo={6}" -f `
                 $t, $sure, $secs, $dugum, $okunan, $toplam, $tablo)
            # Kapsam değişmezleri: makineden bagimsiz doğruluk sartlari.
            Check ($p.ExitCode -eq 0 -and $durum -eq 2 -and $motor -eq 1) "t$t.tamam" `
                  ("exit=$($p.ExitCode) durum=$durum (2=Done) motor=$motor (1=Mft)")
            Check ($okunan -eq $toplam -and $toplam -gt 0) "t$t.kapsam" "okunan == toplam ($toplam bayt)"
            Check ($ulasil -eq $dugum -and $orphan -eq 0) "t$t.erisim" `
                  "kokten ulasilabilir=$ulasil / dugum=$dugum, orphan=$orphan"
            Check ($disi -eq 0 -and $tablo -gt 0) "t$t.sahip-indeks" "tablo=$tablo, tablo-disi=$disi, sahipsiz=$sahips"
            Check ($sidN -gt 0 -and $sidOk -eq $sidN) "t$t.sid" "$sidOk/$sidN sahip satiri Win32 SID'i ile birebir"
            # Boyut orneklemesi: 1 farka kadar tolerans canli disk yazimi yarışi
            # icin (olculdu: tek fark hep seride, dosya tarama sirasinda yazildi).
            # Win32'de olup bizde olmayan bit ise hic olmamali.
            Check ($farkli -ge 0 -and $farkli -le 1 -and $w32faz -eq 0) "t$t.boyut-bayrak" `
                  "boyut farki=$farkli (<=1 yaris toleransi), win32Fazla=$w32faz"
        }

        # Kosullar arasi tutarlilik: sayilar canli diskte degisir ama 1%'ten fazla
        # sapma siranin kaybindan gelir (yanlis ebeveyn / dusen dilim).
        $baseT = $Configs[0]
        if ($results.Count -gt 1 -and $results.ContainsKey($baseT)) {
            $base = Num $results[$baseT] 'dugum=(\d+) dosya='
            foreach ($t in $Configs) {
                if (-not $results.ContainsKey($t)) { continue }
                $d = Num $results[$t] 'dugum=(\d+) dosya='
                $tol = [math]::Max(1, [int64]([math]::Ceiling([math]::Abs($base) * 0.01)))
                Check ($base -ge 0 -and [math]::Abs($d - $base) -le $tol) "t$t.dugum-tutarlilik" `
                      ("dugum $d vs taban $base (+/- $tol)" )
            }
        }

        # Kazanc: #10'da kullanici %25 esigi koydu.
        if ($results.ContainsKey('1')) {
            $seri = Num $results['1'] 'sure=(\d+) ms'
            foreach ($t in $Configs) {
                if ($t -eq '1' -or -not $results.ContainsKey($t)) { continue }
                $ms = Num $results[$t] 'sure=(\d+) ms'
                $gain = if ($seri -gt 0) { [math]::Round(100.0 * ($seri - $ms) / $seri, 1) } else { 0 }
                Check ($gain -ge 25) "t$t.kazanc" ("seri $seri ms -> $ms ms = %$gain (esik %25)")
            }
        }
    }
}

Say ""
if ($fails.Count -eq 0) {
    Say "SONUC: GECTI  ($grandChecks test denetimi, 0 hata)"
    Say "rapor: $report"
    exit 0
}
Say ("SONUC: BASARISIZ  ({0} kalem) -> {1}" -f $fails.Count, ($fails -join ', '))
Say "rapor: $report"
exit 1
