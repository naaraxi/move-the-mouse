#pragma comment(lib, "Comctl32.lib")
// Pull in the v6 common controls so the calendar / edit controls are themed.
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include "WindowsProject1.h"
#include <windows.h>
#include <commctrl.h>
#include <random>
#include <chrono>
#include <string>
#include <memory>
#include <functional>

// Constants
constexpr int WINDOW_WIDTH = 400;
constexpr int WINDOW_HEIGHT = 550;
constexpr int MOUSE_MOVE_INTERVAL_MS = 30;   // WM_TIMER can't do 5ms (~15ms floor); phase length is clock-based
constexpr int MOUSE_MOVE_DURATION_MS = 5000;
constexpr int MOUSE_PAUSE_DURATION_MS = 5000;
constexpr int CHECK_TIME_INTERVAL_MS = 1000;

// Application state
struct AppState {
    HINSTANCE hInstance;
    HWND hMainWindow;
    HWND hWndMonthCalStart;
    HWND hWndMonthCalEnd;
    HWND hWndTimeStartHour;
    HWND hWndTimeStartMinute;
    HWND hWndTimeEndHour;
    HWND hWndTimeEndMinute;
    HWND hWndStartButton;
    HWND hWndStatusText;
    std::chrono::system_clock::time_point startTime;
    std::chrono::system_clock::time_point endTime;
    bool armed;                                        // Start pressed: scheduled or running
    bool isMoving;                                     // inside the active (start..end) window
    bool inMovePhase;                                  // within active window: jiggling vs. pausing
    std::chrono::steady_clock::time_point phaseStart;  // when the current move/pause phase began
    std::mt19937 randomGenerator;
};

std::unique_ptr<AppState> g_appState;

// Forward declarations
ATOM RegisterMainWindowClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow);
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
void CreateControls(HWND hWnd);
void StartMouseMover();
void StopMouseMover();
void UpdateStatusText(const std::wstring& text);
std::chrono::system_clock::time_point GetSelectedDateTime(HWND hWndMonthCal, HWND hWndHour, HWND hWndMinute);
void MoveMouseRandomly();

// Main entry point
int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // Enable DPI awareness
    SetProcessDPIAware();

    // Initialize common controls
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_DATE_CLASSES;
    InitCommonControlsEx(&icex);

    // Initialize application state
    g_appState = std::make_unique<AppState>();
    g_appState->hInstance = hInstance;
    g_appState->armed = false;
    g_appState->isMoving = false;
    g_appState->inMovePhase = false;

    // Initialize random number generator
    std::random_device rd;
    g_appState->randomGenerator = std::mt19937(rd());

    // Register window class
    if (!RegisterMainWindowClass(hInstance)) {
        MessageBox(NULL, L"Window Registration Failed!", L"Error", MB_ICONEXCLAMATION | MB_OK);
        return 1;
    }

    // Initialize the application
    if (!InitInstance(hInstance, nCmdShow)) {
        MessageBox(NULL, L"Window Creation Failed!", L"Error", MB_ICONEXCLAMATION | MB_OK);
        return 1;
    }

    // Main message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}

// Register the window class
ATOM RegisterMainWindowClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_WINDOWSPROJECT1));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    // Change from COLOR_WINDOW+1 to COLOR_BTNFACE
    wcex.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wcex.lpszClassName = L"MouseMoverWindowClass";
    wcex.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

// Initialize the application instance
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    // Create main window
    HWND hWnd = CreateWindowW(
        L"MouseMoverWindowClass",
        L"Mouse Mover",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, 0,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!hWnd) {
        return FALSE;
    }

    g_appState->hMainWindow = hWnd;

    // Create UI controls
    CreateControls(hWnd);

    // Show window
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    return TRUE;
}

// Create all UI controls
void CreateControls(HWND hWnd)
{
    HMENU hMenu = CreateMenu();
    HMENU hFileMenu = CreatePopupMenu();

    // Create labels
    CreateWindow(L"STATIC", L"Start Date:", WS_VISIBLE | WS_CHILD,
        50, 20, 150, 20, hWnd, NULL, g_appState->hInstance, NULL);

    CreateWindow(L"STATIC", L"End Date:", WS_VISIBLE | WS_CHILD,
        50, 200, 150, 20, hWnd, NULL, g_appState->hInstance, NULL);

    CreateWindow(L"STATIC", L"Start Time (HH:MM):", WS_VISIBLE | WS_CHILD,
        50, 380, 150, 20, hWnd, NULL, g_appState->hInstance, NULL);

    CreateWindow(L"STATIC", L"End Time (HH:MM):", WS_VISIBLE | WS_CHILD,
        50, 410, 150, 20, hWnd, NULL, g_appState->hInstance, NULL);

    // Create Start Date Calendar
    g_appState->hWndMonthCalStart = CreateWindowEx(
        0, MONTHCAL_CLASS, L"",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | MCS_NOTODAY,
        50, 40, 300, 150, hWnd, (HMENU)ID_MONTHCAL_START, g_appState->hInstance, NULL
    );

    // Set calendar background color
    COLORREF clrBackground = GetSysColor(COLOR_BTNFACE);
    SendMessage(g_appState->hWndMonthCalStart, MCM_SETCOLOR, MCSC_BACKGROUND, (LPARAM)clrBackground);

    // Create End Date Calendar
    g_appState->hWndMonthCalEnd = CreateWindowEx(
        0, MONTHCAL_CLASS, L"",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | MCS_NOTODAY,
        50, 220, 300, 150, hWnd, (HMENU)ID_MONTHCAL_END, g_appState->hInstance, NULL
    );

    // Set calendar background color
    SendMessage(g_appState->hWndMonthCalEnd, MCM_SETCOLOR, MCSC_BACKGROUND, (LPARAM)clrBackground);

    // For a more consistent look, also set other calendar colors
    SendMessage(g_appState->hWndMonthCalStart, MCM_SETCOLOR, MCSC_MONTHBK, (LPARAM)clrBackground);
    SendMessage(g_appState->hWndMonthCalEnd, MCM_SETCOLOR, MCSC_MONTHBK, (LPARAM)clrBackground);

    // Create time input fields for start time
    g_appState->hWndTimeStartHour = CreateWindow(
        L"EDIT", L"00",
        WS_VISIBLE | WS_CHILD | ES_CENTER | ES_NUMBER | WS_BORDER,
        205, 380, 40, 25, hWnd, (HMENU)ID_TIME_START_HOUR, g_appState->hInstance, NULL
    );

    CreateWindow(L"STATIC", L":", WS_VISIBLE | WS_CHILD,
        250, 382, 10, 20, hWnd, NULL, g_appState->hInstance, NULL);

    g_appState->hWndTimeStartMinute = CreateWindow(
        L"EDIT", L"00",
        WS_VISIBLE | WS_CHILD | ES_CENTER | ES_NUMBER | WS_BORDER,
        260, 380, 40, 25, hWnd, (HMENU)ID_TIME_START_MINUTE, g_appState->hInstance, NULL
    );

    // Create time input fields for end time
    g_appState->hWndTimeEndHour = CreateWindow(
        L"EDIT", L"00",
        WS_VISIBLE | WS_CHILD | ES_CENTER | ES_NUMBER | WS_BORDER,
        205, 410, 40, 25, hWnd, (HMENU)ID_TIME_END_HOUR, g_appState->hInstance, NULL
    );

    CreateWindow(L"STATIC", L":", WS_VISIBLE | WS_CHILD,
        250, 412, 10, 20, hWnd, NULL, g_appState->hInstance, NULL);

    g_appState->hWndTimeEndMinute = CreateWindow(
        L"EDIT", L"00",
        WS_VISIBLE | WS_CHILD | ES_CENTER | ES_NUMBER | WS_BORDER,
        260, 410, 40, 25, hWnd, (HMENU)ID_TIME_END_MINUTE, g_appState->hInstance, NULL
    );

    // Create Start Button
    g_appState->hWndStartButton = CreateWindow(
        L"BUTTON", L"Start Mouse Mover",
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        125, 450, 150, 30, hWnd, (HMENU)ID_BUTTON_MOVE, g_appState->hInstance, NULL
    );

    // Create Status Text
    g_appState->hWndStatusText = CreateWindow(
        L"STATIC", L"Ready",
        WS_VISIBLE | WS_CHILD | SS_CENTER,
        50, 490, 300, 20, hWnd, (HMENU)ID_STATUS_TEXT, g_appState->hInstance, NULL
    );

    // Initialize both calendars with current date
    SYSTEMTIME st;
    GetLocalTime(&st);
    SendMessage(g_appState->hWndMonthCalStart, MCM_SETCURSEL, 0, (LPARAM)&st);
    SendMessage(g_appState->hWndMonthCalEnd, MCM_SETCURSEL, 0, (LPARAM)&st);

    AppendMenu(hFileMenu, MF_STRING, ID_FILE_ABOUT, L"About");
    AppendMenu(hFileMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hFileMenu, MF_STRING, ID_FILE_EXIT, L"Exit");

    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hFileMenu, L"File");
    SetMenu(hWnd, hMenu);
}

// Start the mouse mover
void StartMouseMover()
{
    // Get selected start and end times
    g_appState->startTime = GetSelectedDateTime(
        g_appState->hWndMonthCalStart,
        g_appState->hWndTimeStartHour,
        g_appState->hWndTimeStartMinute
    );

    g_appState->endTime = GetSelectedDateTime(
        g_appState->hWndMonthCalEnd,
        g_appState->hWndTimeEndHour,
        g_appState->hWndTimeEndMinute
    );

    // Validate time range
    if (g_appState->endTime <= g_appState->startTime) {
        MessageBox(g_appState->hMainWindow,
            L"End time must be after start time",
            L"Invalid Time Range", MB_OK | MB_ICONERROR);
        return;
    }
    if (g_appState->endTime <= std::chrono::system_clock::now()) {
        MessageBox(g_appState->hMainWindow,
            L"End time is in the past - nothing to schedule.",
            L"Invalid Time Range", MB_OK | MB_ICONERROR);
        return;
    }

    // Arm and update UI
    g_appState->armed = true;
    SetWindowText(g_appState->hWndStartButton, L"Stop Mouse Mover");
    UpdateStatusText(L"Scheduled - Waiting for start time");

    // Start checking if it's time to move the mouse
    SetTimer(g_appState->hMainWindow, IDT_TIMER_CHECK, CHECK_TIME_INTERVAL_MS, NULL);
}

// Stop the mouse mover
void StopMouseMover()
{
    // Kill all timers
    KillTimer(g_appState->hMainWindow, IDT_TIMER_CHECK);
    KillTimer(g_appState->hMainWindow, IDT_TIMER_MOVE);

    // Update state
    g_appState->armed = false;
    g_appState->isMoving = false;
    g_appState->inMovePhase = false;

    // Update UI
    SetWindowText(g_appState->hWndStartButton, L"Start Mouse Mover");
    UpdateStatusText(L"Ready");
}

// Update the status text
void UpdateStatusText(const std::wstring& text)
{
    SetWindowText(g_appState->hWndStatusText, text.c_str());
}

// Get date/time from UI controls
std::chrono::system_clock::time_point GetSelectedDateTime(
    HWND hWndMonthCal, HWND hWndHour, HWND hWndMinute)
{
    // Get date from calendar
    SYSTEMTIME st;
    SendMessage(hWndMonthCal, MCM_GETCURSEL, 0, (LPARAM)&st);

    // Get time from inputs
    wchar_t hourText[3], minuteText[3];
    GetWindowText(hWndHour, hourText, 3);
    GetWindowText(hWndMinute, minuteText, 3);

    int hour = _wtoi(hourText);
    int minute = _wtoi(minuteText);

    // Validate time
    if (hour < 0 || hour > 23) hour = 0;
    if (minute < 0 || minute > 59) minute = 0;

    // Set time
    st.wHour = static_cast<WORD>(hour);
    st.wMinute = static_cast<WORD>(minute);
    st.wSecond = 0;
    st.wMilliseconds = 0;

    // Convert to time_point using struct tm and mktime
    struct tm timeinfo = {};
    timeinfo.tm_year = st.wYear - 1900;  // struct tm years are years since 1900
    timeinfo.tm_mon = st.wMonth - 1;     // struct tm months are 0-based
    timeinfo.tm_mday = st.wDay;
    timeinfo.tm_hour = st.wHour;
    timeinfo.tm_min = st.wMinute;
    timeinfo.tm_sec = st.wSecond;
    timeinfo.tm_isdst = -1;              // Let system determine DST status

    // Convert to time_t, then to time_point
    time_t timeT = mktime(&timeinfo);
    return std::chrono::system_clock::from_time_t(timeT);
}

// Move the mouse randomly
void MoveMouseRandomly()
{
    // Calculate random movement
    std::uniform_int_distribution<> distr(-15, 15);
    int moveX = distr(g_appState->randomGenerator);
    int moveY = distr(g_appState->randomGenerator);

    // Move more drastically every few moves to avoid getting stuck
    std::uniform_int_distribution<> big_move(0, 20);
    if (big_move(g_appState->randomGenerator) == 0) {
        std::uniform_int_distribution<> large_distr(-100, 100);
        moveX = large_distr(g_appState->randomGenerator);
        moveY = large_distr(g_appState->randomGenerator);
    }

    // Keep the cursor on the primary screen: clamp the target and send the
    // clamped delta, so repeated relative moves can't drift into a corner.
    POINT pt;
    if (GetCursorPos(&pt)) {
        constexpr int margin = 4;
        const int screenW = GetSystemMetrics(SM_CXSCREEN);
        const int screenH = GetSystemMetrics(SM_CYSCREEN);
        int targetX = pt.x + moveX;
        int targetY = pt.y + moveY;
        if (targetX < margin) targetX = margin;
        if (targetY < margin) targetY = margin;
        if (targetX > screenW - 1 - margin) targetX = screenW - 1 - margin;
        if (targetY > screenH - 1 - margin) targetY = screenH - 1 - margin;
        moveX = targetX - pt.x;
        moveY = targetY - pt.y;
    }

    // Create and send mouse input
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dx = moveX;
    input.mi.dy = moveY;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &input, sizeof(input));
}

// Function to show About dialog
void ShowAboutDialog(HWND hWnd) {
    DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, [](HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) -> INT_PTR {
        switch (message) {
        case WM_INITDIALOG:
            return (INT_PTR)TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
                EndDialog(hDlg, LOWORD(wParam));
                return (INT_PTR)TRUE;
            }
            break;
        }
        return (INT_PTR)FALSE;
        });
}

// Window procedure
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
            case ID_FILE_ABOUT:
                ShowAboutDialog(hWnd);
                break;
            case ID_FILE_EXIT:
                PostMessage(hWnd, WM_CLOSE, 0, 0);
                break;
            case ID_BUTTON_MOVE:
                if (!g_appState->armed) {
                    StartMouseMover();
                }
                else {
                    StopMouseMover();
                }
                break;
        }
        break;

    case WM_TIMER:
        if (wParam == IDT_TIMER_CHECK) {
            auto now = std::chrono::system_clock::now();

            // Enter the active window: begin jiggling.
            if (now >= g_appState->startTime && now < g_appState->endTime) {
                if (!g_appState->isMoving) {
                    g_appState->isMoving = true;
                    g_appState->inMovePhase = true;
                    g_appState->phaseStart = std::chrono::steady_clock::now();
                    UpdateStatusText(L"Active - Moving mouse");
                    SetTimer(hWnd, IDT_TIMER_MOVE, MOUSE_MOVE_INTERVAL_MS, NULL);
                }
            }
            // Past the end time: stop.
            else if (now >= g_appState->endTime) {
                StopMouseMover();
            }
        }
        else if (wParam == IDT_TIMER_MOVE) {
            // Stop as soon as the scheduled end time passes.
            if (std::chrono::system_clock::now() >= g_appState->endTime) {
                StopMouseMover();
                return 0;
            }

            // Drive the move/pause cycle off a monotonic clock, so the phase
            // durations are accurate regardless of the timer's real resolution.
            auto elapsed = std::chrono::steady_clock::now() - g_appState->phaseStart;
            if (g_appState->inMovePhase) {
                if (elapsed < std::chrono::milliseconds(MOUSE_MOVE_DURATION_MS)) {
                    MoveMouseRandomly();
                }
                else {
                    g_appState->inMovePhase = false;
                    g_appState->phaseStart = std::chrono::steady_clock::now();
                    UpdateStatusText(L"Active - Pausing");
                }
            }
            else if (elapsed >= std::chrono::milliseconds(MOUSE_PAUSE_DURATION_MS)) {
                g_appState->inMovePhase = true;
                g_appState->phaseStart = std::chrono::steady_clock::now();
                UpdateStatusText(L"Active - Moving mouse");
            }
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hWnd);
        break;

    case WM_DESTROY:
        StopMouseMover();
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}