#include "Conn.h"

#include <qrcodegen.hpp>

#include <format>
#include <fstream>
#include <iterator>
#include <system_error>

namespace bzconn {

namespace fs = std::filesystem;

namespace {

fs::path FromUtf8(const std::string& text) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string ToUtf8(const fs::path& path) {
    const std::u8string u = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

std::string Text(const Json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

bool ReadAll(const fs::path& file, std::string& out) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), {});
    return true;
}

bool WriteAll(const fs::path& file, const std::string& bytes) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

Json Button(const char* id, const char* label, const char* style, const char* key) {
    Json b = {{"id", id}, {"label", label}, {"style", style}};
    if (key) b["key"] = key;
    return b;
}

// The options of a conflict, in the order of conn.apply's choices.
constexpr const char* kChoiceIds[] = {"device", "mine", "both"};
constexpr int         kBoth        = 2;   // what a conflict starts with: nothing is lost

} // namespace

std::vector<std::string> QrRows(const std::string& text) {
    const qrcodegen::QrCode code = qrcodegen::QrCode::encodeText(text.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
    std::vector<std::string> rows;
    const int n = code.getSize();
    for (int y = 0; y < n; ++y) {
        std::string row;
        for (int x = 0; x < n; ++x) row.push_back(code.getModule(x, y) ? '1' : '0');
        rows.push_back(std::move(row));
    }
    return rows;
}

Conn::Conn(const BzHostApi* host) : myHost(host) {}

Conn::~Conn() {
    myStop = true;
    if (myWorker.joinable()) myWorker.join();
}

void Conn::Log(int level, const std::string& text) const {
    if (myHost && myHost->log) myHost->log(level, ("BreezCONN: " + text).c_str());
}

fs::path Conn::WorkDir() const {
    const char* config = BZ_HAS(myHost, config_dir) ? myHost->config_dir() : nullptr;
    fs::path dir = FromUtf8(config ? config : "") / "conn";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

void Conn::AddActions() {
    if (!BZ_HAS(myHost, add_action)) {
        Log(BZ_LOG_WARNING, "программа без окон модулей — нужна BreezeCAD новее");
        return;
    }
    const Json action = {{"id", kAction},
                         {"label", "Продолжить на устройстве"},
                         {"tooltip", "Открытый документ — на телефон или планшет по QR-коду в той же сети, потом изменения обратно"},
                         {"icon", "export"},
                         {"menu", "file"},
                         {"ribbon", {{"tab", "model"}, {"group", "BreezCONN"}}}};
    if (myHost->add_action(action.dump().c_str()) != BZ_OK) Log(BZ_LOG_ERROR, "пункт меню не добавлен");
}

void Conn::OnAction(const std::string& id) {
    if (id != kAction) return;
    if (myStage != Stage::Idle) {   // already going: the window again
        Show();
        return;
    }
    Start();
}

Json Conn::Run(const char* command, const Json& args, std::string& error) {
    const char* reply = nullptr;
    const std::string request = Json{{"command", command}, {"args", args}}.dump();
    if (!BZ_HAS(myHost, execute) || myHost->execute(request.c_str(), &reply) != BZ_OK || !reply) {
        error = "программа не выполнила команду";
        return {};
    }
    const Json r = Json::parse(reply, nullptr, false);
    if (!r.is_object()) {
        error = "непонятный ответ программы";
        return {};
    }
    if (r.value("ok", false)) return r.contains("result") ? r["result"] : Json::object();
    error = Text(r, "error");
    if (error.empty()) error = "команда не выполнена";
    return {};
}

void Conn::Start() {
    const fs::path dir = WorkDir();
    mySessionFile      = ToUtf8(dir / "session.json");
    std::string error;
    const Json pack = Run("conn.pack", {{"package", ToUtf8(dir / "send.bzp")}, {"session", mySessionFile}}, error);
    if (!error.empty()) {
        Fail("Проект не подготовлен к передаче: " + error);
        return;
    }
    const std::u8string stem = FromUtf8(Text(pack, "document")).stem().u8string();
    myTitle = std::string(reinterpret_cast<const char*>(stem.data()), stem.size());
    std::string bytes;
    if (!ReadAll(FromUtf8(Text(pack, "package")), bytes)) {
        Fail("Не удалось прочитать подготовленный проект");
        return;
    }
    myServer = Server::Start(Text(pack, "id"), Text(pack, "key"), std::move(bytes), &error);
    if (!myServer) {
        Fail(error);
        return;
    }
    myStage = Stage::Waiting;
    const Address& a = myServer->GetAddress();
    if (!a.host.empty()) {
        myQr   = QrRows(QrText(a));
        myStop = false;
        myWorker = std::thread([this] {
            while (!myStop) {
                Server::Event e = myServer->Serve(250);
                if (e.kind == Server::Event::Kind::None) continue;
                {
                    const std::lock_guard lock(myMutex);
                    myEvents.push_back(std::move(e));
                }
                if (BZ_HAS(myHost, request_main)) myHost->request_main();   // any thread may ask
            }
        });
        Log(BZ_LOG_INFO, std::format("жду устройство на {}:{}", a.host, a.port));
    }
    Show();
}

void Conn::Tick() {
    std::deque<Server::Event> events;
    {
        const std::lock_guard lock(myMutex);
        events.swap(myEvents);
    }
    if (events.empty() || myStage == Stage::Idle) return;
    for (Server::Event& e : events) {
        switch (e.kind) {
        case Server::Event::Kind::Sent:
            if (myStage == Stage::Waiting) myStage = Stage::Taken;
            Log(BZ_LOG_INFO, "устройство взяло проект");
            break;
        case Server::Event::Kind::Refused:
            ++myRefused;
            break;
        case Server::Event::Kind::Returned:
            if (myStage == Stage::Choosing || myStage == Stage::Finished) break;   // one return at a time
            TakeReturn(std::move(e.data));
            break;
        case Server::Event::Kind::None:
            break;
        }
        if (myStage == Stage::Idle || myStage == Stage::Finished) break;
    }
    Show();
}

void Conn::TakeReturn(std::string bytes) {
    const fs::path returned = WorkDir() / "back.zip";
    if (!WriteAll(returned, bytes)) {
        myMessage = "Не удалось записать полученные данные (диск заполнен?)";
        return;
    }
    std::string error;
    const Json plan = Run("conn.plan", {{"session", mySessionFile}, {"returned", ToUtf8(returned)}}, error);
    if (!error.empty()) {
        myMessage = "Изменения с устройства не приняты: " + error;   // the device may try again
        return;
    }
    myPlan.clear();
    bool conflict = false;
    if (const auto files = plan.find("files"); files != plan.end() && files->is_array())
        for (const Json& f : *files) {
            myPlan.push_back({Text(f, "path"), Text(f, "change"), Text(f, "target")});
            conflict = conflict || myPlan.back().change == "conflict";
        }
    if (myPlan.empty()) {
        Finish("Проект вернулся с устройства без изменений.");
        return;
    }
    myMessage.clear();
    if (conflict) myStage = Stage::Choosing;
    else Apply(Json::object());
}

void Conn::Apply(const Json& values) {
    Json choices = Json::array();
    for (std::size_t i = 0; i < myPlan.size(); ++i) {
        int choice = 0;   // a file changed only on the device simply comes back
        if (myPlan[i].change == "conflict") {
            choice = kBoth;
            const auto v = values.find(std::format("f{}", i));
            if (v != values.end() && v->is_number_integer()) choice = v->get<int>();
            if (choice < 0 || choice > kBoth) choice = kBoth;
        }
        choices.push_back(kChoiceIds[choice]);
    }
    std::string error;
    const Json r = Run("conn.apply", {{"session", mySessionFile}, {"returned", ToUtf8(WorkDir() / "back.zip")}, {"choices", choices}}, error);
    if (!error.empty()) {
        myMessage = "Изменения с устройства не применены: " + error;
        return;
    }
    const std::size_t written = r.contains("written") && r["written"].is_array() ? r["written"].size() : 0;
    Finish(std::format("Изменения с устройства на месте: файлов {}.", written));
}

void Conn::Finish(std::string text) {
    myStop = true;
    if (myWorker.joinable()) myWorker.join();
    myServer.reset();   // the device has given its copy up: nobody is listened to any more
    myStage   = Stage::Finished;
    myFailed  = false;
    myMessage = std::move(text);
    Log(BZ_LOG_INFO, myMessage);
}

void Conn::Fail(std::string text) {
    Finish(std::move(text));
    myFailed = true;
    Show();
}

void Conn::Stop() {
    myStop = true;
    if (myWorker.joinable()) myWorker.join();
    myServer.reset();
    if (myStage != Stage::Idle && BZ_HAS(myHost, close_panel)) myHost->close_panel(kPanel);
    myStage = Stage::Idle;
    myQr.clear();
    myPlan.clear();
    myMessage.clear();
    myRefused  = 0;
    myFailed   = false;
    myShowLink = false;
    {
        const std::lock_guard lock(myMutex);
        myEvents.clear();
    }
    // The package and what came back; the replaced files («Копии») stay.
    std::error_code ec;
    const fs::path dir = WorkDir();
    for (const char* name : {"send.bzp", "back.zip", "session.json"}) fs::remove(dir / name, ec);
}

void Conn::OnPanelEvent(const std::string& json) {
    const Json e = Json::parse(json, nullptr, false);
    if (!e.is_object() || Text(e, "panel") != kPanel) return;
    const std::string id = Text(e, "id");
    if (id == "close") {
        Stop();
    } else if (id == "apply" && myStage == Stage::Choosing) {
        Apply(e.contains("values") ? e["values"] : Json::object());
        Show();
    } else if (id == "link") {
        myShowLink = !myShowLink;
        Show();
    }
}

void Conn::Show() {
    if (!BZ_HAS(myHost, show_panel) || myStage == Stage::Idle) return;
    Json items = Json::array();
    const auto text = [&](const std::string& t, bool secondary) {
        items.push_back({{"type", "text"}, {"text", t}, {"secondary", secondary}});
    };
    const auto buttons = [&](Json list) { items.push_back({{"type", "buttons"}, {"items", std::move(list)}}); };

    switch (myStage) {
    case Stage::Waiting:
        if (myQr.empty()) {
            text("Компьютер не подключён к сети: устройство его не найдёт. Подключитесь к Wi-Fi или к сети и попробуйте снова.", false);
            buttons({Button("close", "Закрыть", "secondary", "Esc")});
            break;
        }
        text(std::format("Наведите камеру телефона или планшета на код — откроется BreezeCAD с «{}». Устройство должно быть в той же сети.",
                         myTitle),
             true);
        items.push_back({{"type", "bitmap"}, {"modules", myQr}, {"size", 240}});
        if (myServer) {
            const Address& a = myServer->GetAddress();
            items.push_back({{"type", "mono"}, {"text", std::format("{}:{}", a.host, a.port)}});
            if (myShowLink) items.push_back({{"type", "mono"}, {"text", QrText(a)}});
        }
        items.push_back({{"type", "status"}, {"text", "Жду устройство…"}, {"busy", true}});
#if defined(_WIN32)
        items.push_back({{"type", "note"}, {"icon", "ui-info"}, {"text", "Если Windows спросит о доступе к сети — разрешите для частных сетей."}});
#endif
        items.push_back({{"type", "link"}, {"id", "link"}, {"label", myShowLink ? "Скрыть ссылку" : "Показать ссылку для устройства"}});
        buttons({Button("close", "Закрыть", "secondary", "Esc")});
        break;
    case Stage::Taken:
        items.push_back({{"type", "status"}, {"text", std::format("«{}» на устройстве", myTitle)}, {"busy", true}});
        text("Когда закончите, нажмите там «Вернуть на компьютер» — изменения придут сюда. Не закрывайте это окно.", true);
        buttons({Button("close", "Прекратить передачу", "secondary", "Esc")});
        break;
    case Stage::Choosing: {
        text("Эти файлы изменились и здесь, и на устройстве. Что оставить?", false);
        int others = 0;
        for (std::size_t i = 0; i < myPlan.size(); ++i) {
            if (myPlan[i].change != "conflict") {
                ++others;
                continue;
            }
            const std::u8string name = FromUtf8(myPlan[i].target).filename().u8string();
            items.push_back({{"type", "choice"},
                             {"id", std::format("f{}", i)},
                             {"label", std::string(reinterpret_cast<const char*>(name.data()), name.size())},
                             {"preview", myPlan[i].target},
                             {"options", {"С устройства", "Своё", "Оба"}},
                             {"value", kBoth}});
        }
        if (others > 0) text(std::format("Остальные изменения применятся сами: файлов {}", others), true);
        buttons({Button("apply", "Применить", "primary", "Enter"), Button("close", "Отмена", "secondary", "Esc")});
        break;
    }
    case Stage::Finished:
        items.push_back({{"type", "status"}, {"text", myMessage}, {"busy", false}});
        buttons({Button("close", "Готово", "primary", "Enter")});
        break;
    case Stage::Idle:
        return;
    }
    if (myStage != Stage::Finished && !myMessage.empty())
        items.push_back({{"type", "note"}, {"icon", "status-warning"}, {"text", myMessage}});
    if (myRefused > 0 && myStage != Stage::Finished)
        items.push_back({{"type", "note"}, {"icon", "status-warning"}, {"text", std::format("Отклонено подключений с чужим кодом: {}", myRefused)}});
    // Buttons last, below the notes.
    if (!items.empty() && items.back()["type"] != "buttons") {
        for (auto it = items.begin(); it != items.end(); ++it)
            if ((*it)["type"] == "buttons") {
                Json b = *it;
                items.erase(it);
                items.push_back(std::move(b));
                break;
            }
    }
    const Json panel = {{"id", kPanel}, {"title", myFailed ? "Передача не удалась" : "Продолжить на устройстве"},
                        {"badge", "BreezCONN"}, {"width", 440}, {"items", items}};
    myHost->show_panel(panel.dump().c_str());
}

} // namespace bzconn
