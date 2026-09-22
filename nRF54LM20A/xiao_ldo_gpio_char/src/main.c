/* Plan-Reduction-Consommation-2026-09-22.md #8.3 -- caracterisation du
 * temps de montee du rail imu_vdd/LDO1 (t_up) selon la methode de
 * commande (registre I2C actuel vs broche P1.25 -> GPIO0 nPM1300),
 * pour decider si l'etape C (commande par broche) est viable. Jamais
 * mesure au PPK2 -- console UART autorisee (regle §4.6 du plan).
 *
 * t_up = temps entre le "front" (declenchement) et le moment ou SDA
 * (P0.08, tiree par R28 vers le rail IMU) lit "1" -- rail > VIH SoC.
 * P0.08 est aussi la ligne SDA du bus I2C IMU (imu_i2c) : le bus est
 * suspendu (PM_DEVICE_ACTION_SUSPEND) le temps de la lecture GPIO brute,
 * puis relance pour la lecture WHO_AM_I qui suit.
 */
#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/mfd/npm13xx.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>

#define PMIC_LDSW_BASE     0x08
#define PMIC_LDSW_TASK1SET 0x00
#define PMIC_LDSW_TASK1CLR 0x01
#define PMIC_LDSW_STATUS   0x04
#define PMIC_LDSW1_GPISEL  0x05

#define LSM6DSL_REG_WHO_AM_I 0x0F
#define LSM6DSL_WHO_AM_I_VAL 0x6A

#define P125_PIN 25 /* gpio1, npm_GPIO0 -> P1.25 */
#define P008_PIN 8  /* gpio0, SDA imu_i2c */

#define N_TRIALS       20
#define POLL_TIMEOUT_US 100000 /* 100 ms, tres large marge */
#define DISCHARGE_MS   1000

static const struct device *const imu_vdd_dev = DEVICE_DT_GET(DT_NODELABEL(imu_vdd));
static const struct device *const pmic_dev = DEVICE_DT_GET(DT_NODELABEL(pmic));
static const struct i2c_dt_spec imu_i2c = I2C_DT_SPEC_GET(DT_ALIAS(imu0));
static const struct device *const gpio0_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
static const struct device *const gpio1_dev = DEVICE_DT_GET(DT_NODELABEL(gpio1));

struct trial_result {
	uint32_t t_up_us;
	bool timed_out;
	bool who_am_i_ok;
};

/* Suspend le bus I2C (liberation du pin SDA), configure P0.08 en entree
 * brute, interroge jusqu'a lecture "1" ou timeout, relance le bus.
 * t0_cycle : instant de reference du "front" (deja capture par l'appelant). */
static uint32_t poll_sda_rise(uint32_t t0_cycle, bool *timed_out)
{
	(void)pm_device_action_run(imu_i2c.bus, PM_DEVICE_ACTION_SUSPEND);
	gpio_pin_configure(gpio0_dev, P008_PIN, GPIO_INPUT);

	uint32_t t1_cycle = t0_cycle;
	*timed_out = true;

	while (1) {
		t1_cycle = k_cycle_get_32();
		uint32_t elapsed_us = k_cyc_to_us_floor32(t1_cycle - t0_cycle);

		if (gpio_pin_get(gpio0_dev, P008_PIN) == 1) {
			*timed_out = false;
			break;
		}
		if (elapsed_us > POLL_TIMEOUT_US) {
			break;
		}
	}

	(void)pm_device_action_run(imu_i2c.bus, PM_DEVICE_ACTION_RESUME);
	return t1_cycle;
}

static bool check_who_am_i(void)
{
	uint8_t who = 0;
	int rc = i2c_reg_read_byte_dt(&imu_i2c, LSM6DSL_REG_WHO_AM_I, &who);

	return (rc == 0) && (who == LSM6DSL_WHO_AM_I_VAL);
}

static void rail_off_gpio(void)
{
	gpio_pin_set(gpio1_dev, P125_PIN, 0);
}

static void rail_off_regulator(void)
{
	regulator_disable(imu_vdd_dev);
}

/* V0 -- regulator_enable() actuel (reference). */
static struct trial_result run_v0(void)
{
	struct trial_result r = {0};
	uint32_t t0 = k_cycle_get_32();

	regulator_enable(imu_vdd_dev);
	uint32_t t1 = poll_sda_rise(t0, &r.timed_out);

	r.t_up_us = k_cyc_to_us_floor32(t1 - t0);
	if (!r.timed_out) {
		k_busy_wait(r.t_up_us < 5000 ? (5000 - r.t_up_us) : 0);
		r.who_am_i_ok = check_who_am_i();
	}
	rail_off_regulator();
	return r;
}

/* V1 -- front P1.25 seul, aucun I2C avant la lecture WHO_AM_I. */
static struct trial_result run_v1(void)
{
	struct trial_result r = {0};
	uint32_t t0 = k_cycle_get_32();

	gpio_pin_set(gpio1_dev, P125_PIN, 1);
	uint32_t t1 = poll_sda_rise(t0, &r.timed_out);

	r.t_up_us = k_cyc_to_us_floor32(t1 - t0);
	if (!r.timed_out) {
		k_busy_wait(r.t_up_us < 5000 ? (5000 - r.t_up_us) : 0);
		r.who_am_i_ok = check_who_am_i();
	}
	rail_off_gpio();
	return r;
}

/* V2 -- front + lecture LDSWSTATUS immediate (contournement errata [38]). */
static struct trial_result run_v2(void)
{
	struct trial_result r = {0};
	uint8_t status;
	uint32_t t0 = k_cycle_get_32();

	gpio_pin_set(gpio1_dev, P125_PIN, 1);
	(void)mfd_npm13xx_reg_read(pmic_dev, PMIC_LDSW_BASE, PMIC_LDSW_STATUS, &status);
	uint32_t t1 = poll_sda_rise(t0, &r.timed_out);

	r.t_up_us = k_cyc_to_us_floor32(t1 - t0);
	if (!r.timed_out) {
		k_busy_wait(r.t_up_us < 5000 ? (5000 - r.t_up_us) : 0);
		r.who_am_i_ok = check_who_am_i();
	}
	rail_off_gpio();
	return r;
}

/* V3 -- front + 1 ms + lecture LDSWSTATUS. */
static struct trial_result run_v3(void)
{
	struct trial_result r = {0};
	uint8_t status;
	uint32_t t0 = k_cycle_get_32();

	gpio_pin_set(gpio1_dev, P125_PIN, 1);
	k_busy_wait(1000);
	(void)mfd_npm13xx_reg_read(pmic_dev, PMIC_LDSW_BASE, PMIC_LDSW_STATUS, &status);
	uint32_t t1 = poll_sda_rise(t0, &r.timed_out);

	r.t_up_us = k_cyc_to_us_floor32(t1 - t0);
	if (!r.timed_out) {
		k_busy_wait(r.t_up_us < 5000 ? (5000 - r.t_up_us) : 0);
		r.who_am_i_ok = check_who_am_i();
	}
	rail_off_gpio();
	return r;
}

/* V4 -- front + adressage seul (ecriture 0 octet a 0x6B). */
static struct trial_result run_v4(void)
{
	struct trial_result r = {0};
	uint32_t t0 = k_cycle_get_32();

	gpio_pin_set(gpio1_dev, P125_PIN, 1);
	(void)i2c_write_dt(&imu_i2c, NULL, 0);
	uint32_t t1 = poll_sda_rise(t0, &r.timed_out);

	r.t_up_us = k_cyc_to_us_floor32(t1 - t0);
	if (!r.timed_out) {
		k_busy_wait(r.t_up_us < 5000 ? (5000 - r.t_up_us) : 0);
		r.who_am_i_ok = check_who_am_i();
	}
	rail_off_gpio();
	return r;
}

static void run_variant(const char *name, struct trial_result (*fn)(void))
{
	uint32_t sum_us = 0, min_us = UINT32_MAX, max_us = 0;
	int n_ok = 0, n_timeout = 0, n_who_ok = 0;

	printf("\n=== %s ===\n", name);
	for (int i = 0; i < N_TRIALS; i++) {
		struct trial_result r = fn();

		if (r.timed_out) {
			printf("  essai %2d : TIMEOUT (> %d us)\n", i + 1, POLL_TIMEOUT_US);
			n_timeout++;
		} else {
			printf("  essai %2d : t_up=%5u us  WHO_AM_I=%s\n", i + 1, r.t_up_us,
			       r.who_am_i_ok ? "OK" : "FAIL");
			sum_us += r.t_up_us;
			if (r.t_up_us < min_us) {
				min_us = r.t_up_us;
			}
			if (r.t_up_us > max_us) {
				max_us = r.t_up_us;
			}
			n_ok++;
			if (r.who_am_i_ok) {
				n_who_ok++;
			}
		}
		k_msleep(DISCHARGE_MS);
	}
	printf("%s : %d/%d sans timeout, WHO_AM_I OK %d/%d", name, n_ok, N_TRIALS, n_who_ok,
	       N_TRIALS);
	if (n_ok > 0) {
		printf(", t_up min=%u max=%u moy=%u us", min_us, max_us, sum_us / n_ok);
	}
	printf("\n");
}

int main(void)
{
	k_msleep(500);
	printf("\n\nxiao_ldo_gpio_char -- Plan-Reduction-Consommation-2026-09-22.md #8.3\n");

	gpio_pin_configure(gpio1_dev, P125_PIN, GPIO_OUTPUT_INACTIVE);

	/* Etat connu au demarrage : GPISEL=0 (V0 = comportement actuel, pur
	 * I2C), LDO1 force a l'arret. */
	(void)mfd_npm13xx_reg_write(pmic_dev, PMIC_LDSW_BASE, PMIC_LDSW1_GPISEL, 0);
	(void)mfd_npm13xx_reg_write(pmic_dev, PMIC_LDSW_BASE, PMIC_LDSW_TASK1CLR, 1);
	k_msleep(DISCHARGE_MS);

	run_variant("V0 (regulator_enable actuel)", run_v0);

	/* GPISEL=1 une seule fois, pour toutes les variantes GPIO qui suivent. */
	(void)mfd_npm13xx_reg_write(pmic_dev, PMIC_LDSW_BASE, PMIC_LDSW1_GPISEL, 1);
	(void)mfd_npm13xx_reg_write(pmic_dev, PMIC_LDSW_BASE, PMIC_LDSW_TASK1CLR, 1);
	k_msleep(DISCHARGE_MS);

	run_variant("V1 (front seul, aucun I2C)", run_v1);
	run_variant("V2 (front + lecture LDSWSTATUS immediate)", run_v2);
	run_variant("V3 (front + 1 ms + lecture)", run_v3);
	run_variant("V4 (front + adressage seul)", run_v4);

	/* Retour a GPISEL=0 : etat par defaut, coherent avec V0 si ce
	 * firmware est relance. */
	(void)mfd_npm13xx_reg_write(pmic_dev, PMIC_LDSW_BASE, PMIC_LDSW1_GPISEL, 0);

	printf("\n=== Termine -- decision : variante la moins couteuse avec t_up <= 2000 us\n"
	       "sur 20/20 et WHO_AM_I OK. Voir plan #8.3. ===\n");
	while (1) {
		k_msleep(60000);
	}
	return 0;
}
