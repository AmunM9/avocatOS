/* Desktop simulator shared state. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

uint32_t sim_millis(void);
/* When non-zero the simulator runs on a fixed, reproducible clock (screenshots). */
time_t sim_fixed_time(void);
bool sim_fresh_settings(void);
void sim_set_brightness(uint8_t percent);
void sim_phone_connect(void);
void sim_phone_push(bool incoming_call);
void sim_set_charger(bool on);
