#pragma once

#include <stdint.h>
#include "lib/defs.h"

#define POKE(type, addr) ((struct PACKED { type value; }*)addr)->value

