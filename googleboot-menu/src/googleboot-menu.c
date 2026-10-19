/*
 * googleboot-menu.c
 *
 * Zero-Dependency Graphical DRM/KMS kexec Bootloader UI for the
 * Acer Googlebook 14 (Moonstone, 2880x1800 OLED Coreboot Framebuffer).
 *
 * Why zero-dependency?
 *   - Runs directly in the 32 MiB Slot B initramfs (vendor_boot_b) using ONLY
 *     standard libc, libm, and libz (~65 KB binary, zero 15 MB GTK/Cairo/Wayland bloat).
 *   - Talks directly to /dev/dri/card0 (simpledrm / xe) via raw Linux DRM KMS ioctls.
 *   - Compiles out-of-the-box on both Linux (x86_64) and macOS (for instant local theme previews).
 *   - Reads live theme customizations from /boot/googlebook-theme/theme.conf.
 *   - Discovers OS entries via `googleboot-scan --list` (Linux BLS/GRUB, Android 17 Slot A,
 *     Windows 11 Bare-Metal UEFI Bridge, and hotplugged Live USB ISOs).
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <zlib.h>

#ifdef __linux__
#include <linux/input.h>
#include <linux/ioctl.h>

/* Minimal Linux DRM/KMS ABI definitions (avoids needing libdrm headers/shared libs) */
#define DRM_IOCTL_BASE                  'd'
#define DRM_IO(nr)                      _IO(DRM_IOCTL_BASE, nr)
#define DRM_IOWR(nr, type)              _IOWR(DRM_IOCTL_BASE, nr, type)

#define DRM_IOCTL_SET_MASTER            DRM_IO(0x1e)
#define DRM_IOCTL_DROP_MASTER           DRM_IO(0x1f)

struct drm_mode_modeinfo {
    uint32_t clock;
    uint16_t hdisplay, hsync_start, hsync_end, htotal, hskew;
    uint16_t vdisplay, vsync_start, vsync_end, vtotal, vscan;
    uint32_t vrefresh;
    uint32_t flags, type;
    char name[32];
};

struct drm_mode_card_res {
    uint64_t fb_id_ptr, crtc_id_ptr, connector_id_ptr, encoder_id_ptr;
    uint32_t count_fbs, count_crtcs, count_connectors, count_encoders;
    uint32_t min_width, max_width, min_height, max_height;
};

struct drm_mode_get_connector {
    uint64_t encoders_ptr, modes_ptr, props_ptr, prop_values_ptr;
    uint32_t count_modes, count_props, count_encoders;
    uint32_t encoder_id, connector_id, connector_type, connector_type_id;
    uint32_t connection, mm_width, mm_height, subpixel, pad;
};

struct drm_mode_get_encoder {
    uint32_t encoder_id, encoder_type, crtc_id, possible_crtcs, possible_clones;
};

struct drm_mode_crtc {
    uint64_t set_connectors_ptr;
    uint32_t count_connectors, crtc_id, fb_id, x, y, gamma_size, mode_valid;
    struct drm_mode_modeinfo mode;
};

struct drm_mode_fb_cmd {
    uint32_t fb_id, width, height, pitch, bpp, depth, handle;
};

struct drm_mode_create_dumb {
    uint32_t height, width, bpp, flags, handle, pitch;
    uint64_t size;
};

struct drm_mode_map_dumb {
    uint32_t handle, pad;
    uint64_t offset;
};

struct drm_mode_fb_dirty_cmd {
    uint32_t fb_id, flags, color, num_clips;
    uint64_t clips_ptr;
};

#define DRM_IOCTL_MODE_GETRESOURCES     DRM_IOWR(0xA0, struct drm_mode_card_res)
#define DRM_IOCTL_MODE_GETCRTC          DRM_IOWR(0xA1, struct drm_mode_crtc)
#define DRM_IOCTL_MODE_SETCRTC          DRM_IOWR(0xA2, struct drm_mode_crtc)
#define DRM_IOCTL_MODE_GETENCODER       DRM_IOWR(0xA6, struct drm_mode_get_encoder)
#define DRM_IOCTL_MODE_GETCONNECTOR     DRM_IOWR(0xA7, struct drm_mode_get_connector)
#define DRM_IOCTL_MODE_ADDFB            DRM_IOWR(0xAE, struct drm_mode_fb_cmd)
#define DRM_IOCTL_MODE_DIRTYFB          DRM_IOWR(0xB1, struct drm_mode_fb_dirty_cmd)
#define DRM_IOCTL_MODE_CREATE_DUMB      DRM_IOWR(0xB2, struct drm_mode_create_dumb)
#define DRM_IOCTL_MODE_MAP_DUMB         DRM_IOWR(0xB3, struct drm_mode_map_dumb)
#endif

#define MAX_ENTRIES 12
#define MAX_STR     512

typedef struct {
    uint8_t r, g, b, a;
} color_t;

typedef struct {
    char id[128];
    char title[128];
    char subtitle[128];
    char os_type[64];
    bool needs_luks;
    char luks_dev[MAX_STR];
    char action_cmd[MAX_STR];
} boot_entry_t;

typedef struct {
    char title[128];
    char subtitle[128];
    color_t bg_top;
    color_t bg_bot;
    color_t card_bg;
    color_t card_sel_bg;
    color_t accent;
    color_t text;
    color_t subtext;
    int card_width;
    int card_height;
    int card_radius;
    int card_gap;
    int timeout_sec;
} theme_config_t;

typedef struct {
    uint32_t *pixels; /* ARGB8888 (0xAARRGGBB) */
    int width;
    int height;
    int stride_px;
} canvas_t;

static color_t parse_hex(const char *hex, uint8_t alpha) {
    color_t c = {0, 0, 0, alpha};
    if (!hex) return c;
    if (*hex == '#') hex++;
    unsigned int v = 0;
    if (sscanf(hex, "%06x", &v) == 1) {
        c.r = (v >> 16) & 0xFF;
        c.g = (v >> 8) & 0xFF;
        c.b = v & 0xFF;
    }
    return c;
}

static void init_default_theme(theme_config_t *t) {
    memset(t, 0, sizeof(*t));
    snprintf(t->title, sizeof(t->title), "GOOGLEBOOK // MOONSTONE");
    snprintf(t->subtitle, sizeof(t->subtitle), "SELECT OPERATING SYSTEM   -   COREBOOT + KEXEC UNIVERSAL BOOTLOADER");
    t->bg_top      = parse_hex("#070B12", 255);
    t->bg_bot      = parse_hex("#111827", 255);
    t->card_bg     = parse_hex("#1E293B", 190);
    t->card_sel_bg = parse_hex("#162B4D", 240);
    t->accent      = parse_hex("#38BDF8", 255);
    t->text        = parse_hex("#F8FAFC", 255);
    t->subtext     = parse_hex("#94A3B8", 255);
    t->card_width  = 520;
    t->card_height = 580;
    t->card_radius = 32;
    t->card_gap    = 52;
    t->timeout_sec = 6;
}

static void trim_str(char *s) {
    char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' || s[len - 1] == ' ' || s[len - 1] == '\t'))
        s[--len] = '\0';
}

static void load_theme_config(theme_config_t *t, const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) return;
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        trim_str(line);
        if (line[0] == '#' || line[0] == '\0') continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *k = line, *v = eq + 1;
        trim_str(k);
        trim_str(v);
        if (strcmp(k, "title") == 0) snprintf(t->title, sizeof(t->title), "%s", v);
        else if (strcmp(k, "subtitle") == 0) snprintf(t->subtitle, sizeof(t->subtitle), "%s", v);
        else if (strcmp(k, "bg_top_color") == 0) t->bg_top = parse_hex(v, 255);
        else if (strcmp(k, "bg_bottom_color") == 0) t->bg_bot = parse_hex(v, 255);
        else if (strcmp(k, "card_color") == 0) t->card_bg = parse_hex(v, t->card_bg.a);
        else if (strcmp(k, "card_alpha") == 0) t->card_bg.a = (uint8_t)(atof(v) * 255.0);
        else if (strcmp(k, "card_selected_color") == 0) t->card_sel_bg = parse_hex(v, t->card_sel_bg.a);
        else if (strcmp(k, "card_selected_alpha") == 0) t->card_sel_bg.a = (uint8_t)(atof(v) * 255.0);
        else if (strcmp(k, "accent_color") == 0) t->accent = parse_hex(v, 255);
        else if (strcmp(k, "text_color") == 0) t->text = parse_hex(v, 255);
        else if (strcmp(k, "subtext_color") == 0) t->subtext = parse_hex(v, 255);
        else if (strcmp(k, "card_width") == 0) t->card_width = atoi(v);
        else if (strcmp(k, "card_height") == 0) t->card_height = atoi(v);
        else if (strcmp(k, "card_radius") == 0) t->card_radius = atoi(v);
        else if (strcmp(k, "card_gap") == 0) t->card_gap = atoi(v);
        else if (strcmp(k, "timeout") == 0) t->timeout_sec = atoi(v);
    }
    fclose(fp);
}

static inline void blend_pixel(canvas_t *cv, int x, int y, color_t c) {
    if (x < 0 || y < 0 || x >= cv->width || y >= cv->height || c.a == 0) return;
    uint32_t *dst = &cv->pixels[y * cv->stride_px + x];
    if (c.a == 255) {
        *dst = (0xFFu << 24) | ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b;
        return;
    }
    uint32_t d = *dst;
    uint32_t dr = (d >> 16) & 0xFF;
    uint32_t dg = (d >> 8) & 0xFF;
    uint32_t db = d & 0xFF;
    uint32_t a = c.a;
    uint32_t ia = 255 - a;
    uint32_t r = (c.r * a + dr * ia) / 255;
    uint32_t g = (c.g * a + dg * ia) / 255;
    uint32_t b = (c.b * a + db * ia) / 255;
    *dst = (0xFFu << 24) | (r << 16) | (g << 8) | b;
}

/* Signed Distance Field (SDF) anti-aliased rounded rectangle with border */
static void draw_sdf_rounded_rect(canvas_t *cv, float x, float y, float w, float h, float r,
                                  color_t fill, color_t border, float border_w) {
    int x0 = (int)(x - border_w - 2);
    int y0 = (int)(y - border_w - 2);
    int x1 = (int)(x + w + border_w + 2);
    int y1 = (int)(y + h + border_w + 2);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= cv->width) x1 = cv->width - 1;
    if (y1 >= cv->height) y1 = cv->height - 1;

    float cx = x + w * 0.5f;
    float cy = y + h * 0.5f;
    float hw = w * 0.5f - r;
    float hh = h * 0.5f - r;

    for (int py = y0; py <= y1; py++) {
        float dy = fabsf((py + 0.5f) - cy) - hh;
        for (int px = x0; px <= x1; px++) {
            float dx = fabsf((px + 0.5f) - cx) - hw;
            float ox = dx > 0.0f ? dx : 0.0f;
            float oy = dy > 0.0f ? dy : 0.0f;
            float dist_out = sqrtf(ox * ox + oy * oy);
            float dist_in = (dx > dy ? dx : dy);
            if (dist_in > 0.0f) dist_in = 0.0f;
            float d = dist_out + dist_in - r;

            /* Fill coverage */
            if (d < 0.5f && fill.a > 0) {
                float cov = 0.5f - d;
                if (cov > 1.0f) cov = 1.0f;
                color_t fc = fill;
                fc.a = (uint8_t)(fill.a * cov);
                blend_pixel(cv, px, py, fc);
            }
            /* Border coverage */
            if (border_w > 0.0f && border.a > 0) {
                float bd = fabsf(d + border_w * 0.5f) - border_w * 0.5f;
                if (bd < 0.7f) {
                    float bcov = 0.7f - bd;
                    if (bcov > 1.0f) bcov = 1.0f;
                    color_t bc = border;
                    bc.a = (uint8_t)(border.a * bcov);
                    blend_pixel(cv, px, py, bc);
                }
            }
        }
    }
}

static void draw_sdf_circle(canvas_t *cv, float cx, float cy, float r, color_t fill, color_t ring, float ring_w) {
    draw_sdf_rounded_rect(cv, cx - r, cy - r, r * 2.0f, r * 2.0f, r, fill, ring, ring_w);
}

/* Built-in crisp 8x8 ASCII bitmap font (scaled smoothly with bilinear/subpixel AA) */
static const uint8_t font8x8_basic[128][8] = {
    [' '] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['!'] = {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00},
    ['"'] = {0x36,0x36,0x14,0x00,0x00,0x00,0x00,0x00},
    ['#'] = {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00},
    ['&'] = {0x1C,0x36,0x1C,0x3B,0x6E,0x33,0x6E,0x00},
    ['('] = {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00},
    [')'] = {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00},
    ['*'] = {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00},
    ['+'] = {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00},
    [','] = {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x0C},
    ['-'] = {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00},
    ['.'] = {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00},
    ['/'] = {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00},
    ['0'] = {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00},
    ['1'] = {0x18,0x1E,0x18,0x18,0x18,0x18,0x7E,0x00},
    ['2'] = {0x3E,0x63,0x30,0x1C,0x06,0x63,0x7F,0x00},
    ['3'] = {0x3E,0x63,0x30,0x1C,0x30,0x63,0x3E,0x00},
    ['4'] = {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00},
    ['5'] = {0x7F,0x03,0x3F,0x60,0x60,0x63,0x3E,0x00},
    ['6'] = {0x1C,0x06,0x03,0x3F,0x63,0x63,0x3E,0x00},
    ['7'] = {0x7F,0x63,0x30,0x18,0x0C,0x0C,0x0C,0x00},
    ['8'] = {0x3E,0x63,0x63,0x3E,0x63,0x63,0x3E,0x00},
    ['9'] = {0x3E,0x63,0x63,0x7E,0x60,0x30,0x1E,0x00},
    [':'] = {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00},
    ['<'] = {0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x00},
    ['='] = {0x00,0x00,0x7E,0x00,0x00,0x7E,0x00,0x00},
    ['>'] = {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00},
    ['?'] = {0x3E,0x63,0x30,0x18,0x18,0x00,0x18,0x00},
    ['['] = {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00},
    [']'] = {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00},
    ['_'] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF},
    ['A'] = {0x1C,0x36,0x63,0x63,0x7F,0x63,0x63,0x00},
    ['B'] = {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00},
    ['C'] = {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00},
    ['D'] = {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00},
    ['E'] = {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00},
    ['F'] = {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00},
    ['G'] = {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00},
    ['H'] = {0x63,0x63,0x63,0x7F,0x63,0x63,0x63,0x00},
    ['I'] = {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
    ['J'] = {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00},
    ['K'] = {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00},
    ['L'] = {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00},
    ['M'] = {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00},
    ['N'] = {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00},
    ['O'] = {0x3E,0x63,0x63,0x63,0x63,0x63,0x3E,0x00},
    ['P'] = {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00},
    ['Q'] = {0x3E,0x63,0x63,0x63,0x6B,0x33,0x5E,0x00},
    ['R'] = {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00},
    ['S'] = {0x3E,0x63,0x07,0x3E,0x60,0x63,0x3E,0x00},
    ['T'] = {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    ['U'] = {0x63,0x63,0x63,0x63,0x63,0x63,0x3E,0x00},
    ['V'] = {0x63,0x63,0x63,0x63,0x63,0x36,0x1C,0x00},
    ['W'] = {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00},
    ['X'] = {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00},
    ['Y'] = {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00},
    ['Z'] = {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00},
    ['a'] = {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00},
    ['b'] = {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00},
    ['c'] = {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00},
    ['d'] = {0x38,0x30,0x30,0x3e,0x33,0x33,0x6E,0x00},
    ['e'] = {0x00,0x00,0x1E,0x33,0x3f,0x03,0x1E,0x00},
    ['f'] = {0x1C,0x36,0x06,0x0f,0x06,0x06,0x0F,0x00},
    ['g'] = {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F},
    ['h'] = {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00},
    ['i'] = {0x18,0x00,0x1E,0x18,0x18,0x18,0x3C,0x00},
    ['j'] = {0x30,0x00,0x38,0x30,0x30,0x33,0x33,0x1E},
    ['k'] = {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00},
    ['l'] = {0x1E,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
    ['m'] = {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00},
    ['n'] = {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00},
    ['o'] = {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00},
    ['p'] = {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F},
    ['q'] = {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78},
    ['r'] = {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00},
    ['s'] = {0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00},
    ['t'] = {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00},
    ['u'] = {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00},
    ['v'] = {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00},
    ['w'] = {0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00},
    ['x'] = {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00},
    ['y'] = {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F},
    ['z'] = {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00},
};

static void draw_text(canvas_t *cv, const char *str, int x, int y, int scale, int align, color_t col) {
    if (!str || !*str) return;
    int len = (int)strlen(str);
    int char_adv = 9 * scale;
    int total_w = len * char_adv - scale;
    int start_x = x;
    if (align == 1) start_x = x - total_w / 2;
    else if (align == 2) start_x = x - total_w;

    for (int i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)str[i];
        if (ch >= 128) ch = '-';
        const uint8_t *glyph = font8x8_basic[ch];
        int gx_base = start_x + i * char_adv;
        for (int row = 0; row < 8; row++) {
            uint8_t bits = glyph[row];
            if (!bits) continue;
            for (int col_idx = 0; col_idx < 8; col_idx++) {
                if (bits & (1u << col_idx)) {
                    int px = gx_base + col_idx * scale;
                    int py = y + row * scale;
                    for (int sy = 0; sy < scale; sy++) {
                        for (int sx = 0; sx < scale; sx++) {
                            blend_pixel(cv, px + sx, py + sy, col);
                        }
                    }
                }
            }
        }
    }
}

static void draw_os_badge(canvas_t *cv, const char *os_type, float cx, float cy, float radius) {
    float sf = radius / 88.0f;
    color_t ring = parse_hex("#38BDF8", 235);
    color_t none = {0, 0, 0, 0};

    if (strcmp(os_type, "omarchy") == 0)      ring = parse_hex("#38BDF8", 235);
    else if (strcmp(os_type, "fedora") == 0)  ring = parse_hex("#3B82F6", 235);
    else if (strcmp(os_type, "arch") == 0)    ring = parse_hex("#06B6D4", 235);
    else if (strcmp(os_type, "ubuntu") == 0)  ring = parse_hex("#F97316", 235);
    else if (strcmp(os_type, "android") == 0) ring = parse_hex("#34D399", 235);
    else if (strcmp(os_type, "windows") == 0) ring = parse_hex("#60A5FA", 235);
    else if (strcmp(os_type, "usb") == 0)     ring = parse_hex("#A78BFA", 235);

    color_t fill = ring;
    fill.a = 38;
    float rw = 4.0f * sf; if (rw < 1.5f) rw = 1.5f;
    draw_sdf_circle(cv, cx, cy, radius, fill, ring, rw);

    if (strcmp(os_type, "windows") == 0) {
        /* 4-pane Windows 11 vector grid */
        float s = radius * 0.42f;
        float g = 6.0f * sf;
        draw_sdf_rounded_rect(cv, cx - s - g/2, cy - s - g/2, s, s, 6.0f * sf, ring, none, 0);
        draw_sdf_rounded_rect(cv, cx + g/2,     cy - s - g/2, s, s, 6.0f * sf, ring, none, 0);
        draw_sdf_rounded_rect(cv, cx - s - g/2, cy + g/2,     s, s, 6.0f * sf, ring, none, 0);
        draw_sdf_rounded_rect(cv, cx + g/2,     cy + g/2,     s, s, 6.0f * sf, ring, none, 0);
    } else if (strcmp(os_type, "omarchy") == 0 || strcmp(os_type, "fedora") == 0 || strcmp(os_type, "arch") == 0) {
        /* Terminal / Hyprland tiling window vector icon */
        float w = radius * 0.92f, h = radius * 0.72f;
        draw_sdf_rounded_rect(cv, cx - w/2, cy - h/2, w * 0.54f, h, 8.0f * sf, ring, none, 0);
        draw_sdf_rounded_rect(cv, cx + w * 0.08f, cy - h/2, w * 0.42f, h * 0.46f, 6.0f * sf, ring, none, 0);
        draw_sdf_rounded_rect(cv, cx + w * 0.08f, cy + h * 0.04f, w * 0.42f, h * 0.46f, 6.0f * sf, ring, none, 0);
    } else if (strcmp(os_type, "android") == 0) {
        int ts = (int)roundf(4.0f * sf); if (ts < 2) ts = 2;
        draw_text(cv, "A17", (int)cx, (int)(cy - 4 * ts), ts, 1, ring);
    } else if (strcmp(os_type, "usb") == 0) {
        float uw = radius * 0.52f, uh = radius * 0.74f;
        draw_sdf_rounded_rect(cv, cx - uw/2, cy - uh * 0.25f, uw, uh * 0.75f, 8.0f * sf, ring, none, 0);
        color_t plug = ring; plug.a = 160;
        draw_sdf_rounded_rect(cv, cx - uw * 0.34f, cy - uh * 0.62f, uw * 0.68f, uh * 0.36f, 4.0f * sf, plug, ring, 2.0f * sf);
    } else {
        int ts = (int)roundf(4.0f * sf); if (ts < 2) ts = 2;
        draw_text(cv, "OS", (int)cx, (int)(cy - 4 * ts), ts, 1, ring);
    }
}

static void render_ui(canvas_t *cv, const theme_config_t *t,
                      const boot_entry_t *entries, int count, int selected,
                      int remaining_sec, bool timer_active,
                      bool luks_modal, const char *luks_pass, const char *status_msg) {
    int w = cv->width;
    int h = cv->height;
    float sf = (float)w / 2880.0f;
    if (sf < 0.32f) sf = 0.32f;

    int cw_i   = (int)(t->card_width * sf);
    int ch_i   = (int)(t->card_height * sf);
    int cgap_i = (int)(t->card_gap * sf);
    float crad = t->card_radius * sf;

    int s_title = (int)roundf(5.0f * sf); if (s_title < 2) s_title = 2;
    int s_card  = (int)roundf(3.0f * sf); if (s_card < 1)  s_card  = 1;
    int s_sub   = (int)roundf(2.0f * sf); if (s_sub < 1)   s_sub   = 1;

    int total_w = count * cw_i + (count - 1) * cgap_i;
    int start_x = (w - total_w) / 2;
    float sel_cx = start_x + selected * (cw_i + cgap_i) + cw_i * 0.5f;
    float sel_cy = h * 0.50f;
    float glow_r = 420.0f * sf;

    /* 1. Smooth linear background gradient + radial glow behind selected card */
    for (int y = 0; y < h; y++) {
        float fy = (float)y / (float)(h - 1);
        float br = t->bg_top.r + (t->bg_bot.r - t->bg_top.r) * fy;
        float bg = t->bg_top.g + (t->bg_bot.g - t->bg_top.g) * fy;
        float bb = t->bg_top.b + (t->bg_bot.b - t->bg_top.b) * fy;
        float dy = y - sel_cy;
        uint32_t *row = &cv->pixels[y * cv->stride_px];
        for (int x = 0; x < w; x++) {
            float dx = x - sel_cx;
            float dist2 = dx * dx + dy * dy;
            float glow = expf(-dist2 / (2.0f * glow_r * glow_r)) * 0.22f;
            int r = (int)(br + t->accent.r * glow);
            int g = (int)(bg + t->accent.g * glow);
            int b = (int)(bb + t->accent.b * glow);
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            row[x] = (0xFFu << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }

    /* 2. Header Title & Subtitle */
    draw_text(cv, t->title, w / 2, (int)(h * 0.11f), s_title, 1, t->text);
    draw_text(cv, t->subtitle, w / 2, (int)(h * 0.165f), s_sub, 1, t->subtext);

    /* 3. OS Boot Cards */
    int card_y = (h - ch_i) / 2 + (int)(24 * sf);
    for (int i = 0; i < count; i++) {
        bool is_sel = (i == selected);
        float x = (float)(start_x + i * (cw_i + cgap_i));
        float y = (float)(is_sel ? (card_y - (int)(22 * sf)) : card_y);
        float cw = (float)cw_i;
        float ch = (float)ch_i;

        color_t border = is_sel ? t->accent : (color_t){255, 255, 255, 32};
        float bw = is_sel ? (4.5f * sf) : (2.0f * sf);
        if (bw < 1.5f) bw = 1.5f;
        draw_sdf_rounded_rect(cv, x, y, cw, ch, crad,
                              is_sel ? t->card_sel_bg : t->card_bg, border, bw);

        /* Index badge [1..N] */
        char idx_s[16];
        snprintf(idx_s, sizeof(idx_s), "[%d]", i + 1);
        draw_text(cv, idx_s, (int)(x + 34 * sf), (int)(y + 34 * sf), s_sub, 0,
                  is_sel ? t->accent : t->subtext);

        if (entries[i].needs_luks) {
            draw_text(cv, "LUKS2", (int)(x + cw - 34 * sf), (int)(y + 34 * sf), s_sub, 2, t->accent);
        }

        /* Vector OS Badge */
        draw_os_badge(cv, entries[i].os_type, x + cw * 0.5f, y + ch * 0.35f, 88.0f * sf);

        /* Title & Subtitle */
        draw_text(cv, entries[i].title, (int)(x + cw * 0.5f), (int)(y + ch * 0.63f), s_card, 1, t->text);
        draw_text(cv, entries[i].subtitle, (int)(x + cw * 0.5f), (int)(y + ch * 0.72f), s_sub, 1, t->subtext);

        if (is_sel) {
            float pw = 280.0f * sf, ph = 56.0f * sf;
            float px = x + (cw - pw) * 0.5f;
            float py = y + ch - 92.0f * sf;
            color_t pill_fill = t->accent;
            pill_fill.a = 48;
            draw_sdf_rounded_rect(cv, px, py, pw, ph, 28.0f * sf, pill_fill, t->accent, 2.5f * sf);
            draw_text(cv, "ENTER TO BOOT", (int)(x + cw * 0.5f), (int)(py + 20.0f * sf), s_sub, 1, t->text);
        }
    }

    /* 4. Footer & Countdown Bar */
    const char *footer = "LEFT/RIGHT: SELECT OS    -    ENTER: BOOT    -    R: RESCAN USB/NVME    -    ESC: SHELL";
    draw_text(cv, (status_msg && status_msg[0]) ? status_msg : footer,
              w / 2, (int)(h * 0.88f), s_sub, 1, t->subtext);

    if (timer_active && t->timeout_sec > 0) {
        float bw = 640.0f * sf, bh = 10.0f * sf;
        if (bh < 4.0f) bh = 4.0f;
        float bx = (w - bw) * 0.5f;
        float by = h * 0.92f;
        color_t track = {255, 255, 255, 36};
        color_t none  = {0, 0, 0, 0};
        draw_sdf_rounded_rect(cv, bx, by, bw, bh, bh * 0.5f, track, none, 0);
        float frac = (float)remaining_sec / (float)t->timeout_sec;
        if (frac < 0.0f) frac = 0.0f;
        if (frac > 1.0f) frac = 1.0f;
        draw_sdf_rounded_rect(cv, bx, by, bw * frac, bh, bh * 0.5f, t->accent, none, 0);
    }

    /* 5. LUKS2 Passphrase Modal Overlay */
    if (luks_modal) {
        color_t dim = {5, 8, 16, 205};
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                blend_pixel(cv, x, y, dim);

        float mw = 960.0f * sf, mh = 420.0f * sf;
        float mx = (w - mw) * 0.5f;
        float my = (h - mh) * 0.5f;
        draw_sdf_rounded_rect(cv, mx, my, mw, mh, 32.0f * sf, t->card_sel_bg, t->accent, 4.0f * sf);

        draw_text(cv, "UNLOCK ENCRYPTED ROOT (LUKS2)", w / 2, (int)(my + 58 * sf), s_card, 1, t->text);
        draw_text(cv, entries[selected].luks_dev, w / 2, (int)(my + 114 * sf), s_sub, 1, t->subtext);

        float ibw = mw - 140.0f * sf, ibh = 84.0f * sf;
        float ibx = mx + 70.0f * sf, iby = my + 180.0f * sf;
        color_t box_bg = {8, 12, 22, 245};
        draw_sdf_rounded_rect(cv, ibx, iby, ibw, ibh, 18.0f * sf, box_bg, t->accent, 2.5f * sf);

        size_t plen = luks_pass ? strlen(luks_pass) : 0;
        if (plen == 0) {
            draw_text(cv, "TYPE PASSPHRASE AND PRESS ENTER...", (int)(ibx + 28 * sf), (int)(iby + 34 * sf), s_sub, 0, t->subtext);
        } else {
            char mask[128] = {0};
            for (size_t k = 0; k < plen && k < 36; k++) strcat(mask, "* ");
            draw_text(cv, mask, (int)(ibx + 28 * sf), (int)(iby + 30 * sf), s_card, 0, t->accent);
        }

        draw_text(cv, "ENTER: UNLOCK & KEXEC BOOT    -    ESC: CANCEL",
                  w / 2, (int)(my + mh - 64 * sf), s_sub, 1, t->subtext);
    }
}

/* Minimal zlib PNG writer for `--preview <output.png>` */
static void write_u32_be(uint8_t *p, uint32_t v) {
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8) & 0xFF;
    p[3] = v & 0xFF;
}

static void write_chunk(FILE *fp, const char type[4], const uint8_t *data, uint32_t len) {
    uint8_t hdr[8];
    write_u32_be(hdr, len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, fp);
    if (len > 0) fwrite(data, 1, len, fp);
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef *)type, 4);
    if (len > 0) crc = crc32(crc, data, len);
    uint8_t crc_b[4];
    write_u32_be(crc_b, (uint32_t)crc);
    fwrite(crc_b, 1, 4, fp);
}

static int save_png(const char *path, const canvas_t *cv) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(sig, 1, 8, fp);

    uint8_t ihdr[13] = {0};
    write_u32_be(ihdr, cv->width);
    write_u32_be(ihdr + 4, cv->height);
    ihdr[8] = 8; /* bit depth */
    ihdr[9] = 2; /* RGB truecolor */
    write_chunk(fp, "IHDR", ihdr, 13);

    size_t raw_size = (size_t)cv->height * (1 + (size_t)cv->width * 3);
    uint8_t *raw = malloc(raw_size);
    for (int y = 0; y < cv->height; y++) {
        uint8_t *dst = &raw[(size_t)y * (1 + (size_t)cv->width * 3)];
        dst[0] = 0; /* filter type 0 */
        const uint32_t *src = &cv->pixels[y * cv->stride_px];
        for (int x = 0; x < cv->width; x++) {
            uint32_t p = src[x];
            dst[1 + x * 3 + 0] = (p >> 16) & 0xFF;
            dst[1 + x * 3 + 1] = (p >> 8) & 0xFF;
            dst[1 + x * 3 + 2] = p & 0xFF;
        }
    }

    uLongf zlen = compressBound(raw_size);
    uint8_t *zbuf = malloc(zlen);
    compress2(zbuf, &zlen, raw, raw_size, Z_BEST_SPEED);
    write_chunk(fp, "IDAT", zbuf, (uint32_t)zlen);
    write_chunk(fp, "IEND", NULL, 0);

    free(zbuf);
    free(raw);
    fclose(fp);
    return 0;
}

static int load_boot_entries(boot_entry_t *entries, int max_entries, const char *entries_file) {
    FILE *fp = NULL;
    if (entries_file && access(entries_file, R_OK) == 0) {
        fp = fopen(entries_file, "r");
    } else if (access("/usr/local/bin/googleboot-scan", X_OK) == 0) {
        fp = popen("/usr/local/bin/googleboot-scan --list", "r");
    }

    int count = 0;
    if (fp) {
        char line[1024];
        while (count < max_entries && fgets(line, sizeof(line), fp)) {
            trim_str(line);
            if (line[0] == '#' || line[0] == '\0') continue;
            boot_entry_t *e = &entries[count];
            memset(e, 0, sizeof(*e));
            char *saveptr = NULL;
            char *tok = strtok_r(line, "|", &saveptr);
            if (!tok) continue;
            snprintf(e->id, sizeof(e->id), "%s", tok);
            tok = strtok_r(NULL, "|", &saveptr);
            snprintf(e->title, sizeof(e->title), "%s", tok ? tok : "Linux");
            tok = strtok_r(NULL, "|", &saveptr);
            snprintf(e->subtitle, sizeof(e->subtitle), "%s", tok ? tok : "");
            tok = strtok_r(NULL, "|", &saveptr);
            snprintf(e->os_type, sizeof(e->os_type), "%s", tok ? tok : "omarchy");
            tok = strtok_r(NULL, "|", &saveptr);
            e->needs_luks = (tok && atoi(tok) == 1);
            tok = strtok_r(NULL, "|", &saveptr);
            snprintf(e->luks_dev, sizeof(e->luks_dev), "%s", tok ? tok : "");
            tok = strtok_r(NULL, "|", &saveptr);
            snprintf(e->action_cmd, sizeof(e->action_cmd), "%s", tok ? tok : "");
            count++;
        }
        if (entries_file && access(entries_file, R_OK) == 0) fclose(fp);
        else pclose(fp);
    }

    if (count == 0) {
        snprintf(entries[0].id, sizeof(entries[0].id), "fedomakase");
        snprintf(entries[0].title, sizeof(entries[0].title), "FEDOMAKASE LINUX");
        snprintf(entries[0].subtitle, sizeof(entries[0].subtitle), "FEDORA 44 - OMARCHY HYPRLAND");
        snprintf(entries[0].os_type, sizeof(entries[0].os_type), "omarchy");
        entries[0].needs_luks = true;
        snprintf(entries[0].luks_dev, sizeof(entries[0].luks_dev), "/dev/nvme0n1p32");
        snprintf(entries[0].action_cmd, sizeof(entries[0].action_cmd), "/usr/local/bin/googleboot-scan --boot fedomakase");

        snprintf(entries[1].id, sizeof(entries[1].id), "android");
        snprintf(entries[1].title, sizeof(entries[1].title), "ANDROID 17");
        snprintf(entries[1].subtitle, sizeof(entries[1].subtitle), "ALUMINUM OS - SLOT A (ONE-SHOT)");
        snprintf(entries[1].os_type, sizeof(entries[1].os_type), "android");
        entries[1].needs_luks = false;
        snprintf(entries[1].action_cmd, sizeof(entries[1].action_cmd), "/usr/local/bin/googleboot-scan --boot android");

        snprintf(entries[2].id, sizeof(entries[2].id), "windows");
        snprintf(entries[2].title, sizeof(entries[2].title), "WINDOWS 11");
        snprintf(entries[2].subtitle, sizeof(entries[2].subtitle), "UEFI BRIDGE -> BOOTMGFW.EFI");
        snprintf(entries[2].os_type, sizeof(entries[2].os_type), "windows");
        entries[2].needs_luks = false;
        snprintf(entries[2].action_cmd, sizeof(entries[2].action_cmd), "/usr/local/bin/googleboot-scan --boot windows");

        snprintf(entries[3].id, sizeof(entries[3].id), "usb");
        snprintf(entries[3].title, sizeof(entries[3].title), "LIVE USB / ISO");
        snprintf(entries[3].subtitle, sizeof(entries[3].subtitle), "SCAN & KEXEC EXTERNAL USB");
        snprintf(entries[3].os_type, sizeof(entries[3].os_type), "usb");
        entries[3].needs_luks = false;
        snprintf(entries[3].action_cmd, sizeof(entries[3].action_cmd), "/usr/local/bin/googleboot-scan --boot usb");
        count = 4;
    }
    return count;
}

int main(int argc, char **argv) {
    const char *theme_path = "/boot/googlebook-theme/theme.conf";
    const char *preview_png = NULL;
    const char *entries_file = NULL;
    bool modal_preview = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--theme") == 0 && i + 1 < argc) theme_path = argv[++i];
        else if (strcmp(argv[i], "--preview") == 0 && i + 1 < argc) preview_png = argv[++i];
        else if (strcmp(argv[i], "--entries") == 0 && i + 1 < argc) entries_file = argv[++i];
        else if (strcmp(argv[i], "--preview-luks") == 0) modal_preview = true;
    }

    theme_config_t theme;
    init_default_theme(&theme);
    load_theme_config(&theme, theme_path);

    boot_entry_t entries[MAX_ENTRIES];
    int count = load_boot_entries(entries, MAX_ENTRIES, entries_file);

    if (preview_png) {
        canvas_t cv = {
            .width = 2880,
            .height = 1800,
            .stride_px = 2880,
            .pixels = calloc(2880 * 1800, sizeof(uint32_t))
        };
        render_ui(&cv, &theme, entries, count, 0,
                  theme.timeout_sec, true, modal_preview, modal_preview ? "hunter22" : "", "");
        save_png(preview_png, &cv);
        free(cv.pixels);
        printf("Rendered 2880x1800 preview to %s\n", preview_png);
        return 0;
    }

#ifdef __linux__
    int drm_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (drm_fd < 0) {
        perror("open /dev/dri/card0");
        return 1;
    }
    ioctl(drm_fd, DRM_IOCTL_SET_MASTER, 0);

    struct drm_mode_card_res res = {0};
    if (ioctl(drm_fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) {
        perror("DRM_IOCTL_MODE_GETRESOURCES");
        return 1;
    }
    uint32_t *conn_ids = calloc(res.count_connectors ? res.count_connectors : 1, sizeof(uint32_t));
    uint32_t *crtc_ids = calloc(res.count_crtcs ? res.count_crtcs : 1, sizeof(uint32_t));
    uint32_t *fb_ids   = calloc(res.count_fbs ? res.count_fbs : 1, sizeof(uint32_t));
    uint32_t *enc_ids  = calloc(res.count_encoders ? res.count_encoders : 1, sizeof(uint32_t));
    res.connector_id_ptr = (uint64_t)(uintptr_t)conn_ids;
    res.crtc_id_ptr      = (uint64_t)(uintptr_t)crtc_ids;
    res.fb_id_ptr        = (uint64_t)(uintptr_t)fb_ids;
    res.encoder_id_ptr   = (uint64_t)(uintptr_t)enc_ids;
    ioctl(drm_fd, DRM_IOCTL_MODE_GETRESOURCES, &res);

    struct drm_mode_modeinfo mode = {0};
    uint32_t conn_id = 0, crtc_id = crtc_ids[0];
    for (uint32_t i = 0; i < res.count_connectors; i++) {
        struct drm_mode_get_connector c = { .connector_id = conn_ids[i] };
        if (ioctl(drm_fd, DRM_IOCTL_MODE_GETCONNECTOR, &c) == 0 && c.count_modes > 0) {
            struct drm_mode_modeinfo *modes = calloc(c.count_modes, sizeof(*modes));
            uint32_t *props = calloc(c.count_props ? c.count_props : 1, sizeof(uint32_t));
            uint64_t *pvals = calloc(c.count_props ? c.count_props : 1, sizeof(uint64_t));
            uint32_t *encs  = calloc(c.count_encoders ? c.count_encoders : 1, sizeof(uint32_t));
            c.modes_ptr       = (uint64_t)(uintptr_t)modes;
            c.props_ptr       = (uint64_t)(uintptr_t)props;
            c.prop_values_ptr = (uint64_t)(uintptr_t)pvals;
            c.encoders_ptr    = (uint64_t)(uintptr_t)encs;
            ioctl(drm_fd, DRM_IOCTL_MODE_GETCONNECTOR, &c);
            mode = modes[0];
            conn_id = c.connector_id;
            if (c.encoder_id) {
                struct drm_mode_get_encoder enc = { .encoder_id = c.encoder_id };
                if (ioctl(drm_fd, DRM_IOCTL_MODE_GETENCODER, &enc) == 0 && enc.crtc_id)
                    crtc_id = enc.crtc_id;
            }
            free(modes); free(props); free(pvals); free(encs);
            break;
        }
    }

    struct drm_mode_create_dumb creq = {
        .width = mode.hdisplay, .height = mode.vdisplay, .bpp = 32
    };
    ioctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);

    struct drm_mode_fb_cmd fb = {
        .width = mode.hdisplay, .height = mode.vdisplay,
        .pitch = creq.pitch, .bpp = 32, .depth = 24, .handle = creq.handle
    };
    ioctl(drm_fd, DRM_IOCTL_MODE_ADDFB, &fb);

    struct drm_mode_map_dumb mreq = { .handle = creq.handle };
    ioctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
    uint32_t *fb_map = mmap(0, creq.size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd, mreq.offset);

    struct drm_mode_crtc crtc = {
        .crtc_id = crtc_id, .fb_id = fb.fb_id, .mode_valid = 1, .mode = mode,
        .set_connectors_ptr = (uint64_t)(uintptr_t)&conn_id, .count_connectors = 1
    };
    ioctl(drm_fd, DRM_IOCTL_MODE_SETCRTC, &crtc);

    canvas_t cv = {
        .width = mode.hdisplay,
        .height = mode.vdisplay,
        .stride_px = creq.pitch / 4,
        .pixels = calloc(creq.size, 1)
    };

    int ev_fds[16], ev_count = 0;
    DIR *d = opendir("/dev/input");
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) && ev_count < 16) {
            if (strncmp(de->d_name, "event", 5) == 0) {
                char p[128];
                snprintf(p, sizeof(p), "/dev/input/%s", de->d_name);
                int fd = open(p, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
                if (fd >= 0) ev_fds[ev_count++] = fd;
            }
        }
        closedir(d);
    }

    int selected = 0, remaining = theme.timeout_sec;
    bool timer_active = (theme.timeout_sec > 0), luks_modal = false, shift_down = false;
    char luks_pass[128] = {0}, status_msg[256] = {0};
    time_t last_tick = time(NULL);

    while (1) {
        render_ui(&cv, &theme, entries, count, selected,
                  remaining, timer_active, luks_modal, luks_pass, status_msg);
        memcpy(fb_map, cv.pixels, creq.size);
        struct drm_mode_fb_dirty_cmd dirty = { .fb_id = fb.fb_id };
        ioctl(drm_fd, DRM_IOCTL_MODE_DIRTYFB, &dirty);

        struct pollfd pfds[16];
        for (int i = 0; i < ev_count; i++) {
            pfds[i].fd = ev_fds[i];
            pfds[i].events = POLLIN;
        }
        int pr = poll(pfds, ev_count, 200);
        time_t now = time(NULL);
        if (timer_active && (now - last_tick) >= 1) {
            remaining -= (int)(now - last_tick);
            last_tick = now;
            if (remaining <= 0) {
                if (entries[selected].needs_luks) { luks_modal = true; timer_active = false; }
                else break;
            }
        }
        if (pr > 0) {
            for (int i = 0; i < ev_count; i++) {
                if (!(pfds[i].revents & POLLIN)) continue;
                struct input_event ev;
                while (read(ev_fds[i], &ev, sizeof(ev)) == sizeof(ev)) {
                    if (ev.type != EV_KEY) continue;
                    if (ev.code == KEY_LEFTSHIFT || ev.code == KEY_RIGHTSHIFT) {
                        shift_down = (ev.value != 0);
                        continue;
                    }
                    if (ev.value != 1 && ev.value != 2) continue;
                    timer_active = false;
                    if (!luks_modal) {
                        if (ev.code == KEY_LEFT || ev.code == KEY_UP) selected = (selected - 1 + count) % count;
                        else if (ev.code == KEY_RIGHT || ev.code == KEY_DOWN || ev.code == KEY_TAB) selected = (selected + 1) % count;
                        else if (ev.code >= KEY_1 && ev.code <= KEY_9) { int idx = ev.code - KEY_1; if (idx < count) selected = idx; }
                        else if (ev.code == KEY_R) { count = load_boot_entries(entries, MAX_ENTRIES, entries_file); }
                        else if (ev.code == KEY_ENTER) {
                            if (entries[selected].needs_luks) { luks_modal = true; luks_pass[0] = '\0'; }
                            else goto do_boot;
                        } else if (ev.code == KEY_ESC) return 0;
                    } else {
                        if (ev.code == KEY_ESC) { luks_modal = false; luks_pass[0] = '\0'; }
                        else if (ev.code == KEY_BACKSPACE) { size_t l = strlen(luks_pass); if (l > 0) luks_pass[l - 1] = '\0'; }
                        else if (ev.code == KEY_ENTER) goto do_boot;
                        else {
                            static const char map_norm[64] = {
                                [KEY_1]='1',[KEY_2]='2',[KEY_3]='3',[KEY_4]='4',[KEY_5]='5',
                                [KEY_6]='6',[KEY_7]='7',[KEY_8]='8',[KEY_9]='9',[KEY_0]='0',
                                [KEY_MINUS]='-',[KEY_EQUAL]='=',
                                [KEY_Q]='q',[KEY_W]='w',[KEY_E]='e',[KEY_R]='r',[KEY_T]='t',
                                [KEY_Y]='y',[KEY_U]='u',[KEY_I]='i',[KEY_O]='o',[KEY_P]='p',
                                [KEY_A]='a',[KEY_S]='s',[KEY_D]='d',[KEY_F]='f',[KEY_G]='g',
                                [KEY_H]='h',[KEY_J]='j',[KEY_K]='k',[KEY_L]='l',
                                [KEY_Z]='z',[KEY_X]='x',[KEY_C]='c',[KEY_V]='v',[KEY_B]='b',
                                [KEY_N]='n',[KEY_M]='m',[KEY_DOT]='.',[KEY_SLASH]='/',[KEY_SPACE]=' '
                            };
                            static const char map_shift[64] = {
                                [KEY_1]='!',[KEY_2]='@',[KEY_3]='#',[KEY_4]='$',[KEY_5]='%',
                                [KEY_6]='^',[KEY_7]='&',[KEY_8]='*',[KEY_9]='(',[KEY_0]=')',
                                [KEY_MINUS]='_',[KEY_EQUAL]='+',
                                [KEY_Q]='Q',[KEY_W]='W',[KEY_E]='E',[KEY_R]='R',[KEY_T]='T',
                                [KEY_Y]='Y',[KEY_U]='U',[KEY_I]='I',[KEY_O]='O',[KEY_P]='P',
                                [KEY_A]='A',[KEY_S]='S',[KEY_D]='D',[KEY_F]='F',[KEY_G]='G',
                                [KEY_H]='H',[KEY_J]='J',[KEY_K]='K',[KEY_L]='L',
                                [KEY_Z]='Z',[KEY_X]='X',[KEY_C]='C',[KEY_V]='V',[KEY_B]='B',
                                [KEY_N]='N',[KEY_M]='M',[KEY_DOT]='>',[KEY_SLASH]='?',[KEY_SPACE]=' '
                            };
                            if (ev.code < 64) {
                                char ch = shift_down ? map_shift[ev.code] : map_norm[ev.code];
                                size_t l = strlen(luks_pass);
                                if (ch && l + 1 < sizeof(luks_pass)) {
                                    luks_pass[l] = ch;
                                    luks_pass[l + 1] = '\0';
                                }
                            }
                        }
                    }
                }
            }
        }
    }

do_boot:
    ioctl(drm_fd, DRM_IOCTL_DROP_MASTER, 0);
    close(drm_fd);
    if (entries[selected].action_cmd[0]) {
        if (entries[selected].needs_luks && luks_pass[0])
            setenv("GOOGLEBOOT_LUKS_PASS", luks_pass, 1);
        execl("/bin/sh", "sh", "-c", entries[selected].action_cmd, (char *)NULL);
    }
#endif
    return 0;
}
