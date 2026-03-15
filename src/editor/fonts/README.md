# Roboto Font Integration

## Шаг 1: Скачайте Roboto шрифт

Перейдите на https://fonts.google.com/specimen/Roboto

Нажмите кнопку **"Download family"** в правом верхнем углу.

Распакуйте скачанный архив.

## Шаг 2: Скопируйте файл шрифта

Из распакованного архива скопируйте файл:
```
Roboto-Medium.ttf
```

В директорию:
```
/Users/vladimir/an24_cpp/src/editor/fonts/
```

## Шаг 3: Проверьте установку

Убедитесь что файл существует:
```bash
ls -la /Users/vladimir/an24_cpp/src/editor/fonts/Roboto-Medium.ttf
```

## Использование

### В существующем приложении с ImGui

После `ImGui::CreateContext()` добавьте:

```cpp
#include "editor/imgui_theme.h"

// ... ImGui::CreateContext() ...

// Загрузить Roboto с поддержкой кириллицы
ImGuiTheme::LoadRobotoWithCyrillic(18.0f);

// Применить современную темную тему
ImGuiTheme::ApplyModernDarkTheme();
```

### В unit тестах

```cpp
class RenderTest : public ::testing::Test {
protected:
    void SetUp() override {
        ImGui::CreateContext();
        ImGuiTheme::LoadRobotoWithCyrillic(16.0f);
        ImGuiTheme::ApplyModernDarkTheme();
    }

    void TearDown() override {
        ImGui::DestroyContext();
    }
};
```

## Доступные функции

```cpp
namespace ImGuiTheme {

// Загрузить Roboto (только English)
ImFont* LoadRoboto(float size_pixels = 18.0f);

// Загрузить Roboto с кириллицей (рекомендуется)
ImFont* LoadRobotoWithCyrillic(float size_pixels = 18.0f);

// Применить современную темную тему
void ApplyModernDarkTheme();

// Применить современную светлую тему
void ApplyModernLightTheme();

}
```

## Кастомизация

### Размер шрифта

```cpp
// Для обычного экрана
ImGuiTheme::LoadRobotoWithCyrillic(16.0f);

// Для high-DPI (Retina)
ImGuiTheme::LoadRobotoWithCyrillic(20.0f);
```

### Скругление углов

После применения темы:

```cpp
ImGui::GetStyle().WindowRounding = 12.0f;  // Более округлое окно
ImGui::GetStyle().FrameRounding = 8.0f;   // Более округлые кнопки
```

### Цвета

```cpp
auto* colors = ImGui::GetStyle().Colors;

// Изменить цвет кнопок
colors[ImGuiCol_Button] = ImVec4(0.3f, 0.6f, 0.9f, 1.0f);
colors[ImGuiCol_ButtonHovered] = ImVec4(0.4f, 0.7f, 1.0f, 1.0f);

// Изменить акцентный цвет
colors[ImGuiCol_CheckMark] = ImVec4(0.1f, 0.8f, 0.4f, 1.0f);  // Зеленый
```

## Troubleshooting

### Шрифт не загружается

Проверьте логи - будет сообщение:
```
[warn] Could not find Roboto font in any of the expected locations
```

Решение: Убедитесь что `Roboto-Medium.ttf` находится в одной из директорий:
- `fonts/Roboto-Medium.ttf`
- `src/editor/fonts/Roboto-Medium.ttf`
- `/Users/vladimir/an24_cpp/src/editor/fonts/Roboto-Medium.ttf`

### Кириллица не отображается

Убедитесь что используете `LoadRobotoWithCyrillic()` вместо `LoadRoboto()`:

```cpp
// ПРАВИЛЬНО (с кириллицей)
ImGuiTheme::LoadRobotoWithCyrillic(18.0f);

// НЕПРАВИЛЬНО (только English)
ImGuiTheme::LoadRoboto(18.0f);
```

### Текст выглядит размыто

Попробуйте другой размер шрифта или отключите oversampling:

```cpp
ImFontConfig config;
config.OversampleH = 2;  // Уменьшить с 3 до 2
config.OversampleV = 1;
io.Fonts->AddFontFromFileTTF(path, 18.0f, &config, io.Fonts->GetGlyphRangesCyrillic());
```

## Примеры

### Полная инициализация

```cpp
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#include "editor/imgui_theme.h"

// ... SDL и OpenGL инициализация ...

ImGui::CreateContext();
ImGuiIO& io = ImGui::GetIO();
io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

// Backend
ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
ImGui_ImplOpenGL3_Init(glsl_version);

// === ТЕМА И ШРИФТ ===
ImGuiTheme::LoadRobotoWithCyrillic(18.0f);
ImGuiTheme::ApplyModernDarkTheme();

// ... main loop ...
```

### Переключение тем

```cpp
bool use_dark_theme = true;

if (ImGui::Button("Toggle Theme")) {
    use_dark_theme = !use_dark_theme;
    if (use_dark_theme) {
        ImGuiTheme::ApplyModernDarkTheme();
    } else {
        ImGuiTheme::ApplyModernLightTheme();
    }
}
```

## Производительность

Первичная загрузка шрифта занимает ~50-100ms, но это происходит только один раз при старте приложения.

В коде нет никаких накладных расходов runtime - все отрисовывается так же быстро как с стандартным шрифтом.

## Лицензия

Roboto шрифт распространяется по лицензии Apache License 2.0.
https://www.apache.org/licenses/LICENSE-2.0
