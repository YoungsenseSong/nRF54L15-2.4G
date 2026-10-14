#include "frame_queue.h"

#include <errno.h>
#include <string.h>
#include <zephyr/spinlock.h>

static struct rx_frame_record records[CONFIG_RF_LINK_FRAME_QUEUE_DEPTH];
static struct k_spinlock queue_lock;
static struct frame_queue_stats queue_stats;
static size_t read_index;
static size_t write_index;
static size_t record_count;

void frame_queue_init(void)
{
	k_spinlock_key_t key = k_spin_lock(&queue_lock);

	read_index = 0u;
	write_index = 0u;
	record_count = 0u;
	memset(&queue_stats, 0, sizeof(queue_stats));
	k_spin_unlock(&queue_lock, key);
}

int frame_queue_push(const struct rx_frame_record *record)
{
	k_spinlock_key_t key;

	if (record == NULL) {
		return -EINVAL;
	}

	key = k_spin_lock(&queue_lock);
	if (record_count == ARRAY_SIZE(records)) {
		queue_stats.overflow++;
		k_spin_unlock(&queue_lock, key);
		return -ENOSPC;
	}

	records[write_index] = *record;
	write_index = (write_index + 1u) % ARRAY_SIZE(records);
	record_count++;
	queue_stats.push_total++;
	if (record_count > queue_stats.high_water) {
		queue_stats.high_water = record_count;
	}
	k_spin_unlock(&queue_lock, key);
	return 0;
}

int frame_queue_peek(struct rx_frame_record *record)
{
	k_spinlock_key_t key;

	if (record == NULL) {
		return -EINVAL;
	}

	key = k_spin_lock(&queue_lock);
	if (record_count == 0u) {
		k_spin_unlock(&queue_lock, key);
		return -ENODATA;
	}

	*record = records[read_index];
	k_spin_unlock(&queue_lock, key);
	return 0;
}

int frame_queue_commit(uint32_t expected_extended_frame_seq)
{
	k_spinlock_key_t key = k_spin_lock(&queue_lock);

	if (record_count == 0u) {
		k_spin_unlock(&queue_lock, key);
		return -ENODATA;
	}

	if (records[read_index].extended_frame_seq != expected_extended_frame_seq) {
		k_spin_unlock(&queue_lock, key);
		return -ESTALE;
	}

	read_index = (read_index + 1u) % ARRAY_SIZE(records);
	record_count--;
	queue_stats.pop_total++;
	k_spin_unlock(&queue_lock, key);
	return 0;
}

void frame_queue_clear(void)
{
	k_spinlock_key_t key = k_spin_lock(&queue_lock);

	read_index = 0u;
	write_index = 0u;
	record_count = 0u;
	k_spin_unlock(&queue_lock, key);
}

size_t frame_queue_level(void)
{
	k_spinlock_key_t key = k_spin_lock(&queue_lock);
	size_t level = record_count;

	k_spin_unlock(&queue_lock, key);
	return level;
}

size_t frame_queue_high_water(void)
{
	k_spinlock_key_t key = k_spin_lock(&queue_lock);
	size_t high_water = queue_stats.high_water;

	k_spin_unlock(&queue_lock, key);
	return high_water;
}

void frame_queue_stats_get(struct frame_queue_stats *stats)
{
	k_spinlock_key_t key;

	if (stats == NULL) {
		return;
	}

	key = k_spin_lock(&queue_lock);
	*stats = queue_stats;
	k_spin_unlock(&queue_lock, key);
}
