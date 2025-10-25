#include <map>
#include <string>
#include <string_view>

#include <android-base/unique_fd.h>

#pragma once

namespace libgrub_editenv {

using GrubEnvMap = std::map<std::string, std::string>;

bool ValidateHeader(std::string_view content);
bool ValidateLength(std::string_view content);
bool Validate(std::string_view content);

bool ParseStringToMap(const std::string& orig_content, GrubEnvMap* key_value_map);
bool LoadFdToMap(android::base::borrowed_fd fd, GrubEnvMap* key_value_map);
bool LoadFileToMap(std::string path, GrubEnvMap* key_value_map);

void TrimOverflowVarsFromString(std::string* content);
std::string GenerateStringFromMap(const GrubEnvMap& key_value_map, bool trim = false);
bool WriteFdFromMap(android::base::borrowed_fd fd, const GrubEnvMap& key_value_map,
                    bool trim = false);
bool WriteFileFromMap(std::string path, const GrubEnvMap& key_value_map, bool trim = false);

}  // namespace libgrub_editenv
