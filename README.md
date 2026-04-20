# Air Quality Monitoring Tools

Small C tools for monitoring indoor air quality and comparing it with outdoor air quality.

- `pms`: reads data from a local PMS sensor via UART (home/indoor).
- `pms_bt`: same as `pms`, but connects via BLE (HC-08 bridge).
- `airly`: fetches outdoor measurements from Airly API.
- all write data to MySQL/MariaDB.

## Requirements

- C compiler (`cc`/`clang`/`gcc`)
- `libcurl`
- `libcjson`
- MySQL/MariaDB client library (`mariadb` via `pkg-config`, or `mysql_config`)
- [SimpleBLE](https://github.com/OpenBluetoothToolbox/SimpleBLE) C bindings (`pms_bt` only)

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
make          # builds all tools
make airly    # builds only airly
make pms      # builds only pms
make pms_bt   # builds only pms_bt
```

Binaries are generated in `bin/`.
