#pragma once

#define THIMBLE_VERSION_MAJOR 0
#define THIMBLE_VERSION_MINOR 1
#define THIMBLE_VERSION_PATCH 0
#define THIMBLE_VERSION_STRING "0.1.0"

namespace thimble {
inline constexpr int version_major = THIMBLE_VERSION_MAJOR;
inline constexpr int version_minor = THIMBLE_VERSION_MINOR;
inline constexpr int version_patch = THIMBLE_VERSION_PATCH;
inline constexpr const char *version_string = THIMBLE_VERSION_STRING;
} // namespace thimble
