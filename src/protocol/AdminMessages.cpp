// SPDX-License-Identifier: GPL-3.0-only
#include "AdminCodec.h"

namespace opentalk::protocol {

using detail::FieldReader;
using detail::id63;

namespace {

// Every request uses key 1 for its request ID.
constexpr qint64 kRequestId = 1;

constexpr qint64 kMaxDuration = 10LL * 365 * 24 * 60 * 60; ///< 10 years, in seconds.

QCborValue u64(quint64 v)
{
    return id63(v);
}

QString reason(FieldReader &r, qint64 key, const char *field)
{
    return r.plainText(key, field, kMaxReasonLength, false);
}

/// Optional free text that may contain line breaks (chat rules) but may be empty.
QString optionalChat(FieldReader &r, const QCborMap &map, qint64 key, const char *field)
{
    if (!map.contains(key) || map.value(key).toString().isEmpty()) {
        return {};
    }
    return r.chatText(key, field);
}

template <typename T, typename Encode> QCborArray arrayOf(const QList<T> &items, Encode encode)
{
    QCborArray array;
    for (const T &item : items) {
        array.append(encode(item));
    }
    return array;
}

template <typename T, typename Decode>
QList<T> readArray(FieldReader &r, qint64 key, const char *field, Decode decode, qsizetype max = kMaxListEntries)
{
    QList<T> result;
    const QCborArray array = r.array(key, field, max);
    for (qsizetype i = 0; r.ok() && i < array.size(); ++i) {
        if (!array.at(i).isMap()) {
            r.fail(QString::fromLatin1(field) + QStringLiteral(": entry is not a map"));
            break;
        }
        const QCborMap entry = array.at(i).toMap();
        FieldReader sub(entry);
        result.append(decode(sub, entry));
        if (!sub.ok()) {
            r.fail(sub.failure());
        }
    }
    return result;
}

// ------------------------------------------------------------------------- records

QCborMap sanctionToCbor(const SanctionInfo &s)
{
    QCborMap map{{1, u64(s.id)},    {2, s.type},       {3, static_cast<qint64>(s.channelId)},
                 {4, s.targetName}, {7, s.actorName},  {8, s.reason},
                 {9, s.createdMs},  {10, s.expiresMs}, {11, s.state}};
    if (!s.targetAccount.isEmpty()) {
        map.insert(5, s.targetAccount);
    }
    if (!s.targetAddress.isEmpty()) {
        map.insert(6, s.targetAddress);
    }
    if (!s.resolvedBy.isEmpty()) {
        map.insert(12, s.resolvedBy);
    }
    if (!s.resolution.isEmpty()) {
        map.insert(13, s.resolution);
    }
    return map;
}

SanctionInfo readSanction(FieldReader &r, const QCborMap &)
{
    SanctionInfo s;
    s.id = r.integer<quint64>(1, "sanction.id");
    s.type = r.integer<quint8>(2, "sanction.type");
    s.channelId = r.integer<quint32>(3, "sanction.channelId");
    s.targetName = r.plainText(4, "sanction.targetName", kMaxDisplayNameLength, true);
    s.targetAccount = r.plainText(5, "sanction.targetAccount", kMaxDisplayNameLength, false);
    s.targetAddress = r.plainText(6, "sanction.targetAddress", 64, false);
    s.actorName = r.plainText(7, "sanction.actorName", kMaxDisplayNameLength, true);
    s.reason = reason(r, 8, "sanction.reason");
    s.createdMs = r.integer<qint64>(9, "sanction.created");
    s.expiresMs = r.integer<qint64>(10, "sanction.expires");
    s.state = r.integer<quint8>(11, "sanction.state");
    s.resolvedBy = r.plainText(12, "sanction.resolvedBy", kMaxDisplayNameLength, false);
    s.resolution = reason(r, 13, "sanction.resolution");
    return s;
}

QCborMap reportToCbor(const ReportInfo &x)
{
    return {{1, u64(x.id)},   {2, x.reporterName}, {3, x.targetName}, {4, x.targetAccount}, {5, x.reason},
            {6, x.createdMs}, {7, x.state},        {8, x.handledBy},  {9, x.resolution}};
}

ReportInfo readReport(FieldReader &r, const QCborMap &)
{
    ReportInfo x;
    x.id = r.integer<quint64>(1, "report.id");
    x.reporterName = r.plainText(2, "report.reporter", kMaxDisplayNameLength, true);
    x.targetName = r.plainText(3, "report.target", kMaxDisplayNameLength, true);
    x.targetAccount = r.plainText(4, "report.targetAccount", kMaxDisplayNameLength, false);
    x.reason = reason(r, 5, "report.reason");
    x.createdMs = r.integer<qint64>(6, "report.created");
    x.state = r.integer<quint8>(7, "report.state");
    x.handledBy = r.plainText(8, "report.handledBy", kMaxDisplayNameLength, false);
    x.resolution = reason(r, 9, "report.resolution");
    return x;
}

QCborMap appealToCbor(const AppealInfo &x)
{
    return {{1, u64(x.id)},   {2, u64(x.sanctionId)}, {3, x.appellantName}, {4, x.text},
            {5, x.createdMs}, {6, x.state},           {7, x.decidedBy},     {8, x.decisionReason}};
}

AppealInfo readAppeal(FieldReader &r, const QCborMap &)
{
    AppealInfo x;
    x.id = r.integer<quint64>(1, "appeal.id");
    x.sanctionId = r.integer<quint64>(2, "appeal.sanctionId");
    x.appellantName = r.plainText(3, "appeal.appellant", kMaxDisplayNameLength, true);
    x.text = r.chatText(4, "appeal.text");
    x.createdMs = r.integer<qint64>(5, "appeal.created");
    x.state = r.integer<quint8>(6, "appeal.state");
    x.decidedBy = r.plainText(7, "appeal.decidedBy", kMaxDisplayNameLength, false);
    x.decisionReason = reason(r, 8, "appeal.reason");
    return x;
}

QCborMap auditToCbor(const AuditEntryInfo &x)
{
    return {{1, u64(x.sequence)}, {2, x.timeMs},  {3, x.actorName},
            {4, x.action},        {5, x.target},  {6, static_cast<qint64>(x.channelId)},
            {7, x.reason},        {8, x.details}, {9, x.hash}};
}

AuditEntryInfo readAudit(FieldReader &r, const QCborMap &)
{
    AuditEntryInfo x;
    x.sequence = r.integer<quint64>(1, "audit.sequence");
    x.timeMs = r.integer<qint64>(2, "audit.time");
    x.actorName = r.plainText(3, "audit.actor", kMaxDisplayNameLength, true);
    x.action = r.plainText(4, "audit.action", 64, true);
    x.target = r.plainText(5, "audit.target", 256, true);
    x.channelId = r.integer<quint32>(6, "audit.channelId");
    x.reason = reason(r, 7, "audit.reason");
    x.details = r.plainText(8, "audit.details", 1024, true);
    x.hash = r.bytes(9, "audit.hash", 0, 64);
    return x;
}

QCborMap accountToCbor(const AccountInfo &x)
{
    return {{1, x.name}, {2, static_cast<qint64>(x.roleId)}, {3, x.disabled}};
}

AccountInfo readAccount(FieldReader &r, const QCborMap &)
{
    AccountInfo x;
    x.name = r.name(1, "account.name", kMaxDisplayNameLength);
    x.roleId = r.integer<quint32>(2, "account.role");
    x.disabled = r.boolean(3, "account.disabled", false);
    return x;
}

QCborMap ruleToCbor(const RuleInfo &x)
{
    QCborMap map{{1, u64(x.id)},    {2, static_cast<qint64>(x.channelId)},
                 {3, x.forAccount}, {5, static_cast<qint64>(x.roleId)},
                 {6, x.capability}, {7, x.allow}};
    if (x.forAccount) {
        map.insert(4, x.account);
    }
    return map;
}

RuleInfo readRule(FieldReader &r, const QCborMap &)
{
    RuleInfo x;
    x.id = r.integer<quint64>(1, "rule.id");
    x.channelId = r.integer<quint32>(2, "rule.channelId");
    x.forAccount = r.boolean(3, "rule.forAccount", true);
    if (x.forAccount) {
        x.account = r.name(4, "rule.account", kMaxDisplayNameLength);
    }
    x.roleId = r.integer<quint32>(5, "rule.role");
    x.capability = r.integer<quint16>(6, "rule.capability");
    x.allow = r.boolean(7, "rule.allow", true);
    return x;
}

QCborMap settingsToCbor(const ServerSettingsInfo &x)
{
    return {{1, x.name},
            {2, x.welcomeMessage},
            {3, x.allowGuests},
            {4, x.maxUsers},
            {5, static_cast<qint64>(x.maxVoiceBitrate)},
            {6, x.maxTemporaryBanDays},
            {7, x.contact}};
}

ServerSettingsInfo readSettings(FieldReader &r, const QCborMap &map)
{
    ServerSettingsInfo x;
    x.name = r.name(1, "settings.name", kMaxNameLength);
    x.welcomeMessage = optionalChat(r, map, 2, "settings.welcomeMessage");
    x.allowGuests = r.boolean(3, "settings.allowGuests", true);
    x.maxUsers = r.integer<quint16>(4, "settings.maxUsers");
    x.maxVoiceBitrate = r.integer<quint32>(5, "settings.maxVoiceBitrate");
    x.maxTemporaryBanDays = r.integer<quint16>(6, "settings.maxTemporaryBanDays");
    x.contact = r.plainText(7, "settings.contact", 256, false);
    if (r.ok() && (x.maxUsers == 0 || x.maxVoiceBitrate < 6000 || x.maxVoiceBitrate > 510000)) {
        r.fail(QStringLiteral("settings: values out of range"));
    }
    return x;
}

QCborMap statsToCbor(const ServerStatsInfo &x)
{
    return {{1, x.uptimeSeconds},
            {2, static_cast<qint64>(x.onlineUsers)},
            {3, static_cast<qint64>(x.connections)},
            {4, static_cast<qint64>(x.channels)},
            {5, u64(x.voicePacketsIn)},
            {6, u64(x.voicePacketsOut)},
            {7, u64(x.voicePacketsRejected)},
            {8, static_cast<qint64>(x.activeSanctions)},
            {9, static_cast<qint64>(x.openReports)},
            {10, static_cast<qint64>(x.openAppeals)},
            {11, u64(x.auditEntries)},
            {12, x.auditIntact}};
}

ServerStatsInfo readStats(FieldReader &r)
{
    ServerStatsInfo x;
    x.uptimeSeconds = r.integer<qint64>(1, "stats.uptime");
    x.onlineUsers = r.integer<quint32>(2, "stats.online");
    x.connections = r.integer<quint32>(3, "stats.connections");
    x.channels = r.integer<quint32>(4, "stats.channels");
    x.voicePacketsIn = r.integer<quint64>(5, "stats.voiceIn");
    x.voicePacketsOut = r.integer<quint64>(6, "stats.voiceOut");
    x.voicePacketsRejected = r.integer<quint64>(7, "stats.voiceRejected");
    x.activeSanctions = r.integer<quint32>(8, "stats.sanctions");
    x.openReports = r.integer<quint32>(9, "stats.reports");
    x.openAppeals = r.integer<quint32>(10, "stats.appeals");
    x.auditEntries = r.integer<quint64>(11, "stats.audit");
    x.auditIntact = r.boolean(12, "stats.auditIntact", true);
    return x;
}

} // namespace

// ----------------------------------------------------------------- shared records

namespace detail {

QCborArray capabilitiesToCbor(const QList<quint16> &capabilities)
{
    QCborArray array;
    for (quint16 c : capabilities) {
        array.append(c);
    }
    return array;
}

QList<quint16> readCapabilities(FieldReader &r, qint64 key, const char *field)
{
    QList<quint16> result;
    const QCborArray array = r.array(key, field, 1000);
    for (const QCborValue &value : array) {
        if (!value.isInteger() || value.toInteger() < 0 || value.toInteger() > 0xffff) {
            r.fail(QString::fromLatin1(field) + QStringLiteral(": not a capability number"));
            break;
        }
        result.append(static_cast<quint16>(value.toInteger()));
    }
    return result;
}

QCborMap roleToCbor(const RoleInfo &role)
{
    return {{1, static_cast<qint64>(role.id)},
            {2, role.name},
            {3, role.rank},
            {4, capabilitiesToCbor(role.capabilities)},
            {5, role.builtIn}};
}

RoleInfo readRole(FieldReader &r)
{
    RoleInfo role;
    role.id = r.integer<quint32>(1, "role.id");
    role.name = r.name(2, "role.name", kMaxRoleNameLength);
    role.rank = r.integer<qint32>(3, "role.rank");
    role.capabilities = readCapabilities(r, 4, "role.capabilities");
    role.builtIn = r.boolean(5, "role.builtIn", false);
    if (r.ok() && role.rank > 100) {
        r.fail(QStringLiteral("role.rank: out of range"));
    }
    return role;
}

bool isAdminType(MessageType type)
{
    switch (type) {
    case MessageType::UserUpdated:
    case MessageType::EditChannel:
    case MessageType::RolesChanged:
    case MessageType::CapabilitiesChanged:
    case MessageType::SanctionNotice:
    case MessageType::MoveUser:
    case MessageType::ApplySanction:
    case MessageType::RevokeSanction:
    case MessageType::ListRequest:
    case MessageType::ListResult:
    case MessageType::SetUserRole:
    case MessageType::ReportUser:
    case MessageType::ResolveReport:
    case MessageType::SubmitAppeal:
    case MessageType::DecideAppeal:
    case MessageType::ReadAudit:
    case MessageType::AuditPage:
    case MessageType::CreateAccount:
    case MessageType::DeleteAccount:
    case MessageType::SetAccountPassword:
    case MessageType::SaveRole:
    case MessageType::DeleteRole:
    case MessageType::AddRule:
    case MessageType::DeleteRule:
    case MessageType::ServerSettingsRequest:
    case MessageType::ServerSettingsResult:
    case MessageType::GetServerStats:
    case MessageType::ServerStatsResult:
    case MessageType::TransferOwnership:
        return true;
    default:
        return false;
    }
}

Message decodeAdmin(MessageType type, FieldReader &r)
{
    const QCborMap &map = r.map();
    const auto requestId = [&r] { return r.integer<quint64>(kRequestId, "requestId"); };
    switch (type) {
    case MessageType::UserUpdated:
        return UserUpdated{r.integer<quint32>(1, "userId"), r.integer<quint32>(2, "roleId"),
                           r.integer<quint32>(3, "flags")};
    case MessageType::RolesChanged: {
        RolesChanged m;
        m.roles =
            readArray<RoleInfo>(r, 1, "roles", [](FieldReader &sub, const QCborMap &) { return readRole(sub); }, 1000);
        return m;
    }
    case MessageType::CapabilitiesChanged: {
        CapabilitiesChanged m;
        m.capabilities = readCapabilities(r, 1, "capabilities");
        if (map.contains(2)) {
            m.channelCapabilities = readCapabilities(r, 2, "channelCapabilities");
        }
        return m;
    }
    case MessageType::SanctionNotice: {
        SanctionInfo s = readSanction(r, map);
        return SanctionNotice{s};
    }
    case MessageType::EditChannel: {
        EditChannel m;
        m.requestId = requestId();
        m.channelId = r.integer<quint32>(2, "channelId");
        m.name = r.name(3, "name", kMaxChannelNameLength);
        m.topic = r.plainText(4, "topic", kMaxTopicLength, false);
        m.maxUsers = r.optionalInteger<quint16>(5, "maxUsers").value_or(0);
        m.changePassword = r.boolean(6, "changePassword", false);
        m.password = r.secret(7, "password", false);
        return m;
    }
    case MessageType::MoveUser:
        return MoveUser{requestId(), r.integer<quint32>(2, "userId"), r.integer<quint32>(3, "channelId")};
    case MessageType::ApplySanction: {
        ApplySanction m;
        m.requestId = requestId();
        m.userId = r.optionalInteger<quint32>(2, "userId").value_or(0);
        m.account = r.plainText(3, "account", kMaxDisplayNameLength, false);
        m.type = r.integer<quint8>(4, "type");
        m.channelId = r.optionalInteger<quint32>(5, "channelId").value_or(0);
        m.durationSeconds = r.optionalInteger<quint32>(6, "duration").value_or(0);
        m.reason = reason(r, 7, "reason");
        m.banAddress = r.boolean(8, "banAddress", false);
        if (r.ok() && ((m.userId == 0) == m.account.isEmpty())) {
            r.fail(QStringLiteral("give either an online user or an account"));
        }
        if (r.ok() && m.durationSeconds > kMaxDuration) {
            r.fail(QStringLiteral("duration: too long"));
        }
        return m;
    }
    case MessageType::RevokeSanction:
        return RevokeSanction{requestId(), r.integer<quint64>(2, "sanctionId"), reason(r, 3, "reason")};
    case MessageType::ListRequest:
        return ListRequest{requestId(), r.enumeration<ListKind>(2, "kind", ListKind::Rules),
                           r.boolean(3, "includeClosed", false)};
    case MessageType::ListResult: {
        ListResult m;
        m.requestId = requestId();
        m.kind = r.enumeration<ListKind>(2, "kind", ListKind::Rules);
        if (!r.ok()) {
            return m;
        }
        switch (m.kind) {
        case ListKind::Sanctions:
            m.sanctions = readArray<SanctionInfo>(r, 3, "items", readSanction);
            break;
        case ListKind::Reports:
            m.reports = readArray<ReportInfo>(r, 3, "items", readReport);
            break;
        case ListKind::Appeals:
            m.appeals = readArray<AppealInfo>(r, 3, "items", readAppeal);
            break;
        case ListKind::Accounts:
            m.accounts = readArray<AccountInfo>(r, 3, "items", readAccount);
            break;
        case ListKind::Rules:
            m.rules = readArray<RuleInfo>(r, 3, "items", readRule);
            break;
        }
        return m;
    }
    case MessageType::SetUserRole:
        return SetUserRole{requestId(), r.name(2, "account", kMaxDisplayNameLength), r.integer<quint32>(3, "roleId")};
    case MessageType::ReportUser: {
        ReportUser m{requestId(), r.integer<quint32>(2, "userId"), reason(r, 3, "reason")};
        if (r.ok() && m.reason.trimmed().isEmpty()) {
            r.fail(QStringLiteral("reason: required"));
        }
        return m;
    }
    case MessageType::ResolveReport:
        return ResolveReport{requestId(), r.integer<quint64>(2, "reportId"), r.integer<quint8>(3, "state"),
                             reason(r, 4, "resolution")};
    case MessageType::SubmitAppeal:
        return SubmitAppeal{requestId(), r.integer<quint64>(2, "sanctionId"), r.chatText(3, "text")};
    case MessageType::DecideAppeal:
        return DecideAppeal{requestId(), r.integer<quint64>(2, "appealId"), r.integer<quint8>(3, "decision"),
                            reason(r, 4, "reason"), r.optionalInteger<quint32>(5, "newDuration").value_or(0)};
    case MessageType::ReadAudit: {
        ReadAudit m{requestId(), r.optionalInteger<quint64>(2, "before").value_or(0),
                    r.optionalInteger<quint16>(3, "limit").value_or(100)};
        if (r.ok() && (m.limit == 0 || m.limit > kMaxAuditPage)) {
            r.fail(QStringLiteral("limit: out of range"));
        }
        return m;
    }
    case MessageType::AuditPage: {
        AuditPage m;
        m.requestId = requestId();
        m.entries = readArray<AuditEntryInfo>(r, 2, "entries", readAudit, kMaxAuditPage);
        m.chainIntact = r.boolean(3, "chainIntact", true);
        m.firstBadSequence = r.optionalInteger<quint64>(4, "firstBad").value_or(0);
        return m;
    }
    case MessageType::CreateAccount:
        return CreateAccount{requestId(), r.name(2, "name", kMaxDisplayNameLength), r.secret(3, "password", true),
                             r.integer<quint32>(4, "roleId")};
    case MessageType::DeleteAccount:
        return DeleteAccount{requestId(), r.name(2, "name", kMaxDisplayNameLength)};
    case MessageType::SetAccountPassword:
        return SetAccountPassword{requestId(), r.name(2, "name", kMaxDisplayNameLength), r.secret(3, "password", true)};
    case MessageType::SaveRole: {
        SaveRole m;
        m.requestId = requestId();
        const QCborValue value = map.value(2);
        if (!value.isMap()) {
            r.fail(QStringLiteral("role: missing or not a map"));
            return m;
        }
        const QCborMap roleMap = value.toMap();
        FieldReader sub(roleMap);
        m.role = readRole(sub);
        if (!sub.ok()) {
            r.fail(sub.failure());
        }
        return m;
    }
    case MessageType::DeleteRole:
        return DeleteRole{requestId(), r.integer<quint32>(2, "roleId")};
    case MessageType::AddRule: {
        AddRule m;
        m.requestId = requestId();
        const QCborValue value = map.value(2);
        if (!value.isMap()) {
            r.fail(QStringLiteral("rule: missing or not a map"));
            return m;
        }
        const QCborMap ruleMap = value.toMap();
        FieldReader sub(ruleMap);
        m.rule = readRule(sub, ruleMap);
        if (!sub.ok()) {
            r.fail(sub.failure());
        }
        return m;
    }
    case MessageType::DeleteRule:
        return DeleteRule{requestId(), r.integer<quint64>(2, "ruleId")};
    case MessageType::ServerSettingsRequest: {
        ServerSettingsRequest m;
        m.requestId = requestId();
        if (map.contains(2)) {
            const QCborValue value = map.value(2);
            if (!value.isMap()) {
                r.fail(QStringLiteral("settings: not a map"));
                return m;
            }
            const QCborMap settingsMap = value.toMap();
            FieldReader sub(settingsMap);
            m.settings = readSettings(sub, settingsMap);
            if (!sub.ok()) {
                r.fail(sub.failure());
            }
        }
        return m;
    }
    case MessageType::ServerSettingsResult: {
        ServerSettingsResult m;
        m.requestId = requestId();
        const QCborValue value = map.value(2);
        if (!value.isMap()) {
            r.fail(QStringLiteral("settings: missing or not a map"));
            return m;
        }
        const QCborMap settingsMap = value.toMap();
        FieldReader sub(settingsMap);
        m.settings = readSettings(sub, settingsMap);
        if (!sub.ok()) {
            r.fail(sub.failure());
        }
        return m;
    }
    case MessageType::GetServerStats:
        return GetServerStats{requestId()};
    case MessageType::ServerStatsResult: {
        ServerStatsResult m;
        m.requestId = requestId();
        const QCborValue value = map.value(2);
        if (!value.isMap()) {
            r.fail(QStringLiteral("stats: missing or not a map"));
            return m;
        }
        const QCborMap statsMap = value.toMap();
        FieldReader sub(statsMap);
        m.stats = readStats(sub);
        if (!sub.ok()) {
            r.fail(sub.failure());
        }
        return m;
    }
    case MessageType::TransferOwnership:
        return TransferOwnership{requestId(), r.name(2, "account", kMaxDisplayNameLength)};
    default:
        r.fail(QStringLiteral("not an administration message"));
        return Message();
    }
}

} // namespace detail

// ------------------------------------------------------------------------ encoding

QCborMap toCbor(const UserUpdated &m)
{
    return {{1, static_cast<qint64>(m.userId)}, {2, static_cast<qint64>(m.roleId)}, {3, static_cast<qint64>(m.flags)}};
}

QCborMap toCbor(const RolesChanged &m)
{
    return {{1, arrayOf(m.roles, detail::roleToCbor)}};
}

QCborMap toCbor(const CapabilitiesChanged &m)
{
    return {{1, detail::capabilitiesToCbor(m.capabilities)}, {2, detail::capabilitiesToCbor(m.channelCapabilities)}};
}

QCborMap toCbor(const SanctionNotice &m)
{
    return sanctionToCbor(m.sanction);
}

QCborMap toCbor(const EditChannel &m)
{
    QCborMap map{{kRequestId, u64(m.requestId)},
                 {2, static_cast<qint64>(m.channelId)},
                 {3, m.name},
                 {4, m.topic},
                 {5, m.maxUsers},
                 {6, m.changePassword}};
    if (m.changePassword && !m.password.isEmpty()) {
        map.insert(7, m.password);
    }
    return map;
}

QCborMap toCbor(const MoveUser &m)
{
    return {{kRequestId, u64(m.requestId)}, {2, static_cast<qint64>(m.userId)}, {3, static_cast<qint64>(m.channelId)}};
}

QCborMap toCbor(const ApplySanction &m)
{
    QCborMap map{{kRequestId, u64(m.requestId)},
                 {4, m.type},
                 {5, static_cast<qint64>(m.channelId)},
                 {6, static_cast<qint64>(m.durationSeconds)},
                 {7, m.reason},
                 {8, m.banAddress}};
    if (m.userId != 0) {
        map.insert(2, static_cast<qint64>(m.userId));
    }
    if (!m.account.isEmpty()) {
        map.insert(3, m.account);
    }
    return map;
}

QCborMap toCbor(const RevokeSanction &m)
{
    return {{kRequestId, u64(m.requestId)}, {2, u64(m.sanctionId)}, {3, m.reason}};
}

QCborMap toCbor(const ListRequest &m)
{
    return {{kRequestId, u64(m.requestId)}, {2, static_cast<int>(m.kind)}, {3, m.includeClosed}};
}

QCborMap toCbor(const ListResult &m)
{
    QCborArray items;
    switch (m.kind) {
    case ListKind::Sanctions:
        items = arrayOf(m.sanctions, sanctionToCbor);
        break;
    case ListKind::Reports:
        items = arrayOf(m.reports, reportToCbor);
        break;
    case ListKind::Appeals:
        items = arrayOf(m.appeals, appealToCbor);
        break;
    case ListKind::Accounts:
        items = arrayOf(m.accounts, accountToCbor);
        break;
    case ListKind::Rules:
        items = arrayOf(m.rules, ruleToCbor);
        break;
    }
    return {{kRequestId, u64(m.requestId)}, {2, static_cast<int>(m.kind)}, {3, items}};
}

QCborMap toCbor(const SetUserRole &m)
{
    return {{kRequestId, u64(m.requestId)}, {2, m.account}, {3, static_cast<qint64>(m.roleId)}};
}

QCborMap toCbor(const ReportUser &m)
{
    return {{kRequestId, u64(m.requestId)}, {2, static_cast<qint64>(m.userId)}, {3, m.reason}};
}

QCborMap toCbor(const ResolveReport &m)
{
    return {{kRequestId, u64(m.requestId)}, {2, u64(m.reportId)}, {3, m.state}, {4, m.resolution}};
}

QCborMap toCbor(const SubmitAppeal &m)
{
    return {{kRequestId, u64(m.requestId)}, {2, u64(m.sanctionId)}, {3, m.text}};
}

QCborMap toCbor(const DecideAppeal &m)
{
    return {{kRequestId, u64(m.requestId)},
            {2, u64(m.appealId)},
            {3, m.decision},
            {4, m.reason},
            {5, static_cast<qint64>(m.newDurationSeconds)}};
}

QCborMap toCbor(const ReadAudit &m)
{
    return {{kRequestId, u64(m.requestId)}, {2, u64(m.beforeSequence)}, {3, m.limit}};
}

QCborMap toCbor(const AuditPage &m)
{
    return {{kRequestId, u64(m.requestId)},
            {2, arrayOf(m.entries, auditToCbor)},
            {3, m.chainIntact},
            {4, u64(m.firstBadSequence)}};
}

QCborMap toCbor(const CreateAccount &m)
{
    return {{kRequestId, u64(m.requestId)}, {2, m.name}, {3, m.password}, {4, static_cast<qint64>(m.roleId)}};
}

QCborMap toCbor(const DeleteAccount &m)
{
    return {{kRequestId, u64(m.requestId)}, {2, m.name}};
}

QCborMap toCbor(const SetAccountPassword &m)
{
    return {{kRequestId, u64(m.requestId)}, {2, m.name}, {3, m.password}};
}

QCborMap toCbor(const SaveRole &m)
{
    return {{kRequestId, u64(m.requestId)}, {2, detail::roleToCbor(m.role)}};
}

QCborMap toCbor(const DeleteRole &m)
{
    return {{kRequestId, u64(m.requestId)}, {2, static_cast<qint64>(m.roleId)}};
}

QCborMap toCbor(const AddRule &m)
{
    return {{kRequestId, u64(m.requestId)}, {2, ruleToCbor(m.rule)}};
}

QCborMap toCbor(const DeleteRule &m)
{
    return {{kRequestId, u64(m.requestId)}, {2, u64(m.ruleId)}};
}

QCborMap toCbor(const ServerSettingsRequest &m)
{
    QCborMap map{{kRequestId, u64(m.requestId)}};
    if (m.settings) {
        map.insert(2, settingsToCbor(*m.settings));
    }
    return map;
}

QCborMap toCbor(const ServerSettingsResult &m)
{
    return {{kRequestId, u64(m.requestId)}, {2, settingsToCbor(m.settings)}};
}

QCborMap toCbor(const GetServerStats &m)
{
    return {{kRequestId, u64(m.requestId)}};
}

QCborMap toCbor(const ServerStatsResult &m)
{
    return {{kRequestId, u64(m.requestId)}, {2, statsToCbor(m.stats)}};
}

QCborMap toCbor(const TransferOwnership &m)
{
    return {{kRequestId, u64(m.requestId)}, {2, m.account}};
}

} // namespace opentalk::protocol
