# v2sort

`v2sort` is a C++ CLI utility for validating and ranking proxy links (VLESS/VMess/Trojan/SS and other formats matched by regex). It launches `xray`, measures network metrics through `libcurl`, and prints a compact report for each proxy.

## What the project does

- reads proxy links from a file (`-l`);
- extracts proxies using a built-in or custom regex (`-R`);
- generates a temporary `xray` config;
- creates local SOCKS5 inbounds on sequential ports;
- checks availability/performance against URLs from `v2sort.toml`;
- fetches geo/IP data from `https://ipinfo.io/json`;
- applies protocol/country/IP filters;
- prints final metrics to stdout.

## Platform support and limitations

> Supported target: **Linux with glibc only**.

Why:

- the code relies on Linux/GNU-specific APIs and behavior (`getopt_long`, `unistd.h`, `pwd.h`, `ftruncate`, `/dev/*` paths);
- build/runtime are oriented to GNU toolchain (`g++`, `libstdc++`, `__gnu_cxx::stdio_filebuf`);
- there is no explicit support in code/build system for macOS/Windows/musl.

If you need Alpine/musl or non-Linux platforms, add dedicated compatibility work and tests.

## Dependencies

### Runtime

- `xray` (default path: `/usr/local/bin/xray`, override in config);
- internet access for URL checks and `ipinfo.io` requests.

### Build-time

- `g++` with C++20 support;
- `make`;
- Boost libraries/components: `json`, `url`, `locale`, `system`, `filesystem`, `thread`, `regex`, `date_time`, `log`, `log_setup`, `process`;
- `libcurl`;
- `pthread`;
- `toml++` headers.

### Example dependency install (Debian/Ubuntu)

```bash
sudo apt update
sudo apt install -y \
  build-essential make pkg-config \
  libboost-all-dev \
  libcurl4-openssl-dev
```

Depending on distro/version, you may need to install `toml++` separately or add include paths manually.

## Build

```bash
make
```

Debug build:

```bash
make debug
```

Clean:

```bash
make clean
```

## Configuration

By default, config is loaded from:

- `$HOME/.v2sort` (preferred);
- fallback path in code: `/etc/v2sort/config`.

You can override config path explicitly:

```bash
./v2sort -c /path/to/config.toml -l proxies.txt
```

A sample config is provided in this repository: `v2sort.toml`.

## Quick start

1. Prepare a proxy list file (for example `proxies.txt`).
2. Copy `v2sort.toml` to `~/.v2sort` and edit as needed.
3. Ensure `xray` is installed and reachable (or set its path in config).
4. Run:

```bash
./v2sort -l proxies.txt -j 4 -T 5
```

## CLI options

- `-c, --config FILE` — config file path;
- `-l, --list FILE` — proxy list file (**required**);
- `-j, --jobs NUM` — number of worker threads;
- `-p, --port NUM` — starting local SOCKS5 inbound port;
- `-v, --verbose` — verbose logging;
- `-r, --random` — test one random URL from `settings.urls`;
- `-w, --wait MS` — wait time for `xray` startup in milliseconds;
- `-T, --timeout` — network timeout in seconds;
- `-R, --regex` — custom regex for proxy extraction;
- `-4, --ipv4_only` — force IPv4;
- `-6, --ipv6_only` — force IPv6.

`-4` and `-6` are mutually exclusive.

## Example run

```bash
./v2sort \
  -l proxies.txt \
  -c ./v2sort.toml \
  -j 8 \
  -p 10808 \
  -T 5 \
  -w 3500
```

## Output

For each proxy, the tool prints:

- original proxy URL;
- IP/country (from `ipinfo.io`);
- `HTTP code`;
- timings: `t_dns`, `t_connect`, `t_tls`, `t_ttfb`, `t_total`;
- download speed and transferred size.

## Notes

- The project uses aggressive optimization flags (`-O3`, `-march=native`, LTO), so binaries may be less portable across different CPUs.
- Verify `xray` log/output paths and permissions in your config for stable operation.
