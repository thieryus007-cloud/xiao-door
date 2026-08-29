/*
 * Pivot d'architecture (2026-08-28) -- System ON IDLE + GRTC, plus de
 * reboot par cycle. Voir XIAO-nRF54LM20A-Solution-System-OFF.md, § "Pivot
 * d'architecture" : le plancher mesure de l'ancienne strategie (System OFF
 * + redemarrage complet a chaque sondage, tests #22-31) etait ~70-144 uA,
 * structurellement incompressible (reinit MFD/regulateur/BLE a chaque
 * reboot). Remplacee par une boucle main() infinie (CONFIG_PM=y, k_sleep()
 * entre cycles) : le SoC ne redemarre plus jamais tant que l'appareil reste
 * alimente -- plancher datasheet documente 4,3 uA (System ON IDLE + GRTC
 * XOSC + 512 KB RAM retenue, page 1 du datasheet Nordic), architecture
 * identique au projet frere nRF52840 (~10 uA mesures, reveil IMU inclus).
 *
 * Consequence directe pour l'IMU (sample_motion()) : imu_vdd/LDO1 est
 * toujours coupe/rallume a chaque cycle (son cout ~250-300 uA en continu
 * reste confirme, voir meme document), mais le SoC ne reinitialise plus
 * son etat RAM entre deux cycles. device_init(imu_dev) ne se re-execute
 * donc plus qu'une seule fois (kernel/device.c: z_impl_device_init()
 * renvoie -EALREADY sans rappeler dev->ops.init() si dev->state->initialized
 * est deja vrai) alors que la puce physique, elle, perd son etat de
 * configuration a chaque coupure de imu_vdd. sample_motion() reecrit donc
 * explicitement par I2C, a CHAQUE cycle, les deux registres que
 * lsm6dsl_init_chip() ne configure qu'au tout premier appel (CTRL3_C:
 * BDU+IF_INC : lsm6dsl.c:778-785 ; CTRL6_C: XL_HM_MODE bas-consommation :
 * lsm6dsl.c:788-793) -- sans quoi les lectures X/Y/Z deviendraient
 * incoherentes en silence a partir du 2e cycle.
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
 * Parite fonctionnelle production (2026-08-29) -- portage des trames A
 * (mouvement/orientation/bouton/tamper=0/vibration=0) et C (IMU brut)
 * depuis l'ancien firmware de reference (archive/xiao_door_sensor-logs-
 * et-backups/reference/main_full_2026-08-27.c.bak, toujours actif sur
 * l'unite #03) sur cette architecture System ON IDLE. L'ancien mecanisme
 * de reveil GPIO/System OFF (configure_imu_wakeup()/arm_gpio_wake(),
 * erratas [37]/[114]) n'est PAS porte : il n'a plus de raison d'etre, la
 * boucle sonde deja l'accelerometre en logiciel toutes les
 * MOTION_POLL_INTERVAL_MS sans jamais dormir en System OFF. Le journal de
 * diagnostic par cycle (diag_log_*) n'est pas porte non plus : il ne
 * servait qu'a diagnostiquer l'ancien cycle reboot-par-sondage, sans objet
 * ici. Le gyroscope reste allume uniquement en rafale (jamais en continu,
 * ~0,9 mA contre ~9 uA pour l'accelerometre seul), lu dans la meme fenetre
 * imu_vdd que l'accelerometre, juste avant de couper le rail -- voir
 * sample_motion(). Deux bugs corriges au passage : troncature de la
 * lecture accelerometre (x.val1 seul, sans x.val2 -- ecrasait ~9,8 m/s^2 en
 * 9, detruisait le seuil de mouvement 0,3 m/s^2) ; echeances GRTC absolues
 * (next_health_us/next_heartbeat_us) potentiellement obsoletes si la RAM
 * retenue (SRAM, pas RRAM) survit a un reset/reflash alors que le compteur
 * GRTC repart de zero -- desormais bornees a au plus un intervalle complet
 * apres le boot, voir main().
 *
 * Copyright (c) 2019 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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
 * d'interruption asynchrone) -- voir XIAO-nRF54LM20A-Solution-System-OFF.md.
 * imu_vdd/LDO1 confirme couter ~250-300 uA en continu quel que soit le
 * firmware (y compris le code de reference Seeed) ; l'objectif est de ne
 * payer ce cout que pendant une courte fenetre par cycle plutot qu'en
 * continu.
 *
 * Test #39 (2026-08-28) -- 1000 -> 1500 ms teste puis retire. Resultat
 * (fenetre PPK2 60 s, unite #01) : 15,87 uA / 951,92 uC a 1500 ms contre
 * 21,52 uA / 1,29 mC a 1000 ms -- confirme le modele "cout quasi fixe
 * par salve" (-26% de consommation pour -33% de frequence de sondage).
 * Revenu a 1000 ms sur decision explicite : reference nRF52840 Sense
 * (~10 uA, reactivite ~1 s) prioritaire sur le gain de consommation --
 * voir XIAO-nRF54LM20A-Solution-System-OFF.md. */
#define MOTION_POLL_INTERVAL_MS 1000

/* --- Logique evenementielle trame A, reprise telle quelle de l'ancien
 * firmware de reference (main_full_2026-08-27.c.bak) -- constantes
 * inchangees, toutes basees sur k_uptime_get() (horloge murale), donc
 * valables quelle que soit la cadence de sondage. --- */
#define FRAME_A_HEARTBEAT_MS     (60 * 60 * 1000)   /* heartbeat sans activite */
#define MOTION_REPORT_MIN_GAP_MS 4000               /* anti-rafale evenement */
#define FRAME_A_MAX_PER_MIN      10                 /* plafond glissant 60s */
#define MOTION_THRESHOLD_MS2     0.3f
#define ANGLE_HYSTERESIS_DD      20                 /* 2,0 deg en dixiemes */
#define REST_FRAME_DELAY_MS      10000              /* delai avant trame repos */
#define REST_FRAME_MAX_WAIT_MS   30000              /* filet de securite */
#define RAD_TO_DEG 57.29577951308232f

#define BTHOME_UUID_LO      0xD2
#define BTHOME_UUID_HI      0xFC
#define BTHOME_INFO_TRIG    0x44

#define OBJ_PACKET_ID       0x00
#define OBJ_BATTERY         0x01
#define OBJ_TEMPERATURE     0x02
#define OBJ_VOLTAGE         0x0C
#define OBJ_GENERIC_BOOL    0x0F
#define OBJ_BATTERY_LOW     0x15
#define OBJ_MOTION          0x21
#define OBJ_TAMPER          0x2B
#define OBJ_VIBRATION       0x2C
#define OBJ_BUTTON          0x3A
#define OBJ_ROTATION        0x3F
#define OBJ_ACCELERATION    0x51
#define OBJ_GYROSCOPE       0x52
#define OBJ_ACCEL_SIGNED    0x63

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

/* Bouton physique (sw0/button0, gpio0.9, PULL_UP|ACTIVE_LOW -- deja cable
 * au niveau materiel, cf. devicetree). Repris a l'identique de l'ancien
 * firmware de reference, y compris son bug connu non resolu (lit toujours
 * 0 en test reel, cause jamais trouvee, hors perimetre de ce portage --
 * l'objectif est la parite avec la production, pas la correction de ce
 * bug). GPIO_DT_SPEC_GET/gpio_pin_get_dt tiennent deja compte de la
 * polarite ACTIVE_LOW. */
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

#define LSM6DSL_REG_CTRL3_C        0x12
#define LSM6DSL_CTRL3_C_BDU        BIT(6)
#define LSM6DSL_CTRL3_C_IF_INC     BIT(2)
#define LSM6DSL_REG_CTRL6_C        0x15
#define LSM6DSL_CTRL6_C_XL_HM_MODE BIT(4)

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
 *
 * Audit broches (2026-08-28) -- appel RETIRE D'URGENCE en meme temps que
 * le pull-down INT1 et la deconnexion NFC : les trois changements ont ete
 * mesures ENSEMBLE et ont produit un pic ~200 mA inedit. PDM est
 * probablement innocent (deja teste seul sans effet en test #12), mais
 * tant que ce n'est pas reconfirme isolement sur l'architecture actuelle,
 * l'appel reste retire par precaution -- voir
 * XIAO-nRF54LM20A-Solution-System-OFF.md. Fonction conservee mais non
 * appelee.
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

/* --- Etat retenu a travers un reset inattendu (watchdog/brownout) --
 * etendu le 2026-08-29 pour porter les trames A/C : derniers angles
 * envoyes (hysteresis), yaw integre (survit au reset comme sur l'ancien
 * firmware), echeance heartbeat en plus de l'echeance sante. RAM retenue,
 * CRC-validee comme l'exemple officiel Zephyr system_off. --- */
struct retained_state {
	uint8_t  bthome_pid;
	int16_t  last_sent_pitch_dd;
	int16_t  last_sent_roll_dd;
	int16_t  yaw_dd;             /* integration gyroscopique cumulee
				       * (dixiemes de degre) -- pas de recalage
				       * anti-derive, voir en-tete de fichier de
				       * l'ancien firmware de reference. */
	uint64_t next_health_us;     /* prochaine trame B, temps GRTC absolu */
	uint64_t next_heartbeat_us;  /* prochain heartbeat trame A, idem */
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

static uint16_t clamp_u16(float scaled)
{
	if (scaled < 0.0f) {
		scaled = 0.0f;
	}
	if (scaled > (float)UINT16_MAX) {
		scaled = (float)UINT16_MAX;
	}
	return (uint16_t)scaled;
}

static int16_t clamp_i16(float scaled)
{
	if (scaled < (float)INT16_MIN) {
		scaled = (float)INT16_MIN;
	}
	if (scaled > (float)INT16_MAX) {
		scaled = (float)INT16_MAX;
	}
	return (int16_t)scaled;
}

static int32_t clamp_i32(double scaled)
{
	if (scaled < (double)INT32_MIN) {
		scaled = (double)INT32_MIN;
	}
	if (scaled > (double)INT32_MAX) {
		scaled = (double)INT32_MAX;
	}
	return (int32_t)scaled;
}

struct imu_sample {
	float ax, ay, az;
	bool valid;
};

/* Temperature interne : suit l'ODR de l'accelerometre (deja actif dans la
 * meme fenetre imu_vdd), fetch separe (registres distincts) -- appele
 * uniquement quand une trame B va etre envoyee (health_due), voir
 * sample_motion(), pour ne pas payer cette transaction I2C supplementaire
 * a chaque cycle de 1s. */
static int read_die_temp(int16_t *temp_cc)
{
	struct sensor_value t;

	if (sensor_sample_fetch_chan(imu_dev, SENSOR_CHAN_DIE_TEMP) == 0 &&
	    sensor_channel_get(imu_dev, SENSOR_CHAN_DIE_TEMP, &t) == 0) {
		*temp_cc = clamp_i16(sensor_value_to_float(&t) * 100.0f);
		return 0;
	}
	*temp_cc = 0;
	return -EIO;
}

/* Delai avant lecture gyro : bug corrige le 2026-08-30 sur l'ancien
 * firmware (jamais mesure avant ce jour) -- le driver Zephyr lsm6dsl lit
 * les registres de sortie gyro directement, sans attendre le drapeau
 * "donnee prete". Ton (temps de demarrage) = 35 ms (datasheet ST
 * DocID030071 Rev 3 Table 4 p.24) ; 35 + 80 (premiere periode ODR a
 * 12,5 Hz) ~= 115 ms minimum -- marge portee a 200 ms (~2,5 periodes ODR). */
#define GYRO_STARTUP_MS 200

static void set_gyro_power(bool on)
{
	struct sensor_value odr_attr = { .val1 = on ? 12 : 0, .val2 = on ? 500000 : 0 };

	sensor_attr_set(imu_dev, SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr_attr);
	if (on) {
		k_msleep(GYRO_STARTUP_MS);
	}
}

static int read_gyro_burst(float *gx, float *gy, float *gz)
{
	struct sensor_value x, y, z;
	int ret;

	set_gyro_power(true);

	ret = sensor_sample_fetch_chan(imu_dev, SENSOR_CHAN_GYRO_XYZ);
	if (ret < 0) {
		set_gyro_power(false);
		return ret;
	}
	sensor_channel_get(imu_dev, SENSOR_CHAN_GYRO_X, &x);
	sensor_channel_get(imu_dev, SENSOR_CHAN_GYRO_Y, &y);
	sensor_channel_get(imu_dev, SENSOR_CHAN_GYRO_Z, &z);
	*gx = sensor_value_to_float(&x) * RAD_TO_DEG;
	*gy = sensor_value_to_float(&y) * RAD_TO_DEG;
	*gz = sensor_value_to_float(&z) * RAD_TO_DEG;

	set_gyro_power(false);
	return 0;
}

/* Horodatage de la derniere integration yaw -- volontairement en RAM non
 * retenue (remise a 0 a chaque boot) : la valeur integree elle-meme
 * (retained.yaw_dd) survit a un reset inattendu, mais le delta de temps
 * entre deux lectures gyro ne doit etre calcule qu'au sein d'une meme
 * session. */
static int64_t last_yaw_update_uptime;

static int read_gyro_and_integrate_yaw(float *gx, float *gy, float *gz, uint16_t *gyro_mag)
{
	int gyro_ret = read_gyro_burst(gx, gy, gz);

	*gyro_mag = 0;
	if (gyro_ret == 0) {
		*gyro_mag = clamp_u16(sqrtf(*gx * *gx + *gy * *gy + *gz * *gz) * 1000.0f);

		int64_t now = k_uptime_get();

		if (last_yaw_update_uptime != 0) {
			float dt_s = (float)(now - last_yaw_update_uptime) / 1000.0f;

			retained.yaw_dd = clamp_i16((float)retained.yaw_dd + *gz * dt_s * 10.0f);
		}
		last_yaw_update_uptime = now;
	}
	return gyro_ret;
}

static bool motion_detected(const struct imu_sample *prev, const struct imu_sample *cur)
{
	float dx, dy, dz, delta_mag;

	if (!prev->valid) {
		return false;
	}
	dx = cur->ax - prev->ax;
	dy = cur->ay - prev->ay;
	dz = cur->az - prev->az;
	delta_mag = sqrtf(dx * dx + dy * dy + dz * dz);

	return delta_mag > MOTION_THRESHOLD_MS2;
}

/* Pitch/roll par projection du vecteur gravite. */
static void accel_to_pitch_roll(float ax, float ay, float az,
				  int16_t *pitch_dd, int16_t *roll_dd)
{
	float pitch = atan2f(-ax, sqrtf(ay * ay + az * az)) * RAD_TO_DEG;
	float roll  = atan2f(ay, az) * RAD_TO_DEG;

	*pitch_dd = clamp_i16(pitch * 10.0f);
	*roll_dd  = clamp_i16(roll * 10.0f);
}

static int init_button(void)
{
	if (!device_is_ready(button.port)) {
		return -ENODEV;
	}
	return gpio_pin_configure_dt(&button, GPIO_INPUT);
}

static uint8_t read_button_state(void)
{
	int val = gpio_pin_get_dt(&button);

	if (val < 0) {
		printf("Warning: button read failed (%d)\n", val);
		return 0;
	}
	return (uint8_t)val;
}

/* --- Trame A : evenement + orientation. --- */
static uint8_t frame_a[] = {
	BTHOME_UUID_LO, BTHOME_UUID_HI, BTHOME_INFO_TRIG,
	OBJ_PACKET_ID, 0x00,
	OBJ_GENERIC_BOOL, 0x00,
	OBJ_MOTION, 0x00,
	OBJ_TAMPER, 0x00,
	OBJ_VIBRATION, 0x00,
	OBJ_BUTTON, 0x00,
	OBJ_ROTATION, 0x00, 0x00,
	OBJ_ROTATION, 0x00, 0x00,
	OBJ_ROTATION, 0x00, 0x00,
};
#define A_OFF_PACKET_ID  4
#define A_OFF_ACTIVITY   6
#define A_OFF_MOTION     8
#define A_OFF_TAMPER     10
#define A_OFF_VIBRATION  12
#define A_OFF_BUTTON     14
#define A_OFF_PITCH      16
#define A_OFF_ROLL       19
#define A_OFF_YAW        22

/* --- Trame B : sante periodique + nom. --- */
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

/* --- Trame C : IMU brut, magnitudes + acceleration signee par axe.
 * Budget serre : 31 octets sur l'air exactement (calcul verifie contre
 * zephyr/subsys/bluetooth/host/adv.c:495, BT_GAP_ADV_MAX_ADV_DATA_LEN) --
 * plus aucune marge, toute mesure supplementaire ajoutee ici fera echouer
 * l'advertising. --- */
static uint8_t frame_c[] = {
	BTHOME_UUID_LO, BTHOME_UUID_HI, BTHOME_INFO_TRIG,
	OBJ_PACKET_ID, 0x00,
	OBJ_ACCELERATION, 0x00, 0x00,
	OBJ_GYROSCOPE, 0x00, 0x00,
	OBJ_ACCEL_SIGNED, 0x00, 0x00, 0x00, 0x00,
	OBJ_ACCEL_SIGNED, 0x00, 0x00, 0x00, 0x00,
	OBJ_ACCEL_SIGNED, 0x00, 0x00, 0x00, 0x00,
};
#define C_OFF_PACKET_ID  4
#define C_OFF_ACCEL_MAG  6
#define C_OFF_GYRO_MAG   9   /* pas 8 : l'octet 8 est l'ID de l'objet gyro */
#define C_OFF_ACCEL_X    12
#define C_OFF_ACCEL_Y    17
#define C_OFF_ACCEL_Z    22

static struct bt_data frame_a_ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_SVC_DATA16, frame_a, sizeof(frame_a)),
};
static struct bt_data frame_b_ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
	BT_DATA(BT_DATA_SVC_DATA16, frame_b, sizeof(frame_b)),
};
static struct bt_data frame_c_ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_SVC_DATA16, frame_c, sizeof(frame_c)),
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

/* Fenetre glissante simple : au plus FRAME_A_MAX_PER_MIN trames A envoyees
 * sur les 60 dernieres secondes. Volontairement non retenue entre deux
 * boots -- etat de session uniquement. */
static int64_t frame_a_send_times[FRAME_A_MAX_PER_MIN];
static size_t frame_a_send_idx;

static bool frame_a_rate_limited(int64_t now)
{
	int64_t oldest = frame_a_send_times[frame_a_send_idx];

	return (oldest != 0) && (now - oldest) < 60000;
}

static void frame_a_record_send(int64_t now)
{
	frame_a_send_times[frame_a_send_idx] = now;
	frame_a_send_idx = (frame_a_send_idx + 1) % FRAME_A_MAX_PER_MIN;
}

static void send_frame_a(const struct imu_sample *accel, bool motion, bool activity,
			   const char *reason)
{
	int16_t pitch_dd, roll_dd;

	accel_to_pitch_roll(accel->ax, accel->ay, accel->az, &pitch_dd, &roll_dd);

	retained.bthome_pid++;
	frame_a[A_OFF_PACKET_ID] = retained.bthome_pid;
	frame_a[A_OFF_ACTIVITY] = activity ? 1 : 0;
	frame_a[A_OFF_MOTION] = motion ? 1 : 0;
	frame_a[A_OFF_TAMPER] = 0;    /* free-fall : non implemente, driver LSM6DSL
				       * n'expose pas cet evenement via sensor_trigger */
	frame_a[A_OFF_VIBRATION] = 0; /* double-tap : non implemente, idem */
	frame_a[A_OFF_BUTTON] = read_button_state();
	sys_put_le16((uint16_t)pitch_dd, &frame_a[A_OFF_PITCH]);
	sys_put_le16((uint16_t)roll_dd, &frame_a[A_OFF_ROLL]);
	sys_put_le16((uint16_t)retained.yaw_dd, &frame_a[A_OFF_YAW]);

	printf("trame A (%s) #%u: pitch=%d.%d roll=%d.%d motion=%d activity=%d\n",
	       reason, retained.bthome_pid, pitch_dd / 10, abs(pitch_dd % 10),
	       roll_dd / 10, abs(roll_dd % 10), motion, activity);

	advertise_burst(frame_a_ad, ARRAY_SIZE(frame_a_ad));
}

/* La trame qui rapporte l'etat actuel (plus de mouvement) apres un
 * mouvement est la seule dont la perte laisse HA fige dans un etat perime
 * pendant longtemps. BLE non connecte (advertising) n'a pas d'accuse de
 * reception : on repete donc cette trame precise plusieurs fois, chacune
 * avec un packet_id different, pour rendre sa perte totale hautement
 * improbable. */
#define FINAL_STATE_REPEATS       3
#define FINAL_STATE_REPEAT_GAP_MS 3000

static void send_final_state_frame(const struct imu_sample *cur, const char *reason)
{
	for (int i = 0; i < FINAL_STATE_REPEATS; i++) {
		if (i > 0) {
			k_msleep(FINAL_STATE_REPEAT_GAP_MS);
		}
		send_frame_a(cur, false, false, reason);
		frame_a_record_send(k_uptime_get());
	}
}

static void send_frame_c(const struct imu_sample *accel, int gyro_ret, float gx, float gy,
			   float gz, uint16_t gyro_mag)
{
	uint16_t accel_mag = clamp_u16(
		sqrtf(accel->ax * accel->ax + accel->ay * accel->ay + accel->az * accel->az) *
		1000.0f);

	printf("trame C: gyro_ret=%d gyro_mag=%u accel_mag_x1000=%u\n",
	       gyro_ret, gyro_mag, accel_mag);

	/* Packet ID propre, distinct de celui de la trame A qui la precede :
	 * deux trames differentes portant le meme id a moins de 4s
	 * d'intervalle sont indiscernables d'une retransmission pour le
	 * deduplicateur BTHome -- la seconde serait silencieusement ecartee
	 * par HA. */
	retained.bthome_pid++;
	frame_c[C_OFF_PACKET_ID] = retained.bthome_pid;
	sys_put_le16(accel_mag, &frame_c[C_OFF_ACCEL_MAG]);
	sys_put_le16(gyro_mag, &frame_c[C_OFF_GYRO_MAG]);
	sys_put_le32((uint32_t)clamp_i32((double)accel->ax * 1000000.0), &frame_c[C_OFF_ACCEL_X]);
	sys_put_le32((uint32_t)clamp_i32((double)accel->ay * 1000000.0), &frame_c[C_OFF_ACCEL_Y]);
	sys_put_le32((uint32_t)clamp_i32((double)accel->az * 1000000.0), &frame_c[C_OFF_ACCEL_Z]);

	advertise_burst(frame_c_ad, ARRAY_SIZE(frame_c_ad));
}

static void send_frame_b(int16_t temp_cc)
{
	uint8_t battery_pct = 0;
	uint16_t battery_mv = 0;

	if (read_battery(&battery_pct, &battery_mv) < 0) {
		printf("battery read failed, sending frame B without a fresh value\n");
	}

	retained.bthome_pid++;
	frame_b[B_OFF_PACKET_ID] = retained.bthome_pid;
	frame_b[B_OFF_BATTERY] = battery_pct;
	sys_put_le16((uint16_t)temp_cc, &frame_b[B_OFF_TEMP]);
	sys_put_le16(battery_mv, &frame_b[B_OFF_VOLTAGE]);
	frame_b[B_OFF_BATT_LOW] = battery_pct < 15 ? 1 : 0;

	printf("trame B #%u: battery=%u%% (%u mV) temp=%d.%02d C\n",
	       retained.bthome_pid, battery_pct, battery_mv,
	       temp_cc / 100, abs(temp_cc % 100));

	advertise_burst(frame_b_ad, ARRAY_SIZE(frame_b_ad));
}

static uint64_t next_health_deadline(uint64_t from_us)
{
	return from_us + ((uint64_t)FRAME_B_INTERVAL_MS * 1000);
}

static uint64_t next_heartbeat_deadline(uint64_t from_us)
{
	return from_us + ((uint64_t)FRAME_A_HEARTBEAT_MS * 1000);
}

/* --- Resultat d'un cycle de sondage IMU : accelerometre systematique,
 * temperature/gyroscope seulement si demandes (want_temp / evenement a
 * rapporter) -- tout est lu dans la MEME fenetre imu_vdd, avant que
 * sample_motion() ne coupe le rail, pour ne jamais payer un second
 * cycle regulateur/I2C. --- */
struct motion_result {
	struct imu_sample accel;
	int16_t temp_cc;
	bool temp_valid;
	int16_t pitch_dd, roll_dd;
	bool moving;
	bool angle_crossed;
	bool want_event_frame;
	bool rate_limited;
	int64_t now;
	bool gyro_read;
	int gyro_ret;
	float gx, gy, gz;
	uint16_t gyro_mag;
};

/* Allume imu_vdd, reecrit CTRL3_C/CTRL6_C (voir commentaire d'en-tete de
 * fichier -- necessaire a CHAQUE cycle : imu_vdd est coupe puis rallume a
 * chaque appel, ce qui remet la puce a son etat de reset materiel, mais
 * device_init() ne re-executera plus jamais lsm6dsl_init_chip() une fois
 * le driver marque initialise), initialise le driver LSM6DSL une seule
 * fois (deferred-init, voir overlay), lit accelerometre (+ temperature si
 * want_temp, + gyroscope en rafale si un evenement va etre rapporte), puis
 * eteint imu_vdd avant tout appel BLE (advertise_burst() peut bloquer
 * jusqu'a ~700ms-~10s -- hors de question de laisser imu_vdd allume
 * pendant ce temps). Ne modifie jamais WK_THS/WAKE_UP_THS -- lecture
 * directe des axes via l'API sensor standard, aucune configuration
 * d'interruption materielle (plus de reveil GPIO dans cette architecture,
 * voir en-tete de fichier). */
static int sample_motion(struct imu_sample *prev, bool heartbeat_pending, bool want_temp,
			   int64_t last_frame_a_uptime, struct motion_result *out)
{
	int rc;
	struct sensor_value x, y, z;

	memset(out, 0, sizeof(*out));

	rc = regulator_enable(imu_vdd_dev);
	if (rc < 0) {
		printf("Warning: imu_vdd regulator_enable failed (%d)\n", rc);
		return rc;
	}

	/* Soft-start LDO nPM1300 (datasheet Table 24 : 1,8 ms typique) --
	 * marge ~2,8x, voir test #32. */
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

	/* ODR 208 Hz -- reduit l'attente d'une periode d'echantillonnage
	 * complete (test #36), XL_HM_MODE (bas-consommation) reste pose. */
	struct sensor_value odr_attr = { .val1 = 208, .val2 = 0 };

	rc = sensor_attr_set(imu_dev, SENSOR_CHAN_ACCEL_XYZ,
			      SENSOR_ATTR_SAMPLING_FREQUENCY, &odr_attr);
	if (rc < 0) {
		printf("Warning: IMU set ODR failed (%d)\n", rc);
		regulator_disable(imu_vdd_dev);
		return rc;
	}
	/* Periode reelle a 208 Hz = ~4,8 ms -- 6 ms garde une marge ~1,2 ms. */
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

	out->accel.ax = sensor_value_to_float(&x);
	out->accel.ay = sensor_value_to_float(&y);
	out->accel.az = sensor_value_to_float(&z);
	out->accel.valid = true;

	if (want_temp) {
		out->temp_valid = (read_die_temp(&out->temp_cc) == 0);
	}

	accel_to_pitch_roll(out->accel.ax, out->accel.ay, out->accel.az,
			     &out->pitch_dd, &out->roll_dd);
	out->moving = motion_detected(prev, &out->accel);
	out->angle_crossed =
		(abs(out->pitch_dd - retained.last_sent_pitch_dd) > ANGLE_HYSTERESIS_DD) ||
		(abs(out->roll_dd - retained.last_sent_roll_dd) > ANGLE_HYSTERESIS_DD);

	out->now = k_uptime_get();

	bool min_gap_ok = (out->now - last_frame_a_uptime) >= MOTION_REPORT_MIN_GAP_MS;

	out->want_event_frame =
		heartbeat_pending || ((out->moving || out->angle_crossed) && min_gap_ok);
	out->rate_limited = frame_a_rate_limited(out->now);

	if (out->want_event_frame && !out->rate_limited) {
		out->gyro_ret = read_gyro_and_integrate_yaw(&out->gx, &out->gy, &out->gz,
							      &out->gyro_mag);
		out->gyro_read = true;
	}

	regulator_disable(imu_vdd_dev);

	/* Audit broches (2026-08-28) -- pull-down sur INT1 (gpio0.6) TENTE
	 * puis RETIRE D'URGENCE : mesure PPK2 a montre un pic ~200 mA,
	 * jamais observe avant sur ce projet. Code retire par securite --
	 * voir XIAO-nRF54LM20A-Solution-System-OFF.md, ne pas retenter sans
	 * comprendre le mecanisme exact. */

	return 0;
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

	/* Plus de reboot periodique -- retained_load() ne sert plus qu'a
	 * survivre a un reset inattendu (watchdog, brownout), pas au
	 * fonctionnement normal. */
	if (!retained_load()) {
		memset(&retained, 0, sizeof(retained));
	}

	/* Correctif 2026-08-29 : la RAM retenue est de la SRAM ordinaire (pas
	 * de la RRAM) -- elle peut survivre a un reset/reflash (CRC valide)
	 * alors que le compteur GRTC, lui, repart de zero. Une echeance
	 * absolue chargee d'une session precedente (ex. plusieurs heures de
	 * fonctionnement) deviendrait alors inatteignable pendant une duree
	 * indeterminee, empechant toute trame B/heartbeat de partir --
	 * symptome observe le 2026-08-29 sur l'unite #01 (aucune trame en
	 * 18 min apres reflash). On borne les deux echeances a au plus un
	 * intervalle complet apres CE boot, jamais plus loin. */
	uint64_t boot_us = z_nrf_grtc_timer_read();
	uint64_t max_health_deadline = next_health_deadline(boot_us);
	uint64_t max_heartbeat_deadline = next_heartbeat_deadline(boot_us);

	if (retained.next_health_us > max_health_deadline) {
		retained.next_health_us = max_health_deadline;
	}
	if (retained.next_heartbeat_us > max_heartbeat_deadline) {
		retained.next_heartbeat_us = max_heartbeat_deadline;
	}

	release_led_gpios();

	/* Configuration une seule fois au vrai demarrage : les broches SPI
	 * externes et l'identite/pile BLE n'ont plus besoin d'etre
	 * reconfigurees a chaque cycle puisque le SoC ne redemarre plus. */
	rc = configure_spi_pins_for_system_off();
	if (rc < 0) {
		printf("Warning: could not configure flash SPI pins (%d)\n", rc);
	}

	rc = init_button();
	if (rc < 0) {
		printf("Warning: button init failed (%d) -- bouton toujours envoye a 0\n", rc);
	}

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

	/* Coupe les sections RAM inutilisees pendant le System ON IDLE.
	 * Appele une seule fois, apres toute l'init qui pourrait avoir
	 * besoin de RAM (BLE/MPSL inclus). */
	power_down_unused_ram();

	struct imu_sample prev = { .valid = false };
	bool was_moving = false;
	int64_t rest_since = 0;
	int64_t first_moving_uptime = 0;
	bool rest_frame_pending = false;
	int64_t last_frame_a_uptime = -(int64_t)MOTION_REPORT_MIN_GAP_MS;

	while (1) {
		uint64_t now_us = z_nrf_grtc_timer_read();
		bool health_due = now_us >= retained.next_health_us;
		bool heartbeat_due = now_us >= retained.next_heartbeat_us;
		struct motion_result mr;

		rc = sample_motion(&prev, heartbeat_due, health_due, last_frame_a_uptime, &mr);
		if (rc < 0) {
			printf("Warning: sample_motion failed (%d)\n", rc);
			k_sleep(K_MSEC(MOTION_POLL_INTERVAL_MS));
			continue;
		}

		prev = mr.accel;

		if (mr.want_event_frame && !mr.rate_limited) {
			const char *reason = heartbeat_due ? "heartbeat" :
					      mr.moving ? "motion" : "angle";

			send_frame_a(&mr.accel, mr.moving, mr.moving, reason);
			send_frame_c(&mr.accel, mr.gyro_read ? mr.gyro_ret : -EIO,
				     mr.gx, mr.gy, mr.gz, mr.gyro_mag);
			frame_a_record_send(mr.now);
			last_frame_a_uptime = mr.now;
			retained.last_sent_pitch_dd = mr.pitch_dd;
			retained.last_sent_roll_dd = mr.roll_dd;
			if (heartbeat_due) {
				retained.next_heartbeat_us =
					next_heartbeat_deadline(z_nrf_grtc_timer_read());
			}
		}

		if (mr.moving) {
			was_moving = true;
			rest_since = 0;
			rest_frame_pending = false;
			if (first_moving_uptime == 0) {
				first_moving_uptime = mr.now;
			}
		} else if (was_moving) {
			if (rest_since == 0) {
				rest_since = mr.now;
			} else if (!rest_frame_pending &&
				   ((mr.now - rest_since) >= REST_FRAME_DELAY_MS ||
				    (first_moving_uptime != 0 &&
				     (mr.now - first_moving_uptime) >= REST_FRAME_MAX_WAIT_MS))) {
				/* La trame "repos" n'est jamais bloquee par le
				 * plafond anti-rafale : c'est le signal de
				 * retour au calme, le bloquer laisserait HA
				 * fige sur "Detecte". */
				send_final_state_frame(&mr.accel, "repos");
				last_frame_a_uptime = k_uptime_get();
				first_moving_uptime = 0;
				was_moving = false;
				rest_since = 0;
				rest_frame_pending = true;
			}
		}

		if (health_due) {
			send_frame_b(mr.temp_valid ? mr.temp_cc : 0);
			retained.next_health_us = next_health_deadline(z_nrf_grtc_timer_read());
		}

		/* RAM ordinaire (pas RRAM) : cout d'ecriture negligeable, pas
		 * de souci d'usure -- sauvegarde a chaque cycle plutot que de
		 * suivre precisement quel champ a change (yaw/pitch/roll/
		 * heartbeat/pid peuvent tous evoluer independamment). */
		retained_save();

		k_sleep(K_MSEC(MOTION_POLL_INTERVAL_MS));
	}

	return 0;
}
