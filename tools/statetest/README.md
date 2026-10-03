# State frontend regression

- `python3 tools/statetest/check_roots.py` checks the production root
  enumeration/preferences, existing HDD query and UI mappings against module
  and mount fixtures. It does not perform PS2 device I/O.
- `python3 tools/statetest/check_payloads.py` compiles the production bank
  reader/writer and actual miniz with ASan/UBSan. It uses temporary files and
  tests legacy fixed SNES and variable Mesen payloads, raw/deflate, CRC,
  size/core/ROM/slot validation, failed allocation and incomplete banks.
- `bash tools/mesencetest/build.sh` separately verifies the actual Mesen
  console serialization and rollback. The bank fixture mocks core buffers.

These checks do not prove that every SNES coprocessor is serialized; the
legacy SNES payload still needs a versioned extension for missing fields.
