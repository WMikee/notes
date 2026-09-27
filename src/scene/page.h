#pragma once

namespace notes {

inline constexpr double kPageWidth = 794.0;
inline constexpr double kPageHeight = 1123.0;
inline constexpr double kPageMargin = 24.0;
inline constexpr double kPageCornerRadius = 0.0;

inline constexpr int kGridCellsAcross = 29;
inline constexpr int kGridCellsPerLevel = 8;
inline constexpr double kGridBaseSpacing = kPageWidth / (kGridCellsAcross * kGridCellsPerLevel);
inline constexpr double kGridMinSpacing = kPageWidth / kGridCellsAcross;

}
