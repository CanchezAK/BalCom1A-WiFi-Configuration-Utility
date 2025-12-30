# Сборка из исходников

Этот документ предназначен для тех, кто собирает утилиту из исходников или хочет собрать установщики.

## Зависимости

### Общие

- CMake >= 3.20
- Компилятор C с поддержкой C11
- pkg-config (pkgconf)
- GTK4 development files + runtime
- GLib 2.0

### Linux / WSL (Ubuntu/Debian)

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake ninja-build pkg-config \
  libgtk-4-dev \
  fakeroot dpkg-dev
```

### Windows (MSYS2 MinGW64)

Минимальный набор пакетов MSYS2 (mingw64):

```bash
pacman -S --needed \
  mingw-w64-x86_64-toolchain \
  mingw-w64-x86_64-cmake \
  mingw-w64-x86_64-pkgconf \
  mingw-w64-x86_64-gtk4
```

## CMake presets

Проект содержит готовые пресеты CMake: `CMakePresets.json`.

### Linux: сборка приложения (Release)

```bash
cmake --preset linux-release
cmake --build --preset linux-release
```

### Linux: сборка `.deb` (CPack)

Обычный `.deb` (включая бандл `esptool`, если он лежит в `packaging/flasher/esptool`):

```bash
cmake --preset linux-release-deb
cmake --build --preset linux-deb
```

Actions-like `.deb` (как в GitHub Actions): пакет **не бандлит** `esptool`, а **скачивает официальный архив** при установке через `postinst` (установка не ломается при офлайне):

```bash
cmake --preset linux-release-deb-actions
cmake --build --preset linux-deb-actions
```

Результат: файл `.deb` появляется в папке сборки выбранного пресета, например:
- `build/linux-release-deb-actions/*.deb`

### Windows: сборка приложения (Release)

```powershell
cmake --preset win-release
cmake --build --preset win-release
```

### Windows: сборка установщика (.exe) через CPack + NSIS

1) Установите NSIS (нужен `makensis` в `PATH`).

2) Соберите проект (Release):

```powershell
cmake --preset win-release
cmake --build --preset win-release
```

3) Соберите установщик:

```powershell
cmake --build --preset win-installer
```

### Windows: Actions-like сборка установщика (как в GitHub Actions)

Чтобы локально получить установщик с тем же поведением, что и в CI (installer + скачивание `esptool.exe` во время установки), используйте пресеты:

```powershell
cmake --preset win-release-installer-actions
cmake --build --preset win-installer-actions
```

Опционально: вспомогательный скрипт (готовит `esptool.exe` из официального zip и печатает путь к installer):

```powershell
./scripts/build-win-installer-actions-like.ps1
```

## Примечания

- Если собирать `.deb` на более новой Ubuntu, он может потянуть версии библиотек, которых нет в Ubuntu 22.04. Для релиза лучше собирать пакет на Ubuntu 22.04.
