/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 Contributors
** Distributed under GPL version 3 or later.
*/
#include <gtest/gtest.h>
#include "Common/FixedSimulationClock.h"

static int runSimulation(unsigned int renderFps)
{
	FixedSimulationClock clock;
	int ticks = 0;
	for (unsigned int i = 0; i < renderFps * 10; ++i)
	{
		clock.advance(1.0 / renderFps, 30);
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
