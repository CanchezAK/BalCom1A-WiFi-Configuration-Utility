# Minimal wrapper used in CI to build a standalone `esptool` binary via PyInstaller.
#
# This file is not used at runtime by the app.

from esptool.__main__ import main

if __name__ == "__main__":
    main()
