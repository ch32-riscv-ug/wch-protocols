// Kept out of the .ino so the Arduino prototype generator sees the type first.
#pragma once
#include <stdint.h>
struct Reply { uint32_t data; bool parity_ok; };
