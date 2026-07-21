#include "Config.h"

#include <cstdio>
#include <cwchar>
#include <cwctype>

namespace {

// Default config, written on first run. Stored as UTF-16 LE with BOM so that
// GetPrivateProfileString reads the Cyrillic comments correctly.
const wchar_t kDefaultIni[] =
L"; ============================================================\r\n"
L";  InfiniteCanvas — настройки\r\n"
L";  Файл создаётся автоматически, если он отсутствует.\r\n"
L";  Кодировка файла: UTF-16 LE (не меняйте её при сохранении).\r\n"
L"; ============================================================\r\n"
L"\r\n"
L"[Pan]\r\n"
L"; Основной жест: Ctrl+Alt + зажатая средняя кнопка — панорамирование\r\n"
L"; из любого места, поверх любого окна\r\n"
L"CtrlAltMiddle=true\r\n"
L"; Дополнительно: средняя кнопка на фоне рабочего стола (без модификаторов)\r\n"
L"MiddleButton=true\r\n"
L"; Дополнительно: Alt + левая кнопка на фоне рабочего стола\r\n"
L"AltLeftButton=true\r\n"
L"; Чувствительность панорамирования (1.0 = движение 1:1)\r\n"
L"Sensitivity=1.0\r\n"
L"; Порог в пикселях: меньше — клик, больше — перетаскивание\r\n"
L"DragThresholdPx=4\r\n"
L"\r\n"
L"[Zoom]\r\n"
L"; Зум колесом: Ctrl+Alt + прокрутка (геометрический зум раскладки окон)\r\n"
L"Enabled=true\r\n"
L"; Глобально поглощать Ctrl+Alt+колесо, не отдавая его активному приложению\r\n"
L"CaptureWheel=true\r\n"
L"; Множитель масштаба на один щелчок колеса\r\n"
L"Step=1.1\r\n"
L"; Пределы масштаба\r\n"
L"Min=0.25\r\n"
L"Max=2.5\r\n"
L"\r\n"
L"[Render]\r\n"
L"; Целевая частота цикла анимации (инерция, перелёты), кадров/сек. 30..240.\r\n"
L"; Реальная перерисовка чужих окон ограничена DWM — см. README.\r\n"
L"TargetFPS=240\r\n"
L"\r\n"
L"[Inertia]\r\n"
L"; Инерция холста после отпускания кнопки\r\n"
L"Enabled=true\r\n"
L"; Затухание, 1/сек: больше значение — быстрее останавливается\r\n"
L"Friction=5.0\r\n"
L"; Минимальная скорость отпускания для запуска инерции, px/сек\r\n"
L"MinVelocity=250\r\n"
L"\r\n"
L"[Camera]\r\n"
L"; Длительность плавного перелёта камеры (Home, закладки, обзор), мс. 0 = мгновенно\r\n"
L"FlightMs=180\r\n"
L"\r\n"
L"[Filter]\r\n"
L"; Пропускать окна без заголовка\r\n"
L"SkipUntitled=true\r\n"
L"; Не двигать полноэкранные окна (игры, видео без рамки)\r\n"
L"SkipFullscreen=true\r\n"
L"; Не двигать развёрнутые (maximized) окна\r\n"
L"SkipMaximized=false\r\n"
L"; Классы окон, которые запрещено двигать (разделитель ;)\r\n"
L"ExcludeClasses=RainmeterMeterWindow;RainmeterTrayClass\r\n"
L"; Подстроки заголовков окон, которые запрещено двигать (разделитель ;)\r\n"
L"ExcludeTitles=\r\n"
L"; Классы окон, считающихся фоном рабочего стола (старт панорамирования)\r\n"
L"BackgroundClasses=Progman;WorkerW\r\n"
L"\r\n"
L"[Overview]\r\n"
L"; Режим обзора холста (Ctrl+Alt+O)\r\n"
L"Enabled=true\r\n"
L"; Непрозрачность оверлея, 0-255\r\n"
L"Alpha=235\r\n"
L"\r\n"
L"[Backdrop]\r\n"
L"; Подложка-полотно за всеми окнами (панорамируется и зумится с камерой)\r\n"
L"Enabled=true\r\n"
L"; Цвет полотна, RRGGBB (hex)\r\n"
L"Color=14161C\r\n"
L"; Точечная сетка на полотне\r\n"
L"ShowGrid=true\r\n"
L"; Цвет сетки, RRGGBB (hex)\r\n"
L"GridColor=2A2E3A\r\n"
L"; Шаг сетки в виртуальных пикселях\r\n"
L"GridStep=96\r\n"
L"\r\n"
L"[Hotkeys]\r\n"
L"; Закладки камеры: Ctrl+Alt+1..4 — перейти, Ctrl+Alt+Shift+1..4 — сохранить\r\n"
L"EnableBookmarks=true\r\n"
L"\r\n"
L"[Debug]\r\n"
L"; Показывать оверлей FPS при запуске (переключается Ctrl+Alt+F)\r\n"
L"ShowFps=false\r\n"
L"; Писать лог в infinitecanvas.log рядом с exe\r\n"
L"LogToFile=false\r\n"
L"\r\n"
L"[Bookmarks]\r\n"
L"; Заполняется автоматически (Ctrl+Alt+Shift+1..4): x,y,масштаб\r\n";

std::wstring Trim(const std::wstring& s)
{
    size_t begin = 0, end = s.size();
    while (begin < end && iswspace(s[begin]))
        ++begin;
    while (end > begin && iswspace(s[end - 1]))
        --end;
    return s.substr(begin, end - begin);
}

} // namespace

void Config::Load()
{
    path_ = ExeDir() + L"\\config.ini";
    EnsureFileExists();

    ctrlAltMiddle = ReadBool(L"Pan", L"CtrlAltMiddle", true);
    middleButton = ReadBool(L"Pan", L"MiddleButton", true);
    altLeftButton = ReadBool(L"Pan", L"AltLeftButton", true);
    sensitivity = ReadDouble(L"Pan", L"Sensitivity", 1.0);
    if (sensitivity < 0.05 || sensitivity > 20.0)
        sensitivity = 1.0;
    dragThresholdPx = ReadInt(L"Pan", L"DragThresholdPx", 4);
    if (dragThresholdPx < 0 || dragThresholdPx > 100)
        dragThresholdPx = 4;

    zoomEnabled = ReadBool(L"Zoom", L"Enabled", true);
    captureWheel = ReadBool(L"Zoom", L"CaptureWheel", true);
    zoomStep = ReadDouble(L"Zoom", L"Step", 1.1);
    if (zoomStep < 1.01 || zoomStep > 2.0)
        zoomStep = 1.1;
    zoomMin = ReadDouble(L"Zoom", L"Min", 0.25);
    zoomMax = ReadDouble(L"Zoom", L"Max", 2.5);
    if (zoomMin < 0.05)
        zoomMin = 0.05;
    if (zoomMax > 8.0)
        zoomMax = 8.0;
    if (zoomMin > 1.0)
        zoomMin = 1.0;
    if (zoomMax < 1.0)
        zoomMax = 1.0;

    targetFps = ReadInt(L"Render", L"TargetFPS", 240);
    if (targetFps < 30)
        targetFps = 30;
    if (targetFps > 240)
        targetFps = 240;

    inertiaEnabled = ReadBool(L"Inertia", L"Enabled", true);
    inertiaFriction = ReadDouble(L"Inertia", L"Friction", 5.0);
    if (inertiaFriction < 0.5 || inertiaFriction > 50.0)
        inertiaFriction = 5.0;
    inertiaMinVelocity = ReadDouble(L"Inertia", L"MinVelocity", 250.0);

    flightMs = ReadInt(L"Camera", L"FlightMs", 180);
    if (flightMs < 0 || flightMs > 5000)
        flightMs = 180;

    skipUntitled = ReadBool(L"Filter", L"SkipUntitled", true);
    skipFullscreen = ReadBool(L"Filter", L"SkipFullscreen", true);
    skipMaximized = ReadBool(L"Filter", L"SkipMaximized", false);
    excludeClasses = ReadList(L"Filter", L"ExcludeClasses", L"RainmeterMeterWindow;RainmeterTrayClass");
    excludeTitles = ReadList(L"Filter", L"ExcludeTitles", L"");
    backgroundClasses = ReadList(L"Filter", L"BackgroundClasses", L"Progman;WorkerW");
    if (backgroundClasses.empty())
        backgroundClasses = {L"Progman", L"WorkerW"};

    overviewEnabled = ReadBool(L"Overview", L"Enabled", true);
    overviewAlpha = ReadInt(L"Overview", L"Alpha", 235);
    if (overviewAlpha < 30 || overviewAlpha > 255)
        overviewAlpha = 235;

    backdropEnabled = ReadBool(L"Backdrop", L"Enabled", true);
    backdropColor = ReadColor(L"Backdrop", L"Color", RGB(0x14, 0x16, 0x1C));
    backdropShowGrid = ReadBool(L"Backdrop", L"ShowGrid", true);
    backdropGridColor = ReadColor(L"Backdrop", L"GridColor", RGB(0x2A, 0x2E, 0x3A));
    backdropGridStep = ReadInt(L"Backdrop", L"GridStep", 96);
    if (backdropGridStep < 16 || backdropGridStep > 1024)
        backdropGridStep = 96;

    enableBookmarks = ReadBool(L"Hotkeys", L"EnableBookmarks", true);
    showFps = ReadBool(L"Debug", L"ShowFps", false);
    logToFile = ReadBool(L"Debug", L"LogToFile", false);

    for (int i = 0; i < 4; ++i) {
        wchar_t key[16];
        _snwprintf_s(key, _TRUNCATE, L"Slot%d", i + 1);
        std::wstring value = ReadString(L"Bookmarks", key, L"");
        double x = 0, y = 0, scale = 1.0;
        int parsed = swscanf_s(value.c_str(), L"%lf , %lf , %lf", &x, &y, &scale);
        if (parsed >= 2) {
            bookmarks[i].set = true;
            bookmarks[i].cam.x = x;
            bookmarks[i].cam.y = y;
            bookmarks[i].cam.scale = (parsed == 3 && scale > 0.01 && scale < 100.0) ? scale : 1.0;
        } else {
            bookmarks[i].set = false;
        }
    }
}

void Config::SaveBookmark(int index)
{
    if (index < 0 || index >= 4 || !bookmarks[index].set)
        return;
    wchar_t key[16], value[96];
    _snwprintf_s(key, _TRUNCATE, L"Slot%d", index + 1);
    _snwprintf_s(value, _TRUNCATE, L"%.2f,%.2f,%.4f",
                 bookmarks[index].cam.x, bookmarks[index].cam.y, bookmarks[index].cam.scale);
    WritePrivateProfileStringW(L"Bookmarks", key, value, path_.c_str());
}

void Config::EnsureFileExists()
{
    if (GetFileAttributesW(path_.c_str()) != INVALID_FILE_ATTRIBUTES)
        return;

    HANDLE file = CreateFileW(path_.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        Log(L"Config: failed to create %s (error %lu)", path_.c_str(), GetLastError());
        return;
    }

    const wchar_t bom = 0xFEFF;
    DWORD written = 0;
    WriteFile(file, &bom, sizeof(bom), &written, nullptr);
    WriteFile(file, kDefaultIni, (DWORD)(wcslen(kDefaultIni) * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(file);
    Log(L"Config: created default %s", path_.c_str());
}

std::wstring Config::ReadString(const wchar_t* section, const wchar_t* key, const wchar_t* def) const
{
    wchar_t buf[1024];
    GetPrivateProfileStringW(section, key, def, buf, 1024, path_.c_str());
    return Trim(buf);
}

bool Config::ReadBool(const wchar_t* section, const wchar_t* key, bool def) const
{
    std::wstring v = ReadString(section, key, def ? L"true" : L"false");
    if (_wcsicmp(v.c_str(), L"true") == 0 || v == L"1" || _wcsicmp(v.c_str(), L"yes") == 0)
        return true;
    if (_wcsicmp(v.c_str(), L"false") == 0 || v == L"0" || _wcsicmp(v.c_str(), L"no") == 0)
        return false;
    return def;
}

int Config::ReadInt(const wchar_t* section, const wchar_t* key, int def) const
{
    std::wstring v = ReadString(section, key, L"");
    if (v.empty())
        return def;
    return _wtoi(v.c_str());
}

double Config::ReadDouble(const wchar_t* section, const wchar_t* key, double def) const
{
    std::wstring v = ReadString(section, key, L"");
    if (v.empty())
        return def;
    return _wtof(v.c_str());
}

COLORREF Config::ReadColor(const wchar_t* section, const wchar_t* key, COLORREF def) const
{
    std::wstring v = ReadString(section, key, L"");
    if (v.empty())
        return def;
    if (v[0] == L'#')
        v.erase(0, 1);
    wchar_t* end = nullptr;
    unsigned long rgb = wcstoul(v.c_str(), &end, 16);
    if (end == v.c_str() || rgb > 0xFFFFFF)
        return def;
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

std::vector<std::wstring> Config::ReadList(const wchar_t* section, const wchar_t* key, const wchar_t* def) const
{
    std::wstring v = ReadString(section, key, def);
    std::vector<std::wstring> items;
    size_t pos = 0;
    while (pos <= v.size()) {
        size_t sep = v.find(L';', pos);
        if (sep == std::wstring::npos)
            sep = v.size();
        std::wstring item = Trim(v.substr(pos, sep - pos));
        if (!item.empty())
            items.push_back(item);
        pos = sep + 1;
    }
    return items;
}
