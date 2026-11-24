// Leptid — .Lchar character asset serialization (ZIP: character.json + part PNGs).
#pragma once
#include <string>
#include "formats/json.h"
#include "rig/character.h"

namespace leptid {

bool saveLchar(const std::string& path, const Character& c, std::string* error = nullptr);
bool loadLchar(const std::string& path, Character& out, std::string* error = nullptr);
bool loadLcharMemory(const uint8_t* data, size_t len, Character& out, std::string* error = nullptr);

// JSON-only pieces (used by .lep embedding too)
Json characterToJson(const Character& c);
bool characterFromJson(const Json& j, Character& out, std::string* error = nullptr);

} // namespace leptid
