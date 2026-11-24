// Leptid — .lep Version 3 project serialization: a self-contained ZIP archive
// embedding the project JSON, raster frame PNGs and character (JSON + limb PNG)
// data, so projects do not depend on external files (blueprint §8-4).
#pragma once
#include <string>
#include "model/project.h"

namespace leptid {

bool saveLep(const std::string& path, const Project& p, std::string* error = nullptr);
bool loadLep(const std::string& path, Project& out, std::string* error = nullptr);

} // namespace leptid
