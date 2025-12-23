# Flasher bundling

This project can bundle a flashing tool inside the packaged installer (.exe) and Debian package (.deb), so end users don't need to install anything.

## Expected output name

The application will look for the flasher next to the executable:
- Windows: `esptool.exe`
- Linux: `esptool`

## How to provide the flasher

Option A (recommended for releases): build a standalone flasher binary and place it here:
- `packaging/flasher/esptool.exe` (Windows)
- `packaging/flasher/esptool` (Linux)

Option B: point CMake directly to a flasher:
- `-DBALCOM_BUNDLE_FLASH_TOOL=ON`
- `-DBALCOM_FLASH_TOOL_EXECUTABLE=...`

## Licensing

If you bundle a third-party flasher, make sure to comply with its license (include notices and source/links as required).

This repo includes a `THIRD_PARTY_NOTICES.txt` template you can ship with the package.
