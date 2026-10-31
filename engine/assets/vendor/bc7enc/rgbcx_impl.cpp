// The one translation unit that holds rgbcx's implementation (a single-header library).
// rgbcx.h uses memset and fabs without including their headers; clang does not get them transitively.
#include <cmath>
#include <cstring>
#include <math.h>
#include <string.h>

#define RGBCX_IMPLEMENTATION
#include "rgbcx.h"
