#pragma once
// «Продолжить на устройстве» over the local network: a server on the PC that
// lives only while the window is open. Moved from BreezeCAD
// (assembly/HandoffNet) without a change to what goes over the wire — the
// phone and the tablet keep their side there.
//
// One request per connection: a frame "BZH1" | type (1 byte) | length (4,
// big-endian) | body; every body is Seal()ed with the key from the QR, so
// only the device that scanned this project's QR is answered. The PC never
// connects anywhere: it only listens.

#include "Seal.h"

#include <cstddef>
#include <memory>
#include <string>

namespace bzconn {

/// The largest package taken over the network (a project of hundreds of parts fits).
inline constexpr std::size_t kMaxTransfer = std::size_t{256} << 20;

/// Something that worked or a reason (Russian, for the user).
struct Result {
    bool        ok = true;
    std::string error;
    static Result Fail(std::string why) { return {false, std::move(why)}; }
};

class Server {
public:
    /// Listens on every network of the PC on a free port. `package` is the
    /// project as bytes (conn.pack). The address is for the QR; its host is
    /// empty when the PC is in no network.
    static std::unique_ptr<Server> Start(const std::string& session, const std::string& key, std::string package, std::string* error);
    ~Server();
    Server(const Server&)            = delete;
    Server& operator=(const Server&) = delete;

    const Address& GetAddress() const;

    struct Event {
        enum class Kind {
            None,       ///< nobody came in time
            Sent,       ///< the device took the project
            Returned,   ///< the device gave changes back: `data` is the return package (conn.plan)
            Refused,    ///< someone without this project's key: answered nothing useful
        };
        Kind        kind = Kind::None;
        std::string data;
    };

    /// Waits up to `timeoutMs` for one device and serves it. Blocks: called
    /// from a worker thread.
    Event Serve(int timeoutMs);

    /// Stops listening. Call it from the thread that calls Serve, or after it ended.
    void Stop();

private:
    struct Impl;
    explicit Server(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> myImpl;
};

/// The PC's address in the local network (the one its default route uses),
/// empty when there is none. No packet is sent to find it.
std::string LocalAddress();

// --- The device's two calls (BreezeCAD's mobile builds make them; here for the tests)

/// Takes the project from the PC at `address`.
Result Fetch(const Address& address, std::string& package, int timeoutMs = 15000);
/// Gives the changes back to the PC.
Result Give(const Address& address, const std::string& returned, int timeoutMs = 15000);

} // namespace bzconn
