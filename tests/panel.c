/* Проверка sdk/bz_panel.h без BreezeCAD: собранный JSON окна совпадает с
 * ожидаемым (ADR-0011), спецсимволы экранируются, переполнение буфера даёт
 * NULL, а не обрезанный JSON. */
#include "bz_panel.h"

#include <stdio.h>
#include <string.h>

static int fails = 0;

static void expect(const char* label, const char* got, const char* want) {
    if (got && want && strcmp(got, want) == 0) return;
    printf("ОШИБКА %s:\n  получено: %s\n  ожидалось: %s\n", label, got ? got : "(NULL)", want ? want : "(NULL)");
    ++fails;
}

int main(void) {
    char    buf[2048];
    BzPanel p;

    bz_panel_begin(&p, buf, sizeof buf, "conn.send", "Изменения с устройства", "BreezCONN");
    bz_panel_text(&p, "Что оставить?", 1);
    const char* options[] = {"С устройства", "Своё", "Оба"};
    bz_panel_choice(&p, "c1", "Вал.bzc", "/tmp/Вал.bzc", options, 3, 2);
    bz_panel_status(&p, "Жду изменения…", 1);
    bz_panel_buttons(&p);
    bz_panel_button(&p, "cancel", "Отмена", "secondary", "Esc");
    bz_panel_button(&p, "apply", "Применить", "primary", NULL);
    expect("окно", bz_panel_end(&p),
           "{\"id\": \"conn.send\", \"title\": \"Изменения с устройства\", \"badge\": \"BreezCONN\", \"items\": ["
           "{\"type\": \"text\", \"text\": \"Что оставить?\", \"secondary\": true}, "
           "{\"type\": \"choice\", \"id\": \"c1\", \"label\": \"Вал.bzc\", \"preview\": \"/tmp/Вал.bzc\", "
           "\"options\": [\"С устройства\", \"Своё\", \"Оба\"], \"value\": 2}, "
           "{\"type\": \"status\", \"text\": \"Жду изменения…\", \"busy\": true}, "
           "{\"type\": \"buttons\", \"items\": [{\"id\": \"cancel\", \"label\": \"Отмена\", \"style\": \"secondary\", \"key\": \"Esc\"}, "
           "{\"id\": \"apply\", \"label\": \"Применить\", \"style\": \"primary\"}]}]}");

    /* Элемент после ряда кнопок и QR-матрица. */
    bz_panel_begin(&p, buf, sizeof buf, "q", NULL, NULL);
    const char* rows[] = {"101", "010"};
    bz_panel_bitmap(&p, rows, 2, 240);
    bz_panel_mono(&p, "1.2.3.4:5");
    expect("bitmap", bz_panel_end(&p),
           "{\"id\": \"q\", \"items\": [{\"type\": \"bitmap\", \"size\": 240, \"modules\": [\"101\", \"010\"]}, "
           "{\"type\": \"mono\", \"text\": \"1.2.3.4:5\"}]}");

    /* Кавычки, обратная косая, перевод строки — экранируются. */
    bz_panel_begin(&p, buf, sizeof buf, "e", NULL, NULL);
    bz_panel_text(&p, "a \"b\" \\ c\nd", 0);
    expect("экранирование", bz_panel_end(&p),
           "{\"id\": \"e\", \"items\": [{\"type\": \"text\", \"text\": \"a \\\"b\\\" \\\\ c\\nd\"}]}");

    /* Мало места — NULL, не обрезанный JSON. */
    char    tiny[24];
    BzPanel t;
    bz_panel_begin(&t, tiny, sizeof tiny, "long.panel.id", "Заголовок", NULL);
    bz_panel_text(&t, "текст", 0);
    expect("переполнение", bz_panel_end(&t) ? "не NULL" : "NULL", "NULL");

    char a[256];
    expect("пункт меню", bz_action_json(a, sizeof a, "conn.send", "Продолжить на устройстве", NULL, "export", "file", "Главная", "BreezCONN"),
           "{\"id\": \"conn.send\", \"label\": \"Продолжить на устройстве\", \"icon\": \"export\", \"menu\": \"file\", "
           "\"ribbon\": {\"tab\": \"Главная\", \"group\": \"BreezCONN\"}}");

    if (fails) return 1;
    printf("OK: bz_panel.h собирает окна и пункты меню как в ADR-0011\n");
    return 0;
}
