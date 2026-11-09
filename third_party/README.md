# Сторонние библиотеки

Все — в исходниках, собираются вместе с модулем; ничего не скачивается при сборке и работе.

| Папка | Что | Версия | Лицензия | Зачем |
|---|---|---|---|---|
| `monocypher/` | [Monocypher](https://monocypher.org) | 4.0.2 | CC0-1.0 или BSD-2-Clause (`LICENCE.md`) | шифрование передачи (XChaCha20-Poly1305) |
| `qrcodegen/` | [QR Code generator](https://www.nayuki.io/page/qr-code-generator-library), Project Nayuki | как в BreezeCAD | MIT (в шапке файлов) | QR-код со ссылкой для устройства |
| `nlohmann/` | [JSON for Modern C++](https://github.com/nlohmann/json) | 3.11.3 | MIT (`LICENSE.MIT`) | JSON команд и окон программы |
| `doctest/` | [doctest](https://github.com/doctest/doctest) | — | MIT (`LICENSE.txt`) | только тесты |

`monocypher/` и `qrcodegen/` — те же файлы, что в BreezeCAD (`third_party/`): телефон и планшет шифруют и читают QR той же библиотекой.
