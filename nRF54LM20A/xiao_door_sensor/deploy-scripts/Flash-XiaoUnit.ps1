# Flash-XiaoUnit.ps1 -- flashe l'image d'or sur UNE unite identifiee par le
# numero de serie de son pont SWD, puis verify_image. Refuse de flasher si
# le numero lu ne correspond pas exactement a celui passe en argument.
# A lancer uniquement apres un branchement propre (voir la procedure,
# § « Tempete USB au branchement »).
#
# Usage : .\Flash-XiaoUnit.ps1 -Serial 9C4A557D -Label unit02
#         #01 (C5F0E209) exige en plus -AllowUnit01 (accord explicite requis).

param(
    [Parameter(Mandatory = $true)][string]$Serial,
    [string]$Label = "",
    [switch]$AllowUnit01
)

$env:Path = "C:\ncs\tools\xpack-openocd-0.12.0-7\bin;" + $env:Path
$BOARD_DIR = "C:/ncs/vendor/platform-seeedboards/zephyr/boards/arm/xiao_nrf54lm20a"
$HEX = "C:/ncs/projects/nRF54LM20A/xiao_door_sensor/golden-image/unit01-verified-2026-09-23-ABD-VBUSFIX-15uA.hex"
$logFile = "$env:TEMP\flash_xiao_unit.txt"
$deployLog = Join-Path $PSScriptRoot "deployment-log.csv"
$mac = "inconnue"

if ($Serial -eq "C5F0E209" -and -not $AllowUnit01) {
    Write-Host "ARRET : C5F0E209 = #01, toute modification exige l'accord explicite de l'utilisateur (-AllowUnit01)."
    exit 1
}

function Write-DeployLog([string]$status) {
    if (-not (Test-Path $deployLog)) { "timestamp,label,serial,image,status" | Out-File $deployLog -Encoding ascii }
    $line = "{0},{1},{2},{3},{4} ; MAC BLE {5}" -f (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ"), $Label, $Serial, (Split-Path $HEX -Leaf), $status, $mac
    Add-Content -Path $deployLog -Value $line -Encoding ascii
}

& openocd -s "$BOARD_DIR/support" -f "$BOARD_DIR/support/openocd.cfg" `
    -c "cmsis-dap vid_pid 0x2886 0x0068" -c "cmsis-dap backend hid" -c "adapter speed 500" `
    -c "init" -c "mdw 0x00FFC304 2" -c "exit" *> $logFile
$read = (Get-Content $logFile | Select-String "Serial#\s*=\s*([A-Za-z0-9]+)").Matches | ForEach-Object { $_.Groups[1].Value }
if ($read -ne $Serial) {
    Write-Host "ARRET : serial lu '$read' different du serial attendu '$Serial'. Rien flashe."
    exit 1
}
Write-Host "Serial confirme : $read"

# Adresse BLE = celle que set_fixed_ble_identity() (main.c) derive de
# FICR.INFO.DEVICEID via hwinfo_get_device_id() (hwinfo_nrf.c).
$ficr = (Get-Content $logFile | Select-String "0x00ffc304:\s*([0-9a-f]{8})\s+([0-9a-f]{8})").Matches
if ($ficr) {
    $d0 = [Convert]::ToUInt32($ficr[0].Groups[1].Value, 16)
    $d1 = [Convert]::ToUInt32($ficr[0].Groups[2].Value, 16)
    $hw = @((($d1 -shr 24) -band 0xFF), (($d1 -shr 16) -band 0xFF), (($d1 -shr 8) -band 0xFF), ($d1 -band 0xFF),
            (($d0 -shr 24) -band 0xFF), (($d0 -shr 16) -band 0xFF))
    $hw[5] = $hw[5] -bor 0xC0
    $mac = ($hw[5..0] | ForEach-Object { '{0:X2}' -f $_ }) -join ':'
    Write-Host ("MAC BLE : {0}  (FICR DEVICEID 0x{1:x8} 0x{2:x8})" -f $mac, $d0, $d1)
} else {
    Write-Host "MAC BLE : lecture FICR impossible"
}

for ($i = 1; $i -le 5; $i++) {
    & openocd -s "$BOARD_DIR/support" -f "$BOARD_DIR/support/openocd.cfg" `
        -c "cmsis-dap vid_pid 0x2886 0x0068" -c "cmsis-dap backend hid" -c "adapter serial $Serial" -c "adapter speed 500" `
        -c "init" -c "reset halt" `
        -c "nrf54lm20a-load `"$HEX`"" `
        -c "reset halt" `
        -c "verify_image `"$HEX`"" `
        -c "reset" -c "exit" *> $logFile
    $ok = Get-Content $logFile | Select-String "verified"
    if ($ok) { Write-Host "REUSSI (tentative $i) : $ok"; Write-DeployLog "verify_image OK -- $ok"; exit 0 }
    Write-Host ("Tentative {0} : echec -- {1}" -f $i, (Get-Content $logFile | Select-String "^Error" | Select-Object -First 1))
    Start-Sleep -Seconds 1
}
Write-Host "ECHEC apres 5 tentatives -- NE PAS DEPLOYER, reflasher ou investiguer"
Write-DeployLog "ECHEC apres 5 tentatives -- NE PAS DEPLOYER"
exit 1
