// BreezCONN without BreezeCAD: the seal and the server against the device's
// calls (the same code BreezeCAD's phone and tablet run), and the real module
// library with a fake program in place of BreezeCAD — the whole way there and
// back through its windows.

#include "Conn.h"
#include "Net.h"
#include "Seal.h"
#include "bz_module.h"

#include <doctest.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

using Json   = nlohmann::json;
namespace fs = std::filesystem;
using namespace bzconn;

namespace {

const std::string kSession = "0123456789abcdef0123456789abcdef";
const std::string kKey     = "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff";

} // namespace

TEST_CASE("seal: opens with the same key and session only, and notices a changed byte") {
    const std::optional<std::string> sealed = Seal("проект", kKey, kSession);
    REQUIRE(sealed);
    CHECK(sealed->size() == 24 + 16 + std::string("проект").size());
    CHECK(Open(*sealed, kKey, kSession) == std::optional<std::string>("проект"));
    CHECK_FALSE(Open(*sealed, kKey, "ffffffffffffffffffffffffffffffff"));
    std::string otherKey = kKey;
    otherKey[0]          = '1';
    CHECK_FALSE(Open(*sealed, otherKey, kSession));
    std::string changed = *sealed;
    changed.back() ^= 1;
    CHECK_FALSE(Open(changed, kKey, kSession));
    CHECK_FALSE(Seal("x", "not hex", kSession));
    CHECK(*Seal("x", kKey, kSession) != *Seal("x", kKey, kSession));   // a fresh nonce each time
}

TEST_CASE("QR: the text BreezeCAD's device reads, and its matrix") {
    const Address a{"192.168.1.5", 50213, kSession, kKey};
    CHECK(QrText(a) == "breezecad://handoff?v=1&h=192.168.1.5&p=50213&s=" + kSession + "&k=" + kKey);
    const std::vector<std::string> rows = QrRows(QrText(a));
    REQUIRE(rows.size() >= 21);
    for (const std::string& r : rows) CHECK(r.size() == rows.size());
    CHECK(rows[0].substr(0, 7) == "1111111");   // the finder pattern in the corner
}

TEST_CASE("server: the device takes the project and gives changes back; a stranger is refused") {
    std::string error;
    std::unique_ptr<Server> server = Server::Start(kSession, kKey, "PACKAGE-BYTES", &error);
    REQUIRE_MESSAGE(server, error);
    Address at = server->GetAddress();
    at.host    = "127.0.0.1";   // the test talks to itself
    CHECK(at.port > 0);

    std::atomic<bool> stop{false};
    std::vector<Server::Event> events;
    std::mutex mutex;
    std::thread worker([&] {
        while (!stop) {
            Server::Event e = server->Serve(100);
            if (e.kind == Server::Event::Kind::None) continue;
            const std::lock_guard lock(mutex);
            events.push_back(std::move(e));
        }
    });

    std::string package;
    CHECK(Fetch(at, package, 5000).ok);
    CHECK(package == "PACKAGE-BYTES");

    Address stranger = at;
    stranger.key[5]  = stranger.key[5] == 'a' ? 'b' : 'a';
    CHECK_FALSE(Fetch(stranger, package, 5000).ok);

    CHECK(Give(at, "CHANGES", 5000).ok);
    stop = true;
    worker.join();
    REQUIRE(events.size() == 3);
    CHECK(events[0].kind == Server::Event::Kind::Sent);
    CHECK(events[1].kind == Server::Event::Kind::Refused);
    CHECK(events[2].kind == Server::Event::Kind::Returned);
    CHECK(events[2].data == "CHANGES");
}

// --- the module with a fake program -------------------------------------------

namespace {

std::atomic<bool> gWantTick{false};
std::string       gConfigDir;
std::string       gReply;
std::string       gPanel;          ///< the last show_panel
bool              gPanelOpen = false;
Json              gAction;          ///< add_action
Json              gApplied;         ///< conn.apply's args
fs::path          gRoot;

const char* HostVersion() { return "0.2.6"; }
void        HostLog(int32_t, const char* message) { const std::string m = std::string("журнал: ") + (message ? message : ""); MESSAGE(m); }
int32_t     NoAssembly(const char**, const char**) { return BZ_ENODATA; }
int32_t     NoCount(int32_t*) { return BZ_ENODATA; }
int32_t     NoComponent(int32_t, BzComponentInfo*) { return BZ_ENODATA; }
int32_t     NoBounds(uint64_t, double*, double*) { return BZ_ENODATA; }
void        RequestMain() { gWantTick = true; }
const char* ConfigDir() { return gConfigDir.c_str(); }

int32_t AddAction(const char* json) {
    gAction = Json::parse(json);
    return BZ_OK;
}
int32_t EnableAction(const char*, int32_t) { return BZ_OK; }
int32_t ShowPanel(const char* json) {
    gPanel     = json;
    gPanelOpen = true;
    return BZ_OK;
}
int32_t ClosePanel(const char*) {
    const bool was = gPanelOpen;
    gPanelOpen     = false;
    return was ? BZ_OK : BZ_ENOTFOUND;
}

/// conn.* as BreezeCAD answers them (assembly/Handoff does the files there).
int32_t Execute(const char* request, const char** reply) {
    const Json r = Json::parse(request);
    const Json& args = r["args"];
    Json out;
    if (r["command"] == "conn.pack") {
        std::ofstream(fs::path(args["package"].get<std::string>()), std::ios::binary) << "PACKAGE";
        out = {{"ok", true},
               {"result",
                {{"session", args["session"]}, {"package", args["package"]}, {"id", kSession}, {"key", kKey}, {"kind", "part"},
                 {"main", "Вал.bzc"}, {"document", (gRoot / "Вал.bzc").generic_string()}}}};
    } else if (r["command"] == "conn.plan") {
        std::ifstream in(fs::path(args["returned"].get<std::string>()), std::ios::binary);
        const std::string got((std::istreambuf_iterator<char>(in)), {});
        out = got == "CHANGES" ? Json{{"ok", true},
                                      {"result",
                                       {{"files",
                                         {{{"path", "Вал.bzc"}, {"change", "update"}, {"target", "/pc/Вал.bzc"}},
                                          {{"path", "Корпус.bzc"}, {"change", "conflict"}, {"target", "/pc/Корпус.bzc"}}}}}}}
                               : Json{{"ok", false}, {"error", "не тот проект"}};
    } else if (r["command"] == "conn.apply") {
        gApplied = args;
        out      = {{"ok", true}, {"result", {{"written", {"/pc/Вал.bzc", "/pc/Корпус.bzc"}}, {"backups", Json::array()}}}};
    } else {
        out = {{"ok", false}, {"error", "нет такой команды"}};
    }
    gReply = out.dump();
    *reply = gReply.c_str();
    return BZ_OK;
}

BzHostApi FakeHost() {
    BzHostApi h;
    std::memset(&h, 0, sizeof h);
    h.abi_version      = BZ_MODULE_ABI_VERSION;
    h.struct_size      = sizeof h;
    h.host_version     = HostVersion;
    h.log              = HostLog;
    h.assembly_name    = NoAssembly;
    h.component_count  = NoCount;
    h.component_at     = NoComponent;
    h.component_bounds = NoBounds;
    h.request_main     = RequestMain;
    h.execute          = Execute;
    h.config_dir       = ConfigDir;
    h.add_action       = AddAction;
    h.enable_action    = EnableAction;
    h.show_panel       = ShowPanel;
    h.close_panel      = ClosePanel;
    return h;
}

const BzModuleApi* LoadModule(const char* path) {
#if defined(_WIN32)
    void* lib = reinterpret_cast<void*>(LoadLibraryA(path));
    if (!lib) return nullptr;
    auto entry = reinterpret_cast<BzModuleEntryFn>(reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(lib), BZ_MODULE_ENTRY_NAME)));
#else
    void* lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!lib) return nullptr;
    auto entry = reinterpret_cast<BzModuleEntryFn>(dlsym(lib, BZ_MODULE_ENTRY_NAME));
#endif
    return entry ? entry() : nullptr;   // the library stays loaded until the process ends
}

Json Panel() { return Json::parse(gPanel); }

/// The first item of `type` in the panel shown last (null when none).
Json Item(const char* type) {
    const Json panel = Panel();   // kept: the loop must not walk a temporary
    for (const Json& i : panel["items"])
        if (i["type"] == type) return i;
    return nullptr;
}

} // namespace

TEST_CASE("module: the project goes to the device and comes back through the windows") {
    gRoot = fs::temp_directory_path() / "bz_conn_module";
    fs::remove_all(gRoot);
    fs::create_directories(gRoot / "config");
    const std::u8string config = (gRoot / "config").generic_u8string();
    gConfigDir                 = std::string(config.begin(), config.end());

    const BzModuleApi* module = LoadModule(BZ_CONN_MODULE);
    REQUIRE(module);
    REQUIRE(BZ_HAS(module, on_action));
    REQUIRE(BZ_HAS(module, on_panel_event));
    static const BzHostApi host = FakeHost();
    REQUIRE(module->init(&host) == BZ_OK);
    CHECK(gAction["id"] == "send");
    CHECK(gAction["menu"] == "file");
    CHECK(gAction["ribbon"]["group"] == "BreezCONN");

    module->on_action("send");
    REQUIRE(gPanelOpen);
    CHECK(Panel()["badge"] == "BreezCONN");
    const Json mono = Item("mono");
    if (mono.is_null()) {   // a machine in no network: the window says so
        CHECK(Panel()["items"][0]["text"].get<std::string>().find("не подключён к сети") != std::string::npos);
        module->on_panel_event(R"({"panel":"send","id":"close","values":{}})");
        CHECK_FALSE(gPanelOpen);
        module->shutdown();
        MESSAGE("нет сети — обмен с устройством не проверен");
        return;
    }
    CHECK_FALSE(Item("bitmap").is_null());
    CHECK(Item("status")["busy"] == true);
    const std::string where = mono["text"];
    Address at{"127.0.0.1", std::stoi(where.substr(where.find(':') + 1)), kSession, kKey};

    // The test plays the program's main thread: main_tick when the module asks.
    const auto pump = [&](auto&& done) {
        for (int i = 0; i < 400 && !done(); ++i) {
            if (gWantTick.exchange(false)) module->main_tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    };

    std::string package;
    REQUIRE(Fetch(at, package, 5000).ok);
    CHECK(package == "PACKAGE");
    pump([] { return Item("status")["text"] != "Жду устройство…"; });
    CHECK(Item("status")["text"] == "«Вал» на устройстве");

    REQUIRE(Give(at, "CHANGES", 5000).ok);
    pump([] { return !Item("choice").is_null(); });
    const Json choice = Item("choice");
    REQUIRE_FALSE(choice.is_null());
    CHECK(choice["label"] == "Корпус.bzc");
    CHECK(choice["value"] == 2);   // «Оба»: nothing is lost unless the user says so
    CHECK(choice["options"].size() == 3);

    module->on_panel_event(R"({"panel":"send","id":"apply","values":{"f1":1}})");   // «Своё»
    CHECK(gApplied["choices"] == Json({"device", "mine"}));
    CHECK(Item("status")["text"] == "Изменения с устройства на месте: файлов 2.");
    CHECK_FALSE(Fetch(at, package, 1000).ok);   // the server is gone

    module->on_panel_event(R"({"panel":"send","id":"close","values":{}})");
    CHECK_FALSE(gPanelOpen);
    CHECK_FALSE(fs::exists(gRoot / "config" / "conn" / "send.bzp"));

    const auto t0 = std::chrono::steady_clock::now();
    module->shutdown();
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2));
    fs::remove_all(gRoot);
}

TEST_CASE("module: closing the window while waiting stops listening") {
    gRoot = fs::temp_directory_path() / "bz_conn_module_close";
    fs::remove_all(gRoot);
    fs::create_directories(gRoot / "config");
    const std::u8string config = (gRoot / "config").generic_u8string();
    gConfigDir                 = std::string(config.begin(), config.end());

    const BzModuleApi* module = LoadModule(BZ_CONN_MODULE);
    REQUIRE(module);
    static const BzHostApi host = FakeHost();
    REQUIRE(module->init(&host) == BZ_OK);
    module->on_action("send");
    const Json mono = Item("mono");
    if (!mono.is_null()) {
        const std::string where = mono["text"];
        const Address at{"127.0.0.1", std::stoi(where.substr(where.find(':') + 1)), kSession, kKey};
        module->on_panel_event(R"({"panel":"send","id":"close","values":{}})");   // Esc
        CHECK_FALSE(gPanelOpen);
        std::string package;
        CHECK_FALSE(Fetch(at, package, 1000).ok);
    }
    module->shutdown();
    fs::remove_all(gRoot);
}
