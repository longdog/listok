# leaf-core — спецификация реализации

## 1. Цель

Реализовать кроссплатформенную C++ библиотеку анализа фотографии листа.

Вход:

* изображение листа;
* конфигурация алгоритма.

Выход:

* результат распознавания;
* координаты геометрических объектов;
* измерения пяти признаков слева/справа;
* относительные асимметрии;
* показатель асимметрии листа;
* качество распознавания;
* ошибки;
* отладочные данные.

Библиотека не должна зависеть от Flutter, Android, iOS, GPS, сети или файловой системы.

Основной pipeline из ТЗ:

```text
выравнивание освещенности
→ бинаризация
→ нахождение границ
→ скелетизация
→ определение геометрии
→ измерения
→ расчет асимметрии
```

---

# 2. Технологический стек

Обязательно:

* C++20;
* CMake;
* OpenCV;
* GoogleTest;
* JSON: `nlohmann/json`.

Опционально, но API предусмотреть:

* ONNX Runtime для ML-модуля.

Не использовать Qt.

---

# 3. Структура проекта

```text
leaf-core/
├── CMakeLists.txt
├── cmake/
├── include/
│   └── leaf/
│       ├── analyzer.h
│       ├── config.h
│       ├── image.h
│       ├── types.h
│       ├── result.h
│       └── error.h
├── src/
│   ├── api/
│   ├── image/
│   ├── preprocess/
│   ├── detection/
│   ├── geometry/
│   ├── measurement/
│   ├── statistics/
│   ├── validation/
│   └── debug/
├── tests/
│   ├── unit/
│   ├── integration/
│   ├── fixtures/
│   └── expected/
├── tools/
│   └── leaf-analyzer-cli/
└── models/
```

---

# 4. Публичный API

## 4.1 Image

```cpp
namespace leaf {

enum class PixelFormat {
    Gray8,
    RGB8,
    RGBA8
};

struct Image {
    int width = 0;
    int height = 0;
    int stride = 0;
    PixelFormat format = PixelFormat::RGB8;
    const uint8_t* data = nullptr;
};

}
```

Требования:

* `width > 0`;
* `height > 0`;
* `stride >= width * bytes_per_pixel`;
* `data != nullptr`.

---

# 5. Точки и геометрия

```cpp
struct Point {
    double x = 0;
    double y = 0;
};
```

Использовать координаты:

* исходного изображения;
* нормализованной системы координат листа.

---

# 6. Система координат листа

Создать:

```cpp
struct CoordinateSystem {
    Point origin;

    Point xAxis;
    Point yAxis;

    double scale = 1.0;
};
```

Требования:

* ось `Y` совпадает с центральной жилкой;
* ось `X` совпадает с линией основания листа;
* все относительные геометрические измерения выполнять в системе координат листа.

Для преобразования:

```cpp
Point imageToLeaf(Point p);
Point leafToImage(Point p);
```

---

# 7. Конфигурация

```cpp
struct PreprocessConfig {
    int targetMaxDimension = 2048;

    bool normalizeIllumination = true;

    int blurKernel = 5;

    bool adaptiveThreshold = false;
};

struct DetectionConfig {
    double minLeafAreaRatio = 0.05;
    double maxLeafAreaRatio = 0.95;

    double minimumConfidence = 0.5;

    bool enableMlFallback = false;
};

struct MeasurementConfig {
    bool useNormalizedCoordinates = true;

    double minDenominator = 1e-9;
};

struct QualityConfig {
    double minimumLeafConfidence = 0.5;
    double minimumVeinConfidence = 0.5;
};

struct AnalyzerConfig {
    PreprocessConfig preprocess;
    DetectionConfig detection;
    MeasurementConfig measurement;
    QualityConfig quality;
};
```

---

# 8. Коды ошибок

```cpp
enum class ErrorCode {
    None,

    InvalidImage,
    UnsupportedFormat,

    LeafNotFound,
    LeafTooSmall,
    LeafOutsideFrame,

    CentralVeinNotFound,

    SecondaryVeinNotFound,
    AmbiguousVeins,

    InsufficientContrast,
    InsufficientLighting,

    InvalidCoordinateSystem,

    InvalidMeasurements,

    InternalError
};
```

---

# 9. Предобработка

Создать:

```cpp
class ImagePreprocessor {
public:
    cv::Mat normalize(const Image& image,
                      const PreprocessConfig& config) const;

    cv::Mat grayscale(const cv::Mat& image) const;

    cv::Mat binarize(const cv::Mat& image,
                     const PreprocessConfig& config) const;
};
```

Pipeline:

```text
Image
→ resize
→ grayscale
→ illumination normalization
→ denoise
→ binary mask
```

Требования:

* не изменять исходный `Image`;
* внутренне использовать `cv::Mat`;
* сохранять debug image каждого этапа.

---

# 10. Поиск листа

Создать:

```cpp
struct LeafContour {
    std::vector<Point> points;
    double area = 0;
    cv::Rect boundingBox;
    double confidence = 0;
};

class LeafDetector {
public:
    LeafContour detect(
        const cv::Mat& binary,
        const DetectionConfig& config) const;
};
```

Алгоритм:

1. найти контуры;
2. отфильтровать по площади;
3. выбрать кандидата;
4. проверить форму;
5. рассчитать confidence;
6. вернуть основной контур.

Ошибки:

```text
LeafNotFound
LeafTooSmall
LeafOutsideFrame
```

---

# 11. Центральная жилка

Создать:

```cpp
struct CenterVein {
    std::vector<Point> path;

    Point base;
    Point apex;

    double confidence = 0;
};
```

Создать:

```cpp
class CenterVeinDetector {
public:
    CenterVein detect(
        const cv::Mat& image,
        const LeafContour& leaf) const;
};
```

Требования:

* определить направление основания → верхушки;
* получить непрерывный path;
* определить `base`;
* определить `apex`;
* построить ось `Y`.

При невозможности определить жилку:

```text
CentralVeinNotFound
```

---

# 12. Точки листа

Результат:

```cpp
struct LeafKeypoints {
    Point a1;
    Point a2;

    Point b1;
    Point b2;

    Point c1;
    Point c2;

    Point d1;
    Point d2;

    Point e;

    Point f;

    Point g1;
    Point g2;
};
```

Семантика:

```text
a1/a2 — начало первой жилки 2-го порядка слева/справа
b1/b2 — начало второй жилки 2-го порядка слева/справа
c1/c2 — конец первой жилки слева/справа
d1/d2 — конец второй жилки слева/справа
e      — верхняя точка листа
f      — центр листа
g1/g2  — левая/правая граница листа в центральной части
```

---

# 13. Жилки второго порядка

```cpp
struct Vein {
    Point start;
    Point end;

    std::vector<Point> path;

    double length = 0;
    double confidence = 0;
};

struct SecondaryVeins {
    Vein left1;
    Vein left2;

    Vein right1;
    Vein right2;

    double confidence = 0;
};
```

Порядок жилок определяется от основания листа.

Если соответствующую жилку невозможно определить однозначно:

```text
SecondaryVeinNotFound
```

или:

```text
AmbiguousVeins
```

---

# 14. Алгоритм определения жилок

Первая реализация:

1. получить grayscale;
2. выделить жилки;
3. выполнить фильтрацию;
4. получить skeleton;
5. удалить короткие ответвления;
6. построить граф скелета;
7. определить центральную жилку;
8. найти ответвления от центральной жилки;
9. определить их направление;
10. выбрать первую и вторую жилки второго порядка;
11. определить начало и конец каждой;
12. рассчитать confidence.

Не предполагать, что skeleton автоматически дает правильную топологию.

Ветка должна быть отфильтрована по:

* длине;
* углу относительно центральной жилки;
* расстоянию от основания;
* принадлежности листу.

Все thresholds вынести в `DetectionConfig`.

---

# 15. Нормализация

После нахождения:

```text
base
apex
central vein
leaf contour
```

построить систему координат.

Установить:

```text
origin = base
Y axis = base → apex
X axis = перпендикуляр к Y
```

Для всех точек сохранить:

```cpp
struct NormalizedPoint {
    Point image;
    Point leaf;
};
```

---

# 16. Измерения

Создать:

```cpp
struct MeasurementValue {
    double value = 0;

    double confidence = 0;

    bool valid = false;
};

struct SideMeasurements {
    MeasurementValue m1;
    MeasurementValue m2;
    MeasurementValue m3;
    MeasurementValue m4;
    MeasurementValue m5;
};

struct LeafMeasurements {
    SideMeasurements left;
    SideMeasurements right;
};
```

---

# 17. Формулы измерений

## M1

Ширина половинки листа в центральной части.

Определить точку `f` на центральной жилке.

Провести линию:

```text
X axis
```

через `f`.

Найти пересечения с левым и правым контуром.

Получить:

```text
m1_left
m1_right
```

---

## M2

Длина второй жилки второго порядка.

```text
m2_left  = length(left2)
m2_right = length(right2)
```

---

## M3

Расстояние между основаниями первой и второй жилки второго порядка по центральной жилке.

```text
m3_left  = distance(a1, b1)
m3_right = distance(a2, b2)
```

---

## M4

Расстояние между концами первой и второй жилки второго порядка по краю листа.

```text
m4_left  = distance(c1, d1)
m4_right = distance(c2, d2)
```

---

## M5

Угол между центральной жилкой и второй жилкой второго порядка.

```text
m5_left  = angle(centerVein, left2)
m5_right = angle(centerVein, right2)
```

Угол должен быть приведен к диапазону:

```text
0..180°
```

---

# 18. Относительная асимметрия

Для каждого параметра:

```text
A = (L - R) / (L + R)
```

Реализовать:

```cpp
struct RelativeAsymmetry {
    MeasurementValue m1;
    MeasurementValue m2;
    MeasurementValue m3;
    MeasurementValue m4;
    MeasurementValue m5;
};
```

Если:

```text
abs(L + R) < minDenominator
```

значение невалидно.

Не использовать silent fallback.

---

# 19. Асимметрия листа

```cpp
struct LeafAsymmetry {
    double value = 0;

    RelativeAsymmetry features;

    bool valid = false;
};
```

Расчет:

```text
k = (A1 + A2 + A3 + A4 + A5) / 5
```

Не считать результат, если обязательные признаки невалидны.

---

# 20. Оценка 1–5

Создать конфигурацию:

```cpp
struct ScoreRange {
    double min;
    double max;
    int score;
};
```

По исходному ТЗ:

```text
score 1: < 0.040
score 2: 0.040–0.044
score 3: 0.045–0.049
score 4: 0.050–0.054
score 5: > 0.054
```

Эти границы не зашивать в алгоритм.

Хранить в `AnalyzerConfig`.

---

# 21. Качество результата

```cpp
struct QualityAssessment {
    bool acceptable = false;

    double overallConfidence = 0;

    double leafConfidence = 0;
    double centerVeinConfidence = 0;
    double secondaryVeinConfidence = 0;

    bool sufficientContrast = true;
    bool sufficientLighting = true;
    bool insideFrame = true;

    std::vector<ErrorCode> errors;
};
```

Правило:

```text
acceptable = true
```

только если:

* лист обнаружен;
* центральная жилка обнаружена;
* необходимые жилки обнаружены;
* все обязательные измерения валидны;
* confidence выше порога.

---

# 22. Полный результат

```cpp
struct AnalysisResult {
    bool success = false;

    ErrorCode error = ErrorCode::None;

    QualityAssessment quality;

    LeafContour leaf;
    CenterVein centerVein;
    SecondaryVeins secondaryVeins;

    LeafKeypoints keypoints;

    LeafMeasurements measurements;

    RelativeAsymmetry relativeAsymmetry;

    LeafAsymmetry asymmetry;

    int score = 0;
};
```

---

# 23. Главный класс

```cpp
class Analyzer {
public:
    explicit Analyzer(const AnalyzerConfig& config);

    AnalysisResult analyze(const Image& image) const;
};
```

Порядок:

```text
analyze
│
├── validate image
├── preprocess
├── detect leaf
├── detect center vein
├── build coordinate system
├── detect secondary veins
├── extract keypoints
├── validate geometry
├── calculate measurements
├── calculate relative asymmetry
├── calculate leaf asymmetry
├── calculate score
└── return result
```

---

# 24. Batch API

```cpp
struct BatchResult {
    int total = 0;
    int successful = 0;

    std::vector<AnalysisResult> leaves;

    double meanAsymmetry = 0;
    bool valid = false;

    int score = 0;
};
```

API:

```cpp
BatchResult analyzeBatch(
    std::span<const Image> images) const;
```

Правило:

```text
meanAsymmetry =
sum(valid leaf asymmetry) / number of valid leaves
```

Неуспешные листья не включать в среднее.

---

# 25. Debug API

Добавить:

```cpp
struct DebugImages {
    cv::Mat normalized;
    cv::Mat binary;
    cv::Mat contour;
    cv::Mat skeleton;
    cv::Mat centerVein;
    cv::Mat secondaryVeins;
    cv::Mat keypoints;
    cv::Mat final;
};
```

Не включать debug images в основной mobile API.

Для desktop CLI включить:

```text
--debug output/debug/
```

---

# 26. CLI для разработки

Создать программу:

```text
leaf-analyzer
```

Использование:

```bash
leaf-analyzer \
    --input leaf.jpg \
    --output result.json \
    --debug debug/
```

Batch:

```bash
leaf-analyzer \
    --input-dir ./dataset \
    --output-dir ./results \
    --debug-dir ./debug
```

CLI должен:

1. загрузить изображение;
2. вызвать `Analyzer`;
3. вывести JSON;
4. при `--debug` сохранить все промежуточные изображения.

---

# 27. JSON результата

Пример:

```json
{
  "success": true,
  "error": "None",
  "quality": {
    "acceptable": true,
    "overallConfidence": 0.94
  },
  "measurements": {
    "left": {
      "m1": 0.42,
      "m2": 0.31,
      "m3": 0.18,
      "m4": 0.22,
      "m5": 31.4
    },
    "right": {
      "m1": 0.40,
      "m2": 0.30,
      "m3": 0.19,
      "m4": 0.21,
      "m5": 30.7
    }
  },
  "relativeAsymmetry": {
    "m1": 0.024,
    "m2": 0.016,
    "m3": -0.027,
    "m4": 0.023,
    "m5": 0.011
  },
  "asymmetry": {
    "value": 0.015
  },
  "score": 1
}
```

Числа выше используются только как форматный пример и не являются эталонными тестовыми значениями.

---

# 28. ML interface

Не реализовывать ML в первой версии.

Но определить интерфейсы:

```cpp
class ILeafDetector {
public:
    virtual ~ILeafDetector() = default;

    virtual LeafContour detect(
        const cv::Mat& image) = 0;
};
```

```cpp
class IVeinDetector {
public:
    virtual ~IVeinDetector() = default;

    virtual SecondaryVeins detect(
        const cv::Mat& image,
        const LeafContour& leaf,
        const CenterVein& center) = 0;
};
```

Реализация первой версии:

```text
OpenCVLeafDetector
OpenCVVeinDetector
```

Будущая:

```text
OnnxLeafDetector
OnnxVeinDetector
```

---

# 29. C ABI

Для Flutter/мобильной интеграции создать отдельную библиотеку `leaf-core-c`.

```c
typedef struct leaf_analyzer_t leaf_analyzer_t;

leaf_analyzer_t*
leaf_analyzer_create(const char* config_json);

int leaf_analyzer_analyze(
    leaf_analyzer_t* analyzer,
    const uint8_t* data,
    int width,
    int height,
    int stride,
    int pixel_format,
    char** result_json);

void leaf_string_free(char* value);

void leaf_analyzer_destroy(
    leaf_analyzer_t* analyzer);
```

C ABI не должен содержать:

* STL;
* C++ classes;
* exceptions;
* OpenCV types.

Все исключения должны перехватываться внутри ABI.

---

# 30. Тесты

Создать unit tests для:

```text
CoordinateSystem
distance
angle
intersection
normalization

M1
M2
M3
M4
M5

relative asymmetry
leaf asymmetry
batch mean
score ranges
```

Каждая математическая формула должна иметь минимум:

* нормальный случай;
* левое > правого;
* левое < правого;
* равные значения;
* нулевой знаменатель;
* пограничные значения.

---

# 31. Dataset для CV

Создать:

```text
tests/fixtures/
├── good/
├── bad/
├── dark/
├── bright/
├── rotated/
├── partial/
├── low_contrast/
├── branching/
└── ambiguous/
```

Для каждого изображения хранить ground truth:

```json
{
  "image": "leaf001.jpg",
  "keypoints": {
    "a1": [100, 200],
    "a2": [300, 200]
  }
}
```

Также хранить ожидаемые:

```text
leaf contour
center vein
vein paths
m1..m5
asymmetry
```

---

# 32. Golden tests

Для каждого эталонного изображения задать допустимое отклонение.

Пример:

```text
point error          <= 5 px
length error         <= 5%
angle error          <= 3°
asymmetry error      <= 0.005
```

Пороговые значения вынести в test config.

При изменении алгоритма запускать весь golden dataset.

---

# 33. Debug visualization

Финальное debug image должно показывать:

```text
контур листа
центральную жилку
оси координат
a1/a2
b1/b2
c1/c2
d1/d2
e
f
g1/g2
```

Пример подписей:

```text
a1 ●
b1 ●

c1 ●
d1 ●

       │
       │ center vein
       │

a2 ●
b2 ●
```

Также отображать численные значения `m1..m5`.

---

# 34. Логи

`leaf-core` не должен писать в stdout/stderr самостоятельно.

Предусмотреть callback:

```cpp
enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

using LogCallback =
    void(*)(LogLevel level, const char* message);
```

Опционально:

```cpp
AnalyzerConfig::logCallback
```

---

# 35. Детерминированность

Для одинаковых:

```text
image
config
algorithm version
model version
```

результат должен быть одинаковым.

Не использовать случайность без фиксированного seed.

---

# 36. Версии

Результат должен содержать:

```cpp
struct VersionInfo {
    std::string libraryVersion;
    std::string algorithmVersion;
    std::string modelVersion;
    std::string methodologyVersion;
};
```

`modelVersion` для OpenCV реализации:

```text
null
```

---

# 37. Производительность

Целевой режим:

```text
input:
JPEG/PNG до 12 MP

internal:
max dimension = 2048 px

target:
< 500 ms
```

На desktop performance test отдельно измерять:

```text
preprocess
leaf detection
center vein
secondary veins
measurements
total
```

Для каждого этапа использовать `std::chrono`.

---

# 38. Memory requirements

Не хранить одновременно несколько копий исходного изображения без необходимости.

Pipeline должен позволять освобождать промежуточные `cv::Mat`.

Debug images создавать только при включенном debug mode.

---

# 39. CMake targets

Обязательные targets:

```text
leaf-core
leaf-core-c
leaf-analyzer
leaf-core-tests
```

Пример:

```bash
cmake -S . -B build \
  -DLEAF_BUILD_TESTS=ON \
  -DLEAF_BUILD_CLI=ON

cmake --build build --config Release
ctest --test-dir build
```

---

# 40. CI

Каждый commit:

```text
configure
→ build
→ unit tests
→ integration tests
→ golden tests
```

Минимальные платформы CI:

```text
Linux x86_64
Windows x86_64
```

После стабилизации:

```text
Android arm64
iOS arm64
```

---

# 41. Этапы реализации

## Этап 1 — infrastructure

Сделать:

```text
CMake
public headers
Image
Config
Result
ErrorCode
Analyzer
CLI
GoogleTest
```

`analyze()` пока возвращает `NotImplemented`.

Готовность:

```text
build = success
tests = success
CLI запускается
```

---

## Этап 2 — preprocessing

Реализовать:

```text
resize
grayscale
illumination normalization
binary mask
```

Добавить debug output.

Тесты на fixtures.

---

## Этап 3 — leaf detection

Реализовать:

```text
contours
area filtering
bounding box
confidence
```

Golden tests:

```text
leaf contour
bounding box
```

---

## Этап 4 — coordinate system

Реализовать:

```text
base
apex
center
X/Y axes
image ↔ leaf coordinates
```

Добавить unit tests на искусственных геометрических данных.

---

## Этап 5 — center vein

Реализовать:

```text
candidate centerlines
selection
path
base
apex
confidence
```

Добавить debug visualization.

---

## Этап 6 — secondary veins

Реализовать:

```text
skeleton
graph
branch detection
vein ordering
start/end points
confidence
```

Это основной CV этап.

---

## Этап 7 — measurements

Реализовать `m1..m5`.

Не смешивать CV и математические расчеты.

`MeasurementExtractor` должен принимать уже распознанную геометрию.

---

## Этап 8 — statistics

Реализовать:

```text
relative asymmetry
leaf asymmetry
batch mean
score
```

Все проверить unit tests.

---

## Этап 9 — quality

Добавить:

```text
confidence
validation
error codes
acceptable/not acceptable
```

---

## Этап 10 — debug CLI

CLI должен позволять анализировать отдельную фотографию и dataset.

Обязательный output:

```text
result.json
debug/
```

---

## Этап 11 — dataset

Собрать реальные фотографии.

Разметить:

```text
leaf contour
center vein
secondary veins
keypoints
```

Сначала минимум 100 изображений.

Разделить:

```text
train/dev/test
```

Даже если ML пока не используется.

---

## Этап 12 — ML fallback

Только после анализа качества OpenCV.

Добавить:

```text
ONNX Runtime
OnnxLeafDetector
OnnxVeinDetector
```

Не менять публичный API `Analyzer`.

---

## Этап 13 — mobile build

Собрать:

```text
Android arm64
iOS arm64
```

Через C ABI.

Проверить:

```text
memory
latency
camera resolution
repeated analysis
```

---

# 42. Критерии готовности первой версии

`leaf-core v0.1` считается готовым, когда:

```text
[ ] собирается Linux
[ ] работает CLI
[ ] работает unit test suite
[ ] работает preprocessing
[ ] определяется лист
[ ] определяется центральная жилка
[ ] определяются 4 вторичные жилки
[ ] определяются a..g
[ ] рассчитываются m1..m5
[ ] рассчитывается relative asymmetry
[ ] рассчитывается asymmetry leaf
[ ] рассчитывается batch result
[ ] работает score
[ ] возвращаются quality/error
[ ] работает debug visualization
[ ] есть golden dataset
[ ] результат детерминирован
```

# 43. Ограничения первой версии

Не реализовывать:

```text
[ ] Flutter
[ ] Android UI
[ ] iOS UI
[ ] GPS
[ ] карты
[ ] сеть
[ ] сервер
[ ] авторизацию
[ ] облачное хранение
[ ] автоматическое обучение модели
```

Не добавлять ML до получения результатов на реальном dataset.

# 44. Главный принцип реализации

Разделить код на четыре независимых слоя:

```text
IMAGE/CV
    ↓
GEOMETRY
    ↓
MEASUREMENT
    ↓
STATISTICS
```

`Measurement` не должен выполнять распознавание изображения.

`Statistics` не должен знать об OpenCV.

`Analyzer` только оркестрирует pipeline.

C ABI используется только на границе мобильного приложения.
