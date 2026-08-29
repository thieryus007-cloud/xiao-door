# XIAO nRF54LM20A Ultra-Low-Power BLE/IMU Sensor — Current State Report

**Prepared for:** Nordic Semiconductor support / DevZone
**Project:** Battery-powered BLE door/motion sensor (BTHome v2 over Home Assistant)
**Date:** 2026-08-28
**Board:** Seeed Studio XIAO nRF54LM20A Sense (nRF54LM20A SoC + nPM1300 PMIC + LSM6DS3TR-C IMU)
**SDK:** nRF Connect SDK v3.4.0 (Zephyr base), GNU ARM toolchain (NCS toolchain manager, bundle `dcbdc366a1`)
**Reference design:** sibling project on nRF52840 (Seeed XIAO nRF52840 Sense) achieves ~10 µA with equivalent BLE + motion-detection functionality — used throughout as the practical target.

---

## 1. Executive summary

We are porting a battery-powered BLE motion/door sensor from an nRF52840-based
design to the XIAO nRF54LM20A Sense. The application: periodic accelerometer
polling (~1 s reactivity), BTHome v2 health telemetry over BLE advertising
every 15 minutes, all running from a single-cell LiPo through the on-board
nPM1300 PMIC.

An initial architecture (System OFF + full SoC reboot on every ~1 s poll
cycle) plateaued at **70–144 µA** average current — well above target and
found to be structurally limited by fixed re-initialization costs paid on
every reboot. Pivoting to a **System ON IDLE** architecture (no reboot; the
SoC sleeps via `CONFIG_PM` tickless idle and wakes on a GRTC-backed
`k_sleep()`) removed that structural floor. A sequence of single-variable
tests — architecture pivot, driver-delay tuning against documented specs,
Nordic's official RAM power-down library, and IMU output-data-rate tuning —
has brought measured current down to **~20–22 µA**, a 3.4–7× reduction from
the legacy architecture.

**One unresolved item is now the main blocker to reaching the 5–6 µA target**
and is the subject of the support request in §8: the nPM1300 `LOADSW1/LDO1`
rail (used to power the IMU) draws **~250–300 µA** whenever enabled, with **no
load and no I2C traffic**, independent of LDO vs. Load Switch mode and
independent of firmware — this figure represents ~70–80% of the current
per-cycle cost and has resisted 13+ independent isolation tests.

---

## 2. Hardware configuration

| Item | Detail |
|---|---|
| Board | Seeed Studio XIAO nRF54LM20A Sense |
| SoC | nRF54LM20A (Cortex-M33, nRF54L series peripheral set) |
| PMIC | nPM1300 (I2C-controlled, battery charger + 2× LDO/LOADSW + bucks) |
| IMU | ST LSM6DS3TR-C (accelerometer + gyroscope, I2C, on `imu_vdd`/LDO1 rail — shared with the on-board PDM microphone) |
| External flash | PY25Q64 (SPI NOR, kept in deep power-down / deterministic pin state) |
| Power source | Single-cell LiPo via nPM1300, no USB connected during measurement |
| Debug/flash interface | On-board SAMD11 USB↔SWD bridge (CMSIS-DAP), OpenOCD |
| Current measurement | Nordic PPK2, Ampere-meter mode, external supply on BAT+/BAT−, USB-C **always disconnected** during measurement (verified protocol, no simultaneous USB+PPK2 connection at any point) |

---

## 3. Software / toolchain

- **nRF Connect SDK:** v3.4.0, workspace at a local mirror (`C:\ncs\v3.4.0`)
- **Board target:** `xiao_nrf54lm20a/nrf54lm20a/cpuapp`
- **Board support package:** community Seeed Studio module (`platform-seeedboards`), used via `BOARD_ROOT`
- **Toolchain:** NCS toolchain manager bundle, GNU ARM Embedded compiler, `west` 1.5.0
- **Build command:**
  ```bash
  west build -b xiao_nrf54lm20a/nrf54lm20a/cpuapp -d build --pristine \
    -- -DBOARD_ROOT="<path>/platform-seeedboards/zephyr"
  ```
- **Flash/verify:** OpenOCD + CMSIS-DAP over the on-board SAMD11 bridge; every
  flash is verified byte-for-byte with `verify_image` against the `.hex`
  (never `dump_image`+`cmp`, which produces false positives on RRAM padding
  that was written by a previous, different binary).

---

## 4. Firmware architecture

### 4.1 Legacy architecture (abandoned)

`System OFF` + full SoC reboot on every ~1 s poll cycle
(`z_nrf_grtc_wakeup_prepare()` + `sys_poweroff()`), matching the classic
nRF52-era ultra-low-power pattern. On this SoC this plateaued at
**70–144 µA**: every reboot re-initializes the MFD/regulator driver, the BLE
controller/host, and (until deferred-init was applied) the nPM1300 charger —
fixed per-boot costs that dominate at a 1 Hz duty cycle. Root-caused via the
Nordic datasheet's own power-consumption table (System ON IDLE + GRTC + full
RAM retained = 4.3 µA documented floor, vs. 0.7–1.0 µA for System OFF) — the
gap was never the power *mode* itself, but the fact that a reboot re-runs
driver `POST_KERNEL` init on every cycle, none of which is retained.

### 4.2 Current architecture (System ON IDLE)

- `main()` is a single infinite loop; the SoC is **never rebooted** during
  normal operation. `CONFIG_PM=y` enables Zephyr's tickless idle so
  `k_sleep()` between cycles is a genuine WFI-based CPU sleep, not a
  busy/software wait.
- Bluetooth (`bt_enable()`) is initialized **once**, at true cold boot —
  removing a per-cycle BLE-stack-init cost that had been the single largest
  contributor under the reboot architecture.
- The accelerometer (`imu_vdd`/LDO1 + LSM6DS3TR-C) is still power-cycled
  every ~1 s (regulator enable → sample → regulator disable) — this remains
  necessary because the rail itself draws ~250–300 µA whenever active (see
  §8); duty-cycling it is the only way found so far to keep the average
  current down while still meeting the ~1 s motion-detection reactivity
  requirement.
- BTHome "health" frames (battery %, voltage) are advertised only when due
  (every 15 minutes), gated **before** any BLE call so the BLE stack is
  touched only for the ~700 ms burst that's actually needed.
- RAM sections unused by the linked image (~94% of the SoC's 512 KB) are
  powered down via Nordic's official `RAM_POWER_DOWN_LIBRARY`
  (`nrf/lib/ram_pwrdn`), called once after boot.

### 4.3 Correctness detail specific to the pivot

Because the SoC no longer reboots, `device_init()` on the LSM6DSL driver
only runs its low-level chip-init routine (`lsm6dsl_init_chip()`) **once**
(Zephyr's `device_init()` returns `-EALREADY` without re-invoking the
driver's `init` callback once `dev->state->initialized` is set — see
`zephyr/kernel/device.c`). Since `imu_vdd` is still power-cycled every
sample, the physical IMU register state resets every cycle even though the
Zephyr-side "initialized" flag does not. `CTRL3_C` (BDU + address
auto-increment) and `CTRL6_C` (low-power mode) are therefore now
re-asserted explicitly by direct I2C write on every sample, independent of
the Zephyr sensor API's init state — without this, X/Y/Z readings would
silently become inconsistent from the second sample onward.

---

## 5. What currently works (functionally verified)

- BLE advertising: fixed static random identity (from `hwinfo` device ID),
  non-connectable, BTHome v2 service-data payload.
- BTHome "health" frame (battery %, battery voltage, packet ID, low-battery
  flag) sent on a 15-minute cadence, correct values confirmed via battery
  reading (`"battery=100% (4228 mV)"` etc., verified with a temporary
  console before switching to the silent measurement build).
- Accelerometer polling every ~1 s: plausible, stable X/Y/Z readings
  verified via temporary UART console across many consecutive cycles after
  every firmware change with functional risk (e.g. after enabling
  `RAM_POWER_DOWN_LIBRARY`, after the ODR change) — no crash, no bus fault,
  no reset loop observed in any test.
- Retained-RAM state (packet ID, next health-frame deadline) survives an
  unexpected reset via a CRC-validated struct in a `zephyr,retained-ram`
  region — no longer required for the normal operating cycle (the SoC does
  not reboot), kept only as a safety net.

**Console/serial is hard-disabled (`CONFIG_SERIAL=n`) for every measurement
build.** A separate, temporary console-enabled build is used for functional
verification, then reverted before any PPK2 measurement.

---

## 6. Test & measurement methodology

1. State the objective, the exact parameter(s) changed and in which file,
   and the expected result **before** building — single-variable changes
   only, one at a time between two measurements.
2. Build, flash, and verify byte-for-byte (`verify_image`) — re-flash and
   re-verify if it fails.
3. If the change carries any functional-correctness risk (e.g. touches
   sensor register state or RAM layout), do a temporary console-enabled
   functional check (several polling cycles observed over UART) **before**
   switching to the silent measurement build.
4. Confirm `CONFIG_SERIAL=n` / `CONFIG_CONSOLE=n` / `CONFIG_UART_CONSOLE=n`
   / `CONFIG_PRINTK=n` are in effect for the measurement build (a UARTE
   driver PM-runtime reference leak was found early on that silently keeps
   the console active in System OFF once any other peripheral is also
   active — console is now always hard-disabled, never runtime-suspended).
5. PPK2 measurement protocol (strict, never varied):
   1. Disconnect the board from USB-C.
   2. Wait 20 s.
   3. Connect the board to the PPK2 (BAT+/BAT− only).
   4. Power the board from the PPK2.
   5. Wait 10 s.
   6. Start the measurement, capture screenshots at multiple zoom levels
      (whole-window average, single-cycle burst, idle-floor zoom).
   7. Stop the PPK2 power output.
   8. Disconnect the board from the PPK2.
   9. Reconnect the board to USB-C.
   PPK2 and USB-C are **never** connected to the board simultaneously at
   any point in this protocol.
6. Record measured µA (whole-window average) and µC (charge — used to
   compare cycles independent of small interval-count variance across
   measurement windows).

---

## 7. Measured results

All measurements: unit "#01", board at rest, no motion, single-cell LiPo via
nPM1300, PPK2 Ampere-meter mode.

| Stage | Change | Avg. current (10 s window) |
|---|---|---|
| Legacy architecture | System OFF + full reboot every ~1 s cycle | 70–144 µA |
| Architecture pivot | System ON IDLE, no reboot, `CONFIG_PM=y` | 30.85 µA |
| Test #32 | `imu_vdd`/LDO1 stabilization delay 20→5 ms (nPM1300 datasheet soft-start spec: 1.8 ms typ.) | 27.07 µA |
| Test #33 | IMU ODR-settle delay 15→10 ms (104 Hz period ≈ 9.6 ms) | 25.21 µA |
| Test #35 | Nordic `RAM_POWER_DOWN_LIBRARY` — unused RAM sections powered off | 20.63 µA |
| Test #36 | IMU ODR 104→208 Hz, settle delay 10→6 ms | ~20–22 µA¹ |

¹ Raw 10 s window for test #36 read 21.54 µA, but that capture happened to
include one BLE health-frame burst (triggered once per boot by the retained
"next health deadline" resetting on a fresh flash) — not directly
comparable to the other rows, which did not contain that event. The
per-cycle burst charge (18.40 µC, down from 17.45–20.11 µC) and the
idle-floor reading (4.26 µA, essentially at the datasheet's documented
System ON IDLE floor of 4.3 µA) both confirm a real, if modest, further
improvement; a clean re-measurement is pending.

**Remaining gap to target:** 5–6 µA final objective, ~10 µA sibling
nRF52840 reference (with equivalent motion-detection functionality). The
per-cycle accelerometer sampling burst (currently ≈18 µC / ~15–19 ms) now
accounts for roughly 70–80% of total cycle cost and is dominated almost
entirely by the fixed ~250–300 µA `imu_vdd`/LDO1 rail current for as long as
it stays enabled — see §8.

### 7.1 Items checked against the datasheet and confirmed already optimal

No firmware change was needed for the following — each was verified in the
datasheet and/or the board's devicetree/Kconfig, not assumed:

- **System ON sub-power mode** defaults to `Low-power` (not `Constant
  Latency`) on entry to System ON — the most power-efficient option,
  requires no software action (§5.1.1 of the datasheet).
- **RRAM (program memory) low-power mode** (`RRAMC.POWER.LOWPOWERCONFIG`)
  resets to `PowerOff` — RRAM is already fully powered off automatically
  during System ON IDLE.
- **Main SoC regulator (VREGMAIN) DC/DC mode** is already enabled by the
  board's devicetree (`regulator-initial-mode = <NRF5X_REG_MODE_DCDC>`),
  not left in the less-efficient LDO fallback mode.
- **HFXO vs. HFINT for I2C (TWIM):** the Zephyr TWIM driver never requests
  HFXO explicitly, and MPSL only requests HFXO on-demand around actual
  radio events (`CONFIG_MPSL_HFCLK_LATENCY`), not continuously or per I2C
  transaction — our I2C traffic to the IMU already runs on the always-on
  HFINT source with no crystal-startup cost paid per sampling cycle.
  Measured burst durations match our own explicit `k_msleep()` budget with
  no unexplained residual latency, confirming this by measurement as well
  as by code inspection.
- **TWIM serial power domain:** confirmed (via a community write-up on
  nRF54L peripheral power domains) that TWI/SPI/UARTE sit in their own
  collapsible domain, separate from the always-on domain. The Zephyr TWIM
  driver already implements `PM_DEVICE_ACTION_SUSPEND`/`RESUME`
  (`i2c_nrfx_twim_common.c`, `pm_device_driver_init()`), and with
  `CONFIG_PM_DEVICE_RUNTIME=y` (already set) the bus auto-suspends between
  our transactions — no code change needed.
- **CRACEN/PSA crypto (see §9):** confirmed to live in the auto-managed
  MCU power domain (Nordic Developer Academy nRF54L course material),
  not a separately always-on block — consistent with it not showing up
  as a recurring cost in steady-state measurements.

---

## 8. Request for support: `imu_vdd`/`LOADSW1`/`LDO1` idle current

This is the primary open question and the main blocker to reaching the
final target.

**Observation:** enabling the nPM1300's `LOADSW1`/`LDO1` output alone — no
I2C traffic to any device on the rail, no sensor reads, nothing else
changed — increases current from a 3.3 µA baseline (SoC in System ON IDLE,
GRTC + BLE only, this rail disabled) to **~250–275 µA**, and this figure is
**stable over multi-minute windows** (not a decaying transient).

**Isolation performed (9 independent single-variable tests, all confirm the
same ~250–275 µA):**

| # | Variable tested | Result |
|---|---|---|
| 1 | Wait up to 5 s for INT1 to settle before power-down | No effect |
| 2 | `WAKE_UP_DUR` debounce register (separate from the 31 mg threshold, which is never modified) | No effect |
| 3 | 200 ms stabilization delay before arming any interrupt | No effect |
| 4 | UARTE console PM-runtime-suspend bug fixed (console hard-disabled) | No effect |
| 5 | I2C bus explicitly suspended (`PM_DEVICE_ACTION_SUSPEND`) | No effect |
| 6 | Accelerometer low-power mode (`XL_HM_MODE`) — verified set via direct register read-back | Confirmed correct, not the cause |
| 7 | **No wake source armed at all** — IMU powered/initialized only | Still ~260 µA — rules out any wake mechanism |
| 8 | `power_en` regulator disabled (only `imu_vdd`/LDO1 active) | Still ~260 µA |
| 9 | **`imu_vdd`/LDO1 alone** — a single `regulator_enable()` call, no I2C to the IMU at all, base = GRTC+RAM+BLE (3.3 µA) | **~253 µA — cause isolated to `regulator_enable()` on this rail itself, reproducible, fully reversible** |
| 10 | Load Switch mode forced instead of LDO (register-verified `LDSWSEL=0x00`) | **275.67 µA — worse than LDO, and outside the LDO's rated voltage safety margin for the IMU** |
| 11 | A second `LDSWSTATUS` read added just before disabling the rail (testing whether the current depends on periodic TWI "refresh" rather than a one-time workaround — by analogy with erratum [31]'s wording, "**prompt** read or write over TWI from host") | **No reduction — burst charge increased slightly (18.40→20.64 µC) from the extra transaction's own cost on the (100 kHz) PMIC bus, separate from the IMU's (400 kHz) bus. Ruled out.** |

**Further checks performed, all negative:**

- **Complete nPM1300 Rev1 errata index checked** (not just [38]):
  anomalies [27], [28], [30]–[32], [34], [36], [38], [40], [41] — only
  [38], [40], [41] concern LOADSW/LDO, and all three are already ruled
  out below. No unconsidered LOADSW/LDO erratum exists in the official
  list.
- **nPM1300 errata [38]** (LOADSW/LDO startup-time-exceeds-spec, applicable
  condition: BUCKs unloaded + no TWI traffic after enable) — the Zephyr
  driver (`regulator_npm13xx_enable()`) already applies Nordic's documented
  workaround (a 2 ms delay + one `LDSWSTATUS` read). We extended this to 20
  reads over 100 ms: **no change (253 µA)** — errata [38] ruled out.
- **Shared microphone rail:** `imu_vdd` and the on-board PDM microphone's
  `dmic_vdd` are the same physical LDO1 node (Seeed's own devicetree labels
  it `imu_vdd: dmic_vdd: LDO1`). Driving the floating `PDM_CLK` pin to a
  defined low level: **no change (253 µA)** — ruled out.
- **`LDSWCONFIG`** (soft-start / active-discharge register): read back at
  its reset value (`0x00`) on the build where `regulator_enable()` is never
  called — nothing in our code writes it.
- **nPM1300 Product Specification, Table 23/24 (LOADSW/LDO electrical
  spec):** no quiescent/ground current figure is documented for this block
  in either mode.
- **Reproduced against the vendor reference example** (Seeed's own
  `imu_click` sample, run unmodified): **same order of magnitude
  (320–350 µA)** — this is not specific to our firmware.

**Question for Nordic:** is there a known quiescent/ground current for the
`LOADSW1`/`LDO1` block on the nPM1300 that is not captured in the
datasheet's electrical-specification tables, or a configuration step
(beyond mode, voltage, soft-start, and active-discharge, all already
checked) required to reach a low idle current in LDO mode with no load?
Given this rail's current dominates ~70–80% of our per-cycle cost, resolving
it is the single highest-value remaining step toward the 5–6 µA target.

---

## 8.1 Nordic's reply and our follow-up

Nordic support (2026-08-28) pointed us to the **Rev2** errata index
(`errata_npm1300_rev2`) for [38]/[40]/[41] and asked us to confirm the
workarounds are in place, noting they have not found any documentation
of an internal LDO leakage on their side. They asked for a current trace,
with and without the LDO enabled.

**Rev2 errata checked in full** (all three): text is identical in
substance to Rev1 for [38] (workaround: "trigger any TWI command after
enabling the LDO" — already applied via the Zephyr driver's built-in
2 ms + `LDSWSTATUS` read, extended to 20 reads over 100 ms in our own
testing with no effect). [40] (voltage drop, condition: LDO powered via
VSYS with BUCK in forced PFM, or two LDOs started within 200 µs of each
other) and [41] (reset risk, condition: VBAT within 300 mV of VSYSPOF)
**do not apply to our configuration** — we power LDO1 directly (no BUCK
in the path), start only one LDO, and have never observed a reset
(battery consistently at a healthy ~4.2 V in our tests).

**Trace captured and provided** (single continuous PPK2 capture, 60 s,
unit #01, dedicated diagnostic build — `regulator_enable()` on
`imu_vdd`/LDO1 called once and left on indefinitely, **zero I2C traffic**
to any device on the rail, no sensor reads):

| Segment | Duration | Average | Notes |
|---|---|---|---|
| Baseline (LDO never enabled) | 0–13.86 s | **3.90 µA** | max 21.12 µA, charge 54.08 µC |
| Enable transition | ~14.6–15 s | — | brief burst up to ~32 mA (soft-start + errata [38] workaround) |
| LDO enabled, steady state | remainder of capture | **~253.98–254.06 µA** | max ~400–477 µA (periodic housekeeping spikes riding on top), charge 877.57 µC over 3.455 s of the plateau |

The steady-state figure (**~254 µA**) reproduces our original isolation
test (test #9 from an earlier session: **253 µA**, same methodology)
almost exactly — independent reproduction on the same unit, different
firmware build, same result. Raw CSV export of the full 60 s capture
kept alongside this report (`nRF54LM20A/` project folder) for reference.

## 9. Secondary observation (not yet confirmed as a recurring cost)

The build links in the full NCS security stack — `mbedtls`, PSA Crypto,
and the CRACEN hardware crypto accelerator/KMU (`CONFIG_NRF_SECURITY=y`,
`CONFIG_PSA_CRYPTO_DRIVER_CRACEN=y`, `CONFIG_CRACEN_IKG_SEED_LOAD=y`, etc.)
— for a BLE **broadcaster-only** application with no pairing, no
connections, and no application-level cryptography. This appears to be
pulled in transitively through the Bluetooth host/controller's own
randomness requirements on this SoC family, not something our application
code requests. Because `bt_enable()` is now called only once (at true cold
boot, not every cycle), any associated CRACEN/KMU provisioning cost would
be a one-time boot cost rather than a recurring one — it does not appear in
our 10 s steady-state measurements, and we have **not** identified evidence
of it holding any power domain active in the background afterward. Flagged
here for completeness in case Nordic support recognizes a known interaction
between this and the idle current figures above.

---

## 10. Key source files (current, significant configuration only)

### 10.1 `boards/xiao_nrf54lm20a_nrf54lm20a_cpuapp.overlay`

```dts
&power_en {
	/delete-property/ regulator-boot-on;
};

#include <zephyr/dt-bindings/regulator/npm13xx.h>

&pmic {
	regulators {
		imu_vdd: LDO1 {
			regulator-min-microvolt = <3300000>;
			regulator-max-microvolt = <3300000>;
		};
	};
};

/* Deferred-init: SYS_INIT would otherwise run before main() powers imu_vdd. */
&lsm6ds3tr_c {
	zephyr,deferred-init;
};

/* Deferred-init: charger init() writes ~12-15 I2C transactions at every
 * boot even without a battery read; only needed when a reading is due. */
&pmic_charger {
	zephyr,deferred-init;
};

/ {
	cpuapp_sram@2007ec00 {
		compatible = "zephyr,memory-region", "mmio-sram";
		reg = <0x2007ec00 DT_SIZE_K(4)>;
		zephyr,memory-region = "RetainedMem";
		status = "okay";

		retainedmem0: retainedmem {
			compatible = "zephyr,retained-ram";
			status = "okay";
		};
	};

	aliases {
		retainedmemdevice = &retainedmem0;
	};
};

&cpuapp_sram {
	reg = <0x20000000 DT_SIZE_K(507)>;
	ranges = <0x0 0x20000000 0x7ec00>;
};

&pmic_leds {
	status = "disabled";
};

&py25q64 {
	status = "okay";
};

&usbhs {
	status = "disabled";
};

&usbhs_wrapper {
	status = "disabled";
};
```

### 10.2 `prj.conf`

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

# Tickless idle -- required for k_sleep() to be a real WFI sleep between
# ~1 s poll cycles, not a busy/software wait.
CONFIG_PM=y
CONFIG_PM_DEVICE=y
CONFIG_PM_DEVICE_RUNTIME=y
CONFIG_HWINFO=y

# Official NCS library, explicitly supported for CONFIG_SOC_NRF54LM20A_CPUAPP
# (nrf/lib/ram_pwrdn) -- powers down RAM sections beyond the linked image
# end (~23.5 KB used out of 507 KB retained by the overlay).
CONFIG_RAM_POWER_DOWN_LIBRARY=y

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

### 10.3 `src/main.c` — structure

- `sample_motion()` — enables `imu_vdd`, re-asserts `CTRL3_C`/`CTRL6_C` by
  direct I2C write every cycle (see §4.3), sets ODR (208 Hz), reads
  X/Y/Z in one burst I2C transaction, disables `imu_vdd`.
- `main()` — one-time init (LED GPIOs released, external-flash pins forced
  to a defined low-leakage state, fixed BLE identity, `bt_enable()`,
  `power_down_unused_ram()`), then an infinite loop:
  `sample_motion()` → send BTHome health frame if due (15 min cadence) →
  `k_sleep(K_MSEC(1000))`.
- No `sys_poweroff()` / `z_nrf_grtc_wakeup_prepare()` anywhere — the SoC is
  never rebooted during normal operation.

Full source available on request; this report includes only the
sections materially relevant to the power-consumption question in §8.

---

## 11. Summary for Nordic

| | |
|---|---|
| Current measured (steady state, no motion) | ~20–22 µA |
| Target | 5–6 µA |
| Sibling reference (nRF52840, same function) | ~10 µA |
| Dominant remaining cost | `imu_vdd`/LDO1 rail: ~250–300 µA whenever enabled, cause not yet identified (§8) |
| Everything else checked against the datasheet | Already at documented/optimal defaults (§7.1) |

We would welcome any guidance on the `LOADSW1`/`LDO1` idle-current question
in §8 — it is, by a wide margin, the single highest-leverage item standing
between the current measured result and the project's target.
