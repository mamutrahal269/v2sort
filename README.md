## v2sort - прокси чекер на базе xray-core

![Go](https://img.shields.io/badge/Go-1.27.0-00ADD8?logo=go&logoColor=white)

![xray-core](https://img.shields.io/badge/xray--core-v26.7.28-2E64FE?logo=github&logoColor=white)

## 🚀 Возможности
Поддерживает **VLESS, VMess, Shadowsocks, Trojan, HTTP(S), SOCKS5 и Hysteria2**. Нативная работа с xray-core, не требует ни внешних зависимостей, ни портов, ни процессов и никаких прочих костылей. Определение гео через cloudflare cdn-cgi/trace, спидтест, множественное get тестирование, формирование имен ключей через шаблоны, гибкая конфигурация. Три режима работы: **dedup, validate, check**. Можно встраивать в скрипты, крон и тд.  

## 🎛 Режимы работы

| Команда    | Что делает                                      |
| ---------- | ----------------------------------------------- |
| `dedup`    | Удаляет дубликаты по структуре конфига xray     |
| `validate` | Проверяет, что ядро запускается с каждым ключом |
| `check`    | dedup + validate + тестирование                 |

## 🚩 Флаги `check`

| Флаг                   | Описание                         | По умолчанию       |
| ---------------------- | -------------------------------- | ------------------ |
| `-c, --config`         | Путь к TOML-конфигу (обязателен) | —                  |
| `-j, --jobs`           | Параллельных воркеров            | `runtime.NumCPU()` |
| `-t, --timeout`        | Таймаут GET-запроса              | `5s`               |
| `-n, --min-successful` | Минимум успешных URL             | `1`                |
| `-r, --random`         | Один случайный URL вместо всех   | `false`            |
| `-g, --geo`            | Определение гео                  | `false`            |
| `-S, --speedtest`      | Замер скорости                   | `false`            |
| `--speedtest-timeout`  | Таймаут спидтеста                | `5s`               |
| `--fragment-fmt`       | Шаблон имени ключа               | —                  |
| `-o, --output`         | Файл для рабочих ключей          | `stdout`           |
| `-x, --trash`          | Файл для мусорных ключей         | `null`             |
| `--fetch-timeout`      | Таймаут загрузки подписок        | `5s`               |

## 🏷 Шаблоны имён ключей

| Плейсхолдер   | Значение                             |
| ------------- | ------------------------------------ |
| `%url%`       | Исходный URL                         |
| `%ipv4%`      | IPv4-адрес (или `N/A`)               |
| `%ipv6%`      | IPv6-адрес (или `N/A`)               |
| `%country%`   | Двухбуквенный код страны (или `N/A`) |
| `%speed%`     | Скорость, байт/с                     |
| `%speed_kib%` | Скорость, KiB/s                      |
| `%speed_mib%` | Скорость, MiB/s                      |
| `%avg_time%`  | Среднее время отклика                |

## 📋 Примеры

```bash
# Дедупликация подписки
v2sort dedup https://example.com/sub -o clean.txt -x trash.txt

# Валидация
v2sort validate sub.txt -o valid.txt -x trash.txt

# Полный чек: 50 потоков, geo, speedtest, шаблонные имена
v2sort check sub.txt -c v2sort.toml -j 50 -t 8s -n 2 -g -S --fragment-fmt "[%country%] %avg_time%" -o result.txt -x dead.txt


v2sort check sub.txt -c v2sort.toml -g -o prxies.txt

v2sort check sub.txt -c v2sort.toml -r -t 5s -o ok.txt
```

## 🔍 Фильтры

- **Протоколы** - оставить/исключить `vless`, `vmess`, `ss`, `trojan`, `http`, `socks5`, `hysteria2`
- **Страны** - фильтрация по geo-данным Cloudflare (требует `-g`)
- **CIDR** - фильтрация по подсетям IPv4/IPv6 (требует `-g`)

## 📄 Лицензия

GPLv3