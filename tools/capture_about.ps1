. .\tools\shoot.ps1

$p = Start-App
Start-Sleep -Seconds 5

# Whether the license screen appears depends on whether "remember me" had a saved key,
# so the activate button is clicked defensively. On an already-authenticated session this
# lands on empty dashboard space and does nothing.
Click-At 490 441
Start-Sleep -Seconds 6

Click-At 100 392          # Hakkinda, the seventh sidebar row
Start-Sleep -Milliseconds 1800
Hover-At 600 400
Start-Sleep -Milliseconds 900
Save-Shot 'docs\screenshots\07-about.png'