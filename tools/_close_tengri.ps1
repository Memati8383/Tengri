$ErrorActionPreference = "SilentlyContinue"
$log = "C:/Users/USER/Downloads/-RELEASE-VOID-Open-Source-System-Optimizer-C-ImGui-56-Tweaks-FiveM-Delay-RAM-Profiles-main/VOID-github/build/_close.log"
$p = Get-Process -Name TENGRI -ErrorAction SilentlyContinue
if ($null -eq $p) { "TENGRI yok" | Out-File -Encoding ascii $log; exit 0 }
Stop-Process -Name TENGRI -Force
Start-Sleep -Milliseconds 800
if (Get-Process -Name TENGRI -ErrorAction SilentlyContinue) { "HALA CALISIYOR" | Out-File -Encoding ascii $log } else { "kapatildi" | Out-File -Encoding ascii $log }
