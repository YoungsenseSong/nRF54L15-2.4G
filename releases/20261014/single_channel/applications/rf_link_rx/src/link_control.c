#include "link_control.h"

#include <errno.h>
#include <string.h>
#include <zephyr/spinlock.h>

#include "frame_queue.h"
#include "sync_manager.h"

static struct k_spinlock control_lock;
static struct link_control_status control_status;

void link_control_init(void)
{
	k_spinlock_key_t key = k_spin_lock(&control_lock);

	memset(&control_status, 0, sizeof(control_status));
	control_status.streaming =
		IS_ENABLED(CONFIG_RF_LINK_DEBUG_UART_TRANSPORT);
	k_spin_unlock(&control_lock, key);
}

int link_control_handle_command(enum fpga_command_code command,
				uint32_t argument, void *context)
{
	int ret = 0;
	k_spinlock_key_t key;

	ARG_UNUSED(context);
	switch (command) {
	case FPGA_CMD_ARM_SYNC:
		frame_queue_clear();
		ret = sync_manager_arm(argument);
		key = k_spin_lock(&control_lock);
		control_status.arm_commands++;
		control_status.streaming = false;
		k_spin_unlock(&control_lock, key);
		break;
	case FPGA_CMD_START_STREAM:
		key = k_spin_lock(&control_lock);
		control_status.start_commands++;
		control_status.streaming = true;
		k_spin_unlock(&control_lock, key);
		break;
	case FPGA_CMD_STOP_STREAM:
		key = k_spin_lock(&control_lock);
		control_status.stop_commands++;
		control_status.streaming = false;
		k_spin_unlock(&control_lock, key);
		break;
	case FPGA_CMD_RESET_LINK:
		frame_queue_clear();
		sync_manager_stop();
		key = k_spin_lock(&control_lock);
		control_status.reset_commands++;
		control_status.streaming = false;
		k_spin_unlock(&control_lock, key);
		break;
	case FPGA_CMD_CLEAR_STATS:
		break;
	default:
		key = k_spin_lock(&control_lock);
		control_status.invalid_commands++;
		k_spin_unlock(&control_lock, key);
		ret = -ENOTSUP;
		break;
	}

	return ret;
}

bool link_control_streaming(void)
{
	k_spinlock_key_t key = k_spin_lock(&control_lock);
	bool streaming = control_status.streaming;

	k_spin_unlock(&control_lock, key);
	return streaming;
}

void link_control_get_status(struct link_control_status *status)
{
	k_spinlock_key_t key;

	if (status == NULL) {
		return;
	}

	key = k_spin_lock(&control_lock);
	*status = control_status;
	k_spin_unlock(&control_lock, key);
}
