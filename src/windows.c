/*
 * Gahshomar Windows desktop frontend.
 *
 * OpenGL 1.1 draws the interface geometry. GDI renders text so Windows can
 * perform Persian glyph shaping and right-to-left layout without extra
 * runtime dependencies.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <time.h>
#include <stdio.h>
#include <wchar.h>

#include "gahshomar/calendar.h"

#define WINDOW_CLASS_NAME L"GahshomarWindow"
#define WINDOW_TITLE      L"گاه‌شمار"
#define TIMER_ID          1
#define TIMER_INTERVAL_MS 60000

static HDC g_dc;
static HGLRC g_gl;
static GhDate g_today;
static GhDate g_view;
static int g_client_width = 800;
static int g_client_height = 600;
static HFONT g_font_title;
static HFONT g_font_body;
static HFONT g_font_small;

static int is_leap_gregorian(int year)
{
    return (year % 400 == 0) || ((year % 4 == 0) && (year % 100 != 0));
}

static int days_in_gregorian_month(int year, int month)
{
    static const int lengths[12] =
        {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && is_leap_gregorian(year)) return 29;
    return lengths[month - 1];
}

static void refresh_today(void)
{
    (void)gh_today(&g_today);
}

static void move_month(int amount)
{
    int absolute = g_view.year * 12 + (g_view.month - 1) + amount;
    g_view.year = absolute / 12;
    g_view.month = absolute % 12 + 1;
    if (g_view.month <= 0) {
        g_view.month += 12;
        --g_view.year;
    }
    if (g_view.day > gh_days_in_month(g_view.year, g_view.month))
        g_view.day = gh_days_in_month(g_view.year, g_view.month);
}

static void draw_rect(float x, float y, float w, float h,
                      float r, float g, float b)
{
    glColor3f(r, g, b);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
}

static void draw_outline(float x, float y, float w, float h,
                         float r, float g, float b)
{
    glColor3f(r, g, b);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
}

static void draw_text(HDC dc, const wchar_t *text, RECT rect,
                      HFONT font, COLORREF color, UINT flags)
{
    HFONT old_font = (HFONT)SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text, -1, &rect, flags | DT_NOPREFIX);
    SelectObject(dc, old_font);
}

static void utf8_to_wide(const char *source, wchar_t *dest, int capacity)
{
    int count;
    if (capacity <= 0) return;
    count = MultiByteToWideChar(CP_UTF8, 0, source, -1, dest, capacity);
    if (count == 0) dest[0] = L'\0';
}

static void render_calendar(void)
{
    RECT client;
    RECT text_rect;
    wchar_t buffer[128];
    wchar_t day_text[8];
    char month_utf8[64];
    int width = g_client_width;
    int height = g_client_height;
    int days = gh_days_in_month(g_view.year, g_view.month);
    int first_weekday = 0;
    int today_is_viewed_month =
        (g_today.year == g_view.year && g_today.month == g_view.month);
    int row, col, day;
    int cell_w, cell_h;
    int grid_x = 48, grid_y = 190;
    int grid_w = width - 96;
    int grid_h = height - 230;
    int start_day = 0;
    HDC dc;

    if (days <= 0 || width < 360 || height < 360) return;

    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, width, height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glClearColor(0.055f, 0.075f, 0.11f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    draw_rect(0, 0, (float)width, 142, 0.075f, 0.105f, 0.15f);
    draw_rect(48, 151, (float)(width - 96), 1, 0.22f, 0.29f, 0.38f);

    dc = GetDC(GetActiveWindow());
    if (!dc) {
        SwapBuffers(g_dc);
        return;
    }
    GetClientRect(GetActiveWindow(), &client);

    /* Title and current date. */
    text_rect.left = 48; text_rect.top = 22;
    text_rect.right = width - 48; text_rect.bottom = 68;
    draw_text(dc, L"گاه‌شمار", text_rect, g_font_title,
              RGB(239, 205, 133), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]),
             L"%d / %d / %d", g_today.day, g_today.month, g_today.year);
    text_rect.top = 76; text_rect.bottom = 120;
    draw_text(dc, buffer, text_rect, g_font_body,
              RGB(238, 240, 244), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    /* Month heading and navigation hints. */
    utf8_to_wide(gh_month_name(g_view.month), buffer,
                 (int)(sizeof(buffer) / sizeof(buffer[0])));
    swprintf(day_text, sizeof(day_text) / sizeof(day_text[0]),
             L"%d", g_view.year);
    {
        wchar_t heading[128];
        swprintf(heading, sizeof(heading) / sizeof(heading[0]),
                 L"%ls  %ls", buffer, day_text);
        text_rect.left = 48; text_rect.top = 160;
        text_rect.right = width - 48; text_rect.bottom = 190;
        draw_text(dc, heading, text_rect, g_font_body,
                  RGB(239, 205, 133), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    text_rect.left = width - 210; text_rect.right = width - 48;
    draw_text(dc, L"‹ قبلی       بعدی ›", text_rect, g_font_small,
              RGB(177, 190, 207), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    /*
     * Derive the weekday of the first day of the displayed month from the
     * operating system's local weekday and today's position in this month.
     */
    {
        time_t now = time(NULL);
        struct tm local_tm;
#if defined(_WIN32)
        if (localtime_s(&local_tm, &now) == 0) {
#else
        if (localtime_r(&now, &local_tm) != NULL) {
#endif
            int offset = g_today.day - 1;
            int weekday = (local_tm.tm_wday - (offset % 7) + 7) % 7;
            int m = g_today.month;
            int y = g_today.year;
            while (y != g_view.year || m != g_view.month) {
                if (y * 12 + m - 1 < g_view.year * 12 + g_view.month - 1) {
                    weekday = (weekday + gh_days_in_month(y, m)) % 7;
                    if (++m == 13) { m = 1; ++y; }
                } else {
                    if (--m == 0) { m = 12; --y; }
                    weekday = (weekday - gh_days_in_month(y, m) % 7 + 7) % 7;
                }
            }
            first_weekday = weekday;
        }
    }

    cell_w = grid_w / 7;
    cell_h = grid_h / 6;

    /* Weekday headings, ordered from Saturday for the Persian week. */
    {
        static const wchar_t *weekdays[7] =
            {L"ش", L"ی", L"د", L"س", L"چ", L"پ", L"ج"};
        for (col = 0; col < 7; ++col) {
            text_rect.left = grid_x + col * cell_w;
            text_rect.right = text_rect.left + cell_w;
            text_rect.top = grid_y;
            text_rect.bottom = grid_y + 30;
            draw_text(dc, weekdays[col], text_rect, g_font_small,
                      RGB(143, 160, 181), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    /* Convert Sunday-based weekday to a Saturday-first column index. */
    start_day = (first_weekday + 1) % 7;
    for (day = 1; day <= days; ++day) {
        int index = start_day + day - 1;
        row = index / 7;
        col = index % 7;
        {
            float x = (float)(grid_x + col * cell_w + 5);
            float y = (float)(grid_y + 36 + row * cell_h);
            float w = (float)(cell_w - 10);
            float h = (float)(cell_h - 8);
            int is_today = today_is_viewed_month && day == g_today.day;
            if (is_today)
                draw_rect(x, y, w, h, 0.34f, 0.24f, 0.12f);
            else
                draw_rect(x, y, w, h, 0.085f, 0.11f, 0.16f);
            draw_outline(x, y, w, h,
                         is_today ? 0.78f : 0.18f,
                         is_today ? 0.57f : 0.23f,
                         is_today ? 0.29f : 0.31f);
            swprintf(day_text, sizeof(day_text) / sizeof(day_text[0]),
                     L"%d", day);
            text_rect.left = (LONG)x;
            text_rect.top = (LONG)y + 8;
            text_rect.right = (LONG)(x + w);
            text_rect.bottom = (LONG)(y + h);
            draw_text(dc, day_text, text_rect, g_font_body,
                      is_today ? RGB(255, 225, 170) : RGB(226, 232, 240),
                      DT_CENTER | DT_TOP | DT_SINGLELINE);
        }
    }

    ReleaseDC(GetActiveWindow(), dc);
    SwapBuffers(g_dc);
}

static void create_fonts(HWND hwnd)
{
    g_font_title = CreateFontW(30, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_font_body = CreateFontW(21, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_font_small = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    (void)hwnd;
}

static void destroy_graphics(HWND hwnd)
{
    if (g_gl) {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(g_gl);
        g_gl = NULL;
    }
    if (g_dc) {
        ReleaseDC(hwnd, g_dc);
        g_dc = NULL;
    }
    if (g_font_title) DeleteObject(g_font_title);
    if (g_font_body) DeleteObject(g_font_body);
    if (g_font_small) DeleteObject(g_font_small);
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message,
                                    WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_CREATE:
        g_dc = GetDC(hwnd);
        {
            PIXELFORMATDESCRIPTOR pfd;
            int format;
            ZeroMemory(&pfd, sizeof(pfd));
            pfd.nSize = sizeof(pfd);
            pfd.nVersion = 1;
            pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
            pfd.iPixelType = PFD_TYPE_RGBA;
            pfd.cColorBits = 24;
            pfd.cDepthBits = 16;
            pfd.iLayerType = PFD_MAIN_PLANE;
            format = ChoosePixelFormat(g_dc, &pfd);
            if (!format || !SetPixelFormat(g_dc, format, &pfd)) return -1;
            g_gl = wglCreateContext(g_dc);
            if (!g_gl || !wglMakeCurrent(g_dc, g_gl)) return -1;
        }
        create_fonts(hwnd);
        refresh_today();
        g_view = g_today;
        SetTimer(hwnd, TIMER_ID, TIMER_INTERVAL_MS, NULL);
        return 0;

    case WM_SIZE:
        g_client_width = LOWORD(lparam);
        g_client_height = HIWORD(lparam);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_TIMER:
        refresh_today();
        if (g_view.year == 0) g_view = g_today;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_LBUTTONDOWN:
        if (HIWORD(lparam) < 205 && HIWORD(lparam) > 155) {
            int x = LOWORD(lparam);
            if (x > g_client_width - 220) {
                if (x > g_client_width - 120) move_month(1);
                else move_month(-1);
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;

    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            render_calendar();
            EndPaint(hwnd, &ps);
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_ID);
        destroy_graphics(hwnd);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous,
                   LPSTR command_line, int show_command)
{
    WNDCLASSW wc;
    HWND hwnd;
    MSG message;
    (void)previous;
    (void)command_line;

    ZeroMemory(&wc, sizeof(wc));
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = WINDOW_CLASS_NAME;
    if (!RegisterClassW(&wc)) return 1;

    hwnd = CreateWindowExW(0, WINDOW_CLASS_NAME, WINDOW_TITLE,
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 820, 660,
        NULL, NULL, instance, NULL);
    if (!hwnd) return 1;

    ShowWindow(hwnd, show_command);
    UpdateWindow(hwnd);

    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return (int)message.wParam;
}
