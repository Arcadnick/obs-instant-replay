# Instant Replay for OBS

Мгновенный повтор («повтор гола») для OBS Studio в духе панели Replay в vMix: оператор жмёт одну
кнопку, в эфир уходят последние секунды программного микса — при необходимости замедленно, — после
чего эфир автоматически возвращается на живую картинку.

Статус: **M0 — каркас плагина и док-панель**. Кольцевой буфер и воспроизведение ещё не подключены.

## Что уже есть

- модуль плагина с регистрацией дока через `obs_frontend_add_dock_by_id`;
- панель оператора: MARK, длина повтора и сдвиг назад, скорости 25/50/75/100 %, список событий,
  PLAY TO PROGRAM / STOP, тумблер авто-возврата, индикатор буфера;
- локализация en-US / ru-RU;
- сборка под Windows и macOS через GitHub Actions из obs-plugintemplate.

## Совместимость

Плагин собирается против **OBS 30.2.3 + Qt 6.6.3** (obs-deps `2024-05-08`). libobs отвергает модули,
собранные новее рантайма, поэтому сборка под 30.2.3 грузится и в OBS 30.2, и в 31.x, и в 32.x.
Поднимать версию SDK в `buildspec.json` можно только осознанно — это поднимет и минимальную версию
OBS у пользователей.

## Сборка

### Windows (продакшен)

Собирается в GitHub Actions (`windows-2022`, Visual Studio 17 2022). Артефакт — zip вида
`instant-replay-for-obs-<version>-windows-x64.zip`.

Установка: распаковать содержимое архива в `C:\ProgramData\obs-studio\plugins\`, чтобы получилось

```
C:\ProgramData\obs-studio\plugins\instant-replay-for-obs\bin\64bit\instant-replay-for-obs.dll
C:\ProgramData\obs-studio\plugins\instant-replay-for-obs\data\locale\en-US.ini
```

Путь `C:\Program Files\obs-studio\obs-plugins\64bit` устарел и использоваться не должен.

### macOS (разработка)

Требуются **CMake ≥ 3.30** и **полный Xcode 16+**: и шаблон плагина, и исходники OBS 30.2.3 на
macOS принудительно требуют генератор Xcode (`cmake/macos/compilerconfig.cmake`), Command Line Tools
недостаточно.

```sh
brew install cmake
xcode-select --switch /Applications/Xcode.app     # после установки Xcode

cmake --preset macos                              # качает зависимости в ./.deps и собирает libobs
cmake --build --preset macos

rsync -a --delete build_macos/RelWithDebInfo/instant-replay-for-obs.plugin \
      ~/Library/"Application Support"/obs-studio/plugins/
```

Первый `cmake --preset macos` скачивает obs-deps, Qt6 и исходники OBS 30.2.3 и собирает из них
libobs и obs-frontend-api — это занимает заметное время, дальше кэшируется в `.deps`.

Отладчик к релизному OBS.app не подключается (hardened runtime без `get-task-allow`) — рабочий
инструмент диагностики это `obs_log()` и Help → Log Files → View Current Log. Запуск
`/Applications/OBS.app/Contents/MacOS/OBS --safe-mode` отключает сторонние плагины и помогает
отделить свой сбой от чужого.

## Дорожная карта

| Веха | Содержание |
|---|---|
| M0 | каркас плагина, док-панель, CI ✅ |
| M1 | кольцевой буфер программного микса (`obs_add_raw_video_callback2`) |
| M2 | источник воспроизведения с покадровой выборкой и замедлением |
| M3 | сцена повтора, авто-возврат в эфир, горячие клавиши |
| M4 | таймлайн с точками IN/OUT, список событий, индикаторы буфера и RAM |
| M5 | поставка под Windows, канареечная сборка против свежего OBS, эфирная полировка |

## Лицензия

GPL-2.0-or-later, см. [LICENSE](LICENSE).
