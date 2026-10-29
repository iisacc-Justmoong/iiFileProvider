#pragma once
#include "ObjectStore.h"

namespace iiFileProvider::detail {
std::string encodeRawMetadata(const RawMetadata &);
RawMetadata decodeRawMetadata(const std::string &);
std::string encodeSessionContext(const ObjectSessionContext &);
ObjectSessionContext decodeSessionContext(const std::string &);
std::vector<MetadataChange> metadataChanges(const std::string &before, const std::string &after);
}
