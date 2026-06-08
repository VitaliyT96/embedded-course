#pragma once

#include <stdatomic.h>

// Global variables for the scope to read
extern atomic_int fruit_pot_value;
extern atomic_bool fruit_is_active;

// Initialize touch pads and start the background task
void fruits_init(void);
