#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>

#define IDLE_SLEEP_MS 1000

int main(void)
{
	int err = bt_enable(NULL);

	if (err) {
		return err;
	}

	while (1) {
		k_sleep(K_MSEC(IDLE_SLEEP_MS));
	}

	return 0;
}
