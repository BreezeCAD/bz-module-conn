#include "Seal.h"

#include <monocypher.h>

#include <array>
#include <cstdint>
#include <format>
#include <random>

namespace bzconn {

namespace {

constexpr std::size_t kNonce = 24, kMac = 16, kKey = 32;

int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/// 64 hex digits → the 32-byte key; false when it is not one.
bool KeyBytes(const std::string& hex, std::array<std::uint8_t, kKey>& key) {
    if (hex.size() != kKey * 2) return false;
    for (std::size_t i = 0; i < kKey; ++i) {
        const int hi = HexDigit(hex[i * 2]), lo = HexDigit(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        key[i] = static_cast<std::uint8_t>(hi * 16 + lo);
    }
    return true;
}

const std::uint8_t* Raw(std::string_view s) { return reinterpret_cast<const std::uint8_t*>(s.data()); }
std::uint8_t*       Raw(std::string& s) { return reinterpret_cast<std::uint8_t*>(s.data()); }

} // namespace

std::string QrText(const Address& a) {
    return std::format("breezecad://handoff?v={}&h={}&p={}&s={}&k={}", kHandoffVersion, a.host, a.port, a.session, a.key);
}

std::optional<std::string> Seal(std::string_view bytes, const std::string& keyHex, const std::string& session) {
    std::array<std::uint8_t, kKey> key{};
    if (!KeyBytes(keyHex, key)) return std::nullopt;
    std::string out(kNonce + kMac + bytes.size(), '\0');
    // A fresh random nonce for every message: XChaCha20's 24 bytes make a repeat practically impossible.
    std::random_device device;
    for (std::size_t i = 0; i < kNonce; ++i) out[i] = static_cast<char>(device() & 0xFFu);
    crypto_aead_lock(Raw(out) + kNonce + kMac, Raw(out) + kNonce, key.data(), Raw(out), Raw(session), session.size(), Raw(bytes),
                     bytes.size());
    crypto_wipe(key.data(), key.size());
    return out;
}

std::optional<std::string> Open(std::string_view sealed, const std::string& keyHex, const std::string& session) {
    if (sealed.size() < kNonce + kMac) return std::nullopt;
    std::array<std::uint8_t, kKey> key{};
    if (!KeyBytes(keyHex, key)) return std::nullopt;
    std::string out(sealed.size() - kNonce - kMac, '\0');
    const int failed = crypto_aead_unlock(Raw(out), Raw(sealed) + kNonce, key.data(), Raw(sealed), Raw(session), session.size(),
                                          Raw(sealed) + kNonce + kMac, out.size());
    crypto_wipe(key.data(), key.size());
    if (failed) return std::nullopt;
    return out;
}

} // namespace bzconn
