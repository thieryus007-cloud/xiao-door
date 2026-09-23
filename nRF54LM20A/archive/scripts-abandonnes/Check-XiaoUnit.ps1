# Check-XiaoUnit.ps1 -- Lecture seule et robuste du numero de serie SWD
# d'un XIAO nRF54LM20A. Equivalent PowerShell de check-unit.sh, avec
# deux corrections issues de l'incident du 2026-09-23 (voir
# Procedure-Test-Connexion-USB-Flash-XIAO-nRF54LM20A.md) :
#
#   1. Sortie OpenOCD TOUJOURS redirigee vers un fichier (*>), jamais
#      capturee via une simple assignation de variable -- capture
#      ambigue en PowerShell qui a deja produit un faux "ECHEC" sur des
#      tentatives ayant reellement reussi.
#   2. Commande OpenOCD termine par "-c reset" avant "-c exit" (hygiene :
#      laisse la cible tourner normalement plutot que halted). ATTENTION,
#      CECI NE RESOUD PAS le Debug Interface Mode : un reset OpenOCD par
#      SWD est un reset logiciel/warm, PAS un cycle d'alimentation. Le
#      projet documente deja (2026-08-27/08-30, avant cet incident) que
#      seul un cycle d'alimentation complet (debrancher l'USB-C, ou PPK2)
#      sort reellement le SoC du Debug Interface Mode. La cause exacte de
#      l'instabilite rencontree le 2026-09-23 sur unit04 (D8E37DD8) N'A
#      PAS ete identifiee -- seul le contournement deja connu (cycle
#      d'alimentation) a ete reconfirme fonctionnel. Ne pas presenter
#      "-c reset" comme un correctif de ce symptome.
#
# Usage : .\Check-XiaoUnit.ps1 [-Attempts 6] [-WaitTimeoutSeconds 15]

param(
    [int]$Attempts = 6,
    [int]$WaitTimeoutSeconds = 15
)

$env:Path = "C:\ncs\tools\xpack-openocd-0.12.0-7\bin;" + $env:Path
$BOARD_DIR = "C:/ncs/vendor/platform-seeedboards/zephyr/boards/arm/xiao_nrf54lm20a"
$logFile = "$env:TEMP\check_xiao_unit_output.txt"

# Numeros de serie de production existants -- alerte seulement, cet
# outil ne flashe rien donc pas besoin de bloquer comme check-unit.sh.
$KnownUnits = @{
    "C5F0E209" = "#01 (PRODUCTION)"
    "9C4A557D" = "#02 (PRODUCTION)"
    "4587B5C1" = "#03 (PRODUCTION)"
}

function Wait-CmsisDapPresent {
    param([int]$TimeoutSeconds = 15)
    $elapsed = 0
    while ($elapsed -lt $TimeoutSeconds) {
        $dev = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue | Where-Object {
            $_.InstanceId -like "*VID_2886&PID_0068&MI_01*" -and $_.Status -eq "OK"
        }
        $composite = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue | Where-Object {
            $_.InstanceId -like "USB\VID_2886&PID_0068\*" -and $_.Status -eq "OK"
        }
        if ($dev -and $composite) {
            return $composite.InstanceId.Split('\')[-1]
        }
        Start-Sleep -Milliseconds 500
        $elapsed += 0.5
    }
    return $null
}

Write-Host "=== Etape 1 : attente d'une presence PnP stable (jusqu'a ${WaitTimeoutSeconds}s) ==="
$expectedSerial = Wait-CmsisDapPresent -TimeoutSeconds $WaitTimeoutSeconds
if (-not $expectedSerial) {
    Write-Host "ECHEC : aucun CMSIS-DAP v2 Adapter present apres ${WaitTimeoutSeconds}s d'attente."
    exit 1
}
Write-Host "  Present (serial attendu d'apres PnP : $expectedSerial)"

Write-Host "`n=== Etape 2 : lecture OpenOCD avec reset final, jusqu'a $Attempts tentatives ==="
$success = $false
$readSerial = $null
for ($i = 1; $i -le $Attempts; $i++) {
    Write-Host "--- Tentative $i/$Attempts ---"
    Remove-Item $logFile -ErrorAction SilentlyContinue

    & openocd -s "$BOARD_DIR/support" -f "$BOARD_DIR/support/openocd.cfg" `
        -c "cmsis-dap vid_pid 0x2886 0x0068" -c "cmsis-dap backend hid" -c "adapter speed 500" `
        -c "init" -c "reset" -c "exit" *> $logFile

    $content = Get-Content $logFile -Raw
    if ($content -match "CMSIS-DAP: Serial#\s*=\s*([A-Za-z0-9]+)") {
        $readSerial = $matches[1]
        $hasError = $content -match "(?m)^Error"
        if (-not $hasError) {
            Write-Host "  REUSSI, propre : Serial=$readSerial, aucune erreur, carte reset a la fin"
            $success = $true
            break
        } else {
            Write-Host "  Serial lu ($readSerial) MAIS erreur(s) presente(s) :"
            ($content -split "`r?`n" | Select-String "^Error") | ForEach-Object { Write-Host "    $_" }
        }
    } else {
        Write-Host "  ECHEC complet (pas de serial lu) : $(($content -split "`r?`n") | Select-Object -Last 1)"
    }
    Start-Sleep -Seconds 2
}

Write-Host ""
if ($success) {
    $label = if ($KnownUnits.ContainsKey($readSerial)) { $KnownUnits[$readSerial] } else { "unite inconnue de la table" }
    Write-Host "=== RESULTAT : $readSerial ($label) -- carte reset proprement apres verification ==="
    exit 0
} else {
    Write-Host "=== RESULTAT : ECHEC apres $Attempts tentatives ==="
    exit 1
}
