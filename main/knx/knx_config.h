#pragma once

#include "knx_object.h"

#include <cstddef>
#include <string>
#include <vector>

constexpr size_t kKnxMaximumConfigurationLength = 3999;
constexpr char kKnxFactoryConfigurationAsset[] = "interfaces/knxConfig.json";
constexpr char kKnxSettingsNamespace[] = "knx";
constexpr char kKnxSettingsKey[] = "config";

bool KnxLoadFactoryConfiguration(std::string& json_text, std::string& error);
bool KnxLoadPersistedConfiguration(std::string& json_text, bool& found,
                                   std::string& error);
bool KnxPersistConfiguration(const std::string& json_text, std::string& error);

bool KnxParseConfiguration(const std::string& json_text, size_t maximum_objects,
                           size_t maximum_group_addresses,
                           std::vector<KnxCommunicationObject>& objects,
                           std::string& canonical_json, std::string& error);