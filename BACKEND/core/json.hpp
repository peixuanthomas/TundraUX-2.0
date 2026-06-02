#pragma once

#include "protocol_json.hpp"

namespace tundraux::backend {

#ifndef TUNDRAUX_CORE_JSON_HPP_INCLUDED
#define TUNDRAUX_CORE_JSON_HPP_INCLUDED
#endif

using tundraux::protocol::JsonParseError;
using tundraux::protocol::JsonParseErrorCode;
using tundraux::protocol::JsonValue;
using tundraux::protocol::JsonParseResult;
using tundraux::protocol::parseJson;
using tundraux::protocol::stringifyJson;

} // namespace tundraux::backend
