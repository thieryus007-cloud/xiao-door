/*
 * Firmware de diagnostic TEMPORAIRE -- test 3/3, demande explicite
 * utilisateur (2026-09-19), voir test1_rail_only/src/main.c pour le
 * contexte complet.
 *
 * Objectif de CE test : imu_vdd/LDO1 active UNE FOIS et plus jamais
 * coupe (contrairement a sample_motion() en production, qui coupe/
 * rallume le rail a chaque cycle de 1 s). Entre deux lectures, seule la
 * puce elle-meme est mise en power-down (CTRL1_XL=0), puis reconfiguree
 * (ODR 208 Hz + XL_HM_MODE, meme delai de stabilisation 40 ms que la
 * production) avant chaque lecture. Teste directement si "ne plus
 * jamais couper le rail, seulement la puce" fait mieux ou moins bien
 * que la strategie actuelle (coupure du rail a chaque cycle, ~20-22 uA
 * mesures en production).
 *
 * Attente enoncee a l'avance, a verifier -- pas une conclusion : si le
 * test 1 confirme que le rail seul (puce en power-down, jamais touchee
 * par I2C) consomme deja ~240-270 uA en continu, ce test-ci -- qui garde
 * la puce en power-down l'essentiel du temps aussi -- devrait donner un
 * chiffre proche de ce meme plancher en continu, donc tres au-dessus
 * des ~20-22 uA actuels (moyenne obtenue en coupant le rail, pas en le
 * laissant actif). Si au contraire ce test donne un chiffre proche de
 * l'actuel ~20-22 uA malgre le rail jamais coupe, ce serait une
 * observation nouvelle et importante, a l'oppose de l'hypothese
 * ci-dessus.
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

#define LSM6DSL_REG_CTRL1_XL       0x10
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

	regulator_enable(imu_vdd_dev); /* active une seule fois, plus jamais coupe */
	k_msleep(5);

	i2c_reg_write_byte_dt(&imu_i2c, LSM6DSL_REG_CTRL3_C,
			       LSM6DSL_CTRL3_C_BDU | LSM6DSL_CTRL3_C_IF_INC);

	if (!device_is_ready(imu_dev)) {
		device_init(imu_dev);
	}

	while (1) {
		struct sensor_value x, y, z;
		struct sensor_value odr_attr = { .val1 = 208, .val2 = 0 };

		i2c_reg_update_byte_dt(&imu_i2c, LSM6DSL_REG_CTRL6_C,
					LSM6DSL_CTRL6_C_XL_HM_MODE, LSM6DSL_CTRL6_C_XL_HM_MODE);
		sensor_attr_set(imu_dev, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY,
				 &odr_attr);
		k_msleep(40);

		sensor_sample_fetch_chan(imu_dev, SENSOR_CHAN_ACCEL_XYZ);
		sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_X, &x);
		sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_Y, &y);
		sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_Z, &z);

		/* imu_vdd reste actif -- seule la puce repasse en power-down
		 * (ODR=0) entre deux lectures. */
		i2c_reg_write_byte_dt(&imu_i2c, LSM6DSL_REG_CTRL1_XL, 0x00);

		k_sleep(K_MSEC(1000));
	}

	return 0;
}
