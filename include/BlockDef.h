#pragma once
#include <string>
#include <vector>
#include <cstdint>

// Structure inferred from blocks.txt header and rows
// #Index S-Index Uxp Vxp Uyp Vyp Uzp Vzp Uxn Vxn Uyn Vyn Uzn Vzn BH BMT MBI SAU Col All UVR XT YT ZT W H L FTL FTW Comment
struct BlockDef {
    int index;
    int sIndex; // shape/sprite index?
    float uxp, vxp, uyp, vyp, uzp, vzp; // positive face UVs
    float uxn, vxn, uyn, vyn, uzn, vzn; // negative
    int bh;   // ?
    int bmt;  // block material type?
    int mbi;  // ?
    int sau;
    int col;  // collision?
    int all;  // alpha?
    int uvr;
    int xt, yt, zt, w, h, l;
    int ftl, ftw;
    std::string comment;
    bool isAir() const { return index == 0; }
    bool isFluid() const { return comment.find("Water") != std::string::npos || comment.find("Lava") != std::string::npos; }
};

std::vector<BlockDef> LoadBlockDefs(const std::string& path);
