// SPDX-License-Identifier: GPL-3.0-only
#include "opentalk/security/PacketCipher.h"

#include <QtEndian>

#include <sodium.h>

#include <array>
#include <limits>
#include <utility>

namespace opentalk::security {
namespace {

static_assert(crypto_aead_xchacha20poly1305_ietf_KEYBYTES == kPacketKeyBytes);
static_assert(crypto_aead_xchacha20poly1305_ietf_ABYTES == kPacketTagBytes);

using Nonce = std::array<unsigned char, crypto_aead_xchacha20poly1305_ietf_NPUBBYTES>;

Nonce makeNonce(quint64 counter, PacketDirection direction)
{
    Nonce nonce{};
    qToBigEndian(counter, nonce.data());
    nonce[8] = static_cast<unsigned char>(direction);
    return nonce;
}

const unsigned char *bytes(QByteArrayView view)
{
    return reinterpret_cast<const unsigned char *>(view.data());
}

bool ensureSodium()
{
    return sodium_init() >= 0;
}

} // namespace

QByteArray randomPacketKey()
{
    QByteArray key(kPacketKeyBytes, Qt::Uninitialized);
    if (!ensureSodium()) {
        return {};
    }
    randombytes_buf(key.data(), static_cast<size_t>(key.size()));
    return key;
}

std::optional<PacketHeader> peekPacketHeader(QByteArrayView datagram)
{
    if (datagram.size() < kPacketHeaderBytes) {
        return std::nullopt;
    }
    const auto *p = bytes(datagram);
    PacketHeader header;
    header.kind = p[0];
    header.sessionId = qFromBigEndian<quint32>(p + 1);
    header.counter = qFromBigEndian<quint64>(p + 5);
    return header;
}

PacketSealer::PacketSealer(QByteArray key, PacketDirection direction, quint32 sessionId)
    : m_key(std::move(key))
    , m_direction(direction)
    , m_sessionId(sessionId)
{
    ensureSodium();
}

QByteArray PacketSealer::seal(quint8 kind, QByteArrayView plaintext)
{
    if (!isValid() || m_counter == std::numeric_limits<quint64>::max()) {
        return {};
    }
    ++m_counter;
    QByteArray datagram(kPacketOverheadBytes + plaintext.size(), Qt::Uninitialized);
    auto *out = reinterpret_cast<unsigned char *>(datagram.data());
    out[0] = kind;
    qToBigEndian(m_sessionId, out + 1);
    qToBigEndian(m_counter, out + 5);
    const Nonce nonce = makeNonce(m_counter, m_direction);
    unsigned long long written = 0;
    crypto_aead_xchacha20poly1305_ietf_encrypt(out + kPacketHeaderBytes, &written, bytes(plaintext),
                                               static_cast<unsigned long long>(plaintext.size()), out,
                                               kPacketHeaderBytes, nullptr, nonce.data(), bytes(m_key));
    datagram.truncate(kPacketHeaderBytes + static_cast<qsizetype>(written));
    return datagram;
}

PacketOpener::PacketOpener(QByteArray key, PacketDirection direction, quint32 sessionId)
    : m_key(std::move(key))
    , m_direction(direction)
    , m_sessionId(sessionId)
{
    ensureSodium();
}

std::optional<QByteArray> PacketOpener::open(QByteArrayView datagram, PacketHeader *headerOut)
{
    const std::optional<PacketHeader> header = peekPacketHeader(datagram);
    if (!isValid() || !header || datagram.size() < kPacketOverheadBytes || header->sessionId != m_sessionId ||
        header->counter == 0) {
        return std::nullopt;
    }
    // Cheap replay check before spending time on decryption.
    const quint64 counter = header->counter;
    if (counter <= m_highest) {
        const quint64 age = m_highest - counter;
        if (age >= 64 || (m_window & (quint64{1} << age)) != 0) {
            return std::nullopt;
        }
    }
    const qsizetype cipherLength = datagram.size() - kPacketHeaderBytes;
    QByteArray plaintext(cipherLength - kPacketTagBytes, Qt::Uninitialized);
    const Nonce nonce = makeNonce(counter, m_direction);
    unsigned long long written = 0;
    const auto *in = bytes(datagram);
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(
            reinterpret_cast<unsigned char *>(plaintext.data()), &written, nullptr, in + kPacketHeaderBytes,
            static_cast<unsigned long long>(cipherLength), in, kPacketHeaderBytes, nonce.data(), bytes(m_key)) != 0) {
        return std::nullopt;
    }
    // Only authentic packets move the window.
    if (counter > m_highest) {
        const quint64 shift = counter - m_highest;
        m_window = shift >= 64 ? 0 : m_window << shift;
        m_window |= 1;
        m_highest = counter;
    } else {
        m_window |= quint64{1} << (m_highest - counter);
    }
    plaintext.truncate(static_cast<qsizetype>(written));
    if (headerOut != nullptr) {
        *headerOut = *header;
    }
    return plaintext;
}

} // namespace opentalk::security
