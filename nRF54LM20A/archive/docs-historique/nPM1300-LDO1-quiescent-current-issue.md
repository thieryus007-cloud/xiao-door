# nPM1300 LOADSW1/LDO1 quiescent current issue — XIAO nRF54LM20A Sense

Status: unresolved as of 2026-08-27. Written to summarize the investigation for external reference (e.g. Nordic DevZone) and to keep the exact configuration used in one place.

## Hardware / software

- Board: Seeed XIAO nRF54LM20A Sense (`xiao_nrf54lm20a/nrf54lm20a/cpuapp`), Seeed vendor board files (not upstream in NCS 3.4.0), board root `vendor/platform-seeedboards/zephyr`.
- SoC: nRF54LM20A. PMIC: nPM1300 (regulator + charger + fuel gauge).
- IMU: LSM6DS3TR-C, I2C address `0x6A`, on dedicated bus `i2c30`.
- NCS 3.4.0 / Zephyr OS v4.4.0.
- Toolchain: `dcbdc366a1` (nRF Connect SDK toolchain bundle).

## Objective

Deep-sleep (System OFF) current budget target: 5-6 µA continuous for the full device (BLE/BTHome periodic health frame + motion wake latency), beating a sibling nRF52840-based design that already achieves ~10 µA.

## Symptom

With the SoC in System OFF (GRTC periodic wake armed, BLE advertising burst sent once per session, then `sys_poweroff()`), measured quiescent current (PPK2, Source meter mode, 3.7 V, injected on BAT+/BAT- only — USB-C fully disconnected during measurement):

| Configuration | Measured current |
|---|---|
| Baseline: no `imu_vdd`/LDO1 activity at all | **3.3 µA** |
| `regulator_enable()` on `imu_vdd`/LDO1 alone, mode forced to LDO, nothing else changed | **253 µA** |
| Same, mode forced to Load Switch instead of LDO | **275.67 µA** |
| Same, no mode forced (`regulator-initial-mode` property removed) | **250 µA** |
| Same (LDO), plus 20 extra I2C status-register reads after enable (testing nPM1300 errata [38] workaround timing) | **253 µA** (no change) |
| Same (LDO), plus driving the (unrelated, shared-rail) PDM microphone's clock pin to a defined low level instead of floating | **253 µA** (no change) |
| Same (LDO) + LSM6DS3TR-C actually initialized (104 Hz, low-power mode) with all interrupt routing explicitly disabled (`INT1_CTRL`/`INT2_CTRL`/`MD1_CFG`/`MD2_CFG`/`TAP_CFG` = 0x00) | **300 µA** (the ~50 µA delta over the 250-253 µA baseline is consistent with the accelerometer's own expected low-power operating current — no anomaly from the sensor itself) |

**The ~250 µA jump is caused by `regulator_enable()` on LOADSW1/LDO1 itself**, reproducibly, and is independent of:
- which mode the channel is placed in (LDO vs. Load Switch — both elevated, LDO slightly lower)
- any I2C traffic to devices on that rail (none, some, or full sensor configuration all show the same ~250-300 µA floor)
- the shared PDM microphone's clock pin state

This is roughly two orders of magnitude above what would be expected for a modern PMIC LDO/load-switch channel at idle, and contradicts Seeed's own published reference figure for the same board (~4.76-4.93 µA with the IMU rail active).

## What has been checked and ruled out

- **`LDSWCONFIG` register (0x07, soft-start level / active discharge)**: read back directly via `mfd_npm13xx_reg_read()` — confirmed at its power-on reset value (`0x00`). Not the cause.
- **nPM1300 register persistence across SoC resets**: the PMIC stays powered independently of the SoC's System OFF state, so its I2C registers survive SoC-side flashes/resets. This was used to rule out stale test state, but a genuine full power-cycle (both USB-C and the PPK2/external supply disconnected simultaneously) was performed before each of the measurements above, so all figures reflect a true power-on-reset state of the PMIC.
- **Zephyr driver behavior**: `regulator_npm13xx_enable()` (in `drivers/regulator/regulator_npm13xx.c`) writes `TASKLDSW1SET`, then (workaround for nPM1300 errata [38], present by default) sleeps 2 ms and does one status-register read. Extending this to 20 reads over 100 ms made no difference.
- **nPM1300 official errata** (`docs.nordicsemi.com/bundle/errata_nPM1300_Rev1`): checked anomalies [38] (LDO startup time — ruled out per above), [40]/[41] (VSYS/VBAT drop or reset at LDO startup — describe transients, not a sustained elevated current, and don't match our symptom: no resets observed, battery-only supply). [27]/[31] are BUCK-specific, not LOADSW/LDO.
- **nPM1300 official datasheet electrical specification tables** (Product Specification v1.1, 4490_483, 2024-06-16, §6.4.1 "Electrical specification", Tables 23 and 24): **no quiescent/ground current parameter is documented at all** for the LOADSW/LDO block, in either mode. Only RDSON (200 mΩ typ), max output current (100 mA load switch / 50 mA LDO), soft-start time (1.8 ms typ), pull-down/active-discharge resistance (2 kΩ typ), and voltage ranges (VIN 2.6 V-VSYS, VOUT 1.0-3.3 V) are specified.
- **nRF54LM20A SoC errata**: [37] (POWER, current after pin reset), [114] (GPIO wake-on-pin), [105] (TWIM), [47] (debug reset in System OFF) — none applicable.
- **Zephyr PR #83790** (nPM1300 LDO HW-bug workaround, merged 2025-01-29, present in NCS 3.4.0 via `nordic,anomaly38-disable-workaround` devicetree property, default off = workaround active): already applied by default in this build.
- **Shared power rail**: `imu_vdd` and the onboard PDM microphone's `dmic_vdd` are the *same* devicetree regulator node (`imu_vdd: dmic_vdd: LDO1`). The microphone (`MSM261D3526H1CPM`) is never configured/clocked by this firmware. Its own datasheet specifies 290 µA typ. in "Low-Power Mode" vs. 1 µA typ. in "Sleep Mode" depending on clock condition — close to the measured anomaly, so its clock pin was driven to a defined level (instead of left floating) as a test. No change in measured current, ruling this out as the (or a) cause.
- **Community research** (Nordic DevZone threads on nPM1300 quiescent current, LDO enabling causing VSYS drop, BUCK PFM current): no thread matches this exact symptom (LOADSW/LDO alone, no load, elevated and stable current, independent of mode/load/downstream configuration).

## Exact configuration in use

### Devicetree overlay (`boards/xiao_nrf54lm20a_nrf54lm20a_cpuapp.overlay`, relevant excerpt)

```dts
&power_en {
	/delete-property/ regulator-boot-on;
};

#include <zephyr/dt-bindings/regulator/npm13xx.h>

&pmic {
	regulators {
		imu_vdd: LDO1 {
			/delete-property/ regulator-boot-on;
			regulator-min-microvolt = <3300000>;
			regulator-max-microvolt = <3300000>;
			/* regulator-initial-mode intentionally NOT set in the
			 * current test — measured 250 µA either way (with LDO
			 * mode forced: 253 µA; with Load Switch mode forced:
			 * 275.67 µA). */
		};
	};
};
```

### `prj.conf` (relevant excerpt)

```ini
CONFIG_SERIAL=n
CONFIG_CONSOLE=n
CONFIG_UART_CONSOLE=n
CONFIG_PRINTK=n
CONFIG_BOOT_BANNER=n
CONFIG_NCS_BOOT_BANNER=n
CONFIG_GPIO=y
CONFIG_SPI=y
CONFIG_FLASH=y
CONFIG_SPI_NOR=y
CONFIG_PM_DEVICE=y
CONFIG_PM_DEVICE_RUNTIME=y
CONFIG_POWEROFF=y
CONFIG_HWINFO=y

CONFIG_BT=y
CONFIG_BT_BROADCASTER=y
CONFIG_BT_DEVICE_NAME="XIAO-DOOR"

CONFIG_RETAINED_MEM=y
CONFIG_CRC=y

CONFIG_SENSOR=y
CONFIG_NPM13XX_CHARGER=y
CONFIG_MFD=y

CONFIG_REGULATOR=y

CONFIG_MAIN_STACK_SIZE=4096
CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=2048

CONFIG_REBOOT=y
```

### Minimal C reproduction (relevant excerpt from `src/main.c`)

```c
static const struct device *const imu_vdd_dev = DEVICE_DT_GET(DT_NODELABEL(imu_vdd));

int main(void)
{
	/* ... GPIO/LED cleanup, reset-cause handling ... */

	if (device_is_ready(imu_vdd_dev)) {
		int rc = regulator_enable(imu_vdd_dev);
		/* rc == 0. From this point on, measured current is
		 * ~250-253 µA instead of the ~3.3 µA baseline, with
		 * nothing else in the system changed. */
	}

	/* ... BLE advertising burst, retained-RAM save, GRTC wake
	 * arming, sys_poweroff() ... */
}
```

## Open question

Is there a known, undocumented quiescent/ground current for the nPM1300 LOADSW/LDO block when enabled (in either mode) with no external load, that simply isn't captured in the datasheet's electrical specification tables? Or is there a configuration step — beyond mode selection, output voltage, soft-start level, and active-discharge (all already verified at their default/expected values) — required to reach a low idle current on this block?

Any pointer to a known erratum, application note, or prior report of this exact behavior would be appreciated.
