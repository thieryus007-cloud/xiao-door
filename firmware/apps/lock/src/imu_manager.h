/*
 * Copyright (c) 2026 Thiery Fontaine
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#pragma once

#include <zephyr/kernel.h>

#include <cstdint>

struct ImuManagerEvent;

class ImuManager {
public:
	void Init();

private:
	void ReadAndUpdate();

	static void PollTimerEventHandler(k_timer *timer);
	static void PollAppEventHandler(const ImuManagerEvent &event);

	static constexpr uint32_t kPollIntervalMs = 2000;

	k_timer mPollTimer = {};

	static ImuManager sInstance;
	friend ImuManager &ImuMgr();
};

inline ImuManager &ImuMgr()
{
	return ImuManager::sInstance;
}

struct ImuManagerEvent {
	ImuManager *manager;
};
