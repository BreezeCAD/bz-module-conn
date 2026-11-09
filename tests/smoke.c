/* Проверка без BreezeCAD: загрузить собранную библиотеку так же, как это
 * делает программа, и прогнать init → main_tick → shutdown с поддельной
 * таблицей программы. Ловит самое частое: не тот экспорт, не та версия ABI,
 * падение в init.
 *
 *   smoke <путь к библиотеке модуля>
 */
#include "bz_module.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
static void* open_lib(const char* path) { return (void*)LoadLibraryA(path); }
static void* find_symbol(void* lib, const char* name) { return (void*)GetProcAddress((HMODULE)lib, name); }
#else
#include <dlfcn.h>
static void* open_lib(const char* path) { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }
static void* find_symbol(void* lib, const char* name) { return dlsym(lib, name); }
#endif

static int logs = 0, ticks_asked = 0;

static const char* fake_version(void) { return "0.2.5"; }
static void fake_log(int32_t level, const char* message) {
    (void)level;
    printf("  журнал: %s\n", message ? message : "(null)");
    ++logs;
}
static int32_t fake_assembly_name(const char** n, const char** d) { (void)n; (void)d; return BZ_ENODATA; }
static int32_t fake_count(int32_t* c) { (void)c; return BZ_ENODATA; }
static int32_t fake_at(int32_t i, BzComponentInfo* o) { (void)i; (void)o; return BZ_ENODATA; }
static int32_t fake_bounds(uint64_t id, double a[3], double b[3]) { (void)id; (void)a; (void)b; return BZ_ENODATA; }
static void fake_request_main(void) { ++ticks_asked; }
static int32_t fake_execute(const char* req, const char** reply) {
    (void)req;
    *reply = "{\"ok\": true, \"result\": []}";
    return BZ_OK;
}
static const char* fake_config_dir(void) { return "/tmp/breezecad"; }

int main(int argc, char** argv) {
    if (argc != 2) { fprintf(stderr, "smoke <библиотека модуля>\n"); return 2; }
    void* lib = open_lib(argv[1]);
    if (!lib) { fprintf(stderr, "ОШИБКА: библиотека не загружается: %s\n", argv[1]); return 1; }

    BzModuleEntryFn entry = (BzModuleEntryFn)find_symbol(lib, BZ_MODULE_ENTRY_NAME);
    if (!entry) { fprintf(stderr, "ОШИБКА: нет экспорта %s\n", BZ_MODULE_ENTRY_NAME); return 1; }
    const BzModuleApi* m = entry();
    if (!m || m->abi_version != BZ_MODULE_ABI_VERSION || !m->init || !m->shutdown) {
        fprintf(stderr, "ОШИБКА: таблица модуля пустая или другой версии ABI\n");
        return 1;
    }

    BzHostApi host;
    memset(&host, 0, sizeof host);
    host.abi_version = BZ_MODULE_ABI_VERSION;
    host.struct_size = sizeof host;
    host.host_version = fake_version;
    host.log = fake_log;
    host.assembly_name = fake_assembly_name;
    host.component_count = fake_count;
    host.component_at = fake_at;
    host.component_bounds = fake_bounds;
    host.request_main = fake_request_main;
    host.execute = fake_execute;
    host.config_dir = fake_config_dir;

    if (m->init(&host) != BZ_OK) { fprintf(stderr, "ОШИБКА: init() отказался запускаться\n"); return 1; }
    if (BZ_HAS(m, main_tick) && ticks_asked) m->main_tick();
    m->shutdown();

    printf("OK: модуль загружается, init/main_tick/shutdown прошли (записей в журнал: %d)\n", logs);
    return 0;
}
