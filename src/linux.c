/*
 * Gahshomar Linux desktop frontend.
 *
 * Native backends:
 *   - X11 + GLX
 *   - Wayland + xdg-shell + EGL (when built with GH_HAVE_WAYLAND)
 *
 * Both backends feed the same OpenGL 1.1 renderer and calendar core.
 * Pango/Cairo handles Unicode shaping and right-to-left Persian text.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#include <GL/gl.h>
#include <cairo/cairo.h>
#include <pango/pangocairo.h>

#include "gahshomar/calendar.h"
#include "faravahar.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <GL/glx.h>

#ifdef GH_HAVE_WAYLAND
#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include "xdg-shell-client-protocol.h"
#endif

#define INITIAL_WIDTH  820
#define INITIAL_HEIGHT 660
#define HEADER_HEIGHT 142
#define TIMER_SECONDS 60

typedef struct GhFrontend {
    int width;
    int height;
    GhDate today;
    GhDate view;
    int running;
    int dirty;
} GhFrontend;

static GhFrontend g_ui = {
    INITIAL_WIDTH, INITIAL_HEIGHT, {0, 0, 0}, {0, 0, 0}, 1, 1
};

static void refresh_today(void)
{
    (void)gh_today(&g_ui.today);
    if (g_ui.view.year == 0)
        g_ui.view = g_ui.today;
}

static void move_month(int amount)
{
    int absolute = g_ui.view.year * 12 + (g_ui.view.month - 1) + amount;
    g_ui.view.year = absolute / 12;
    g_ui.view.month = absolute % 12 + 1;
    if (g_ui.view.month <= 0) {
        g_ui.view.month += 12;
        --g_ui.view.year;
    }
    if (g_ui.view.day > gh_days_in_month(g_ui.view.year, g_ui.view.month))
        g_ui.view.day = gh_days_in_month(g_ui.view.year, g_ui.view.month);
    g_ui.dirty = 1;
}

static int first_weekday_of_view(void)
{
    time_t now = time(NULL);
    struct tm local_date;
    int weekday;
    int year;
    int month;
    int current_index;
    int target_index;

    if (now == (time_t)-1 || localtime_r(&now, &local_date) == NULL)
        return 0;

    weekday = (local_date.tm_wday - ((g_ui.today.day - 1) % 7) + 7) % 7;
    year = g_ui.today.year;
    month = g_ui.today.month;
    current_index = year * 12 + month - 1;
    target_index = g_ui.view.year * 12 + g_ui.view.month - 1;

    while (current_index < target_index) {
        weekday = (weekday + gh_days_in_month(year, month)) % 7;
        if (++month == 13) {
            month = 1;
            ++year;
        }
        ++current_index;
    }
    while (current_index > target_index) {
        if (--month == 0) {
            month = 12;
            --year;
        }
        weekday = (weekday - (gh_days_in_month(year, month) % 7) + 7) % 7;
        --current_index;
    }

    /* tm_wday: Sunday=0; the calendar's first column is Saturday. */
    return (weekday + 1) % 7;
}

static void color3(float r, float g, float b)
{
    glColor3f(r, g, b);
}

static void fill_rect(float x, float y, float w, float h,
                      float r, float g, float b)
{
    color3(r, g, b);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
}

static void outline_rect(float x, float y, float w, float h,
                         float r, float g, float b)
{
    color3(r, g, b);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
}

static int base64_value(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static void draw_mark(void)
{
    static GLuint texture;
    static int initialized;
    unsigned char pixels[64 * 32 * 4];
    unsigned char packed[(FARAVAHAR_WIDTH * FARAVAHAR_HEIGHT + 7) / 8];
    unsigned int accumulator = 0;
    int bits = 0;
    size_t output = 0;
    size_t i;
    const char *source = faravahar_alpha_base64;

    if (!initialized) {
        memset(pixels, 0, sizeof(pixels));
        while (*source && output < sizeof(packed)) {
            int value = base64_value(*source++);
            if (value < 0) continue;
            accumulator = (accumulator << 6) | (unsigned int)value;
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                packed[output++] = (unsigned char)(accumulator >> bits);
            }
        }
        if (output != sizeof(packed))
            return;
        for (i = 0; i < FARAVAHAR_WIDTH * FARAVAHAR_HEIGHT; ++i) {
            size_t p = i * 4u;
            unsigned char alpha =
                (packed[i / 8u] & (0x80u >> (i % 8u))) ? 255u : 0u;
            pixels[p] = 224;
            pixels[p + 1] = 176;
            pixels[p + 2] = 90;
            pixels[p + 3] = alpha;
        }
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 32, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        initialized = 1;
    }

    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_TEXTURE_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(48, 27);
    glTexCoord2f(1, 0); glVertex2f(112, 27);
    glTexCoord2f(1, (float)FARAVAHAR_HEIGHT / 32.0f);
    glVertex2f(112, 57);
    glTexCoord2f(0, (float)FARAVAHAR_HEIGHT / 32.0f);
    glVertex2f(48, 57);
    glEnd();
    glPopAttrib();
}

static void draw_text(cairo_t *cr, const char *text,
                      double x, double y, double width, double height,
                      double font_size, const char *color,
                      PangoAlignment alignment)
{
    PangoLayout *layout;
    PangoFontDescription *font;
    double red, green, blue;

    layout = pango_cairo_create_layout(cr);
    font = pango_font_description_from_string("Noto Sans Arabic");
    pango_font_description_set_absolute_size(font, font_size * PANGO_SCALE);
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, text, -1);
    pango_layout_set_width(layout, (int)(width * PANGO_SCALE));
    pango_layout_set_height(layout, (int)(height * PANGO_SCALE));
    pango_layout_set_alignment(layout, alignment);
    pango_layout_set_single_paragraph_mode(layout, TRUE);

    red = green = blue = 1.0;
    {
        unsigned int r8 = 255, g8 = 255, b8 = 255;
        if (sscanf(color, "#%02x%02x%02x", &r8, &g8, &b8) == 3) {
            red = r8 / 255.0;
            green = g8 / 255.0;
            blue = b8 / 255.0;
        }
    }

    cairo_set_source_rgba(cr, red, green, blue, 1.0);
    cairo_move_to(cr, x, y);
    pango_cairo_update_layout(cr, layout);
    pango_cairo_show_layout(cr, layout);

    pango_font_description_free(font);
    g_object_unref(layout);
}

static void draw_text_overlay(int width, int height)
{
    cairo_surface_t *surface;
    cairo_t *cr;
    unsigned char *source;
    unsigned char *rgba;
    size_t count;
    int x, y;
    int days = gh_days_in_month(g_ui.view.year, g_ui.view.month);
    int first = first_weekday_of_view();
    int cell_w = (width - 96) / 7;
    int cell_h = (height - 230) / 6;
    int grid_x = 48;
    int grid_y = 190;
    int today_month = g_ui.today.year == g_ui.view.year &&
                      g_ui.today.month == g_ui.view.month;
    char buffer[128];
    static const char *weekdays[7] = {
        "ش", "ی", "د", "س", "چ", "پ", "ج"
    };

    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cr = cairo_create(surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    draw_text(cr, "گاه‌شمار", 126, 22, width - 174, 46,
              30, "#efcd85", PANGO_ALIGN_LEFT);

    {
        char day[32], month[32], year[32];
        if (gh_format_number(g_ui.today.day, day, sizeof(day)) &&
            gh_format_number(g_ui.today.month, month, sizeof(month)) &&
            gh_format_number(g_ui.today.year, year, sizeof(year)))
            (void)snprintf(buffer, sizeof(buffer), "%s / %s / %s",
                           day, month, year);
        else
            buffer[0] = '\\0';
    }
    draw_text(cr, buffer, 126, 76, width - 174, 42,
              21, "#eef0f4", PANGO_ALIGN_LEFT);

    {
        char year[32];
        if (gh_format_number(g_ui.view.year, year, sizeof(year)))
            (void)snprintf(buffer, sizeof(buffer), "%s  %s",
                           gh_month_name(g_ui.view.month), year);
        else
            buffer[0] = '\\0';
    }
    draw_text(cr, buffer, 48, 155, width - 280, 36,
              21, "#efcd85", PANGO_ALIGN_LEFT);
    draw_text(cr, "بعدی ›", width - 220, 155, 82, 36,
              15, "#b1becf", PANGO_ALIGN_CENTER);
    draw_text(cr, "‹ قبلی", width - 132, 155, 84, 36,
              15, "#b1becf", PANGO_ALIGN_CENTER);

    for (x = 0; x < 7; ++x) {
        draw_text(cr, weekdays[x], grid_x + x * cell_w, grid_y,
                  cell_w, 30, 15, "#8fa0b5", PANGO_ALIGN_CENTER);
    }

    for (y = 1; y <= days; ++y) {
        int index = first + y - 1;
        int row = index / 7;
        int col = index % 7;
        if (!gh_format_number(y, buffer, sizeof(buffer)))
            buffer[0] = '\\0';
        draw_text(cr, buffer,
                  grid_x + col * cell_w + 5,
                  grid_y + 36 + row * cell_h + 8,
                  cell_w - 10, cell_h - 16, 21,
                  today_month && y == g_ui.today.day
                    ? "#ffe1aa" : "#e2e8f0",
                  PANGO_ALIGN_CENTER);
    }

    cairo_destroy(cr);
    cairo_surface_flush(surface);
    source = cairo_image_surface_get_data(surface);
    count = (size_t)width * (size_t)height;
    rgba = (unsigned char *)malloc(count * 4u);
    if (rgba != NULL) {
        for (y = 0; y < height; ++y) {
            for (x = 0; x < width; ++x) {
                size_t i = ((size_t)y * (size_t)width + (size_t)x) * 4u;
                /* Cairo ARGB32 is BGRA in memory on little-endian systems. */
                rgba[i]     = source[i + 2];
                rgba[i + 1] = source[i + 1];
                rgba[i + 2] = source[i];
                rgba[i + 3] = source[i + 3];
            }
        }

        glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_TEXTURE_BIT);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_TEXTURE_2D);
        {
            GLuint texture;
            int tw = 1, th = 1;
            unsigned char *padded;
            while (tw < width) tw <<= 1;
            while (th < height) th <<= 1;
            padded = (unsigned char *)calloc((size_t)tw * (size_t)th, 4u);
            if (padded != NULL) {
                for (y = 0; y < height; ++y)
                    memcpy(padded + (size_t)y * (size_t)tw * 4u,
                           rgba + (size_t)y * (size_t)width * 4u,
                           (size_t)width * 4u);
                glGenTextures(1, &texture);
                glBindTexture(GL_TEXTURE_2D, texture);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0,
                             GL_RGBA, GL_UNSIGNED_BYTE, padded);
                glColor4f(1, 1, 1, 1);
                glBegin(GL_QUADS);
                glTexCoord2f(0, 0); glVertex2f(0, 0);
                glTexCoord2f((float)width / tw, 0);
                glVertex2f((float)width, 0);
                glTexCoord2f((float)width / tw, (float)height / th);
                glVertex2f((float)width, (float)height);
                glTexCoord2f(0, (float)height / th); glVertex2f(0, (float)height);
                glEnd();
                glDeleteTextures(1, &texture);
                free(padded);
            }
        }
        glPopAttrib();
        free(rgba);
    }
    cairo_surface_destroy(surface);
}

static void render(void)
{
    int width = g_ui.width;
    int height = g_ui.height;
    int days = gh_days_in_month(g_ui.view.year, g_ui.view.month);
    int first = first_weekday_of_view();
    int cell_w = (width - 96) / 7;
    int cell_h = (height - 230) / 6;
    int day;

    if (width < 360 || height < 360 || days <= 0)
        return;

    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, width, height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glClearColor(0.055f, 0.075f, 0.11f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    fill_rect(0, 0, (float)width, HEADER_HEIGHT, 0.075f, 0.105f, 0.15f);
    fill_rect(48, 151, (float)(width - 96), 1, 0.22f, 0.29f, 0.38f);
    draw_mark();

    for (day = 1; day <= days; ++day) {
        int index = first + day - 1;
        int row = index / 7;
        int col = index % 7;
        float x = (float)(48 + col * cell_w + 5);
        float y = (float)(226 + row * cell_h);
        float w = (float)(cell_w - 10);
        float h = (float)(cell_h - 8);
        int is_today = g_ui.today.year == g_ui.view.year &&
                       g_ui.today.month == g_ui.view.month &&
                       day == g_ui.today.day;
        fill_rect(x, y, w, h,
                  is_today ? 0.34f : 0.085f,
                  is_today ? 0.24f : 0.11f,
                  is_today ? 0.12f : 0.16f);
        outline_rect(x, y, w, h,
                     is_today ? 0.78f : 0.18f,
                     is_today ? 0.57f : 0.23f,
                     is_today ? 0.29f : 0.31f);
    }

    draw_text_overlay(width, height);
    g_ui.dirty = 0;
}

static int click_navigation(int x, int y)
{
    int left = g_ui.width - 220;
    int middle = g_ui.width - 134;
    int right = g_ui.width - 48;

    if (y < 155 || y > 195)
        return 0;
    if (x >= left && x < middle) {
        move_month(1);
        return 1;
    }
    if (x >= middle && x < right) {
        move_month(-1);
        return 1;
    }
    return 0;
}

/* ------------------------------- X11 / GLX ------------------------------ */

static int run_x11(void)
{
    Display *display = XOpenDisplay(NULL);
    int screen;
    Window window;
    Colormap colormap;
    XSetWindowAttributes attributes;
    XVisualInfo *visual;
    GLXContext context;
    Atom wm_delete;
    XEvent event;
    int result = 0;

    if (display == NULL)
        return 1;

    screen = DefaultScreen(display);
    {
        int attributes_list[] = {
            GLX_RGBA, GLX_DOUBLEBUFFER,
            GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8,
            GLX_DEPTH_SIZE, 16, None
        };
        visual = glXChooseVisual(display, screen, attributes_list);
    }
    if (visual == NULL) {
        XCloseDisplay(display);
        return 1;
    }

    colormap = XCreateColormap(display, RootWindow(display, screen),
                               visual->visual, AllocNone);
    memset(&attributes, 0, sizeof(attributes));
    attributes.colormap = colormap;
    attributes.event_mask = ExposureMask | StructureNotifyMask |
                            ButtonPressMask | KeyPressMask;
    window = XCreateWindow(display, RootWindow(display, screen),
        0, 0, INITIAL_WIDTH, INITIAL_HEIGHT, 0, visual->depth,
        InputOutput, visual->visual, CWColormap | CWEventMask, &attributes);
    XStoreName(display, window, "گاه‌شمار");
    wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &wm_delete, 1);

    context = glXCreateContext(display, visual, NULL, True);
    XFree(visual);
    if (context == NULL || !glXMakeCurrent(display, window, context)) {
        XDestroyWindow(display, window);
        XFreeColormap(display, colormap);
        XCloseDisplay(display);
        return 1;
    }

    XMapWindow(display, window);
    refresh_today();

    while (g_ui.running) {
        if (!XPending(display)) {
            if (g_ui.dirty) {
                render();
                glXSwapBuffers(display, window);
            }
            /* Block until the next native window event. */
            XNextEvent(display, &event);
        } else {
            XNextEvent(display, &event);
        }

        switch (event.type) {
        case Expose:
            g_ui.dirty = 1;
            break;
        case ConfigureNotify:
            g_ui.width = event.xconfigure.width;
            g_ui.height = event.xconfigure.height;
            g_ui.dirty = 1;
            break;
        case ButtonPress:
            if (event.xbutton.button == Button1)
                (void)click_navigation(event.xbutton.x, event.xbutton.y);
            break;
        case KeyPress:
            if (event.xkey.keycode == XKeysymToKeycode(display, XK_Escape))
                g_ui.running = 0;
            break;
        case ClientMessage:
            if ((Atom)event.xclient.data.l[0] == wm_delete)
                g_ui.running = 0;
            break;
        }
        if (g_ui.dirty) {
            render();
            glXSwapBuffers(display, window);
        }
    }

    glXMakeCurrent(display, None, NULL);
    glXDestroyContext(display, context);
    XDestroyWindow(display, window);
    XFreeColormap(display, colormap);
    XCloseDisplay(display);
    return result;
}

#ifdef GH_HAVE_WAYLAND
/* -------------------------- Wayland / EGL ------------------------------- */

typedef struct GhWayland {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct xdg_wm_base *wm_base;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct wl_egl_window *egl_window;
    EGLDisplay egl_display;
    EGLConfig egl_config;
    EGLContext egl_context;
    EGLSurface egl_surface;
    double pointer_x;
    double pointer_y;
    int configured;
} GhWayland;

static GhWayland g_wl;

static void wm_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_listener = {
    wm_ping
};

static void toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                               int32_t width, int32_t height,
                               struct wl_array *states)
{
    (void)data;
    (void)toplevel;
    (void)states;
    if (width > 0) g_ui.width = width;
    if (height > 0) g_ui.height = height;
    if (g_wl.egl_window != NULL)
        wl_egl_window_resize(g_wl.egl_window, g_ui.width, g_ui.height, 0, 0);
    g_ui.dirty = 1;
}

static void toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
    (void)data;
    (void)toplevel;
    g_ui.running = 0;
}

static const struct xdg_toplevel_listener toplevel_listener = {
    toplevel_configure,
    toplevel_close
};

static void surface_configure(void *data, struct xdg_surface *surface,
                              uint32_t serial)
{
    (void)data;
    xdg_surface_ack_configure(surface, serial);
    g_wl.configured = 1;
    g_ui.dirty = 1;
}

static const struct xdg_surface_listener surface_listener = {
    surface_configure
};

static void pointer_enter(void *data, struct wl_pointer *pointer,
                          uint32_t serial, struct wl_surface *surface,
                          wl_fixed_t sx, wl_fixed_t sy)
{
    (void)data; (void)pointer; (void)serial; (void)surface;
    g_wl.pointer_x = wl_fixed_to_double(sx);
    g_wl.pointer_y = wl_fixed_to_double(sy);
}

static void pointer_leave(void *data, struct wl_pointer *pointer,
                          uint32_t serial, struct wl_surface *surface)
{
    (void)data; (void)pointer; (void)serial; (void)surface;
}

static void pointer_motion(void *data, struct wl_pointer *pointer,
                           uint32_t time, wl_fixed_t sx, wl_fixed_t sy)
{
    (void)data; (void)pointer; (void)time;
    g_wl.pointer_x = wl_fixed_to_double(sx);
    g_wl.pointer_y = wl_fixed_to_double(sy);
}

static void pointer_button(void *data, struct wl_pointer *pointer,
                           uint32_t serial, uint32_t time,
                           uint32_t button, uint32_t state)
{
    (void)data; (void)pointer; (void)serial; (void)time;
    if (button == 0x110 && state == WL_POINTER_BUTTON_STATE_PRESSED)
        (void)click_navigation((int)g_wl.pointer_x, (int)g_wl.pointer_y);
}

static void pointer_axis(void *data, struct wl_pointer *pointer,
                         uint32_t time, uint32_t axis, wl_fixed_t value)
{
    (void)data; (void)pointer; (void)time; (void)axis; (void)value;
}

static const struct wl_pointer_listener pointer_listener = {
    pointer_enter, pointer_leave, pointer_motion, pointer_button,
    pointer_axis
};

static void seat_capabilities(void *data, struct wl_seat *seat,
                              uint32_t capabilities)
{
    (void)data;
    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && g_wl.pointer == NULL) {
        g_wl.pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(g_wl.pointer, &pointer_listener, NULL);
    } else if (!(capabilities & WL_SEAT_CAPABILITY_POINTER) &&
               g_wl.pointer != NULL) {
        wl_pointer_destroy(g_wl.pointer);
        g_wl.pointer = NULL;
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name)
{
    (void)data; (void)seat; (void)name;
}

static const struct wl_seat_listener seat_listener = {
    seat_capabilities, seat_name
};

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface,
                            uint32_t version)
{
    (void)data;
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        g_wl.compositor = wl_registry_bind(registry, name,
            &wl_compositor_interface, version < 4 ? version : 4);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        g_wl.shm = wl_registry_bind(registry, name,
            &wl_shm_interface, 1);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        g_wl.seat = wl_registry_bind(registry, name,
            &wl_seat_interface, version < 5 ? version : 5);
        wl_seat_add_listener(g_wl.seat, &seat_listener, NULL);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        g_wl.wm_base = wl_registry_bind(registry, name,
            &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(g_wl.wm_base, &wm_listener, NULL);
    }
}

static void registry_remove(void *data, struct wl_registry *registry,
                            uint32_t name)
{
    (void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener registry_listener = {
    registry_global, registry_remove
};

static int wayland_egl_init(void)
{
    EGLint count;
    EGLint attributes[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_NONE
    };
    EGLint context_attributes[] = { EGL_NONE };

    g_wl.egl_display = eglGetDisplay((EGLNativeDisplayType)g_wl.display);
    if (g_wl.egl_display == EGL_NO_DISPLAY ||
        !eglInitialize(g_wl.egl_display, NULL, NULL) ||
        !eglBindAPI(EGL_OPENGL_API) ||
        !eglChooseConfig(g_wl.egl_display, attributes,
                         &g_wl.egl_config, 1, &count) || count == 0)
        return 0;

    g_wl.egl_context = eglCreateContext(g_wl.egl_display, g_wl.egl_config,
                                        EGL_NO_CONTEXT, context_attributes);
    if (g_wl.egl_context == EGL_NO_CONTEXT)
        return 0;

    g_wl.egl_surface = eglCreateWindowSurface(g_wl.egl_display,
        g_wl.egl_config, (EGLNativeWindowType)g_wl.egl_window, NULL);
    if (g_wl.egl_surface == EGL_NO_SURFACE)
        return 0;

    return eglMakeCurrent(g_wl.egl_display, g_wl.egl_surface,
                          g_wl.egl_surface, g_wl.egl_context);
}

static int run_wayland(void)
{
    memset(&g_wl, 0, sizeof(g_wl));
    g_wl.egl_display = EGL_NO_DISPLAY;
    g_wl.egl_context = EGL_NO_CONTEXT;
    g_wl.egl_surface = EGL_NO_SURFACE;
    g_wl.display = wl_display_connect(NULL);
    if (g_wl.display == NULL)
        return 1;

    g_wl.registry = wl_display_get_registry(g_wl.display);
    wl_registry_add_listener(g_wl.registry, &registry_listener, NULL);
    if (wl_display_roundtrip(g_wl.display) < 0 ||
        g_wl.compositor == NULL || g_wl.wm_base == NULL) {
        wl_display_disconnect(g_wl.display);
        return 1;
    }

    g_wl.surface = wl_compositor_create_surface(g_wl.compositor);
    g_wl.xdg_surface = xdg_wm_base_get_xdg_surface(g_wl.wm_base, g_wl.surface);
    xdg_surface_add_listener(g_wl.xdg_surface, &surface_listener, NULL);
    g_wl.toplevel = xdg_surface_get_toplevel(g_wl.xdg_surface);
    xdg_toplevel_add_listener(g_wl.toplevel, &toplevel_listener, NULL);
    xdg_toplevel_set_title(g_wl.toplevel, "گاه‌شمار");
    xdg_toplevel_set_app_id(g_wl.toplevel, "org.gahshomar.desktop");

    g_wl.egl_window = wl_egl_window_create(g_wl.surface,
                                            INITIAL_WIDTH, INITIAL_HEIGHT);
    if (g_wl.egl_window == NULL || !wayland_egl_init()) {
        if (g_wl.egl_window) wl_egl_window_destroy(g_wl.egl_window);
        wl_display_disconnect(g_wl.display);
        return 1;
    }

    wl_surface_commit(g_wl.surface);
    (void)wl_display_roundtrip(g_wl.display);
    refresh_today();

    while (g_ui.running) {
        /* Render pending work before dispatch can block for another event. */
        if (g_wl.configured && g_ui.dirty) {
            render();
            eglSwapBuffers(g_wl.egl_display, g_wl.egl_surface);
        }

        if (wl_display_dispatch(g_wl.display) < 0)
            break;
    }

    eglMakeCurrent(g_wl.egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                   EGL_NO_CONTEXT);
    if (g_wl.egl_surface != EGL_NO_SURFACE)
        eglDestroySurface(g_wl.egl_display, g_wl.egl_surface);
    if (g_wl.egl_context != EGL_NO_CONTEXT)
        eglDestroyContext(g_wl.egl_display, g_wl.egl_context);
    if (g_wl.egl_display != EGL_NO_DISPLAY)
        eglTerminate(g_wl.egl_display);
    if (g_wl.egl_window) wl_egl_window_destroy(g_wl.egl_window);
    if (g_wl.toplevel) xdg_toplevel_destroy(g_wl.toplevel);
    if (g_wl.xdg_surface) xdg_surface_destroy(g_wl.xdg_surface);
    if (g_wl.surface) wl_surface_destroy(g_wl.surface);
    if (g_wl.pointer) wl_pointer_destroy(g_wl.pointer);
    if (g_wl.seat) wl_seat_destroy(g_wl.seat);
    if (g_wl.wm_base) xdg_wm_base_destroy(g_wl.wm_base);
    if (g_wl.shm) wl_shm_destroy(g_wl.shm);
    if (g_wl.compositor) wl_compositor_destroy(g_wl.compositor);
    if (g_wl.registry) wl_registry_destroy(g_wl.registry);
    wl_display_disconnect(g_wl.display);
    return 0;
}
#endif

int main(int argc, char **argv)
{
    const char *backend = getenv("GAHSHOMAR_BACKEND");
    (void)argc;
    (void)argv;

    refresh_today();
#ifdef GH_HAVE_WAYLAND
    if (backend != NULL && strcmp(backend, "wayland") == 0)
        return run_wayland();
    if (backend != NULL && strcmp(backend, "x11") == 0)
        return run_x11();
    if (getenv("WAYLAND_DISPLAY") != NULL && run_wayland() == 0)
        return 0;
    return run_x11();
#else
    if (backend != NULL && strcmp(backend, "wayland") == 0) {
        fprintf(stderr, "Gahshomar was built without Wayland development libraries.\n");
        return 1;
    }
    return run_x11();
#endif
}
