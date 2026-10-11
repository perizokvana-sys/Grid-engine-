# GridEngine v0.9 — Export & Build System

Экспортёр формирует переносимый пакет проекта из `.grid`-дескриптора, `assets/`,
`scenes/` и `build.gridcfg`. Путь назначения должен быть новым и располагаться вне
папки исходного проекта: существующие каталоги никогда не перезаписываются.
Символические ссылки и нестандартные файловые записи внутри экспортируемых
деревьев запрещены.

## Windows x64: настоящий исполняемый файл

Начиная с v0.9 экспорт умеет компилировать игру. В `BuildSettings` появился флаг
`build_executable`; при его включении в пакет добавляется каталог `runtime/` с
`game_main.cpp` и `CMakeLists.txt`, пакет собирается локальным тулчейном
(CMake + MSVC x64), а готовый бинарник кладётся в `bin/GridEngineGame.exe`.
Манифест получает `binary_included=true`.

Тулчейн передаётся через `ExportToolchain` (cmake, заголовки движка,
`gridengine.lib`, заголовки SDL3, `SDL3-static.lib`, источник игрового runtime).
Поведение воспроизводится тестом `gridengine_export_build`: он собирает пакет,
запускает собранный .exe в headless-режиме и проверяет, что Grid-скрипт сцены
реально сдвинул сущность.

```cpp
gridengine::BuildSettings settings;
settings.type = gridengine::BuildType::release;
settings.build_executable = true;          // компилировать игру
gridengine::ExportToolchain toolchain;      // см. tests/export_build.cpp
auto result = gridengine::export_project("MyGame/MyGame.grid", "dist/MyGame", settings, &toolchain);
```

Из командной строки:

```
gridengine_demo --export MyGame/MyGame.grid dist/MyGame --build [--release]
```

## Пакет

```
dist/MyGame/
  MyGame.grid           дескриптор проекта
  build.gridcfg         настройки сборки (окно, VSync, FPS, binary_included)
  assets/ scenes/       данные проекта
  runtime/              игровой runtime (источник и CMakeLists)
  runtime-build/        артефакты сборки (лог в runtime-build.log)
  bin/GridEngineGame.exe
```

Игровой runtime читает `build.gridcfg` рядом с исполняемым файлом, открывает
проект и первую сцену из `scenes/`, затем каждый кадр выполняет нативные
C++-модули из `modules/`, Grid-скрипты сущностей и интегрирует движение.
Флаги `--project <file.grid>` и `--frames N` позволяют проверить сборку без
окна (`SDL_VIDEODRIVER=offscreen`).

## Ограничения текущего среза

- Кросс-компиляция под Linux не реализована: собирается только платформа
  текущего тулчейна (Windows x64 с MSVC).
- Упаковка в архив (zip/7z) в меню не поддерживается: импорт unpacked-папок
  работает, для архивов выводится честное сообщение.
- Иконка приложения берётся по умолчанию; кастомная иконка — будущая итерация.
