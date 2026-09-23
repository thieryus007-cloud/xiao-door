/*
 * Diagnostic en lecture seule des registres ADC/chargeur nPM1300, XIAO
 * nRF54LM20A. But : comprendre pourquoi SENSOR_CHAN_GAUGE_VOLTAGE lit
 * ~4V sur #01 et ~1V sur #02 alors qu'aucune des deux unites n'a de
 * batterie connectee (BAT+/BAT- non cablees, fait confirme). Seules des
 * lectures I2C sont effectuees (mfd_npm13xx_reg_read_burst), sauf le
 * declenchement explicite de la conversion VBAT (ADC_OFFSET_TASK_VBAT),
 * strictement identique a ce que read_battery() du firmware de
 * production declenche deja a chaque trame B.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/mfd/npm13xx.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define PMIC_NODE    DT_NODELABEL(pmic)
#define CHARGER_NODE DT_NODELABEL(pmic_charger)

static const struct device *const pmic = DEVICE_DT_GET(PMIC_NODE);
static const struct device *const charger = DEVICE_DT_GET(CHARGER_NODE);

#define CHGR_BASE 0x03U
#define ADC_BASE  0x05U
#define CHGR_OFFSET_ERR_REASON 0x36U
#define ADC_OFFSET_TASK_VBAT   0x00U
#define ADC_OFFSET_RESULTS     0x10U

static void dump_bank(const char *name, uint8_t base, uint8_t len)
{
	uint8_t buf[64];
	int ret;

	ret = mfd_npm13xx_reg_read_burst(pmic, base, 0x00, buf, len);
	if (ret < 0) {
		printk("  %s: ERREUR LECTURE I2C (%d)\n", name, ret);
		return;
	}
	printk("  %s (base 0x%02x):", name, base);
	for (uint8_t i = 0; i < len; i++) {
		if ((i % 16U) == 0U) {
			printk("\n    +0x%02x:", i);
		}
		printk(" %02x", buf[i]);
	}
	printk("\n");
}

int main(void)
{
	uint32_t cycle = 0;

	if (!device_is_ready(pmic)) {
		printk("ERREUR: device PMIC (npm1300) pas pret\n");
		return 0;
	}
	printk("charger device_is_ready() = %d\n", device_is_ready(charger));

	printk("\n=== Diagnostic ADC/chargeur nPM1300 -- demarrage ===\n");

	while (1) {
		printk("\n--- Cycle %u ---\n", cycle++);

		uint8_t err_reason = 0xFF;
		int rc = mfd_npm13xx_reg_read(pmic, CHGR_BASE, CHGR_OFFSET_ERR_REASON, &err_reason);
		printk("  CHGR_ERR_REASON (0x03/0x36) = 0x%02x (rc=%d)\n", err_reason, rc);

		dump_bank("CHGR", CHGR_BASE, 0x40);
		dump_bank("ADC ", ADC_BASE, 0x30);

		/* Reproduit exactement ce que read_battery() du firmware de
		 * production declenche a chaque trame B. */
		if (device_is_ready(charger)) {
			struct sensor_value v;

			rc = sensor_sample_fetch_chan(charger, SENSOR_CHAN_GAUGE_VOLTAGE);
			if (rc < 0) {
				printk("  sensor_sample_fetch_chan(GAUGE_VOLTAGE) rc=%d\n", rc);
			} else {
				rc = sensor_channel_get(charger, SENSOR_CHAN_GAUGE_VOLTAGE, &v);
				printk("  sensor_channel_get rc=%d  val1=%d val2=%d  -> %d mV\n",
				       rc, v.val1, v.val2,
				       (int)(v.val1 * 1000 + v.val2 / 1000));
			}
		}

		/* Etat brut post-fetch (le sample_fetch ci-dessus vient de
		 * redeclencher TASK_VBAT et relire RESULTS en interne). */
		uint8_t adc_results[8];
		rc = mfd_npm13xx_reg_read_burst(pmic, ADC_BASE, ADC_OFFSET_RESULTS, adc_results, 8);
		printk("  ADC_RESULTS (0x05/0x10, 8o) rc=%d :", rc);
		for (int i = 0; i < 8; i++) {
			printk(" %02x", adc_results[i]);
		}
		printk("\n");

		k_msleep(3000);
	}

	return 0;
}
