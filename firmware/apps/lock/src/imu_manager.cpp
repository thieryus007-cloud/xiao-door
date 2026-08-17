/*
 * Copyright (c) 2026 Thiery Fontaine
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*
 * Lecture périodique de l'IMU LSM6DS3TR-C (accéléromètre + gyroscope) et mise à
 * jour du cluster Matter Boolean State (porte ouverte/fermée) -- Priorité 2 du
 * cahier des charges (voir docs/XIAO-Door-specs.md).
 *
 * Étape volontairement simple : polling par timer classique, pas d'interruption
 * de réveil sur mouvement (le moteur bas-niveau wake-up du chip -- registres
 * WAKE_UP_THS/MD1_CFG -- n'est pas exposé par le driver Zephyr LSM6DSL standard
 * et sera une étape de suivi séparée, une fois ce pipeline de données validé sur
 * matériel réel). L'alimentation du capteur (régulateur imu_vdd) et sa
 * fréquence d'échantillonnage sont configurées au boot via devicetree/Kconfig
 * (voir boards/xiao_nrf54lm20a_nrf54lm20a_cpuapp.overlay et prj.conf) -- aucune
 * configuration matérielle à faire ici, seulement lire les échantillons.
 */

#include "imu_manager.h"
#include "app/task_executor.h"

#include <app-common/zap-generated/attributes/Accessors.h>
#include <app/clusters/boolean-state-server/CodegenIntegration.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

#include <cmath>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

namespace
{
constexpr chip::EndpointId kDoorEndpointId = 1;

/* Seuil "porte ouverte" du cahier des charges (XIAO-Door-specs.md §4.1.1 :
 * "Angle > 15° = Ouvert"). Référence "porte fermée" = 0°, pas de calibration
 * par unité pour l'instant (Phase 5 du cahier des charges). */
constexpr double kDoorOpenThresholdDeg = 15.0;

const struct device *ImuDevice()
{
	return DEVICE_DT_GET(DT_ALIAS(imu0));
}
} /* namespace */

ImuManager ImuManager::sInstance;

void ImuManager::Init()
{
	const struct device *imu = ImuDevice();

	if (!device_is_ready(imu)) {
		LOG_ERR("IMU device (lsm6ds3tr_c) not ready -- door state via IMU disabled");
		return;
	}

	k_timer_init(&mPollTimer, &ImuManager::PollTimerEventHandler, nullptr);
	k_timer_user_data_set(&mPollTimer, this);
	k_timer_start(&mPollTimer, K_MSEC(kPollIntervalMs), K_MSEC(kPollIntervalMs));

	LOG_INF("IMU manager initialized, polling every %u ms", kPollIntervalMs);
}

void ImuManager::PollTimerEventHandler(k_timer *timer)
{
	/* Contexte ISR (horloge système) -- on ne fait jamais d'I2C ici, on poste
	 * la lecture réelle dans le contexte de la tâche applicative, comme
	 * BoltLockManager::ActuatorTimerEventHandler. */
	ImuManagerEvent event;
	event.manager = static_cast<ImuManager *>(k_timer_user_data_get(timer));
	Nrf::PostTask([event] { PollAppEventHandler(event); });
}

void ImuManager::PollAppEventHandler(const ImuManagerEvent &event)
{
	event.manager->ReadAndUpdate();
}

void ImuManager::ReadAndUpdate()
{
	const struct device *imu = ImuDevice();

	int err = sensor_sample_fetch(imu);
	if (err) {
		LOG_ERR("IMU sample fetch failed: %d", err);
		return;
	}

	struct sensor_value accel[3];
	struct sensor_value gyro[3];

	sensor_channel_get(imu, SENSOR_CHAN_ACCEL_XYZ, accel);
	sensor_channel_get(imu, SENSOR_CHAN_GYRO_XYZ, gyro);

	const double accelX = sensor_value_to_double(&accel[0]);
	const double accelZ = sensor_value_to_double(&accel[2]);

	/* Angle d'inclinaison approximatif (roll) à partir de l'accéléromètre --
	 * axes X/Z choisis arbitrairement en l'absence de montage final sur une
	 * porte réelle ; à confirmer/ajuster en observant les valeurs pendant les
	 * tests physiques (basculer la carte à la main et vérifier dans HA que
	 * l'entity bascule au bon moment). */
	const double angleDeg = std::atan2(accelX, accelZ) * (180.0 / 3.14159265358979323846);
	const bool isOpen = std::fabs(angleDeg) > kDoorOpenThresholdDeg;
	const int32_t angleCentiDeg = static_cast<int32_t>(angleDeg * 100.0);

	LOG_INF("IMU accel=(%d.%06d, %d.%06d, %d.%06d) g, gyro=(%d, %d, %d) dps, angle=%d (x0.01 deg), door=%s",
		accel[0].val1, accel[0].val2, accel[1].val1, accel[1].val2, accel[2].val1, accel[2].val2,
		gyro[0].val1, gyro[1].val1, gyro[2].val1, angleCentiDeg, isOpen ? "open" : "closed");

	auto booleanState = chip::app::Clusters::BooleanState::FindClusterOnEndpoint(kDoorEndpointId);
	if (booleanState == nullptr) {
		LOG_ERR("Boolean State cluster not found on endpoint %u", kDoorEndpointId);
		return;
	}

	booleanState->SetStateValue(isOpen);
}
