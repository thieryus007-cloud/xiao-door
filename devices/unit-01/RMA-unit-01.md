# RMA Request — Seeed XIAO nRF54LM20A Sense — Defective onboard IMU (unit-01)

**Status:** Draft, ready to send to vendor. Fields marked `[TO FILL IN]` need order/purchase details only the
buyer has — everything else below is verified technical evidence.

## 1. Summary

The onboard 6-axis IMU (LSM6DS3TR-C, ST Microelectronics) on one specific **Seeed XIAO nRF54LM20A Sense**
unit never initializes and never responds on its I2C bus, while two other units of the exact same product,
tested with the **byte-identical firmware image** and the exact same test procedure, initialize and produce
correct live sensor readings every time. This points to a hardware defect isolated to this specific unit
(solder joint, component placement, or component failure on the IMU circuit), not a firmware or design issue.

## 2. Product / Unit Identification

| Field | Value |
|---|---|
| Product | Seeed Studio XIAO nRF54LM20A Sense |
| Order number | `[TO FILL IN]` |
| Purchase date | `[TO FILL IN]` |
| Vendor / storefront | `[TO FILL IN]` |
| Quantity in this order | `[TO FILL IN]` (at least 3 units of this SKU were purchased together; 2 of the 3 work correctly) |
| **Defective unit identifier** | Integrated CMSIS-DAP debug probe serial number **`9C4A557D`** (VID:PID `0x2886:0x0068`) — this is a unique, hardware-burned identifier read directly from the unit via SWD, and is the most reliable way to physically identify which of several visually-identical boards this report is about. |
| Reference "known-good" units used for comparison | Two other units of the same SKU, from the same purchase batch. One identified by debug probe serial **`C5F0E209`**; the second was also tested successfully but its probe serial was not logged (available on request — a 10-second non-destructive query). |

## 3. Defect Summary

- **Component affected:** onboard 6-axis IMU, ST Microelectronics **LSM6DS3TR-C** (accelerometer + gyroscope), I2C address `0x6A`, on the board's dedicated internal I2C bus.
- **Symptom:** the IMU never responds to I2C communication. The very first I2C transaction the (unmodified, stock Zephyr) sensor driver performs at boot — a chip identification / soft-reset register access — fails, and the driver's initialization is reported as failed for the entire lifetime of the running firmware.
- **Reproducibility:** 100% deterministic across multiple independent test attempts on this specific unit, including after a full power cycle (physically unplugging and replugging the USB cable, not just a debug-probe soft reset) — ruling out any transient or stale-state explanation.
- **Comparison:** the exact same firmware image, flashed with the exact same procedure, initializes this IMU correctly and produces live, correct accelerometer/gyroscope readings on **two other units of the same product**, tested moments before and after the defective unit, in the same session, with the same tools.

## 4. Test Environment

| Item | Detail |
|---|---|
| Host | macOS, Nordic nRF Connect SDK v3.2.1 (Zephyr RTOS 4.2.99) |
| Debug probe | CMSIS-DAP, integrated on each XIAO board (no external probe used) |
| Debug tool | OpenOCD 0.12.0 |
| Firmware under test | Custom application forked from Nordic's official `samples/matter/lock` reference sample (Matter-over-Thread), built with the standard, **unmodified** Zephyr `LSM6DSL` sensor driver (`zephyr/drivers/sensor/st/lsm6dsl/`) — the IMU read path uses stock upstream Zephyr code, not custom driver code, for reading the IMU |
| Firmware build identity | The **same compiled binary** (`merged.hex`, built once) was flashed to all 3 units — not 3 separate builds — eliminating any possibility of a build-to-build software difference explaining the discrepancy |
| Devicetree reference | IMU node: `lsm6ds3tr_c: lsm6ds3tr-c@6a` (I2C address `0x6A`), alias `imu0` — see `firmware/boards/xiao_nrf54lm20a/nrf54lm20a_cpuapp_common.dtsi` in the linked repository |

## 5. Test Methodology

For each unit, the identical procedure was followed:

1. Flash the identical firmware image via SWD (`west flash --runner openocd`).
2. Reset the board (for the defective unit, tested both via a debug-probe reset **and** via a full USB power
   cycle — same result both ways).
3. Set a hardware breakpoint (via OpenOCD, using the target's Cortex-M33 FPB unit — a genuine hardware
   breakpoint, not a software patch) at two addresses in the compiled firmware, resolved from debug symbols:
   - The code path taken when the IMU driver reports `device_is_ready() == false` (initialization failed).
   - The code path taken when initialization succeeds and periodic sensor reading begins.
4. Let the firmware run for several seconds and observe which breakpoint is hit.
5. For units where initialization succeeds, a second pair of breakpoints confirms that the periodic read
   loop actually completes a full accelerometer/gyroscope read cycle and reaches the point where the value is
   used by the application (i.e., not just "driver says ready" but "real data was successfully read").
6. Independently, Zephyr's internal device-driver state structure (`struct device_state`, containing the
   driver's own recorded initialization result code) was read directly from RAM over SWD for the IMU device
   and, for context, for two other on-board peripherals sharing the same I2C infrastructure — to distinguish
   an IMU-specific failure from a bus-wide or power-rail-wide failure.

This methodology reads the microcontroller's own internal state directly over the physical debug
interface — it does not rely on application-level logs or behavior, and is unaffected by any application
bug, since the same measurement was taken identically on all three units.

## 6. Test Results

| Unit | Debug probe serial | IMU driver init result | Periodic IMU read confirmed working |
|---|---|---|---|
| **Unit under RMA** | `9C4A557D` | ❌ **Failed** — `device_is_ready() == false`, tested after both a soft reset and a full USB power cycle | ❌ Never reached (initialization never succeeds) |
| Reference unit A | `C5F0E209` | ✅ **Succeeded** | ✅ Confirmed — full read cycle completes, sensor data reaches the application |
| Reference unit B | *(available on request)* | ✅ **Succeeded** | ✅ Confirmed — full read cycle completes, sensor data reaches the application |

## 7. Note on an unrelated, separate finding (explicitly NOT part of this RMA claim)

During this investigation, a **separate, unrelated** issue was found affecting the onboard PMIC (nPM1300)
I2C communication path — and this second issue reproduces **identically on all three units**, including the
two units that work fine otherwise. Because it affects all three units equally, it does not explain the
IMU-specific failure above and is **not part of this RMA request** — it is very likely a devicetree/firmware
configuration matter, not a hardware defect, and is being investigated separately. It is mentioned here only
for full transparency, since the same debugging session surfaced it. The comparative methodology in section 6
(identical firmware, identical test, different physical units) is precisely what let us separate this
general/harmless-for-the-IMU issue from the unit-specific IMU defect being reported here.

## 8. Conclusion / Request

Given: (a) the byte-identical firmware and test procedure across all three units, (b) 100% reproducible
failure on this one unit across multiple attempts including a full power cycle, and (c) 100% reproducible
success on two sibling units from the same order — the evidence points to a hardware defect specific to this
individual unit's IMU circuit (e.g. a solder joint, component placement, or component failure), not a
firmware, configuration, or design issue.

**Requesting:** replacement of this unit (debug probe serial `9C4A557D`) under warranty/RMA.

Photos of the physical board (if requested by the vendor) can be added to
`devices/unit-01/photos/` in the linked repository.

---

*This report was generated from a live diagnostic session on 2026-08-17. Full project repository, including
the exact firmware build configuration and additional technical detail, available at:
`https://github.com/thieryus007-cloud/xiao-door` — see in particular
`firmware/apps/lock/KNOWN-ISSUES.md` (section "Test comparatif sur 3 unités") and `devices/unit-01/unit-01.md`
for the raw session history.*
