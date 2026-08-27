/*
 * XIAO nRF54LM20A Sense -- capteur de porte/fenêtre BTHome v2, profil L
 * (Evolution-XIAO-BLE.md §3, §4, §7). Trois trames partageant un compteur
 * Packet ID unique :
 *
 *   Trame A -- événement + orientation (pitch/roll/yaw, activité, mouvement,
 *   free-fall, double-tap, bouton). Émise sur événement, avec un heartbeat
 *   à 60 min en l'absence d'activité (§4.3).
 *
 *   Trame B -- santé (batterie %, température interne, tension), toutes les
 *   15 min ± jitter, porte aussi le nom de l'appareil (§3.2).
 *
 *   Trame C -- IMU brut (magnitudes accel/gyro), envoyée avec la trame A
 *   pour la mise au point d'un ouvrant (§3.3, variante recommandée).
 *
 * Free-fall et double-tap ne sont pas implémentés : le driver Zephyr
 * lsm6dsl n'expose pas ces événements matériels via l'API sensor_trigger
 * standard (les registres FREE_FALL/TAP_CFG existent sur la puce mais ne
 * sont pas configurés par ce firmware). Envoyés à 0 en attendant.
 * Le bouton physique de la carte (button0/sw0) est lu depuis le
 * 2026-08-25 -- état courant (1=appuyé) rapporté à chaque trame A, pas de
 * réveil System OFF dédié pour l'instant (prévu pour une session
 * ultérieure).
 * Le yaw est calculé depuis le 2026-08-25 par intégration temporelle du
 * gyroscope (gz × Δt à chaque lecture, cumulé dans `retained.yaw_dd`,
 * survit au System OFF) -- **pas de recalage anti-dérive pour l'instant**
 * (§7.7), la valeur dérive donc lentement dans le temps sans mouvement
 * réel. Recalage à prévoir pour une session ultérieure.
 * La charge batterie (0x16) n'est pas mesurée par ce firmware : le nPM1300
 * expose un statut de charge, mais son intégration n'est pas encore faite.
 *
 * --- Réveil matériel + System OFF (2026-08-23) ---
 * Voir C:\ncs\projects\Recherche-Reveil-Materiel-XIAO.md pour l'étude
 * complète (registres, sources, budget énergétique). Architecture
 * "hybride" retenue : le SoC dort en System OFF entre les événements ;
 * un réveil GPIO (interruption INT1 de l'IMU sur seuil de mouvement,
 * registres écrits en I2C brut -- WAKE_UP_THS/WAKE_UP_DUR/MD1_CFG/TAP_CFG,
 * hors API sensor_trigger standard) ou GRTC (échéance trame santé 15 min
 * ou heartbeat 60 min, stockées comme temps GRTC absolus en RAM retenue)
 * le relance. Une fois réveillé, la logique de détection de mouvement,
 * hystérésis d'angle, anti-rafale et retour au repos est **inchangée**
 * (sondage logiciel à 2s, cf. run_active_window()) -- seul l'état qui doit
 * survivre à un redémarrage complet (packet_id, derniers angles envoyés,
 * échéances GRTC) passe par retained_mem. L'historique anti-rafale
 * (frame_a_send_times[]) et l'état du delta de mouvement (prev) restent
 * volontairement en RAM non retenue : ils ne servent qu'au sein d'une
 * même fenêtre active, jamais entre deux sessions.
 *
 * Point de vigilance connu, pas mesuré : le tout premier échantillon
 * accéléromètre de chaque fenêtre active n'a pas de "prev" valide (comme
 * au boot aujourd'hui), donc un mouvement très bref (< ~2s, plus court que
 * l'intervalle de sondage) qui a réveillé le SoC via l'IMU mais est déjà
 * terminé au premier sondage logiciel peut ne produire qu'une trame de
 * repos sans trame de mouvement l'ayant précédée -- à surveiller lors des
 * tests, comportement différent du sondage continu 24/7 d'avant.
 *
 * Seuil de réveil matériel (WAKE_UP_THS) choisi par défaut : 1 LSb à pleine
 * échelle ±2g (valeur par défaut du driver, jamais changée par ce
 * firmware) = FS_XL/64 ≈ 31,25 mg, à comparer au seuil logiciel existant
 * MOTION_THRESHOLD_MS2 = 0,3 m/s² ≈ 30,6 mg -- ordre de grandeur choisi
 * pour rester proche du comportement actuel, **non calibré sur matériel
 * réel**. À ajuster si le réveil est trop/pas assez sensible en pratique.
 */

#include <errno.h>
#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/retained_mem.h>
#include <zephyr/drivers/timer/nrf_grtc_timer.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/pm/device.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <hal/nrf_gpio.h>

LOG_MODULE_REGISTER(xiao_door_sensor, LOG_LEVEL_INF);

/* DIAGNOSTIC TEMPORAIRE (2026-08-27), PAS UN BUILD DE PRODUCTION : coupe
 * bt_enable() et tout envoi de trame pour isoler la contribution du
 * contrôleur BLE/MPSL dans le plancher de courant élevé mesuré au PPK2
 * (~250-300 µA, six autres pistes déjà écartées -- voir
 * Transition-nRF54LM20A-Optimisation-Consommation.md § Objectif chiffré).
 * Test directement inspiré de la même vérification faite sur le projet
 * XIAO nRF52840 (CONFIG_BT=n / BT=y, pics identiques dans les deux cas
 * chez eux). À remettre à 0 après le test, quel qu'en soit le résultat --
 * ce mode casse la fonction réelle du capteur (aucune trame envoyée). */
#define DIAG_NO_RADIO_TEST 1
/* Idem, IMU cette fois (2026-08-27, sur demande explicite -- même méthode
 * que celle qui avait exclu les capteurs comme cause sur le projet frère
 * nRF52840) : coupe l'init/lecture/réveil matériel de l'IMU pour obtenir
 * une carte nue (SoC + réveil GRTC seul), la comparer au budget calculé
 * SoC seul (~1 µA), puis réintroduire IMU et BLE un par un. */
#define DIAG_NO_IMU_TEST 1
/* Phase 1 du plan de test 2026-08-27 : coupe tout après leds_off() --
 * aucun réveil (GPIO/GRTC) armé, aucune écriture d'erratum. Référence
 * absolue SoC nu. À remettre à 0 dès la phase 1 terminée. */
#define DIAG_PHASE1_BARE_POWEROFF 1

#define IMU_NODE DT_ALIAS(imu0)
#define RAD_TO_DEG 57.29577951308232f

#define POLL_INTERVAL_MS         2000
#define FRAME_A_HEARTBEAT_MS     (60 * 60 * 1000)   /* §4.3 */
#define FRAME_B_INTERVAL_MS      (15 * 60 * 1000)   /* §4.3 */
#define FRAME_B_JITTER_MS        30000              /* ± jitter, §4.3 */
#define MOTION_REPORT_MIN_GAP_MS 4000               /* §4.3, aligné §2.3c */
#define FRAME_A_MAX_PER_MIN      10                 /* §4.3 */
#define MOTION_THRESHOLD_MS2     0.3f
#define ANGLE_HYSTERESIS_DD      20                 /* 2,0° en dixièmes, §4.3 */
#define REST_FRAME_DELAY_MS      10000              /* §4.3 -- relevé de 15000 à
						      * 10000 le 2026-08-24, préférence
						      * utilisateur (délai observé
						      * réel ~10-12s avec la
						      * granularité de sondage 2s) */
#define REST_FRAME_MAX_WAIT_MS   30000              /* 2026-08-24 : filet de
						      * sécurité -- REST_FRAME_DELAY_MS
						      * ne redémarre qu'à partir du
						      * dernier "moving=true" réel ; de
						      * petites vibrations résiduelles
						      * répétées pouvaient repousser le
						      * retour au repos indéfiniment.
						      * Borne absolue depuis le tout
						      * premier mouvement de la fenêtre :
						      * garantit un retour dans HA. */
#define ADV_INT      0x00A0   /* 100 ms, unités de 0,625 ms (§4.2) */
#define ADV_BURST_MS 700      /* §4.2 : ~7 events sur 3 canaux primaires */

/* Fenêtre active bornée par prudence (cf. en-tête). Initialement un
 * plafond de 900 *itérations* (~30 min en supposant ~2s/itération), pensé
 * comme un cas extrême "ne devrait jamais arriver", réduit à 60 itérations
 * (~2 min) le 2026-08-24 -- mais des blocages réels de 14 à 27 minutes ont
 * persisté malgré ça. Hypothèse retenue : le plafond comptait des
 * *itérations*, pas du temps réel -- si `read_accel()` (I2C) se met à
 * bloquer/réessayer plus longtemps que les ~2s nominaux par cycle (bus en
 * défaut transitoire, déjà observé ce jour), le compteur de 60 itérations
 * est respecté mais la durée réelle ne l'est plus. **Correctif du
 * 2026-08-24 (2e itération) : le plafond est maintenant basé sur le temps
 * réel écoulé (k_uptime_get()), robuste peu importe la durée de chaque
 * cycle.** Voir Transition-2026-08-24-Soiree.md. `ACTIVE_WINDOW_MAX_ITERS`
 * conservé uniquement pour le diagnostic (aw_iters), n'est plus le critère
 * de sortie. */
#define ACTIVE_WINDOW_MAX_ITERS  60
#define ACTIVE_WINDOW_MAX_MS     (2 * 60 * 1000)

/* --- BTHome v2, Object IDs utilisés (§2.5/§7.5) --- */
#define BTHOME_UUID_LO      0xD2
#define BTHOME_UUID_HI      0xFC
#define BTHOME_INFO_TRIG    0x44   /* v2, clair, trigger-based -- toutes trames (§2.2) */

#define OBJ_PACKET_ID       0x00
#define OBJ_BATTERY         0x01
#define OBJ_TEMPERATURE     0x02
#define OBJ_VOLTAGE         0x0C
#define OBJ_GENERIC_BOOL    0x0F
#define OBJ_BATTERY_LOW     0x15
#define OBJ_BATTERY_CHARGE  0x16
#define OBJ_MOTION          0x21
#define OBJ_TAMPER          0x2B
#define OBJ_VIBRATION       0x2C
#define OBJ_BUTTON          0x3A
#define OBJ_ROTATION        0x3F
#define OBJ_ACCELERATION    0x51
#define OBJ_GYROSCOPE       0x52
#define OBJ_ACCEL_SIGNED    0x63

#if defined(DT_N_NODELABEL_power_en)
static const struct device *const power_en_dev = DEVICE_DT_GET(DT_NODELABEL(power_en));
#endif
#if defined(DT_N_NODELABEL_imu_vdd)
static const struct device *const imu_vdd_dev = DEVICE_DT_GET(DT_NODELABEL(imu_vdd));
#endif
static const struct device *const charger_dev = DEVICE_DT_GET(DT_NODELABEL(pmic_charger));

/* --- Identité BLE fixe (§7.2), inchangée --- */
static int set_fixed_ble_identity(void)
{
	bt_addr_le_t addr = { .type = BT_ADDR_LE_RANDOM };
	uint8_t hw_id[8];
	ssize_t len;

	len = hwinfo_get_device_id(hw_id, sizeof(hw_id));
	if (len < (ssize_t)sizeof(addr.a.val)) {
		LOG_WRN("hwinfo device id too short (%d), using stack-generated address", (int)len);
		return 0;
	}

	memcpy(addr.a.val, hw_id, sizeof(addr.a.val));
	addr.a.val[5] |= 0xC0;

	return bt_id_create(&addr, NULL);
}

/* --- Batterie : courbe LiPo non calibrée (inchangée) --- */
static uint8_t voltage_to_percent(int32_t mv)
{
	static const struct {
		int32_t mv;
		uint8_t pct;
	} curve[] = {
		{ 4200, 100 }, { 4000, 80 }, { 3800, 55 }, { 3700, 35 },
		{ 3500, 15 },  { 3400, 5 },  { 3000, 0 },
	};

	if (mv >= curve[0].mv) {
		return curve[0].pct;
	}
	if (mv <= curve[ARRAY_SIZE(curve) - 1].mv) {
		return curve[ARRAY_SIZE(curve) - 1].pct;
	}
	for (size_t i = 0; i < ARRAY_SIZE(curve) - 1; i++) {
		if (mv <= curve[i].mv && mv >= curve[i + 1].mv) {
			int32_t span_mv = curve[i].mv - curve[i + 1].mv;
			int32_t span_pct = curve[i].pct - curve[i + 1].pct;

			return curve[i + 1].pct +
			       (uint8_t)((mv - curve[i + 1].mv) * span_pct / span_mv);
		}
	}
	return 0;
}

static int read_battery(uint8_t *out_pct, uint16_t *out_mv)
{
	struct sensor_value v;
	int ret;

	if (!device_is_ready(charger_dev)) {
		return -ENODEV;
	}
	ret = sensor_sample_fetch_chan(charger_dev, SENSOR_CHAN_GAUGE_VOLTAGE);
	if (ret < 0) {
		return ret;
	}
	ret = sensor_channel_get(charger_dev, SENSOR_CHAN_GAUGE_VOLTAGE, &v);
	if (ret < 0) {
		return ret;
	}

	*out_mv = (uint16_t)(v.val1 * 1000 + v.val2 / 1000);
	*out_pct = voltage_to_percent(*out_mv);
	return 0;
}

static void leds_off(void)
{
	static const struct gpio_dt_spec leds[] = {
		GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios),
		GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios),
		GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios),
	};

	for (size_t i = 0; i < ARRAY_SIZE(leds); i++) {
		if (!device_is_ready(leds[i].port)) {
			continue;
		}
		gpio_pin_configure_dt(&leds[i], GPIO_OUTPUT_INACTIVE);
	}
}

/* Bouton physique (sw0/button0, gpio0.9, PULL_UP|ACTIVE_LOW -- deja cable
 * au niveau materiel, cf. devicetree
 * xiao_nrf54lm20a_nrf54lm20a-common.dtsi). 2026-08-25 : lecture d'etat
 * simple uniquement -- pas de reveil System OFF dedie pour l'instant
 * (prevu pour une session ulterieure). GPIO_DT_SPEC_GET/gpio_pin_get_dt
 * tiennent deja compte de la polarite ACTIVE_LOW : la valeur logique
 * retournee est 1 quand le bouton est appuye, quelle que soit la
 * polarite physique. */
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

static int init_button(void)
{
	if (!device_is_ready(button.port)) {
		return -ENODEV;
	}
	return gpio_pin_configure_dt(&button, GPIO_INPUT);
}

static uint8_t read_button_state(void)
{
	int val = gpio_pin_get_dt(&button);

	if (val < 0) {
		LOG_WRN("button read failed (%d)", val);
		return 0;
	}
	return (uint8_t)val;
}

static int enable_imu_power(void)
{
#if defined(DT_N_NODELABEL_power_en) || defined(DT_N_NODELABEL_imu_vdd)
	int ret;
#endif
#if defined(DT_N_NODELABEL_power_en)
	if (!device_is_ready(power_en_dev)) {
		return -ENODEV;
	}
	ret = regulator_enable(power_en_dev);
	if (ret < 0 && ret != -EALREADY) {
		return ret;
	}
#endif
#if defined(DT_N_NODELABEL_imu_vdd)
	if (!device_is_ready(imu_vdd_dev)) {
		return -ENODEV;
	}
	ret = regulator_enable(imu_vdd_dev);
	if (ret < 0 && ret != -EALREADY) {
		return ret;
	}
#endif
	k_sleep(K_MSEC(20));
	return 0;
}

static int init_imu(const struct device *dev)
{
	int ret;

	if (device_is_ready(dev)) {
		return 0;
	}
	ret = enable_imu_power();
	if (ret < 0) {
		return ret;
	}
	ret = device_init(dev);
	if (ret < 0 && ret != -EALREADY) {
		LOG_ERR("Failed to initialize %s: %d", dev->name, ret);
		return ret;
	}
	if (!device_is_ready(dev)) {
		return -ENODEV;
	}
	return 0;
}

/* Accéléromètre seul, doit tourner en continu -- que ce soit pour le
 * sondage logiciel ou pour armer le réveil matériel INT1, voir
 * configure_imu_wakeup(). Le gyroscope N'EST PAS activé ici -- voir
 * set_gyro_power(). Le laisser actif en continu (comme le faisait la
 * version précédente, qui appelait cet attr_set aussi sur
 * SENSOR_CHAN_GYRO_XYZ) coûte ~0,9 mA en continu, soit ~100x le courant
 * accéléromètre seul : sur une batterie 1500 mAh, la différence est de
 * l'ordre de quelques années d'autonomie contre quelques semaines.
 *
 * Tentative 2026-08-27 : ODR abaissé à 1,6 Hz (code ODR_XL spécial bas-
 * power du LSM6DS3TR-C) pour réduire le poste dominant du budget (9 µA à
 * 12,5 Hz, LA_IddLM, datasheet DocID030071 Rev 3, Table 4 p.24) -- REVENU
 * EN ARRIÈRE le même jour : mesure PPK2 montrant un plancher élevé et
 * soutenu (~572 µA de moyenne) au lieu d'un retour rapide au repos,
 * flashé en même temps que la désactivation du logging (deux changements
 * à la fois, erreur de méthode -- un seul changement à la fois désormais).
 * Hypothèse retenue, non encore confirmée : à 625 ms entre échantillons
 * (contre 80 ms à 12,5 Hz), le seuil de réveil matériel WAKE_UP_THS
 * (0x01, très sensible) peut se redéclencher en boucle sur la vibration
 * ambiante, empêchant la carte de rester en System OFF. Retour à 12,5 Hz
 * pour isoler cette variable avant de retenter 1,6 Hz avec un seuil moins
 * sensible ou une autre approche. Voir
 * Transition-nRF54LM20A-Optimisation-Consommation.md § « Objectif
 * chiffré » pour le suivi complet.
 */
static void set_accel_sampling_freq(const struct device *dev)
{
	struct sensor_value odr_attr = { .val1 = 12, .val2 = 500000 };

	sensor_attr_set(dev, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr_attr);
}

/* Active/coupe le gyroscope à la demande (ODR 12,5 Hz <-> 0 = power-down).
 * Délai avant lecture : corrigé le 2026-08-30 (bug préexistant, jamais
 * mesuré avant ce jour) -- le driver Zephyr lsm6dsl lit les registres de
 * sortie (OUTX_L_G...) directement, sans attendre le drapeau "donnée
 * prête" (voir lsm6dsl_sample_fetch_gyro, lsm6dsl.c). L'ancien délai
 * (80ms) correspond exactement à une période ODR à 12,5 Hz -- confirmé
 * insuffisant sur matériel réel (gyroscope systématiquement figé à la
 * même valeur quel que soit le mouvement, y compris sous rotation
 * vigoureuse). Ton (temps de démarrage) = 35 ms d'après la datasheet
 * ST DocID030071 Rev 3 Table 4 p.24 ; 35 + 80 (première période ODR)
 * ≈ 115 ms minimum avant un premier échantillon réel -- marge portée à
 * 200 ms (~2,5 périodes ODR) pour rester au-delà de ce minimum avec de
 * la marge, faute de datasheet donnant un temps combiné exact. */
#define GYRO_STARTUP_MS 200

static void set_gyro_power(const struct device *dev, bool on)
{
	struct sensor_value odr_attr = { .val1 = on ? 12 : 0, .val2 = on ? 500000 : 0 };

	sensor_attr_set(dev, SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr_attr);
	if (on) {
		k_msleep(GYRO_STARTUP_MS);
	}
}

/* --- Réveil matériel de l'IMU (WAKE_UP_THS/WAKE_UP_DUR/MD1_CFG/TAP_CFG,
 * §9.75-9.81 de la datasheet LSM6DS3TR-C DocID030071 Rev 3, vérifiés
 * directement le 2026-08-23) --- accès I2C indépendant du driver Zephyr
 * lsm6dsl : celui-ci n'utilise que l'API i2c_dt_spec standard sur le
 * même bus/adresse (lsm6dsl_i2c.c), donc un second i2c_dt_spec construit
 * ici ne rentre pas en conflit (transactions sérialisées par l'API I2C
 * Zephyr elle-même). Écrit une seule fois au cold boot -- l'IMU est
 * alimenté en continu par le PMIC (I2C, indépendant du System OFF du
 * SoC), ses registres ne sont jamais perdus tant que la carte reste sous
 * tension. */
static const struct i2c_dt_spec imu_i2c = {
	.bus = DEVICE_DT_GET(DT_BUS(IMU_NODE)),
	.addr = DT_REG_ADDR(IMU_NODE),
};

#define LSM6DS3TR_REG_TAP_CFG      0x58
#define LSM6DS3TR_MASK_INT_ENABLE  BIT(7)
#define LSM6DS3TR_REG_WAKE_UP_THS  0x5B
#define LSM6DS3TR_REG_WAKE_UP_DUR  0x5C
#define LSM6DS3TR_REG_MD1_CFG      0x5E
#define LSM6DS3TR_MASK_MD1_INT1_WU BIT(5)

/* Tentative 0x01 -> 0x04 (2026-08-27) ECARTEE le même jour : mesure PPK2
 * montrant des rafales périodiques très régulières (~3,5-4 s d'écart,
 * export CSV `ppk-20260826T192639.csv`) au lieu d'un bruit irrégulier --
 * incompatible avec l'hypothèse d'un redéclenchement par bruit capteur
 * (qui serait irrégulier, pas cadencé). La cause est ailleurs. Revenu à
 * 0x01. Durée : 0 (déclenchement dès le premier échantillon au-dessus du
 * seuil, voir en-tête du fichier pour la justification du choix, non
 * calibré sur matériel réel). */
#define IMU_WAKE_UP_THS_VALUE 0x01
#define IMU_WAKE_UP_DUR_VALUE 0x00

static int configure_imu_wakeup(void)
{
	int ret;

	if (!device_is_ready(imu_i2c.bus)) {
		return -ENODEV;
	}

	/* TAP_CFG bit7 : porte globale des interruptions "basiques"
	 * (6D/4D, free-fall, wake-up, tap, inactivité) -- prérequis
	 * documenté indépendant de MD1_CFG (§9.75). */
	ret = i2c_reg_update_byte_dt(&imu_i2c, LSM6DS3TR_REG_TAP_CFG,
				      LSM6DS3TR_MASK_INT_ENABLE, LSM6DS3TR_MASK_INT_ENABLE);
	if (ret < 0) {
		LOG_ERR("IMU wake-up: TAP_CFG write failed (%d)", ret);
		return ret;
	}

	ret = i2c_reg_write_byte_dt(&imu_i2c, LSM6DS3TR_REG_WAKE_UP_THS, IMU_WAKE_UP_THS_VALUE);
	if (ret < 0) {
		LOG_ERR("IMU wake-up: WAKE_UP_THS write failed (%d)", ret);
		return ret;
	}

	ret = i2c_reg_write_byte_dt(&imu_i2c, LSM6DS3TR_REG_WAKE_UP_DUR, IMU_WAKE_UP_DUR_VALUE);
	if (ret < 0) {
		LOG_ERR("IMU wake-up: WAKE_UP_DUR write failed (%d)", ret);
		return ret;
	}

	/* MD1_CFG bit5 : routage de l'événement wake-up vers INT1 (§9.81). */
	ret = i2c_reg_update_byte_dt(&imu_i2c, LSM6DS3TR_REG_MD1_CFG,
				      LSM6DS3TR_MASK_MD1_INT1_WU, LSM6DS3TR_MASK_MD1_INT1_WU);
	if (ret < 0) {
		LOG_ERR("IMU wake-up: MD1_CFG write failed (%d)", ret);
		return ret;
	}

	LOG_INF("IMU wake-up configuré (THS=0x%02x DUR=0x%02x)",
		IMU_WAKE_UP_THS_VALUE, IMU_WAKE_UP_DUR_VALUE);
	return 0;
}

/* Broche de réveil System OFF : même pin que l'IRQ de l'IMU
 * (irq-gpios sur le nœud lsm6ds3tr_c), lue ici indépendamment du driver
 * lsm6dsl -- CONFIG_LSM6DSL_TRIGGER n'est pas activé, donc pas de
 * callback GPIO enregistré par le driver sur cette broche. LIR (TAP_CFG
 * bit0) reste à sa valeur par défaut (0, non latché) : INT1 redescend
 * tout seul une fois l'accélération repassée sous le seuil, pas besoin
 * de lire/effacer WAKE_UP_SRC ici. Reconfigurée à chaque entrée en
 * System OFF (comme l'exemple officiel Zephyr system_off/src/main.c) --
 * l'état matériel PIN_CNF est documenté "retained" à travers le System
 * OFF (datasheet nRF54LM20A §5.8.9/§8.9.1), mais l'état logiciel Zephyr
 * (callback GPIOTE, etc.) ne l'est pas et doit être réarmé après chaque
 * redémarrage complet. */
static const struct gpio_dt_spec imu_int1 = GPIO_DT_SPEC_GET(IMU_NODE, irq_gpios);

static int arm_gpio_wake(void)
{
	int ret;

	ret = gpio_pin_configure_dt(&imu_int1, GPIO_INPUT);
	if (ret < 0) {
		return ret;
	}
	ret = gpio_pin_interrupt_configure_dt(&imu_int1, GPIO_INT_LEVEL_ACTIVE);
	if (ret < 0) {
		return ret;
	}

	/* Erratum nRF54LM20A [114] GPIO -- 2026-08-27, trouvé par recherche
	 * dans l'errata officiel Nordic (docs.nordicsemi.com,
	 * errata_nRF54LM20A_EngB, anomaly_20A_114) : le mode DETECT par
	 * défaut du GPIO peut faire consommer ~300 µA de plus qu'attendu en
	 * System ON all idle si le signal DETECT oscille rapidement (bas-
	 * haut-bas en moins de 0,5 µs) -- exactement notre cas d'usage
	 * (réveil sur broche, interruption de niveau actif sur INT1 IMU).
	 * Contournement officiel : passer le port en mode DETECT latché
	 * (LDETECT) plutôt que le mode par défaut. Corrèle avec le plancher
	 * de courant élevé mesuré au PPK2 (~250-400 µA en excès, du bon ordre
	 * de grandeur) -- ODR, seuil de réveil et mode régulateur déjà
	 * écartés comme causes (voir
	 * Transition-nRF54LM20A-Optimisation-Consommation.md § Objectif
	 * chiffré). */
	nrf_gpio_port_detect_latch_set(NRF_P0, true);

	return 0;
}

static uint16_t clamp_u16(float scaled)
{
	if (scaled < 0.0f) {
		scaled = 0.0f;
	}
	if (scaled > (float)UINT16_MAX) {
		scaled = (float)UINT16_MAX;
	}
	return (uint16_t)scaled;
}

static int16_t clamp_i16(float scaled)
{
	if (scaled < (float)INT16_MIN) {
		scaled = (float)INT16_MIN;
	}
	if (scaled > (float)INT16_MAX) {
		scaled = (float)INT16_MAX;
	}
	return (int16_t)scaled;
}

static int32_t clamp_i32(double scaled)
{
	if (scaled < (double)INT32_MIN) {
		scaled = (double)INT32_MIN;
	}
	if (scaled > (double)INT32_MAX) {
		scaled = (double)INT32_MAX;
	}
	return (int32_t)scaled;
}

/* --- État retenu à travers le System OFF (retained_mem, CRC-validé comme
 * l'exemple officiel Zephyr system_off/src/retained.c) ---
 * Volontairement minimal : packet_id, derniers angles envoyés (pour
 * l'hystérésis) et les deux échéances GRTC absolues (trame santé,
 * heartbeat). Le reste de l'état (anti-rafale, delta de mouvement) est
 * local à une session, voir en-tête du fichier. GRTC : résolution 1 µs
 * documentée (datasheet nRF54LM20A §8.11, SYSCOUNTER 52 bits) -- ticks et
 * microsecondes traités comme équivalents dans tout ce fichier.
 * Déclarée ici (avant send_frame_a/send_frame_c, qui lisent/écrivent
 * retained.yaw_dd) plutôt que juste avant retained_load()/retained_save()
 * -- déplacée le 2026-08-25 pour cette raison. */
struct retained_state {
	uint8_t  bthome_pid;
	int16_t  last_sent_pitch_dd;
	int16_t  last_sent_roll_dd;
	int16_t  yaw_dd;             /* 2026-08-25 : integration gyroscopique
				       * cumulee (dixiemes de degre) -- pas de
				       * recalage anti-derive pour l'instant,
				       * voir § "Preparation -- yaw" des notes
				       * projet. */
	uint64_t next_health_us;     /* prochaine trame B, temps GRTC absolu */
	uint64_t next_heartbeat_us;  /* prochain heartbeat trame A, idem */
	uint32_t crc;
};

static struct retained_state retained;

struct imu_sample {
	float ax, ay, az;
	bool valid;
};

static int read_accel(const struct device *dev, struct imu_sample *out)
{
	struct sensor_value x, y, z;
	int ret;

	ret = sensor_sample_fetch_chan(dev, SENSOR_CHAN_ACCEL_XYZ);
	if (ret < 0) {
		return ret;
	}
	sensor_channel_get(dev, SENSOR_CHAN_ACCEL_X, &x);
	sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Y, &y);
	sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Z, &z);
	out->ax = sensor_value_to_float(&x);
	out->ay = sensor_value_to_float(&y);
	out->az = sensor_value_to_float(&z);
	out->valid = true;

	return 0;
}

/* Température interne : suit l'ODR de l'accéléromètre (toujours actif),
 * ne nécessite pas d'activer le gyroscope. */
static int read_die_temp(const struct device *dev, int16_t *temp_cc)
{
	struct sensor_value t;

	if (sensor_sample_fetch_chan(dev, SENSOR_CHAN_DIE_TEMP) == 0 &&
	    sensor_channel_get(dev, SENSOR_CHAN_DIE_TEMP, &t) == 0) {
		*temp_cc = clamp_i16(sensor_value_to_float(&t) * 100.0f);
		return 0;
	}
	*temp_cc = 0;
	return -EIO;
}

/* Gyroscope : activé juste avant la lecture, coupé juste après (voir
 * set_gyro_power) -- lu seulement au moment d'émettre un rapport, jamais
 * en continu. */
static int read_gyro_burst(const struct device *dev, float *gx, float *gy, float *gz)
{
	struct sensor_value x, y, z;
	int ret;

	set_gyro_power(dev, true);

	ret = sensor_sample_fetch_chan(dev, SENSOR_CHAN_GYRO_XYZ);
	if (ret < 0) {
		set_gyro_power(dev, false);
		return ret;
	}
	sensor_channel_get(dev, SENSOR_CHAN_GYRO_X, &x);
	sensor_channel_get(dev, SENSOR_CHAN_GYRO_Y, &y);
	sensor_channel_get(dev, SENSOR_CHAN_GYRO_Z, &z);
	*gx = sensor_value_to_float(&x) * RAD_TO_DEG;
	*gy = sensor_value_to_float(&y) * RAD_TO_DEG;
	*gz = sensor_value_to_float(&z) * RAD_TO_DEG;

	set_gyro_power(dev, false);
	return 0;
}

static bool motion_detected(const struct imu_sample *prev, const struct imu_sample *cur)
{
	float dx, dy, dz, delta_mag;

	if (!prev->valid) {
		return false;
	}
	dx = cur->ax - prev->ax;
	dy = cur->ay - prev->ay;
	dz = cur->az - prev->az;
	delta_mag = sqrtf(dx * dx + dy * dy + dz * dz);

	return delta_mag > MOTION_THRESHOLD_MS2;
}

/* Pitch/roll par projection du vecteur gravité (§7.7). Yaw non calculé --
 * nécessiterait une intégration gyroscopique dans la durée avec recalage
 * à l'inactivité, non implémenté sur ce firmware. */
static void accel_to_pitch_roll(float ax, float ay, float az,
				  int16_t *pitch_dd, int16_t *roll_dd)
{
	float pitch = atan2f(-ax, sqrtf(ay * ay + az * az)) * RAD_TO_DEG;
	float roll  = atan2f(ay, az) * RAD_TO_DEG;

	*pitch_dd = clamp_i16(pitch * 10.0f);
	*roll_dd  = clamp_i16(roll * 10.0f);
}

/* --- Trame A : événement + orientation (§3.1) --- */
static uint8_t frame_a[] = {
	BTHOME_UUID_LO, BTHOME_UUID_HI, BTHOME_INFO_TRIG,
	OBJ_PACKET_ID, 0x00,
	OBJ_GENERIC_BOOL, 0x00,
	OBJ_MOTION, 0x00,
	OBJ_TAMPER, 0x00,
	OBJ_VIBRATION, 0x00,
	OBJ_BUTTON, 0x00,
	OBJ_ROTATION, 0x00, 0x00,
	OBJ_ROTATION, 0x00, 0x00,
	OBJ_ROTATION, 0x00, 0x00,
};
#define A_OFF_PACKET_ID  4
#define A_OFF_ACTIVITY   6
#define A_OFF_MOTION     8
#define A_OFF_TAMPER     10
#define A_OFF_VIBRATION  12
#define A_OFF_BUTTON     14
#define A_OFF_PITCH      16
#define A_OFF_ROLL       19
#define A_OFF_YAW        22

/* --- Trame B : santé périodique + nom (§3.2) --- */
/* OBJ_BATTERY_CHARGE (0x16) retiré le 2026-08-30 : la trame dépassait de
 * 2 octets la limite de 31 octets de l'advertising BLE legacy (calcul
 * vérifié dans zephyr/subsys/bluetooth/host/adv.c:495,
 * BT_GAP_ADV_MAX_ADV_DATA_LEN) -- bug préexistant, indépendant du
 * System OFF, découvert en test réel sur l'unité #3. Le champ n'était de
 * toute façon jamais lu par ce firmware (toujours envoyé à 0, voir
 * en-tête de fichier) -- HA perd l'entité "Battery charging", déjà
 * documentée comme non fonctionnelle dans
 * xiao_nrf54lm20a_project_notes.md. */
static uint8_t frame_b[] = {
	BTHOME_UUID_LO, BTHOME_UUID_HI, BTHOME_INFO_TRIG,
	OBJ_PACKET_ID, 0x00,
	OBJ_BATTERY, 0x00,
	OBJ_TEMPERATURE, 0x00, 0x00,
	OBJ_VOLTAGE, 0x00, 0x00,
	OBJ_BATTERY_LOW, 0x00,
};
#define B_OFF_PACKET_ID 4
#define B_OFF_BATTERY   6
#define B_OFF_TEMP      8
#define B_OFF_VOLTAGE   11
#define B_OFF_BATT_LOW  14

/* --- Trame C : IMU brut, magnitudes + accélération signée par axe
 * (§3.3). Ajout des 3 axes signés le 2026-08-30 pour retrouver les 15
 * entités HA d'origine (§0x63, cf. table de référence BTHome du projet :
 * signé, 4 octets, facteur 0.000001 m/s²). ⚠️ Budget serré : cette trame
 * fait maintenant exactement 31 octets sur l'air (calcul vérifié contre
 * zephyr/subsys/bluetooth/host/adv.c:495, BT_GAP_ADV_MAX_ADV_DATA_LEN) --
 * plus aucune marge, toute mesure supplémentaire ajoutée ici fera
 * échouer l'advertising comme la trame B l'a fait avant correction. */
static uint8_t frame_c[] = {
	BTHOME_UUID_LO, BTHOME_UUID_HI, BTHOME_INFO_TRIG,
	OBJ_PACKET_ID, 0x00,
	OBJ_ACCELERATION, 0x00, 0x00,
	OBJ_GYROSCOPE, 0x00, 0x00,
	OBJ_ACCEL_SIGNED, 0x00, 0x00, 0x00, 0x00,
	OBJ_ACCEL_SIGNED, 0x00, 0x00, 0x00, 0x00,
	OBJ_ACCEL_SIGNED, 0x00, 0x00, 0x00, 0x00,
};
#define C_OFF_PACKET_ID  4
#define C_OFF_ACCEL_MAG  6
/* Bug corrigé le 2026-08-30 (préexistant, sans lien avec le System OFF) :
 * OBJ_GYROSCOPE est à l'index 8 ; sa valeur 2 octets doit donc être écrite
 * aux index 9-10, pas 8-9 -- l'ancienne valeur (8) écrasait l'octet d'ID
 * de l'objet lui-même, corrompant la trame (Gyroscope jamais décodé côté
 * HA, et l'octet corrompu était parfois réinterprété comme un ID BTHome
 * différent -- ex. entité fantôme "Weight" observée en test). */
#define C_OFF_GYRO_MAG   9
#define C_OFF_ACCEL_X    12
#define C_OFF_ACCEL_Y    17
#define C_OFF_ACCEL_Z    22

static struct bt_data frame_a_ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_SVC_DATA16, frame_a, sizeof(frame_a)),
};
static struct bt_data frame_b_ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
	BT_DATA(BT_DATA_SVC_DATA16, frame_b, sizeof(frame_b)),
};
static struct bt_data frame_c_ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_SVC_DATA16, frame_c, sizeof(frame_c)),
};

static uint8_t bthome_pid;

static void advertise_burst(struct bt_data *data, size_t data_len)
{
	int err;

	err = bt_le_adv_start(BT_LE_ADV_NCONN_IDENTITY, data, data_len, NULL, 0);
	if (err) {
		LOG_ERR("Advertising failed to start (err %d)", err);
		return;
	}
	k_msleep(ADV_BURST_MS);
	err = bt_le_adv_stop();
	if (err) {
		LOG_ERR("Advertising failed to stop (err %d)", err);
	}
}

/* Fenêtre glissante simple : au plus FRAME_A_MAX_PER_MIN trames A envoyées
 * sur les 60 dernières secondes (§4.3). Volontairement non retenue entre
 * deux sessions -- voir en-tête du fichier. */
static int64_t frame_a_send_times[FRAME_A_MAX_PER_MIN];
static size_t frame_a_send_idx;

static bool frame_a_rate_limited(int64_t now)
{
	int64_t oldest = frame_a_send_times[frame_a_send_idx];

	return (oldest != 0) && (now - oldest) < 60000;
}

static void frame_a_record_send(int64_t now)
{
	frame_a_send_times[frame_a_send_idx] = now;
	frame_a_send_idx = (frame_a_send_idx + 1) % FRAME_A_MAX_PER_MIN;
}

static void send_frame_a(const struct imu_sample *accel, bool motion, bool activity,
			   const char *reason)
{
	int16_t pitch_dd, roll_dd;

	accel_to_pitch_roll(accel->ax, accel->ay, accel->az, &pitch_dd, &roll_dd);

	bthome_pid++;
	frame_a[A_OFF_PACKET_ID] = bthome_pid;
	frame_a[A_OFF_ACTIVITY] = activity ? 1 : 0;
	frame_a[A_OFF_MOTION] = motion ? 1 : 0;
	frame_a[A_OFF_TAMPER] = 0;    /* free-fall : non implémenté (voir en-tête) */
	frame_a[A_OFF_VIBRATION] = 0; /* double-tap : non implémenté */
	frame_a[A_OFF_BUTTON] = read_button_state(); /* 2026-08-25 : lecture d'etat simple */
	sys_put_le16((uint16_t)pitch_dd, &frame_a[A_OFF_PITCH]);
	sys_put_le16((uint16_t)roll_dd, &frame_a[A_OFF_ROLL]);
	/* 2026-08-25 : retained.yaw_dd doit deja etre a jour ici -- appeler
	 * read_gyro_and_integrate_yaw() AVANT send_frame_a() dans
	 * l'appelant, jamais apres (bug de la tentative precedente : le yaw
	 * envoye avait toujours un cycle de retard). */
	sys_put_le16((uint16_t)retained.yaw_dd, &frame_a[A_OFF_YAW]);

	LOG_INF("trame A (%s) #%u: pitch=%d.%d roll=%d.%d motion=%d activity=%d",
		reason, bthome_pid, pitch_dd / 10, abs(pitch_dd % 10),
		roll_dd / 10, abs(roll_dd % 10), motion, activity);
	LOG_HEXDUMP_INF(frame_a, sizeof(frame_a), "trame A octets bruts");

	advertise_burst(frame_a_ad, ARRAY_SIZE(frame_a_ad));
}

/* Horodatage de la derniere integration yaw -- volontairement en RAM non
 * retenue (remise a 0 a chaque boot) : la valeur integree elle-meme
 * (retained.yaw_dd) survit au System OFF, mais le delta de temps entre
 * deux lectures gyro ne doit etre calcule qu'au sein d'une meme session,
 * jamais entre deux redemarrages (le temps "ecoule" pendant un System
 * OFF n'a pas de sens ici -- pas de rotation a integrer pendant qu'on
 * dort). */
static int64_t last_yaw_update_uptime;

/* 2026-08-25 : separe du reste de la construction de la trame C pour que
 * l'appelant puisse integrer le yaw (et donc mettre a jour retained.
 * yaw_dd) AVANT d'appeler send_frame_a() -- qui lit retained.yaw_dd --
 * tout en ne lisant le gyroscope qu'une seule fois par cycle (cout
 * energetique, voir § "Autonomie"). */
static int read_gyro_and_integrate_yaw(const struct device *imu, float *gx, float *gy,
					 float *gz, uint16_t *gyro_mag)
{
	int gyro_ret = read_gyro_burst(imu, gx, gy, gz);

	*gyro_mag = 0;
	if (gyro_ret == 0) {
		*gyro_mag = clamp_u16(sqrtf(*gx * *gx + *gy * *gy + *gz * *gz) * 1000.0f);

		/* Yaw : integration temporelle du taux de rotation gz (°/s,
		 * deja converti par read_gyro_burst) -- pas de recalage
		 * anti-derive pour l'instant, la valeur derive donc lentement
		 * dans le temps. */
		int64_t now = k_uptime_get();

		if (last_yaw_update_uptime != 0) {
			float dt_s = (float)(now - last_yaw_update_uptime) / 1000.0f;

			retained.yaw_dd = clamp_i16((float)retained.yaw_dd + *gz * dt_s * 10.0f);
		}
		last_yaw_update_uptime = now;
	}
	return gyro_ret;
}

static void send_frame_c(const struct imu_sample *accel, int gyro_ret, float gx, float gy,
			   float gz, uint16_t gyro_mag)
{
	uint16_t accel_mag;

	/* Diagnostic temporaire (2026-08-30) : trame C n'avait aucune trace
	 * jusqu'ici, contrairement à A/B -- utile pour distinguer "lecture
	 * gyroscope en échec" de "lecture correcte mais proche de zéro". */
	LOG_INF("trame C: gyro_ret=%d gx=%d.%03d gy=%d.%03d gz=%d.%03d gyro_mag=%u accel_mag_x1000=%u",
		gyro_ret,
		(int)gx, (int)fabsf((gx - (int)gx) * 1000),
		(int)gy, (int)fabsf((gy - (int)gy) * 1000),
		(int)gz, (int)fabsf((gz - (int)gz) * 1000),
		gyro_mag,
		clamp_u16(sqrtf(accel->ax * accel->ax + accel->ay * accel->ay +
				accel->az * accel->az) * 1000.0f));
	accel_mag = clamp_u16(
		sqrtf(accel->ax * accel->ax + accel->ay * accel->ay + accel->az * accel->az) *
		1000.0f);

	/* Packet ID propre, distinct de celui de la trame A qui la précède de
	 * ~1,5s : deux trames différentes portant le même id à moins de 4s
	 * sont indiscernables d'une retransmission pour le déduplicateur
	 * BTHome (§2.3c) -- la seconde serait silencieusement écartée par HA. */
	bthome_pid++;
	frame_c[C_OFF_PACKET_ID] = bthome_pid;
	sys_put_le16(accel_mag, &frame_c[C_OFF_ACCEL_MAG]);
	sys_put_le16(gyro_mag, &frame_c[C_OFF_GYRO_MAG]);
	sys_put_le32((uint32_t)clamp_i32((double)accel->ax * 1000000.0), &frame_c[C_OFF_ACCEL_X]);
	sys_put_le32((uint32_t)clamp_i32((double)accel->ay * 1000000.0), &frame_c[C_OFF_ACCEL_Y]);
	sys_put_le32((uint32_t)clamp_i32((double)accel->az * 1000000.0), &frame_c[C_OFF_ACCEL_Z]);

	advertise_burst(frame_c_ad, ARRAY_SIZE(frame_c_ad));
}

static void send_frame_b(const struct device *imu)
{
	uint8_t battery_pct = 0;
	uint16_t battery_mv = 0;
	int16_t temp_cc = 0;

	if (read_battery(&battery_pct, &battery_mv) < 0) {
		LOG_WRN("battery read failed, sending frame B without a fresh value");
	}
	if (read_die_temp(imu, &temp_cc) < 0) {
		LOG_WRN("temperature read failed");
	}

	bthome_pid++;
	frame_b[B_OFF_PACKET_ID] = bthome_pid;
	frame_b[B_OFF_BATTERY] = battery_pct;
	sys_put_le16((uint16_t)temp_cc, &frame_b[B_OFF_TEMP]);
	sys_put_le16(battery_mv, &frame_b[B_OFF_VOLTAGE]);
	frame_b[B_OFF_BATT_LOW] = battery_pct < 15 ? 1 : 0;

	LOG_INF("trame B #%u: battery=%u%% (%u mV) temp=%d.%02d°C",
		bthome_pid, battery_pct, battery_mv, temp_cc / 100, abs(temp_cc % 100));

	advertise_burst(frame_b_ad, ARRAY_SIZE(frame_b_ad));
}

#if DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(retainedmemdevice))
static const struct device *const retained_dev = DEVICE_DT_GET(DT_ALIAS(retainedmemdevice));
#else
#error "retained_mem region not defined -- voir l'overlay de carte"
#endif

#define RETAINED_CRC_OFFSET offsetof(struct retained_state, crc)

static bool retained_load(void)
{
	uint32_t crc;

	if (retained_mem_read(retained_dev, 0, (uint8_t *)&retained, sizeof(retained)) != 0) {
		return false;
	}
	crc = crc32_ieee((const uint8_t *)&retained, RETAINED_CRC_OFFSET);
	return crc == retained.crc;
}

static void retained_save(void)
{
	retained.crc = crc32_ieee((const uint8_t *)&retained, RETAINED_CRC_OFFSET);
	retained_mem_write(retained_dev, 0, (uint8_t *)&retained, sizeof(retained));
}

/* --- Historique de diagnostic (2026-08-24, silence radio prolongé sur les
 * unités déployées -- voir Diagnostic-Silence-Unites-Transition.md) : anneau
 * des derniers cycles dans la même région retenue, à l'offset suivant
 * struct retained_state. Survit au System OFF comme le reste de cette
 * région -- PAS à une coupure d'alimentation complète (SRAM retenue, pas
 * RRAM ; voir commentaire au-dessus de struct retained_state). Chaque
 * entrée capture pourquoi ce boot a eu lieu (reset_cause/cold_boot/
 * gpio_wake) et ce qui a été (tenté d')armé pour le cycle suivant
 * (wake_in_us, codes retour de arm_gpio_wake()/z_nrf_grtc_wakeup_prepare())
 * -- affiché en entier à chaque boot (diag_log_dump()) pour rester lisible
 * même après une longue période sans capture série. */
#define DIAG_LOG_ENTRIES 20
#define DIAG_LOG_OFFSET  sizeof(struct retained_state)

/* Raison de sortie de run_active_window() (2026-08-24, diagnostic blocage
 * "Détecté" persistant dans HA) -- voir run_active_window() pour le détail
 * de chaque cas. */
enum aw_exit_reason {
	AW_EXIT_NO_MOTION         = 0, /* jamais was_moving pendant la fenêtre */
	AW_EXIT_STATE_SENT        = 1, /* trame d'état actuel envoyée normalement (délai/max-wait) */
	AW_EXIT_CAP_STATE_FORCED  = 2, /* plafond ACTIVE_WINDOW_MAX_ITERS atteint, trame d'état actuel forcée avant sortie */
	AW_EXIT_CAP_NO_STATE      = 3, /* plafond atteint, was_moving déjà false (trame pas nécessaire) */
};

struct diag_entry {
	uint32_t seq;
	uint32_t reset_cause;
	uint64_t wake_in_us;
	int32_t  ret_gpio_wake;
	int32_t  ret_grtc_prepare;
	uint8_t  cold_boot;
	uint8_t  gpio_wake;
	uint8_t  aw_exit_reason; /* enum aw_exit_reason, 0xFF si fenêtre active non entrée */
	uint8_t  _pad;
	uint32_t aw_iters;
};

struct diag_log {
	uint32_t next_idx;
	uint32_t count;
	struct diag_entry entries[DIAG_LOG_ENTRIES];
	uint32_t crc;
};

static struct diag_log diag;

#define DIAG_CRC_OFFSET offsetof(struct diag_log, crc)

static bool diag_log_load(void)
{
	uint32_t crc;

	if (retained_mem_read(retained_dev, DIAG_LOG_OFFSET, (uint8_t *)&diag, sizeof(diag)) != 0) {
		return false;
	}
	crc = crc32_ieee((const uint8_t *)&diag, DIAG_CRC_OFFSET);
	return crc == diag.crc;
}

static void diag_log_save(void)
{
	diag.crc = crc32_ieee((const uint8_t *)&diag, DIAG_CRC_OFFSET);
	retained_mem_write(retained_dev, DIAG_LOG_OFFSET, (uint8_t *)&diag, sizeof(diag));
}

static void diag_log_append(uint32_t reset_cause, bool cold_boot, bool gpio_wake,
			     uint64_t wake_in_us, int ret_gpio_wake, int ret_grtc_prepare,
			     uint8_t aw_exit_reason, uint32_t aw_iters)
{
	struct diag_entry *e = &diag.entries[diag.next_idx];

	e->seq = diag.count++;
	e->reset_cause = reset_cause;
	e->cold_boot = cold_boot;
	e->gpio_wake = gpio_wake;
	e->wake_in_us = wake_in_us;
	e->ret_gpio_wake = ret_gpio_wake;
	e->ret_grtc_prepare = ret_grtc_prepare;
	e->aw_exit_reason = aw_exit_reason;
	e->aw_iters = aw_iters;
	diag.next_idx = (diag.next_idx + 1) % DIAG_LOG_ENTRIES;
	diag_log_save();
}

/* Ordre chronologique, le plus ancien encore en mémoire en premier. */
static void diag_log_dump(void)
{
	if (diag.count == 0) {
		LOG_INF("Historique diag : vide (rien depuis la dernière RAM retenue valide)");
		return;
	}

	uint32_t shown = MIN(diag.count, DIAG_LOG_ENTRIES);
	uint32_t start = (diag.count <= DIAG_LOG_ENTRIES) ? 0 : diag.next_idx;

	LOG_INF("Historique diag (%u cycle(s) au total, %u conserve(s)):", diag.count, shown);
	for (uint32_t i = 0; i < shown; i++) {
		const struct diag_entry *e = &diag.entries[(start + i) % DIAG_LOG_ENTRIES];

		LOG_INF("  #%u reset_cause=0x%08x cold_boot=%d gpio_wake=%d -> wake_in_us=%llu "
			"ret_gpio=%d ret_grtc=%d aw_exit=%u aw_iters=%u",
			e->seq, e->reset_cause, e->cold_boot, e->gpio_wake,
			e->wake_in_us, e->ret_gpio_wake, e->ret_grtc_prepare,
			e->aw_exit_reason, e->aw_iters);
	}
}

/* Prochaine échéance trame B avec un jitter aléatoire ±30s (§4.3),
 * désynchronise les nœuds redémarrés en même temps après une coupure. */
static uint64_t next_health_deadline(uint64_t from_us)
{
	int32_t jitter = (int32_t)(sys_rand32_get() % (2 * FRAME_B_JITTER_MS)) - FRAME_B_JITTER_MS;

	return from_us + ((uint64_t)FRAME_B_INTERVAL_MS * 1000) + ((int64_t)jitter * 1000);
}

static uint64_t next_heartbeat_deadline(uint64_t from_us)
{
	return from_us + ((uint64_t)FRAME_A_HEARTBEAT_MS * 1000);
}

/* 2026-08-24 : la trame qui rapporte l'état actuel (plus de mouvement)
 * après un mouvement est la seule dont la perte laisse HA figé dans un
 * état perimé pendant longtemps (jusqu'au prochain mouvement réel ou
 * jusqu'au prochain heartbeat périodique, jusqu'à 60 min) -- contrairement
 * à la perte d'une trame d'événement, qui ne fait que retarder un rapport
 * de quelques secondes. BLE non connecté (advertising) n'a pas d'accusé de
 * réception ni de retransmission automatique : une seule rafale de 700ms
 * peut occasionnellement être manquée par le scanner, même à courte
 * distance (collision radio, congestion, timing du scan -- pas une
 * question de portée, voir CLAUDE.md § « Chercher la cause dans le code,
 * jamais dans l'environnement »). On répète donc cette trame précise
 * plusieurs fois, chacune avec un packet_id different (incrémenté par
 * send_frame_a() à chaque appel), pour rendre sa perte totale hautement
 * improbable. */
#define FINAL_STATE_REPEATS       3
#define FINAL_STATE_REPEAT_GAP_MS 3000

static void send_final_state_frame(const struct imu_sample *cur, const char *reason)
{
	for (int i = 0; i < FINAL_STATE_REPEATS; i++) {
		if (i > 0) {
			k_msleep(FINAL_STATE_REPEAT_GAP_MS);
		}
		send_frame_a(cur, false, false, reason);
		frame_a_record_send(k_uptime_get());
	}
}

/* --- Fenêtre active : sondage logiciel 2s, logique de détection de
 * mouvement/repos/hystérésis d'angle **inchangée** par rapport à la
 * version précédente (voir en-tête du fichier). Tourne tant qu'il y a de
 * l'activité en cours ou un heartbeat en attente, puis rend la main pour
 * que main() puisse réarmer les réveils et repasser en System OFF. */
static void run_active_window(const struct device *imu, bool heartbeat_pending,
				bool triggered_by_motion, uint8_t *out_exit_reason,
				uint32_t *out_iters)
{
	struct imu_sample prev = { .valid = false };
	struct imu_sample cur = { .valid = false };
	bool was_moving = triggered_by_motion;
	int64_t rest_since = 0;
	int64_t first_moving_uptime = triggered_by_motion ? k_uptime_get() : 0;
	bool rest_frame_pending = false;
	bool final_state_sent = false;
	int64_t last_frame_a_uptime = -FRAME_A_HEARTBEAT_MS;
	int64_t window_start = k_uptime_get();
	int iters = 0;

	while (1) {
		if (iters > 0) {
			k_msleep(POLL_INTERVAL_MS);
		}
		iters++;
		if ((k_uptime_get() - window_start) > ACTIVE_WINDOW_MAX_MS) {
			/* 2026-08-24 : ce plafond sortait sans jamais envoyer de
			 * trame rapportant l'état actuel (plus de mouvement) si
			 * was_moving était encore vrai -- HA restait figé sur
			 * "Détecté" jusqu'au prochain événement réel. On force
			 * l'envoi ici avec le dernier échantillon connu avant de
			 * sortir. Basé sur le temps ecoule reel (pas le nombre
			 * d'iterations, cf. commentaire ACTIVE_WINDOW_MAX_MS) --
			 * robuste meme si un cycle dure plus longtemps que prevu. */
			if (was_moving) {
				send_final_state_frame(&cur, "etat actuel (plafond)");
				final_state_sent = true;
			}
			LOG_WRN("fenêtre active bornée atteinte (%lld ms ecoulees, %d cycles), retour forcé en veille%s",
				(long long)(k_uptime_get() - window_start), iters,
				was_moving ? " (trame etat actuel forcée)" : "");
			*out_exit_reason = was_moving ? AW_EXIT_CAP_STATE_FORCED : AW_EXIT_CAP_NO_STATE;
			*out_iters = (uint32_t)iters;
			return;
		}

		if (read_accel(imu, &cur) < 0) {
			continue;
		}

		int64_t now = k_uptime_get();
		bool moving = motion_detected(&prev, &cur);

		/* Le réveil matériel (INT1 IMU) a déjà détecté le mouvement --
		 * inutile d'attendre un deuxième échantillon logiciel pour le
		 * confirmer : le tout premier n'a jamais de "prev" valide,
		 * motion_detected() y renvoie toujours faux, ce qui retardait
		 * la première trame d'environ un cycle de sondage (2s) après
		 * chaque réveil -- absent de l'ancien firmware, qui gardait
		 * toujours un échantillon de référence récent (jamais en
		 * veille). Corrigé le 2026-08-30. */
		if (iters == 1 && triggered_by_motion) {
			moving = true;
		}

		int16_t pitch_dd, roll_dd;
		accel_to_pitch_roll(cur.ax, cur.ay, cur.az, &pitch_dd, &roll_dd);
		bool angle_crossed =
			(abs(pitch_dd - retained.last_sent_pitch_dd) > ANGLE_HYSTERESIS_DD) ||
			(abs(roll_dd - retained.last_sent_roll_dd) > ANGLE_HYSTERESIS_DD);

		prev = cur;

		bool min_gap_ok = (now - last_frame_a_uptime) >= MOTION_REPORT_MIN_GAP_MS;
		bool want_event_frame =
			heartbeat_pending || ((moving || angle_crossed) && min_gap_ok);

		if (want_event_frame && !frame_a_rate_limited(now)) {
			const char *reason = heartbeat_pending ? "heartbeat" :
					      moving ? "motion" : "angle";
			float gx = 0, gy = 0, gz = 0;
			uint16_t gyro_mag;
			int gyro_ret = read_gyro_and_integrate_yaw(imu, &gx, &gy, &gz, &gyro_mag);

			send_frame_a(&cur, moving, moving, reason);
			send_frame_c(&cur, gyro_ret, gx, gy, gz, gyro_mag);
			frame_a_record_send(now);
			last_frame_a_uptime = now;
			retained.last_sent_pitch_dd = pitch_dd;
			retained.last_sent_roll_dd = roll_dd;
			heartbeat_pending = false;
		}

		if (moving) {
			was_moving = true;
			rest_since = 0;
			rest_frame_pending = false;
			if (first_moving_uptime == 0) {
				first_moving_uptime = now;
			}
		} else if (was_moving) {
			if (rest_since == 0) {
				rest_since = now;
			} else if (!rest_frame_pending &&
				   ((now - rest_since) >= REST_FRAME_DELAY_MS ||
				    (first_moving_uptime != 0 &&
				     (now - first_moving_uptime) >= REST_FRAME_MAX_WAIT_MS))) {
				/* La trame "repos" n'est jamais bloquée par
				 * frame_a_rate_limited() (2026-08-24) --
				 * contrairement aux trames d'événement, c'est
				 * le signal de retour au calme : le bloquer
				 * laisse HA figé sur "Détecté" indéfiniment si
				 * beaucoup de trames sont déjà parties dans la
				 * minute précédente. */
				send_final_state_frame(&cur, "repos");
				last_frame_a_uptime = k_uptime_get();
				first_moving_uptime = 0;
				was_moving = false;
				rest_since = 0;
				rest_frame_pending = true;
				final_state_sent = true;
			}
		}

		if (!moving && !was_moving && !heartbeat_pending) {
			*out_exit_reason = final_state_sent ? AW_EXIT_STATE_SENT : AW_EXIT_NO_MOTION;
			*out_iters = (uint32_t)iters;
			return;
		}
	}
}

int main(void)
{
	const struct device *const imu = DEVICE_DT_GET(IMU_NODE);
	uint32_t reset_cause = 0;
	int err;

	hwinfo_get_reset_cause(&reset_cause);

	/* Causes "à froid" : redémarrage complet sans état utile à
	 * conserver (alimentation coupée/rebranchée, reset debug/pin,
	 * reflash). RESET_LOW_POWER_WAKE = réveil GPIO (IMU). Toute autre
	 * cause (GRTC, VBUS...) est traitée comme un réveil générique : les
	 * deux échéances retenues sont de toute façon revérifiées ci-dessous
	 * quelle qu'en soit la cause. */
	bool cold_boot = (reset_cause &
			   (RESET_PIN | RESET_SOFTWARE | RESET_POR | RESET_DEBUG)) != 0;
	bool gpio_wake = (reset_cause & RESET_LOW_POWER_WAKE) != 0;

	leds_off();

#if DIAG_PHASE1_BARE_POWEROFF
	/* Phase 1 du plan de test 2026-08-27 (fourni par l'utilisateur,
	 * vérifié et appliqué) : référence absolue SoC+GRTC. Aucun réveil
	 * armé (ni GPIO ni GRTC), aucune écriture d'erratum, sys_poweroff()
	 * immédiatement après leds_off(). La carte ne se réveillera plus du
	 * tout après ce cycle (aucune source de réveil programmée) -- attendu
	 * et voulu pour cette mesure : un seul transitoire de boot puis un
	 * plancher vrai pendant toute la fenêtre de mesure. Voir
	 * Transition-nRF54LM20A-Optimisation-Consommation.md § Plan de test
	 * (phases). */
	sys_poweroff();
	return 0;
#endif

	bool have_state = retained_load();
	bool fresh_session = cold_boot || !have_state;

	/* 2026-08-24 : ne remettre packet_id/angles/echeances GRTC a zero que
	 * si la RAM retenue est reellement invalide (CRC, !have_state), pas a
	 * chaque reset "cold_boot" au sens large (pin/software/debug) -- ces
	 * resets ne coupent pas l'alimentation de la RAM retenue, son contenu
	 * y reste valide (deja verifie independamment par le CRC). Sans ce
	 * decouplage, une sequence de resets rapprochee (sonde de debug
	 * connectee, ou rebond mecanique du connecteur USB-C a la
	 * reconnexion) remet packet_id a la meme valeur a chaque cycle : les
	 * trames suivantes sont alors toutes jetees par le deduplicateur
	 * BTHome de HA (meme packet_id a <4s d'intervalle), ce qui peut
	 * laisser HA bloque sur un etat perime -- voir
	 * Transition-2026-08-24-Soiree.md. */
	if (!have_state) {
		memset(&retained, 0, sizeof(retained));
	}
	bthome_pid = retained.bthome_pid;

	if (!diag_log_load()) {
		memset(&diag, 0, sizeof(diag));
	}

#if !DIAG_NO_IMU_TEST
	err = init_imu(imu);
	if (err < 0) {
		/* 2026-08-24 : ce chemin faisait "return 0" sans jamais armer de
		 * source de réveil ni flusher le log -- une carte qui échoue ici
		 * restait bloquée indéfiniment dans un état qui n'est même pas un
		 * vrai System OFF (main() qui rend la main, pas sys_poweroff()),
		 * invisible en série (log différé jamais vidé) et injoignable en
		 * BLE. Reboot au lieu de rester silencieusement bloqué -- un échec
		 * transitoire (bus I2C pas encore stable, etc.) a une chance de se
		 * résoudre au prochain essai plutôt que de bricker le réveil pour
		 * de bon. */
		LOG_ERR("IMU init failed (%d), reboot", err);
		sys_reboot(SYS_REBOOT_COLD);
	}
	set_accel_sampling_freq(imu);
	set_gyro_power(imu, false); /* explicite : gyro éteint par défaut au boot */

	err = init_button();
	if (err < 0) {
		LOG_WRN("button init failed (%d) -- bouton toujours envoyé à 0", err);
	}

	if (fresh_session) {
		err = configure_imu_wakeup();
		if (err < 0) {
			LOG_ERR("IMU wake-up config failed (%d) -- le réveil matériel ne "
				"fonctionnera pas, mais le firmware continue (heartbeat/santé "
				"toujours actifs via GRTC)", err);
		}
	}
#endif /* !DIAG_NO_IMU_TEST */

	err = set_fixed_ble_identity();
	if (err < 0) {
		LOG_WRN("Could not set fixed BLE identity (err %d), "
			"falling back to stack-generated address", err);
	}

#if !DIAG_NO_RADIO_TEST
	err = bt_enable(NULL);
	if (err) {
		/* Même correctif que pour l'échec IMU ci-dessus : reboot au lieu
		 * de rester bloqué silencieusement sans source de réveil armée. */
		LOG_ERR("Bluetooth init failed (err %d), reboot", err);
		sys_reboot(SYS_REBOOT_COLD);
	}
#endif

	LOG_INF("Bluetooth initialized -- door sensor, profil L (System OFF): "
		"cold_boot=%d gpio_wake=%d reset_cause=0x%08x",
		fresh_session, gpio_wake, reset_cause);

	diag_log_dump();

	uint64_t now_us = z_nrf_grtc_timer_read();
	bool health_due = fresh_session || (now_us >= retained.next_health_us);
	/* Comme le firmware précédent (last_frame_a_uptime initialisé à
	 * -FRAME_A_HEARTBEAT_MS avant la boucle -- premier passage toujours
	 * "heartbeat_due"), un cold boot déclenche aussi une trame A
	 * immédiate : comportement documenté et utilisé pour la vérification
	 * post-flash (xiao_nrf54lm20a_project_notes.md, "trame B... puis une
	 * trame A (heartbeat) dans les 2-4s suivantes") -- à préserver. */
	bool heartbeat_due = fresh_session || (now_us >= retained.next_heartbeat_us);

#if !DIAG_NO_RADIO_TEST
	if (fresh_session) {
		/* Trame B immédiate au boot, comme aujourd'hui. */
		send_frame_b(imu);
		retained.next_health_us = next_health_deadline(now_us);
		health_due = false;
	} else if (health_due) {
		send_frame_b(imu);
		retained.next_health_us = next_health_deadline(now_us);
	}
#else
	ARG_UNUSED(health_due);
#endif

	uint8_t aw_exit_reason = 0xFF; /* fenêtre active non entrée ce cycle */
	uint32_t aw_iters = 0;

#if !DIAG_NO_RADIO_TEST
	if (gpio_wake || heartbeat_due) {
		run_active_window(imu, heartbeat_due, gpio_wake, &aw_exit_reason, &aw_iters);
		retained.next_heartbeat_us = next_heartbeat_deadline(z_nrf_grtc_timer_read());
	}
#else
	retained.next_heartbeat_us = next_heartbeat_deadline(now_us);
#endif

	retained.bthome_pid = bthome_pid;
	retained_save();

#if !DIAG_NO_IMU_TEST
	int ret_gpio_wake = arm_gpio_wake();

	if (ret_gpio_wake < 0) {
		LOG_ERR("arm_gpio_wake failed (%d) -- le réveil sur mouvement ne "
			"fonctionnera pas pour ce cycle", ret_gpio_wake);
	}
#else
	/* IMU non initialisée dans ce mode -- ne pas réarmer une interruption
	 * sur une broche non alimentée/flottante (risque de réveil parasite). */
	int ret_gpio_wake = 0;
#endif

	uint64_t deadline_us = MIN(retained.next_health_us, retained.next_heartbeat_us);
	uint64_t now2_us = z_nrf_grtc_timer_read();
	uint64_t wake_in_us = (deadline_us > now2_us) ? (deadline_us - now2_us) : 0;

	if (wake_in_us < 1000000) {
		wake_in_us = 1000000; /* plancher 1s (z_nrf_grtc_wakeup_prepare
					* refuse une valeur trop basse) */
	}

	int ret_grtc_prepare = z_nrf_grtc_wakeup_prepare(wake_in_us);

	if (ret_grtc_prepare < 0) {
		LOG_ERR("z_nrf_grtc_wakeup_prepare failed (%d) -- pas de réveil "
			"périodique programmé pour ce cycle", ret_grtc_prepare);
	}

	diag_log_append(reset_cause, fresh_session, gpio_wake, wake_in_us,
			 ret_gpio_wake, ret_grtc_prepare, aw_exit_reason, aw_iters);

	LOG_INF("Entrée en System OFF, réveil GRTC dans %llu ms (ou plus tôt sur mouvement), "
		"ret_gpio=%d ret_grtc=%d",
		wake_in_us / 1000, ret_gpio_wake, ret_grtc_prepare);

	/* Pas de pm_device_action_run(PM_DEVICE_ACTION_SUSPEND) sur la
	 * console ici : nécessiterait CONFIG_PM_DEVICE=y globalement, qui
	 * activerait le framework PM device pour tous les drivers du
	 * projet (IMU, PMIC...) sans qu'ils aient été vérifiés vis-à-vis de
	 * ce cycle veille/réveil personnalisé -- risque jugé supérieur au
	 * bénéfice (juste un arrêt propre de l'UART avant poweroff). */
	hwinfo_clear_reset_cause();

	/* log_panic() (vidage synchrone du tampon de log, ajouté puis retiré
	 * le 2026-08-24) a été identifié comme suspect direct d'un blocage
	 * reproductible en boucle UART, interruptions masquées -- retiré par
	 * prudence avant la validation longue durée. À reprendre plus tard :
	 * les logs de fin de cycle peuvent de nouveau être perdus (mode
	 * différé jamais vidé avant le reset), voir "N messages dropped". */

	/* Erratum nRF54LM20A [37] POWER -- 2026-08-27, errata officiel Nordic
	 * (anomaly_20A_37) : "Current consumption might increase after pin
	 * reset or power cycle" si le firmware entre en System OFF trop tôt
	 * après un reset/power-cycle. Contournement officiel exact : écrire 1
	 * dans ce registre puis exécuter au moins 40 cycles CPU avant
	 * sys_poweroff() -- largement respecté ici vu tout le travail déjà
	 * fait dans ce cycle (I2C, BLE, calculs) entre le boot et ce point. */
	*(volatile uint32_t *)0x5005340CUL = 1;

	sys_poweroff();

	return 0;
}
