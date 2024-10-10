#pragma once

#include <vector>
#include "utils.h"

namespace libfnt
{
	const RasterTarget* rasterise_outline(const OutlineData &Outline, float upem);
};