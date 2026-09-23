/*
 * Firmware de diagnostic TEMPORAIRE -- test 1/3, demande explicite
 * utilisateur (2026-09-19) : "sleep mode" nPM1300 LOADSW1/LDO1 - piste
 * fermee par lecture du driver (pas de retention mode pour LDSW, voir
 * regulator_npm13xx.c), donc redemarrage des 3 tests d'isolation
 * restants a la place, SANS PREJUGEMENT du resultat.
 *
 * Objectif de CE test : reproduire exactement la mesure du rapport
 * Nordic §8.1 (regulator_enable() sur imu_vdd/LDO1, plus jamais touche
 * ensuite, ZERO trafic I2C vers la puce -- elle reste a son etat de
 * reset materiel = power-down) mais sur l'unite #02 (S/N pont SWD
 * 9C4A557D) au lieu de #01, pour verification croisee.
 *
 * Attendu sur #01 (rapport Nordic §8.1, deja mesure) : ~3.90 uA rail
 * coupe, ~253.98-254.06 uA rail active en regime etabli. Hypothese a
 * verifier ici, pas une conclusion : #02 devrait donner un chiffre du
 * meme ordre (variance normale de session a session), puisque le
 * rapport indique deja que l'anomalie est reproduite sur plusieurs
 * unites et meme sur l'exemple de reference Seeed. Un ecart net
 * infirmerait cette hypothese.
 *
 * NE JAMAIS laisser ce firmware flashe apres la mesure -- reflasher
 * l'image d'or de production immediatement, voir Configuration-
 * nRF54LM20A-System-ON-IDLE.md §4.
 */
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/kernel.h>
#include <ram_pwrdn.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/addr.h>

static const struct device *const imu_vdd_dev = DEVICE_DT_GET(DT_NODELABEL(imu_vdd));

/* Repris tel quel de xiao_door_sensor/src/main.c -- omis par erreur dans la
 * premiere version de ce firmware de diagnostic, cause plausible de l'ecart
 * de baseline observe (37,6 uA mesures contre ~3,3-4,3 uA documentes en
 * §8.1, meme protocole, meme architecture). */
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

	/* Fenetre baseline (imu_vdd coupe) pour voir la transition sur la
	 * meme capture PPK2 que le regime etabli qui suit. */
	k_msleep(15000);

	regulator_enable(imu_vdd_dev);
	/* A partir d'ici : imu_vdd reste actif indefiniment, aucun acces
	 * I2C vers la puce n'est jamais effectue -- elle reste a son etat
	 * de reset materiel (power-down). */

	while (1) {
		k_sleep(K_MSEC(1000));
	}

	return 0;
}
