#include "BlockDef.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cctype>

std::vector<BlockDef> LoadBlockDefs(const std::string& path) {
    std::vector<BlockDef> defs;
    std::ifstream f(path);
    if (!f) {
        std::cerr << "Failed to open " << path << std::endl;
        return defs;
    }
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        // skip BOM / header
        if (line[0] == '#' || (line.size() > 1 && (unsigned char)line[0] == 0xEF)) continue;
        // trim leading whitespace
        size_t start = line.find_first_not_of(" \t\r");
        if (start == std::string::npos) continue;
        line = line.substr(start);

        std::istringstream iss(line);
        BlockDef b{};
        if (!(iss >> b.index >> b.sIndex
                  >> b.uxp >> b.vxp >> b.uyp >> b.vyp >> b.uzp >> b.vzp
                  >> b.uxn >> b.vxn >> b.uyn >> b.vyn >> b.uzn >> b.vzn
                  >> b.bh >> b.bmt >> b.mbi >> b.sau >> b.col >> b.all >> b.uvr
                  >> b.xt >> b.yt >> b.zt >> b.w >> b.h >> b.l
                  >> b.ftl >> b.ftw)) {
            continue;
        }
        std::getline(iss, b.comment);
        size_t cs = b.comment.find_first_not_of(" \t");
        if (cs != std::string::npos) b.comment = b.comment.substr(cs);
        // strip trailing comment markers if any
        defs.push_back(b);
    }
    return defs;
}
