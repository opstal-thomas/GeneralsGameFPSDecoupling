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

#include <math.h>

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

	inline bool isRotationDiscontinuity(float quaternionDot)
	{
		// Quaternion representations q and -q are equivalent. A change of
		// more than 150 degrees in one 30 Hz tick is typically a scripted snap.
		// Cos(150 / 2) ~= 0.258819. Do not smear instant reorientation.
		const float cosineThreshold = 0.2588190451f;
		return quaternionDot > -cosineThreshold && quaternionDot < cosineThreshold;
	}

	// Integrate the authored 30 Hz recoil rate over fractional presentation
	// frames. Full steps preserve the legacy distance, and subdivisions compose.
	inline float recoilDampingFactor(float damping, float thirtyHzSteps)
	{
		if (!(thirtyHzSteps > 0.0f) || !(damping >= 0.0f))
			return 1.0f;
		return powf(damping, thirtyHzSteps);
	}

	inline float recoilTravel(float rate, float damping, float thirtyHzSteps)
	{
		if (!(thirtyHzSteps > 0.0f))
			return 0.0f;
		if (!(damping >= 0.0f) || fabsf(damping - 1.0f) < 0.00001f)
			return rate * thirtyHzSteps;
		return rate * (1.0f - recoilDampingFactor(damping, thirtyHzSteps)) / (1.0f - damping);
	}

	inline float presentationBlend(float perThirtyHzStep, float thirtyHzFrameRatio)
	{
		// Convert a legacy blend (e.g. 10% per 30 Hz render) into an
		// exponential blend for the actual frame duration. Unlike direct
		// multiplication this remains stable across large and small deltas.
		const float blend = clampAlpha(perThirtyHzStep);
		if (!(thirtyHzFrameRatio > 0.0f) || blend <= 0.0f)
			return 0.0f;
		if (blend >= 1.0f)
			return 1.0f;
		return 1.0f - powf(1.0f - blend, thirtyHzFrameRatio);
	}
}
