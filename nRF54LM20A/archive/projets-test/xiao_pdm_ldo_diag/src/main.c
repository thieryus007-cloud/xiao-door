/*
 * Firmware de diagnostic -- Audit-Consommation-2026-09-13.md, §6.
 * Aucune fonctionnalite de production. Boucle infinie de 6 segments
 * longs (15-20 s chacun) pour une lecture PPK2 sans ambiguite -- meme
 * methode que le rapport Nordic §8.1 (une seule capture continue,
 * segments clairement delimites par leurs transitoires).
 *
 * Segment 0 : imu_vdd OFF (ligne de base, attendu ~3-4 uA).
 * Segment 1 : imu_vdd ON, CTRL3_C ecrit (H_LACTIVE=1), PDM_CLK bas fixe
 *             DES LE DEBUT -- reproduction exacte du test archive
 *             2026-08-27 (nPM1300-LDO1-quiescent-current-issue.md).
 *             Sert de controle : si ce segment ne reproduit PAS le
 *             ~239 uA deja mesure, la reconstruction de ce firmware a
 *             elle-meme un ecart (voir §4 de l'audit) et le resultat du
 *             segment 2 ne peut pas etre interprete avec confiance.
 * Segment 2 : meme etat imu_vdd, mais PDM_CLK bascule en salve (~500 kHz
 *             vise, cf. limite §2.4 -- pas de PWM materiel, bit-banging
 *             logiciel) pendant >10 ms (Mode-Change Time documente, fiche
 *             MSM261D3526H1CPM p.3) puis repose bas fixe -- teste
 *             l'hypothese §2 : le micro n'atteint peut-etre jamais son
 *             Sleep Mode documente (1 uA) faute d'etre jamais passe par
 *             un etat caracterise (Standard Performance Mode) au
 *             prealable.
 * Segment 3 : imu_vdd OFF (controle, retour ligne de base).
 * Segment 4 : LDO2 seul active (aucune charge connue sur ce board, voir
 *             §3) -- isole le courant propre du bloc LDO du PMIC, sans
 *             le micro.
 * Segment 5 : LDO2 OFF (controle, retour ligne de base).
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>

static const struct device *const imu_vdd_dev = DEVICE_DT_GET(DT_NODELABEL(imu_vdd));
static const struct device *const ldo2_dev = DEVICE_DT_GET(DT_NODELABEL(ldo2_diag));
static const struct device *const gpio1 = DEVICE_DT_GET(DT_NODELABEL(gpio1));
static const struct i2c_dt_spec imu_i2c = I2C_DT_SPEC_GET(DT_ALIAS(imu0));

/* Hypothese #2 (voir prj.conf) -- repris tel quel de xiao_door_sensor/src/
 * main.c (suspend_external_flash()/configure_spi_pins_for_system_off()),
 * jamais appele dans la premiere version de ce diagnostic. */
static const struct device *const flash_dev = DEVICE_DT_GET(DT_NODELABEL(py25q64));
static const struct device *const flash_bus = DEVICE_DT_GET(DT_BUS(DT_NODELABEL(py25q64)));

static void configure_spi_pins_for_system_off(void)
{
	const struct device *gpio2 = DEVICE_DT_GET(DT_NODELABEL(gpio2));

	if (!device_is_ready(gpio2)) {
		return;
	}
	gpio_pin_configure(gpio2, 5, GPIO_OUTPUT_HIGH);
	gpio_pin_configure(gpio2, 0, GPIO_OUTPUT_HIGH);
	gpio_pin_configure(gpio2, 3, GPIO_OUTPUT_HIGH);
	gpio_pin_configure(gpio2, 1, GPIO_OUTPUT_LOW);
	gpio_pin_configure(gpio2, 2, GPIO_OUTPUT_LOW);
	gpio_pin_configure(gpio2, 4, GPIO_INPUT | GPIO_PULL_DOWN);
}

static void suspend_external_flash(void)
{
	if (device_is_ready(flash_dev)) {
		pm_device_action_run(flash_dev, PM_DEVICE_ACTION_SUSPEND);
	}
	if (device_is_ready(flash_bus)) {
		pm_device_action_run(flash_bus, PM_DEVICE_ACTION_SUSPEND);
	}
	configure_spi_pins_for_system_off();
}

#define PIN_PDM_CLK 13U /* P1.13, gpio1 -- meme broche que
			  * configure_pdm_pins_for_system_off() en
			  * production (jamais appelee actuellement). */

#define LSM6DSL_REG_CTRL3_C    0x12U
#define LSM6DSL_CTRL3_C_BDU    BIT(6)
#define LSM6DSL_CTRL3_C_H_LACTIVE BIT(5)
#define LSM6DSL_CTRL3_C_IF_INC BIT(2)

#define SEGMENT_MS 18000

/* Bit-banging logiciel -- PAS de PWM materiel, frequence non garantie ni
 * verifiee a l'oscilloscope (limite documentee, §2.4 de l'audit). Vise le
 * milieu de la plage Low-Power du micro (150-900 kHz, fiche p.3/p.6) :
 * k_busy_wait(1) par demi-periode ~ 500 kHz nominal, plage large (facteur
 * 6) volontairement choisie pour absorber l'imprecision logicielle. Duree
 * totale ~20 ms >> Mode-Change Time documente (max 10 ms, fiche p.3). */
static void pdm_clk_wake_burst(void)
{
	for (int i = 0; i < 10000; i++) {
		gpio_pin_set_raw(gpio1, PIN_PDM_CLK, 1);
		k_busy_wait(1);
		gpio_pin_set_raw(gpio1, PIN_PDM_CLK, 0);
		k_busy_wait(1);
	}
	/* ~20 ms de bascule (10000 x 2 us nominal) -- couvre le
	 * Mode-Change Time documente (max 10 ms) avec marge x2. */
}

static void write_ctrl3_c_h_lactive(void)
{
	(void)i2c_reg_write_byte_dt(&imu_i2c, LSM6DSL_REG_CTRL3_C,
				     LSM6DSL_CTRL3_C_BDU | LSM6DSL_CTRL3_C_H_LACTIVE |
					     LSM6DSL_CTRL3_C_IF_INC);
}

/* Question Nordic (2026-09-13, Simon) : "confirm which voltages you're
 * using for VOUT2 and LDO1 -- in your .dts file LDO1 is set to 1.8V".
 * Le fichier source actuel indique 3,3V partout (aucune trace de 1,8V
 * dans tout l'historique du projet, verifie par recherche exhaustive) --
 * mais les registres I2C du PMIC persistent a travers les flashs du SoC
 * (fait deja etabli sur ce projet). Lecture directe du registre reel
 * (LDSW_OFFSET_VOUTSEL via regulator_get_voltage(), pas une supposition
 * basee sur le devicetree) pour trancher empiriquement. Ecrit en RAM,
 * jamais desactive/reactive -- lu ensuite par SWD (openocd mdw), pas de
 * console. */
static volatile int32_t ldo1_voltage_uv_readback = -1;
static volatile int ldo1_voltage_readback_rc = -1;

int main(void)
{
	if (!device_is_ready(gpio1)) {
		return 0;
	}
	gpio_pin_configure(gpio1, PIN_PDM_CLK, GPIO_OUTPUT_LOW);
	suspend_external_flash();

	if (device_is_ready(imu_vdd_dev)) {
		int32_t v;
		int rc = regulator_get_voltage(imu_vdd_dev, &v);

		ldo1_voltage_readback_rc = rc;
		ldo1_voltage_uv_readback = (rc == 0) ? v : -1;
	}

	while (1) {
		/* Segment 0 -- ligne de base. */
		if (device_is_ready(imu_vdd_dev)) {
			regulator_disable(imu_vdd_dev);
		}
		k_msleep(SEGMENT_MS);

		/* Segment 1 -- reproduction exacte du test archive
		 * 2026-08-27 : PDM_CLK deja bas AVANT regulator_enable(),
		 * jamais d'episode d'horloge. */
		gpio_pin_set_raw(gpio1, PIN_PDM_CLK, 0);
		if (device_is_ready(imu_vdd_dev)) {
			regulator_enable(imu_vdd_dev);
		}
		/* Power-up Time documente du micro : typ. 6 ms, max 20 ms
		 * (fiche MSM261D3526H1CPM p.3). Soft-start LDO nPM1300 (voir
		 * production, 5 ms) inclus dans cette marge. */
		k_msleep(20);
		write_ctrl3_c_h_lactive();
		k_msleep(SEGMENT_MS);

		/* Segment 2 -- sequence de reveil proposee (§2.3) : salve
		 * d'horloge dans la plage Low-Power/Standard, PUIS repos bas
		 * fixe (plage Sleep, <=50 kHz). imu_vdd reste allume sans
		 * coupure entre segment 1 et 2 -- seule variable qui change
		 * est la sequence PDM_CLK. */
		pdm_clk_wake_burst();
		gpio_pin_set_raw(gpio1, PIN_PDM_CLK, 0);
		/* Fall-asleep Time documente : max 30 us -- k_msleep(1)
		 * (granularite tick) largement suffisant. */
		k_msleep(1);
		k_msleep(SEGMENT_MS);

		/* Segment 3 -- controle, retour ligne de base. */
		if (device_is_ready(imu_vdd_dev)) {
			regulator_disable(imu_vdd_dev);
		}
		gpio_pin_set_raw(gpio1, PIN_PDM_CLK, 0);
		k_msleep(SEGMENT_MS);

		/* Segment 4 -- LDO2 seul, aucune charge connue sur ce board
		 * (§3). imu_vdd reste OFF (deja fait au segment 3). */
		if (device_is_ready(ldo2_dev)) {
			regulator_enable(ldo2_dev);
		}
		k_msleep(SEGMENT_MS);

		/* Segment 5 -- controle, retour ligne de base. */
		if (device_is_ready(ldo2_dev)) {
			regulator_disable(ldo2_dev);
		}
		k_msleep(SEGMENT_MS);
	}

	return 0;
}
