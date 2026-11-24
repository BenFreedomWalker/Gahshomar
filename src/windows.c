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
#include <wchar.h>
#include <stdlib.h>
#include <string.h>

#include "gahshomar/calendar.h"

#define WINDOW_CLASS_NAME L"GahshomarWindow"
#define WINDOW_TITLE      L"گاه‌شمار"
#define TIMER_ID          1
#define TIMER_INTERVAL_MS 60000

static HDC g_dc;
static HWND g_hwnd;
static HGLRC g_gl;
static GhDate g_today;
static GhDate g_view;
static int g_client_width = 800;
static int g_client_height = 600;
static HFONT g_font_title;
static HFONT g_font_body;
static HFONT g_font_small;

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
    RECT text_rect;
    wchar_t buffer[128];
    wchar_t day_text[8];
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
    int start_day;
    HDC dc;
    HBITMAP text_bitmap;
    HGDIOBJ old_bitmap;
    BITMAPINFO bitmap_info;
    void *dib_bits = NULL;
    unsigned char *rgba;
    size_t pixel_count;
    int px, py;
    GLuint text_texture;
    unsigned char *texture_pixels;
    int texture_width, texture_height, tx;

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

    /*
     * Find the weekday of the first day of the displayed month. tm_wday
     * uses Sunday=0; the Persian calendar grid starts on Saturday.
     */
    {
        time_t now = time(NULL);
        struct tm local_tm;
#if defined(_WIN32)
        if (localtime_s(&local_tm, &now) == 0) {
#else
        if (localtime_r(&now, &local_tm) != NULL) {
#endif
            int weekday = (local_tm.tm_wday - ((g_today.day - 1) % 7) + 7) % 7;
            int m = g_today.month;
            int y = g_today.year;
            int current_index = y * 12 + m - 1;
            int target_index = g_view.year * 12 + g_view.month - 1;
            while (current_index < target_index) {
                weekday = (weekday + gh_days_in_month(y, m)) % 7;
                if (++m == 13) { m = 1; ++y; }
                ++current_index;
            }
            while (current_index > target_index) {
                if (--m == 0) { m = 12; --y; }
                weekday = (weekday - (gh_days_in_month(y, m) % 7) + 7) % 7;
                --current_index;
            }
            first_weekday = weekday;
        }
    }

    cell_w = grid_w / 7;
    cell_h = grid_h / 6;
    start_day = (first_weekday + 1) % 7;

    /* Draw all calendar cells before swapping the OpenGL back buffer. */
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
        }
    }

    /*
     * Render text into an off-screen 32-bit DIB. GDI performs Unicode
     * shaping and RTL layout; OpenGL then composites the resulting pixels
     * into its back buffer before the buffer swap.
     */
    dc = CreateCompatibleDC(NULL);
    if (!dc) {
        SwapBuffers(g_dc);
        return;
    }

    ZeroMemory(&bitmap_info, sizeof(bitmap_info));
    bitmap_info.bmiHeader.biSize = sizeof(bitmap_info.bmiHeader);
    bitmap_info.bmiHeader.biWidth = width;
    bitmap_info.bmiHeader.biHeight = -height; /* top-down DIB */
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;
    text_bitmap = CreateDIBSection(dc, &bitmap_info, DIB_RGB_COLORS,
                                   &dib_bits, NULL, 0);
    if (!text_bitmap || !dib_bits) {
        if (text_bitmap) DeleteObject(text_bitmap);
        DeleteDC(dc);
        SwapBuffers(g_dc);
        return;
    }

    old_bitmap = SelectObject(dc, text_bitmap);
    PatBlt(dc, 0, 0, width, height, BLACKNESS);

    text_rect.left = 48; text_rect.top = 22;
    text_rect.right = width - 48; text_rect.bottom = 68;
    draw_text(dc, L"گاه‌شمار", text_rect, g_font_title,
              RGB(239, 205, 133), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]),
             L"%d / %d / %d", g_today.day, g_today.month, g_today.year);
    text_rect.top = 76; text_rect.bottom = 120;
    draw_text(dc, buffer, text_rect, g_font_body,
              RGB(238, 240, 244), DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    {
        wchar_t month_name[64];
        wchar_t heading[128];
        utf8_to_wide(gh_month_name(g_view.month), month_name,
                     (int)(sizeof(month_name) / sizeof(month_name[0])));
        swprintf(heading, sizeof(heading) / sizeof(heading[0]),
                 L"%ls  %d", month_name, g_view.year);
        text_rect.left = 48; text_rect.top = 160;
        text_rect.right = width - 230; text_rect.bottom = 190;
        draw_text(dc, heading, text_rect, g_font_body,
                  RGB(239, 205, 133), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    text_rect.left = width - 220; text_rect.top = 160;
    text_rect.right = width - 48; text_rect.bottom = 190;
    draw_text(dc, L"‹ قبلی       بعدی ›", text_rect, g_font_small,
              RGB(177, 190, 207), DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

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

    for (day = 1; day <= days; ++day) {
        int index = start_day + day - 1;
        row = index / 7;
        col = index % 7;
        swprintf(day_text, sizeof(day_text) / sizeof(day_text[0]),
                 L"%d", day);
        text_rect.left = grid_x + col * cell_w + 5;
        text_rect.top = grid_y + 36 + row * cell_h + 8;
        text_rect.right = text_rect.left + cell_w - 10;
        text_rect.bottom = grid_y + 36 + (row + 1) * cell_h - 8;
        draw_text(dc, day_text, text_rect, g_font_body,
                  (today_is_viewed_month && day == g_today.day)
                    ? RGB(255, 225, 170) : RGB(226, 232, 240),
                  DT_CENTER | DT_TOP | DT_SINGLELINE);
    }

    /*
     * GDI's 32-bit DIB is BGRA with an unused alpha byte. Its RGB values
     * represent antialiased text on black, so derive coverage from the
     * strongest channel and un-premultiply the color for GL blending.
     * Reverse the rows because glDrawPixels consumes bottom-up image data.
     */
    pixel_count = (size_t)width * (size_t)height;
    rgba = (unsigned char *)malloc(pixel_count * 4u);
    if (rgba) {
        const unsigned char *source = (const unsigned char *)dib_bits;
        for (py = 0; py < height; ++py) {
            int source_y = height - 1 - py;
            for (px = 0; px < width; ++px) {
                size_t src = ((size_t)source_y * (size_t)width + (size_t)px) * 4u;
                size_t dst = ((size_t)py * (size_t)width + (size_t)px) * 4u;
                unsigned int blue = source[src];
                unsigned int green = source[src + 1];
                unsigned int red = source[src + 2];
                unsigned int alpha = red > green ? red : green;
                if (blue > alpha) alpha = blue;

                if (alpha == 0) {
                    rgba[dst] = rgba[dst + 1] = rgba[dst + 2] = rgba[dst + 3] = 0;
                } else {
                    rgba[dst] = (unsigned char)((red * 255u + alpha / 2u) / alpha);
                    rgba[dst + 1] = (unsigned char)((green * 255u + alpha / 2u) / alpha);
                    rgba[dst + 2] = (unsigned char)((blue * 255u + alpha / 2u) / alpha);
                    rgba[dst + 3] = (unsigned char)alpha;
                }
            }
        }

        /*
         * OpenGL 1.1 requires power-of-two texture dimensions. Pad the
         * client-sized overlay so it also works on older Windows drivers.
         */
        texture_width = 1;
        texture_height = 1;
        while (texture_width < width) texture_width <<= 1;
        while (texture_height < height) texture_height <<= 1;
        texture_pixels = (unsigned char *)calloc(
            (size_t)texture_width * (size_t)texture_height, 4u);
        if (texture_pixels) {
            for (py = 0; py < height; ++py) {
                memcpy(texture_pixels + (size_t)py * (size_t)texture_width * 4u,
                       rgba + (size_t)py * (size_t)width * 4u,
                       (size_t)width * 4u);
            }
        }

        /*
         * Composite the overlay as a temporary OpenGL 1.1 texture. A
         * textured quad avoids raster-position clipping at the window edge.
         */
        if (texture_pixels) {
        glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT |
                     GL_CURRENT_BIT | GL_TEXTURE_BIT);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_TEXTURE_2D);
        glGenTextures(1, &text_texture);
        glBindTexture(GL_TEXTURE_2D, text_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                     texture_width, texture_height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, texture_pixels);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, (float)height / (float)texture_height);
        glVertex2f(0.0f, 0.0f);
        glTexCoord2f((float)width / (float)texture_width,
                     (float)height / (float)texture_height);
        glVertex2f((float)width, 0.0f);
        glTexCoord2f((float)width / (float)texture_width, 0.0f);
        glVertex2f((float)width, (float)height);
        glTexCoord2f(0.0f, 0.0f);
        glVertex2f(0.0f, (float)height);
        glEnd();
        glDeleteTextures(1, &text_texture);
        glPopAttrib();
        free(texture_pixels);
        }
        free(rgba);
    }

    SelectObject(dc, old_bitmap);
    DeleteObject(text_bitmap);
    DeleteDC(dc);
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
        g_hwnd = hwnd;
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
