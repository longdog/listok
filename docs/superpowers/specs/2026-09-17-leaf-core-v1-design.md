# leaf-core v1 — нормативный дизайн

Дата: 2026-09-17
Статус: утверждённый дизайн для планирования реализации

## 1. Назначение и приоритет

Этот документ является нормативным дополнением к `docs/leaf-core-spec.md`. Он фиксирует границы v1 и разрешает неоднозначности исходной спецификации. При расхождении для v1 действует этот документ; не затронутые здесь требования исходной спецификации сохраняются.

Ключевые слова «должен», «не должен», «только» и «ошибка» задают обязательные требования.

## 2. Scope v1

В v1 входят:

- Linux x86_64;
- библиотека C++20 с публичным C++ API;
- стабильный C ABI v1;
- CLI `leaf-analyzer`;
- полный детерминированный OpenCV pipeline;
- unit-, integration- и golden-test harness;
- отладочные изображения для desktop;
- инъецируемые интерфейсы детекторов, пригодные для будущих ML-реализаций.

В v1 не входят:

- ONNX Runtime и любые ML-модели;
- Android, iOS, Flutter и mobile builds;
- обучение моделей и сбор размеченного production dataset;
- сеть, сервер, GUI, GPS, карты и облачное хранение;
- Windows как обязательная платформа.

Входной класс v1 — фотография одного изолированного простого листа без перекрытий и самопересечений контура. Центральная жилка должна быть видна непрерывно, а с каждой стороны должны различаться минимум две вторичные жилки. Нарушение этих предусловий не разрешает алгоритму угадывать геометрию: анализ завершается точной ошибкой обязательной стадии либо полным результатом с `quality.acceptable == false`.

## 3. Подход и архитектурные правила

Реализация контрактно-ориентированная и гибридная:

1. сначала фиксируются нормативные contracts, публичные заголовки, версии форматов и C ABI;
2. затем независимо реализуются чистые модули geometry, measurement/statistics, preprocessing, skeleton graph, JSON и test harness;
3. CV-стадии интегрируются последовательно: contour → center vein → secondary veins → keypoints;
4. в конце собираются quality/Analyzer, debug/CLI и C ABI.

Система разделена на шесть слоёв:

1. **domain/contracts** — публичные типы, конфигурация, ошибки, outcome, версии;
2. **image/CV** — валидация пикселей, resize, preprocessing, contour, vein enhancement, thinning;
3. **geometry** — кривые, преобразования координат, skeleton graph, пересечения, касательные;
4. **measurement/statistics** — M1–M5, относительная асимметрия, итоговая асимметрия, score;
5. **validation/orchestration** — межстадийные инварианты, quality и `Analyzer`;
6. **adapters** — JSON, C ABI, debug, CLI и будущие ML-адаптеры.

Зависимости направлены только вниз по этому списку, кроме orchestration, которое связывает стадии через их contracts. `Analyzer` не содержит CV-, геометрических или статистических алгоритмов: он только вызывает стадии, передаёт их результаты, останавливает pipeline при ошибке и агрегирует confidence.

OpenCV разрешён только внутри image/CV и desktop adapters. Geometry, measurement/statistics, domain/contracts и C ABI не зависят от OpenCV.

## 4. Публичные contracts

### 4.1 Политика публичных заголовков

Все стабильные C++ заголовки находятся в `include/leaf/`. Они не должны:

- включать заголовки OpenCV;
- упоминать `cv::Mat`, `cv::Point`, `cv::Rect` или другие OpenCV-типы;
- передавать владение через сырые owning pointers;
- выбрасывать исключения из публичных функций.

`Image` остаётся non-owning view на декодированные пиксели. Пользователь владеет буфером и обязан сохранять его неизменным на время синхронного вызова. Поддерживаются только `Gray8`, `RGB8`, `RGBA8`; порядок RGB/RGBA не является BGR/BGRA. Размер строки и произведения размеров проверяются с защитой от переполнения.

Вместо `cv::Rect` публичные структуры используют `leaf::Rect`; кривые и точки используют `leaf::Point`. Инъецируемые интерфейсы `ILeafDetector` и `IVeinDetector` принимают `leaf::ImageView`/domain contracts и возвращают domain types. Преобразование в `cv::Mat` является внутренней деталью OpenCV-адаптеров.

### 4.2 Outcome и ошибки без исключений

Публичная модель возврата:

```cpp
template<class T>
class Outcome final {
public:
    static Outcome success(T value) noexcept;
    static Outcome failure(Error error) noexcept;

    bool hasValue() const noexcept;
    const T* value() const noexcept;
    T* value() noexcept;
    const Error* error() const noexcept;
};
```

`Outcome<T>` содержит ровно одно из двух: значение или `Error`. Доступ через указатель не бросает исключений. Публичные операции анализа и парсинга конфигурации помечаются `noexcept`. Ошибки выделения памяти, исключения OpenCV и прочие непредвиденные исключения перехватываются на границе `Analyzer` и преобразуются в `InternalError`; исключение не пересекает публичный API.

```cpp
enum class Stage {
    Input, Config, Preprocess, LeafContour, CenterVein,
    CoordinateSystem, Skeleton, SecondaryVeins, Keypoints,
    Measurements, Statistics, Score, Quality, Serialization, Internal
};

struct Error {
    ErrorCode code;
    Stage stage;
    std::string message;
};
```

`message` предназначено для диагностики и не является машинным контрактом. Машинный контракт — `ErrorCode`. Для одной и той же причины при одинаковых input/config/version код должен быть одинаковым.

Нормативный набор `ErrorCode` v1:

- `None`;
- `InvalidImage`, `UnsupportedPixelFormat`;
- `InvalidConfigJson`, `UnknownConfigField`, `InvalidConfigValue`;
- `InsufficientContrast`, `InsufficientLighting`, `PreprocessFailed`;
- `LeafNotFound`, `LeafTooSmall`, `LeafOutsideFrame`, `AmbiguousLeafContour`;
- `CentralVeinNotFound`, `AmbiguousCentralVein`;
- `InvalidCoordinateSystem`;
- `SkeletonizationFailed`;
- `SecondaryVeinNotFound`, `AmbiguousVeins`;
- `InvalidKeypoints`, `InvalidMeasurements`, `ZeroAsymmetryDenominator`;
- `InvalidScoreRanges`;
- `SerializationFailed`;
- `InternalError`.

Каждая обязательная стадия возвращает `Outcome<StageResult>`. Первый failure немедленно завершает одиночный анализ этим же `ErrorCode`; последующие стадии не запускаются, fallback не применяется. `InternalError` нельзя использовать вместо известной предметной причины.

`Analyzer::analyze` возвращает `Outcome<AnalysisResult>`. Успешный outcome означает, что все M1–M5 и score вычислены. Он может иметь `quality.acceptable == false`, если полная геометрия вычислена, но confidence или фотометрические признаки не прошли quality thresholds. Структурная невозможность вычислить обязательный признак всегда является failure, а не частичным результатом.

Причины неприемлемого, но полного результата задаются отдельным стабильным `QualityIssue`, а не `ErrorCode`: `LowLeafConfidence`, `LowCenterVeinConfidence`, `LowSecondaryVeinConfidence`, `LowKeypointConfidence`, `LowMeasurementConfidence`, `MarginalContrast`, `MarginalLighting`. `quality.issues` содержит все применимые причины в этом фиксированном порядке. Успешный result не содержит stage error.

Batch запускает каждый элемент независимо, сохраняет outcome каждого элемента в исходном порядке и продолжает после failures. Среднее вычисляется только по значениям с валидной асимметрией и `quality.acceptable == true`; batch score определяется теми же ranges по `abs(meanAsymmetry)`. Пустое множество успешных приемлемых значений даёт `BatchResult.valid == false` и `score == 0`, без подстановки asymmetry.

### 4.3 Версии результата и debug

Каждый результат, включая JSON error envelope, содержит:

- `resultSchemaVersion = "1.0"`;
- `libraryVersion`;
- `algorithmVersion`;
- `methodologyVersion`;
- `modelVersion = null` для v1;
- `cAbiVersion = 1` в C ABI JSON.

В пределах schema major `1` поля не меняют смысл и не удаляются. Добавление полей требует minor bump. Несовместимое изменение требует major bump.

Debug API является нестабильным desktop-only API в `include/leaf/debug.h` и отдельном target `leaf-core-debug`. Он может быть включён только при `LEAF_ENABLE_DEBUG=ON`, не входит в `leaf-core-c`, не сериализуется в основной result JSON и не влияет на результат анализа. При выключенной сборке CLI с `--debug` завершает разбор аргументов ненулевым кодом и объясняет, что debug недоступен.

## 5. Конфигурация

### 5.1 DetectionConfig

Все величины расстояния ниже относятся к resized image, если явно не указана нормализованная величина. Нормализованные расстояния делятся на arc length центральной жилки.

Нормативные поля и defaults v1:

| Поле | Default | Допустимый диапазон | Назначение |
|---|---:|---:|---|
| `minLeafAreaRatio` | 0.05 | `(0, 1)` | минимальная площадь контура / площадь кадра |
| `maxLeafAreaRatio` | 0.95 | `(0, 1]` | максимальная площадь контура / площадь кадра |
| `frameMarginPx` | 2 | `[0, 64]` | минимальный зазор контура от границы resized image |
| `minContourSolidity` | 0.80 | `[0, 1]` | фильтр кандидатов листа |
| `maxContourCandidates` | 8 | `[1, 64]` | предел детерминированного ранжирования |
| `minCenterVeinLengthRatio` | 0.55 | `(0, 1]` | длина жилки / продольный размер листа |
| `maxCenterVeinGapPx` | 12 | `[0, 128]` | допустимый локальный разрыв до graph build |
| `veinThresholdBlockSize` | 31 | нечётное `[3, 255]` | локальный threshold жилок |
| `veinThresholdC` | 5.0 | `[-64, 64]` | смещение локального threshold |
| `minSkeletonBranchLengthNorm` | 0.04 | `(0, 1]` | pruning коротких ветвей |
| `minSecondaryVeinLengthNorm` | 0.08 | `(0, 1]` | минимальная длина secondary vein |
| `minSecondaryAngleDeg` | 15.0 | `[0, 180]` | допустимый угол ветви |
| `maxSecondaryAngleDeg` | 165.0 | `[0, 180]` | допустимый угол ветви |
| `minAttachmentSeparationNorm` | 0.05 | `(0, 1]` | разделение первой и второй attachment point |
| `endpointContourSnapPx` | 8.0 | `[0, 64]` | максимальный snap endpoint к контуру |
| `branchMergeRadiusPx` | 4.0 | `[0, 32]` | объединение соседних graph nodes |
| `ambiguityScoreDelta` | 0.03 | `[0, 1]` | разница top-2 ниже этого значения неоднозначна |
| `minimumStageConfidence` | 0.50 | `[0, 1]` | порог существования кандидата detector stage |

Обязательные межполевые ограничения:

- `minLeafAreaRatio < maxLeafAreaRatio`;
- `minSecondaryAngleDeg < maxSecondaryAngleDeg`;
- `minSkeletonBranchLengthNorm <= minSecondaryVeinLengthNorm`;
- все числа конечны;
- `enableMlFallback` отсутствует в v1: неизвестное поле является ошибкой, а не неработающим переключателем.

Detector candidate с confidence ниже `minimumStageConfidence` считается отсутствующим и даёт предметный `*NotFound`; при равных/близких top candidates используется `Ambiguous*`. Quality threshold применяется только после структурно успешной стадии и может быть выше, но не ниже `minimumStageConfidence`; его непрохождение создаёт полный неприемлемый result, а не stage failure.

`PreprocessConfig` сохраняет `targetMaxDimension = 2048`, `normalizeIllumination = true`, `blurKernel = 5`, `adaptiveThreshold = false`. `targetMaxDimension` допустим в `[256, 8192]`; `blurKernel` — нечётный в `[1, 31]`. Фотометрические defaults: `minProcessLuminanceStdDev = 8.0`, `minAcceptableLuminanceStdDev = 12.0`, `processMeanLuminanceRange = [10,245]`, `acceptableMeanLuminanceRange = [20,235]` для Gray8 шкалы `0..255`. Значение ниже processing contrast или вне processing mean range даёт соответственно `InsufficientContrast` или `InsufficientLighting`; значение только вне acceptable range даёт полный result с `MarginalContrast`/`MarginalLighting`. Processing thresholds должны быть не строже соответствующих acceptance thresholds.

Quality thresholds для leaf, center vein, skeleton/secondary veins, keypoints и measurements задаются отдельно в `QualityConfig`, имеют default `0.50` и диапазон `[0,1]`. Каждый обязан быть не ниже `minimumStageConfidence`.

Конфигурация immutable внутри созданного `Analyzer`. Невалидная конфигурация отклоняется при создании analyzer; значения не clamp-ятся.

### 5.2 Score ranges

Score применяется к `abs(k)`, где `k` — среднее пяти signed relative asymmetries. Конфигурация содержит ровно пять диапазонов со score строго `1,2,3,4,5`; они упорядочены, непрерывны и полуинтервальны `[min, max)`. Первый `min` обязан быть `0`; `min` каждого следующего диапазона обязан равняться `max` предыдущего; последний диапазон открыт сверху и в JSON имеет `max: null`. Перекрытия, пробелы, не возрастающие границы или иной набор score дают `InvalidScoreRanges`.

Defaults v1:

- score 1: `[0.000, 0.040)`;
- score 2: `[0.040, 0.045)`;
- score 3: `[0.045, 0.050)`;
- score 4: `[0.050, 0.055)`;
- score 5: `[0.055, +∞)`.

Таким образом, `0.040` относится к score 2, `0.045` — к score 3, `0.050` — к score 4, `0.055` — к score 5. Это устраняет пробелы исходных десятичных интервалов и максимально сохраняет их пороги.

### 5.3 JSON policy

JSON-конфигурация кодируется UTF-8 и разбирается строго:

- пустая строка, malformed JSON, trailing non-whitespace и duplicate keys дают `InvalidConfigJson`;
- корень обязан быть object;
- неизвестное поле на любом уровне даёт `UnknownConfigField`;
- неверный JSON type, `null` вместо значения, нецелое значение integer-поля, неконечное или выходящее за диапазон число дают `InvalidConfigValue`;
- отсутствующие поля получают документированные defaults;
- numeric strings и неявные преобразования запрещены.

Result JSON всегда является object и использует `camelCase`, enum names из contracts и JSON `null` для отсутствующих optional values. Сериализатор не выводит NaN/Infinity: их появление считается `SerializationFailed`. Порядок ключей не является API, но реализация использует фиксированный порядок для golden/determinism tests. Result JSON v1 не принимается библиотекой обратно и поэтому не имеет parsing policy.

## 6. Координаты и геометрия

Preprocessor возвращает `ResizeTransform` с исходными и рабочими размерами и точными коэффициентами для каждой оси. Aspect ratio сохраняется; padding не применяется. Если resize не нужен, transform является identity. Любая точка, контур и путь, найденные в рабочем изображении, обязательно отображаются обратно в координаты исходного изображения до формирования публичного результата.

Система координат листа:

- `origin` — base центральной жилки;
- положительная ось Y направлена от base к apex;
- положительная ось X направлена вправо для наблюдателя при движении от base к apex и образует правую ортонормированную пару в плоскости изображения;
- `scale` — arc length центральной жилки от base до apex в координатах исходного изображения;
- normalized point равен проекциям в этот базис, делённым на `scale`.

Для каждого публичного keypoint хранятся `image` и `normalized` coordinates. Пути contour/veins хранятся в image coordinates; нормализованное представление должно быть воспроизводимо через опубликованный `CoordinateSystem`. Round-trip `image → normalized → image` обязан укладываться в `1e-9 * scale` для double-геометрии до дискретизации.

Base и apex выбираются по центральной жилке, не по bounding box. Если направление base→apex нельзя выбрать однозначно, возвращается `AmbiguousCentralVein`.

## 7. Data flow

Нормативный порядок одиночного анализа:

1. validate `Image`;
2. preprocess и сформировать `ResizeTransform`;
3. detect leaf contour;
4. detect center vein, base и apex;
5. build coordinate system;
6. enhance veins, thin и build skeleton graph;
7. detect secondary veins;
8. extract and validate keypoints;
9. calculate M1–M5;
10. calculate five relative asymmetries;
11. calculate leaf asymmetry `k`;
12. calculate score по `abs(k)`;
13. aggregate quality и сформировать versioned result.

Каждая стадия получает только outputs предыдущих стадий и immutable config. Она не читает файлы, не пишет логи сама и не обращается к глобальному mutable state. Debug observer получает копии/представления артефактов после стадии, но не может менять pipeline.

Confidence каждой стадии находится в `[0,1]`; значения вне диапазона являются `InternalError`. `overallConfidence` равен минимуму confidence обязательных успешно завершённых стадий: leaf contour, center vein, coordinate system, skeleton/secondary veins, keypoints и measurements. Он не является средним. `quality.acceptable` истинно только если выполнены структурные предусловия, фотометрические flags истинны и каждая стадия достигает своего quality threshold.

## 8. Морфологическая семантика и измерения

Левая/правая сторона определяется знаком normalized X. Secondary veins каждой стороны сортируются по arc distance attachment point от base по центральной жилке. `first` ближе к base, `second` следующая. Attachment — ближайшая graph-точка соединения ветви с центральной жилкой после детерминированного merge.

Точка `f` — точка центральной жилки на половине её полной arc length, полученная интерполяцией внутри сегмента.

- **M1**: через `f` проводится прямая, параллельная normalized X. Для каждой стороны берётся ближайшее к `f` пересечение с leaf contour на соответствующем луче. M1 — длина от `f` до этого пересечения.
- **M2**: arc length пути второй secondary vein от attachment point до endpoint, отдельно слева и справа.
- **M3**: arc distance по центральной жилке между attachment points первой и второй secondary vein той же стороны.
- **M4**: shortest same-side contour arc distance между endpoints первой и второй secondary vein. Дуги, пересекающие base/apex separator или переходящие на противоположный знак normalized X, не допускаются. Если допустимы две равные с точностью `1e-9 * scale` дуги, геометрия неоднозначна и возвращается `InvalidMeasurements`.
- **M5**: меньший неориентированный угол `0..180°` между локальными касательными центральной и второй secondary vein в branch point. Касательная центральной направлена base→apex и оценивается регрессией по интервалу arc coordinate `[s-w, s+w]`; касательная secondary направлена от branch к endpoint и оценивается по `[0,w]`, где `w = 0.02 * scale`. Интервал обрезается границами пути, но обязан содержать минимум три различные точки и arc span не менее `0.5w`; иначе возвращается `InvalidMeasurements`.

M1–M4 вычисляются как неотрицательные длины и публикуются как normalized values; при необходимости diagnostic image lengths могут быть выведены отдельно, но не заменяют нормативные поля. M5 публикуется в градусах.

Для каждого признака:

`Ai = (Li - Ri) / (Li + Ri)`.

Если `abs(Li + Ri) < minDenominator`, одиночный анализ завершается `ZeroAsymmetryDenominator`. Default `minDenominator = 1e-9`; значение обязано быть конечным и положительным. Итог:

`k = (A1 + A2 + A3 + A4 + A5) / 5`.

Ни один обязательный признак нельзя исключить, заменить нулём или восстановить fallback-алгоритмом.

## 9. CV pipeline и детерминированность

OpenCV pipeline включает resize, channel conversion, illumination normalization, denoise, threshold/mask, contour ranking, vein enhancement и graph extraction. Все thresholds берутся из config. Tie-break для кандидатов фиксирован: больший primary score, затем большая площадь/длина, затем меньшая координата Y, затем X, затем исходный row-major index.

Скелетизация не зависит от `opencv_contrib`. Используется собственная бинарная Zhang–Suen thinning implementation:

- вход нормализуется в значения `{0,1}`;
- пиксели просматриваются row-major;
- удаление выполняется одновременно после каждой из двух подитераций;
- критерии Zhang–Suen применяются без эвристического изменения;
- цикл завершается, когда обе подитерации не удалили пиксели;
- граница за пределами изображения считается фоном.

Graph строится после thinning с фиксированным порядком 8-соседей: N, NE, E, SE, S, SW, W, NW. Pruning и merge используют config и стабильные tie-break rules.

Для воспроизводимости библиотека:

- не использует случайность;
- отключает OpenCL;
- один раз через `std::call_once` при создании первого analyzer отключает OpenCL и устанавливает process-wide OpenCV thread count в 1; это документированный global side effect публичной библиотеки;
- не зависит от порядка обхода unordered containers;
- не включает timestamps и абсолютные пути в result JSON.

Детерминированность гарантируется побитно для JSON и дискретных артефактов на одной поддержанной toolchain/architecture при одинаковых image bytes, config и version fields. Между разными OpenCV patch versions численная эквивалентность проверяется tolerances, но побитная идентичность не обещается.

## 10. JPEG/PNG и файловая ответственность

`leaf-core` и `leaf-core-c` принимают только уже декодированные pixel buffers и не читают файлы. Они не определяют JPEG/PNG, не владеют filesystem и не зависят от OpenCV `imgcodecs`.

CLI отвечает за:

- чтение `.jpg`, `.jpeg` и `.png`;
- декодирование через OpenCV `imgcodecs`;
- преобразование BGR/BGRA в RGB/RGBA до вызова library;
- отклонение неизвестного/повреждённого файла как CLI input error;
- atomic write JSON через временный файл и rename;
- создание debug directories только по явному флагу.

Расширение файла не считается доказательством формата: успешное декодирование обязательно. EXIF orientation v1 не применяется автоматически; CLI сообщает это в документации, а fixtures хранятся уже в канонической ориентации.

## 11. C ABI v1

`leaf-core-c` экспортирует только declarations из `include/leaf/c/leaf.h`. ABI использует fixed-width integers и непрозрачный handle.

```c
#define LEAF_C_ABI_VERSION 1u

typedef enum leaf_status_t {
    LEAF_STATUS_OK = 0,
    LEAF_STATUS_INVALID_ARGUMENT = 1,
    LEAF_STATUS_CONFIG_ERROR = 2,
    LEAF_STATUS_ANALYSIS_ERROR = 3,
    LEAF_STATUS_SERIALIZATION_ERROR = 4,
    LEAF_STATUS_OUT_OF_MEMORY = 5,
    LEAF_STATUS_INTERNAL_ERROR = 6,
    LEAF_STATUS_ABI_MISMATCH = 7
} leaf_status_t;

uint32_t leaf_c_abi_version(void);

leaf_status_t leaf_analyzer_create(
    uint32_t requested_abi_version,
    const char* config_json,
    leaf_analyzer_t** out_analyzer,
    char** out_error_json);

leaf_status_t leaf_analyzer_analyze(
    leaf_analyzer_t* analyzer,
    const uint8_t* data,
    int32_t width,
    int32_t height,
    int32_t stride,
    leaf_pixel_format_t pixel_format,
    char** out_result_json);

void leaf_string_free(char* value);
void leaf_analyzer_destroy(leaf_analyzer_t* analyzer);
```

Нормативное поведение:

- `leaf_c_abi_version()` всегда возвращает ABI, с которым собрана библиотека;
- несовпадающий `requested_abi_version` даёт `LEAF_STATUS_ABI_MISMATCH`;
- `config_json == NULL` означает defaults; пустая строка является invalid JSON;
- `out_analyzer == NULL` или `out_result_json == NULL` даёт `LEAF_STATUS_INVALID_ARGUMENT`;
- перед дальнейшей работой функция устанавливает предоставленный output pointer в `NULL`;
- `out_error_json` в create опционален; если он предоставлен, он также сначала устанавливается в `NULL`;
- null analyzer/data, неположительные dimensions, invalid stride/format и arithmetic overflow дают `LEAF_STATUS_INVALID_ARGUMENT`;
- analysis domain failure даёт `LEAF_STATUS_ANALYSIS_ERROR` и по возможности versioned error JSON в `out_result_json`;
- успешный анализ, включая `quality.acceptable == false`, даёт `LEAF_STATUS_OK` и полный result JSON;
- строки выделяются библиотекой единым внутренним allocator и освобождаются только `leaf_string_free`;
- `leaf_string_free(NULL)` и `leaf_analyzer_destroy(NULL)` безопасны и ничего не делают;
- после destroy handle недействителен; повторный destroy того же ненулевого адреса является ошибкой вызывающей стороны;
- ни одна функция C ABI не сохраняет `data` после возврата;
- все C++/OpenCV exceptions перехватываются второй exception barrier внутри каждой exported function;
- при `OUT_OF_MEMORY` output string может остаться `NULL`; это единственный допустимый случай без error JSON при доступном output pointer;
- thread safety: разные analyzer handles можно вызывать параллельно; одновременный вызов одного handle запрещён; destroy нельзя выполнять одновременно с analyze.

Enum numeric values и signatures неизменяемы в ABI v1. OpenCV, STL, C++ bool, exceptions и platform-sized types в ABI запрещены. Pure-C consumer компилируется компилятором C11 и линкуется только с опубликованной C target/import metadata.

## 12. Компоненты, файлы и ownership

Нормативные границы:

```text
include/leaf/                 stable C++ contracts; владелец: integrator
include/leaf/c/leaf.h         C ABI v1; integrator + C ABI owner
src/contracts/                contract validation and versions
src/image/                    image views, resize transform
src/preprocess/               photometric preprocessing
src/detection/contour/        leaf contour stage
src/detection/center/         center vein stage
src/detection/secondary/      secondary vein stage
src/geometry/                 pure geometry and coordinate system
src/skeleton/                 deterministic thinning and graph
src/measurement/              M1–M5
src/statistics/               asymmetry, batch, score
src/validation/               quality and invariants
src/api/analyzer.cpp          orchestration only
src/adapters/json/            strict config and result JSON
src/adapters/c/               C ABI and exception barrier
src/debug/                    desktop-only artifacts/rendering
tools/leaf-analyzer-cli/      filesystem, codecs and CLI
tests/unit/                   pure module tests
tests/integration/            pipeline, CLI and C ABI consumers
tests/fixtures/               synthetic/labeled fixtures
tests/expected/               versioned golden outputs
benchmarks/                   Release benchmark harness
```

Интегратор единолично владеет root `CMakeLists.txt`, `cmake/` и `include/leaf/`, принимает contract changes и предотвращает конфликтующие edits. Параллельно допускаются geometry, measurement/statistics, preprocessing, skeleton graph, JSON и fixtures/golden harness. После стабилизации contracts последовательно интегрируются contour, center, secondary и keypoints. Финальная последовательность: quality/Analyzer → debug/CLI → C ABI.

Каждый модуль предоставляет малый contract, не включает заголовки соседней реализации и не читает внутренние структуры другого модуля.

## 13. Dependencies и сборка

Минимумы v1:

- CMake 3.24;
- GCC 11.2 или Clang 14;
- OpenCV 4.5.5 (`core`, `imgproc`; `imgcodecs` только для CLI);
- nlohmann/json 3.11.2;
- GoogleTest 1.14.0 только для tests.

Допустимы более новые minor/patch версии в той же major версии после прохождения полного verification matrix. OpenCV major 5 и nlohmann/json major 4 требуют отдельной совместимости и не входят в v1.

Acquisition policy:

- OpenCV всегда предоставляется system/package-manager toolchain и находится через `find_package`; проект не собирает OpenCV из исходников;
- nlohmann/json и GoogleTest сначала ищутся как installed packages;
- при `LEAF_FETCH_DEPS=ON` CMake может получить только эти две зависимости через `FetchContent` по зафиксированным release tags и проверенным SHA-256 archives;
- `LEAF_FETCH_DEPS` по умолчанию `OFF`, поэтому configure без зависимостей завершается понятной ошибкой и не обращается к сети;
- production target не линкует GoogleTest;
- ONNX Runtime не ищется и не упоминается в CMake v1.

Обязательные options: `LEAF_BUILD_TESTS`, `LEAF_BUILD_CLI`, `LEAF_BUILD_BENCHMARKS`, `LEAF_ENABLE_DEBUG`, `LEAF_FETCH_DEPS`, `LEAF_ENABLE_SANITIZERS`. Build должен экспортировать targets `leaf-core`, `leaf-core-c`, `leaf-analyzer`, `leaf-core-tests` при включённых соответствующих options.

## 14. Тестовая стратегия и review

Разработка каждого модуля идёт RED → GREEN → REFACTOR. До интеграции модуль проходит два отдельных review:

1. соответствие исходной спецификации и этому design;
2. code quality: границы, владение, determinism, ошибки, тестируемость.

Unit tests покрывают:

- image/config validation и каждый `ErrorCode`;
- resize round-trip и coordinate transforms;
- intersections, contour arcs, arc interpolation, local tangents и angles;
- Zhang–Suen на фиксированных binary patterns и стабильный graph;
- M1–M5, включая вырожденные и boundary cases;
- signed relative asymmetry, denominator boundary, `k`, `abs(k)` score boundaries;
- strict JSON malformed/duplicate/unknown/type/range policy;
- batch order, continuation и пустое приемлемое множество.

Integration tests покрывают полный pipeline на synthetic/labeled fixtures, stage failure propagation, deterministic repeat runs, debug on/off, CLI single/batch и C ABI.

Golden tolerances применяются только к synthetic или явно размеченным fixtures:

- keypoint: ≤ 5 px в исходных image coordinates;
- length: ≤ 5% от размеченного значения;
- angle: ≤ 3°;
- asymmetry: ≤ 0.005.

Текущие неразмеченные файлы `testdata/` используются только как smoke/negative inputs. Прохождение pipeline на них не создаёт ground truth и не позволяет применять перечисленные tolerances. Golden expected files изменяются только отдельным осознанным review с указанием algorithm version.

Pure-C test — отдельный `.c` consumer, собранный в режиме C11 C-компилятором; он проверяет ABI version, defaults, invalid/null cases, analyze и освобождение каждой строки/handle.

Sanitizer matrix использует ASan+UBSan в Debug для unit/integration/golden/CLI/C-consumer. Отдельный repeat test выполняет один fixture не менее 100 раз и сравнивает serialized JSON и debug-disabled outputs.

## 15. Performance benchmark

Цель `< 500 ms` относится только к median полного library pipeline без JPEG/PNG decode и без записи файлов.

Нормативные условия:

- Linux x86_64, Intel Core i7-1165G7 либо документированная машина не медленнее по однопоточному baseline;
- Release, GCC 11.2+, `-O3 -DNDEBUG`, sanitizers и debug artifacts выключены;
- OpenCV OpenCL выключен, один CPU thread;
- decoded 12 MP RGB8 input, resize до max dimension 2048;
- analyzer создан до измерения;
- 5 warm-up и 30 measured iterations одного фиксированного benchmark corpus;
- steady clock, без параллельной нагрузки;
- отчёт содержит machine/CPU, compiler, dependency versions, median и p95 для preprocess, contour, center vein, skeleton/secondary, measurements и total.

Критерий v1 — median total `< 500 ms`; p95 публикуется, но не является блокирующим порогом v1. На иной машине benchmark остаётся полезным regression signal, но не доказывает выполнение абсолютного порога.

## 16. Критерии готовности v1

v1 готова, когда одновременно выполнено следующее:

- Linux configure/build проходит с документированными minimum dependencies;
- публичные C++ headers компилируются без OpenCV include paths;
- полный OpenCV pipeline выдаёт M1–M5, asymmetry, score и quality для поддержанного labeled fixture;
- все обязательные stage failures возвращают ожидаемый точный `ErrorCode` без fallback;
- unit, integration и golden suites проходят;
- CLI single/batch smoke проходит, batch продолжает обработку после плохого файла;
- pure-C C11 consumer проходит null, memory и ABI tests;
- ASan/UBSan suite не сообщает ошибок;
- determinism repeat test проходит;
- Release benchmark соответствует нормативным условиям и median target;
- debug недоступен в C ABI и не создаётся при выключенном режиме;
- result/config JSON policy и schema version покрыты contract tests;
- каждый модуль прошёл отдельные spec и code-quality review;
- untracked исходная спецификация и `testdata/` не включаются в implementation commits без отдельного решения.

Любая реализация ML, mobile build или расширение класса входов требует отдельного design change и не может быть условием готовности v1.
