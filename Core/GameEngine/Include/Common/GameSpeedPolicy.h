/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 Contributors
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
*/
#pragma once

// Translate the legacy skirmish Game Speed message to an independent
// simulation policy. A logic tick still represents 1/30 in-game second;
// the slider selects how many such ticks run per real-world second.
class GameSpeedPolicy
{
public:
    enum
    {
        DefaultTicksPerSecond = 30,
        MinimumTicksPerSecond = 15,
        MaximumTicksPerSecond = 60,
        UnlimitedSliderPosition = 61,
        LegacyUnlimitedValue = 1000
    };

    // The slider stores 61, but MSG_NEW_GAME and replay headers encode
    // that choice as 1000. Neither value is a literal 1000-Hz clock.
    static int fromNewGameArgument(int requested)
    {
        if (requested == UnlimitedSliderPosition || requested == LegacyUnlimitedValue)
            return LegacyUnlimitedValue;
        if (requested >= MinimumTicksPerSecond && requested <= MaximumTicksPerSecond)
            return requested;
        return DefaultTicksPerSecond;
    }

    static bool isUnlimited(int value)
    {
        return value == LegacyUnlimitedValue;
    }
};
