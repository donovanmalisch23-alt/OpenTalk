# SPDX-License-Identifier: GPL-3.0-only
# TLS setup and server-identity checks shared by the client and the server.
add_library(opentalk_security STATIC
    include/opentalk/security/Tls.h
    include/opentalk/security/ServerTrust.h
    include/opentalk/security/PasswordHash.h
    include/opentalk/security/CertificateGenerator.h
    Tls.cpp
    ServerTrust.cpp
    PasswordHash.cpp
    CertificateGenerator.cpp)
add_library(OpenTalk::Security ALIAS opentalk_security)
target_include_directories(opentalk_security PUBLIC include)
target_link_libraries(opentalk_security
    PUBLIC Qt6::Network
    PRIVATE Qt6::Core OpenTalk::Logging OpenTalk::Sodium OpenSSL::Crypto OpenTalk::CompilerOptions)
