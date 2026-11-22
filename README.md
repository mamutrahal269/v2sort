# v2sort

[![CI](https://github.com/mamutrahal269/v2sort/actions/workflows/ci.yml/badge.svg)](https://github.com/mamutrahal269/v2sort/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/mamutrahal269/v2sort)](https://github.com/mamutrahal269/v2sort/releases)
[![Go version](https://img.shields.io/github/go-mod/go-version/mamutrahal269/v2sort?logo=go)](go.mod)
[![License: GPLv3](https://img.shields.io/badge/license-GPLv3-blue.svg)](LICENSE)

Чекер и сортировщик прокси-ключей на базе [xray-core](https://github.com/XTLS/Xray-core).

xray-core используется как библиотека, а не как внешний бинарник: весь трафик идёт через ядро внутри процесса по пайпам в памяти. Никаких подпроцессов, открытых портов и временных конфигов на диске.

Протоколы: **VLESS, VMess, Shadowsocks, Trojan, HTTP(S), SOCKS5, Hysteria2** (`hy2`).

## Возможности

- Три режима: `dedup` — дедупликация, `validate` — проверка запуска ядра, `check` — полное тестирование.
- Дедупликация по структуре конфига xray, а не по строке: ключи с разным URL, но одинаковыми параметрами считаются дубликатами.
- GET-тесты через прокси: ключ рабочий, если тестовые URL отвечают HTTP 2xx.
- Гео-определение (IPv4, IPv6, страна) через Cloudflare `cdn-cgi/trace`.
- Спидтест и фильтр по минимальной скорости.
- Фильтры по протоколам, странам и подсетям IPv4/IPv6 (CIDR).
- Переименование ключей по шаблону (страна, пинг, скорость и т.д.).
- Источники — локальные файлы и ссылки на подписки; base64 распознаётся автоматически.
- Вывод списком или JSON; мёртвые ключи складываются в отдельный файл с причиной.
- Один статический бинарник без зависимостей: Linux, Windows, macOS, Android.

## Установка

### Готовый бинарник

Скачай под свою платформу со [страницы релизов](https://github.com/mamutrahal269/v2sort/releases): Linux (amd64, arm64, armv7, mips/mipsle/mips64/mips64le), Windows (amd64), macOS (amd64, arm64), Android (arm64).

```bash
curl -L -o v2sort https://github.com/mamutrahal269/v2sort/releases/latest/download/v2sort-linux-amd64
chmod +x v2sort
```

### go install

```bash
go install github.com/mamutrahal269/v2sort@latest
```

### Из исходников

```bash
git clone https://github.com/mamutrahal269/v2sort.git
cd v2sort
go build -o v2sort .
```

Требуется Go версии из [go.mod](go.mod). Версии v2sort, xray-core и рантайма Go: `v2sort --version`.

## Быстрый старт

Минимальный конфиг `v2sort.toml`:

```toml
[settings]
urls = ["https://www.gstatic.com/generate_204"]
```

Проверить подписку:

```bash
v2sort check https://example.com/sub -c v2sort.toml -o ok.txt -x dead.txt
```

`ok.txt` — рабочие ключи, `dead.txt` — остальные, с причиной отвала.

## Режимы работы

| Команда    | Что делает                                                     |
| ---------- | -------------------------------------------------------------- |
| `dedup`    | Парсит ключи и удаляет дубликаты                               |
| `validate` | Проверяет, что ядро собирает конфиг и запускается с каждым ключом |
| `check`    | `dedup` + `validate` + GET-тесты, гео, спидтест, фильтры       |

Все команды принимают один или несколько источников: пути к файлам или URL подписок (источник считается ссылкой, если содержит `://`). Содержимое в base64 декодируется автоматически, ключи вытаскиваются из текста по шаблону `схема://...` — посторонний мусор в файле игнорируется.

### `dedup`

Ключи парсятся в outbound-конфиги xray и сравниваются по сериализованной структуре (без тега). Поэтому дубликатом считается не совпадающая строка, а совпадающий набор параметров: адрес, порт, uuid, flow, transport, tls и т.д.

### `validate`

Для каждого ключа собирается конфиг из одного outbound'а, создаётся и запускается инстанс ядра. Ключ валиден, если ядро приняло конфиг и стартовало. Сетевые запросы не выполняются, дедупликация не производится.

### `check`

Полный пайплайн: загрузка → фильтр по протоколам → парсинг → дедупликация → валидация → тестирование. Все валидные ключи загружаются в один инстанс xray, дальше `-j` воркеров параллельно гоняют GET-запросы через каждый outbound. Прогресс со счётчиком `(OK:n FAIL:n)` рисуется в stderr, stdout остаётся чистым для пайпов.

Логика теста:

- Без `-r`: идут запросы по списку `settings.urls` из конфига. Ключ рабочий, если минимум `-n` URL ответили кодом 2xx (по умолчанию `1`; если `-n` больше числа URL — приравнивается к нему).
- С `-r`: берётся один случайный URL из списка, нужен один ответ 2xx. Несовместим с `-n`.
- С `-g`: дополнительно определяются IPv4/страна (через `https://1.1.1.1/cdn-cgi/trace`) и IPv6 (через `https://[2606:4700:4700::1111]/cdn-cgi/trace`).
- С `-S`: замер скорости — GET на `settings.speedtest_url`, скорость = байты тела / время запроса.

После тестов применяются фильтры по странам и CIDR (только с `-g`), фильтр по скорости (только с `-S`), затем переименование по `--fragment-fmt`.

## Конфигурация

Конфиг обязателен только для `check`. В `dedup` и `validate` он опционален и используется лишь ради фильтра по протоколам.

Полный пример (см. также [v2sort_example.toml](v2sort_example.toml)):

```toml
[settings]
# URL для GET-тестов. Обязательно для check.
urls = [
  "https://www.gstatic.com/generate_204",
  "https://cloudflare.com/cdn-cgi/trace",
]
# URL файла для спидтеста. Обязательно при флаге -S.
speedtest_url = "https://speed.cloudflare.com/__down?bytes=10485760"

# Для всех фильтров одна логика:
#   allow = true  — белый список: оставить только перечисленное в list
#   allow = false — чёрный список: выкинуть перечисленное в list
# Пустой list — фильтр отключён.

[settings.protocols]
# Схемы URL: vless, vmess, ss, trojan, http, https, socks5, hysteria2.
# Работает во всех трёх командах.
allow = false
list = ["http", "https"]

[settings.countries]
# Двухбуквенные коды стран из гео Cloudflare. Работает только с -g.
allow = false
list = ["KP", "CN"]

[settings.cidrs]
# Подсети IPv4/IPv6, проверяются по адресу из гео. Работает только с -g.
allow = true
list = ["0.0.0.0/0", "::/0"]
```

## Флаги

### Общие (все команды)

| Флаг              | Описание                                  | По умолчанию |
| ----------------- | ----------------------------------------- | ------------ |
| `-c, --config`    | Путь к TOML-конфигу (обязателен для `check`) | —            |
| `--fetch-timeout` | Таймаут загрузки подписок по сети         | `5s`         |
| `-o, --output`    | Файл для результата                       | `stdout`     |
| `-x, --trash`     | Файл для мёртвых ключей                   | `/dev/null`  |

### Флаги `check`

| Флаг                  | Описание                                                   | По умолчанию       |
| --------------------- | ---------------------------------------------------------- | ------------------ |
| `-j, --jobs`          | Параллельных воркеров                                      | число ядер CPU     |
| `-t, --timeout`       | Таймаут одного GET-запроса                                 | `5s`               |
| `-n, --min-successful` | Минимум успешных URL из `settings.urls`                   | `1`                |
| `-r, --random`        | Один случайный URL вместо всего списка                     | `false`            |
| `-g, --geo`           | Собирать гео (IPv4, IPv6, страна)                          | `false`            |
| `-S, --speedtest`     | Замерять скорость по `settings.speedtest_url`              | `false`            |
| `--speedtest-timeout` | Таймаут спидтеста                                          | `5s`               |
| `--min-speed`         | Минимальная скорость, работает только с `-S`               | `0` (выкл)         |
| `--fragment-fmt`      | Шаблон имени ключа                                         | —                  |
| `--json`              | JSON-отчёт вместо списка URL                               | `false`            |

Единицы `--min-speed`: число без суффикса — Mbps; также `Kbps`/`Mbps`/`Gbps` (биты), `KB`/`MB`/`GB` (десятичные), `KiB`/`MiB`/`GiB` (двоичные), опциональный `/s` в конце, регистр не важен. Примеры: `10`, `10Mbps`, `2MiB/s`, `500KB`.

## Шаблоны имён ключей

`--fragment-fmt` переименовывает прошедшие ключи. Для VMess подменяется поле `ps`, для остальных протоколов — фрагмент после `#` (заменяется или дописывается).

| Плейсхолдер   | Значение                                    |
| ------------- | ------------------------------------------- |
| `%url%`       | Исходный URL ключа                          |
| `%ipv4%`      | IPv4-адрес, нужен `-g` (иначе `N/A`)        |
| `%ipv6%`      | IPv6-адрес, нужен `-g` (иначе `N/A`)        |
| `%country%`   | Двухбуквенный код страны, нужен `-g`        |
| `%speed%`     | Скорость в байт/с, нужен `-S` (иначе `0`)   |
| `%speed_kib%` | Скорость в KiB/s, нужен `-S`                |
| `%speed_mib%` | Скорость в MiB/s, нужен `-S`                |
| `%avg_time%`  | Среднее время ответа тестов, напр. `145.3ms`|

## Вывод

- Рабочие ключи — в stdout или `-o`. С `--json` — массив объектов:
  ```json
  [
    {
      "url": "vless://...",
      "geo": { "ipv4": "1.2.3.4", "ipv6": "", "country": "NL" },
      "speed": 1234567,
      "avg_time_ms": 231
    }
  ]
  ```
  `speed` — байт/с, `avg_time_ms` — среднее время ответа в миллисекундах. Без `-g`/`-S` соответствующие поля нулевые.
- Мусор — в `-x` (по умолчанию `/dev/null`), по строке на ключ в формате `url : причина`. Туда попадают нераспарсенные URL, ключи, которые не запустились в ядре, и не прошедшие GET-тесты (`no test passed`).
- При фатальной ошибке код возврата — `1`.

## Примеры

```bash
# Убрать дубликаты из подписки
v2sort dedup https://example.com/sub -o clean.txt

# Склеить несколько источников и убрать дубликаты
v2sort dedup sub1.txt sub2.txt https://example.com/sub -o merged.txt -x trash.txt

# Проверить, что ядро принимает каждый ключ
v2sort validate keys.txt -o valid.txt -x invalid.txt

# Базовый чек: 50 воркеров, таймаут 8s
v2sort check sub.txt -c v2sort.toml -j 50 -t 8s -o ok.txt -x dead.txt

# Ключ рабочий, если ответили минимум 2 URL из конфига
v2sort check sub.txt -c v2sort.toml -n 2 -o ok.txt

# Быстрый чек по одному случайному URL + гео + имена со страной и пингом
v2sort check sub.txt -c v2sort.toml -r -g --fragment-fmt "[%country%] %avg_time%" -o ok.txt

# Только быстрые ключи: спидтест + порог 10 Mbps
v2sort check sub.txt -c v2sort.toml -S --min-speed 10Mbps --fragment-fmt "[%country%] %speed_mib% MiB/s" -o fast.txt

# JSON-отчёт для скриптов и крона
v2sort check sub.txt -c v2sort.toml -g -S --json -o report.json
```

## Лицензия

[GPLv3](LICENSE)
