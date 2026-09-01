/*
 * Diagnostic en lecture seule : etat electrique des broches IMU pendant
 * que imu_vdd/LDO1 est ETEINT, puis ALLUME, puis re-ETEINT. But :
 * repondre a la question de Nordic (voir Nordic-Support-Report-XIAO-
 * nRF54LM20A.md, §8.1 reponse Nordic) -- "are all IMU pins set to
 * high-Z (they have external pull resistors)".
 *
 * MISE A JOUR 2026-09-01 -- Nordic a fourni le schema reel en reponse a
 * notre §8.3 : IMU_CS (broche 12 du LSM6DS3TR-C) est cablee sur P3.12,
 * pull-up 100K (R37) vers IMU_VDD -- broche JAMAIS identifiee ni testee
 * avant dans ce projet (usage I2C pur, personne n'avait vu qu'un CS etait
 * quand meme sorti vers le SoC). Nordic : "If the nRF54LM20A drives
 * P3.12 or P0.06 low... you will get power from those right away.
 * ~660uA." gpio3 (port de P3.12) est status="disabled" par defaut, jamais
 * active dans l'overlay de production -- P3.12 n'a donc jamais ete gere.
 * Quatre broches testees ici desormais : SDA=gpio0.8, SCL=gpio0.7,
 * INT1=gpio0.6, CS=gpio3.12. Configurees en entree pure (GPIO_INPUT,
 * sans pull interne) : si l'une d'elles lit systematiquement HIGH alors
 * que imu_vdd n'a jamais ete active, seul un pull-up EXTERNE branche sur
 * un autre rail que imu_vdd peut l'expliquer -- un courant de fuite
 * possible via les diodes de protection ESD de l'IMU non alimente,
 * mecanisme jamais teste par aucun des tests deja menes (tous mesurent
 * le courant, aucun ne lit la tension reelle sur ces broches avec
 * imu_vdd confirme a 0V). Interet supplementaire du schema : le pull-up
 * etant sur IMU_VDD (pas un rail toujours actif), une lecture LOW alors
 * que imu_vdd est ALLUME (Phase 1 ci-dessous) indique desormais quelque
 * chose de precis -- une charge active (l'IMU lui-meme, ou le SoC) tire
 * la ligne vers le bas malgre le pull-up, courant continu resistif.
 *
 * Methode non-invasive : aucune UART/console (CONFIG_SERIAL=n), les
 * echantillons sont ecrits dans un tampon statique lu ensuite par
 * `dump_image` via SWD (meme technique que les traces de diagnostic
 * utilisees ailleurs dans ce projet).
 *
 * imu_vdd n'a PAS regulator-boot-on (voir devicetree vendor) : au
 * demarrage, avant tout regulator_enable(), le rail est deja a 0V par
 * defaut materiel -- pas besoin d'action pour la Phase 0.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/kernel.h>

#define GPIO0_NODE DT_NODELABEL(gpio0)
#define GPIO3_NODE DT_NODELABEL(gpio3)
static const struct device *const gpio0 = DEVICE_DT_GET(GPIO0_NODE);
static const struct device *const gpio3 = DEVICE_DT_GET(GPIO3_NODE);
static const struct device *const imu_vdd_dev = DEVICE_DT_GET(DT_NODELABEL(imu_vdd));
static const struct i2c_dt_spec imu_i2c = I2C_DT_SPEC_GET(DT_ALIAS(imu0));

/* Registres bruts LSM6DS3TR-C -- valeurs reset documentees (datasheet
 * DocID030071 Rev3, table registres) : CTRL1_XL/CTRL2_G = 0x00 =
 * power-down (ODR=0000) pour accelerometre et gyroscope. Lus ICI, avant
 * toute ecriture de configuration (contrairement a sample_motion() en
 * production, qui reecrit CTRL3_C/CTRL6_C avant toute lecture) pour
 * confirmer empiriquement l'etat reel de la puce juste apres power-on,
 * pas seulement la valeur documentee. */
#define LSM6DSL_REG_CTRL1_XL 0x10U
#define LSM6DSL_REG_CTRL2_G  0x11U
#define LSM6DSL_REG_CTRL3_C  0x12U
#define LSM6DSL_CTRL3_C_BDU    BIT(6)
#define LSM6DSL_CTRL3_C_IF_INC BIT(2)

struct reg_sample {
	uint8_t attempt;
	uint8_t delay_ms;  /* delai ecoule depuis regulator_enable() */
	uint8_t bus_ready; /* device_is_ready(imu_i2c.bus), 0/1 */
	uint8_t rc1;       /* code retour lecture CTRL1_XL */
	uint8_t ctrl1_xl;
	uint8_t rc2;       /* code retour lecture CTRL2_G */
	uint8_t ctrl2_g;
};

#define REG_TRACE_LEN 5U
static volatile struct reg_sample reg_trace[REG_TRACE_LEN];
static volatile uint8_t priming_write_rc = 0xAAU; /* sentinelle "jamais ecrit" */

#define PIN_INT1 6U
#define PIN_SCL  7U
#define PIN_SDA  8U
#define PIN_CS   12U /* gpio3.12 = P3.12 = IMU_CS, pull-up 100K vers IMU_VDD (R37,
			schema Nordic 2026-09-01) -- jamais teste avant. */

#define SAMPLES_PER_PHASE 30U
#define SAMPLE_PERIOD_MS  100U

struct pin_sample {
	uint8_t phase;         /* 0 = jamais allume, 1 = imu_vdd ON, 2 = re-OFF */
	uint8_t sample_in_phase;
	uint8_t int1;
	uint8_t scl;
	uint8_t sda;
	uint8_t cs;            /* P3.12/IMU_CS, gpio3 -- jamais teste avant */
	uint8_t ldsw1_enabled; /* regulator_is_enabled(imu_vdd_dev), 0/1 */
};

#define TRACE_LEN (SAMPLES_PER_PHASE * 3U)
static volatile struct pin_sample debug_trace[TRACE_LEN];
static volatile uint32_t debug_trace_idx;
static volatile uint32_t debug_cycle_count;

static void record_sample(uint8_t phase, uint8_t sample_in_phase)
{
	if (debug_trace_idx >= TRACE_LEN) {
		return;
	}

	struct pin_sample s;

	s.phase = phase;
	s.sample_in_phase = sample_in_phase;
	s.int1 = (uint8_t)gpio_pin_get_raw(gpio0, PIN_INT1);
	s.scl = (uint8_t)gpio_pin_get_raw(gpio0, PIN_SCL);
	s.sda = (uint8_t)gpio_pin_get_raw(gpio0, PIN_SDA);
	s.cs = (uint8_t)gpio_pin_get_raw(gpio3, PIN_CS);
	s.ldsw1_enabled = regulator_is_enabled(imu_vdd_dev) ? 1U : 0U;

	debug_trace[debug_trace_idx] = s;
	debug_trace_idx++;
	debug_cycle_count++;
}

int main(void)
{
	if (!device_is_ready(gpio0) || !device_is_ready(gpio3)) {
		return 0;
	}

	/* Phase 3 (executee EN PREMIER, avant que les Phases 0-2 ci-dessous
	 * ne reclament gpio0.7/gpio0.8 en GPIO_INPUT brut -- premiere
	 * version de ce fichier faisait l'inverse : la Phase 3 tentait des
	 * transactions I2C sur des broches deja detachees de la fonction
	 * peripherique TWIM par les phases precedentes, echec garanti
	 * (rc != 0, 0xFF sur les 5 tentatives) -- artefact du test, pas de
	 * la puce, corrige en inversant l'ordre plutot qu'en tentant de
	 * rendre la main sur le pinctrl prive du driver I2C.
	 *
	 * Cycle d'alimentation propre (comme a chaque cycle en production),
	 * puis lecture BRUTE de CTRL1_XL/CTRL2_G avant toute ecriture --
	 * aucun i2c_reg_write_byte_dt() n'est jamais appele dans ce
	 * firmware, contrairement a sample_motion() en production.
	 *
	 * Troisieme execution de ce test : bus_ready=1 et delai croissant
	 * jusqu'a 45 ms n'ont rien change, echec systematique avec
	 * errno=ETIMEDOUT (116) -- ni un probleme de peripherique non pret,
	 * ni un temps de demarrage insuffisant. Hypothese testee ici :
	 * sample_motion() en production ecrit TOUJOURS CTRL3_C EN PREMIER
	 * (avant toute lecture) -- peut-etre que la toute premiere
	 * transaction I2C sur ce bus fraichement alimente doit etre cette
	 * ecriture precise pour que le bus se stabilise, et qu'une lecture
	 * en tout premier echoue pour une raison propre a ce driver/bus,
	 * independante de l'etat de la puce. Ecriture d'amorçage identique
	 * a la production avant toute lecture ci-dessous. */
	if (device_is_ready(imu_vdd_dev)) {
		regulator_enable(imu_vdd_dev);
	}
	k_msleep(5);
	{
		int rc = i2c_reg_write_byte_dt(&imu_i2c, LSM6DSL_REG_CTRL3_C,
						 LSM6DSL_CTRL3_C_BDU | LSM6DSL_CTRL3_C_IF_INC);
		priming_write_rc = (rc == 0) ? 0U : (uint8_t)(-rc);
	}

	for (uint8_t i = 0; i < REG_TRACE_LEN; i++) {
		struct reg_sample rs;
		uint8_t val;
		int rc;
		uint8_t delay = (uint8_t)(5U + (10U * i));

		k_msleep(delay);

		rs.attempt = i;
		rs.delay_ms = delay;
		rs.bus_ready = device_is_ready(imu_i2c.bus) ? 1U : 0U;

		rc = i2c_reg_read_byte_dt(&imu_i2c, LSM6DSL_REG_CTRL1_XL, &val);
		rs.rc1 = (rc == 0) ? 0U : (uint8_t)(-rc);
		rs.ctrl1_xl = (rc == 0) ? val : 0xFFU;

		rc = i2c_reg_read_byte_dt(&imu_i2c, LSM6DSL_REG_CTRL2_G, &val);
		rs.rc2 = (rc == 0) ? 0U : (uint8_t)(-rc);
		rs.ctrl2_g = (rc == 0) ? val : 0xFFU;

		reg_trace[i] = rs;
	}

	if (device_is_ready(imu_vdd_dev)) {
		regulator_disable(imu_vdd_dev);
	}

	/* Entree pure, sans pull interne SoC -- toute polarisation observee
	 * ne peut venir que d'une source externe a ce SoC. Fait APRES la
	 * Phase 3 : ces gpio_pin_configure() detachent gpio0.7/gpio0.8 de
	 * la fonction peripherique TWIM, ce qui casserait l'I2C si fait
	 * avant. */
	gpio_pin_configure(gpio0, PIN_INT1, GPIO_INPUT);
	gpio_pin_configure(gpio0, PIN_SCL, GPIO_INPUT);
	gpio_pin_configure(gpio0, PIN_SDA, GPIO_INPUT);
	gpio_pin_configure(gpio3, PIN_CS, GPIO_INPUT);

	/* Phase 0 : imu_vdd re-eteint depuis la Phase 3 ci-dessus -- pas
	 * "jamais touche depuis le boot" dans cette version (Phase 3 est
	 * passee avant), mais l'etat regime permanent OFF est le meme :
	 * seule une eventuelle polarisation EXTERNE persistante interesse
	 * ce test, pas la transitoire de decharge (deja caracterisee en
	 * Phase 2 ci-dessous). Court delai pour laisser cette transitoire
	 * se dissiper avant le premier echantillon. */
	k_msleep(500);
	for (uint8_t i = 0; i < SAMPLES_PER_PHASE; i++) {
		record_sample(0, i);
		k_msleep(SAMPLE_PERIOD_MS);
	}

	/* Phase 1 : imu_vdd allume -- reference de comparaison. Si les
	 * broches passent a HIGH stable ICI et pas avant, le pull-up est
	 * sur imu_vdd lui-meme (sans danger). */
	if (device_is_ready(imu_vdd_dev)) {
		regulator_enable(imu_vdd_dev);
	}
	k_msleep(10);
	for (uint8_t i = 0; i < SAMPLES_PER_PHASE; i++) {
		record_sample(1, i);
		k_msleep(SAMPLE_PERIOD_MS);
	}

	/* Phase 2 : imu_vdd re-eteint -- verifie une decharge/decroissance
	 * capacitive par rapport a un flottement instantane. */
	if (device_is_ready(imu_vdd_dev)) {
		regulator_disable(imu_vdd_dev);
	}
	for (uint8_t i = 0; i < SAMPLES_PER_PHASE; i++) {
		record_sample(2, i);
		k_msleep(SAMPLE_PERIOD_MS);
	}

	while (1) {
		k_msleep(1000);
	}

	return 0;
}
