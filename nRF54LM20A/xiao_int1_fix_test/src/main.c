/*
 * Test isole, une seule capture PPK2 continue, QUATRE segments
 * directement comparables (meme rail, jamais coupe entre segments 2, 3
 * et 4 -- seul le contenu du registre CTRL3_C change) :
 *
 *   0-15 s    : imu_vdd ETEINT (reference -- doit redonner ~3-4 uA,
 *               comparable a la ligne "Baseline" de Nordic-Support-
 *               Report-XIAO-nRF54LM20A.md §8.1, 3,90 uA).
 *   15-45 s   : imu_vdd ALLUME, CTRL3_C = BDU|IF_INC uniquement --
 *               EXACTEMENT ce que sample_motion() ecrivait avant le
 *               correctif (H_LACTIVE=0, PP_OD=0, valeurs reset). Doit
 *               redonner l'anomalie deja connue (~253-265 uA).
 *   45-75 s   : SANS jamais couper imu_vdd, CTRL3_C reecrit avec
 *               H_LACTIVE=1 en plus (BDU|IF_INC|H_LACTIVE), PP_OD
 *               toujours 0. C'est le correctif deja valide et deploye
 *               en production (voir Nordic-Support-Report-XIAO-
 *               nRF54LM20A.md §8.3) : 265,330 -> 232,605 uA mesure la
 *               premiere fois (delta 32,725 uA, theorique 33 uA a moins
 *               de 1%).
 *   75-105 s  : SANS jamais couper imu_vdd, PP_OD=1 ajoute en plus
 *               (BDU|IF_INC|H_LACTIVE|PP_OD, drain ouvert). Suite a la
 *               reponse Nordic du 2026-09-07 (Simon) qui suggere aussi
 *               ce changement, en plus de H_LACTIVE. Prediction : AUCUN
 *               changement supplementaire attendu par rapport au
 *               segment precedent (~232 uA) -- le gain deja mesure avec
 *               H_LACTIVE seul correspond au calcul theorique complet
 *               du courant R38 (3,3V/100k=33 uA) a moins de 1% pres,
 *               ce qui ne laisse pas de marge pour un gain supplementaire
 *               de PP_OD. Si le resultat contredit cette prediction
 *               (nouvelle baisse), ce serait une decouverte reelle
 *               (le driver push-pull n'atteint pas vraiment VDDIO en
 *               sortie HIGH) -- a signaler immediatement.
 *   105 s+    : imu_vdd re-eteint, idle.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/kernel.h>

static const struct device *const imu_vdd_dev = DEVICE_DT_GET(DT_NODELABEL(imu_vdd));
static const struct i2c_dt_spec imu_i2c = I2C_DT_SPEC_GET(DT_ALIAS(imu0));

#define LSM6DSL_REG_CTRL3_C       0x12U
#define LSM6DSL_CTRL3_C_BDU       BIT(6)
#define LSM6DSL_CTRL3_C_H_LACTIVE BIT(5)
#define LSM6DSL_CTRL3_C_PP_OD     BIT(4)
#define LSM6DSL_CTRL3_C_IF_INC    BIT(2)

static volatile uint8_t write1_rc = 0xAAU;
static volatile uint8_t write2_rc = 0xAAU;
static volatile uint8_t write3_rc = 0xAAU;

int main(void)
{
	if (!device_is_ready(imu_vdd_dev) || !device_is_ready(imu_i2c.bus)) {
		while (1) {
			k_msleep(1000);
		}
	}

	/* Segment 1 : 0-15 s, imu_vdd eteint (etat par defaut, pas de
	 * regulator-boot-on -- rien a faire). */
	k_msleep(15000);

	/* Segment 2 : 15-45 s, imu_vdd allume, CTRL3_C = etat production
	 * actuel (H_LACTIVE=0). */
	regulator_enable(imu_vdd_dev);
	k_msleep(5);
	{
		int rc = i2c_reg_write_byte_dt(&imu_i2c, LSM6DSL_REG_CTRL3_C,
						 LSM6DSL_CTRL3_C_BDU | LSM6DSL_CTRL3_C_IF_INC);
		write1_rc = (rc == 0) ? 0U : (uint8_t)(-rc);
	}
	k_msleep(30000);

	/* Segment 3 : 45-75 s, imu_vdd JAMAIS coupe, CTRL3_C reecrit avec
	 * H_LACTIVE=1 en plus (correctif deja valide et deploye). */
	{
		int rc = i2c_reg_write_byte_dt(&imu_i2c, LSM6DSL_REG_CTRL3_C,
						 LSM6DSL_CTRL3_C_BDU | LSM6DSL_CTRL3_C_IF_INC |
						 LSM6DSL_CTRL3_C_H_LACTIVE);
		write2_rc = (rc == 0) ? 0U : (uint8_t)(-rc);
	}
	k_msleep(30000);

	/* Segment 4 : 75-105 s, imu_vdd JAMAIS coupe, PP_OD=1 ajoute en plus
	 * (suggestion Nordic/Simon, 2026-09-07). */
	{
		int rc = i2c_reg_write_byte_dt(&imu_i2c, LSM6DSL_REG_CTRL3_C,
						 LSM6DSL_CTRL3_C_BDU | LSM6DSL_CTRL3_C_IF_INC |
						 LSM6DSL_CTRL3_C_H_LACTIVE | LSM6DSL_CTRL3_C_PP_OD);
		write3_rc = (rc == 0) ? 0U : (uint8_t)(-rc);
	}
	k_msleep(30000);

	regulator_disable(imu_vdd_dev);

	while (1) {
		k_msleep(1000);
	}

	return 0;
}
