# Flash-XiaoUnit-JLink.ps1 -- variante de Flash-XiaoUnit.ps1 pour une unite
# SANS USB (connecteur casse) : sonde J-Link externe sur les pastilles SWD
# du nRF54 (SWCLK/SWDIO/GND + VTref), carte alimentee par BAT+/BAT-.
# Pas de pont SAMD11 alimente, donc pas de numero de serie de pont :
# l'identite de l'unite = son adresse BLE, lue dans FICR.INFO.DEVICEID.
#
# 1) Identification (lecture seule) :  .\Flash-XiaoUnit-JLink.ps1
# 2) Flash, une fois l'unite identifiee :
#    .\Flash-XiaoUnit-JLink.ps1 -Flash -ExpectedMac <MAC lue en 1> -Label <unitNN>

param(
    [switch]$Flash,
    [string]$ExpectedMac = "",
    [string]$Label = "",
    [switch]$AllowProtected
)

# #01 (reference) et #03 (ancienne architecture, aucun flash prevu)
$protected = @{ "D2:3A:F7:B1:E8:18" = "#01"; "E6:C9:11:CE:6E:C6" = "#03" }

$JLINK = "C:\Program Files\SEGGER\JLink_V924a\JLink.exe"
$HEX = "C:\ncs\projects\nRF54LM20A\xiao_door_sensor\golden-image\unit01-verified-2026-09-23-ABD-VBUSFIX-15uA.hex"
$BIN = "C:\ncs\projects\nRF54LM20A\xiao_door_sensor\golden-image\unit01-verified-2026-09-23-ABD-VBUSFIX-15uA.bin"
$cmdFile = "$env:TEMP\xiao_jlink_cmd.jlink"
$logFile = "$env:TEMP\xiao_jlink_out.txt"
$deployLog = Join-Path $PSScriptRoot "deployment-log.csv"

function Invoke-JLink([string[]]$commands) {
    ($commands + "qc") | Out-File $cmdFile -Encoding ascii
    & $JLINK -device nRF54LM20A_M33 -if SWD -speed 1000 -autoconnect 1 -NoGui 1 -ExitOnError 1 -CommanderScript $cmdFile *> $logFile
    return Get-Content $logFile
}

$out = Invoke-JLink @("mem32 0x00FFC304, 2")
$m = [regex]::Match(($out -join "`n"), "00FFC304\s*=\s*([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{8})")
if (-not $m.Success) {
    Write-Host "ECHEC : lecture FICR impossible. Sortie J-Link complete :"
    $out | ForEach-Object { Write-Host "  $_" }
    exit 1
}
$d0 = [Convert]::ToUInt32($m.Groups[1].Value, 16)
$d1 = [Convert]::ToUInt32($m.Groups[2].Value, 16)
$hw = @((($d1 -shr 24) -band 0xFF), (($d1 -shr 16) -band 0xFF), (($d1 -shr 8) -band 0xFF), ($d1 -band 0xFF),
        (($d0 -shr 24) -band 0xFF), (($d0 -shr 16) -band 0xFF))
$hw[5] = $hw[5] -bor 0xC0
$mac = ($hw[5..0] | ForEach-Object { '{0:X2}' -f $_ }) -join ':'
Write-Host ("MAC BLE : {0}  (FICR DEVICEID 0x{1:x8} 0x{2:x8})" -f $mac, $d0, $d1)

if (-not $Flash) {
    Write-Host "Identification seule. Comparer la MAC au tableau § 7 de Configuration-nRF54LM20A-System-ON-IDLE.md."
    exit 0
}
if ($mac -ne $ExpectedMac.ToUpper()) {
    Write-Host "ARRET : MAC lue '$mac' differente de la MAC attendue '$ExpectedMac'. Rien flashe."
    exit 1
}
if ($protected.ContainsKey($mac) -and -not $AllowProtected) {
    Write-Host ("ARRET : cette MAC est celle de {0} ; la flasher exige l'accord explicite de l'utilisateur (-AllowProtected)." -f $protected[$mac])
    exit 1
}

$out = Invoke-JLink @("r", "h", "loadfile `"$HEX`"", "verifybin `"$BIN`" 0x0", "r", "g")
$out | ForEach-Object { Write-Host "  $_" }
$text = $out -join "`n"
$status = if ($text -match "Verify successful" -and $text -notmatch "(?i)error|failed") { "verify OK (J-Link)" } else { "ECHEC ou resultat a verifier -- NE PAS DEPLOYER" }
Write-Host "RESULTAT : $status"
if (-not (Test-Path $deployLog)) { "timestamp,label,serial,image,status" | Out-File $deployLog -Encoding ascii }
Add-Content -Path $deployLog -Encoding ascii -Value ("{0},{1},(SWD externe J-Link),{2},{3} ; MAC BLE {4}" -f (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ"), $Label, (Split-Path $HEX -Leaf), $status, $mac)
if ($status -ne "verify OK (J-Link)") { exit 1 }
exit 0
