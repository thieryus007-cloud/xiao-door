/*
 * Firmware de diagnostic TEMPORAIRE -- test 2/3, demande explicite
 * utilisateur (2026-09-19), voir test1_rail_only/src/main.c pour le
 * contexte complet.
 *
 * Objectif de CE test : imu_vdd/LDO1 active UNE FOIS et plus jamais
 * coupe (comme test 1), mais la puce est cette fois configuree
 * (CTRL3_C: BDU+IF_INC, CTRL6_C: XL_HM_MODE -- memes bits que
 * sample_motion() en production) et echantillonnee en continu a
 * ODR=208 Hz, toutes les 1 s, jamais mise en power-down entre deux
 * lectures. N'a jamais ete mesure isolement dans le rapport Nordic (le
 * test d'isolation #6 a seulement verifie que XL_HM_MODE etait bien
 * positionne par relecture registre, sans chiffre a part).
 *
 * Attente enoncee a l'avance, a verifier -- pas une conclusion : si le
 * ~250-300 uA du rail est independant de l'activite de la puce (comme
 * le suggere le test #9 du rapport, rail seul sans I2C), ce test
 * devrait donner un chiffre proche du test 1 (memes ~240-270 uA). Si le
 * trafic I2C/l'activite de la puce ajoute un cout significatif non vu
 * jusqu'ici, ce test donnerait un chiffre nettement plus eleve que le
 * test 1 -- observation nouvelle a explorer.
 *
 * NE JAMAIS laisser ce firmware flashe apres la mesure -- reflasher
 * l'image d'or de production immediatement.
 */
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <ram_pwrdn.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/addr.h>

static const struct device *const imu_vdd_dev = DEVICE_DT_GET(DT_NODELABEL(imu_vdd));
static const struct device *const imu_dev = DEVICE_DT_GET(DT_ALIAS(imu0));
static const struct i2c_dt_spec imu_i2c = I2C_DT_SPEC_GET(DT_ALIAS(imu0));

/* Repris tel quel de xiao_door_sensor/src/main.c -- meme correctif que
 * test1 (baseline 37,6 uA vs ~3,3-4,3 uA attendus, cause plausible :
 * broches LED/SPI externe non mises en etat bas-fuite). */
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec led_blue = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

static void release_led(const struct gpio_dt_spec *led, const char *name)
{
	int rc;

	if (!gpio_is_ready_dt(led)) {
		return;
	}

	rc = gpio_pin_configure(led->port, led->pin, GPIO_DISCONNECTED);
	if (rc < 0) {
		printf("Warning: could not disconnect %s (%d)\n", name, rc);
	}
}

static void release_led_gpios(void)
{
	release_led(&led_red, "red LED");
	release_led(&led_blue, "blue LED");
	release_led(&led_green, "green LED");
}

static int configure_spi_pins_for_system_off(void)
{
	const struct device *gpio2 = DEVICE_DT_GET(DT_NODELABEL(gpio2));
	int rc;

	if (!device_is_ready(gpio2)) {
		printf("GPIO2 not ready.\n");
		return -ENODEV;
	}

	rc = gpio_pin_configure(gpio2, 5, GPIO_OUTPUT_HIGH);
	if (rc < 0) {
		return rc;
	}
	rc = gpio_pin_configure(gpio2, 0, GPIO_OUTPUT_HIGH);
	if (rc < 0) {
		return rc;
	}
	rc = gpio_pin_configure(gpio2, 3, GPIO_OUTPUT_HIGH);
	if (rc < 0) {
		return rc;
	}
	rc = gpio_pin_configure(gpio2, 1, GPIO_OUTPUT_LOW);
	if (rc < 0) {
		return rc;
	}
	rc = gpio_pin_configure(gpio2, 2, GPIO_OUTPUT_LOW);
	if (rc < 0) {
		return rc;
	}
	rc = gpio_pin_configure(gpio2, 4, GPIO_INPUT | GPIO_PULL_DOWN);
	if (rc < 0) {
		return rc;
	}

	return 0;
}

#define LSM6DSL_REG_CTRL3_C        0x12
#define LSM6DSL_CTRL3_C_BDU        BIT(6)
#define LSM6DSL_CTRL3_C_IF_INC     BIT(2)
#define LSM6DSL_REG_CTRL6_C        0x15
#define LSM6DSL_CTRL6_C_XL_HM_MODE BIT(4)

static int set_fixed_ble_identity(void)
{
	bt_addr_le_t addr = { .type = BT_ADDR_LE_RANDOM };
	uint8_t hw_id[8];
	ssize_t len = hwinfo_get_device_id(hw_id, sizeof(hw_id));

	if (len < (ssize_t)sizeof(addr.a.val)) {
		return 0;
	}
	memcpy(addr.a.val, hw_id, sizeof(addr.a.val));
	addr.a.val[5] |= 0xC0;
	return bt_id_create(&addr, NULL);
}

int main(void)
{
	release_led_gpios();
	configure_spi_pins_for_system_off();
	set_fixed_ble_identity();
	bt_enable(NULL);
	power_down_unused_ram();

	k_msleep(15000);

	regulator_enable(imu_vdd_dev);
	k_msleep(5);

	i2c_reg_write_byte_dt(&imu_i2c, LSM6DSL_REG_CTRL3_C,
			       LSM6DSL_CTRL3_C_BDU | LSM6DSL_CTRL3_C_IF_INC);
	i2c_reg_update_byte_dt(&imu_i2c, LSM6DSL_REG_CTRL6_C,
				LSM6DSL_CTRL6_C_XL_HM_MODE, LSM6DSL_CTRL6_C_XL_HM_MODE);

	if (!device_is_ready(imu_dev)) {
		device_init(imu_dev);
	}

	struct sensor_value odr_attr = { .val1 = 208, .val2 = 0 };

	sensor_attr_set(imu_dev, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr_attr);
	k_msleep(40);

	/* Rail et ODR laisses actifs indefiniment a partir d'ici -- jamais
	 * coupes, jamais remis en power-down. Lecture periodique seulement,
	 * pour garder une activite I2C representative d'un cycle reel. */
	while (1) {
		struct sensor_value x, y, z;

		sensor_sample_fetch_chan(imu_dev, SENSOR_CHAN_ACCEL_XYZ);
		sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_X, &x);
		sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_Y, &y);
		sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_Z, &z);

		k_sleep(K_MSEC(1000));
	}

	return 0;
}
