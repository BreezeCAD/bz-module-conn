/* Модуль BreezeCAD — точка входа.
 *
 * Весь договор с программой — в sdk/bz_module.h:
 *  - экспортировать bz_module_entry() и вернуть таблицу своих функций;
 *  - в init() проверить версию ABI программы и запомнить её таблицу;
 *  - читать сборку через host->component_*; менять документы — только
 *    через host->execute() (это обычные шаги отмены Ctrl+Z);
 *  - в shutdown() всё освободить и забыть указатели программы.
 *
 * Пункт меню и окно (ADR-0011, ABI 2) — внизу, в блоке
 * #if BZ_MODULE_ABI_VERSION >= 2: включится сам, когда sdk/bz_module.h
 * обновится до программы с окнами модулей. Не нужны — удалите блок.
 *
 * Модуль можно писать и на C++: переименуйте файл в .cpp и поправьте
 * список исходников в CMakeLists.txt. Исключения через границу не бросать.
 */
#include "bz_module.h"
#include "bz_panel.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static const BzHostApi* host = NULL;

static int32_t module_init(const BzHostApi* h) {
    /* Программа другой версии ABI или слишком старая — не запускаемся. */
    if (!h || h->abi_version != BZ_MODULE_ABI_VERSION || h->struct_size < offsetof(BzHostApi, request_main)) return BZ_EFAIL;
    host = h;

    char    line[160];
    int32_t count = 0;
    if (host->component_count(&count) == BZ_OK)
        snprintf(line, sizeof line, "Модуль example: в сборке компонентов — %d", (int)count);
    else
        snprintf(line, sizeof line, "Модуль example запущен (BreezeCAD %s), сборка не открыта", host->host_version());
    host->log(BZ_LOG_INFO, line);

    /* Команды execute() — только на главном потоке, в main_tick. */
    if (BZ_HAS(host, request_main)) host->request_main();

#if BZ_MODULE_ABI_VERSION >= 2
    /* Пункт в меню «Файл» и в поиске команд. Программа старше окон модулей —
     * просто без него. */
    if (BZ_HAS(host, add_action)) {
        char action[512];
        const char* json = bz_action_json(action, sizeof action, "example.hello", "Пример модуля",
                                          "Показывает окно модуля", "info", "file", NULL, NULL);
        if (json) host->add_action(json);
    }
#endif
    return BZ_OK;
}

#if BZ_MODULE_ABI_VERSION >= 2
/* Пункт меню нажат — показываем окно. */
static void module_on_action(const char* id) {
    if (!host || !id || strcmp(id, "example.hello") != 0 || !BZ_HAS(host, show_panel)) return;
    char    buf[2048];
    BzPanel p;
    bz_panel_begin(&p, buf, sizeof buf, "example.hello", "Пример модуля", "Пример");
    bz_panel_text(&p, "Окно рисует BreezeCAD по этому описанию — своими виджетами и темой.", 1);
    bz_panel_buttons(&p);
    bz_panel_button(&p, "close", "Закрыть", "secondary", "Esc");
    if (bz_panel_end(&p)) host->show_panel(buf);
}

/* Нажатия в окне: {"panel": "...", "id": "...", "values": {...}}. */
static void module_on_panel_event(const char* json) {
    if (host && json && strstr(json, "\"id\": \"close\"") && BZ_HAS(host, close_panel)) host->close_panel("example.hello");
}
#endif

static void module_main_tick(void) {
    const char* reply = NULL;
    if (!host || !BZ_HAS(host, execute)) return;
    /* Пример команды. Список всех: {"command": "commands.list"}. */
    if (host->execute("{\"command\": \"app.documents\"}", &reply) == BZ_OK && reply) {
        char line[400];
        snprintf(line, sizeof line, "Модуль example: открытые документы — %.300s", reply);
        host->log(BZ_LOG_INFO, line);
    }
}

static void module_shutdown(void) {
    if (host) host->log(BZ_LOG_INFO, "Модуль example: выключен");
    host = NULL;
}

#if BZ_MODULE_ABI_VERSION >= 2
static const BzModuleApi api = {BZ_MODULE_ABI_VERSION, sizeof(BzModuleApi), module_init, module_shutdown, module_main_tick,
                                module_on_action, module_on_panel_event};
#else
static const BzModuleApi api = {BZ_MODULE_ABI_VERSION, sizeof(BzModuleApi), module_init, module_shutdown, module_main_tick};
#endif

BZ_MODULE_EXPORT const BzModuleApi* bz_module_entry(void) { return &api; }
