/*
**  Command & Conquer Generals Zero Hour(tm)
**  Copyright 2026 Contributors
**  Distributed under GPL version 3 or later.
*/
#include <gtest/gtest.h>
#include <limits>
#include "Common/DrawableInterpolationMath.h"

TEST(DrawableInterpolationMath, ClampsInvalidAndOutOfRangeAlpha)
{
	EXPECT_FLOAT_EQ(0.0f, DrawableInterpolationMath::clampAlpha(-1.0f));
	EXPECT_FLOAT_EQ(0.0f, DrawableInterpolationMath::clampAlpha(std::numeric_limits<float>::quiet_NaN()));
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


TEST(DrawableInterpolationMath, LargeOrientationChangesSnapButQuaternionSignDoesNot)
{
	EXPECT_FALSE(DrawableInterpolationMath::isRotationDiscontinuity(1.0f));
	EXPECT_FALSE(DrawableInterpolationMath::isRotationDiscontinuity(-1.0f));
	EXPECT_FALSE(DrawableInterpolationMath::isRotationDiscontinuity(0.5f));
	EXPECT_TRUE(DrawableInterpolationMath::isRotationDiscontinuity(0.0f));
	EXPECT_TRUE(DrawableInterpolationMath::isRotationDiscontinuity(-0.1f));
}

TEST(DrawableInterpolationMath, CameraBlendPreservesDurationAtDifferentRenderRates)
{
	const float legacyBlend = 0.1f;
	EXPECT_NEAR(legacyBlend, DrawableInterpolationMath::presentationBlend(legacyBlend, 1.0f), 0.00001f);
	EXPECT_NEAR(0.0513167f, DrawableInterpolationMath::presentationBlend(legacyBlend, 0.5f), 0.00001f);

	// Two 60 Hz frames and one 30 Hz frame should have identical damping.
	const float half = DrawableInterpolationMath::presentationBlend(legacyBlend, 0.5f);
	const float composed = 1.0f - (1.0f - half) * (1.0f - half);
	EXPECT_NEAR(legacyBlend, composed, 0.00001f);
	EXPECT_FLOAT_EQ(0.0f, DrawableInterpolationMath::presentationBlend(legacyBlend, 0.0f));
	EXPECT_FLOAT_EQ(1.0f, DrawableInterpolationMath::presentationBlend(2.0f, 0.1f));
}
