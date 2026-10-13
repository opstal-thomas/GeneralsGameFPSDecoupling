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

// Standalone deterministic tick scheduler; never passes variable delta to gameplay.
class FixedSimulationClock
{
public:
    enum { MaxCatchUpSteps = 8 };
    FixedSimulationClock() : m_seconds(0.0), m_frequency(0) {}

    void reset() { m_seconds = 0.0; m_frequency = 0; }

    void advance(double elapsedSeconds, unsigned int frequency)
    {
        if (frequency == 0) { reset(); return; }
        if (m_frequency != frequency) { m_frequency = frequency; m_seconds = 0.0; }
        if (!(elapsedSeconds > 0.0)) return;
        const double step = 1.0 / frequency;
        const double elapsed = elapsedSeconds < 0.25 ? elapsedSeconds : 0.25;
        m_seconds += elapsed;
        if (m_seconds > step * MaxCatchUpSteps) m_seconds = step * MaxCatchUpSteps;
    }

    bool isTickDue() const
    {
        return m_frequency && m_seconds + 1.0e-10 >= 1.0 / m_frequency;
    }
    void consumeTick()
    {
        if (!isTickDue()) return;
        m_seconds -= 1.0 / m_frequency;
        if (m_seconds < 0.0) m_seconds = 0.0;
    }
    float getInterpolationAlpha() const
    {
        if (!m_frequency) return 1.0f;
        const double phase = m_seconds * m_frequency;
        return phase <= 0.0 ? 0.0f : phase >= 1.0 ? 1.0f : static_cast<float>(phase);
    }
private:
    double m_seconds;
    unsigned int m_frequency;
};
