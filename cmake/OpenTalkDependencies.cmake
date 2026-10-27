# SPDX-License-Identifier: GPL-3.0-only
# Third-party libraries beyond Qt. Each is recorded in docs/licensing/third-party-register.md.
# Windows builds get them from vcpkg (vcpkg.json); Linux builds use system packages.

# OpenSSL: libcrypto is used directly for certificate generation. Qt's TLS backend loads
# the same OpenSSL at run time (ADR 0007).
find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)

# libsodium: Argon2id password hashing.
add_library(opentalk_sodium INTERFACE)
add_library(OpenTalk::Sodium ALIAS opentalk_sodium)
find_package(unofficial-sodium CONFIG QUIET)
if(unofficial-sodium_FOUND)
    target_link_libraries(opentalk_sodium INTERFACE unofficial-sodium::sodium)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium>=1.0.18)
    target_link_libraries(opentalk_sodium INTERFACE PkgConfig::SODIUM)
endif()
