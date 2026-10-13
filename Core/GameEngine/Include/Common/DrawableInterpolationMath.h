/*
**  Command & Conquer Generals Zero Hour(tm)
**  Copyright 2026 Contributors
**
**  This program is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
*/
#pragma once

// Presentation-only interpolation helpers. Never use these values to update
// authoritative simulation transforms, collision or the lockstep state.
namespace DrawableInterpolationMath
{
	inline float clampAlpha(float alpha)
	{
		// The comparison form deliberately treats NaN as zero.
		if (!(alpha > 0.0f))
			return 0.0f;
		return alpha < 1.0f ? alpha : 1.0f;
	}

	inline float lerp(float previous, float current, float alpha)
	{
		return previous + (current - previous) * alpha;
	}

	inline float shortestAngleDelta(float previous, float current)
	{
		const float pi = 3.14159265358979323846f;
		const float twoPi = 2.0f * pi;
		float delta = current - previous;
		while (delta > pi)
			delta -= twoPi;
		while (delta < -pi)
			delta += twoPi;
		return delta;
	}

	inline bool isTeleport(float dx, float dy, float dz)
	{
		// Keep instant repositioning from being smeared between logic frames.
		const float teleportThreshold = 60.0f;
		return (dx * dx + dy * dy + dz * dz) > teleportThreshold * teleportThreshold;
	}
}
