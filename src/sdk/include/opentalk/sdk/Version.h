// SPDX-License-Identifier: GPL-3.0-only
#pragma once

/// SDK version, following Semantic Versioning (see docs/sdk/README.md). While the major
/// version is 0 the API is a draft and may change in any release.
#define OPENTALK_SDK_VERSION_MAJOR 0
#define OPENTALK_SDK_VERSION_MINOR 4
#define OPENTALK_SDK_VERSION_PATCH 0

namespace opentalk::sdk {

struct SdkVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;
};

/// Version of the SDK the program is running with (may differ from the headers it was
/// compiled against once the SDK ships as a shared library).
SdkVersion runtimeVersion();

} // namespace opentalk::sdk
