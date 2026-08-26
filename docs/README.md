# Documents de référence — XIAO nRF54LM20A / LSM6DS3TR-C

Datasheets sources primaires téléchargées localement le 2026-08-23 pour
servir de référence stable (éviter la re-recherche et le risque de
blocage anti-bot en cas de nouvelle consultation). Sources de l'architecture
System OFF / réveil matériel IMU documentée dans
`../xiao_nrf54lm20a_project_notes.md`.

## `nRF54LM20A_nRF54LM20B_Datasheet_v1.0.pdf`

- **Source officielle** : Nordic Semiconductor, doc n° 4539_001 v1.0 (mai 2026).
- **Récupéré via** : miroir Seeed —
  https://files.seeedstudio.com/wiki/XIAO_nRF54LM20A/getting_start/RES/nRF54LM20A_nRF54LM20B_Datasheet_v1.0.pdf
  (le PDF direct nordicsemi.com n'a pas été localisé ; le contenu est
  identique à `docs.nordicsemi.com/bundle/ps_nrf54lm20a/` — même
  numérotation de sections, copyright Nordic en pied de page — vérifié
  par lecture directe des pages 70 et 1253, voir ci-dessous).
- **114 pages** (page numérotée dans le doc = page PDF, ex. `-f 70 -l 70`).
- **Sections clés vérifiées par lecture directe** (`pdftotext -raw`,
  pas seulement citées par un agent) :
  - §5.2 « System OFF mode », p. 70 — sources de réveil, comportement
    au réveil (reset complet), précondition `RESET.RESETREAS` à
    vider avant l'entrée en System OFF.
  - §8.11 GRTC, p. 312-313 — domaine « always-on », bascule sur LFXO
    en System OFF, `tOFF2ON` = 37 µs typ.
  - §11.2.1.1 « Sleep », p. 1253 — table de consommation System OFF
    (`IOFF0`/`IOFF1`) et System ON idle (`IONIDLE0/1/2`).

## `LSM6DS3TR-C_datasheet_DocID030071_Rev3.pdf`

- **Source officielle** : STMicroelectronics, DocID030071 Rev 3.
- **Récupéré via** : miroir Adafruit (distributeur officiel du
  composant) — https://cdn-shop.adafruit.com/product-files/4503/4503_LSM6DS3TR-C_datasheet.pdf
  (le PDF direct st.com bloque la récupération automatisée — protection
  anti-bot Akamai, timeout systématique même avec en-têtes navigateur).
- **114/115 pages** selon la pagination interne du document.
- **Sections clés vérifiées par lecture directe** :
  - §4.2 « Electrical characteristics », Table 4, p. 24 — courants de
    consommation par mode (dont `LA_IddLM` = 9 µA typ., accéléromètre
    seul low-power @ODR=12.5 Hz).
  - §9.18 CTRL6_C (15h), p. 66 — bit `XL_HM_MODE` (bit 4).
  - §9.75 TAP_CFG (58h), p. 88 — bits `INTERRUPTS_ENABLE` (7),
    `INACT_EN[1:0]` (6:5), `SLOPE_FDS` (4), `TAP_X/Y/Z_EN` (3:1),
    `LIR` (0).
  - §9.78 WAKE_UP_THS (5Bh), p. 90.
  - §9.79 WAKE_UP_DUR (5Ch), p. 90.
  - §9.81 MD1_CFG (5Eh), p. 92 — bit `INT1_WU` (5).

**Note** : l'AN5130 (note d'application ST sur le wake-up interrupt) est
référencée sur st.com mais n'a pas pu être récupérée (même blocage
anti-bot, aucun miroir trouvé). Si besoin plus tard, retenter le
téléchargement à froid depuis une IP différente ou demander le PDF
directement à ST/un distributeur.

## `nPM1300_ProductSpecification_v1.1.pdf`

- **Source officielle** : Nordic Semiconductor, doc n° 4490_483 v1.1
  (2024-06-16). C'est le PMIC de la XIAO nRF54LM20A Sense (charge
  LiPo, régulateurs LDO/BUCK, gauge de batterie).
- **Récupéré via** : miroir MikroElektronika —
  https://download.mikroe.com/documents/datasheets/nPM1300_datasheet.pdf
  (le PDF complet n'a pas été localisé sur nordicsemi.com/Mouser/
  DigiKey — ces canaux n'hébergent qu'un "Product Brief" marketing
  incomplet ; le contenu du miroir est vérifié identique par lecture
  directe page 16, même identifiant `4490_483 v1.1` en pied de page).
- **Section clé vérifiée par lecture directe** :
  - §3.8 « System electrical specification », Table 4, p. 16 —
    `IQBAT` = 0,8 µA typ. (quiescent, sans charge BUCK, VBUS
    déconnecté), `IQSHIP` = 0,37 µA, `IQSHIPT` (Hibernate) = 0,5 µA.
- **Gap identifié** : pas de quiescent current documenté pour un LDO
  individuel à faible charge (Table 24 « LDO electrical
  specification », p. 73, ne couvre que courant de sortie max, VIN/VOUT,
  RDSON — rien sur l'IQ statique). Utilisé dans le budget énergétique
  600 mAh de `xiao_nrf54lm20a_project_notes.md` § « Budget énergétique »
  comme un plancher, pas une valeur complète.
- Pas de note d'application Nordic trouvée spécifiquement sur la
  configuration basse consommation du nPM1300 pour un capteur IoT sur
  batterie (recherchée, absente de la doc officielle à cette date).

## Comment relire ces PDF sans un lecteur graphique

```bash
# pdftotext (Poppler) est déjà disponible dans l'environnement de build
pdftotext -f <page_debut> -l <page_fin> -raw "<fichier>.pdf" -
```

`-raw` préserve l'ordre de lecture correct pour les tableaux denses
(les tests avec `-layout` cassent l'alignement colonnes sur les tables
de registres/consommation de ces deux documents).
