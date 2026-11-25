#pragma once
#include "GridData.h"

// Free functions from binary:
//   CheckTreePosition(GridDataEdit&, int x, int y, int z, int height) @ 0xea290
//   BuildTree(GridDataEdit&, int x, int y, int z, int height, int seedBits, bool variant) @ 0xea610
// GridDataEdit is a thin edit wrapper over GridData; we use GridData directly.

// Block / material-row indices from assets/scenes/blocks.txt
namespace BlockId {
    constexpr unsigned char Air        = 0;
    constexpr unsigned char Stone      = 1;
    constexpr unsigned char OakWood    = 4;
    // Material table row for water UV/FTW only (S-Index hidden). World liquid
    // is always type Air(0) + state bits — never stored as cell type 5.
    constexpr unsigned char WaterMatRow = 5;
    constexpr unsigned char Water      = 5; // legacy alias → material row, not cell type
    constexpr unsigned char Dirt       = 7;
    constexpr unsigned char Grass      = 8;
    constexpr unsigned char Leaves1    = 10;
    constexpr unsigned char Flower1    = 13;
    constexpr unsigned char BirchWood  = 16;
    constexpr unsigned char Leaves2    = 20; // pattern sparse marker in BuildTree table, not cell type
    constexpr unsigned char Mushroom1  = 26;
    constexpr unsigned char Mushroom2  = 27;
    constexpr unsigned char Glass      = 11;
    constexpr unsigned char Torch      = 17;
    constexpr unsigned char ActiveTorch= 18;
}

// Binary CheckTreePosition @ 0xea290 — ground Dirt/Grass at (x,y,z), 3x3 column clearance.
bool CheckTreePosition(GridData& grid, int x, int y, int z, int height);

// Binary BuildTree @ 0xea610
// height: clamped to >=5; AddTree passes (rng>>16&3)+5 → [5,8]
// seedBits: bit stream for pattern value 0x14 sparse leaves
// variant: false → oak (4), true → birch (16); leaves always type 10
void BuildTree(GridData& grid, int x, int y, int z, int height, int seedBits, bool variant);
