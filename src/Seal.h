#pragma once
// What goes over the network, as BreezeCAD's device side expects it
// (assembly/Handoff in BreezeCAD — the phone and the tablet keep that code):
// the QR text, and every message sealed with the project's key.
//
// Seal: XChaCha20-Poly1305 (Monocypher), the session id as associated data —
// a message for another project does not open. Layout: nonce (24) | MAC (16) | data.

#include <optional>
#include <string>
#include <string_view>

namespace bzconn {

inline constexpr int kHandoffVersion = 1;

/// Where the PC listens and for which project: what the QR carries.
struct Address {
    std::string host;
    int         port = 0;
    std::string session;   ///< 32 hex digits
    std::string key;       ///< 64 hex digits
};

/// "breezecad://handoff?v=1&h=192.168.1.5&p=50213&s=…&k=…"
std::string QrText(const Address& address);

/// `bytes` sealed with `keyHex` for `session`; nullopt when the key is not 64 hex digits.
std::optional<std::string> Seal(std::string_view bytes, const std::string& keyHex, const std::string& session);
/// The bytes back; nullopt when the key or the session differs or a byte was changed.
std::optional<std::string> Open(std::string_view sealed, const std::string& keyHex, const std::string& session);

} // namespace bzconn
