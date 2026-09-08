#pragma once

#include "knx_object.h"

#include <cstddef>
#include <string>
#include <vector>

constexpr size_t kKnxMaximumConfigurationLength = 3999;

bool KnxParseConfiguration(const std::string& json_text, size_t maximum_objects,
                           size_t maximum_group_addresses,
                           std::vector<KnxCommunicationObject>& objects,
                           std::string& canonical_json, std::string& error);