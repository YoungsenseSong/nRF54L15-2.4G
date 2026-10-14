#ifndef RF_LINK_TIMEBASE_H_
#define RF_LINK_TIMEBASE_H_

#include <stdint.h>

int timebase_init(void);
uint64_t timebase_now_ticks(void);
uint32_t timebase_frequency_hz(void);
void timebase_on_sync_capture(uint64_t captured_tick);
uint64_t timebase_last_sync_capture(void);

#endif /* RF_LINK_TIMEBASE_H_ */
