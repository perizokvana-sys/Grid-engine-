# GridEngine

GridEngine — небольшой C++20 2D-движок: числовой язык Grid, сценовый граф,
редактор на SDL3 + OpenGL, тайлмапы, партиклы, нативный экспорт Windows x64 и
интеграция поведений на C++ и C# поверх собственного языка.

```
┌──────────────────────────────────────────────────────────────────┐
│  gridengine_editor   один exe: меню + редактор (SDL3 + ImGui + GL)│
│  gridengine_game     игровой runtime (SDL3 + OpenGL, headless ok)│
│  gridengine_demo     CLI: проекты, экспорт                       │
└──────────────────────────────────────────────────────────────────┘
```

## Сборка

Исходники SDL3 и Dear ImGui уже лежат в `dependencies/`, скачивать их не
нужно; бинарники CMake в репозитории не хранятся (см. ниже).

```powershell
$cmake = ".\dependencies\cmake\cmake-4.4.4-windows-x86_64\bin\cmake.exe"
$ctest = ".\dependencies\cmake\cmake-4.4.4-windows-x86_64\bin\ctest.exe"
& $cmake -S . -B build -G "Visual Studio 18 2026" -A x64
& $cmake --build build --config Debug --parallel
& $ctest --test-dir build -C Debug --output-on-failure   # 16 тестов
```

Бинарники инструментов в репозитории не хранятся, поэтому в свежем клоне
`dependencies\cmake\...\bin` пуст: используйте системный CMake ≥ 3.20
(и `ctest` из него) — движок, редактор и экспорт сами берут `cmake` из `PATH`,
если в `dependencies\cmake` его нет.

Опции сборки: `-DGRIDENGINE_BUILD_TESTS=OFF`, `-DGRIDENGINE_BUILD_EDITOR=OFF`.
Для экспорта исполняемого файла нужен установленный MSVC (x64) — он же
собирает игровой runtime внутри пакета. Тесты C# требуют dotnet SDK, тесты
текстур и звука требуют Windows (WIC) и SDL3.

## Что внутри

| Компонент | Файлы | Статус |
|---|---|---|
| Движок/конфиг | `src/engine.cpp` | рабочий, `version() == 1.0.0` |
| Проекты `.grid` | `src/project.cpp` | рабочий |
| Сцены (`GridScene` 1–6) | `src/scene.cpp` | рабочий: 4 — скрипты, 5 — размеры спрайтов, 6 — аудио-источники |
| Дерево сцены | `src/node.cpp` | переписан под тесты, рабочий |
| Партиклы, тайлмапы | `src/particles.cpp`, `src/tilemap.cpp` | рабочий |
| Аудио | `src/audio.cpp`, `src/audio_device.cpp` | рабочее воспроизведение WAV через SDL3 (стерео, gain, loop) |
| Текстуры | `src/texture.cpp`, `src/texture_gpu.cpp` | рабочие: PNG (WIC) и BMP, загрузка в OpenGL |
| Редактор (модель) | `src/editor.cpp` | рабочий: иерархия, компоненты, play/stop, консоль |
| Редактор (интерфейс) | `src/tools/editor_frontend.cpp` | полноценное окно: Hierarchy, Scene, Resources, Console, Inspector |
| Язык Grid | `src/grid.cpp` | рабочий, см. `docs/grid-language-v0.9.md` |
| Экспорт | `src/export.cpp`, `src/tools/game_runtime.cpp` | Windows x64, реальная сборка exe |
| C++ модули | `src/module.cpp`, `include/gridengine/module.hpp` | рабочий, см. `docs/script-integration-v0.9.md` |
| C# мост | `src/csharp.cpp` | рабочий, требует dotnet SDK |
| Меню (OpenGL) | `src/main_menu.cpp` | рабочее, тёмно-бирюзовая тема с мятным акцентом |

## Главное меню

`gridengine_editor` — это меню движка: знак из `docs/brand-reference.jpg` —
тонкий белый контур «G» с контуром контрформы, инженерная сетка в верхней
левой части кольца, плотная тёмно-бирюзовая стойка и стрелка-перекладина,
плюс тихое мятное свечение слева; ниже четыре действия (**New Project**,
**Open Project**, **Import**, **Settings**), тихая статусная строка и ссылки
Window / Scripts / Help. Весь
интерфейс рисуется через OpenGL: сцена Dear ImGui (бэкенд OpenGL2) поверх
очистки буфера. Палитра — тёмный сланец с единственным мятным акцентом
(`#3ECF8E`): белое только у текста и иконок.

«New Project» создаёт проект и сразу открывает его в редакторе — в том же
процессе: `gridengine_editor` передаёт выбранный проект внутрь исполняемого
файла и запускает интерфейс редактора без нового окна процесса. «Open
Project» и «Import» делают то же для существующего проекта. Прямой запуск
редактора без меню: `gridengine_editor --project <файл.grid>`.

Раскладка полностью производная от размера окна: при ширине меньше 900px
карточки выстраиваются в столбец, все элементы остаются внутри полей, обрезки
и переполнения нет. Проверено вручную на 1180×760.

## Окно редактора

Редактор проекта — второй интерфейс того же `gridengine_editor`: строка меню
(бренд, File…Help, кнопки окна), панель инструментов (new/open/save,
vsync), вкладка сцены и панель «Scene Editor» над вьюпортом. В полосе «Scene
Editor» — инструменты select / paint / erase, Stop, зум и переключатель сетки;
запуск и пауза вынесены на F5 / F6 (F5 — play/stop, F6 — пауза/продолжить),
кнопки запуска в полосе нет.

Меню `Window` управляет окном (minimize / maximize / restore / close) и
флоатами (Frames, каталог нод), меню `Help` открывает документацию из `docs/`
и профиль автора в Discord (`https://discord.com/users/1502361530950353178`,
ссылка есть и в меню `Scripts`, и в футере лаунчера — кнопки
Window / Scripts / Help). Пять рабочих зон:

| Зона | Что делает |
|---|---|
| Hierarchy | дерево сцены, поиск, выбор, отдельная кнопка «+» (каталог нод), Add / Duplicate / Delete, глаз видимости на каждой строке |
| Scene view | чистая OpenGL-сцена: сетка и оси, тайлмап, спрайты, габариты коллайдеров, гизмо осей XYZ в правом верхнем углу; инструменты select / paint / erase, зум, переключатель сетки |
| Resources | список `assets/`: выбор и клик по сущности привязывают спрайт или WAV |
| Inspector | вкладки Inspector / Resources: имя, трансформ, секции компонентов (sprite / collider / motion / camera / audio / скрипт Grid) с кнопкой удаления, кнопка «Add Component» добавляет недостающие компоненты |
| Console | вкладки Output / Errors / Warnings / Log и командная строка (`help`, `save`, `play`, `stop`, `add`, `delete`, `clear`, `select <имя>`); запуск также F5 / F6 |

Play прогоняет сценарий и мэшене c реальным звуком, Stop возвращает отредактированное состояние.

## Быстрая сборка одним кликом

`build.cmd` в корне репозитория собирает Windows x64 Release: он находит CMake
(сначала `dependencies/cmake/.../bin`, потом `PATH`) и печатает пути к готовым
exe. Достаточно двойного клика по файлу; нужен установленный Visual Studio
(MSVC x64). После сборки запускайте редактор из корня репозитория:
`build\Release\gridengine_editor.exe --project <файл.grid>`.

## Сборка exe без Visual Studio (CI)

Шаблон воркфлоу лежит в `docs/windows-ci.yml`: он собирает Windows x64 на
`windows-latest` (MSVC + системный CMake) и публикует `gridengine_editor.exe`,
`gridengine_demo.exe` и `gridengine_game.exe` как artifact. Файл нельзя
закоммитить под `Cline`-токеном (нужно право `workflows`), поэтому включите его
одной командой:

```powershell
mkdir .github\workflows
cp docs/windows-ci.yml .github/workflows/windows-build.yml
git add .github/workflows/windows-build.yml && git commit -m "Add Windows CI" && git push
```

Дальше push в `main` (или кнопка Run workflow на вкладке Actions) собирает exe,
файлы забираются из artifact «gridengine-windows-x64».

## Экспорт Windows x64

```powershell
.\build\Debug\gridengine_demo.exe --new .\MyGame MyGame
.\build\Debug\gridengine_demo.exe --export .\MyGame\MyGame.grid .\dist\MyGame --build
.\dist\MyGame\bin\GridEngineGame.exe          # запуск собранной игры
```

Экспорт копирует `assets/` и `scenes/`, пишет `build.gridcfg`, генерирует
игровой runtime, собирает его через CMake + MSVC (x64) и кладёт бинарник в
`bin/`. Подробности и ограничения — в `docs/export-v0.9.md`.

## Поведения: Grid, C++, C#

Grid отвечает за простые числовые правила на сущностях. Всё, что требует
доступа к движку, сложных типов или файловой системы, пишется как нативный
C++-модуль (`samples/modules/spinner`, кладётся в `modules/` пакета) либо как
C#-поведение через процесс-мост. См. `docs/script-integration-v0.9.md`.

## Тесты

| Тест | Что проверяет |
|---|---|
| `gridengine_smoke` | движок, проекты, сцены, редактор, тайлмапы |
| `gridengine_audio` | валидация/сериализация аудио-источников |
| `gridengine_particles` | спавн, время жизни, лимиты эмиттера |
| `gridengine_grid` | язык Grid и привязку к сущностям |
| `gridengine_export` | упаковка пакета и правила безопасности |
| `gridengine_tilemap` | сетки тайлов, сериализация, границы |
| `gridengine_node` | дерево сцены: трансформы, репарент, сериализация |
| `gridengine_export_build` | полный цикл: сборка игрового exe и его headless-прогон |
| `gridengine_module` | компиляция и загрузка нативного C++ модуля |
| `gridengine_csharp` | сборка и прогон C# моста |
| `gridengine_texture` | декодирование PNG (WIC) и BMP, граничные случаи |
| `gridengine_audio_device` | реальное воспроизведение WAV: очереди, gain, пауза, loop |
| `gridengine_scene_audio` | аудио-источники в `GridScene 6` и обратная совместимость |
| `gridengine_editor` | состояния редактора: компоненты, play/stop, консоль |

## Известные ограничения v1.0

- Экспорт компилирует игру только под тулчейн хоста (Windows x64 / MSVC);
  кросс-компиляция и Linux пока не реализованы.
- Упаковка в архив (zip/7z) недоступна: импорт работает только с распакованными
  папками, для архивов выводится честное сообщение.
- Редактор хранит все сущности в `scenes/main.scene` (одна сцена на проект);
  несколько сцен и drag-and-drop оформления — следующие итерации.

## Лицензия

Проект распространяется под лицензией MIT: файл [LICENSE](LICENSE).
Copyright (c) 2026 perizokvana-sys.

Каждый файл в `src/`, `include/` и `tests/` содержит строку об
авторских правах в шапке.
