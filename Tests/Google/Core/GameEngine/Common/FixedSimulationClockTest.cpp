/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 Contributors
** Distributed under GPL version 3 or later.
*/
#include <gtest/gtest.h>
#include "Common/FixedSimulationClock.h"
#include "Common/GameSpeedPolicy.h"

static int runSimulation(unsigned int renderFps, unsigned int logicFps = 30)
{
	FixedSimulationClock clock;
	int ticks = 0;
	for (unsigned int i = 0; i < renderFps * 10; ++i)
	{
		clock.advance(1.0 / renderFps, logicFps);
		int steps = 0;
		while (clock.isTickDue() && steps < FixedSimulationClock::MaxCatchUpSteps)
		{
			clock.consumeTick();
			++steps;
			++ticks;
		}
		EXPECT_LE(steps, FixedSimulationClock::MaxCatchUpSteps);
		EXPECT_GE(clock.getInterpolationAlpha(), 0.0f);
		EXPECT_LE(clock.getInterpolationAlpha(), 1.0f);
	}
	return ticks;
}

TEST(FixedSimulationClock, GameplayAtMultipleRenderRates)
{
	const unsigned int rates[] = { 5, 10, 20, 24, 30, 60, 120, 144, 240 };
	for (unsigned int i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i)
		EXPECT_NEAR(300.0, runSimulation(rates[i]), 1.0);
}

TEST(FixedSimulationClock, GameSpeedIsIndependentFromPresentationRate)
{
	// The skirmish Game Speed slider selects a simulation frequency.
	// Rendering must continue at its independently selected frequency.
	EXPECT_NEAR(150.0, runSimulation(144, 15), 1.0);
	EXPECT_NEAR(300.0, runSimulation(144, 30), 1.0);
	EXPECT_NEAR(600.0, runSimulation(144, 60), 1.0);
	EXPECT_NEAR(600.0, runSimulation(240, 60), 1.0);
}

TEST(FixedSimulationClock, GameSpeedPolicyAcceptsFiniteAndUnlimited)
{
    EXPECT_EQ(15, GameSpeedPolicy::fromNewGameArgument(15));
    EXPECT_EQ(30, GameSpeedPolicy::fromNewGameArgument(30));
    EXPECT_EQ(60, GameSpeedPolicy::fromNewGameArgument(60));
    EXPECT_EQ(1000, GameSpeedPolicy::fromNewGameArgument(61));
    EXPECT_EQ(1000, GameSpeedPolicy::fromNewGameArgument(1000));
    EXPECT_TRUE(GameSpeedPolicy::isUnlimited(GameSpeedPolicy::fromNewGameArgument(61)));
    EXPECT_FALSE(GameSpeedPolicy::isUnlimited(GameSpeedPolicy::fromNewGameArgument(60)));
    EXPECT_EQ(30, GameSpeedPolicy::fromNewGameArgument(0));
    EXPECT_EQ(30, GameSpeedPolicy::fromNewGameArgument(999));
    EXPECT_EQ(30, GameSpeedPolicy::fromNewGameArgument(1001));
}

TEST(FixedSimulationClock, GameSpeedAtDifferentRenderRates)
{
    const unsigned int renderRates[] = { 30, 60, 144, 240 };
    const unsigned int gameSpeeds[] = { 15, 30, 45, 60 };
    for (unsigned int i = 0; i < sizeof(renderRates) / sizeof(renderRates[0]); ++i)
        for (unsigned int j = 0; j < sizeof(gameSpeeds) / sizeof(gameSpeeds[0]); ++j)
            EXPECT_NEAR(10.0 * gameSpeeds[j], runSimulation(renderRates[i], gameSpeeds[j]), 1.0);
}

TEST(FixedSimulationClock, BoundedCatchUp)
{
	FixedSimulationClock clock;
	clock.advance(5.0, 30);
	int ticks = 0;
	while (clock.isTickDue())
	{
		clock.consumeTick();
		++ticks;
	}
	EXPECT_GE(ticks, 7);
	EXPECT_LE(ticks, FixedSimulationClock::MaxCatchUpSteps);
}

TEST(FixedSimulationClock, ResetClearsBacklog)
{
	FixedSimulationClock clock;
	clock.advance(0.25, 30);
	clock.reset();
	EXPECT_FALSE(clock.isTickDue());
	EXPECT_FLOAT_EQ(1.0f, clock.getInterpolationAlpha());
}

TEST(FixedSimulationClock, ChangingFrequencyClearsFractionalTick)
{
	FixedSimulationClock clock;
	clock.advance(1.0 / 60.0, 30);
	EXPECT_FALSE(clock.isTickDue());
	clock.advance(1.0 / 60.0, 60);
	EXPECT_TRUE(clock.isTickDue());
	clock.consumeTick();
	EXPECT_FALSE(clock.isTickDue());
}
