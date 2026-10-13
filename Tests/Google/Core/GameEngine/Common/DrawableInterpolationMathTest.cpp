/*
**  Command & Conquer Generals Zero Hour(tm)
**  Copyright 2026 Contributors
**  Distributed under GPL version 3 or later.
*/
#include <gtest/gtest.h>
#include <limits>
#include "Common/DrawableInterpolationMath.h"
#include "WWMath/quat.h"

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


TEST(DrawableInterpolationMath, QuaternionSlerpKeepsPitchAndRoll)
{
	Matrix3D before(true);
	Matrix3D after(true);
	after.Rotate_X(1.0f);
	after.Rotate_Y(0.5f);

	const Quaternion a = Build_Quaternion(before);
	const Quaternion b = Build_Quaternion(after);
	Quaternion halfway;
	Slerp(halfway, a, b, 0.5f);

	EXPECT_NEAR(1.0f, halfway.Length2(), 0.001f);
	Matrix3D halfwayMatrix(true);
	halfwayMatrix.Set_Rotation(halfway);
	const Vector3 up = halfwayMatrix.Get_Z_Vector();
	EXPECT_GT(fabsf(up.Y), 0.1f);
	EXPECT_LT(up.Z, 1.0f);
}

TEST(DrawableInterpolationMath, QuaternionSlerpUsesShortestYawAcrossWrap)
{
	const float pi = 3.14159265358979323846f;
	Matrix3D before(true);
	Matrix3D after(true);
	before.Rotate_Z(pi - 0.02f);
	after.Rotate_Z(-pi + 0.02f);

	const Quaternion a = Build_Quaternion(before);
	const Quaternion b = Build_Quaternion(after);
	Quaternion halfway;
	Slerp(halfway, a, b, 0.5f);

	Matrix3D interpolated(true);
	interpolated.Set_Rotation(halfway);
	const Vector3 forward = interpolated.Get_X_Vector();
	EXPECT_LT(forward.X, -0.99f);
	EXPECT_NEAR(0.0f, forward.Y, 0.03f);
}


TEST(DrawableInterpolationMath, RecoilPreservesAuthoredThirtyHzStep)
{
	const float rate = 5.0f;
	const float damping = 0.75f;
	EXPECT_NEAR(rate, DrawableInterpolationMath::recoilTravel(rate, damping, 1.0f), 0.00001f);
	EXPECT_NEAR(rate * damping, rate * DrawableInterpolationMath::recoilDampingFactor(damping, 1.0f), 0.00001f);
	EXPECT_FLOAT_EQ(0.0f, DrawableInterpolationMath::recoilTravel(rate, damping, 0.0f));
	EXPECT_FLOAT_EQ(1.0f, DrawableInterpolationMath::recoilDampingFactor(damping, 0.0f));
	EXPECT_FLOAT_EQ(4.0f, DrawableInterpolationMath::recoilTravel(2.0f, 1.0f, 2.0f));
}

TEST(DrawableInterpolationMath, RecoilDistanceComposesAcrossPresentationRates)
{
	const float initialRate = 3.5f;
	const float damping = 0.8f;
	const float reference = DrawableInterpolationMath::recoilTravel(initialRate, damping, 30.0f);
	const unsigned int renderRates[] = { 30, 60, 144, 240 };
	for (unsigned int i = 0; i < sizeof(renderRates) / sizeof(renderRates[0]); ++i)
	{
		const float frameRatio = 30.0f / renderRates[i];
		float rate = initialRate;
		float traveled = 0.0f;
		for (unsigned int frame = 0; frame < renderRates[i]; ++frame)
		{
			traveled += DrawableInterpolationMath::recoilTravel(rate, damping, frameRatio);
			rate *= DrawableInterpolationMath::recoilDampingFactor(damping, frameRatio);
		}
		EXPECT_NEAR(reference, traveled, 0.001f);
		EXPECT_NEAR(initialRate * DrawableInterpolationMath::recoilDampingFactor(damping, 30.0f),
			rate, 0.001f);
	}
}
