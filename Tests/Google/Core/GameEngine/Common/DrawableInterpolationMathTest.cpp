/*
**  Command & Conquer Generals Zero Hour(tm)
**  Copyright 2026 Contributors
**  Distributed under GPL version 3 or later.
*/
#include <gtest/gtest.h>
#include <cmath>
#include "Common/DrawableInterpolationMath.h"

TEST(DrawableInterpolationMath, ClampsInvalidAndOutOfRangeAlpha)
{
	EXPECT_FLOAT_EQ(0.0f, DrawableInterpolationMath::clampAlpha(-1.0f));
	EXPECT_FLOAT_EQ(0.0f, DrawableInterpolationMath::clampAlpha(NAN));
	EXPECT_FLOAT_EQ(0.5f, DrawableInterpolationMath::clampAlpha(0.5f));
	EXPECT_FLOAT_EQ(1.0f, DrawableInterpolationMath::clampAlpha(2.0f));
}

TEST(DrawableInterpolationMath, PositionInterpolationUsesPreviousAndCurrentSnapshots)
{
	EXPECT_FLOAT_EQ(10.0f, DrawableInterpolationMath::lerp(10.0f, 20.0f, 0.0f));
	EXPECT_FLOAT_EQ(15.0f, DrawableInterpolationMath::lerp(10.0f, 20.0f, 0.5f));
	EXPECT_FLOAT_EQ(20.0f, DrawableInterpolationMath::lerp(10.0f, 20.0f, 1.0f));
}

TEST(DrawableInterpolationMath, RotationUsesShortestArcAcrossPiBoundary)
{
	const float radians = 3.14159265358979323846f / 180.0f;
	const float delta = DrawableInterpolationMath::shortestAngleDelta(179.0f * radians, -179.0f * radians);
	EXPECT_NEAR(2.0f * radians, delta, 0.00001f);
	const float reverse = DrawableInterpolationMath::shortestAngleDelta(-179.0f * radians, 179.0f * radians);
	EXPECT_NEAR(-2.0f * radians, reverse, 0.00001f);
}

TEST(DrawableInterpolationMath, TeleportSkipsSmearingAcrossLargeDistance)
{
	EXPECT_FALSE(DrawableInterpolationMath::isTeleport(15.0f, 0.0f, 0.0f));
	EXPECT_FALSE(DrawableInterpolationMath::isTeleport(30.0f, 30.0f, 30.0f));
	EXPECT_TRUE(DrawableInterpolationMath::isTeleport(61.0f, 0.0f, 0.0f));
	EXPECT_TRUE(DrawableInterpolationMath::isTeleport(50.0f, 50.0f, 0.0f));
}
