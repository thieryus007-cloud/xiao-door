# Problèmes connus — Matter Door Lock sur XIAO nRF54LM20A

## 🟡 PMIC (nPM1300) inaccessible en I2C — bug général confirmé, mais probablement pas bloquant pour l'IMU

**Statut mis à jour (17/08/2026, test comparatif sur 3 unités)** : le bug PMIC ci-dessous est **confirmé
général** (identique sur les 3 XIAO testés), mais contrairement à ce qu'on pensait initialement, **il
n'empêche pas l'IMU de fonctionner sur 2 des 3 unités** (`unit-02`, `unit-03`) — seule `unit-01` a l'IMU en
échec. Voir la section « Test comparatif sur 3 unités » plus bas pour le détail : `unit-01` a très
probablement un **défaut matériel isolé**, distinct du bug PMIC général. La Priorité 2 (IMU) n'est donc **pas
bloquée dans l'absolu** — juste sur `unit-01` spécifiquement. Utiliser `unit-02` ou `unit-03` pour la suite de
la validation (commissioning + test en conditions réelles dans HA).

Le code applicatif (lecture IMU, cluster Matter Boolean State) est écrit, compile, et fonctionne déjà sur
matériel réel (`unit-02`/`unit-03`) — voir plus bas « Priorité 2 — IMU 6 axes ». Le bug PMIC lui-même
(description ci-dessous) reste non résolu et mérite d'être compris/corrigé proprement à terme (le nPM1300 a
d'autres usages prévus — charge batterie, cluster Power Source), mais n'est plus le facteur bloquant pour
faire avancer la Priorité 2.

### Symptôme

`ImuManager::Init()` trouve `device_is_ready(lsm6ds3tr_c) == false` en permanence → le timer de lecture IMU
ne démarre jamais → le cluster Boolean State reste à sa valeur par défaut, jamais mis à jour, quoi qu'on
fasse physiquement à la carte (aucun changement visible dans Home Assistant, testé en inclinant l'unité dans
plusieurs positions).

### Cause identifiée

Confirmée sur matériel réel, sans console série — via lecture directe de `device_state.init_res` de chaque
device concerné par OpenOCD/GDB (voir méthode détaillée plus haut dans ce fichier).

La chaîne de dépendance est : `pmic_i2c` (bus I2C bit-bangé sur GPIO1.15/16) → driver MFD `nordic,npm1300`
(gère l'ensemble du PMIC : charger, LEDs, régulateurs) → régulateur `imu_vdd`/LDO1 → alimentation de l'IMU.

`mfd_npm13xx_init()` (priorité `MFD_NPM13XX_INIT_PRIORITY=80`) échoue avec **`-EIO` (init_res=5)** dès sa
toute première écriture I2C vers le PMIC. Ceci se propage en cascade : le régulateur `imu_vdd` échoue
ensuite avec `-ENODEV` (dépend du MFD), donc l'IMU n'est jamais alimenté, donc son propre driver échoue
aussi.

Bug **latent depuis le début du projet**, jamais rencontré avant l'ajout de l'IMU car rien d'autre dans
cette app ne communique en I2C avec le PMIC (les LEDs sont câblées en GPIO direct, indépendantes du PMIC).

### Trois hypothèses testées sur matériel réel, toutes écartées

1. **Timing de boot** — ajout d'un délai dédié (`SYS_INIT` à la priorité juste avant le MFD) testé à 50 ms,
   300 ms, puis 500 ms. Aucun effet à aucune valeur → ce n'est pas un problème de temps de démarrage du PMIC.
2. **Pull-up manquant sur SDA/SCL** — `sda-gpios`/`scl-gpios` (dans
   `firmware/boards/xiao_nrf54lm20a/xiao_nrf54lm20a_nrf54lm20a-common.dtsi`) n'avaient que `GPIO_ACTIVE_HIGH`,
   sans `GPIO_PULL_UP`. Ajouté → aucun changement.
3. **Open-drain manquant** — le driver `i2c_gpio.c` documente explicitement dans son en-tête que les pins
   doivent être configurées en open-drain avec pull-up (sans quoi le pin pousse activement le niveau haut,
   empêchant le PMIC de tirer la ligne à la masse pour acquitter). Ajout de `GPIO_OPEN_DRAIN` en plus du
   pull-up, vérifié dans le devicetree compilé (flags `0x16`, décomposition confirmée :
   `GPIO_SINGLE_ENDED | GPIO_LINE_OPEN_DRAIN | GPIO_PULL_UP`) et confirmé supporté par le driver GPIO nrfx
   (`gpio_nrfx.c`) pour ce SoC. **Toujours `-EIO`.**

Ces deux derniers fixes (pull-up + open-drain) sont **corrects et conservés** dans
`xiao_nrf54lm20a_nrf54lm20a-common.dtsi` (nécessaires même s'ils ne suffisent pas seuls), mais la cause
racine reste non identifiée.

### Piste écartée mais notée : ce n'est pas spécifique à ce repo

Le dépôt de référence Seeed cloné en local (`~/nrf-seeed-boards/zephyr/boards/arm/xiao_nrf54lm20a/`) contient
exactement la même config `pmic_i2c` non corrigée (sans pull-up ni open-drain) que celle dont ce projet est
parti — ce sous-système PMIC/charger n'a probablement jamais été testé par Seeed non plus (rien dans leurs
exemples publics n'utilise le charger/régulateurs du PMIC, seulement les LEDs en GPIO direct).

### Test comparatif sur 3 unités (17/08/2026) — résultat clé

Même firmware (`/tmp/build-lock`, avec les fixes pull-up + open-drain sur `pmic_i2c`) flashé successivement
sur les 3 XIAO disponibles, avec le même diagnostic par breakpoints matériels OpenOCD à chaque fois
(`ImuManager::Init()` ligne 59 "not ready" vs ligne 67 "success", puis `ReadAndUpdate()` jusqu'à
`SetStateValue()` pour confirmer une lecture complète) :

| Unité | `mfd_npm13xx_init()` (`init_res`) | `imu_vdd` regulator (`init_res`) | IMU (`device_is_ready`) | Lecture + cluster Boolean State |
|---|---|---|---|---|
| `unit-01` | `5` (`-EIO`) | `19` (`-ENODEV`) | ❌ faux (testé après reset **et** après un vrai power-cycle USB — même résultat) | ❌ jamais atteint |
| `unit-02` | `5` (`-EIO`) | `19` (`-ENODEV`) | ✅ vrai | ✅ confirmé (breakpoint sur `SetStateValue` atteint) |
| `unit-03` | `5` (`-EIO`) | `19` (`-ENODEV`) | ✅ vrai | ✅ confirmé (breakpoint sur `SetStateValue` atteint) |

**Conclusions** :

- Le bug PMIC (`-EIO`) est **strictement identique sur les 3 unités** — confirmé général, pas un défaut de
  soudure sur le bus `pmic_i2c` d'une unité en particulier.
- **L'IMU n'a pourtant besoin de rien côté PMIC pour fonctionner, sur 2 unités sur 3.** Ceci contredit
  l'hypothèse de départ (`imu_vdd`, contrôlé par le PMIC, serait indispensable à l'alimentation de l'IMU) —
  en pratique, sur `unit-02`/`unit-03`, l'IMU répond correctement dès le boot malgré `imu_vdd` en échec
  `-ENODEV`. Explication exacte non creusée (alimentation partagée/résiduelle suffisante malgré l'échec du
  contrôle I2C ?), non prioritaire vu que ça fonctionne.
- **`unit-01` a très probablement un défaut matériel isolé** (soudure ou composant sur le circuit IMU ou son
  alimentation propre), indépendant du bug PMIC général. Score 2/3 penche fortement vers "défaut de cette
  unité", pas "problème de design".

### À faire plus tard (reprise de l'investigation)

- **`unit-01`** : investigation matérielle dédiée si on veut la récupérer (inspection visuelle/loupe des
  soudures autour de l'IMU et de `imu_vdd`, continuité au multimètre) — non urgent, `unit-02`/`unit-03`
  suffisent pour continuer la Priorité 2.
- **Bug PMIC général** (toujours présent sur les 3 unités, `-EIO` sur `mfd_npm13xx_init()`) : à investiguer
  proprement avant d'avoir besoin des autres fonctions du PMIC (charge batterie, cluster Power Source,
  Priorité 2 §5min telemetry batterie). Pistes non essayées :
  - Vérifier la datasheet nPM1300 pour une éventuelle broche d'activation/reset supplémentaire non
    représentée dans le devicetree (`host-int-gpios` existe comme option dans le driver MFD mais n'est pas
    utilisée ici).
  - Essayer de forcer `CONFIG_I2C_GPIO_CLOCK_STRETCHING=n` (actuellement `y` par défaut) pour écarter un
    problème de timeout côté clock-stretching plutôt qu'un vrai NACK.
  - Remontée possible en tant qu'issue sur le repo Seeed `nrf-seeed-boards` (bug confirmé général sur 3
    unités, personne d'autre ne semble l'avoir documenté).
- Une fois le bug PMIC résolu : revalider que `imu_vdd`/le régulateur s'initialisent proprement (`init_res
  == 0`) sur les 3 unités, par cohérence — même si l'IMU fonctionne déjà sans.

## ✅ RÉSOLU (17/08/2026) — Commissioning Matter de bout en bout validé via Home Assistant + iPhone

Les trois causes ci-dessous (deux côté firmware, une côté commissioner) ont été corrigées et le commissioning
complet — QR code / code manuel → Home Assistant → appairage Thread → Device Attestation → fabric CHIP —
**a réussi de bout en bout** avec un iPhone et l'app Home Assistant. Confirmé dans les logs du Matter Server :

```text
INFO Controller~ndHandler Attestation accepted
INFO Controller~missioner Device attestation successfully verified with 2 accepted finding(s)
...
NOTICE CommissioningClient Commissioned peer19 as @1:22
INFO Endpoint server-2-134b.@1:22.ep1 ready endpoint#: 1 type: DoorLock (0x0a, rev 3) behaviors: ◆descriptor ◆identify ◆doorLock
```

Le device apparaît dans Home Assistant comme nœud Matter fonctionnel, endpoint `DoorLock`, fabric label
"Santuario". Voir le détail des trois causes ci-dessous.

## Le stack Matter/CHIP ne s'initialisait jamais (Bluetooth inclus)

**La session du 16/08/2026 (ci-dessous, conservée pour référence) avait diagnostiqué "le stack Matter n'est
jamais appelé". Ce diagnostic était FAUX** : il reposait sur `debug-inspect.sh` (GDB), qui retourne des
valeurs à zéro quand la connexion GDB se coupe en cours de session — au lieu d'une erreur. Comme la connexion
se coupe effectivement très souvent sur cette sonde (voir plus bas), toutes les lectures de cette session
étaient des faux zéros, pas l'état réel du firmware.

**Preuve du faux diagnostic** : relancer `debug-inspect.sh` tel quel sur un appareil dont l'état réel
(vérifié par lecture mémoire brute OpenOCD `mdw`, sans GDB) montrait `PlatformManagerImpl.mInitialized = true`
donne quand même `mInitialized = false` en sortie — parce que la ligne `Remote connection closed` apparaît
avant même le premier `print`.

**Méthode fiable utilisée pour la suite de l'investigation** : lecture mémoire brute via OpenOCD directement
(`mdw <addr> <n>`, sans passer par GDB du tout), en résolvant les adresses de symboles au préalable avec
`arm-zephyr-eabi-nm`/`arm-zephyr-eabi-gdb -batch -ex "ptype /o ..."` **sans connexion cible** (ces deux
dernières commandes lisent seulement les infos de debug de l'ELF, pas la cible live — donc jamais affectées
par une déconnexion). Pour localiser un point de blocage précis dans le code, poser un breakpoint matériel
OpenOCD (`bp <addr> 2 hw`, `reset run`, attendre, `targets` pour voir si l'état est resté `running` = jamais
atteint) fonctionne de façon fiable, contrairement à une session GDB continue.

### Ce qui a été vérifié comme fonctionnant réellement

- `Platform::MemoryInit()` et `PlatformMgr().InitChipStack()` réussissent (`PlatformManagerImpl.mInitialized
  == true` en mémoire live).
- `BLEManagerImpl::_Init()` s'exécute (`mFlags` contient `kAsyncInitCompleted`).
- Le contrôleur Bluetooth répond (`bt_dev` contient une adresse BLE réelle et cohérente).

### Cause réelle n°1 (bloquante) — bug de génération des factory data (nRF Connect SDK v3.2.1)

`AppTask::Init()` → `Nrf::Matter::PrepareServer()` → (planifié sur le thread CHIP) `DoInitChipServer()`
(`matter_init.cpp`) échouait systématiquement à l'étape `FactoryDataProvider::Init()` avec
`CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND` (0xA0) — **avant** d'atteindre
`InitializeStaticResourcesBeforeServerInit()` ou `Server::Init()` (confirmé par breakpoints matériels : ces
deux fonctions ne sont jamais atteintes). Résultat : aucune fenêtre de commissioning BLE ne s'ouvre jamais,
d'où « Unable to Add Accessory » côté app de commissioning.

**Mécanisme précis** : `generate_factory_data.cmake` (et `..._sysbuild.cmake`) ajoute toujours
`--product_finish ${CONFIG_CHIP_DEVICE_PRODUCT_FINISH}` (Kconfig avec défaut `"other"`) comme **dernier**
argument passé au générateur, donc `product_finish` est systématiquement la dernière clé du blob CBOR de
factory data. Sur cible, `ParseFactoryData()` (`FactoryDataParser.c`) échoue à décoder ce dernier champ —
confirmé en testant successivement :

1. Build normal avec `product_finish` présent → échec (0xA0), reproductible à l'identique après mass-erase.
2. Retrait uniquement de l'octet `0x00` parasite ajouté par un bug séparé de `putsz()` (voir plus bas) →
   toujours 0xA0, ce bug-là n'est **pas** la cause.
3. Retrait complet du champ `product_finish` du blob (donc `enable_key` devient le dernier champ) →
   `sInitResult == CHIP_NO_ERROR`, tout le reste de `DoInitChipServer()` s'exécute avec succès.

Le mécanisme exact côté `zcbor` (pourquoi le *dernier* champ précisément échoue) n'a pas été creusé plus loin
— non nécessaire pour la suite, le contournement est validé sur matériel réel à plusieurs reprises.

**Fix appliqué** : `firmware/apps/lock/fix_factory_data.py <build_dir>`, à lancer après `west build` et avant
`west flash`. Charge le `factory_data.bin` déjà généré, retire l'entrée CBOR `product_finish`, et réécrit
`merged.hex` (+ `factory_data.hex`/`.bin`) en place. `product_finish` est un champ cosmétique (finition du
boîtier, cluster Basic Information) — son absence n'a aucun impact fonctionnel.

**Bug séparé trouvé en cours de route (réel, mais pas la cause de ce blocage)** :
`scripts/tools/nrfconnect/nrfconnect_generate_partition.py` (dans le SDK, pas dans ce repo) utilise
`IntelHex.putsz()` au lieu de `puts()` pour écrire le blob CBOR — `putsz` est prévu pour des chaînes C
terminées par zéro et ajoute donc un octet `0x00` parasite après les données binaires. Confirmé en lisant le
code source d'`intelhex` installé. Testé isolément (voir point 2 ci-dessus) : ne cause pas ce blocage précis,
mais reste une vraie corruption de la partition factory data, à signaler à Nordic. `fix_factory_data.py`
corrige cet octet en passant par la même occasion (réécrit le blob proprement via `cbor2`/`IntelHex.puts()`).

### Cause réelle n°2 (bloquante, indépendante) — commissioning non démarré automatiquement

Même une fois la cause n°1 corrigée, aucune publicité BLE ne démarre : `Server::Init()` réussit
(`CHIP_NO_ERROR`), mais `BLEManagerImpl.mFlags` ne contient jamais `kAdvertisingEnabled`/`kAdvertising`.

**Cause** : `chip::CommonCaseDeviceServerInitParams::advertiseCommissionableIfNoFabrics` vaut `false` au
runtime (vérifié en lisant sa valeur statique compilée directement dans l'ELF). La lib CHIP amont documente un
défaut à `1` (`CHIP_DEVICE_CONFIG_ENABLE_PAIRING_AUTOSTART`), **mais le Kconfig nRF Connect SDK
`CHIP_ENABLE_PAIRING_AUTOSTART` (`config/zephyr/Kconfig`) le redéfinit à `default n`** — choix de conception
Nordic (économie d'énergie), pas un bug. Sans ce flag, il faut appuyer sur le bouton BLE
(`BLUETOOTH_ADV_BUTTON`, voir `board_config.h` et `Board::StartBLEAdvertisementHandler`) pour ouvrir la
fenêtre de commissioning.

Sur cette board custom, un seul bouton physique existe (`button0`, voir
`xiao_nrf54lm20a_nrf54lm20a-common.dtsi`) et `FUNCTION_BUTTON`/`BLUETOOTH_ADV_BUTTON` y sont tous les deux
mappés (`board_config.h`) — un appui court dessus devrait fonctionner, mais n'a pas été testé physiquement
(pas d'accès matériel direct pendant cette session de debug).

**Fix appliqué** : `firmware/apps/lock/pairing-autostart.conf`
(`CONFIG_CHIP_ENABLE_PAIRING_AUTOSTART=y`) — ouvre la fenêtre de commissioning automatiquement au premier
boot si l'appareil n'est pas commissionné. Alternative sans recompiler : appuyer sur le bouton physique de
l'unité.

### Cause réelle n°3 (bloquante, côté commissioner) — certificats DAC/PAI de développement rejetés par la politique de confiance par défaut de Home Assistant

Une fois les causes n°1 et n°2 corrigées, l'appareil publicise bien en BLE, le QR code/code manuel sont
acceptés par Home Assistant, le Thread join réussit — mais le commissioning échouait quand même
systématiquement (« Unable to Add Accessory » côté iPhone), sans jamais atteindre `CommissioningComplete`.

**Diagnostic** : les logs du Matter Server (`core_matter_server_*.log`, add-on Home Assistant) montrent
l'échec précisément à l'étape **Device Attestation** : le firmware envoie ses certificats DAC/PAI (Device
Attestation Certificate / Product Attestation Intermediate), et le commissioner les rejette. Le SDK Nordic
utilise par défaut les certificats de **développement/test** du CSA (`credentials/development/attestation/`
dans le SDK), qui ne sont **pas** inscrits dans la Distributed Compliance Ledger (DCL) de production que
Home Assistant interroge par défaut (`https://on.dcl.csa-iot.org`).

**Fix appliqué (côté Home Assistant, pas de recompilation nécessaire)** : activer l'option **« Enable
test-net DCL usage »** dans la configuration du Matter Server de Home Assistant (Paramètres → Appareils et
services → Matter → Configurer → options avancées). Cette option relance le `matter-server` avec le flag
`--enable-test-net-dcl`, qui fait consulter la DCL de test
(`https://on.test-net.dcl.csa-iot.org`) où les certificats de développement du CSA sont bien enregistrés.
Après activation et redémarrage du `matter-server`, le commissioning a réussi immédiatement, sans avoir
besoin de reflasher ou de power-cycler le XIAO (le Thread join et l'état de provisioning avaient déjà
persisté sur l'appareil).

**⚠️ Sur les ~20 unités de production**, il faudra soit continuer à activer cette option côté Home Assistant
(la plus simple), soit passer à des certificats DAC/PAI de production propres (signés et enregistrés dans la
vraie DCL) si l'appairage doit fonctionner avec des commissioners tiers n'offrant pas cette option (Apple
Home, Google Home — non testé, ces écosystèmes n'ont généralement pas d'équivalent grand public à ce
toggle).

### Validation matérielle complète (build minimal validé au 17/08/2026)

> Commande telle qu'utilisée à l'époque (build direct depuis le SDK, avant le fork Priorité 2). Depuis
> l'ajout de l'IMU, l'app est forkée dans ce repo et la commande a légèrement changé (chemin de l'app,
> `pairing-autostart.conf` fusionné dans `prj.conf`, `sysbuild/mcuboot/boards/` renommé en
> `mcuboot-overlay/`) — voir la commande à jour dans [README.md](README.md).

```bash
source firmware/build-env.sh

west build -p always -b xiao_nrf54lm20a/nrf54lm20a/cpuapp \
  -d /tmp/build-lock \
  firmware/apps/lock \
  -- \
  -DBOARD_ROOT=$(pwd)/firmware \
  -DEXTRA_DTC_OVERLAY_FILE=$(pwd)/firmware/apps/lock/boards/xiao_nrf54lm20a_nrf54lm20a_cpuapp.overlay \
  -DPM_STATIC_YML_FILE=$(pwd)/firmware/apps/lock/pm_static_xiao_nrf54lm20a_nrf54lm20a_cpuapp.yml \
  -Dmcuboot_EXTRA_DTC_OVERLAY_FILE=$(pwd)/firmware/apps/lock/mcuboot-overlay/xiao_nrf54lm20a_nrf54lm20a_cpuapp.overlay \
  -Dmcuboot_EXTRA_CONF_FILE=$(pwd)/firmware/apps/lock/mcuboot-overlay/xiao_nrf54lm20a_nrf54lm20a_cpuapp.conf

python3 firmware/apps/lock/fix_factory_data.py /tmp/build-lock   # <-- étape supplémentaire indispensable

west flash -d /tmp/build-lock --runner openocd
```

Résultat vérifié par scan BLE réel (`bleak`, macOS) après ce flash : l'appareil apparaît sous le nom
**`MatterLock`**, publicité Matter commissionable standard (UUID de service `0xFFF6`), avec le bon
vendor_id/product_id (`0xFFF1`/`0x8006`). Testé avec la publicité étendue par défaut
(`CONFIG_CHIP_BLE_EXT_ADVERTISING=y`, pas besoin de `legacy-ble.conf`) — donc le point 2 de l'ancienne
investigation ("extended advertising invisible aux scanners génériques") n'était pas non plus la cause :
c'était un symptôme du même stack cassé, pas un problème d'advertising en lui-même.

**Commissioning de bout en bout validé** (17/08/2026, iPhone + app Home Assistant) après activation de
« Enable test-net DCL usage » côté Matter Server — voir cause n°3 ci-dessus. `legacy-ble.conf` reste
disponible si un contrôleur particulier a du mal avec l'extended advertising (non nécessaire dans nos tests).

### Questions pratiques (post-commissioning)

- **Le bouton de commissioning reste-t-il actif après un appairage réussi ?** Oui — `pairing-autostart.conf`
  ouvre la fenêtre de commissioning automatiquement tant que l'appareil n'a **aucune fabric CHIP
  commissionnée** ; une fois commissionné, c'est le bouton physique (`button0`, fonction
  `BLUETOOTH_ADV_BUTTON`) qui permet de rouvrir une fenêtre de commissioning (ex. pour ajouter une deuxième
  fabric), pas de mécanisme automatique au boot une fois provisionné.
- **Peut-on recommissionner sur un autre réseau Thread ?** Oui, mais il faut d'abord retirer l'appareil de la
  fabric actuelle côté Home Assistant (« Supprimer l'appareil ») — CHIP supporte plusieurs fabrics
  simultanées (jusqu'à 5, voir `chip::FabricTable`), mais rejoindre un **autre réseau Thread** nécessite un
  nouveau commissioning complet (le Thread join fait partie du provisioning réseau transmis pendant la phase
  `NetworkCommissioning`, pas juste un ajout de fabric).
- **Temps de commissioning observé ?** De l'ordre de quelques dizaines de secondes une fois la publicité BLE
  détectée par Home Assistant (BLE PASE → Thread join → device attestation → NOC → CASE reconnect →
  `CommissioningComplete`) — non chronométré précisément, mais rapide et sans intervention manuelle
  au-delà du scan du QR code / saisie du code.

### ✅ RÉSOLU (17/08/2026) — passcode/discriminator uniques par unité

`unit-01` avait été commissionnée avec les valeurs d'exemple par défaut du SDK (passcode `20202021`,
discriminator `0xF00`, salt SPAKE2+ fixe — documentées comme telles dans
`modules/lib/matter/config/zephyr/Kconfig`, jamais surchargées jusqu'ici dans ce projet). `unit-02` et
`unit-03` ont été flashées avec le même firmware/factory data, donc les 3 unités partageaient exactement le
même code de commissioning — risque réel dès qu'on vise un parc de plusieurs unités (jusqu'à 25 en
configuration finale) : la fuite du code d'une porte compromet la fenêtre de commissioning de toutes les
autres.

**Fix** : `firmware/apps/lock/generate_unit_secrets.py` génère un discriminator (12 bits), un passcode
(27 bits, respecte les plages/exclusions imposées par le SDK) et un salt SPAKE2+ aléatoires et
cryptographiquement uniques par unité, avec vérification anti-collision contre les unités déjà générées. Le
fichier produit (`firmware/apps/lock/unit-secrets/<unit>.conf`, contient
`CONFIG_CHIP_DEVICE_DISCRIMINATOR`/`CONFIG_CHIP_DEVICE_SPAKE2_PASSCODE`/`CONFIG_CHIP_DEVICE_SPAKE2_SALT`) est
**gitignored** (jamais commit — le passcode est un secret) et se passe au build via
`-DEXTRA_CONF_FILE=firmware/apps/lock/unit-secrets/<unit>.conf`. Le SPAKE2+ verifier n'a pas besoin d'être
calculé à la main : `CONFIG_CHIP_FACTORY_DATA_GENERATE_SPAKE2_VERIFIER=y` (actif par défaut) le régénère
automatiquement au build à partir du passcode/salt configurés.

**Chaque unité doit maintenant avoir son propre dossier de build** (ex: `/tmp/build-lock-unit-03`), pas
partager le build générique `/tmp/build-lock` utilisé pour le développement/debug. Voir
`firmware/apps/lock/README.md` pour la commande complète.

**Reste à faire** : régénérer `unit-01`/`unit-02` avec leurs propres secrets uniques (seule `unit-03` l'a été
au moment de la rédaction) ; les DAC/PAI de développement fournis par le SDK restent, eux, partagés entre
toutes les unités (acceptable tant que « Enable test-net DCL usage » reste nécessaire côté Home Assistant,
voir plus haut) — l'attestation d'appareil (DAC/PAI) est un problème distinct du passcode de commissioning.

---

## Session du 16/08/2026 (investigation initiale — diagnostic partiellement invalidé, conservé pour traçabilité)

**Statut au 16/08/2026 (fin de session) : cause précise pas encore trouvée, mais très bien circonscrite. À
reprendre en priorité.**

> Note ajoutée le 17/08/2026 : la conclusion "`PlatformMgr().InitChipStack()` n'est jamais appelé du tout"
> ci-dessous s'est avérée **fausse** — voir section "RÉSOLU" en haut de ce fichier. Elle reposait sur des
> lectures GDB faussées par des déconnexions silencieuses. Le reste de cette section (bugs écartés,
> méthode de lecture PC via OpenOCD, fichiers créés) reste correct et utile.

### Symptôme observé côté utilisateur

Le firmware compile, flashe et tourne sans planter (pas de HardFault, CPU actif normalement, LED par défaut
visible — voir `README.md` de ce dossier pour le bug MCUboot déjà corrigé séparément). Mais :

- Aucune publicité BLE n'est jamais détectée par un scanner réel (testé : `bleak`/Python, réglages Bluetooth
  iPhone/macOS, app Home Assistant) — y compris à quelques centimètres, antenne connectée, puce entièrement
  effacée avant reflash.
- Le commissioning Matter échoue systématiquement (« Unable to Add Accessory »).

### Ce qui a été définitivement écarté (testé, pas juste supposé)

1. **Bug MCUboot bloquant le démarrage** — réel, corrigé (voir `README.md`), mais ne suffit pas : le firmware
   tourne bien après ce correctif, le problème ci-dessus est distinct et survient après.
2. **Extended advertising (BLE5) invisible aux scanners génériques** — écarté à nouveau, différemment : testé
   le 17/08 avec le stack réparé, `bleak` détecte bien l'appareil en extended advertising (défaut du sample).
3. **État de commissioning résiduel en flash** — écarté : `nrf54l_mass_erase` complet (voir `openocd.cfg`)
   puis reflash, toujours rien à l'époque (cohérent : la vraie cause n'avait rien à voir avec un résidu).
4. **Antenne 2.4 GHz non connectée** — écarté : antenne connectée, aucun changement.
5. **RF switch mal configuré** — écarté : l'exemple `zephyr-rfsw` de Seeed concerne la variante XIAO
   nRF54L**15**, pas la nôtre (nRF54LM20A). Pas de switch RF sur notre variante.
6. **Bug connu de la SoftDevice Controller Nordic sur ce SoC**
   ([Seeed-Studio/platform-seeedboards#65](https://github.com/Seeed-Studio/platform-seeedboards/issues/65))
   — écarté pour notre cas précis (basculé vers `CONFIG_BT_LL_SW_SPLIT=y`, symptôme resté identique).

### Fichiers créés pendant cette investigation (conservés dans le repo)

- `legacy-ble.conf` — force la publicité BLE classique (utile si un contrôleur a du mal avec l'extended
  advertising, non nécessaire pour le fix de base)
- `pairing-autostart.conf` — **fix réel** au moment de sa création (voir section "RÉSOLU"), depuis fusionné
  tel quel dans `prj.conf` lors du fork Priorité 2 et supprimé comme fichier séparé
- `fix_factory_data.py` — **fix réel**, voir section "RÉSOLU"
- `sw-split-ble.conf` + overlay associé — bascule vers le contrôleur Bluetooth logiciel Zephyr (test négatif
  concluant à l'époque, cause écartée, gardé en référence)
- `debug-rtt.conf` — tentatives RTT, ne fonctionne pas de façon fiable sur cette carte, gardé en documentation
- `debug-inspect.sh` — **⚠️ à ne plus utiliser tel quel pour diagnostiquer un blocage d'init** : donne des
  faux zéros silencieux en cas de coupure de connexion GDB (voir section "RÉSOLU"). Préférer les lectures
  OpenOCD brutes (`mdw`) documentées plus haut. Reste utilisable pour une inspection ponctuelle si on sait
  vérifier que la connexion n'a pas été coupée (absence de `Remote connection closed` dans la sortie).
