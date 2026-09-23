/* Plan-Reduction-Consommation-2026-09-22.md #9.1 -- caracterisation du
 * temps avant 1er/2e echantillon accelerometre valide, pour chaque
 * configuration ODR/mode candidate (208 Hz normal, 833 Hz HP, 1,66 kHz
 * HP). Jamais mesure au PPK2 -- console UART autorisee (regle §4.6).
 *
 * Chaque cycle reproduit fidelement le cycle de production reel (rail
 * imu_vdd coupe entre deux cycles, la puce perd son etat) : rail on ->
 * ecriture config -> mesure -> rail off. Erreur des echantillons n°1 et
 * n°2 calculee par rapport a la moyenne des echantillons 5 a 10 (carte
 * immobile), par axe.
 */
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#define LSM6DSL_REG_CTRL1_XL      0x10
#define LSM6DSL_REG_CTRL3_C       0x12
#define LSM6DSL_CTRL3_C_BDU       BIT(6)
#define LSM6DSL_CTRL3_C_H_LACTIVE BIT(5)
#define LSM6DSL_CTRL3_C_IF_INC    BIT(2)
#define LSM6DSL_REG_CTRL6_C        0x15
#define LSM6DSL_CTRL6_C_XL_HM_MODE BIT(4)
#define LSM6DSL_REG_STATUS   0x1E
#define LSM6DSL_STATUS_XLDA  BIT(0)
#define LSM6DSL_REG_OUTX_L_XL 0x28

#define ACCEL_MS2_PER_LSB (61e-6f * 9.80665f) /* 0,061 mg/LSB a +/-2 g */

#define N_CYCLES        200
#define POLL_TIMEOUT_US  50000
#define RAIL_SETTLE_MS   5
#define RAIL_OFF_MS      100
#define ERROR_LIMIT_MS2  0.05f

static const struct device *const imu_vdd_dev = DEVICE_DT_GET(DT_NODELABEL(imu_vdd));
static const struct i2c_dt_spec imu_i2c = I2C_DT_SPEC_GET(DT_ALIAS(imu0));

struct accel_config {
	const char *name;
	uint8_t ctrl1_xl;
	uint8_t ctrl6_c;
	uint32_t odr_period_us;
};

static const struct accel_config configs[] = {
	{ "208 Hz normal (reference)", 0x50, LSM6DSL_CTRL6_C_XL_HM_MODE, 4808 },
	{ "833 Hz HP", 0x70, 0x00, 1200 },
	{ "1,66 kHz HP", 0x80, 0x00, 602 },
};

struct axes {
	float x, y, z;
};

static int poll_and_read(struct axes *out, uint32_t *waited_us)
{
	uint8_t status;
	uint8_t raw[6];
	int rc;
	uint32_t t0 = k_cycle_get_32();

	while (1) {
		rc = i2c_reg_read_byte_dt(&imu_i2c, LSM6DSL_REG_STATUS, &status);
		if (rc < 0) {
			return rc;
		}
		if (status & LSM6DSL_STATUS_XLDA) {
			break;
		}
		uint32_t elapsed = k_cyc_to_us_floor32(k_cycle_get_32() - t0);

		if (elapsed > POLL_TIMEOUT_US) {
			return -ETIMEDOUT;
		}
	}
	if (waited_us) {
		*waited_us = k_cyc_to_us_floor32(k_cycle_get_32() - t0);
	}
	rc = i2c_burst_read_dt(&imu_i2c, LSM6DSL_REG_OUTX_L_XL, raw, sizeof(raw));
	if (rc < 0) {
		return rc;
	}
	out->x = (int16_t)sys_get_le16(&raw[0]) * ACCEL_MS2_PER_LSB;
	out->y = (int16_t)sys_get_le16(&raw[2]) * ACCEL_MS2_PER_LSB;
	out->z = (int16_t)sys_get_le16(&raw[4]) * ACCEL_MS2_PER_LSB;
	return 0;
}

static void run_config(const struct accel_config *cfg)
{
	int n_pass = 0, n_fail = 0, n_err = 0;
	uint32_t sum_t1_us = 0, min_t1_us = UINT32_MAX, max_t1_us = 0;
	float max_err1 = 0.0f, max_err2 = 0.0f;

	printf("\n=== %s ===\n", cfg->name);
	for (int cycle = 0; cycle < N_CYCLES; cycle++) {
		int rc;

		rc = regulator_enable(imu_vdd_dev);
		if (rc < 0) {
			printf("  cycle %3d : regulator_enable failed (%d)\n", cycle + 1, rc);
			n_err++;
			continue;
		}
		k_msleep(RAIL_SETTLE_MS);

		(void)i2c_reg_write_byte_dt(&imu_i2c, LSM6DSL_REG_CTRL3_C,
					     LSM6DSL_CTRL3_C_BDU | LSM6DSL_CTRL3_C_H_LACTIVE |
					     LSM6DSL_CTRL3_C_IF_INC);
		(void)i2c_reg_write_byte_dt(&imu_i2c, LSM6DSL_REG_CTRL6_C, cfg->ctrl6_c);
		rc = i2c_reg_write_byte_dt(&imu_i2c, LSM6DSL_REG_CTRL1_XL, cfg->ctrl1_xl);
		if (rc < 0) {
			printf("  cycle %3d : CTRL1_XL write failed (%d)\n", cycle + 1, rc);
			regulator_disable(imu_vdd_dev);
			k_msleep(RAIL_OFF_MS);
			n_err++;
			continue;
		}

		struct axes s1, s2, savg = {0};
		uint32_t t1_us = 0;

		rc = poll_and_read(&s1, &t1_us);
		if (rc == 0) {
			rc = poll_and_read(&s2, NULL);
		}
		if (rc < 0) {
			printf("  cycle %3d : timeout/erreur I2C (%d)\n", cycle + 1, rc);
			regulator_disable(imu_vdd_dev);
			k_msleep(RAIL_OFF_MS);
			n_err++;
			continue;
		}

		/* Echantillons 3 et 4 volontairement non lus (voir plan #9.1) --
		 * attente de 2 periodes ODR pour les laisser passer. */
		k_usleep(2 * cfg->odr_period_us);

		bool ok = true;

		for (int k = 5; k <= 10 && ok; k++) {
			struct axes s;

			if (poll_and_read(&s, NULL) < 0) {
				ok = false;
				break;
			}
			savg.x += s.x;
			savg.y += s.y;
			savg.z += s.z;
		}
		regulator_disable(imu_vdd_dev);
		k_msleep(RAIL_OFF_MS);

		if (!ok) {
			printf("  cycle %3d : timeout/erreur I2C (echantillons 5-10)\n", cycle + 1);
			n_err++;
			continue;
		}
		savg.x /= 6.0f;
		savg.y /= 6.0f;
		savg.z /= 6.0f;

		float e1 = fmaxf(fmaxf(fabsf(s1.x - savg.x), fabsf(s1.y - savg.y)),
				  fabsf(s1.z - savg.z));
		float e2 = fmaxf(fmaxf(fabsf(s2.x - savg.x), fabsf(s2.y - savg.y)),
				  fabsf(s2.z - savg.z));
		bool pass = (e1 <= ERROR_LIMIT_MS2) && (e2 <= ERROR_LIMIT_MS2);

		if (pass) {
			n_pass++;
		} else {
			n_fail++;
			printf("  cycle %3d : FAIL  t1=%u us  err1=%.3f  err2=%.3f m/s2\n",
			       cycle + 1, t1_us, (double)e1, (double)e2);
		}
		if (e1 > max_err1) {
			max_err1 = e1;
		}
		if (e2 > max_err2) {
			max_err2 = e2;
		}
		sum_t1_us += t1_us;
		if (t1_us < min_t1_us) {
			min_t1_us = t1_us;
		}
		if (t1_us > max_t1_us) {
			max_t1_us = t1_us;
		}
	}

	int n_valid = n_pass + n_fail;

	printf("%s : %d/%d cycles valides, PASS %d/%d (%.1f%%), erreurs I2C/timeout %d\n",
	       cfg->name, n_valid, N_CYCLES, n_pass, n_valid,
	       n_valid > 0 ? (100.0 * n_pass / n_valid) : 0.0, n_err);
	if (n_valid > 0) {
		printf("  t1 (1er echantillon) : min=%u max=%u moy=%u us\n", min_t1_us, max_t1_us,
		       sum_t1_us / n_valid);
		printf("  erreur max observee : err1=%.3f  err2=%.3f m/s2 (limite %.2f)\n",
		       (double)max_err1, (double)max_err2, (double)ERROR_LIMIT_MS2);
	}
}

int main(void)
{
	k_msleep(500);
	printf("\n\nxiao_accel_odr_char -- Plan-Reduction-Consommation-2026-09-22.md #9.1\n");
	printf("%d cycles par configuration, limite erreur %.2f m/s2, carte immobile.\n", N_CYCLES,
	       (double)ERROR_LIMIT_MS2);

	for (size_t i = 0; i < ARRAY_SIZE(configs); i++) {
		run_config(&configs[i]);
	}

	printf("\n=== Termine -- critere : |erreur| <= 0,05 m/s2 sur >= 99,5%% des cycles.\n"
	       "Voir plan #9.1 pour la decision de ACCEL_FIRST_SAMPLE_US. ===\n");
	while (1) {
		k_msleep(60000);
	}
	return 0;
}
