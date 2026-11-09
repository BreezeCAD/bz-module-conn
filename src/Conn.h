#pragma once
// BreezCONN on the PC: «Продолжить на устройстве» as a module (ADR-0011).
//
// The program packs and puts back the files (commands conn.pack, conn.plan,
// conn.apply); the module brings the rest — the server in the local network,
// the QR and the windows. Everything here runs on the program's main thread
// (init, on_action, on_panel_event, main_tick) except the server's worker,
// which only queues what happened and asks for a main_tick.

#include "Net.h"
#include "bz_module.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace bzconn {

using Json = nlohmann::json;

class Conn {
public:
    explicit Conn(const BzHostApi* host);
    ~Conn();
    Conn(const Conn&)            = delete;
    Conn& operator=(const Conn&) = delete;

    /// The menu item and the ribbon button (init).
    void AddActions();
    void OnAction(const std::string& id);
    void OnPanelEvent(const std::string& json);
    /// What the server's worker queued (main_tick).
    void Tick();
    /// Ends a transfer: the worker stops, the window closes.
    void Stop();

    // The ids of what the module shows.
    static constexpr const char* kAction = "send";
    static constexpr const char* kPanel  = "send";

private:
    enum class Stage {
        Idle,       ///< nothing going on
        Waiting,    ///< the QR is shown, the device has not come yet
        Taken,      ///< the device has the project
        Choosing,   ///< changes came back; some files changed here too
        Finished,   ///< the result is shown; closing ends it
    };

    struct ReturnedFile {
        std::string path, change, target;
    };

    void Start();
    /// Runs a command of the program; the "result", or empty with `error` set.
    Json Run(const char* command, const Json& args, std::string& error);
    void TakeReturn(std::string bytes);
    void Apply(const Json& values);
    void Finish(std::string text);
    void Fail(std::string text);
    void Show();
    void Log(int level, const std::string& text) const;
    std::filesystem::path WorkDir() const;

    const BzHostApi* myHost;
    Stage            myStage = Stage::Idle;
    std::string      myTitle;        ///< the document's name, for the texts
    std::string      mySessionFile;  ///< conn.pack's session (UTF-8 path)
    std::string      myMessage;      ///< Finished: what to say
    bool             myFailed  = false;
    bool             myShowLink = false;
    int              myRefused = 0;
    std::vector<std::string> myQr;   ///< rows of '0' / '1'; empty: the PC is in no network
    std::vector<ReturnedFile> myPlan;

    std::unique_ptr<Server> myServer;
    std::thread             myWorker;
    std::atomic<bool>       myStop{false};
    std::mutex              myMutex;
    std::deque<Server::Event> myEvents;   ///< from the worker, taken in Tick
};

/// The QR of `text` as rows of '0' / '1' (medium error correction, as before in BreezeCAD).
std::vector<std::string> QrRows(const std::string& text);

} // namespace bzconn
