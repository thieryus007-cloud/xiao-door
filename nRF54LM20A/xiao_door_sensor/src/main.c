/*
 * Etape 2 (2026-08-27) -- base System OFF validee (test #13, 3,02 uA) +
 * GRTC + RAM retenue + BLE/BTHome (trame B sante/batterie uniquement).
 * Pas d'IMU dans ce test (mis de cote separement, voir
 * XIAO-nRF54LM20A-Solution-System-OFF.md) -- ni trame A (mouvement/
 * pitch/roll/yaw) ni trame C (accel/gyro brut), qui dependent toutes les
 * deux de donnees IMU.
 *
 * Intervalle trame B : 15 min (valeur de production, §4.3 -- remis en
 * place le 2026-08-27 apres validation du cycle GRTC->BLE->re-endormissement
 * a 60 s lors du test precedent).
 *
 * Elements System OFF repris a l'identique du test #13 (3,02 uA) :
 * LED deconnectees, flash SPI externe suspendu (driver + bus + broches
 * GPIO brutes), regulateurs power_en/LDO1 sans regulator-boot-on.
 * Console DESACTIVEE EN DUR (CONFIG_SERIAL=n) plutot que suspendue a
 * l'execution -- la suspension a l'execution (fonctionnelle en test #13
 * sans BLE) echoue systematiquement des que BLE est actif (bug driver
 * UARTE documente, voir prj.conf).
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
#include <zephyr/drivers/retained_mem.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/timer/nrf_grtc_timer.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/addr.h>

#define FRAME_B_INTERVAL_MS (15 * 60 * 1000)
#define ADV_BURST_MS         700

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

/* Test #16 (2026-08-27) -- configuration alignee sur l'exemple officiel
 * Seeed (wiki.seeedstudio.com/xiao_nrf54lm20a_with_onboard/, imu_click) :
 * imu_vdd/LDO1 alimente via regulator-boot-on (overlay) au lieu d'un
 * regulator_enable() manuel, et le driver LSM6DSL (zephyr,deferred-init
 * dans l'overlay) est initialise explicitement ici, une fois l'IMU sous
 * tension -- sans ca, son SYS_INIT automatique tourne avant main() (donc
 * avant toute alimentation), echoue silencieusement, et rien de ce que
 * fait le driver ensuite (bas niveau, interruption) ne s'applique
 * reellement. WK_THS/WAKE_UP_THS n'est modifie nulle part (consigne
 * explicite du projet). */
static const struct device *const lsm6dsl_dev = DEVICE_DT_GET(DT_NODELABEL(lsm6ds3tr_c));

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

	printf("\n=== %s ultra-low-power system off demo (etape 2 : GRTC+RAM+BLE) ===\n",
	       CONFIG_BOARD);

	rc = hwinfo_get_reset_cause(&reset_cause);
	if (rc == 0) {
		print_reset_cause(reset_cause);
	} else {
		printf("Warning: could not read reset cause (%d)\n", rc);
	}

	bool cold_boot = (reset_cause &
			   (RESET_PIN | RESET_SOFTWARE | RESET_POR | RESET_DEBUG)) != 0;
	bool have_state = retained_load();
	bool fresh_session = cold_boot || !have_state;

	if (!have_state) {
		memset(&retained, 0, sizeof(retained));
	}

	release_led_gpios();

	/* Test #16 (2026-08-27) -- imu_vdd/LDO1 alimente via regulator-boot-on
	 * (overlay, deja actif a ce stade) -- plus de regulator_enable()
	 * manuel. Initialisation differee du driver LSM6DSL declenchee
	 * explicitement maintenant que l'alimentation est stable. */
	rc = device_init(lsm6dsl_dev);
	if (rc < 0) {
		printf("Warning: lsm6dsl device_init failed (%d)\n", rc);
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

	printf("Bluetooth initialized -- etape 2 (GRTC+RAM+BLE, pas d'IMU): "
	       "fresh_session=%d reset_cause=0x%08x\n", fresh_session, reset_cause);

	uint64_t now_us = z_nrf_grtc_timer_read();
	bool health_due = fresh_session || (now_us >= retained.next_health_us);

	if (health_due) {
		send_frame_b();
		retained.next_health_us = next_health_deadline(z_nrf_grtc_timer_read());
	}

	rc = suspend_external_flash();
	if (rc < 0) {
		printf("Warning: flash low-power preparation incomplete (%d)\n", rc);
	}

	k_msleep(20);

	retained_save();

	uint64_t now2_us = z_nrf_grtc_timer_read();
	uint64_t wake_in_us = (retained.next_health_us > now2_us) ?
			       (retained.next_health_us - now2_us) : 0;

	if (wake_in_us < 1000000) {
		wake_in_us = 1000000; /* plancher 1s, z_nrf_grtc_wakeup_prepare
					* refuse une valeur trop basse */
	}

	rc = z_nrf_grtc_wakeup_prepare(wake_in_us);
	if (rc < 0) {
		printf("Warning: z_nrf_grtc_wakeup_prepare failed (%d) -- pas de reveil "
		       "periodique programme pour ce cycle\n", rc);
	}

	printf("Entering system off; next GRTC wake in %llu ms\n", wake_in_us / 1000);

	rc = hwinfo_clear_reset_cause();
	if (rc < 0) {
		printf("Warning: could not clear reset cause (%d)\n", rc);
	}

	sys_poweroff();

	while (1) {
		k_sleep(K_FOREVER);
	}
}
