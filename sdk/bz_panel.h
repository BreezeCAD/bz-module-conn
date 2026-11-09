/* bz_panel.h — пункты меню и окна модуля (ADR-0011, ABI 2).
 *
 * ПРЕДВАРИТЕЛЬНО: интерфейс предложен в BreezeCAD ADR-0011 и ждёт решения
 * автора программы. Пока в sdk/bz_module.h BZ_MODULE_ABI_VERSION == 1,
 * программа окон модулей не показывает; пример в src/module.c их и не
 * вызывает (#if BZ_MODULE_ABI_VERSION >= 2).
 *
 * Окно модуль не рисует сам: он описывает его в JSON, программа рисует своими
 * виджетами и темой (host->show_panel), а нажатия приходят в on_panel_event.
 * Этот файл собирает такой JSON без библиотек — на C, в буфер вызывающего:
 *
 *   char buf[4096];
 *   BzPanel p;
 *   bz_panel_begin(&p, buf, sizeof buf, "bom.result", "Спецификация", "Мой модуль");
 *   bz_panel_text(&p, "Найдено позиций: 12", 1);
 *   bz_panel_buttons(&p);
 *   bz_panel_button(&p, "save", "Сохранить", "primary", NULL);
 *   bz_panel_button(&p, "close", "Закрыть", "secondary", "Esc");
 *   const char* json = bz_panel_end(&p);   // NULL — не поместилось в buf
 *
 * Типы элементов — ADR-0011: text, mono, note, status, link, bitmap (QR и
 * подобное: строки из '0'/'1'), image (PNG в base64), choice (сегменты,
 * с миниатюрой файла), buttons. Тексты — уже переведённые, UTF-8.
 *
 * MIT No Attribution, как весь шаблон.
 */
#ifndef BZ_PANEL_H
#define BZ_PANEL_H

#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct BzPanel {
    char*  buf;
    size_t cap, len;
    int    items;     /* элементов в текущем списке (для запятых) */
    int    buttons;   /* 1 — открыт ряд кнопок */
    int    ok;        /* 0 — буфер переполнен */
} BzPanel;

static inline void bz_panel__raw(BzPanel* p, const char* s) {
    const size_t n = strlen(s);
    if (!p->ok || p->len + n + 1 > p->cap) { p->ok = 0; return; }
    memcpy(p->buf + p->len, s, n + 1);
    p->len += n;
}

/* Строка JSON в кавычках, с экранированием. */
static inline void bz_panel__str(BzPanel* p, const char* s) {
    char one[8];
    bz_panel__raw(p, "\"");
    for (; s && *s; ++s) {
        const unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { one[0] = '\\'; one[1] = (char)c; one[2] = 0; }
        else if (c == '\n') strcpy(one, "\\n");
        else if (c < 0x20) snprintf(one, sizeof one, "\\u%04x", c);
        else { one[0] = (char)c; one[1] = 0; }
        bz_panel__raw(p, one);
    }
    bz_panel__raw(p, "\"");
}

/* "key": "value" (value NULL — пропуск). */
static inline void bz_panel__kv(BzPanel* p, const char* key, const char* value) {
    if (!value) return;
    bz_panel__raw(p, ", ");
    bz_panel__str(p, key);
    bz_panel__raw(p, ": ");
    bz_panel__str(p, value);
}

static inline void bz_panel__item(BzPanel* p, const char* type) {
    if (p->items++) bz_panel__raw(p, ", ");
    bz_panel__raw(p, "{\"type\": ");
    bz_panel__str(p, type);
}

static inline void bz_panel__close_buttons(BzPanel* p) {
    if (!p->buttons) return;
    bz_panel__raw(p, "]}");
    p->buttons = 0;
    p->items   = 1;   /* ряд кнопок — тоже элемент окна */
}

/* badge — имя модуля в заголовке («чьё окно»), можно NULL. */
static inline void bz_panel_begin(BzPanel* p, char* buf, size_t cap, const char* id, const char* title, const char* badge) {
    p->buf = buf; p->cap = cap; p->len = 0; p->items = 0; p->buttons = 0; p->ok = cap > 0;
    if (cap) buf[0] = 0;
    bz_panel__raw(p, "{\"id\": ");
    bz_panel__str(p, id);
    bz_panel__kv(p, "title", title);
    bz_panel__kv(p, "badge", badge);
    bz_panel__raw(p, ", \"items\": [");
}

/* secondary — серым, как пояснение. */
static inline void bz_panel_text(BzPanel* p, const char* text, int secondary) {
    bz_panel__close_buttons(p);
    bz_panel__item(p, "text");
    bz_panel__kv(p, "text", text);
    if (secondary) bz_panel__raw(p, ", \"secondary\": true");
    bz_panel__raw(p, "}");
}

/* Моноширинный: адрес, код, путь. */
static inline void bz_panel_mono(BzPanel* p, const char* text) {
    bz_panel__close_buttons(p);
    bz_panel__item(p, "mono");
    bz_panel__kv(p, "text", text);
    bz_panel__raw(p, "}");
}

/* Подсказка со значком из набора программы ("shield", "info", "warning"). */
static inline void bz_panel_note(BzPanel* p, const char* icon, const char* text) {
    bz_panel__close_buttons(p);
    bz_panel__item(p, "note");
    bz_panel__kv(p, "icon", icon);
    bz_panel__kv(p, "text", text);
    bz_panel__raw(p, "}");
}

/* Плашка статуса; busy — с точкой-индикатором («Жду изменения…»). */
static inline void bz_panel_status(BzPanel* p, const char* text, int busy) {
    bz_panel__close_buttons(p);
    bz_panel__item(p, "status");
    bz_panel__kv(p, "text", text);
    if (busy) bz_panel__raw(p, ", \"busy\": true");
    bz_panel__raw(p, "}");
}

/* Ссылка-кнопка: нажатие — событие с этим id. */
static inline void bz_panel_link(BzPanel* p, const char* id, const char* label) {
    bz_panel__close_buttons(p);
    bz_panel__item(p, "link");
    bz_panel__kv(p, "id", id);
    bz_panel__kv(p, "label", label);
    bz_panel__raw(p, "}");
}

/* Чёрно-белая картинка из квадратов (QR-код): rows строк по '0'/'1',
 * size — сторона на экране, точки интерфейса. Белое поле вокруг рисует программа. */
static inline void bz_panel_bitmap(BzPanel* p, const char* const* rows, int count, int size) {
    char n[32];
    bz_panel__close_buttons(p);
    bz_panel__item(p, "bitmap");
    snprintf(n, sizeof n, ", \"size\": %d", size);
    bz_panel__raw(p, n);
    bz_panel__raw(p, ", \"modules\": [");
    for (int i = 0; i < count; ++i) {
        if (i) bz_panel__raw(p, ", ");
        bz_panel__str(p, rows[i]);
    }
    bz_panel__raw(p, "]}");
}

/* Картинка модуля: PNG в base64 (иллюстрация), по центру. */
static inline void bz_panel_image(BzPanel* p, const char* png_base64, int width) {
    char n[32];
    bz_panel__close_buttons(p);
    bz_panel__item(p, "image");
    snprintf(n, sizeof n, ", \"width\": %d", width);
    bz_panel__raw(p, n);
    bz_panel__kv(p, "png", png_base64);
    bz_panel__raw(p, "}");
}

/* Выбор сегментами («С устройства | Своё | Оба»). preview — путь к .bzc/.bza:
 * слева миниатюра файла (можно NULL). value — номер выбранного. */
static inline void bz_panel_choice(BzPanel* p, const char* id, const char* label, const char* preview,
                            const char* const* options, int count, int value) {
    char n[32];
    bz_panel__close_buttons(p);
    bz_panel__item(p, "choice");
    bz_panel__kv(p, "id", id);
    bz_panel__kv(p, "label", label);
    bz_panel__kv(p, "preview", preview);
    bz_panel__raw(p, ", \"options\": [");
    for (int i = 0; i < count; ++i) {
        if (i) bz_panel__raw(p, ", ");
        bz_panel__str(p, options[i]);
    }
    snprintf(n, sizeof n, "], \"value\": %d}", value);
    bz_panel__raw(p, n);
}

/* Ряд кнопок внизу окна; дальше — bz_panel_button(). */
static inline void bz_panel_buttons(BzPanel* p) {
    bz_panel__close_buttons(p);
    bz_panel__item(p, "buttons");
    bz_panel__raw(p, ", \"items\": [");
    p->buttons = 1;
    p->items   = 0;
}

/* style: "primary" | "secondary"; key — подпись клавиши ("Esc"), можно NULL. */
static inline void bz_panel_button(BzPanel* p, const char* id, const char* label, const char* style, const char* key) {
    if (p->items++) bz_panel__raw(p, ", ");
    bz_panel__raw(p, "{\"id\": ");
    bz_panel__str(p, id);
    bz_panel__kv(p, "label", label);
    bz_panel__kv(p, "style", style);
    bz_panel__kv(p, "key", key);
    bz_panel__raw(p, "}");
}

/* JSON окна или NULL, если буфер мал. */
static inline const char* bz_panel_end(BzPanel* p) {
    bz_panel__close_buttons(p);
    bz_panel__raw(p, "]}");
    return p->ok ? p->buf : NULL;
}

/* JSON пункта меню для host->add_action(). menu: "file" (и поиск команд);
 * ribbon_tab / ribbon_group — кнопка на ленте, можно NULL. */
static inline const char* bz_action_json(char* buf, size_t cap, const char* id, const char* label, const char* tooltip,
                                  const char* icon, const char* menu, const char* ribbon_tab, const char* ribbon_group) {
    BzPanel p;
    p.buf = buf; p.cap = cap; p.len = 0; p.items = 0; p.buttons = 0; p.ok = cap > 0;
    if (cap) buf[0] = 0;
    bz_panel__raw(&p, "{\"id\": ");
    bz_panel__str(&p, id);
    bz_panel__kv(&p, "label", label);
    bz_panel__kv(&p, "tooltip", tooltip);
    bz_panel__kv(&p, "icon", icon);
    bz_panel__kv(&p, "menu", menu);
    if (ribbon_tab) {
        bz_panel__raw(&p, ", \"ribbon\": {\"tab\": ");
        bz_panel__str(&p, ribbon_tab);
        bz_panel__kv(&p, "group", ribbon_group);
        bz_panel__raw(&p, "}");
    }
    bz_panel__raw(&p, "}");
    return p.ok ? p.buf : NULL;
}

#endif /* BZ_PANEL_H */
