# Air Quality Monitoring Tools

Small C tools for monitoring indoor air quality and comparing it with outdoor air quality.

- `pms`: reads data from a local PMS sensor (home/indoor).
- `airly`: fetches outdoor measurements from Airly API.
- both write data to MySQL/MariaDB.

## Requirements

- C compiler (`cc`/`clang`/`gcc`)
- `libcurl`
- `libcjson`
- MySQL/MariaDB client library (`mariadb` via `pkg-config`, or `mysql_config`)

Example (MacPorts):

```bash
sudo port install curl libcjson mariadb pkgconfig
```

## Config

Create `src/config.h` from the template:

```bash
cp src/config.h.example src/config.h
```

Fill database credentials, Airly token, coordinates, and device values.
Table structure for stored measurements is in `src/schema.sql`.

## Build

```bash
make        # builds both tools
make airly  # builds only airly
make pms    # builds only pms
```

Binaries are generated in `bin/`.
