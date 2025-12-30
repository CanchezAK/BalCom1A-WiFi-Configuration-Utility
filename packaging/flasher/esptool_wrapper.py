"""Minimal wrapper used in CI to build a standalone `esptool` binary via PyInstaller.

This file is not used at runtime by the app.

We intentionally avoid importing internal symbols like `esptool.__main__.main`,
because esptool's internal API can change between versions.
Instead, run esptool the same way users do: `python -m esptool`.
"""

import runpy
import sys


def _main() -> None:
    # Make argv[0] look like the real tool name in help/errors.
    if sys.argv:
        sys.argv[0] = "esptool"
    # Equivalent to: python -m esptool
    runpy.run_module("esptool.__main__", run_name="__main__")


if __name__ == "__main__":
    _main()
