/*
 * Test isole, une seule capture PPK2 continue, trois segments
 * directement comparables (meme rail, jamais coupe entre segments 2 et
 * 3 -- seul le contenu du registre CTRL3_C change) :
 *
 *   0-15 s   : imu_vdd ETEINT (reference -- doit redonner ~3-4 uA,
 *              comparable a la ligne "Baseline" de Nordic-Support-
 *              Report-XIAO-nRF54LM20A.md §8.1, 3,90 uA).
 *   15-45 s  : imu_vdd ALLUME, CTRL3_C = BDU|IF_INC uniquement --
 *              EXACTEMENT ce que sample_motion() en production ecrit
 *              aujourd'hui (H_LACTIVE=0, valeur reset). Doit redonner
 *              l'anomalie deja connue (~253-254 uA).
 *   45-75 s  : SANS jamais couper imu_vdd, CTRL3_C reecrit avec
 *              H_LACTIVE=1 en plus (BDU|IF_INC|H_LACTIVE). Hypothese a
 *              verifier : l'etat "inactif" (aucune source routee sur
 *              INT1, MD1_CFG=0x00 par defaut, jamais touche ici) passe
 *              de LOW a HIGH (voir datasheet DocID030071 Rev3, Table 57
 *              p.63 : "H_LACTIVE... 0: interrupt output pads active
 *              high [-> inactif = LOW, pousse par le driver push-pull
 *              contre le pull-up R38 100K vers imu_vdd, ~33 uA
 *              theorique] ; 1: interrupt output pads active low
 *              [-> inactif = HIGH, meme sens que le pull-up, plus de
 *              conflit]"). PP_OD n'est PAS touche ici (reste 0 =
 *              push-pull) -- erreur initialement envisagee puis
 *              corrigee : PP_OD ne change que le comportement du
 *              niveau HIGH, pas le LOW, donc n'aurait rien corrige.
 *   75 s+    : imu_vdd re-eteint, idle.
 *
 * Comparaison attendue si l'hypothese est correcte : segment 45-75 s
 * mesure ~30 uA de moins que le segment 15-45 s (l'ecart theorique du
 * pull-up R38, 3,3 V / 100 kOhm ~ 33 uA), le reste (~220 uA) restant
 * inexplique -- confirme ou infirme l'ampleur exacte de cette piste
 * sans toucher a la production avant d'avoir la preuve.
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
#define LSM6DSL_CTRL3_C_IF_INC    BIT(2)

static volatile uint8_t write1_rc = 0xAAU;
static volatile uint8_t write2_rc = 0xAAU;

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
	 * H_LACTIVE=1 en plus (correctif teste). */
	{
		int rc = i2c_reg_write_byte_dt(&imu_i2c, LSM6DSL_REG_CTRL3_C,
						 LSM6DSL_CTRL3_C_BDU | LSM6DSL_CTRL3_C_IF_INC |
						 LSM6DSL_CTRL3_C_H_LACTIVE);
		write2_rc = (rc == 0) ? 0U : (uint8_t)(-rc);
	}
	k_msleep(30000);

	regulator_disable(imu_vdd_dev);

	while (1) {
		k_msleep(1000);
	}

	return 0;
}
