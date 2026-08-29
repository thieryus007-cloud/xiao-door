# Transposition — leçons de l'optimisation consommation XIAO nRF52840 Sense vers XIAO nRF54LM20A Sense

**Document de démarrage pour une nouvelle conversation**, dédiée à la
mesure/optimisation de consommation du firmware `xiao_door_sensor`
(XIAO nRF54LM20A Sense) au PPK II. Rédigé le 2026-08-26 à la demande de
l'utilisateur, à partir de la campagne d'optimisation tout juste terminée
sur le projet **différent** `xiao_nrf52840_door_sensor` (XIAO nRF52840
Sense) — objectif : transposer ce qui est transposable, signaler
explicitement ce qui ne l'est pas, sans dupliquer le contenu déjà écrit
ailleurs.

## Fichiers à lire, dans cet ordre

1. `C:\ncs\CLAUDE.md` — règles de travail, s'appliquent ici aussi.
2. `C:\ncs\projects\xiao_nrf54lm20a_project_notes.md` — référence complète
   du projet nRF54LM20A (architecture System OFF, registres IMU, trames
   BTHome, budget énergétique calculé). **Ce document-ci ne le remplace
   pas.**
3. `C:\ncs\projects\PPK-Mesures-Transition.md` — document de démarrage
   **déjà existant** pour cette carte précise, rédigé le 2026-08-30,
   contenant : l'avertissement matériel PPK II (voir § ci-dessous, il
   recoupe une découverte faite aujourd'hui sur l'autre projet), les
   chiffres théoriques à valider, les décisions à prendre en début de
   session (câblage PPK2 pas encore recherché pour cette carte, quelle
   unité utiliser). **Ce document-ci le complète, ne le remplace pas** —
   les deux sont à lire.
4. Ce document-ci, pour la transposition méthodologique depuis la
   campagne nRF52840.

## Ce qui a été fait sur le projet nRF52840 (résumé, détail complet dans
`C:\ncs\projects\xiao_nrf52840_door_sensor\Transition-nRF52840-Optimisation-Consommation.md`)

Campagne du 2026-08-26 : treize pistes matérielles/logicielles testées et
éliminées une à une (ODR accéléromètre, régulateur DC-DC/LDO, QSPI,
`CONFIG_PM`, `CONFIG_PM_DEVICE_SYSTEM_MANAGED`, VDDH, BLE/MPSL, etc.),
comparaison avec une nRF52840 DK nue, série de mesures à plusieurs
tensions, et une découverte méthodologique majeure sur le PPK2 lui-même
(voir section suivante). Résultat final : consommation de repos ramenée
de ~18 µA à ~11 µA (Source Meter, 3700 mV), autonomie estimée ~6,0 ans
sur 600 mAh — **chiffre probablement sous-estimé**, voir découverte
ci-dessous.

## Découverte la plus importante à transposer : l'artefact Source Meter du PPK2

**C'est la leçon la plus critique de toute la campagne nRF52840, et elle
recoupe directement un avertissement déjà présent dans
`PPK-Mesures-Transition.md`** (§ « Règle de travail impérative », danger
DevZone Nordic carte nRF54L15-DK endommagée) : ce document interdisait
déjà le mode Source Meter pour une raison de **sécurité matérielle**
(risque de destruction si la tension dépasse le maximum admissible d'un
point d'injection). La campagne nRF52840 apporte une **deuxième raison,
indépendante, de précision de mesure** :

Sur la DK nRF52840 nue, une comparaison directe à tension quasi identique
entre les deux modes PPK2 a donné :
- **Source Meter (3700 mV, PPK2 = seule source, remplace la pile)** :
  ~11 µA de moyenne.
- **Ampere meter (alimentation externe stable réelle, PPK2 = ampèremètre
  passif en série)** : ~3 µA de moyenne — **même carte, tension quasi
  identique, seul le mode PPK2 change.**

Treize pistes matérielles + le contrôleur BLE/MPSL + le sondage IMU ont
été testés et exclus comme cause de cet écart avant d'isoler le mode
PPK2 lui-même comme variable responsable. Conclusion : **le mode Source
Meter du PPK2 semble injecter plusieurs µA de courant parasite non
représentatif du courant réellement consommé par la carte.**

**Implication directe pour la session nRF54LM20A** : `PPK-Mesures-Transition.md`
imposait déjà le mode Ampere meter pour des raisons de sécurité — cette
règle est maintenant *doublement* justifiée (sécurité **et** précision de
mesure). **Ne jamais utiliser le mode Source Meter pour les mesures de
repos sur cette carte**, même si l'occasion se présente d'alimenter le
SoC directement (ex. un connecteur d'alimentation externe similaire à ce
qui existe sur une DK) — toujours en série avec l'alimentation réelle de
la carte (pile/batterie), jamais en remplacement.

**Non résolu, à garder en tête** : on ne sait pas encore si cet écart
Source/Ampere meter est un phénomène **générique au PPK2** (donc présent
aussi sur la nRF54LM20A) ou spécifique au nRF52840. Aucune mesure croisée
n'a été faite sur un SoC nRF54L. Si les deux modes sont disponibles pour
la nRF54LM20A (câblage à confirmer, voir `PPK-Mesures-Transition.md`),
refaire la même comparaison croisée serait très informatif — mais
`PPK-Mesures-Transition.md` interdit déjà le Source Meter pour raison de
sécurité, donc cette comparaison ne pourra probablement pas être faite
sans risque sur cette carte précise. À ne tenter que si un point
d'injection sûr et dont la tension max est vérifiée existe.

## Autres méthodes/disciplines validées aujourd'hui, transposables telles quelles

- **Un seul changement à la fois entre deux mesures PPK2**, jamais
  plusieurs hypothèses testées ensemble (leçon déjà connue, reconfirmée).
- **Consigner chaque mesure dans le document de suivi immédiatement**,
  y compris les résultats négatifs/« sans effet » — ne pas attendre
  d'avoir une conclusion pour documenter le test.
- **Captures PPK2 de ~3 s suffisent** au repos pour des statistiques
  moyenne/max/charge stables (fichiers CSV volumineux sinon).
- **Comparaison avec une carte de référence nue** (DK officielle Nordic,
  sans les circuits spécifiques du produit XIAO) : méthode qui a permis
  de trancher définitivement le caractère générique vs spécifique-carte
  des pics ~20-25 ms sur nRF52840. **Vérifier si une nRF54L-DK ou
  équivalent est disponible** avant de supposer que cette méthode est
  applicable ici — non vérifié à la rédaction de ce document.
- **Identification fiable d'une unité connectée** : leçon apprise à la
  dure aujourd'hui — ne jamais déduire l'identité d'une carte à partir
  d'un scan BLE ou d'une tuile Home Assistant déjà existante (les deux
  peuvent capter une **autre** unité déjà déployée à proximité). Seule
  une **lecture directe du port série** (log de boot affichant l'adresse
  BLE fixe) ou une **vérification physique** (étiquette) fait foi. Ne
  brancher qu'**une seule carte à la fois** sur le PC.
- **Procédure de flash à valider pour cette carte** : le projet
  nRF52840 utilise un bootloader UF2 (glisser-déposer, pas de sonde SWD).
  La nRF54LM20A utilise (voir `xiao_nrf54lm20a_project_notes.md`) un pont
  CMSIS-DAP (SAMD11, VID/PID `2886:0068`) — **procédure différente, déjà
  documentée dans ce projet**, ne pas réutiliser la procédure UF2 du
  nRF52840 par réflexe.

## Ce qui n'est PAS transposable tel quel — à revérifier sur cette carte, pas à supposer

- **`CONFIG_PM=y` inopérant** : confirmé spécifiquement pour les Kconfig
  `nrf52*` (`zephyr/soc/nordic/nrf52/Kconfig` ne fait pas
  `select HAS_PM`). La nRF54LM20A est de la famille **nRF54L**, pas
  nRF52 — le statut de `HAS_PM` pour cette famille précise **n'a pas été
  revérifié** dans le cadre de ce document. Ne pas supposer que
  `CONFIG_PM` est également inopérant ici sans revérifier
  `zephyr/soc/nordic/nrf54l/Kconfig` (ou équivalent) sur ce checkout NCS.
- **Comparaison régulateur DC-DC vs LDO (`&reg1`)** : spécifique au
  régulateur interne du nRF52840. La nRF54LM20A utilise un **PMIC externe
  nPM1300** (architecture d'alimentation complètement différente,
  batterie → PMIC → SoC) — cette piste ne se transpose pas directement ;
  l'équivalent pertinent serait plutôt les modes du nPM1300 lui-même
  (Hibernate/Ship mode, déjà mentionnés dans `PPK-Mesures-Transition.md`
  comme piste à explorer, retour communautaire Seeed forum).
- **Architecture d'alimentation opposée** : le projet nRF52840 a
  **abandonné** le System OFF (jugé non fiable après plusieurs
  correctifs infructueux, voir `Transition-nRF52840-Sense-Demarrage.md`
  § « Cinquième révision ») pour rester en System ON en permanence. Le
  projet nRF54LM20A fait l'inverse avec succès : **System OFF + réveil
  GRTC/GPIO + RAM retenue**, déjà validé fonctionnellement sur les 3
  unités déployées. Les deux architectures ne sont pas comparables terme
  à terme — ne pas chercher à « aligner » l'une sur l'autre, elles
  répondent à des contraintes matérielles différentes (GRTC disponible
  sur nRF54L, absent sur nRF52840).
- **ODR accéléromètre 1,6 Hz** : même IMU (LSM6DS3TR-C) sur les deux
  cartes, donc le principe (ODR basse suffisante si le sondage applicatif
  est plus lent que le rafraîchissement capteur) est probablement
  transposable — **mais à revalider par mesure sur cette carte**, pas à
  copier la valeur sans test, l'architecture System OFF change la façon
  dont l'IMU est sondé (réveil sur événement, pas sondage périodique
  continu comme sur la XIAO nRF52840 en System ON).
- **Pics ~20-25 ms génériques au SoC/Zephyr** : trouvaille spécifique au
  nRF52840 (confirmée via comparaison DK). Rien n'indique qu'un phénomène
  analogue existe ou non sur nRF54LM20A/GRTC — à chercher indépendamment
  si un plancher de consommation inexpliqué apparaît, pas à supposer
  présent ou absent par défaut.

## Point de vigilance supplémentaire déjà documenté pour cette carte

`PPK-Mesures-Transition.md` mentionne un retour DevZone Nordic distinct
(carte proche, nRF54L15) : consommation qui augmente fortement si la
tension d'alimentation appliquée est **inférieure à ~3,3V** (conception
du convertisseur DC-DC de cette gamme). C'est une dépendance à la tension
dans le sens **inverse** de ce qui a été observé aujourd'hui sur le
nRF52840 (où la moyenne restait plate entre 3,0 et 4,0V, seule
l'amplitude des pics changeait, avec un seuil situé entre 3,3 et 3,5V).
Les deux familles de SoC pourraient avoir des comportements de seuil de
tension complètement différents — **vérifier la tension réellement
appliquée pendant chaque mesure nRF54LM20A**, et ne pas supposer que la
conclusion « la tension n'affecte pas la moyenne » du nRF52840
s'applique ici.

## Pas de dépôt Git sur ce projet

Contrairement à `xiao_nrf52840_door_sensor` (dépôt GitHub dédié, commits/
push sur demande explicite), **`xiao_door_sensor` n'a pas de contrôle de
version** à la date de rédaction (`git status` : "not a git repository").
Si un suivi de version est souhaité pour cette campagne, le proposer
explicitement à l'utilisateur plutôt que de le supposer acquis.

## Ce que la nouvelle conversation devra faire en premier

1. Lire les trois documents listés en tête de ce fichier.
2. Vérifier l'état matériel réel avant de commencer (carte disponible
   pour le test, câblage PPK2 pas encore confirmé pour cette carte
   précise — `PPK-Mesures-Transition.md` § « Décisions à prendre en
   début de session » reste entièrement d'actualité, rien n'a été résolu
   sur ces points depuis sa rédaction).
3. Appliquer la règle **Ampere meter uniquement**, jamais Source Meter,
   pour les raisons cumulées (sécurité déjà documentée + précision
   confirmée aujourd'hui sur l'autre carte).
4. Une seule carte connectée à la fois, identification par lecture série
   directe uniquement.
