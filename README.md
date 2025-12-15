# BalCom1A Configuration Utility

Кроссплатформенная утилита (Windows / Linux / WSL) для настройки устройства BalCom1A (ESP32‑S3) по UART.
Интерфейс реализован на GTK4 (GtkBuilder/Glade UI), сборка — CMake + pkg-config.

## Возможности

- Автоматический поиск устройства по доступным последовательным портам (probe через команду `FirmVersion`).
- Чтение версии прошивки и проверка корректного ответа устройства.
- Сканирование Wi‑Fi сетей (`scan_Networks` до ответа `complete`) и отображение списка SSID.
- Подключение устройства:
  - Режим AP: команда `connect_AP`
  - Режим STA: команда `connect_sta,SSID,PASSWORD`
- Получение IP адреса (polling `get_ip`) и определение режима по IP:
  - `192.168.4.1` → AP
  - иначе → STA
- Автоподстановка сохранённого пароля для известных SSID.
- Если пароль не подошёл — пароль для этого SSID забывается (чтобы не «залипать» на неверном).

## Файлы и данные

### UI файл

UI загружается из `glade.ui` рядом с исполняемым файлом.
CMake автоматически копирует `ui/glade.ui` в папку бинарника как `glade.ui` на этапе сборки.

### Хранение паролей известных сетей

Пароли сохраняются в INI-файл:

- Linux: `~/.config/BalCom1A_Configuration_Utility/known_networks.ini`
- Windows: `%APPDATA%`-эквивалент через `g_get_user_config_dir()` (GLib)

Также поддерживается «portable» режим: если `known_networks.ini` лежит рядом с `.exe`, он будет использован для загрузки.
Для совместимости со старыми сборками при загрузке также проверяется legacy-путь `.../GTK_Test/known_networks.ini`.

## Запуск

### Обычный режим

Приложение сначала синхронно ищет устройство. Если устройство не найдено — показывает диалог и завершается.

### Режим отладки

Флаг `--debug` разрешает запуск UI без подключенного устройства (для работы над интерфейсом):

```bash
BalCom1A\ Configuration\ Utility --debug
```

На Windows доступны два бинарника:

- **GUI**: `BalCom1A Configuration Utility.exe` (не открывает консоль при двойном клике)
- **Console**: `BalCom1A Configuration Utility Console.exe` (удобно для просмотра stdout/stderr)

## Сборка

## Зависимости

Ниже перечислены зависимости, необходимые для сборки и запуска.

### Общие (для всех платформ)

- CMake >= 3.20
- Компилятор C с поддержкой C11
- pkg-config (pkgconf)
- GTK4 development files + runtime
- GLib 2.0 (обычно ставится вместе с GTK4)

### Windows (MSYS2 MinGW64)

Минимальный набор пакетов MSYS2 (mingw64):

```bash
pacman -S --needed \
  mingw-w64-x86_64-toolchain \
  mingw-w64-x86_64-cmake \
  mingw-w64-x86_64-pkgconf \
  mingw-w64-x86_64-gtk4
```

### Linux / WSL (Ubuntu/Debian)

Минимальный набор пакетов:

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake ninja-build pkg-config \
  libgtk-4-dev
```

### Дополнительно (опционально)

- WSL + реальный USB-UART: может потребоваться usbipd-win на Windows-хосте для проброса USB в WSL.

## Сборка

### Требования

См. раздел «Зависимости» выше.

Проект содержит готовые пресеты CMake: [CMakePresets.json](CMakePresets.json).

---

### Windows (MSYS2 MinGW64)

1) Установите MSYS2 и зависимости (см. раздел «Зависимости»).

2) Конфигурация и сборка через пресеты:

```powershell
cmake --preset win-release
cmake --build --preset win-release
```

3) Запуск:

- Через VS Code (CMake Tools), либо
- Через `cmake --build ... --target run` (добавляет MSYS2 DLL в `PATH`):

```powershell
cmake --build build/win-release --target run
```

---

### Linux / WSL (Ubuntu)

1) Установите зависимости (см. раздел «Зависимости»).

2) Конфигурация и сборка:

```bash
cmake --preset wsl-release
cmake --build --preset wsl-release
```

3) Запуск:

```bash
./build/wsl-release/"BalCom1A Configuration Utility" --debug
```

Примечание: для работы с реальным USB-UART устройством из WSL может потребоваться проброс USB (например, через usbipd-win).

## Типичные проблемы

- **Не найден `glade.ui`**: файл должен лежать рядом с `.exe`/бинарником. При сборке он копируется автоматически; проверьте, что вы запускаете бинарник из папки сборки, где лежит `glade.ui`.
- **Serial port busy / access denied (Windows)**: порт занят другим приложением (serial monitor, PuTTY, Arduino Serial Monitor и т.п.). Закройте его и перезапустите утилиту.
- **Не хватает GTK DLL на Windows**: запускайте через target `run` или убедитесь, что `C:/msys64/mingw64/bin` в `PATH`.
