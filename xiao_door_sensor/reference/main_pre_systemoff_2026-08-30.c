/*
 * ARCHIVE — reconstitué le 2026-08-30, PAS un fichier de build actif.
 *
 * Copie exacte de src/main.c telle qu'elle existait au tout début de la
 * session de travail du 2026-08-30, AVANT la réécriture System OFF et
 * tous les correctifs qui ont suivi (trame B >31 octets, décalage
 * d'index gyroscope dans frame_c, délai GYRO_STARTUP_MS insuffisant).
 * C'est donc le firmware "toujours actif" (System ON + sondage logiciel
 * 2s en continu), celui qui tournait sur les 3 unités déployées avant
 * cette date.
 *
 * Conservée ici uniquement parce que ce projet n'a pas de contrôle de
 * version (pas de dépôt git) — sans cette copie, le contenu exact de
 * cette version serait irrécupérable une fois la conversation d'origine
 * terminée. Utile si une comparaison directe de consommation "ancien vs
 * nouveau firmware" est souhaitée au PPK II (voir
 * ../../PPK-Mesures-Transition.md) : il faudrait alors créer un second
 * dossier de projet (CMakeLists.txt + prj.conf copiés depuis
 * xiao_door_sensor, sans les ajouts System OFF de prj.conf ni l'overlay
 * retained_mem) pour la builder et la flasher sur une unité de test
 * séparée, JAMAIS sur une des 3 unités en service actuellement sur ce
 * firmware.
 *
 * Contient aussi le bug préexistant de décalage d'index gyroscope
 * (C_OFF_GYRO_MAG = 8, devrait être 9) tel qu'il était avant sa
 * découverte et sa correction — préservé tel quel pour fidélité
 * historique, ne pas corriger cette copie.
 */

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
 * Free-fall, double-tap et bouton ne sont pas câblés sur ce firmware : le
 * driver Zephyr lsm6dsl n'expose pas ces événements matériels via l'API
 * sensor_trigger standard (nécessiterait d'écrire WAKE_UP_THS/TAP_CFG en
 * I2C brut, cf. §9 phase suivante). Ils sont envoyés à 0 en attendant.
 * Le yaw nécessiterait une intégration gyroscopique dans la durée avec
 * recalage à l'inactivité (§7.7) : non implémenté ici non plus, envoyé à 0.
 * La charge batterie (0x16) n'est pas mesurée par ce firmware : le nPM1300
 * expose un statut de charge, mais son intégration n'est pas encore faite.
 */

#include <errno.h>
#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(xiao_door_sensor, LOG_LEVEL_INF);

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
#define REST_FRAME_DELAY_MS      15000              /* §4.3 */
#define ADV_INT      0x00A0   /* 100 ms, unités de 0,625 ms (§4.2) */
#define ADV_BURST_MS 700      /* §4.2 : ~7 events sur 3 canaux primaires */

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

/* Accéléromètre seul : ~9 µA en low-power @12,5 Hz (LA_IddLM, datasheet
 * LSM6DS3TR-C DocID030071 Rev 3, Table 4 p. 24 -- vérifié le 2026-08-23,
 * voir C:\ncs\projects\Recherche-Reveil-Materiel-XIAO.md), doit tourner
 * en continu pour la détection de mouvement. Le gyroscope N'EST PAS
 * activé ici -- voir set_gyro_power(). Le laisser actif en continu
 * (comme le faisait la version précédente, qui appelait cet attr_set
 * aussi sur SENSOR_CHAN_GYRO_XYZ) coûte ~0,9 mA en continu, soit ~100x
 * le courant accéléromètre seul : sur une batterie 1500 mAh, la
 * différence est de l'ordre de quelques années d'autonomie contre
 * quelques semaines.
 */
static void set_accel_sampling_freq(const struct device *dev)
{
	struct sensor_value odr_attr = { .val1 = 12, .val2 = 500000 };

	sensor_attr_set(dev, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr_attr);
}

/* Active/coupe le gyroscope à la demande (ODR 12,5 Hz <-> 0 = power-down).
 * Délai de stabilisation conservateur après activation avant que la mesure
 * soit fiable (turn-on time typique ST, à confirmer par mesure réelle). */
#define GYRO_STARTUP_MS 80

static void set_gyro_power(const struct device *dev, bool on)
{
	struct sensor_value odr_attr = { .val1 = on ? 12 : 0, .val2 = on ? 500000 : 0 };

	sensor_attr_set(dev, SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr_attr);
	if (on) {
		k_msleep(GYRO_STARTUP_MS);
	}
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
static uint8_t frame_b[] = {
	BTHOME_UUID_LO, BTHOME_UUID_HI, BTHOME_INFO_TRIG,
	OBJ_PACKET_ID, 0x00,
	OBJ_BATTERY, 0x00,
	OBJ_TEMPERATURE, 0x00, 0x00,
	OBJ_VOLTAGE, 0x00, 0x00,
	OBJ_BATTERY_LOW, 0x00,
	OBJ_BATTERY_CHARGE, 0x00,
};
#define B_OFF_PACKET_ID 4
#define B_OFF_BATTERY   6
#define B_OFF_TEMP      8
#define B_OFF_VOLTAGE   11
#define B_OFF_BATT_LOW  14
#define B_OFF_BATT_CHG  16

/* --- Trame C : IMU brut, magnitudes seules (§3.3, variante recommandée) --- */
static uint8_t frame_c[] = {
	BTHOME_UUID_LO, BTHOME_UUID_HI, BTHOME_INFO_TRIG,
	OBJ_PACKET_ID, 0x00,
	OBJ_ACCELERATION, 0x00, 0x00,
	OBJ_GYROSCOPE, 0x00, 0x00,
};
#define C_OFF_PACKET_ID  4
#define C_OFF_ACCEL_MAG  6
#define C_OFF_GYRO_MAG   8

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
 * sur les 60 dernières secondes (§4.3). */
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
	frame_a[A_OFF_TAMPER] = 0;    /* free-fall : non câblé (voir en-tête) */
	frame_a[A_OFF_VIBRATION] = 0; /* double-tap : non câblé */
	frame_a[A_OFF_BUTTON] = 0;    /* bouton : non câblé */
	sys_put_le16((uint16_t)pitch_dd, &frame_a[A_OFF_PITCH]);
	sys_put_le16((uint16_t)roll_dd, &frame_a[A_OFF_ROLL]);
	sys_put_le16(0, &frame_a[A_OFF_YAW]); /* yaw : non calculé (voir en-tête) */

	LOG_INF("trame A (%s) #%u: pitch=%d.%d roll=%d.%d motion=%d activity=%d",
		reason, bthome_pid, pitch_dd / 10, abs(pitch_dd % 10),
		roll_dd / 10, abs(roll_dd % 10), motion, activity);

	advertise_burst(frame_a_ad, ARRAY_SIZE(frame_a_ad));
}

static void send_frame_c(const struct device *imu, const struct imu_sample *accel)
{
	float gx = 0, gy = 0, gz = 0;
	uint16_t accel_mag, gyro_mag = 0;

	if (read_gyro_burst(imu, &gx, &gy, &gz) == 0) {
		gyro_mag = clamp_u16(sqrtf(gx * gx + gy * gy + gz * gz) * 1000.0f);
	}
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
	frame_b[B_OFF_BATT_CHG] = 0; /* charge : non mesurée (voir en-tête) */

	LOG_INF("trame B #%u: battery=%u%% (%u mV) temp=%d.%02d°C",
		bthome_pid, battery_pct, battery_mv, temp_cc / 100, abs(temp_cc % 100));

	advertise_burst(frame_b_ad, ARRAY_SIZE(frame_b_ad));
}

/* Prochaine échéance trame B avec un jitter aléatoire ±30s (§4.3),
 * désynchronise les nœuds redémarrés en même temps après une coupure. */
static int64_t next_frame_b_deadline(int64_t from)
{
	int32_t jitter = (int32_t)(sys_rand32_get() % (2 * FRAME_B_JITTER_MS)) - FRAME_B_JITTER_MS;

	return from + FRAME_B_INTERVAL_MS + jitter;
}

int main(void)
{
	const struct device *const imu = DEVICE_DT_GET(IMU_NODE);
	struct imu_sample prev = { .valid = false };
	struct imu_sample cur;
	int16_t last_sent_pitch_dd = 0, last_sent_roll_dd = 0;
	bool was_moving = false;
	int64_t rest_since = 0;
	bool rest_frame_pending = false;
	int64_t last_frame_a_uptime = -FRAME_A_HEARTBEAT_MS;
	int64_t next_frame_b_uptime;
	int err;

	leds_off();

	err = init_imu(imu);
	if (err < 0) {
		LOG_ERR("IMU init failed (%d), halting", err);
		return 0;
	}
	set_accel_sampling_freq(imu);
	set_gyro_power(imu, false); /* explicite : gyro éteint par défaut au boot */

	err = set_fixed_ble_identity();
	if (err < 0) {
		LOG_WRN("Could not set fixed BLE identity (err %d), "
			"falling back to stack-generated address", err);
	}

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return 0;
	}

	next_frame_b_uptime = next_frame_b_deadline(k_uptime_get());

	LOG_INF("Bluetooth initialized -- door sensor, profil L: trame A "
		"événementielle (heartbeat %d min), trame B toutes les %d min",
		FRAME_A_HEARTBEAT_MS / 60000, FRAME_B_INTERVAL_MS / 60000);

	/* Trame B immédiate au boot, comme le heartbeat trame A. */
	send_frame_b(imu);

	while (1) {
		k_msleep(POLL_INTERVAL_MS);

		if (read_accel(imu, &cur) < 0) {
			continue;
		}

		int64_t now = k_uptime_get();
		bool moving = motion_detected(&prev, &cur);
		bool heartbeat_due = (now - last_frame_a_uptime) >= FRAME_A_HEARTBEAT_MS;

		int16_t pitch_dd, roll_dd;
		accel_to_pitch_roll(cur.ax, cur.ay, cur.az, &pitch_dd, &roll_dd);
		bool angle_crossed = (abs(pitch_dd - last_sent_pitch_dd) > ANGLE_HYSTERESIS_DD) ||
				      (abs(roll_dd - last_sent_roll_dd) > ANGLE_HYSTERESIS_DD);

		prev = cur;

		/* Intervalle minimum de 4s entre deux trames A motion/angle
		 * (§4.3) -- le heartbeat, par construction 60 min après la
		 * dernière trame, n'est jamais concerné par cette garde. */
		bool min_gap_ok = (now - last_frame_a_uptime) >= MOTION_REPORT_MIN_GAP_MS;
		bool want_event_frame = heartbeat_due || ((moving || angle_crossed) && min_gap_ok);

		if (want_event_frame && !frame_a_rate_limited(now)) {
			const char *reason = heartbeat_due ? "heartbeat" :
					      moving ? "motion" : "angle";

			send_frame_a(&cur, moving, moving, reason);
			send_frame_c(imu, &cur);
			frame_a_record_send(now);
			last_frame_a_uptime = now;
			last_sent_pitch_dd = pitch_dd;
			last_sent_roll_dd = roll_dd;
		}

		/* Trame de retour au repos : tous les binaires à 0, une seule
		 * fois, REST_FRAME_DELAY_MS après la fin du dernier mouvement
		 * (§2.6/§4.1). */
		if (moving) {
			was_moving = true;
			/* Repart de zéro à chaque mouvement détecté : sans ce
			 * reset, un sursaut de mouvement après un premier arrêt
			 * (bruit capteur, vibration) laissait un chronomètre
			 * périmé, faisant déclencher la trame de repos sur la
			 * base d'un arrêt bien antérieur au lieu du dernier. */
			rest_since = 0;
			rest_frame_pending = false;
		} else if (was_moving) {
			if (rest_since == 0) {
				rest_since = now;
			} else if (!rest_frame_pending &&
				   (now - rest_since) >= REST_FRAME_DELAY_MS &&
				   !frame_a_rate_limited(now)) {
				send_frame_a(&cur, false, false, "repos");
				frame_a_record_send(now);
				last_frame_a_uptime = now;
				was_moving = false;
				rest_since = 0;
				rest_frame_pending = true;
			}
		}

		if (now >= next_frame_b_uptime) {
			send_frame_b(imu);
			next_frame_b_uptime = next_frame_b_deadline(now);
		}
	}
	return 0;
}
