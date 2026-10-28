// SPDX-License-Identifier: GPL-3.0-only
// Private: CBOR encoding and decoding of the moderation and administration messages.
#pragma once

#include "CborFields.h"
#include "opentalk/protocol/Messages.h"

#include <optional>

namespace opentalk::protocol {

QCborMap toCbor(const UserUpdated &m);
QCborMap toCbor(const RolesChanged &m);
QCborMap toCbor(const CapabilitiesChanged &m);
QCborMap toCbor(const SanctionNotice &m);
QCborMap toCbor(const EditChannel &m);
QCborMap toCbor(const MoveUser &m);
QCborMap toCbor(const ApplySanction &m);
QCborMap toCbor(const RevokeSanction &m);
QCborMap toCbor(const ListRequest &m);
QCborMap toCbor(const ListResult &m);
QCborMap toCbor(const SetUserRole &m);
QCborMap toCbor(const ReportUser &m);
QCborMap toCbor(const ResolveReport &m);
QCborMap toCbor(const SubmitAppeal &m);
QCborMap toCbor(const DecideAppeal &m);
QCborMap toCbor(const ReadAudit &m);
QCborMap toCbor(const AuditPage &m);
QCborMap toCbor(const CreateAccount &m);
QCborMap toCbor(const DeleteAccount &m);
QCborMap toCbor(const SetAccountPassword &m);
QCborMap toCbor(const SaveRole &m);
QCborMap toCbor(const DeleteRole &m);
QCborMap toCbor(const AddRule &m);
QCborMap toCbor(const DeleteRule &m);
QCborMap toCbor(const ServerSettingsRequest &m);
QCborMap toCbor(const ServerSettingsResult &m);
QCborMap toCbor(const GetServerStats &m);
QCborMap toCbor(const ServerStatsResult &m);
QCborMap toCbor(const TransferOwnership &m);

namespace detail {

QCborMap roleToCbor(const RoleInfo &role);
RoleInfo readRole(FieldReader &r);
QCborArray capabilitiesToCbor(const QList<quint16> &capabilities);
QList<quint16> readCapabilities(FieldReader &r, qint64 key, const char *field);

/// True for the 0x05xx types and EditChannel/UserUpdated, which live in AdminCodec.
bool isAdminType(MessageType type);
/// Decodes an admin message. Problems are recorded in `r`.
Message decodeAdmin(MessageType type, FieldReader &r);

} // namespace detail
} // namespace opentalk::protocol
