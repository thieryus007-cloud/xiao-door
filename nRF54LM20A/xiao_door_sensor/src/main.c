/*
 * Pivot d'architecture (2026-08-28) -- System ON IDLE + GRTC, plus de
 * reboot par cycle. Voir XIAO-nRF54LM20A-Solution-System-OFF.md,
 * § "Pivot d'architecture" : le plancher mesure de l'ancienne strategie
 * (System OFF + redemarrage complet a chaque sondage, tests #22-31)
 * etait ~70-144 uA, structurellement incompressible (reinit MFD/
 * regulateur/BLE a chaque reboot). Remplacee par une boucle main()
 * infinie (CONFIG_PM=y, k_sleep() entre cycles) : le SoC ne redemarre
 * plus jamais tant que l'appareil reste alimente -- plancher datasheet
 * documente 4,3 uA (System ON IDLE + GRTC XOSC + 512 KB RAM retenue,
 * page 1 du datasheet Nordic), architecture identique au projet frere
 * nRF52840 (~10 uA mesures, reveil IMU inclus).
 *
 * Consequence directe pour l'IMU (sample_motion()) : imu_vdd/LDO1 est
 * toujours coupe/rallume a chaque cycle (son cout ~250-300 uA en continu
 * reste confirme, voir meme document), mais le SoC ne reinitialise plus
 * son etat RAM entre deux cycles. device_init(imu_dev) ne se
 * re-execute donc plus qu'une seule fois (kernel/device.c:
 * z_impl_device_init() renvoie -EALREADY sans rappeler dev->ops.init()
 * si dev->state->initialized est deja vrai) alors que la puce physique,
 * elle, perd son etat de configuration a chaque coupure de imu_vdd.
 * sample_motion() reecrit donc explicitement par I2C, a CHAQUE cycle,
 * les deux registres que lsm6dsl_init_chip() ne configure qu'au tout
 * premier appel (CTRL3_C: BDU+IF_INC : lsm6dsl.c:778-785 ; CTRL6_C:
 * XL_HM_MODE bas-consommation : lsm6dsl.c:788-793) -- sans quoi les
 * lectures X/Y/Z deviendraient incoherentes en silence a partir du 2e
 * cycle.
 *
 * Elements System OFF conserves tels quels (n'ont rien a voir avec le
 * mode d'alimentation du SoC) : LED deconnectees, flash SPI externe
 * suspendu (driver + bus + broches GPIO brutes), regulateurs
 * power_en/LDO1 sans regulator-boot-on (sauf imu_vdd, voir overlay).
 * Console DESACTIVEE EN DUR (CONFIG_SERIAL=n), jamais suspendue a
 * l'execution (bug driver UARTE documente, voir prj.conf).
 *
 * Intervalle trame B : 15 min (valeur de production).
 *
 * Copyright (c) 2019 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/drivers/retained_mem.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/timer/nrf_grtc_timer.h>
#include <zephyr/kernel.h>
#include <ram_pwrdn.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/addr.h>

#define FRAME_B_INTERVAL_MS (15 * 60 * 1000)
#define ADV_BURST_MS         700

/* Sondage IMU a faible rapport cyclique (introduit test #22, conserve
 * dans le pivot System ON IDLE) : imu_vdd/LDO1 est allume brievement a
 * CHAQUE cycle de la boucle main(), le temps de lire un echantillon
 * accelerometre, puis eteint avant le k_sleep() suivant. Strategie
 * validee sur le projet frere nRF52840 Sense (sondage periodique, pas
 * d'interruption asynchrone) -- voir
 * XIAO-nRF54LM20A-Solution-System-OFF.md. imu_vdd/LDO1 confirme couter
 * ~250-300 uA en continu quel que soit le firmware (y compris le code
 * de reference Seeed) ; l'objectif est de ne payer ce cout que pendant
 * une courte fenetre par cycle plutot qu'en continu.
 *
 * Test #39 (2026-08-28) -- 1000 -> 1500 ms teste puis retire. Resultat
 * (fenetre PPK2 60 s, unite #01) : 15,87 uA / 951,92 uC a 1500 ms contre
 * 21,52 uA / 1,29 mC a 1000 ms -- confirme le modele "cout quasi fixe
 * par salve" (-26% de consommation pour -33% de frequence de sondage).
 * Revenu a 1000 ms sur decision explicite : reference nRF52840 Sense
 * (~10 uA, reactivite ~1 s) prioritaire sur le gain de consommation --
 * voir XIAO-nRF54LM20A-Solution-System-OFF.md. */
#define MOTION_POLL_INTERVAL_MS 1000

#define BTHOME_UUID_LO      0xD2
#define BTHOME_UUID_HI      0xFC
#define BTHOME_INFO_TRIG    0x44

#define OBJ_PACKET_ID       0x00
#define OBJ_BATTERY         0x01
#define OBJ_TEMPERATURE     0x02
#define OBJ_VOLTAGE         0x0C
#define OBJ_BATTERY_LOW     0x15

static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec led_blue = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

#if DT_NODE_HAS_STATUS(DT_NODELABEL(py25q64), okay)
static const struct device *const flash_dev = DEVICE_DT_GET(DT_NODELABEL(py25q64));
static const struct device *const flash_bus = DEVICE_DT_GET(DT_BUS(DT_NODELABEL(py25q64)));
#endif

static const struct device *const charger_dev = DEVICE_DT_GET(DT_NODELABEL(pmic_charger));
static const struct device *const imu_vdd_dev = DEVICE_DT_GET(DT_NODELABEL(imu_vdd));
static const struct device *const imu_dev = DEVICE_DT_GET(DT_ALIAS(imu0));

/* Acces I2C direct au LSM6DS3TR-C, independant du driver Zephyr --
 * necessaire pour reecrire CTRL3_C/CTRL6_C a chaque cycle (voir
 * commentaire d'en-tete de fichier). Registres pris dans
 * zephyr/drivers/sensor/st/lsm6dsl/lsm6dsl.h (prive au driver, non
 * inclus ici -- valeurs recopiees). */
static const struct i2c_dt_spec imu_i2c = I2C_DT_SPEC_GET(DT_ALIAS(imu0));

/* Test #37 (2026-08-28) -- TENTE puis ECARTE PAR LA MESURE : lecture
 * LDSWSTATUS supplementaire sur le nPM1300 (bus i2c22, separe du bus
 * IMU i2c30) juste avant de couper imu_vdd, hypothese d'un rafraichissement
 * TWI periodique necessaire (errata BUCK [31] analogue, "prompt read or
 * write over TWI"). Resultat PPK2 : charge de la salve AUGMENTEE
 * (18,40 -> 20,64 uC), pas reduite -- le courant ~250-300 uA de LDO1 ne
 * depend pas d'un rafraichissement TWI repete. Code retire, hypothese
 * fermee -- voir XIAO-nRF54LM20A-Solution-System-OFF.md. */

#define LSM6DSL_REG_CTRL3_C        0x12
#define LSM6DSL_CTRL3_C_BDU        BIT(6)
#define LSM6DSL_CTRL3_C_IF_INC     BIT(2)
#define LSM6DSL_REG_CTRL6_C        0x15
#define LSM6DSL_CTRL6_C_XL_HM_MODE BIT(4)

/* Allume imu_vdd, reecrit CTRL3_C/CTRL6_C (voir commentaire d'en-tete de
 * fichier -- necessaire a CHAQUE cycle : imu_vdd est coupe puis rallume
 * a chaque appel, ce qui remet la puce a son etat de reset materiel,
 * mais device_init() ne re-executera plus jamais lsm6dsl_init_chip()
 * une fois le driver marque initialise), initialise le driver LSM6DSL
 * une seule fois (deferred-init, voir overlay), lit un echantillon
 * accelerometre, puis eteint imu_vdd. Ne modifie jamais WK_THS/
 * WAKE_UP_THS (consigne explicite du projet) -- lecture directe des
 * axes via l'API sensor standard, aucune configuration d'interruption
 * materielle. */
static int sample_motion(int16_t *out_x, int16_t *out_y, int16_t *out_z)
{
	int rc;
	struct sensor_value x, y, z;

	rc = regulator_enable(imu_vdd_dev);
	if (rc < 0) {
		printf("Warning: imu_vdd regulator_enable failed (%d)\n", rc);
		return rc;
	}

	/* Test #32 (2026-08-28) -- reduit de 20 a 5 ms. Poste dominant
	 * identifie au PPK2 (~24 uC sur ~31 uC/cycle mesures en System ON
	 * IDLE, salve imu_vdd). 20 ms n'avait jamais ete justifie par un
	 * spec (valeur copiee de xiao_seeed_imu_click) -- le seul chiffre
	 * documente pour ce projet est le soft-start LDO du nPM1300
	 * (datasheet nPM1300 Table 24 : 1,8 ms typique, voir
	 * XIAO-nRF54LM20A-Solution-System-OFF.md). 5 ms garde une marge
	 * ~2,8x au-dessus de ce typique. */
	k_msleep(5);

	rc = i2c_reg_write_byte_dt(&imu_i2c, LSM6DSL_REG_CTRL3_C,
				    LSM6DSL_CTRL3_C_BDU | LSM6DSL_CTRL3_C_IF_INC);
	if (rc < 0) {
		printf("Warning: IMU CTRL3_C rewrite failed (%d)\n", rc);
		regulator_disable(imu_vdd_dev);
		return rc;
	}
	rc = i2c_reg_update_byte_dt(&imu_i2c, LSM6DSL_REG_CTRL6_C,
				     LSM6DSL_CTRL6_C_XL_HM_MODE, LSM6DSL_CTRL6_C_XL_HM_MODE);
	if (rc < 0) {
		printf("Warning: IMU CTRL6_C rewrite failed (%d)\n", rc);
		regulator_disable(imu_vdd_dev);
		return rc;
	}

	if (!device_is_ready(imu_dev)) {
		rc = device_init(imu_dev);
		if (rc < 0 && rc != -EALREADY) {
			printf("Warning: IMU device_init failed (%d)\n", rc);
			regulator_disable(imu_vdd_dev);
			return rc;
		}
	}

	/* Test #36 (2026-08-28) -- ODR doublee 104->208 Hz, valeur standard
	 * documentee du LSM6DSL en mode bas-consommation (XL_HM_MODE reste
	 * pose, voir CTRL6_C plus haut). But : reduire l'attente d'une
	 * periode d'echantillonnage complete (necessaire pour une lecture
	 * fraiche), pas la frequence d'usage reelle -- une seule lecture est
	 * prise puis imu_vdd est coupe. Le cout dominant de la salve est le
	 * regulateur imu_vdd (~250-300 uA independamment de l'activite du
	 * capteur, voir § Resultat de l'investigation IMU) : raccourcir la
	 * duree totale d'activation reduit directement ce cout, quel que
	 * soit le supplement de courant du capteur lui-meme a 208 Hz. */
	struct sensor_value odr_attr = { .val1 = 208, .val2 = 0 };

	rc = sensor_attr_set(imu_dev, SENSOR_CHAN_ACCEL_XYZ,
			      SENSOR_ATTR_SAMPLING_FREQUENCY, &odr_attr);
	if (rc < 0) {
		printf("Warning: IMU set ODR failed (%d)\n", rc);
		regulator_disable(imu_vdd_dev);
		return rc;
	}
	/* Periode reelle a 208 Hz = ~4,8 ms -- 6 ms garde une marge
	 * ~1,2 ms au-dessus (meme logique de marge que le test #33 a
	 * 104 Hz). */
	k_msleep(6);

	rc = sensor_sample_fetch_chan(imu_dev, SENSOR_CHAN_ACCEL_XYZ);
	if (rc < 0) {
		printf("Warning: IMU sample fetch failed (%d)\n", rc);
		regulator_disable(imu_vdd_dev);
		return rc;
	}
	sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_X, &x);
	sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_Y, &y);
	sensor_channel_get(imu_dev, SENSOR_CHAN_ACCEL_Z, &z);

	*out_x = (int16_t)x.val1;
	*out_y = (int16_t)y.val1;
	*out_z = (int16_t)z.val1;

	regulator_disable(imu_vdd_dev);

	/* Audit broches (2026-08-28) -- pull-down sur INT1 (gpio0.6) TENTE
	 * puis RETIRE D'URGENCE : mesure PPK2 a montre un pic ~200 mA,
	 * jamais observe avant sur ce projet (pire pic precedent ~87 mA,
	 * salve BLE). Cause probable : contention electrique entre le
	 * pull-down interne et la sortie INT1 potentiellement encore pilotee
	 * par le LSM6DS3TR-C (decharge lente de imu_vdd apres
	 * regulator_disable(), cf. decouplage lent observe sur un fil
	 * DevZone nPM1300 similaire). Code retire par securite -- voir
	 * XIAO-nRF54LM20A-Solution-System-OFF.md, ne pas retenter sans
	 * comprendre le mecanisme exact. */

	return 0;
}

static void print_reset_cause(uint32_t reset_cause)
{
	if (reset_cause & RESET_DEBUG) {
		printf("Reset by debugger.\n");
	} else if (reset_cause & RESET_CLOCK) {
		printf("Wakeup from System OFF by clock source.\n");
	} else if (reset_cause & RESET_LOW_POWER_WAKE) {
		printf("Wakeup from System OFF by GPIO.\n");
	} else if (reset_cause != 0U) {
		printf("Other wake up cause 0x%08" PRIX32 ".\n", reset_cause);
	} else {
		printf("Power-on reset or reset cause unavailable.\n");
	}
}

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

/* Audit broches (2026-08-28) -- deconnexion NFC (P1.01/P1.02) RETIREE
 * D'URGENCE en meme temps que le pull-down INT1 (voir sample_motion()) :
 * les deux changements ont ete mesures ENSEMBLE (pas en variable unique)
 * et ont produit un pic ~200 mA jamais observe avant. Le pull-down INT1
 * est le suspect le plus probable (broche reellement pilotee par un
 * composant externe, contrairement aux broches NFC jamais routees), mais
 * tant que les deux n'ont pas ete re-testes separement, aucun des deux
 * n'est reintroduit -- voir XIAO-nRF54LM20A-Solution-System-OFF.md. */

/*
 * Put the external flash pins into deterministic, low-leakage states before
 * System OFF. These pin numbers are confirmed by the board pinctrl and DTS.
 */
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

/*
 * Test #12 (2026-08-27) -- imu_vdd/LDO1 est un rail PARTAGE avec le
 * microphone PDM embarque (devicetree Seeed : "imu_vdd: dmic_vdd: LDO1").
 * Le peripherique pdm20 n'est jamais active (status="disabled" par
 * defaut, jamais touche -- hors perimetre du projet) : PDM_CLK (P1.13)
 * reste donc flottant des que imu_vdd alimente le micro. D'apres le
 * datasheet MSM261D3526H1CPM, seul VDD=0 V garantit un courant bas
 * (etat "Powered Down") -- sans horloge active/definie, rien ne
 * garantit le mode Sleep (1 uA typ) plutot qu'un mode actif indefini
 * (Low-Power typ 290 uA, tres proche du ~250-275 uA mesure). On force
 * ici PDM_CLK a un niveau bas defini (0 Hz stable, meme technique que
 * pour les broches SPI du flash externe) -- le micro n'est ni
 * configure ni utilise, seule sa broche d'horloge recoit un niveau
 * deterministe au lieu de flotter.
 */
static int configure_pdm_pins_for_system_off(void)
{
	const struct device *gpio1 = DEVICE_DT_GET(DT_NODELABEL(gpio1));
	int rc;

	if (!device_is_ready(gpio1)) {
		printf("GPIO1 not ready.\n");
		return -ENODEV;
	}

	rc = gpio_pin_configure(gpio1, 13, GPIO_OUTPUT_LOW); /* PDM_CLK */
	if (rc < 0) {
		return rc;
	}
	rc = gpio_pin_configure(gpio1, 14, GPIO_INPUT | GPIO_PULL_DOWN); /* PDM_DIN */
	if (rc < 0) {
		return rc;
	}

	return 0;
}

static int suspend_external_flash(void)
{
	int first_error = 0;
	int rc;

#if DT_NODE_HAS_STATUS(DT_NODELABEL(py25q64), okay)
	if (device_is_ready(flash_dev)) {
		rc = pm_device_action_run(flash_dev, PM_DEVICE_ACTION_SUSPEND);
		if ((rc < 0) && (first_error == 0)) {
			first_error = rc;
			printf("Warning: could not suspend external flash (%d)\n", rc);
		}
	} else {
		first_error = -ENODEV;
		printf("Warning: flash device is not ready; skipping driver DPD.\n");
	}

	if (device_is_ready(flash_bus)) {
		rc = pm_device_action_run(flash_bus, PM_DEVICE_ACTION_SUSPEND);
		if ((rc < 0) && (first_error == 0)) {
			first_error = rc;
			printf("Warning: could not suspend SPI bus (%d)\n", rc);
		}
	} else if (first_error == 0) {
		first_error = -ENODEV;
		printf("Warning: flash SPI bus is not ready.\n");
	}
#else
	first_error = -ENODEV;
	printf("Warning: py25q64 is not enabled in DTS.\n");
#endif

	rc = configure_spi_pins_for_system_off();
	if ((rc < 0) && (first_error == 0)) {
		first_error = rc;
		printf("Warning: could not configure flash SPI pins (%d)\n", rc);
	}

	return first_error;
}

/* --- Identite BLE fixe, inchangee -- reprise du firmware de production. --- */
static int set_fixed_ble_identity(void)
{
	bt_addr_le_t addr = { .type = BT_ADDR_LE_RANDOM };
	uint8_t hw_id[8];
	ssize_t len;

	len = hwinfo_get_device_id(hw_id, sizeof(hw_id));
	if (len < (ssize_t)sizeof(addr.a.val)) {
		printf("hwinfo device id too short (%d), using stack-generated address\n",
		       (int)len);
		return 0;
	}

	memcpy(addr.a.val, hw_id, sizeof(addr.a.val));
	addr.a.val[5] |= 0xC0;

	return bt_id_create(&addr, NULL);
}

/* --- Batterie : courbe LiPo non calibree, reprise du firmware de
 * production. --- */
static uint8_t voltage_to_percent(int32_t mv)
{
	static const struct {
		int32_t mv;
		uint8_t pct;
	} curve[] = {
		{ 4200, 100 }, { 4000, 80 }, { 3800, 55 }, { 3700, 35 },
		{ 3500, 15 },  { 3400, 5 },  { 3000, 0 },
	};

	if (mv >= curve[0].mv) {
		return curve[0].pct;
	}
	if (mv <= curve[ARRAY_SIZE(curve) - 1].mv) {
		return curve[ARRAY_SIZE(curve) - 1].pct;
	}
	for (size_t i = 0; i < ARRAY_SIZE(curve) - 1; i++) {
		if (mv <= curve[i].mv && mv >= curve[i + 1].mv) {
			int32_t span_mv = curve[i].mv - curve[i + 1].mv;
			int32_t span_pct = curve[i].pct - curve[i + 1].pct;

			return curve[i + 1].pct +
			       (uint8_t)((mv - curve[i + 1].mv) * span_pct / span_mv);
		}
	}
	return 0;
}

static int read_battery(uint8_t *out_pct, uint16_t *out_mv)
{
	struct sensor_value v;
	int ret;

	/* Test #29 (2026-08-28) -- init differee (voir overlay) : le driver
	 * chargeur n'ecrit ses ~12-15 transactions I2C de configuration que
	 * lorsqu'une lecture batterie est reellement demandee (health_due),
	 * plus a chaque demarrage. */
	if (!device_is_ready(charger_dev)) {
		ret = device_init(charger_dev);
		if (ret < 0 && ret != -EALREADY) {
			printf("Warning: charger device_init failed (%d)\n", ret);
			return ret;
		}
	}
	if (!device_is_ready(charger_dev)) {
		return -ENODEV;
	}
	ret = sensor_sample_fetch_chan(charger_dev, SENSOR_CHAN_GAUGE_VOLTAGE);
	if (ret < 0) {
		return ret;
	}
	ret = sensor_channel_get(charger_dev, SENSOR_CHAN_GAUGE_VOLTAGE, &v);
	if (ret < 0) {
		return ret;
	}

	*out_mv = (uint16_t)(v.val1 * 1000 + v.val2 / 1000);
	*out_pct = voltage_to_percent(*out_mv);
	return 0;
}

/* --- Etat retenu a travers le System OFF -- minimal pour ce test (juste
 * ce qu'il faut pour la trame B : packet_id + echeance GRTC). RAM
 * retenue, CRC-validee comme l'exemple officiel Zephyr system_off. --- */
struct retained_state {
	uint8_t  bthome_pid;
	uint64_t next_health_us;
	uint32_t crc;
};

static struct retained_state retained;

#if DT_NODE_HAS_STATUS_OKAY(DT_ALIAS(retainedmemdevice))
static const struct device *const retained_dev = DEVICE_DT_GET(DT_ALIAS(retainedmemdevice));
#else
#error "retained_mem region not defined -- voir l'overlay de carte"
#endif

#define RETAINED_CRC_OFFSET offsetof(struct retained_state, crc)

static bool retained_load(void)
{
	uint32_t crc;

	if (retained_mem_read(retained_dev, 0, (uint8_t *)&retained, sizeof(retained)) != 0) {
		return false;
	}
	crc = crc32_ieee((const uint8_t *)&retained, RETAINED_CRC_OFFSET);
	return crc == retained.crc;
}

static void retained_save(void)
{
	retained.crc = crc32_ieee((const uint8_t *)&retained, RETAINED_CRC_OFFSET);
	retained_mem_write(retained_dev, 0, (uint8_t *)&retained, sizeof(retained));
}

/* --- Trame B : sante periodique + nom, reprise du firmware de
 * production (temperature omise -- necessiterait l'IMU, pas present
 * dans ce test : envoyee a 0). --- */
static uint8_t frame_b[] = {
	BTHOME_UUID_LO, BTHOME_UUID_HI, BTHOME_INFO_TRIG,
	OBJ_PACKET_ID, 0x00,
	OBJ_BATTERY, 0x00,
	OBJ_TEMPERATURE, 0x00, 0x00,
	OBJ_VOLTAGE, 0x00, 0x00,
	OBJ_BATTERY_LOW, 0x00,
};
#define B_OFF_PACKET_ID 4
#define B_OFF_BATTERY   6
#define B_OFF_TEMP      8
#define B_OFF_VOLTAGE   11
#define B_OFF_BATT_LOW  14

static struct bt_data frame_b_ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
	BT_DATA(BT_DATA_SVC_DATA16, frame_b, sizeof(frame_b)),
};

static void advertise_burst(struct bt_data *data, size_t data_len)
{
	int err;

	err = bt_le_adv_start(BT_LE_ADV_NCONN_IDENTITY, data, data_len, NULL, 0);
	if (err) {
		printf("Advertising failed to start (err %d)\n", err);
		return;
	}
	k_msleep(ADV_BURST_MS);
	err = bt_le_adv_stop();
	if (err) {
		printf("Advertising failed to stop (err %d)\n", err);
	}
}

static void send_frame_b(void)
{
	uint8_t battery_pct = 0;
	uint16_t battery_mv = 0;

	if (read_battery(&battery_pct, &battery_mv) < 0) {
		printf("battery read failed, sending frame B without a fresh value\n");
	}

	retained.bthome_pid++;
	frame_b[B_OFF_PACKET_ID] = retained.bthome_pid;
	frame_b[B_OFF_BATTERY] = battery_pct;
	sys_put_le16(0, &frame_b[B_OFF_TEMP]);
	sys_put_le16(battery_mv, &frame_b[B_OFF_VOLTAGE]);
	frame_b[B_OFF_BATT_LOW] = battery_pct < 15 ? 1 : 0;

	printf("trame B #%u: battery=%u%% (%u mV)\n", retained.bthome_pid, battery_pct, battery_mv);

	advertise_burst(frame_b_ad, ARRAY_SIZE(frame_b_ad));
}

static uint64_t next_health_deadline(uint64_t from_us)
{
	return from_us + ((uint64_t)FRAME_B_INTERVAL_MS * 1000);
}

int main(void)
{
	int rc;
	uint32_t reset_cause = 0U;

	printf("\n=== %s ultra-low-power system-on-idle (GRTC+RAM+BLE+IMU) ===\n", CONFIG_BOARD);

	rc = hwinfo_get_reset_cause(&reset_cause);
	if (rc == 0) {
		print_reset_cause(reset_cause);
	} else {
		printf("Warning: could not read reset cause (%d)\n", rc);
	}
	hwinfo_clear_reset_cause();

	/* Plus de reboot periodique (voir commentaire d'en-tete de fichier) --
	 * retained_load() ne sert plus qu'a survivre a un reset inattendu
	 * (watchdog, brownout), pas au fonctionnement normal. */
	if (!retained_load()) {
		memset(&retained, 0, sizeof(retained));
	}

	release_led_gpios();

	/* Configuration une seule fois au vrai demarrage : les broches SPI
	 * externes et l'identite/pile BLE n'ont plus besoin d'etre
	 * reconfigurees a chaque cycle puisque le SoC ne redemarre plus. */
	rc = configure_spi_pins_for_system_off();
	if (rc < 0) {
		printf("Warning: could not configure flash SPI pins (%d)\n", rc);
	}

	/* Audit broches (2026-08-28) -- appel a configure_pdm_pins_for_system_off()
	 * RETIRE D'URGENCE en meme temps que le pull-down INT1 et la
	 * deconnexion NFC : les trois changements ont ete mesures ENSEMBLE
	 * et ont produit un pic ~200 mA inedit. PDM est probablement
	 * innocent (deja teste seul sans effet en test #12), mais tant que
	 * ce n'est pas reconfirme isolement sur l'architecture actuelle,
	 * l'appel reste retire par precaution -- voir
	 * XIAO-nRF54LM20A-Solution-System-OFF.md. */

	rc = set_fixed_ble_identity();
	if (rc < 0) {
		printf("Warning: could not set fixed BLE identity (err %d)\n", rc);
	}

	rc = bt_enable(NULL);
	if (rc) {
		printf("Error: Bluetooth init failed (err %d), reboot\n", rc);
		sys_reboot(SYS_REBOOT_COLD);
	}

	printf("Bluetooth initialized once -- reset_cause=0x%08x\n", reset_cause);

	/* Etude datasheet Nordic (2026-08-28) -- coupe les sections RAM
	 * inutilisees (au-dela de la fin reelle de l'image liee) pendant le
	 * System ON IDLE. Appele une seule fois, apres toute l'init qui
	 * pourrait avoir besoin de RAM (BLE/MPSL inclus) -- rien dans ce
	 * firmware n'alloue dynamiquement au-dela de ce point. */
	power_down_unused_ram();

	while (1) {
		int16_t accel_x = 0, accel_y = 0, accel_z = 0;

		rc = sample_motion(&accel_x, &accel_y, &accel_z);
		if (rc < 0) {
			printf("Warning: sample_motion failed (%d)\n", rc);
		} else {
			printf("accel x=%d y=%d z=%d\n", accel_x, accel_y, accel_z);
		}

		uint64_t now_us = z_nrf_grtc_timer_read();
		bool health_due = now_us >= retained.next_health_us;

		if (health_due) {
			send_frame_b();
			retained.next_health_us = next_health_deadline(z_nrf_grtc_timer_read());
			retained_save();
		}

		k_sleep(K_MSEC(MOTION_POLL_INTERVAL_MS));
	}

	return 0;
}
