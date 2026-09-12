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

Плагин собирается против **OBS 32.2.2 + Qt 6.11.1** (obs-deps `2026-07-15`) — ровно та версия,
что стоит на эфирной машине. libobs сравнивает major.minor и отвергает модули, собранные новее
рантайма, поэтому **минимальная версия OBS — 32.2**. На 30.x и 31.x плагин не загрузится; если
понадобится поддержка более старых OBS, нужно понижать пин `obs-studio` в `buildspec.json`
(и тогда шаблон потребует правки: его аргумент `-A x64,version=...` исходники OBS 30.2.3 не
понимают).

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

Полноценная локальная сборка на macOS требует **полного Xcode 16+**: и шаблон плагина, и исходники
OBS принудительно требуют генератор Xcode (`cmake/macos/compilerconfig.cmake`), Command Line Tools
недостаточно. Сборки под macOS в CI отключены — продакшен-платформа одна, Windows.

Без Xcode доступна быстрая проверка синтаксиса: она компилирует исходники с `-fsyntax-only`
против зафиксированных в `buildspec.json` заголовков OBS и фреймворков Qt.

```sh
build-aux/syntax-check.sh
```

Форматирование — как в OBS (`clang-format-19` и `gersemi` из `obsproject/tools`):

```sh
brew install obsproject/tools/clang-format@19 obsproject/tools/gersemi
zsh build-aux/run-clang-format
zsh build-aux/run-gersemi
```

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
