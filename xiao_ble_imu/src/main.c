#include <errno.h>
#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(xiao_ble_imu, LOG_LEVEL_INF);

#define IMU_NODE DT_ALIAS(imu0)

#define ADV_INTERVAL_MS 5000
#define RAD_TO_DEG      57.29577951308232f

/*
 * BTHome v2 unencrypted, unsigned-integer object layout.
 * Acceleration (0x51) and Gyroscope (0x52) are magnitude values
 * (factor 0.001, uint16) -- BTHome has no signed per-axis object, so raw
 * X/Y/Z is folded into a single vector length per sensor. This decodes
 * natively in Home Assistant's BTHome integration with zero extra config.
 * https://bthome.io/format/
 */
#define BTHOME_UUID_LO   0xD2
#define BTHOME_UUID_HI   0xFC
#define BTHOME_INFO_V2_UNENCRYPTED 0x40
#define BTHOME_OBJ_PACKET_ID       0x00
#define BTHOME_OBJ_ACCELERATION    0x51
#define BTHOME_OBJ_GYROSCOPE       0x52

static uint8_t bthome_payload[] = {
	BTHOME_UUID_LO, BTHOME_UUID_HI,
	BTHOME_INFO_V2_UNENCRYPTED,
	BTHOME_OBJ_PACKET_ID, 0x00,
	BTHOME_OBJ_ACCELERATION, 0x00, 0x00,
	BTHOME_OBJ_GYROSCOPE, 0x00, 0x00,
};

static struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_SVC_DATA16, bthome_payload, sizeof(bthome_payload)),
};

#if defined(DT_N_NODELABEL_power_en)
static const struct device *const power_en_dev = DEVICE_DT_GET(DT_NODELABEL(power_en));
#endif
#if defined(DT_N_NODELABEL_imu_vdd)
static const struct device *const imu_vdd_dev = DEVICE_DT_GET(DT_NODELABEL(imu_vdd));
#endif

static int enable_imu_power(void)
{
#if defined(DT_N_NODELABEL_power_en) || defined(DT_N_NODELABEL_imu_vdd)
	int ret;
#endif

#if defined(DT_N_NODELABEL_power_en)
	if (!device_is_ready(power_en_dev)) {
		LOG_ERR("power_en regulator is not ready");
		return -ENODEV;
	}
	ret = regulator_enable(power_en_dev);
	if (ret < 0 && ret != -EALREADY) {
		LOG_ERR("Failed to enable power_en: %d", ret);
		return ret;
	}
#endif

#if defined(DT_N_NODELABEL_imu_vdd)
	if (!device_is_ready(imu_vdd_dev)) {
		LOG_ERR("imu_vdd regulator is not ready");
		return -ENODEV;
	}
	ret = regulator_enable(imu_vdd_dev);
	if (ret < 0 && ret != -EALREADY) {
		LOG_ERR("Failed to enable imu_vdd: %d", ret);
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
		LOG_ERR("%s: device not ready after init", dev->name);
		return -ENODEV;
	}

	return 0;
}

static uint16_t clamp_bthome_u16(float scaled)
{
	if (scaled < 0.0f) {
		scaled = 0.0f;
	}
	if (scaled > (float)UINT16_MAX) {
		scaled = (float)UINT16_MAX;
	}
	return (uint16_t)scaled;
}

static int read_imu_magnitudes(const struct device *dev, uint16_t *accel_mm_s2,
				uint16_t *gyro_mdeg_s)
{
	struct sensor_value x, y, z;
	int ret;
	float ax, ay, az, gx, gy, gz;
	float accel_mag, gyro_mag;

	ret = sensor_sample_fetch_chan(dev, SENSOR_CHAN_ACCEL_XYZ);
	if (ret < 0) {
		return ret;
	}
	sensor_channel_get(dev, SENSOR_CHAN_ACCEL_X, &x);
	sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Y, &y);
	sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Z, &z);
	ax = sensor_value_to_float(&x);
	ay = sensor_value_to_float(&y);
	az = sensor_value_to_float(&z);
	accel_mag = sqrtf(ax * ax + ay * ay + az * az);

	ret = sensor_sample_fetch_chan(dev, SENSOR_CHAN_GYRO_XYZ);
	if (ret < 0) {
		return ret;
	}
	sensor_channel_get(dev, SENSOR_CHAN_GYRO_X, &x);
	sensor_channel_get(dev, SENSOR_CHAN_GYRO_Y, &y);
	sensor_channel_get(dev, SENSOR_CHAN_GYRO_Z, &z);
	gx = sensor_value_to_float(&x) * RAD_TO_DEG;
	gy = sensor_value_to_float(&y) * RAD_TO_DEG;
	gz = sensor_value_to_float(&z) * RAD_TO_DEG;
	gyro_mag = sqrtf(gx * gx + gy * gy + gz * gz);

	*accel_mm_s2 = clamp_bthome_u16(accel_mag * 1000.0f);
	*gyro_mdeg_s = clamp_bthome_u16(gyro_mag * 1000.0f);

	LOG_INF("accel |a|=%d.%03d m/s^2  gyro |w|=%d.%03d deg/s", *accel_mm_s2 / 1000,
		*accel_mm_s2 % 1000, *gyro_mdeg_s / 1000, *gyro_mdeg_s % 1000);

	return 0;
}

static void set_sampling_freq(const struct device *dev)
{
	struct sensor_value odr_attr = { .val1 = 12, .val2 = 500000 };

	sensor_attr_set(dev, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr_attr);
	sensor_attr_set(dev, SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr_attr);
}

int main(void)
{
	const struct device *const imu = DEVICE_DT_GET(IMU_NODE);
	uint16_t accel_mag, gyro_mag;
	uint8_t packet_id = 0;
	int err;

	err = init_imu(imu);
	if (err < 0) {
		LOG_ERR("IMU init failed (%d), halting", err);
		return 0;
	}
	set_sampling_freq(imu);

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return 0;
	}
	LOG_INF("Bluetooth initialized, starting BTHome broadcast");

	if (read_imu_magnitudes(imu, &accel_mag, &gyro_mag) == 0) {
		bthome_payload[4] = packet_id;
		sys_put_le16(accel_mag, &bthome_payload[6]);
		sys_put_le16(gyro_mag, &bthome_payload[9]);
	}

	err = bt_le_adv_start(BT_LE_ADV_NCONN, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		LOG_ERR("Advertising failed to start (err %d)", err);
		return 0;
	}

	while (1) {
		k_msleep(ADV_INTERVAL_MS);

		if (read_imu_magnitudes(imu, &accel_mag, &gyro_mag) < 0) {
			continue;
		}

		packet_id++;
		bthome_payload[4] = packet_id;
		sys_put_le16(accel_mag, &bthome_payload[6]);
		sys_put_le16(gyro_mag, &bthome_payload[9]);

		err = bt_le_adv_update_data(ad, ARRAY_SIZE(ad), NULL, 0);
		if (err) {
			LOG_ERR("Failed to update advertising data (err %d)", err);
		}
	}
	return 0;
}
