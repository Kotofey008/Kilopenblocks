#include "BlockDef.h"
#include "Assets.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cctype>

std::vector<BlockDef> LoadBlockDefsFromText(const std::string& text) {
    std::vector<BlockDef> defs;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        // skip BOM
        if ((unsigned char)line[0] == 0xEF) continue;
        if (line[0] == '#') continue;
        if (line.find("Index") != std::string::npos && line.find("Uxp") != std::string::npos)
            continue;

        std::istringstream iss(line);
        BlockDef d{};
        if (!(iss >> d.index)) continue;
        iss >> d.sIndex
            >> d.uxp >> d.vxp >> d.uyp >> d.vyp >> d.uzp >> d.vzp
            >> d.uxn >> d.vxn >> d.uyn >> d.vyn >> d.uzn >> d.vzn
            >> d.bh >> d.bmt >> d.mbi >> d.sau >> d.col >> d.all >> d.uvr
            >> d.xt >> d.yt >> d.zt >> d.w >> d.h >> d.l >> d.ftl >> d.ftw;
        // remainder of line is comment
        std::string rest;
        std::getline(iss, rest);
        // trim
        size_t a = 0;
        while (a < rest.size() && std::isspace((unsigned char)rest[a])) ++a;
        d.comment = rest.substr(a);

        if ((int)defs.size() <= d.index) defs.resize(d.index + 1);
        defs[d.index] = d;
    }
    return defs;
}

std::vector<BlockDef> LoadBlockDefs(const std::string& path) {
    std::string text;
    if (Assets::ReadText(path.c_str(), text))
        return LoadBlockDefsFromText(text);
    std::ifstream f(path);
    if (!f) {
        std::cerr << "Failed to open " << path << std::endl;
        return {};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return LoadBlockDefsFromText(ss.str());
}
