# release.yml'daki PowerShell bloklarinin SOZDIZIMINI dogrular.
#
# Neden ayri bir adim: GitHub Actions, run: blogu icindeki bir sozdizimi hatasini
# adimin BASINDA gostermez; blogu yorumlariyla birlikte tek bir komut olarak
# pwsh'e gecer ve hata, kodun hangi satirinda oldugu belli olmayan bir
# "else is not recognized" mesaji olarak cikar. v1.5.0'ta bir oyle durum yasandi:
# iki eski duzenlemeden kalma `else` blogu, `if` ile arasinda bos satir birakmis
# ve PowerShell "The term 'else' is not recognized" demişti. Hata kodda degil,
# blogun YAPISINDA ve ancak CI'da gorunuyordu.
#
# Dogrulama yerel calisir: YAML'in `run: |` bloklari cikarilir, girinti atilir
# ve PowerShell ayristiricisinden gecirilir. Ayristirici hata vermezse blogun
# sozdizimi gecerlidir.
#
# Kullanim:  powershell -File tools\check_workflow_syntax.ps1
# Cikis kodu: 0 = gecerli, 1 = sozdizimi hatasi var.

param(
    [string]$Workflow = '.github\workflows\release.yml'
)

$ErrorActionPreference = 'Stop'

$path = if ([System.IO.Path]::IsPathRooted($Workflow)) { $Workflow }
        else { Join-Path (Get-Location) $Workflow }

if (-not (Test-Path $path)) { throw "is akisi bulunamadi: $path" }

# BOM'lu UTF-8 ile okunur: bloklarda Turkce harfler var, ANSI'de okunursa
# metin degisir ve hata yerleri kayar.
$lines = Get-Content $path -Encoding UTF8

# "run: |" bloklarini ve bunlarin govde girintisini cikar.
$blocks = @()
for ($i = 0; $i -lt $lines.Count; $i++)
{
    if ($lines[$i] -notmatch '^(\s*)run: \|\s*$') { continue }

    $headIndent = $Matches[1].Length
    $body = @()
    $j = $i + 1

    # Govde, run: satirindan DAHA AZ girintili olamaz; o an blog biter.
    # Govde girintisi run: satirindan genellikle fazladır (YAML'ın gerekli
    # kabul ettiği tek fark), bu yüzden "tam eşleşme" aranmaz; daha az
    # girintili ilk satir blogun sonudur.
    $bodyIndent = $null
    for (; $j -lt $lines.Count; $j++)
    {
        $ln = $lines[$j]
        if ($ln.Trim().Length -eq 0) { $body += ''; continue }

        $lead = $ln.Length - $ln.TrimStart().Length
        if ($lead -lt $headIndent) { break }
        if ($null -eq $bodyIndent) { $bodyIndent = $lead }

        if ($lead -lt $bodyIndent) { break }
        $body += $ln.Substring($bodyIndent)
    }
    $blocks += , @{ StartLine = $i + 2; Body = $body }
}

if ($blocks.Count -eq 0) { throw "'run: |' blogu bulunamadi" }

$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("wfcheck_{0}.ps1" -f [Guid]::NewGuid().ToString('N'))
$bad = 0

try
{
    foreach ($b in $blocks)
    {
        [System.IO.File]::WriteAllLines($tmp, $b.Body, [System.Text.UTF8Encoding]::new($true))

        $errors = $null
        [void][System.Management.Automation.Language.Parser]::ParseFile($tmp, [ref]$null, [ref]$errors)

        if ($errors.Count -eq 0)
        {
            Write-Host ("  ok    satir {0} ({1} satir)" -f $b.StartLine, $b.Body.Count)
        }
        else
        {
            $bad++
            Write-Host ("  HATA  satir {0} ({1} satir)" -f $b.StartLine, $b.Body.Count) -ForegroundColor Red
            foreach ($e in $errors)
            {
                Write-Host ("        {0}  (blog satir {1})" -f $e.Message, $e.Extent.StartLineNumber)
            }
        }
    }
}
finally
{
    Remove-Item $tmp -ErrorAction SilentlyContinue
}

Write-Host ""
if ($bad -eq 0)
{
    Write-Host ("{0} blogun sozdizimi gecerli." -f $blocks.Count)
    exit 0
}

Write-Host ("{0}/{1} blogda sozdizimi hatasi var." -f $bad, $blocks.Count) -ForegroundColor Red
exit 1
