#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/poweroff.h>

LOG_MODULE_REGISTER(imu_wakeup, LOG_LEVEL_INF);

#define IMU_NODE DT_ALIAS(imu0)
#define IMU_ADDR DT_REG_ADDR(IMU_NODE)

/* LSM6DS3TR-C register definitions for tap detection */
#define LSM6DSL_REG_INT1_CTRL 0x0D
#define LSM6DSL_REG_TAP_SRC 0x1C
#define LSM6DSL_REG_TAP_CFG 0x58
#define LSM6DSL_REG_TAP_THS_6D 0x59
#define LSM6DSL_REG_INT_DUR2 0x5A
#define LSM6DSL_REG_WAKE_UP_THS 0x5B
#define LSM6DSL_REG_MD1_CFG 0x5E

#define TAP_CFG_INTERRUPTS_ENABLE BIT(7)
#define TAP_CFG_X_EN BIT(3)
#define TAP_CFG_Y_EN BIT(2)
#define TAP_CFG_Z_EN BIT(1)
#define TAP_CFG_LIR BIT(0)

#define MD1_CFG_INT1_SINGLE_TAP BIT(6)
#define MD1_CFG_INT1_DOUBLE_TAP BIT(3)

#define WAKE_UP_THS_SINGLE_DOUBLE_TAP BIT(7)

#define TAP_SRC_TAP_IA BIT(6)
#define TAP_SRC_SINGLE_TAP BIT(5)
#define TAP_SRC_DOUBLE_TAP BIT(4)

enum posture_state
{
	POSTURE_UNKNOWN,
	POSTURE_FACE_UP,
	POSTURE_FACE_DOWN,
	POSTURE_LEFT_TILT,
	POSTURE_RIGHT_TILT,
	POSTURE_UPRIGHT,
	POSTURE_INVERTED,
};

/* Test #21 (2026-08-27) -- vrai System OFF (sys_poweroff()) au lieu de
 * la boucle infinie k_sem_take(K_FOREVER) de l'exemple original.
 * Reveil arme directement sur la broche INT1 de l'IMU (meme propriete
 * devicetree irq-gpios que le driver LSM6DSL utilise en interne),
 * meme mecanisme que l'exemple officiel Zephyr
 * samples/boards/nordic/system_off (gpio_pin_interrupt_configure_dt
 * + GPIO_INT_LEVEL_ACTIVE avant sys_poweroff()). Le SoC redemarre
 * entierement au reveil (pas de retour dans la boucle C) -- reset_cause
 * indique RESET_LOW_POWER_WAKE. */
static const struct gpio_dt_spec imu_int1 = GPIO_DT_SPEC_GET(IMU_NODE, irq_gpios);

/*
 * nRF54LM20A needs power_en (fixed regulator on gpio1.12) and imu_vdd
 * (PMIC NPM1300 LDO1) enabled before the IMU can be accessed.
 */
#if defined(DT_N_NODELABEL_power_en)
static const struct device *const power_en_dev =
	DEVICE_DT_GET(DT_NODELABEL(power_en));
#endif

#if defined(DT_N_NODELABEL_imu_vdd)
static const struct device *const imu_vdd_dev =
	DEVICE_DT_GET(DT_NODELABEL(imu_vdd));
#endif

static int enable_imu_power(void)
{
#if defined(DT_N_NODELABEL_power_en) || defined(DT_N_NODELABEL_imu_vdd)
	int ret;
#endif

#if defined(DT_N_NODELABEL_power_en)
	if (!device_is_ready(power_en_dev))
	{
		LOG_ERR("power_en regulator is not ready");
		return -ENODEV;
	}
	ret = regulator_enable(power_en_dev);
	if (ret < 0 && ret != -EALREADY)
	{
		LOG_ERR("Failed to enable power_en: %d", ret);
		return ret;
	}
#endif

#if defined(DT_N_NODELABEL_imu_vdd)
	if (!device_is_ready(imu_vdd_dev))
	{
		LOG_ERR("imu_vdd regulator is not ready");
		return -ENODEV;
	}
	ret = regulator_enable(imu_vdd_dev);
	if (ret < 0 && ret != -EALREADY)
	{
		LOG_ERR("Failed to enable imu_vdd: %d", ret);
		return ret;
	}
#endif

#if defined(DT_N_NODELABEL_power_en) || defined(DT_N_NODELABEL_imu_vdd)
	/* Wait for power rail to stabilize */
	k_sleep(K_MSEC(20));
#endif

	return 0;
}

static inline float out_ev(struct sensor_value *val)
{
	return (val->val1 + (float)val->val2 / 1000000);
}

static enum posture_state determine_posture(float ax, float ay, float az)
{
	const float threshold = 6.87f;

	if (az > threshold)
	{
		return POSTURE_FACE_UP;
	}
	if (az < -threshold)
	{
		return POSTURE_FACE_DOWN;
	}
	if (ax > threshold)
	{
		return POSTURE_RIGHT_TILT;
	}
	if (ax < -threshold)
	{
		return POSTURE_LEFT_TILT;
	}
	if (ay > threshold)
	{
		return POSTURE_UPRIGHT;
	}
	if (ay < -threshold)
	{
		return POSTURE_INVERTED;
	}
	return POSTURE_UNKNOWN;
}

static const char *posture_to_str(enum posture_state posture)
{
	switch (posture)
	{
	case POSTURE_FACE_UP:
		return "Face up";
	case POSTURE_FACE_DOWN:
		return "Face down";
	case POSTURE_LEFT_TILT:
		return "Left tilt";
	case POSTURE_RIGHT_TILT:
		return "Right tilt";
	case POSTURE_UPRIGHT:
		return "Upright";
	case POSTURE_INVERTED:
		return "Inverted";
	default:
		return "Unknown";
	}
}

static enum posture_state read_posture(const struct device *dev)
{
	struct sensor_value x, y, z;

	sensor_sample_fetch_chan(dev, SENSOR_CHAN_ACCEL_XYZ);
	sensor_channel_get(dev, SENSOR_CHAN_ACCEL_X, &x);
	sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Y, &y);
	sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Z, &z);

	return determine_posture(out_ev(&x), out_ev(&y), out_ev(&z));
}

static int imu_reg_write(const struct device *i2c, uint8_t reg, uint8_t val)
{
	return i2c_reg_write_byte(i2c, IMU_ADDR, reg, val);
}

static int imu_reg_read(const struct device *i2c, uint8_t reg, uint8_t *val)
{
	return i2c_reg_read_byte(i2c, IMU_ADDR, reg, val);
}

static int configure_tap_detection(const struct device *i2c,
								   const struct device *imu_dev)
{
	int ret;
	struct sensor_value odr_attr;

	/* Set accelerometer ODR to 104 Hz for reliable tap detection */
	odr_attr.val1 = 104;
	odr_attr.val2 = 0;
	ret = sensor_attr_set(imu_dev, SENSOR_CHAN_ACCEL_XYZ,
						  SENSOR_ATTR_SAMPLING_FREQUENCY, &odr_attr);
	if (ret != 0)
	{
		LOG_ERR("Failed to set accel ODR: %d", ret);
		return ret;
	}

	/*
	 * TAP_CFG: enable interrupts, X/Y/Z tap, latched interrupt mode.
	 * Latched mode keeps INT1 asserted until TAP_SRC is read.
	 */
	ret = imu_reg_write(i2c, LSM6DSL_REG_TAP_CFG,
						TAP_CFG_INTERRUPTS_ENABLE |
							TAP_CFG_X_EN |
							TAP_CFG_Y_EN |
							TAP_CFG_Z_EN |
							TAP_CFG_LIR);
	if (ret != 0)
	{
		return ret;
	}

	/* TAP_THS_6D: tap threshold = 0x0C (~750 mg at 2g full scale) */
	ret = imu_reg_write(i2c, LSM6DSL_REG_TAP_THS_6D, 0x0C);
	if (ret != 0)
	{
		return ret;
	}

	/*
	 * INT_DUR2: DUR=4, QUIET=2, SHOCK=1
	 * SHOCK: max shock time for tap detection
	 * QUIET: min quiet time after shock before another tap
	 * DUR: max duration of tap event
	 */
	ret = imu_reg_write(i2c, LSM6DSL_REG_INT_DUR2,
						(4 << 4) | (2 << 2) | (1 << 0));
	if (ret != 0)
	{
		return ret;
	}

	/* WAKE_UP_THS: enable both single and double tap detection */
	ret = imu_reg_write(i2c, LSM6DSL_REG_WAKE_UP_THS,
						WAKE_UP_THS_SINGLE_DOUBLE_TAP);
	if (ret != 0)
	{
		return ret;
	}

	/*
	 * Clear INT1_CTRL data-ready bits so INT1 only fires for tap events.
	 * The Zephyr driver sets DRDY_XL | DRDY_G during init; we override
	 * this to prevent periodic wakeups and achieve ultra low power.
	 */
	ret = imu_reg_write(i2c, LSM6DSL_REG_INT1_CTRL, 0x00);
	if (ret != 0)
	{
		return ret;
	}

	/* MD1_CFG: route single tap and double tap to INT1 pin */
	ret = imu_reg_write(i2c, LSM6DSL_REG_MD1_CFG,
						MD1_CFG_INT1_SINGLE_TAP |
							MD1_CFG_INT1_DOUBLE_TAP);
	if (ret != 0)
	{
		return ret;
	}

	LOG_INF("Tap detection configured on IMU hardware");
	return 0;
}

int main(void)
{
	const struct device *const imu_dev = DEVICE_DT_GET(IMU_NODE);
	const struct device *const i2c_dev = DEVICE_DT_GET(DT_BUS(IMU_NODE));
	uint32_t reset_cause = 0U;
	int ret;

	LOG_INF("Low Power IMU Wakeup System (System OFF)");
	LOG_INF("=========================================");

	hwinfo_get_reset_cause(&reset_cause);
	if (reset_cause & RESET_LOW_POWER_WAKE) {
		LOG_INF("Wakeup from System OFF by GPIO (tap).");
	} else if (reset_cause & RESET_DEBUG) {
		LOG_INF("Reset by debugger.");
	} else {
		LOG_INF("Cold boot / power-on, reset_cause=0x%08" PRIX32, reset_cause);
	}

	/*
	 * On nRF54LM20A, enable power_en + imu_vdd before accessing IMU.
	 * The IMU has zephyr,deferred-init; must init manually.
	 */
	ret = enable_imu_power();
	if (ret < 0)
	{
		LOG_ERR("Failed to enable IMU power: %d", ret);
		return 1;
	}

	if (!device_is_ready(imu_dev))
	{
		ret = device_init(imu_dev);
		if (ret < 0 && ret != -EALREADY)
		{
			LOG_ERR("Failed to initialize %s: %d", imu_dev->name, ret);
			return 1;
		}
	}

	if (!device_is_ready(imu_dev))
	{
		LOG_ERR("%s: device not ready", imu_dev->name);
		return 1;
	}
	LOG_INF("IMU device ready");

	if (!device_is_ready(i2c_dev))
	{
		LOG_ERR("I2C device not ready");
		return 1;
	}

	/* Si ce reveil vient d'un tap reel, lire TAP_SRC (efface le
	 * verrou LIR) et la posture avant de reconfigurer/rearmer. */
	if (reset_cause & RESET_LOW_POWER_WAKE)
	{
		uint8_t tap_src = 0;

		ret = imu_reg_read(i2c_dev, LSM6DSL_REG_TAP_SRC, &tap_src);
		if (ret == 0)
		{
			if (tap_src & TAP_SRC_SINGLE_TAP)
			{
				LOG_INF("Tap event: Single tap");
			}
			else if (tap_src & TAP_SRC_DOUBLE_TAP)
			{
				LOG_INF("Tap event: Double tap");
			}
			else
			{
				LOG_INF("Tap event: Unknown (TAP_SRC=0x%02x)", tap_src);
			}

			enum posture_state posture = read_posture(imu_dev);
			LOG_INF("Posture: %s", posture_to_str(posture));
		}
		else
		{
			LOG_ERR("Failed to read TAP_SRC: %d", ret);
		}
	}

	/* Configure hardware tap detection on the IMU -- sequence Seeed
	 * inchangee, voir configure_tap_detection(). */
	ret = configure_tap_detection(i2c_dev, imu_dev);
	if (ret != 0)
	{
		LOG_ERR("Failed to configure tap detection: %d", ret);
		return 1;
	}

	/* Armer INT1 comme source de reveil System OFF (meme mecanisme que
	 * samples/boards/nordic/system_off : niveau actif, pas front). */
	ret = gpio_pin_configure_dt(&imu_int1, GPIO_INPUT);
	if (ret < 0)
	{
		LOG_ERR("Could not configure IMU INT1 GPIO (%d)", ret);
		return 1;
	}
	ret = gpio_pin_interrupt_configure_dt(&imu_int1, GPIO_INT_LEVEL_ACTIVE);
	if (ret < 0)
	{
		LOG_ERR("Could not configure IMU INT1 wake interrupt (%d)", ret);
		return 1;
	}

	LOG_INF("Entering System OFF; tap the board to wake.");

	hwinfo_clear_reset_cause();
	sys_poweroff();

	return 0;
}
