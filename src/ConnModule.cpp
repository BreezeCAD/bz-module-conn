// BreezCONN — the module's entry: the table BreezeCAD calls (sdk/bz_module.h).
// The work is in Conn; this only hands the calls over and never lets an
// exception cross into the program.

#include "Conn.h"
#include "bz_module.h"

#include <exception>
#include <memory>
#include <string>

namespace {

const BzHostApi*               gHost = nullptr;
std::unique_ptr<bzconn::Conn>  gConn;

template <class F> void Guarded(const char* where, F&& f) {
    try {
        f();
    } catch (const std::exception& e) {
        if (gHost && gHost->log) gHost->log(BZ_LOG_ERROR, (std::string("BreezCONN: ") + where + ": " + e.what()).c_str());
    } catch (...) {
        if (gHost && gHost->log) gHost->log(BZ_LOG_ERROR, (std::string("BreezCONN: ") + where + ": сбой").c_str());
    }
}

int32_t ConnInit(const BzHostApi* host) {
    if (!host || host->abi_version != BZ_MODULE_ABI_VERSION) return BZ_EFAIL;
    gHost  = host;
    int32_t rc = BZ_EFAIL;
    Guarded("init", [&] {
        gConn = std::make_unique<bzconn::Conn>(host);
        gConn->AddActions();
        rc = BZ_OK;
    });
    return rc;
}

void ConnShutdown() {
    Guarded("shutdown", [] {
        if (gConn) gConn->Stop();
        gConn.reset();
    });
    gHost = nullptr;
}

void ConnMainTick() {
    Guarded("main_tick", [] {
        if (gConn) gConn->Tick();
    });
}

void ConnOnAction(const char* id) {
    Guarded("on_action", [&] {
        if (gConn && id) gConn->OnAction(id);
    });
}

void ConnOnPanelEvent(const char* json) {
    Guarded("on_panel_event", [&] {
        if (gConn && json) gConn->OnPanelEvent(json);
    });
}

const BzModuleApi kApi = {BZ_MODULE_ABI_VERSION, sizeof(BzModuleApi), &ConnInit, &ConnShutdown, &ConnMainTick,
                          &ConnOnAction, &ConnOnPanelEvent};

} // namespace

extern "C" BZ_MODULE_EXPORT const BzModuleApi* bz_module_entry(void) { return &kApi; }
