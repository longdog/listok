# Биоиндикация листьев

`leaf-core` 1.0.0 считает геометрию одного изолированного листа: контур, центральную и вторичные жилки, ключевые точки a1–g2, признаки M1–M5, относительную асимметрию, балл от 1 до 5 и оценку качества. Рядом лежат CLI `leaf-analyzer` и стабильный C ABI версии 1.

Библиотека не открывает файлы. На вход нужен уже декодированный буфер `Gray8`, `RGB8` или `RGBA8`. `ImageView` буфер не забирает: указатель на пиксели должен жить до возврата из `analyze` и после возврата не сохраняется.

В библиотеке нет машинного обучения, мобильных сборок и разбора ориентации EXIF. «EXIF orientation is not applied; inputs must be canonically oriented.»

Подробности CLI: [docs/leaf-analyzer.md](docs/leaf-analyzer.md).

## Требования

Linux x86_64.

- CMake 3.24 или новее
- GCC 11.2 или новее, либо Clang 14 или новее
- системный OpenCV 4.5.5 или новее, только мажор 4; библиотеке нужны `core` и `imgproc`, CLI дополнительно нужен `imgcodecs`
- nlohmann/json 3.11.2 или новее, только мажор 3
- GoogleTest 1.14.0 или новее, только если собираются тесты

`LEAF_FETCH_DEPS` по умолчанию `OFF`: CMake не ходит в сеть. `ON` разрешает скачать nlohmann/json и GoogleTest, если пакетов нет в системе. OpenCV при любом значении опции остаётся системной зависимостью.

## Сборка

Опции CMake:

| Опция | По умолчанию | Назначение |
| --- | --- | --- |
| `LEAF_BUILD_TESTS` | `OFF` | тесты |
| `LEAF_BUILD_CLI` | `OFF` | `leaf-analyzer` |
| `LEAF_BUILD_BENCHMARKS` | `OFF` | `leaf-pipeline-benchmark` |
| `LEAF_ENABLE_DEBUG` | `OFF` | цель `leaf-core-debug` и оверлеи CLI |
| `LEAF_FETCH_DEPS` | `OFF` | сетевая загрузка json и GoogleTest |
| `LEAF_ENABLE_SANITIZERS` | `OFF` | ASan и UBSan для GCC или Clang |

Библиотека, тесты и CLI:

```bash
cmake -S . -B build \
  -DLEAF_BUILD_TESTS=ON \
  -DLEAF_BUILD_CLI=ON \
  -DLEAF_ENABLE_DEBUG=OFF \
  -DLEAF_FETCH_DEPS=OFF \
  -DLEAF_ENABLE_SANITIZERS=OFF
cmake --build build -j2
```

С оверлеями отладки та же команда, но `-DLEAF_ENABLE_DEBUG=ON`. Без этого флага цели `leaf-core-debug` нет.

Санитайзеры, Debug:

```bash
cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug \
  -DLEAF_BUILD_TESTS=ON -DLEAF_BUILD_CLI=ON \
  -DLEAF_ENABLE_DEBUG=ON -DLEAF_ENABLE_SANITIZERS=ON
cmake --build build-san -j2
```

Флаги санитайзеров: `-fsanitize=address,undefined -fno-omit-frame-pointer`.

Публичные заголовки: `include/leaf/*.h` и `include/leaf/c/leaf.h`. Они не подключают OpenCV.

## Тесты

```bash
ctest --test-dir build --output-on-failure
```

Метки, которые регистрирует сборка с тестами:

- `unit` — модульные тесты, включая допуск золотого сравнения
- `integration` — полный разбор фикстуры `leaf_cross`
- `determinism` — сто одинаковых JSON подряд
- `c_abi` — потребитель на C11

Метка `cli` появляется только при `LEAF_BUILD_CLI=ON`. Метки `golden` нет: сравнение `leaf_v1` с разметкой не доходит до конца, см. раздел ниже.

```bash
ctest --test-dir build -L unit --output-on-failure
ctest --test-dir build -L integration --output-on-failure
ctest --test-dir build -L determinism --output-on-failure
ctest --test-dir build -L c_abi --output-on-failure
ctest --test-dir build -L cli --output-on-failure
```

Допуски размеченной фикстуры: ключевая точка не дальше 5 px, длина не больше 5 %, угол не больше 3°, асимметрия не больше 0,005. Пороги заданы в `tests/support/golden_compare.cpp`.

## Командная строка

`leaf-analyzer` декодирует JPEG и PNG через OpenCV `imgcodecs`. Библиотека по-прежнему видит только буфер.

Один файл:

```text
leaf-analyzer --input <image> --output <result.json> [--config <config.json>] [--debug <dir>]
```

Каталог, в лексикографическом порядке путей. Ошибочный файл не останавливает остальные. Код выхода процесса — наибольший из кодов файлов.

```text
leaf-analyzer --input-dir <dir> --output-dir <dir> [--config <config.json>] [--debug-dir <dir>]
```

Берутся только `.jpg`, `.jpeg` и `.png`. Расширение выбирает файл, байты всё равно должны декодироваться. BGR превращается в RGB, BGRA в RGBA.

«EXIF orientation is not applied; inputs must be canonically oriented.»

JSON-конфиг накладывается на значения по умолчанию. Неизвестное поле и неверный тип отвергаются. Пример рабочего конфига для синтетического креста: `tests/fixtures/cli/leaf_cross.config.json`. Конфиг по умолчанию этот крест до измерений не доводит.

```bash
leaf-analyzer --input tests/fixtures/cli/leaf_cross.png \
  --output /tmp/cross.json \
  --config tests/fixtures/cli/leaf_cross.config.json
```

Коды выхода:

| Код | Смысл |
| --- | --- |
| 0 | Успех, в том числе полный результат с `quality.acceptable == false` |
| 2 | Смешанные, неизвестные или пустые опции, либо `--debug` в сборке без отладки |
| 3 | Файл не прочитан или не декодирован |
| 4 | Ошибка конфига или анализа. Версионный JSON ошибки записан |
| 5 | JSON или отладочный файл не удалось записать |

Выход пишется во временный файл рядом с целью, сбрасывается на диск и переименовывается на место.

`--debug` и `--debug-dir` при `LEAF_ENABLE_DEBUG=OFF` отклоняются на разборе аргументов. Сообщение: `debug support is unavailable in this build`. Каталоги отладки создаются только если флаг передан. Оверлеи рисуют контур, центральную жилку, оси, точки a1–g2 и M1–M5 и в расчёт не возвращаются. Имена файлов перечислены в [docs/leaf-analyzer.md](docs/leaf-analyzer.md).

## C++ API

```cpp
#include <leaf/analyzer.h>

auto created = leaf::Analyzer::create();
if (!created.hasValue()) {
  const leaf::Error* error = created.error();
  return;
}
const leaf::Analyzer& analyzer = **created.value();

leaf::ImageView view{pixels, width, height, width * 3, leaf::PixelFormat::RGB8};
auto result = analyzer.analyze(view);
if (!result.hasValue()) {
  const leaf::Error* error = result.error();
  return;
}
```

`Analyzer::create` принимает `AnalyzerConfig`. Пустой аргумент — конфиг по умолчанию. `analyzeBatch` принимает `std::span<const ImageView>` и возвращает `BatchResult`. Ошибка отдельного кадра лежит в элементе пакета.

Форматы `ImageView`: `PixelFormat::Gray8`, `RGB8`, `RGBA8`. `stride` задаётся в байтах. При ошибке значения нет: `error()->code`, `error()->stage`, `error()->message`. Успешный разбор может иметь `quality.acceptable == false`: геометрия посчитана, порог качества не пройден.

Свой JSON конфига: `leaf::parseAnalyzerConfigJson`. Результат в JSON: `leaf::analysisResultToJson`.

## C ABI

Заголовок `include/leaf/c/leaf.h`, версия `LEAF_C_ABI_VERSION` равна 1. Её же возвращает `leaf_c_abi_version`.

```c
#include <leaf/c/leaf.h>

leaf_analyzer_t* analyzer = NULL;
char* error_json = NULL;
if (leaf_analyzer_create(leaf_c_abi_version(), NULL, &analyzer, &error_json) != LEAF_STATUS_OK) {
  leaf_string_free(error_json);
  return;
}

char* result_json = NULL;
leaf_status_t status = leaf_analyzer_analyze(
    analyzer, pixels, width, height, stride, LEAF_PIXEL_RGB8, &result_json);
leaf_string_free(result_json);
leaf_analyzer_destroy(analyzer);
```

`config_json == NULL` означает конфиг по умолчанию. Каждую строку, которую вернули `leaf_analyzer_create` и `leaf_analyzer_analyze`, освобождает вызывающий через `leaf_string_free`. `leaf_string_free(NULL)` ничего не делает. `leaf_analyzer_destroy` строки не освобождает. `leaf_analyzer_destroy(NULL)` тоже ничего не делает. Один и тот же анализатор нельзя вызывать параллельно и нельзя уничтожать одновременно с `analyze`. Разные анализаторы можно.

## Бенчмарк

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS_RELEASE='-O3 -DNDEBUG' \
  -DLEAF_BUILD_BENCHMARKS=ON -DLEAF_BUILD_TESTS=OFF -DLEAF_BUILD_CLI=OFF \
  -DLEAF_ENABLE_DEBUG=OFF -DLEAF_ENABLE_SANITIZERS=OFF
cmake --build build-release --target leaf-pipeline-benchmark -j2
./build-release/benchmarks/leaf-pipeline-benchmark \
  --warmup 5 --iterations 30 \
  --input tests/fixtures/benchmark/leaf_12mp.rgb
```

Файл `tests/fixtures/benchmark/leaf_12mp.rgb` — сырой кадр 4000×3000, `RGB8`. Это прямоугольник. При конфиге по умолчанию центральная жилка на нём не находится, поэтому интервалы скелета и измерений в отчёте остаются нулевыми. Суммарное время всё равно измеряется: в него входят предобработка и поиск контура. Прогон информационный, порог 500 мс он не проверяет.

## Фикстура leaf_v1

`tests/fixtures/synthetic/leaf_v1.ppm` — эллипс с жилками по разметке `leaf_v1.ground-truth.json`. `analyze()` с конфигом по умолчанию доходит до вторичных жилок и возвращает ошибку: стадия `SecondaryVeins`, код `SecondaryVeinNotFound`, сообщение `secondary vein not found`.

Концы первых вторичных жилок в разметке лежат внутри контура дальше порога `endpointContourSnapPx` (8 px): c1 на 18,89 px, c2 на 14,34 px. Любая точка в 5 px от c1 отстоит от контура минимум на 13,89 px, от c2 минимум на 9,34 px. Порог привязки и допуск ключевой точки для этих точек вместе не выполняются. `tests/expected/1.0/leaf_v1.json` остаётся снимком сериализатора для модульного теста.

Найденная центральная жилка считает основанием более широкий конец. У этого эллипса верхний конец шире, поэтому основание попадает в (119, 54), на 216 px от размеченного (120, 270).

Крест из `tests/fixtures/cli/` с его JSON-конфигом пайплайн проходит.
