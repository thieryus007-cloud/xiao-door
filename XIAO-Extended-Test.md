Voici le fichier source complet en C et la configuration correspondante à intégrer dans votre projet nRF Connect SDK (Zephyr) pour tester l'émission en Publicité Étendue (Extended Advertising).
Ce code fusionne l'ensemble de vos données (Angles IMU, Batterie, Capteurs Binaires) et le nom de l'appareil dans une trame unique de 43 octets, ce qui dépasse la limite historique de 31 octets du mode Legacy.
## 1. Configuration du projet (prj.conf)
Ajoutez ces lignes à votre fichier de configuration pour activer l'Extended Advertising et les dépendances mathématiques :

#--- Activation Bluetooth et Publicité Étendue ---
CONFIG_BT=y
CONFIG_BT_BROADCASTER=y
CONFIG_BT_PERIPHERAL=n
CONFIG_BT_OBSERVER=n
CONFIG_BT_PRIVACY=n

# Activation spécifique BLE 5.0+ Extended
CONFIG_BT_EXT_ADV=y
CONFIG_BT_EXT_ADV_MAX_ADV_SET=1

#--- Identité et Nom ---
CONFIG_HWINFO=y
CONFIG_BT_DEVICE_NAME="SANT-EXT"
CONFIG_BT_DEVICE_NAME_DYNAMIC=n

#--- Support Mathématique (Calcul d'angles) ---
CONFIG_NEWLIB_LIBC=y
CONFIG_CMSIS_DSP=y

------------------------------
## 2. Code Source (main.c)
Ce code construit le payload BTHome v2 unique non chiffré (Device Info 0x44), l'associe au nom complet de l'appareil, puis diffuse le tout via l'API Extended de Zephyr.

#include <zephyr/kernel.h>#include <zephyr/bluetooth/bluetooth.h>#include <zephyr/drivers/hwinfo.h>#include <string.h>#include <stdint.h>#include <stdbool.h>
#define BTHOME_UUID_LO   0xD2#define BTHOME_UUID_HI   0xFC#define BTHOME_INFO_TRIG 0x44 /* v2, clair, trigger-based */
/* Object IDs BTHome v2 */#define OBJ_PACKET_ID    0x00#define OBJ_BATTERY      0x01#define OBJ_TEMPERATURE  0x02#define OBJ_VOLTAGE      0x0C#define OBJ_GENERIC_BOOL 0x0F#define OBJ_MOTION       0x21#define OBJ_TAMPER       0x2B#define OBJ_VIBRATION    0x2C#define OBJ_BUTTON       0x3A#define OBJ_ROTATION     0x3F
/* Structure pour accumuler les données du payload étendu */struct bthome_ext_buf {
    uint8_t data[60]; /* Capacité élargie pour accueillir toutes les mesures */
    uint8_t len;
};
static uint8_t bthome_pid = 0;static struct bt_le_ext_adv *adv_set;
/* Initialisation des en-têtes BTHome */static void bth_init(struct bthome_ext_buf *b)
{
    b->data[0] = BTHOME_UUID_LO;
    b->data[1] = BTHOME_UUID_HI;
    b->data[2] = BTHOME_INFO_TRIG;
    b->len = 3;
}
static void bth_u8(struct bthome_ext_buf *b, uint8_t id, uint8_t v)
{
    b->data[b->len++] = id;
    b->data[b->len++] = v;
}
static void bth_u16(struct bthome_ext_buf *b, uint8_t id, uint16_t v)
{
    b->data[b->len++] = id;
    b->data[b->len++] = (uint8_t)(v & 0xFF);
    b->data[b->len++] = (uint8_t)(v >> 8);
}
static void bth_s16(struct bthome_ext_buf *b, uint8_t id, int16_t v)
{
    bth_u16(b, id, (uint16_t)v);
}
/* Génération de l'adresse statique aléatoire stable basée sur le hardware */static int init_ble_identity(void)
{
    uint8_t devid[16];
    bt_addr_le_t addr = {.type = BT_ADDR_LE_RANDOM};
    ssize_t n = hwinfo_get_device_id(devid, sizeof(devid));
    
    if (n < 6) return -ENODEV;
    
    memcpy(addr.a.val, devid, 6);
    addr.a.val[5] |= 0xC0; /* Structure adresse statique aléatoire */
    return bt_id_create(&addr, NULL);
}
/* Configuration du set de publicité étendue */static int bthome_extended_setup(void)
{
    int err;
    /* Publicité étendue : non connectable, non scannable, PHY 1M primaire et secondaire */
    struct bt_le_adv_param param = BT_LE_ADV_PARAM_INIT(
        BT_LE_ADV_OPT_EXT_ADV | BT_LE_ADV_OPT_USE_IDENTITY,
        0x00A0, /* Intervalle 100 ms */
        0x00A0, 
        NULL
    );

    err = bt_le_ext_adv_create(&param, NULL, &adv_set);
    if (err) return err;
    return 0;
}
/* Émission du train d'advertising étendu (durée 700ms) */static int bthome_broadcast_extended(const struct bthome_ext_buf *b)
{
    int err;
    struct bt_data ad[3];
    size_t ad_len = 0;
    static const uint8_t flags = BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR;

    ad[ad_len++] = (struct bt_data)BT_DATA(BT_DATA_FLAGS, &flags, 1);
    ad[ad_len++] = (struct bt_data)BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1);
    ad[ad_len++] = (struct bt_data)BT_DATA(BT_DATA_SVC_DATA16, b->data, b->len);

    /* Attribution du payload étendu */
    err = bt_le_ext_adv_set_data(adv_set, ad, ad_len, NULL, 0);
    if (err) return err;

    /* Démarrage pour un nombre fini de paquets (7 événements à 100ms d'intervalle = 700ms) */
    struct bt_le_ext_adv_start_param start_param = {
        .timeout = 0,
        .num_events = 7
    };

    err = bt_le_ext_adv_start(adv_set, &start_param);
    if (err) return err;

    k_msleep(700);
    
    /* Arrêt explicite pour préserver l'énergie */
    return bt_le_ext_adv_stop(adv_set);
}
int main(void)
{
    int err;
    struct bthome_ext_buf test_buf;

    k_msleep(500); /* Attente de stabilisation électrique */

    err = init_ble_identity();
    if (err) return 0;

    err = bt_enable(NULL);
    if (err) return 0;

    err = bthome_extended_setup();
    if (err) return 0;

    while (1) {
        /* Construction du payload unique fusionné de test */
        bth_init(&test_buf);
        
        // 1. Métadonnées (3 octets)
        bth_u8(&test_buf, OBJ_PACKET_ID, bthome_pid);
        
        // 2. Santé / Batterie (10 octets)
        bth_u8(&test_buf, OBJ_BATTERY, 98);               // 98%
        bth_s16(&test_buf, OBJ_TEMPERATURE, 2250);        // 22.50°C
        bth_u16(&test_buf, OBJ_VOLTAGE, 4120);            // 4.120 V
        
        // 3. Capteurs binaires et bouton (8 octets)
        bth_u8(&test_buf, OBJ_GENERIC_BOOL, 1);           // Activité active
        bth_u8(&test_buf, OBJ_MOTION, 1);                 // Mouvement détecté
        bth_u8(&test_buf, OBJ_TAMPER, 0);                 // Pas de choc
        bth_u8(&test_buf, OBJ_BUTTON, 0x01);              // Événement clic simple
        
        // 4. Angles IMU calculés (9 octets)
        bth_s16(&test_buf, OBJ_ROTATION, 450);            // Pitch = +45.0°
        bth_s16(&test_buf, OBJ_ROTATION, -125);           // Roll = -12.5°
        bth_s16(&test_buf, OBJ_ROTATION, 900);            // Yaw = +90.0°

        /* Envoi de la trame étendue unique */
        bthome_broadcast_extended(&test_buf);

        /* Incrémentation du Packet ID pour le prochain cycle */
        bthome_pid++;

        /* Cadence de test accélérée à 10 secondes pour validation */
        k_msleep(10000);
    }
    return 0;
}

------------------------------
## 3. Protocole de test pas à pas

   1. Flash : Flashez un de vos modules Seeed XIAO avec ce code.
   2. Vérification Radio Mobile : Ouvrez l'application nRF Connect sur votre smartphone. Localisez l'appareil nommé SANT-EXT. L'application doit afficher explicitement la mention "Extended Advertising" ou "LE Extended Advertisements" et l'intégralité du payload brut doit être visible sous l'UUID 0xFCD2.
   3. Validation Récepteur : Approchez le module de votre récepteur de test (un ESP32-S3 configuré avec le proxy Bluetooth natif de Home Assistant, ou votre serveur HA muni d'un dongle USB BLE 5.0).
   4. Analyse HA : Rendez-vous dans Paramètres → Appareils et services → Bluetooth → Advertisement Monitor ou consultez les logs de l'intégration BTHome pour confirmer si l'adresse MAC unique est capturée en une seule fois.

Si vous lancez ce test, faites-moi savoir quelle méthode de réception vous utilisez (proxy natif ou dongle local) pour que nous analysions les retours de logs en cas de non-découverte.

