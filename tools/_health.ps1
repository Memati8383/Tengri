$ErrorActionPreference = 'Continue'
$md = [System.IO.File]::ReadAllText("$env:TEMP\pr.md", [System.Text.Encoding]::UTF8)

$urls = [regex]::Matches($md, 'https://[^\s"''<>)]+') | ForEach-Object { $_.Value.TrimEnd('.',',',';') } | Sort-Object -Unique

$sonuc = @()
foreach($u in $urls){
    $durum = '?'
    $not = ''
    try{
        $r = Invoke-WebRequest -Uri $u -UseBasicParsing -TimeoutSec 20 -ErrorAction Stop
        $durum = [string]$r.StatusCode
        $not = '{0} bayt' -f $r.RawContentLength
    } catch {
        $sc = 'HATA'
        if($_.Exception.Response){ $sc = [string][int]$_.Exception.Response.StatusCode }
        $msg = $_.Exception.Message
        if($msg -match 'resolve|çözülemedi|Name or service'){ $sc = 'DNS-YOK' }
        if($msg -match 'SSL|tls'){ $sc = 'SSL' }
        $durum = $sc
        $not = $msg.Substring(0,[Math]::Min(48,$msg.Length))
    }
    $sonuc += [pscustomobject]@{ Durum=$durum; Bayt=$not; Url=$u }
}

$sonuc | Sort-Object Durum, Url | Format-Table -AutoSize -Wrap | Out-String -Width 200