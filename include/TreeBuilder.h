#pragma once
#include "GridData.h"

// Free functions observed in binary:
//   CheckTreePosition(GridDataEdit&, int x, int y, int z, int height) @ 0xea290
//   BuildTree(GridDataEdit&, int x, int y, int z, int height, int woodType, bool) @ 0xea610
// GridDataEdit is a thin edit wrapper over GridData; we use GridData directly.

// Block indices from assets/scenes/blocks.txt
namespace BlockId {
    constexpr unsigned char Air        = 0;
    constexpr unsigned char Stone      = 1;
    constexpr unsigned char OakWood    = 4;
    constexpr unsigned char Water      = 5;
    constexpr unsigned char Dirt       = 7;
    constexpr unsigned char Grass      = 8;
    constexpr unsigned char Leaves1    = 10;
    constexpr unsigned char Flower1    = 13;
    constexpr unsigned char BirchWood  = 16;
    constexpr unsigned char Leaves2    = 20;
    constexpr unsigned char Mushroom1  = 26;
    constexpr unsigned char Mushroom2  = 27;
    constexpr unsigned char Glass      = 11;
    constexpr unsigned char Torch      = 17;
    constexpr unsigned char ActiveTorch= 18;
}


// Returns true if a tree of given height can be placed at (x,y,z)
bool CheckTreePosition(GridData& grid, int x, int y, int z, int height);

// Build trunk + canopy. height clamped to [5,15] (observed min 5, cmp 0x0f).
// woodType: 4=Oak, 16=Birch (observed (rand&0xc)+4 pattern can yield both)
void BuildTree(GridData& grid, int x, int y, int z, int height, int woodType, bool variant);
