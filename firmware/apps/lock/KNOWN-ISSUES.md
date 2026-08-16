# Problèmes connus — Matter Door Lock sur XIAO nRF54LM20A

## 🔴 BLOQUANT — Le stack Matter/CHIP ne s'initialise jamais (Bluetooth inclus)

**Statut au 16/08/2026 (fin de session) : cause précise pas encore trouvée, mais très bien circonscrite. À reprendre en priorité.**

### Symptôme observé côté utilisateur

Le firmware compile, flashe et tourne sans planter (pas de HardFault, CPU actif normalement, LED par défaut visible — voir `README.md` de ce dossier pour le bug MCUboot déjà corrigé séparément). Mais :
- Aucune publicité BLE n'est jamais détectée par un scanner réel (testé : `bleak`/Python, réglages Bluetooth iPhone/macOS, app Home Assistant) — y compris à quelques centimètres, antenne connectée, puce entièrement effacée avant reflash.
- Le commissioning Matter échoue systématiquement (« Unable to Add Accessory »).

### Ce qui a été définitivement écarté (testé, pas juste supposé)

1. **Bug MCUboot bloquant le démarrage** — réel, corrigé (voir `README.md`), mais ne suffit pas : le firmware tourne bien après ce correctif, le problème ci-dessous est distinct et survient après.
2. **Extended advertising (BLE5) invisible aux scanners génériques** — écarté : testé aussi avec `CONFIG_CHIP_BLE_EXT_ADVERTISING=n` (publicité classique), toujours rien détecté.
3. **État de commissioning résiduel en flash** — écarté : `nrf54l_mass_erase` complet (voir `openocd.cfg`) puis reflash, toujours rien.
4. **Antenne 2.4 GHz non connectée** — écarté : antenne connectée, aucun changement. Cohérent avec le fait qu'OpenThread fonctionne très bien sur ce SoC/board (voir point 6) — le chemin RF/antenne n'est pas en cause.
5. **RF switch mal configuré** — écarté : l'exemple `zephyr-rfsw` de Seeed concerne la variante XIAO nRF54L**15**, pas la nôtre (nRF54LM20A). Pas de switch RF sur notre variante.
6. **Bug connu de la SoftDevice Controller Nordic sur ce SoC** ([Seeed-Studio/platform-seeedboards#65](https://github.com/Seeed-Studio/platform-seeedboards/issues/65), symptôme quasi identique, toujours ouverte) — **écarté pour NOTRE cas précis** : basculé vers le contrôleur logiciel Zephyr (`CONFIG_BT_LL_SW_SPLIT=y`, voir `sw-split-ble.conf` + overlay associé) et **le symptôme est resté identique à l'octet près** (mêmes valeurs `mFlags`/`mServiceMode`/`bt_dev` en mémoire). Donc pas le même bug que l'issue GitHub, ou pas seulement celui-là — notre blocage est plus en amont (voir ci-dessous).

### Ce qui est confirmé avec certitude (inspection mémoire live via GDB, pas des suppositions)

Méthode : `firmware/apps/lock/debug-inspect.sh <elf>` — connexion GDB courte (attach/lire/detach) sur le port GDB d'OpenOCD (3333), en lisant directement les objets C++ du firmware par leur nom de symbole grâce aux infos de debug (DWARF) incluses dans le build. RTT et une session GDB continue (`continue`/breakpoints) ne fonctionnent **pas** de façon fiable sur cette sonde CMSIS-DAP + OpenOCD (la connexion est coupée après quelques secondes, sur tous les essais, y compris gestion d'énergie désactivée) — seul le mode "instantané" (attach → lire → detach) est fiable.

Constats, du plus « en aval » au plus « en amont » :

- `bt_dev.flags == 0`, `bt_dev.hci_version == 0` → le contrôleur Bluetooth (HCI) n'a **jamais répondu**, quel que soit le contrôleur choisi (SoftDevice ou logiciel Zephyr).
- `BLEManagerImpl::sInstance.mFlags == 0`, `mServiceMode == kCHIPoBLEServiceMode_NotSupported` (valeur par défaut/zéro) → `BLEManagerImpl::_Init()` n'a **jamais exécuté son corps** (ses toutes premières lignes assignent inconditionnellement `mServiceMode` et `mFlags` avant tout risque d'échec — donc si elles sont à zéro, la fonction n'a même pas commencé).
- `PlatformManagerImpl::sInstance.mInitialized == false`, ET **`mChipThread` est intégralement à zéro** (thread jamais créé), **`mChipStackLock` a l'air jamais initialisé** (une mutex Zephyr fraîchement `k_mutex_init()`ée n'a normalement pas tous ses champs à zéro brut) → **`GenericPlatformManagerImpl_Zephyr::_InitChipStack()` n'a même pas exécuté ses toutes premières lignes** (`k_mutex_init`, `k_msgq_init`), qui sont les premières instructions de la fonction, avant tout appel à quoi que ce soit d'autre.

**Conclusion** : `PlatformMgr().InitChipStack()` — donc tout le stack Matter, pas seulement le Bluetooth — **n'est jamais appelé du tout**. Le problème n'est pas dans Matter/BLE lui-même, mais probablement **avant**, dans la séquence de démarrage de l'application (`AppTask::Init()` / `StartApp()`, ou même avant), qui ne semble jamais atteindre l'appel à `PrepareServer()`/`InitChipStack()` (dans `nrf/samples/matter/common/src/app/matter_init.cpp`).

Point notable : le firmware ne plante pas et le CPU tourne normalement (thread idle Zephyr actif, LED par défaut visible) — donc s'il y a un échec quelque part avant `InitChipStack()`, il est **silencieusement avalé** (pas de crash, pas de log visible faute de console série fonctionnelle sur cette carte).

### Prochaine étape (à faire en priorité à la reprise)

Vérifier, dans l'ordre, avec la même méthode GDB (`debug-inspect.sh` à étendre) :

1. Est-ce que `AppTask::Instance().StartApp()` (`main.cpp`) est même appelé/atteint un point avancé ? Chercher un symbole/état simple à vérifier dans `nrf/samples/matter/lock/src/app_task.cpp` et `nrf/samples/matter/common/src/app/app_task.cpp` (classe de base).
2. Que fait `Nrf::Board::Init()` (`nrf/samples/matter/common/src/board/board.cpp`) avant que Matter démarre — pourrait bloquer sur un périphérique (IMU I2C ? NFC ? PMIC ?) qui répond mal sur cette carte custom.
3. Vérifier si un composant appelé **avant** `PrepareServer()` dans `matter_init.cpp` (stockage persistant, factory data, NFC) bloque indéfiniment (attente d'un semaphore/mutex jamais libéré) plutôt que d'échouer proprement — expliquerait l'absence de crash ET l'absence de progression.
4. Envisager un breakpoint GDB sur `AppTask::Init` ou `main` directement **au moment du flash + premier attach** (avant que le CPU n'ait eu le temps de s'installer dans l'état idle qu'on observe systématiquement) — nos essais de breakpoint post-boot ont échoué à cause de déconnexions, mais un breakpoint posé immédiatement après flash (avant tout `resume`) pourrait tenir plus longtemps.

### Fichiers créés pendant cette investigation (conservés dans le repo)

- `legacy-ble.conf` — force la publicité BLE classique (test, à garder pour éliminer cette variable si besoin)
- `sw-split-ble.conf` + `boards/xiao_nrf54lm20a_nrf54lm20a_cpuapp-sw-split.overlay` — bascule vers le contrôleur Bluetooth logiciel Zephyr (test négatif concluant, cause écartée mais fichiers gardés en référence)
- `debug-rtt.conf` — tentatives RTT, **ne fonctionne pas de façon fiable sur cette carte**, gardé en documentation de ce qui a été essayé
- `debug-inspect.sh` — script GDB fonctionnel, réutilisable, à étendre pour la suite de l'investigation

### État du matériel en fin de session

Unit-01 a été reflashée plusieurs fois pendant cette session (dont un `nrf54l_mass_erase` complet). Le dernier firmware flashé est `/tmp/build-lock-swsplit` (contrôleur BLE logiciel) — **ce build est dans `/tmp`, pas dans le repo, il sera perdu au redémarrage de la machine**. Pour reprendre : reflasher `build-lock` (config standard, SoftDevice Controller) ou recompiler selon les commandes de `README.md`.
