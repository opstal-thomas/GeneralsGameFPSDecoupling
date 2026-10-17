/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
** Distributed under the GNU General Public License, v3 or later.
*/
// T01: original heightmap's 4-independent-vertices-per-cell topology.
// Kept backend-private and pure so the authoritative producer and WARP
// geometry tests use exactly the same edge-tile index assembly.
#pragma once

#include <cstdint>
#include <limits>
#include <vector>

namespace DX12TerrainTileGeometry
{
    inline bool BuildIndices(unsigned tileLength, unsigned columns,
                             unsigned rows, std::vector<std::uint16_t> & out)
    {
        out.clear();
        if (!tileLength || !columns || !rows ||
            columns > tileLength || rows > tileLength ||
            static_cast<std::uint64_t>(tileLength) * tileLength * 4u >
                std::numeric_limits<std::uint16_t>::max() + 1ull ||
            static_cast<std::uint64_t>(columns) * rows * 6u >
                std::numeric_limits<std::uint16_t>::max())
            return false;
        out.reserve(columns * rows * 6u);
        for (unsigned y = 0; y < rows; ++y)
            for (unsigned x = 0; x < columns; ++x)
            {
                const std::uint16_t base = static_cast<std::uint16_t>(
                    (y * tileLength + x) * 4u);
                // Exactly the two original DX8 triangles. The world-data
                // FLIP_TRIANGLES pass permutes the four corner vertices.
                out.push_back(base);
                out.push_back(base + 2);
                out.push_back(base + 3);
                out.push_back(base);
                out.push_back(base + 1);
                out.push_back(base + 2);
            }
        return true;
    }
}
