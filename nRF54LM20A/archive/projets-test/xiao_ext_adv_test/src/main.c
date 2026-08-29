/*
 * Banc de test BLE Extended Advertising -- Evolution-XIAO-BLE.md §6.
 *
 * Trame BTHome v2 unique (profil E, §6.3) portant l'intégralité des mesures
 * du périmètre en un seul advertising étendu, jusqu'à 255 octets. Réutilise
 * l'identité BLE fixe, la lecture IMU et la lecture batterie déjà validées
 * dans xiao_door_sensor -- seule la construction/émission du payload change.
 *
 * Champs non encore instrumentés sur ce banc (freefall, double-tap, bouton,
 * charge batterie, yaw) sont envoyés à 0 : ce test valide le lien radio
 * étendu et le décodage, pas la détection d'événements IMU complète
 * (voir §9 Phase 2 pour cette suite).
 */

#include <errno.h>
#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(xiao_ext_adv_test, LOG_LEVEL_INF);

#define IMU_NODE DT_ALIAS(imu0)
#define RAD_TO_DEG 57.29577951308232f

#define REPORT_INTERVAL_MS 15000
#define ADV_INT      0x00A0   /* 100 ms, unités de 0,625 ms (§6.4) */
#define TRAIN_MS     700

/* --- BTHome v2, profil E : Object IDs utilisés (§2.5/§7.5) --- */
#define BTHOME_UUID_LO      0xD2
#define BTHOME_UUID_HI      0xFC
#define BTHOME_INFO_TRIG    0x44   /* v2, clair, trigger-based (§2.2) */

#define OBJ_PACKET_ID       0x00
#define OBJ_BATTERY         0x01
#define OBJ_TEMPERATURE     0x02
#define OBJ_VOLTAGE         0x0C
#define OBJ_GENERIC_BOOL    0x0F
#define OBJ_BATTERY_LOW     0x15
#define OBJ_BATTERY_CHARGE  0x16
#define OBJ_MOTION          0x21
#define OBJ_MOVING          0x22
#define OBJ_TAMPER          0x2B
#define OBJ_VIBRATION       0x2C
#define OBJ_BUTTON          0x3A
#define OBJ_ROTATION        0x3F
#define OBJ_ACCELERATION    0x51
#define OBJ_GYROSCOPE       0x52
#define OBJ_FW_VERSION      0xF2

#if defined(DT_N_NODELABEL_power_en)
static const struct device *const power_en_dev = DEVICE_DT_GET(DT_NODELABEL(power_en));
#endif
#if defined(DT_N_NODELABEL_imu_vdd)
static const struct device *const imu_vdd_dev = DEVICE_DT_GET(DT_NODELABEL(imu_vdd));
#endif
static const struct device *const charger_dev = DEVICE_DT_GET(DT_NODELABEL(pmic_charger));

/* --- Identité BLE fixe (reprise de xiao_door_sensor, §7.2) --- */
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

/* --- Batterie (reprise de xiao_door_sensor) --- */
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

static void set_sampling_freq(const struct device *dev)
{
	struct sensor_value odr_attr = { .val1 = 12, .val2 = 500000 };

	sensor_attr_set(dev, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr_attr);
	sensor_attr_set(dev, SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr_attr);
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

struct imu_reading {
	float ax, ay, az;
	float gx, gy, gz;
	int16_t die_temp_cc; /* x0.01 °C */
};

static int read_imu(const struct device *dev, struct imu_reading *out)
{
	struct sensor_value x, y, z, t;
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

	ret = sensor_sample_fetch_chan(dev, SENSOR_CHAN_GYRO_XYZ);
	if (ret < 0) {
		return ret;
	}
	sensor_channel_get(dev, SENSOR_CHAN_GYRO_X, &x);
	sensor_channel_get(dev, SENSOR_CHAN_GYRO_Y, &y);
	sensor_channel_get(dev, SENSOR_CHAN_GYRO_Z, &z);
	out->gx = sensor_value_to_float(&x) * RAD_TO_DEG;
	out->gy = sensor_value_to_float(&y) * RAD_TO_DEG;
	out->gz = sensor_value_to_float(&z) * RAD_TO_DEG;

	if (sensor_sample_fetch_chan(dev, SENSOR_CHAN_DIE_TEMP) == 0 &&
	    sensor_channel_get(dev, SENSOR_CHAN_DIE_TEMP, &t) == 0) {
		out->die_temp_cc = clamp_i16(sensor_value_to_float(&t) * 100.0f);
	} else {
		out->die_temp_cc = 0;
	}

	return 0;
}

/* Pitch/roll par projection du vecteur gravité (§7.7). Yaw non calculé sur
 * ce banc (nécessiterait une intégration gyroscopique dans la durée). */
static void accel_to_pitch_roll(float ax, float ay, float az,
				  int16_t *pitch_dd, int16_t *roll_dd)
{
	float pitch = atan2f(-ax, sqrtf(ay * ay + az * az)) * RAD_TO_DEG;
	float roll  = atan2f(ay, az) * RAD_TO_DEG;

	*pitch_dd = clamp_i16(pitch * 10.0f);
	*roll_dd  = clamp_i16(roll * 10.0f);
}

/* --- Construction du payload BTHome, profil E (§6.3/§7.5) --- */
struct bthome_buf {
	uint8_t data[64];
	uint8_t len;
};

static void bth_init(struct bthome_buf *b)
{
	b->data[0] = BTHOME_UUID_LO;
	b->data[1] = BTHOME_UUID_HI;
	b->data[2] = BTHOME_INFO_TRIG;
	b->len = 3;
}

static void bth_u8(struct bthome_buf *b, uint8_t id, uint8_t v)
{
	b->data[b->len++] = id;
	b->data[b->len++] = v;
}

static void bth_u16(struct bthome_buf *b, uint8_t id, uint16_t v)
{
	b->data[b->len++] = id;
	b->data[b->len++] = (uint8_t)(v & 0xFF);
	b->data[b->len++] = (uint8_t)(v >> 8);
}

static void bth_s16(struct bthome_buf *b, uint8_t id, int16_t v)
{
	bth_u16(b, id, (uint16_t)v);
}

static uint8_t bthome_pid;

static void build_frame_e(struct bthome_buf *b, const struct imu_reading *imu,
			    uint8_t battery_pct, uint16_t battery_mv, bool moved)
{
	int16_t pitch_dd, roll_dd;

	accel_to_pitch_roll(imu->ax, imu->ay, imu->az, &pitch_dd, &roll_dd);

	bth_init(b);
	bth_u8 (b, OBJ_PACKET_ID,      bthome_pid);
	bth_u8 (b, OBJ_BATTERY,        battery_pct);
	bth_s16(b, OBJ_TEMPERATURE,    imu->die_temp_cc);
	bth_u16(b, OBJ_VOLTAGE,        battery_mv);
	bth_u8 (b, OBJ_GENERIC_BOOL,   moved ? 1 : 0);   /* activity (stub) */
	bth_u8 (b, OBJ_BATTERY_LOW,    battery_pct < 15 ? 1 : 0);
	bth_u8 (b, OBJ_BATTERY_CHARGE, 0);                /* pas mesuré sur ce banc */
	bth_u8 (b, OBJ_MOTION,         moved ? 1 : 0);
	bth_u8 (b, OBJ_MOVING,         moved ? 1 : 0);
	bth_u8 (b, OBJ_TAMPER,         0);                /* free-fall (stub) */
	bth_u8 (b, OBJ_VIBRATION,      0);                /* double-tap (stub) */
	bth_u8 (b, OBJ_BUTTON,         0);                /* pas de bouton sur ce banc */
	bth_s16(b, OBJ_ROTATION,       pitch_dd);
	bth_s16(b, OBJ_ROTATION,       roll_dd);
	bth_s16(b, OBJ_ROTATION,       0);                /* yaw (stub) */
	bth_u16(b, OBJ_ACCELERATION,   clamp_u16(fabsf(imu->ax) * 1000.0f));
	bth_u16(b, OBJ_ACCELERATION,   clamp_u16(fabsf(imu->ay) * 1000.0f));
	bth_u16(b, OBJ_ACCELERATION,   clamp_u16(fabsf(imu->az) * 1000.0f));
	bth_u16(b, OBJ_GYROSCOPE,      clamp_u16(fabsf(imu->gx) * 1000.0f));
	bth_u16(b, OBJ_GYROSCOPE,      clamp_u16(fabsf(imu->gy) * 1000.0f));
	bth_u16(b, OBJ_GYROSCOPE,      clamp_u16(fabsf(imu->gz) * 1000.0f));
	b->data[b->len++] = OBJ_FW_VERSION;
	b->data[b->len++] = 0;  /* patch */
	b->data[b->len++] = 1;  /* minor */
	b->data[b->len++] = 0;  /* major */
}

/* --- Émission étendue (§6.4) --- */
static struct bt_le_ext_adv *adv_set;

static const struct bt_le_adv_param ext_param =
	BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_EXT_ADV | BT_LE_ADV_OPT_USE_IDENTITY,
			      ADV_INT, ADV_INT, NULL);

static int ext_adv_init(void)
{
	return bt_le_ext_adv_create(&ext_param, NULL, &adv_set);
}

static int ext_adv_broadcast(const struct bthome_buf *b)
{
	static const uint8_t flags = BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR;
	struct bt_data ad[3];
	int err;

	ad[0] = (struct bt_data)BT_DATA(BT_DATA_FLAGS, &flags, 1);
	ad[1] = (struct bt_data)BT_DATA(BT_DATA_NAME_COMPLETE,
					 CONFIG_BT_DEVICE_NAME,
					 sizeof(CONFIG_BT_DEVICE_NAME) - 1);
	ad[2] = (struct bt_data)BT_DATA(BT_DATA_SVC_DATA16, b->data, b->len);

	err = bt_le_ext_adv_set_data(adv_set, ad, 3, NULL, 0);
	if (err) {
		return err;
	}
	err = bt_le_ext_adv_start(adv_set, BT_LE_EXT_ADV_START_DEFAULT);
	if (err) {
		return err;
	}
	k_msleep(TRAIN_MS);
	return bt_le_ext_adv_stop(adv_set);
}

int main(void)
{
	const struct device *const imu = DEVICE_DT_GET(IMU_NODE);
	struct imu_reading reading = {0};
	struct imu_reading prev = {0};
	uint8_t battery_pct = 0;
	uint16_t battery_mv = 0;
	struct bthome_buf frame;
	int err;
	bool have_prev = false;

	leds_off();

	err = init_imu(imu);
	if (err < 0) {
		LOG_ERR("IMU init failed (%d), halting", err);
		return 0;
	}
	set_sampling_freq(imu);

	err = set_fixed_ble_identity();
	if (err < 0) {
		LOG_WRN("Could not set fixed BLE identity (err %d)", err);
	}

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return 0;
	}

	err = ext_adv_init();
	if (err) {
		LOG_ERR("Extended adv set creation failed (err %d)", err);
		return 0;
	}

	LOG_INF("Bluetooth initialized -- banc de test BLE Extended, profil E, "
		"trame toutes les %d s", REPORT_INTERVAL_MS / 1000);

	while (1) {
		if (read_imu(imu, &reading) < 0) {
			k_msleep(REPORT_INTERVAL_MS);
			continue;
		}
		if (read_battery(&battery_pct, &battery_mv) < 0) {
			LOG_WRN("battery read failed");
		}

		bool moved = have_prev &&
			     (fabsf(reading.ax - prev.ax) + fabsf(reading.ay - prev.ay) +
			      fabsf(reading.az - prev.az)) > 0.3f;
		prev = reading;
		have_prev = true;

		bthome_pid++;
		build_frame_e(&frame, &reading, battery_pct, battery_mv, moved);

		LOG_INF("trame E #%u: %d octets de mesures, batt=%u%% (%u mV), "
			"accel x=%d.%02d y=%d.%02d z=%d.%02d",
			bthome_pid, frame.len - 3, battery_pct, battery_mv,
			(int)reading.ax, abs((int)(reading.ax * 100)) % 100,
			(int)reading.ay, abs((int)(reading.ay * 100)) % 100,
			(int)reading.az, abs((int)(reading.az * 100)) % 100);

		err = ext_adv_broadcast(&frame);
		if (err) {
			LOG_ERR("Extended advertising failed (err %d)", err);
		}

		k_msleep(REPORT_INTERVAL_MS);
	}
	return 0;
}
