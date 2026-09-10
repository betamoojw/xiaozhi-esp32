#pragma once

#include "knx_object.h"

#include <cstddef>
#include <string>
#include <vector>

constexpr size_t kKnxMaximumConfigurationLength = 3999;
constexpr char kKnxConfigurationDirectory[] = "assets/interfaces";
constexpr char kKnxConfigurationPath[] = "assets/interfaces/knxConfig.json";

bool KnxReadConfigurationFile(const char* path, std::string& json_text,
                              std::string& error);
bool KnxWriteConfigurationFile(const char* path, const std::string& json_text,
                               std::string& error);

bool KnxParseConfiguration(const std::string& json_text, size_t maximum_objects,
                           size_t maximum_group_addresses,
                           std::vector<KnxCommunicationObject>& objects,
                           std::string& canonical_json, std::string& error);