/*
 * googleboot-menu.c
 *
 * Zero-Dependency Graphical DRM/KMS kexec Bootloader UI for the
 * Acer Googlebook 14 (Moonstone, 2880x1800 OLED Coreboot Framebuffer).
 *
 * Styled after native Googlebook OS (ChromeOS / Aluminum OS Material 3 Dark)
 * splash screens and system modals, with an embedded zlib-compressed
 * anti-aliased geometric sans-serif glyph atlas (Google Sans / Avenir Next style).
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
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

#include "google_sans_atlas.h"

#ifdef __linux__
#include <linux/input.h>
#include <linux/ioctl.h>
#include <sys/reboot.h>

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

static uint8_t g_atlas_raw[GS_ATLAS_RAW_SIZE];
static bool g_atlas_ready = false;

static void ensure_font_atlas(void) {
    if (g_atlas_ready) return;
    uLongf dest_len = GS_ATLAS_RAW_SIZE;
    if (uncompress(g_atlas_raw, &dest_len, gs_atlas_zlib, GS_ATLAS_ZLIB_SIZE) == Z_OK) {
        g_atlas_ready = true;
    }
}

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
    snprintf(t->title, sizeof(t->title), "Googlebook");
    snprintf(t->subtitle, sizeof(t->subtitle), "Select an operating system to start");
    t->bg_top      = parse_hex("#202124", 255);
    t->bg_bot      = parse_hex("#1B1C1F", 255);
    t->card_bg     = parse_hex("#292A2D", 255);
    t->card_sel_bg = parse_hex("#303134", 255);
    t->accent      = parse_hex("#8AB4F8", 255);
    t->text        = parse_hex("#E8EAED", 255);
    t->subtext     = parse_hex("#9AA0A6", 255);
    t->card_width  = 540;
    t->card_height = 560;
    t->card_radius = 28;
    t->card_gap    = 44;
    t->timeout_sec = 3;
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

/* Soft drop shadow behind elevated Material 3 surfaces (cards & modals) */
static void draw_soft_shadow(canvas_t *cv, float x, float y, float w, float h, float r, float blur, uint8_t max_alpha) {
    int x0 = (int)(x - blur);
    int y0 = (int)(y - blur);
    int x1 = (int)(x + w + blur);
    int y1 = (int)(y + h + blur);
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
            if (d > -2.0f && d < blur) {
                float t = (d < 0.0f) ? 0.0f : (d / blur);
                float falloff = (1.0f - t) * (1.0f - t);
                color_t sc = {0, 0, 0, (uint8_t)(max_alpha * falloff)};
                blend_pixel(cv, px, py, sc);
            }
        }
    }
}

/* Anti-aliased line segment (capsule SDF) for crisp vector icons */
static void draw_sdf_segment(canvas_t *cv, float ax, float ay, float bx, float by, float radius, color_t col) {
    float min_x = (ax < bx ? ax : bx) - radius - 2.0f;
    float max_x = (ax > bx ? ax : bx) + radius + 2.0f;
    float min_y = (ay < by ? ay : by) - radius - 2.0f;
    float max_y = (ay > by ? ay : by) + radius + 2.0f;
    int x0 = (int)min_x; if (x0 < 0) x0 = 0;
    int y0 = (int)min_y; if (y0 < 0) y0 = 0;
    int x1 = (int)max_x; if (x1 >= cv->width) x1 = cv->width - 1;
    int y1 = (int)max_y; if (y1 >= cv->height) y1 = cv->height - 1;

    float vx = bx - ax, vy = by - ay;
    float len2 = vx * vx + vy * vy;

    for (int py = y0; py <= y1; py++) {
        for (int px = x0; px <= x1; px++) {
            float wx = (px + 0.5f) - ax;
            float wy = (py + 0.5f) - ay;
            float h = (len2 > 0.0001f) ? ((wx * vx + wy * vy) / len2) : 0.0f;
            if (h < 0.0f) h = 0.0f;
            if (h > 1.0f) h = 1.0f;
            float dx = wx - vx * h;
            float dy = wy - vy * h;
            float d = sqrtf(dx * dx + dy * dy) - radius;
            if (d < 0.5f) {
                float cov = 0.5f - d;
                if (cov > 1.0f) cov = 1.0f;
                color_t c = col;
                c.a = (uint8_t)(col.a * cov);
                blend_pixel(cv, px, py, c);
            }
        }
    }
}

/* Decode next glyph code from string (maps UTF-8 bullet '•' \xE2\x80\xA2 or \x7F to glyph 127) */
static int next_glyph_code(const char **p) {
    const unsigned char *s = (const unsigned char *)*p;
    if (!*s) return 0;
    if (s[0] == 0xE2 && s[1] == 0x80 && s[2] == 0xA2) {
        *p += 3;
        return 127;
    }
    if (s[0] == 0xC2 && s[1] == 0xB7) {
        *p += 2;
        return 127;
    }
    int c = s[0];
    *p += 1;
    if (c == 127) return 127;
    if (c < 32 || c > 126) return '-';
    return c;
}

static float measure_text_gs(const char *str, float px_size, float tracking) {
    if (!str || !*str) return 0.0f;
    float scale = px_size / GS_FONT_BASE_SIZE;
    float w = 0.0f;
    const char *p = str;
    int count = 0;
    while (*p) {
        int ch = next_glyph_code(&p);
        w += gs_glyph_metrics[ch].advance * scale;
        count++;
    }
    if (count > 1) w += (count - 1) * tracking;
    return w;
}

static inline float sample_glyph_bilinear(const uint8_t *bmp, int gw, int gh, float u, float v) {
    if (u < -0.5f || v < -0.5f || u > (float)gw - 0.5f || v > (float)gh - 0.5f) return 0.0f;
    int x0 = (int)floorf(u);
    int y0 = (int)floorf(v);
    float fx = u - (float)x0;
    float fy = v - (float)y0;
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    float v00 = (x0 >= 0 && x0 < gw && y0 >= 0 && y0 < gh) ? bmp[y0 * gw + x0] : 0.0f;
    float v10 = (x1 >= 0 && x1 < gw && y0 >= 0 && y0 < gh) ? bmp[y0 * gw + x1] : 0.0f;
    float v01 = (x0 >= 0 && x0 < gw && y1 >= 0 && y1 < gh) ? bmp[y1 * gw + x0] : 0.0f;
    float v11 = (x1 >= 0 && x1 < gw && y1 >= 0 && y1 < gh) ? bmp[y1 * gw + x1] : 0.0f;

    float top = v00 + (v10 - v00) * fx;
    float bot = v01 + (v11 - v01) * fx;
    return top + (bot - top) * fy;
}

/*
 * Draw anti-aliased geometric sans-serif text (Google Sans / Avenir Next style).
 * `y` is the vertical center of the cap-height (so y aligns naturally with original UI layout).
 */
static void draw_text_gs(canvas_t *cv, const char *str, float x, float y,
                         float px_size, int align, color_t col, float tracking) {
    if (!str || !*str) return;
    ensure_font_atlas();

    float scale = px_size / GS_FONT_BASE_SIZE;
    float total_w = measure_text_gs(str, px_size, tracking);
    float pen_x = x;
    if (align == 1) pen_x = x - total_w * 0.5f;
    else if (align == 2) pen_x = x - total_w;

    /* Baseline positioned so `y` sits at top of cap-height (matching prior top-left text convention) */
    float cap_h = 54.0f * scale;
    float baseline_y = y + cap_h;

    const char *p = str;
    while (*p) {
        int ch = next_glyph_code(&p);
        const gs_glyph_metric_t *m = &gs_glyph_metrics[ch];
        if (m->w > 0 && m->h > 0) {
            const uint8_t *bmp = &g_atlas_raw[m->data_offset];
            float gx0 = pen_x + m->xoff * scale;
            float gy0 = baseline_y + m->yoff * scale;
            float gw_f = m->w * scale;
            float gh_f = m->h * scale;

            int ix0 = (int)floorf(gx0 - 1.0f);
            int iy0 = (int)floorf(gy0 - 1.0f);
            int ix1 = (int)ceilf(gx0 + gw_f + 1.0f);
            int iy1 = (int)ceilf(gy0 + gh_f + 1.0f);

            for (int py = iy0; py <= iy1; py++) {
                for (int px = ix0; px <= ix1; px++) {
                    /* 2x2 supersampled bilinear filter for ultra-crisp downscaling */
                    float u0 = ((px + 0.25f) - gx0) / scale - 0.5f;
                    float u1 = ((px + 0.75f) - gx0) / scale - 0.5f;
                    float v0 = ((py + 0.25f) - gy0) / scale - 0.5f;
                    float v1 = ((py + 0.75f) - gy0) / scale - 0.5f;

                    float a = 0.25f * (
                        sample_glyph_bilinear(bmp, m->w, m->h, u0, v0) +
                        sample_glyph_bilinear(bmp, m->w, m->h, u1, v0) +
                        sample_glyph_bilinear(bmp, m->w, m->h, u0, v1) +
                        sample_glyph_bilinear(bmp, m->w, m->h, u1, v1)
                    );
                    if (a > 1.0f) {
                        color_t gc = col;
                        gc.a = (uint8_t)((col.a * a) / 255.0f);
                        blend_pixel(cv, px, py, gc);
                    }
                }
            }
        }
        pen_x += m->advance * scale + tracking;
    }
}

/* Convert legacy ALL-CAPS scanner strings into authentic Googlebook OS mixed-case UI labels */
static void prettify_label(const char *in, char *out, size_t out_sz, bool is_subtitle) {
    if (!in || !*in) {
        out[0] = '\0';
        return;
    }
    if (strcasecmp(in, "FEDOMAKASE LINUX") == 0 || strcasecmp(in, "FEDORA") == 0 || strcasecmp(in, "FEDORA LINUX") == 0) {
        snprintf(out, out_sz, "Fedora");
        return;
    }
    if (strcasecmp(in, "ANDROID 17") == 0 || strcasecmp(in, "GOOGLEBOOK OS") == 0) {
        snprintf(out, out_sz, "Googlebook OS");
        return;
    }
    if (strcasecmp(in, "WINDOWS 11") == 0) {
        snprintf(out, out_sz, "Windows 11");
        return;
    }
    if (strcasecmp(in, "LIVE USB / ISO") == 0) {
        snprintf(out, out_sz, "Live USB / ISO");
        return;
    }
    if (strcasecmp(in, "FEDORA 44 - OMARCHY HYPRLAND") == 0) {
        snprintf(out, out_sz, "Fedora 44 • Omarchy Hyprland");
        return;
    }
    if (strcasecmp(in, "ALUMINUM OS - SLOT A (ONE-SHOT)") == 0 || strcasecmp(in, "ANDROID 17 - SLOT A (ONE-SHOT)") == 0) {
        snprintf(out, out_sz, "Android 17 • Slot A (One-Shot)");
        return;
    }
    if (strcasecmp(in, "UEFI BRIDGE -> BOOTMGFW.EFI") == 0) {
        snprintf(out, out_sz, "UEFI Bridge • bootmgfw.efi");
        return;
    }
    if (strcasecmp(in, "SCAN & KEXEC EXTERNAL USB") == 0) {
        snprintf(out, out_sz, "External USB • Live ISO");
        return;
    }

    /* Check if string has any lowercase letters already */
    bool has_lower = false;
    for (const char *p = in; *p; p++) {
        if (*p >= 'a' && *p <= 'z') { has_lower = true; break; }
    }
    if (has_lower) {
        snprintf(out, out_sz, "%s", in);
        return;
    }

    /* Smart Title-Case conversion for dynamic scanner strings (e.g. "KERNEL 6.19.6 - DIRECT BOOT") */
    size_t j = 0;
    bool new_word = true;
    for (size_t i = 0; in[i] && j + 4 < out_sz; i++) {
        if (in[i] == ' ' && in[i + 1] == '-' && in[i + 2] == ' ') {
            out[j++] = ' ';
            out[j++] = '\xE2';
            out[j++] = '\x80';
            out[j++] = '\xA2';
            out[j++] = ' ';
            i += 2;
            new_word = true;
            continue;
        }
        unsigned char c = (unsigned char)in[i];
        if (isalpha(c)) {
            /* Keep known short acronyms uppercase */
            if (new_word && (strncmp(&in[i], "OS", 2) == 0 || strncmp(&in[i], "USB", 3) == 0 ||
                             strncmp(&in[i], "ISO", 3) == 0 || strncmp(&in[i], "EFI", 3) == 0 ||
                             strncmp(&in[i], "UEFI", 4) == 0 || strncmp(&in[i], "LUKS", 4) == 0)) {
                while (isalpha((unsigned char)in[i]) && j + 1 < out_sz) {
                    out[j++] = in[i++];
                }
                i--;
                new_word = false;
                continue;
            }
            out[j++] = new_word ? (char)toupper(c) : (char)tolower(c);
            new_word = false;
        } else {
            out[j++] = (char)c;
            new_word = (c == ' ' || c == '/' || c == '(' || c == ':');
        }
    }
    out[j] = '\0';
    (void)is_subtitle;
}

/*
 * Authentic 4-Color Google "G" Emblem rendered via Signed Distance Fields:
 *   Blue (#4285F4), Red (#EA4335), Yellow (#FBBC05), Green (#34A853)
 */
static void draw_google_g_logo(canvas_t *cv, float cx, float cy, float outer_r) {
    color_t c_blue   = parse_hex("#4285F4", 255);
    color_t c_red    = parse_hex("#EA4335", 255);
    color_t c_yellow = parse_hex("#FBBC05", 255);
    color_t c_green  = parse_hex("#34A853", 255);

    float stroke = outer_r * 0.38f;
    float mid_r  = outer_r - stroke * 0.5f;
    float half_s = stroke * 0.5f;

    int x0 = (int)(cx - outer_r - 3);
    int y0 = (int)(cy - outer_r - 3);
    int x1 = (int)(cx + outer_r + 3);
    int y1 = (int)(cy + outer_r + 3);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= cv->width) x1 = cv->width - 1;
    if (y1 >= cv->height) y1 = cv->height - 1;

    for (int py = y0; py <= y1; py++) {
        for (int px = x0; px <= x1; px++) {
            float dx = (px + 0.5f) - cx;
            float dy = (py + 0.5f) - cy;
            float r  = sqrtf(dx * dx + dy * dy);
            float deg = atan2f(dy, dx) * (180.0f / (float)M_PI);

            /* Horizontal crossbar of the Google 'G' */
            float bar_dx = fabsf(dx - outer_r * 0.46f) - outer_r * 0.52f;
            float bar_dy = fabsf(dy) - half_s * 0.92f;
            float bar_d  = (bar_dx > bar_dy ? bar_dx : bar_dy);

            /* Ring distance */
            float ring_d = fabsf(r - mid_r) - half_s;

            /* Upper-right opening gap (-42 deg to -2 deg) */
            if (deg > -42.0f && deg < -2.0f && bar_d > 0.5f) {
                continue;
            }

            float d = ring_d;
            color_t col = c_blue;
            if (bar_d < ring_d && dx >= -half_s * 0.2f && r <= outer_r + 0.5f) {
                d = bar_d;
                col = c_blue;
            } else {
                if (deg >= -2.0f && deg < 48.0f) col = c_blue;
                else if (deg >= 48.0f && deg < 142.0f) col = c_green;
                else if (deg >= 142.0f || deg < -142.0f) col = c_yellow;
                else col = c_red;
            }

            if (d < 0.5f) {
                float cov = 0.5f - d;
                if (cov > 1.0f) cov = 1.0f;
                col.a = (uint8_t)(255.0f * cov);
                blend_pixel(cv, px, py, col);
            }
        }
    }
}

/* 4-Color Googlebook accent pill bar below header */
static void draw_google_four_color_bar(canvas_t *cv, float cx, float cy, float sf) {
    color_t cols[4] = {
        parse_hex("#4285F4", 255),
        parse_hex("#EA4335", 255),
        parse_hex("#FBBC05", 255),
        parse_hex("#34A853", 255)
    };
    color_t none = {0, 0, 0, 0};
    float seg_w = 34.0f * sf;
    float seg_h = 6.0f * sf;
    float gap   = 10.0f * sf;
    float total = 4 * seg_w + 3 * gap;
    float sx    = cx - total * 0.5f;
    for (int i = 0; i < 4; i++) {
        draw_sdf_rounded_rect(cv, sx + i * (seg_w + gap), cy - seg_h * 0.5f,
                              seg_w, seg_h, seg_h * 0.5f, cols[i], none, 0);
    }
}

/* Vector padlock icon for LUKS2 chip & modal header */
static void draw_lock_icon(canvas_t *cv, float cx, float cy, float size, color_t col, color_t bg_hole) {
    color_t none = {0, 0, 0, 0};
    float bw = size * 0.76f;
    float bh = size * 0.58f;
    float bx = cx - bw * 0.5f;
    float by = cy - bh * 0.12f;
    float sh_r = size * 0.25f;
    float sh_w = size * 0.11f;
    if (sh_w < 1.8f) sh_w = 1.8f;

    /* Shackle loop */
    draw_sdf_rounded_rect(cv, cx - sh_r, cy - size * 0.48f, sh_r * 2.0f, size * 0.52f,
                          sh_r, none, col, sh_w);
    /* Lock body */
    draw_sdf_rounded_rect(cv, bx, by, bw, bh, size * 0.15f, col, none, 0);
    /* Keyhole */
    draw_sdf_circle(cv, cx, by + bh * 0.46f, size * 0.085f, bg_hole, none, 0);
}

/* Official Fedora infinity-'f' emblem rendered via exact tangent circle + stem SDFs */
static void draw_fedora_logo(canvas_t *cv, float cx, float cy, float radius) {
    float sf = radius / 84.0f;
    color_t fed_blue = parse_hex("#3C6EB4", 255);
    color_t fed_ring = parse_hex("#51A2DA", 220);
    color_t white    = parse_hex("#FFFFFF", 255);
    color_t none     = {0, 0, 0, 0};

    /* Outer badge circle with Fedora Blue fill + teardrop bottom-left corner */
    float br = radius * 0.72f;
    draw_sdf_rounded_rect(cv, cx - br, cy - br * 0.1f, br * 1.1f, br * 1.1f, br * 0.32f, fed_blue, none, 0);
    draw_sdf_circle(cv, cx, cy, br, fed_blue, fed_ring, 2.5f * sf);

    /* Infinity 'f' geometry: two loops of radius R tangent to x = cx and y = cy */
    float R = radius * 0.195f;
    float stroke = 8.5f * sf;
    if (stroke < 2.2f) stroke = 2.2f;
    float ox = cx - R * 0.12f;
    float oy = cy + R * 0.12f;

    /* Bottom-left loop centered at (ox - R, oy + R) */
    draw_sdf_circle(cv, ox - R, oy + R, R, none, white, stroke);
    /* Top-right hook loop centered at (ox + R, oy - R) */
    int x0 = (int)(ox - 2), x1 = (int)(ox + 2.4f * R + stroke + 2);
    int y0 = (int)(oy - 2.4f * R - stroke - 2), y1 = (int)(oy + 2);
    for (int py = y0; py <= y1; py++) {
        for (int px = x0; px <= x1; px++) {
            float dx = (px + 0.5f) - (ox + R);
            float dy = (py + 0.5f) - (oy - R);
            float deg = atan2f(dy, dx) * (180.0f / (float)M_PI);
            /* Open bottom-right quadrant of top hook (25 deg to 165 deg) so it forms 'f' hook */
            if (deg > 28.0f && deg < 172.0f) continue;
            float d = fabsf(sqrtf(dx * dx + dy * dy) - R) - stroke * 0.5f;
            if (d < 0.6f) {
                float cov = 0.6f - d; if (cov > 1.0f) cov = 1.0f;
                color_t c = white; c.a = (uint8_t)(255.0f * cov);
                blend_pixel(cv, px, py, c);
            }
        }
    }
    /* Central vertical stem and horizontal crossbar */
    draw_sdf_segment(cv, ox, oy - R, ox, oy + R, stroke * 0.5f, white);
    draw_sdf_segment(cv, ox - R, oy, ox + R * 1.12f, oy, stroke * 0.5f, white);
}

static void draw_os_badge(canvas_t *cv, const char *os_type, float cx, float cy, float radius) {
    float sf = radius / 84.0f;
    color_t ring = parse_hex("#8AB4F8", 255);
    color_t none = {0, 0, 0, 0};

    if (strcmp(os_type, "omarchy") == 0 || strcmp(os_type, "fedora") == 0) ring = parse_hex("#51A2DA", 255);
    else if (strcmp(os_type, "arch") == 0)    ring = parse_hex("#78D9EC", 255);
    else if (strcmp(os_type, "ubuntu") == 0)  ring = parse_hex("#FCAD70", 255);
    else if (strcmp(os_type, "android") == 0) ring = parse_hex("#8AB4F8", 255);
    else if (strcmp(os_type, "windows") == 0) ring = parse_hex("#4285F4", 255);
    else if (strcmp(os_type, "usb") == 0)     ring = parse_hex("#FDD663", 255);

    color_t fill = ring;
    fill.a = 26;
    color_t ring_border = ring;
    ring_border.a = 80;
    float rw = 2.5f * sf; if (rw < 1.5f) rw = 1.5f;
    draw_sdf_circle(cv, cx, cy, radius, fill, ring_border, rw);

    if (strcmp(os_type, "windows") == 0) {
        /* 4-pane Windows 11 vector grid */
        float s = radius * 0.38f;
        float g = 6.0f * sf;
        draw_sdf_rounded_rect(cv, cx - s - g/2, cy - s - g/2, s, s, 5.0f * sf, ring, none, 0);
        draw_sdf_rounded_rect(cv, cx + g/2,     cy - s - g/2, s, s, 5.0f * sf, ring, none, 0);
        draw_sdf_rounded_rect(cv, cx - s - g/2, cy + g/2,     s, s, 5.0f * sf, ring, none, 0);
        draw_sdf_rounded_rect(cv, cx + g/2,     cy + g/2,     s, s, 5.0f * sf, ring, none, 0);
    } else if (strcmp(os_type, "omarchy") == 0 || strcmp(os_type, "fedora") == 0) {
        /* Official Fedora infinity-'f' emblem */
        draw_fedora_logo(cv, cx, cy, radius);
    } else if (strcmp(os_type, "arch") == 0) {
        float w = radius * 0.88f, h = radius * 0.68f;
        draw_sdf_rounded_rect(cv, cx - w/2, cy - h/2, w * 0.54f, h, 8.0f * sf, ring, none, 0);
        color_t sub_pane = ring; sub_pane.a = 175;
        draw_sdf_rounded_rect(cv, cx + w * 0.08f, cy - h/2, w * 0.42f, h * 0.45f, 7.0f * sf, sub_pane, none, 0);
        draw_sdf_rounded_rect(cv, cx + w * 0.08f, cy + h * 0.05f, w * 0.42f, h * 0.45f, 7.0f * sf, sub_pane, none, 0);
    } else if (strcmp(os_type, "android") == 0) {
        /* Official 4-Color Googlebook OS Emblem */
        draw_google_g_logo(cv, cx, cy, radius * 0.56f);
    } else if (strcmp(os_type, "usb") == 0) {
        float uw = radius * 0.48f, uh = radius * 0.72f;
        draw_sdf_rounded_rect(cv, cx - uw/2, cy - uh * 0.22f, uw, uh * 0.74f, 8.0f * sf, ring, none, 0);
        color_t plug = ring; plug.a = 160;
        draw_sdf_rounded_rect(cv, cx - uw * 0.34f, cy - uh * 0.58f, uw * 0.68f, uh * 0.34f, 4.0f * sf, plug, ring, 2.0f * sf);
    } else {
        draw_text_gs(cv, "OS", cx, cy - 16.0f * sf, 32.0f * sf, 1, ring, 0.5f);
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

    int total_w = count * cw_i + (count - 1) * cgap_i;
    int start_x = (w - total_w) / 2;
    float sel_cx = start_x + selected * (cw_i + cgap_i) + cw_i * 0.5f;
    float sel_cy = h * 0.51f;
    float glow_r = 460.0f * sf;

    /* 1. Native Googlebook OS Material 3 Dark surface (#202124) with subtle ambient glow */
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
            float glow = expf(-dist2 / (2.0f * glow_r * glow_r)) * 0.08f;
            int r = (int)(br + t->accent.r * glow);
            int g = (int)(bg + t->accent.g * glow);
            int b = (int)(bb + t->accent.b * glow);
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            row[x] = (0xFFu << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }

    /* 2. Googlebook OS Splash Header: 4-Color Google 'G' Emblem + "Googlebook" Wordmark */
    const char *brand_title = (strstr(t->title, "//") != NULL) ? "Googlebook" : t->title;
    const char *brand_sub   = (strstr(t->subtitle, "COREBOOT") != NULL)
                              ? "Select an operating system to start" : t->subtitle;

    float title_px = 68.0f * sf;
    float sub_px   = 28.0f * sf;
    float logo_r   = 29.0f * sf;
    float title_w  = measure_text_gs(brand_title, title_px, 0.5f * sf);
    float header_gap = 22.0f * sf;
    float group_w  = (logo_r * 2.0f) + header_gap + title_w;
    float group_x  = (w - group_w) * 0.5f;
    float header_y = h * 0.115f;

    draw_google_g_logo(cv, group_x + logo_r, header_y + 24.0f * sf, logo_r);
    /* Render "Googlebook" header wordmark in Bold */
    float bx = group_x + logo_r * 2.0f + header_gap;
    draw_text_gs(cv, brand_title, bx, header_y, title_px, 0, t->text, 1.2f * sf);
    draw_text_gs(cv, brand_title, bx + 1.35f * sf, header_y, title_px, 0, t->text, 1.2f * sf);
    draw_text_gs(cv, brand_title, bx + 0.65f * sf, header_y - 0.55f * sf, title_px, 0, t->text, 1.2f * sf);

    draw_google_four_color_bar(cv, w * 0.5f, h * 0.175f, sf);
    draw_text_gs(cv, brand_sub, w * 0.5f, h * 0.205f, sub_px, 1, t->subtext, 0.2f * sf);

    /* 3. Material 3 OS Selector Surface Cards */
    int card_y = (h - ch_i) / 2 + (int)(44 * sf);
    color_t none = {0, 0, 0, 0};
    color_t outline_subtle = parse_hex("#3C4043", 255);
    color_t dark_text = parse_hex("#202124", 255);

    for (int i = 0; i < count; i++) {
        bool is_sel = (i == selected);
        float x = (float)(start_x + i * (cw_i + cgap_i));
        float y = (float)(is_sel ? (card_y - (int)(16 * sf)) : card_y);
        float cw = (float)cw_i;
        float ch = (float)ch_i;

        /* Soft Material elevation shadow */
        draw_soft_shadow(cv, x, y + (is_sel ? 14.0f * sf : 6.0f * sf), cw, ch, crad,
                         is_sel ? 32.0f * sf : 16.0f * sf, is_sel ? 110 : 60);

        color_t border = is_sel ? t->accent : outline_subtle;
        float bw = is_sel ? (3.5f * sf) : (1.5f * sf);
        if (bw < 1.2f) bw = 1.2f;
        draw_sdf_rounded_rect(cv, x, y, cw, ch, crad,
                              is_sel ? t->card_sel_bg : t->card_bg, border, bw);

        /* Shortcut key pill [1..N] in top-left */
        char idx_s[16];
        snprintf(idx_s, sizeof(idx_s), "%d", i + 1);
        float k_rad = 18.0f * sf;
        color_t k_bg = is_sel ? (color_t){t->accent.r, t->accent.g, t->accent.b, 38} : parse_hex("#202124", 180);
        color_t k_bd = is_sel ? t->accent : outline_subtle;
        draw_sdf_circle(cv, x + 44.0f * sf, y + 44.0f * sf, k_rad, k_bg, k_bd, 1.5f * sf);
        draw_text_gs(cv, idx_s, x + 44.0f * sf, y + 35.0f * sf, 24.0f * sf, 1,
                     is_sel ? t->accent : t->subtext, 0.0f);

        /* Encrypted LUKS2 badge in top-right */
        if (entries[i].needs_luks) {
            float lw = 128.0f * sf, lh = 36.0f * sf;
            float lx = x + cw - lw - 28.0f * sf;
            float ly = y + 26.0f * sf;
            color_t l_bg = {t->accent.r, t->accent.g, t->accent.b, 28};
            color_t l_bd = {t->accent.r, t->accent.g, t->accent.b, 96};
            draw_sdf_rounded_rect(cv, lx, ly, lw, lh, lh * 0.5f, l_bg, l_bd, 1.2f * sf);
            draw_lock_icon(cv, lx + 24.0f * sf, ly + lh * 0.5f, 18.0f * sf, t->accent,
                           is_sel ? t->card_sel_bg : t->card_bg);
            draw_text_gs(cv, "LUKS2", lx + 42.0f * sf, ly + 10.5f * sf, 20.0f * sf, 0, t->accent, 0.4f * sf);
        }

        /* Vector OS Icon Badge */
        draw_os_badge(cv, entries[i].os_type, x + cw * 0.5f, y + ch * 0.34f, 84.0f * sf);

        /* Formatted Mixed-Case Card Title & Subtitle */
        char disp_title[128], disp_sub[128];
        prettify_label(entries[i].title, disp_title, sizeof(disp_title), false);
        prettify_label(entries[i].subtitle, disp_sub, sizeof(disp_sub), true);

        draw_text_gs(cv, disp_title, x + cw * 0.5f, y + ch * 0.59f, 36.0f * sf, 1, t->text, 0.2f * sf);
        draw_text_gs(cv, disp_sub,   x + cw * 0.5f, y + ch * 0.685f, 23.0f * sf, 1, t->subtext, 0.1f * sf);

        /* Bottom Material 3 Pill Action Button */
        if (is_sel) {
            float pw = 220.0f * sf, ph = 56.0f * sf;
            float px = x + (cw - pw) * 0.5f;
            float py = y + ch - 88.0f * sf;
            draw_sdf_rounded_rect(cv, px, py, pw, ph, ph * 0.5f, t->accent, none, 0);
            draw_text_gs(cv, "Boot", x + cw * 0.5f, py + 18.0f * sf, 26.0f * sf, 1, dark_text, 0.3f * sf);
        } else {
            float pw = 180.0f * sf, ph = 50.0f * sf;
            float px = x + (cw - pw) * 0.5f;
            float py = y + ch - 85.0f * sf;
            draw_sdf_rounded_rect(cv, px, py, pw, ph, ph * 0.5f, none, outline_subtle, 1.5f * sf);
            draw_text_gs(cv, "Select", x + cw * 0.5f, py + 16.0f * sf, 23.0f * sf, 1, t->subtext, 0.2f * sf);
        }
    }

    /* 4. Footer Navigation Hint & Material Linear Progress Bar */
    const char *footer = "Arrow keys: Select OS   •   Enter: Boot   •   R: Rescan USB   •   Esc: Recovery shell";
    char disp_status[256];
    if (status_msg && status_msg[0]) {
        prettify_label(status_msg, disp_status, sizeof(disp_status), true);
    }
    draw_text_gs(cv, (status_msg && status_msg[0]) ? disp_status : footer,
                 w * 0.5f, h * 0.885f, 23.0f * sf, 1, t->subtext, 0.2f * sf);

    if (timer_active && t->timeout_sec > 0) {
        float bw = 560.0f * sf, bh = 8.0f * sf;
        if (bh < 4.0f) bh = 4.0f;
        float bx = (w - bw) * 0.5f;
        float by = h * 0.925f;
        color_t track = parse_hex("#3C4043", 255);
        draw_sdf_rounded_rect(cv, bx, by, bw, bh, bh * 0.5f, track, none, 0);
        float frac = (float)remaining_sec / (float)t->timeout_sec;
        if (frac < 0.0f) frac = 0.0f;
        if (frac > 1.0f) frac = 1.0f;
        draw_sdf_rounded_rect(cv, bx, by, bw * frac, bh, bh * 0.5f, t->accent, none, 0);
    }

    /* 5. Authentic ChromeOS / Googlebook OS LUKS2 Passphrase Modal Dialog */
    if (luks_modal) {
        color_t scrim = {14, 15, 18, 200};
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                blend_pixel(cv, x, y, scrim);

        float mw = 880.0f * sf, mh = 490.0f * sf;
        float mx = (w - mw) * 0.5f;
        float my = (h - mh) * 0.5f;
        float mrad = 28.0f * sf;

        draw_soft_shadow(cv, mx, my + 18.0f * sf, mw, mh, mrad, 42.0f * sf, 150);
        color_t modal_surface = parse_hex("#292A2D", 255);
        color_t modal_border  = parse_hex("#3C4043", 255);
        draw_sdf_rounded_rect(cv, mx, my, mw, mh, mrad, modal_surface, modal_border, 1.5f * sf);

        /* Top circular lock icon badge */
        float lock_cy = my + 68.0f * sf;
        color_t lock_bg = {t->accent.r, t->accent.g, t->accent.b, 32};
        draw_sdf_circle(cv, w * 0.5f, lock_cy, 34.0f * sf, lock_bg, none, 0);
        draw_lock_icon(cv, w * 0.5f, lock_cy, 32.0f * sf, t->accent, modal_surface);

        /* Modal Title & Subtitle */
        draw_text_gs(cv, "Enter your passphrase", w * 0.5f, my + 122.0f * sf, 38.0f * sf, 1, t->text, 0.2f * sf);

        char modal_sub[256];
        const char *ldev = (entries[selected].luks_dev[0]) ? entries[selected].luks_dev : "/dev/nvme0n1p32";
        snprintf(modal_sub, sizeof(modal_sub), "Unlock encrypted disk (%.120s) to start Fedora", ldev);
        draw_text_gs(cv, modal_sub, w * 0.5f, my + 174.0f * sf, 23.0f * sf, 1, t->subtext, 0.1f * sf);

        /* Material Outlined Password Input Field */
        float ibw = mw - 128.0f * sf, ibh = 82.0f * sf;
        float ibx = mx + 64.0f * sf, iby = my + 228.0f * sf;
        color_t field_bg = parse_hex("#202124", 255);
        draw_sdf_rounded_rect(cv, ibx, iby, ibw, ibh, 14.0f * sf, field_bg, t->accent, 2.5f * sf);

        /* Floating Material Field Label "Passphrase" on top outline */
        float lbl_w = measure_text_gs("Passphrase", 19.0f * sf, 0.2f * sf) + 20.0f * sf;
        draw_sdf_rounded_rect(cv, ibx + 20.0f * sf, iby - 10.0f * sf, lbl_w, 20.0f * sf, 4.0f * sf, modal_surface, none, 0);
        draw_text_gs(cv, "Passphrase", ibx + 30.0f * sf, iby - 7.0f * sf, 19.0f * sf, 0, t->accent, 0.2f * sf);

        size_t plen = luks_pass ? strlen(luks_pass) : 0;
        if (plen == 0) {
            draw_text_gs(cv, "Enter LUKS2 passphrase...", ibx + 28.0f * sf, iby + 30.0f * sf, 25.0f * sf, 0, t->subtext, 0.1f * sf);
        } else {
            /* Render bullet '•' (glyph 127) password mask + active cursor */
            char mask[256] = {0};
            for (size_t k = 0; k < plen && k < 36; k++) {
                strcat(mask, "\xE2\x80\xA2");
            }
            float mask_x = ibx + 28.0f * sf;
            float mask_y = iby + 26.0f * sf;
            draw_text_gs(cv, mask, mask_x, mask_y, 34.0f * sf, 0, t->text, 6.0f * sf);
            float mw_px = measure_text_gs(mask, 34.0f * sf, 6.0f * sf);
            draw_sdf_rounded_rect(cv, mask_x + mw_px + 8.0f * sf, iby + 20.0f * sf,
                                  3.0f * sf, 42.0f * sf, 1.5f * sf, t->accent, none, 0);
        }

        /* Bottom Action Row: Status hint on left + "Cancel" & "Unlock" pills on right */
        float btn_y = my + mh - 96.0f * sf;
        float btn_h = 52.0f * sf;
        float unlock_w = 152.0f * sf;
        float cancel_w = 152.0f * sf;
        float unlock_x = mx + mw - 64.0f * sf - unlock_w;
        float cancel_x = unlock_x - 18.0f * sf - cancel_w;

        if (status_msg && status_msg[0]) {
            char mstat[256];
            prettify_label(status_msg, mstat, sizeof(mstat), true);
            color_t err_col = (strstr(status_msg, "INCORRECT") != NULL)
                              ? parse_hex("#F28B82", 255) : t->accent;
            draw_text_gs(cv, mstat, mx + 64.0f * sf, btn_y + 17.0f * sf, 22.0f * sf, 0, err_col, 0.1f * sf);
        } else {
            draw_text_gs(cv, "Esc to cancel   •   Enter to unlock", mx + 64.0f * sf, btn_y + 17.0f * sf,
                         22.0f * sf, 0, t->subtext, 0.1f * sf);
        }

        /* "Cancel" Outlined Material Pill */
        draw_sdf_rounded_rect(cv, cancel_x, btn_y, cancel_w, btn_h, btn_h * 0.5f,
                              none, modal_border, 1.5f * sf);
        draw_text_gs(cv, "Cancel", cancel_x + cancel_w * 0.5f, btn_y + 17.0f * sf,
                     24.0f * sf, 1, t->accent, 0.2f * sf);

        /* "Unlock" Filled Material Pill */
        draw_sdf_rounded_rect(cv, unlock_x, btn_y, unlock_w, btn_h, btn_h * 0.5f,
                              t->accent, none, 0);
        draw_text_gs(cv, "Unlock", unlock_x + unlock_w * 0.5f, btn_y + 17.0f * sf,
                     24.0f * sf, 1, dark_text, 0.2f * sf);
    }
}

/*
 * Native GPT A/B Depthcharge Slot Switcher (zero dependency on sgdisk/libpopt/libstdc++).
 * Updates both Primary and Backup GPT headers & partition entry arrays on 512B or 4096B disks.
 */
#pragma pack(push, 1)
typedef struct {
    char     signature[8];
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t current_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t  disk_guid[16];
    uint64_t part_entries_lba;
    uint32_t num_part_entries;
    uint32_t size_part_entry;
    uint32_t part_entries_crc32;
} gpt_header_t;

typedef struct {
    uint8_t  type_guid[16];
    uint8_t  unique_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attributes;
    uint16_t name_utf16[36];
} gpt_entry_t;
#pragma pack(pop)

static void utf16le_to_ascii(const uint16_t *in, char *out, size_t max_len) {
    size_t i = 0;
    for (; i < 36 && i + 1 < max_len && in[i] != 0; i++) {
        out[i] = (char)(in[i] & 0x7F);
    }
    out[i] = '\0';
}

static int gpt_switch_slot(const char *disk_path, const char *mode, bool do_reboot) {
    int flags = (strcmp(mode, "status") == 0) ? O_RDONLY : O_RDWR;
    int fd = open(disk_path, flags);
    if (fd < 0) {
        perror("open disk for GPT slot switch");
        return 1;
    }

    /* Auto-detect 512-byte vs 4096-byte logical sector size by locating "EFI PART" at LBA 1 */
    size_t lba_size = 0;
    gpt_header_t hdr;
    const size_t candidates[2] = {512, 4096};
    for (int i = 0; i < 2; i++) {
        if (pread(fd, &hdr, sizeof(hdr), (off_t)candidates[i]) == sizeof(hdr) &&
            memcmp(hdr.signature, "EFI PART", 8) == 0) {
            lba_size = candidates[i];
            break;
        }
    }
    if (lba_size == 0) {
        fprintf(stderr, "Error: no valid GPT header found on %s\n", disk_path);
        close(fd);
        return 1;
    }

    size_t entries_bytes = (size_t)hdr.num_part_entries * hdr.size_part_entry;
    uint8_t *entries_buf = malloc(entries_bytes);
    if (!entries_buf || pread(fd, entries_buf, entries_bytes, (off_t)(hdr.part_entries_lba * lba_size)) != (ssize_t)entries_bytes) {
        fprintf(stderr, "Error: failed reading GPT partition entries from %s\n", disk_path);
        free(entries_buf);
        close(fd);
        return 1;
    }

    uint64_t attr_a = 0, attr_b = 0;
    bool write_changes = false;
    if (strcmp(mode, "a") == 0 || strcmp(mode, "android") == 0) {
        attr_a = 0x0102000000000000ULL;
        attr_b = 0x0101000000000000ULL;
        write_changes = true;
    } else if (strcmp(mode, "android-once") == 0 || strcmp(mode, "oneshot-a") == 0) {
        attr_a = 0x001f000000000000ULL; /* Priority=15, Tries=1, Successful=0 */
        attr_b = 0x010e000000000000ULL; /* Priority=14, Tries=0, Successful=1 */
        write_changes = true;
    } else if (strcmp(mode, "b") == 0 || strcmp(mode, "linux") == 0 || strcmp(mode, "fedomakase") == 0) {
        attr_a = 0x0101000000000000ULL;
        attr_b = 0x0102000000000000ULL;
        write_changes = true;
    } else if (strcmp(mode, "status") != 0) {
        fprintf(stderr, "Unknown slot mode: %s (expected status|a|b|android-once)\n", mode);
        free(entries_buf);
        close(fd);
        return 1;
    }

    uint64_t cur_vba = 0, cur_vbb = 0;
    int found = 0;
    for (uint32_t i = 0; i < hdr.num_part_entries; i++) {
        gpt_entry_t *e = (gpt_entry_t *)(entries_buf + (size_t)i * hdr.size_part_entry);
        char name[40];
        utf16le_to_ascii(e->name_utf16, name, sizeof(name));
        if (strcmp(name, "boot_a") == 0 || strcmp(name, "vbmeta_a") == 0) {
            if (write_changes) e->attributes = attr_a;
            if (strcmp(name, "vbmeta_a") == 0 || cur_vba == 0) cur_vba = e->attributes;
            found++;
        } else if (strcmp(name, "boot_b") == 0 || strcmp(name, "vbmeta_b") == 0) {
            if (write_changes) e->attributes = attr_b;
            if (strcmp(name, "vbmeta_b") == 0 || cur_vbb == 0) cur_vbb = e->attributes;
            found++;
        }
    }

    if (found == 0) {
        fprintf(stderr, "Error: boot_a/boot_b/vbmeta_a/vbmeta_b partitions not found on %s\n", disk_path);
        free(entries_buf);
        close(fd);
        return 1;
    }

    if (write_changes) {
        uint32_t pe_crc = (uint32_t)crc32(0L, entries_buf, (uInt)entries_bytes);
        hdr.part_entries_crc32 = pe_crc;
        hdr.header_crc32 = 0;
        hdr.header_crc32 = (uint32_t)crc32(0L, (const Bytef *)&hdr, hdr.header_size);

        /* Write Primary GPT entries & header */
        if (pwrite(fd, entries_buf, entries_bytes, (off_t)(hdr.part_entries_lba * lba_size)) < 0 ||
            pwrite(fd, &hdr, sizeof(hdr), (off_t)(hdr.current_lba * lba_size)) < 0) {
            perror("pwrite primary GPT");
        }

        /* Update Backup GPT if present */
        gpt_header_t bhdr;
        if (pread(fd, &bhdr, sizeof(bhdr), (off_t)(hdr.backup_lba * lba_size)) == sizeof(bhdr) &&
            memcmp(bhdr.signature, "EFI PART", 8) == 0) {
            bhdr.part_entries_crc32 = pe_crc;
            bhdr.header_crc32 = 0;
            bhdr.header_crc32 = (uint32_t)crc32(0L, (const Bytef *)&bhdr, bhdr.header_size);
            if (pwrite(fd, entries_buf, entries_bytes, (off_t)(bhdr.part_entries_lba * lba_size)) < 0 ||
                pwrite(fd, &bhdr, sizeof(bhdr), (off_t)(hdr.backup_lba * lba_size)) < 0) {
                perror("pwrite backup GPT");
            }
        }
        fsync(fd);
    }

    printf("Disk: %s (LBA=%zuB)\n", disk_path, lba_size);
    printf("  Slot A (Android / vbmeta_a): %016llx\n", (unsigned long long)cur_vba);
    printf("  Slot B (Linux   / vbmeta_b): %016llx\n", (unsigned long long)cur_vbb);

    free(entries_buf);
    close(fd);

#ifdef __linux__
    if (write_changes && do_reboot) {
        sync();
        /* Perform immediate kernel hardware reset into Coreboot/Depthcharge Slot A */
        reboot(RB_AUTOBOOT);
        execl("/sbin/reboot", "reboot", "-f", (char *)NULL);
        execl("/usr/sbin/reboot", "reboot", "-f", (char *)NULL);
        _exit(1);
    }
#else
    (void)do_reboot;
#endif
    return 0;
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

static int load_boot_entries(boot_entry_t *entries, int max_entries, const char *entries_file, bool is_preview) {
    FILE *fp = NULL;
    if (entries_file && access(entries_file, R_OK) == 0) {
        fp = fopen(entries_file, "r");
    } else if (!is_preview && access("/usr/local/bin/googleboot-scan", X_OK) == 0) {
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
            char *fields = line;
            char *tok = strsep(&fields, "|");
            if (!tok) continue;
            snprintf(e->id, sizeof(e->id), "%s", tok);
            tok = strsep(&fields, "|");
            snprintf(e->title, sizeof(e->title), "%s", tok ? tok : "Linux");
            tok = strsep(&fields, "|");
            snprintf(e->subtitle, sizeof(e->subtitle), "%s", tok ? tok : "");
            tok = strsep(&fields, "|");
            snprintf(e->os_type, sizeof(e->os_type), "%s", tok ? tok : "omarchy");
            tok = strsep(&fields, "|");
            e->needs_luks = (tok && atoi(tok) == 1);
            tok = strsep(&fields, "|");
            snprintf(e->luks_dev, sizeof(e->luks_dev), "%s", tok ? tok : "");
            tok = strsep(&fields, "|");
            snprintf(e->action_cmd, sizeof(e->action_cmd), "%s", tok ? tok : "");
            count++;
        }
        if (entries_file && access(entries_file, R_OK) == 0) fclose(fp);
        else pclose(fp);
    }

    if (count == 0) {
        snprintf(entries[0].id, sizeof(entries[0].id), "fedomakase");
        snprintf(entries[0].title, sizeof(entries[0].title), "Fedora");
        snprintf(entries[0].subtitle, sizeof(entries[0].subtitle), "Fedora 44 • Omarchy Hyprland");
        snprintf(entries[0].os_type, sizeof(entries[0].os_type), "fedora");
        entries[0].needs_luks = true;
        snprintf(entries[0].luks_dev, sizeof(entries[0].luks_dev), "/dev/nvme0n1p32");
        snprintf(entries[0].action_cmd, sizeof(entries[0].action_cmd), "/usr/local/bin/googleboot-scan --boot fedomakase");

        snprintf(entries[1].id, sizeof(entries[1].id), "android");
        snprintf(entries[1].title, sizeof(entries[1].title), "Googlebook OS");
        snprintf(entries[1].subtitle, sizeof(entries[1].subtitle), "Android 17 • Slot A (One-Shot)");
        snprintf(entries[1].os_type, sizeof(entries[1].os_type), "android");
        entries[1].needs_luks = false;
        snprintf(entries[1].action_cmd, sizeof(entries[1].action_cmd), "/usr/local/bin/googleboot-scan --boot android");
        count = 2;
    }
    return count;
}

static int set_gbb_short_delay(const char *path) {
    int fd = open(path, O_RDWR);
    if (fd < 0) {
        perror("open GBB image/device");
        return 1;
    }
    off_t sz = lseek(fd, 0, SEEK_END);
    if (sz <= 128) {
        close(fd);
        return 1;
    }
    uint8_t *buf = malloc((size_t)sz);
    if (!buf || pread(fd, buf, (size_t)sz, 0) != (ssize_t)sz) {
        free(buf);
        close(fd);
        return 1;
    }
    int updated = 0;
    for (size_t off = 0; off + 128 <= (size_t)sz; off += 4096) {
        if (memcmp(buf + off, "$GBB", 4) == 0) {
            uint16_t maj = *(uint16_t *)(buf + off + 4);
            uint16_t min = *(uint16_t *)(buf + off + 6);
            uint32_t hsz = *(uint32_t *)(buf + off + 8);
            uint32_t flg = *(uint32_t *)(buf + off + 12);
            if (maj == 1 && min >= 1 && hsz == 128) {
                uint32_t new_flg = flg | 0x00000001u; /* GBB_FLAG_DEV_SCREEN_SHORT_DELAY (2s) */
                if (new_flg != flg) {
                    pwrite(fd, &new_flg, sizeof(new_flg), (off_t)(off + 12));
                    fsync(fd);
                }
                printf("GBB header at 0x%zx: flags 0x%08x -> 0x%08x (2s bootloader delay active)\n", off, flg, new_flg);
                updated = 1;
                break;
            }
        }
    }
    free(buf);
    close(fd);
    return updated ? 0 : 1;
}

int main(int argc, char **argv) {
    const char *theme_path = "/boot/googlebook-theme/theme.conf";
    const char *preview_png = NULL;
    const char *entries_file = NULL;
    const char *slot_mode = NULL;
    const char *gbb_path = NULL;
    const char *disk_path = "/dev/nvme0n1";
    bool slot_reboot = false;
    bool modal_preview = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--theme") == 0 && i + 1 < argc) theme_path = argv[++i];
        else if (strcmp(argv[i], "--preview") == 0 && i + 1 < argc) preview_png = argv[++i];
        else if (strcmp(argv[i], "--entries") == 0 && i + 1 < argc) entries_file = argv[++i];
        else if (strcmp(argv[i], "--preview-luks") == 0) modal_preview = true;
        else if (strcmp(argv[i], "--switch-slot") == 0 && i + 1 < argc) slot_mode = argv[++i];
        else if (strcmp(argv[i], "--set-gbb-short-delay") == 0 && i + 1 < argc) gbb_path = argv[++i];
        else if (strcmp(argv[i], "--disk") == 0 && i + 1 < argc) disk_path = argv[++i];
        else if (strcmp(argv[i], "--reboot") == 0) slot_reboot = true;
    }

    if (gbb_path) {
        return set_gbb_short_delay(gbb_path);
    }
    if (slot_mode) {
        return gpt_switch_slot(disk_path, slot_mode, slot_reboot);
    }

    theme_config_t theme;
    init_default_theme(&theme);
    load_theme_config(&theme, theme_path);

    boot_entry_t entries[MAX_ENTRIES];
    int count = load_boot_entries(entries, MAX_ENTRIES, entries_file, preview_png != NULL);

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
                char p[300];
                snprintf(p, sizeof(p), "/dev/input/%s", de->d_name);
                int fd = open(p, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
                if (fd >= 0) ev_fds[ev_count++] = fd;
            }
        }
        closedir(d);
    }

    int selected = 0, remaining = theme.timeout_sec;
    bool timer_active = (theme.timeout_sec > 0), luks_modal = false, shift_down = false, caps_lock = false;
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
                    if (ev.code == KEY_CAPSLOCK && ev.value == 1) {
                        caps_lock = !caps_lock;
                        continue;
                    }
                    if (ev.value != 1 && ev.value != 2) continue;
                    timer_active = false;
                    if (!luks_modal) {
                        status_msg[0] = '\0';
                        if (ev.code == KEY_LEFT || ev.code == KEY_UP) selected = (selected - 1 + count) % count;
                        else if (ev.code == KEY_RIGHT || ev.code == KEY_DOWN || ev.code == KEY_TAB) selected = (selected + 1) % count;
                        else if (ev.code >= KEY_1 && ev.code <= KEY_9) { int idx = ev.code - KEY_1; if (idx < count) selected = idx; }
                        else if (ev.code == KEY_R) { count = load_boot_entries(entries, MAX_ENTRIES, entries_file, false); }
                        else if (ev.code == KEY_ENTER || ev.code == KEY_KPENTER) {
                            if (entries[selected].needs_luks) { luks_modal = true; luks_pass[0] = '\0'; status_msg[0] = '\0'; }
                            else goto do_boot;
                        } else if (ev.code == KEY_ESC) return 0;
                    } else {
                        if (ev.code == KEY_ESC) { luks_modal = false; luks_pass[0] = '\0'; status_msg[0] = '\0'; }
                        else if (ev.code == KEY_BACKSPACE) { size_t l = strlen(luks_pass); if (l > 0) luks_pass[l - 1] = '\0'; }
                        else if (ev.code == KEY_ENTER || ev.code == KEY_KPENTER) {
                            snprintf(status_msg, sizeof(status_msg), "Unlocking encrypted disk...");
                            render_ui(&cv, &theme, entries, count, selected,
                                      remaining, timer_active, luks_modal, luks_pass, status_msg);
                            memcpy(fb_map, cv.pixels, creq.size);
                            ioctl(drm_fd, DRM_IOCTL_MODE_DIRTYFB, &dirty);

                            setenv("GOOGLEBOOT_LUKS_PASS", luks_pass, 1);
                            setenv("GOOGLEBOOT_LUKS_DEV", entries[selected].luks_dev, 1);
                            int urc = system("/usr/local/bin/googleboot-scan --unlock");
                            if (urc == 0) {
                                snprintf(status_msg, sizeof(status_msg), "Disk unlocked • Starting operating system...");
                                render_ui(&cv, &theme, entries, count, selected,
                                          remaining, timer_active, false, "", status_msg);
                                memcpy(fb_map, cv.pixels, creq.size);
                                ioctl(drm_fd, DRM_IOCTL_MODE_DIRTYFB, &dirty);
                                goto do_boot;
                            } else {
                                luks_pass[0] = '\0';
                                snprintf(status_msg, sizeof(status_msg), "INCORRECT PASSPHRASE - TRY AGAIN");
                            }
                        } else {
                            static const char map_norm[64] = {
                                [KEY_1]='1',[KEY_2]='2',[KEY_3]='3',[KEY_4]='4',[KEY_5]='5',
                                [KEY_6]='6',[KEY_7]='7',[KEY_8]='8',[KEY_9]='9',[KEY_0]='0',
                                [KEY_MINUS]='-',[KEY_EQUAL]='=',[KEY_GRAVE]='`',
                                [KEY_Q]='q',[KEY_W]='w',[KEY_E]='e',[KEY_R]='r',[KEY_T]='t',
                                [KEY_Y]='y',[KEY_U]='u',[KEY_I]='i',[KEY_O]='o',[KEY_P]='p',
                                [KEY_LEFTBRACE]='[',[KEY_RIGHTBRACE]=']',[KEY_BACKSLASH]='\\',
                                [KEY_A]='a',[KEY_S]='s',[KEY_D]='d',[KEY_F]='f',[KEY_G]='g',
                                [KEY_H]='h',[KEY_J]='j',[KEY_K]='k',[KEY_L]='l',
                                [KEY_SEMICOLON]=';',[KEY_APOSTROPHE]='\'',
                                [KEY_Z]='z',[KEY_X]='x',[KEY_C]='c',[KEY_V]='v',[KEY_B]='b',
                                [KEY_N]='n',[KEY_M]='m',[KEY_COMMA]=',',[KEY_DOT]='.',[KEY_SLASH]='/',[KEY_SPACE]=' '
                            };
                            static const char map_shift[64] = {
                                [KEY_1]='!',[KEY_2]='@',[KEY_3]='#',[KEY_4]='$',[KEY_5]='%',
                                [KEY_6]='^',[KEY_7]='&',[KEY_8]='*',[KEY_9]='(',[KEY_0]=')',
                                [KEY_MINUS]='_',[KEY_EQUAL]='+',[KEY_GRAVE]='~',
                                [KEY_Q]='Q',[KEY_W]='W',[KEY_E]='E',[KEY_R]='R',[KEY_T]='T',
                                [KEY_Y]='Y',[KEY_U]='U',[KEY_I]='I',[KEY_O]='O',[KEY_P]='P',
                                [KEY_LEFTBRACE]='{',[KEY_RIGHTBRACE]='}',[KEY_BACKSLASH]='|',
                                [KEY_A]='A',[KEY_S]='S',[KEY_D]='D',[KEY_F]='F',[KEY_G]='G',
                                [KEY_H]='H',[KEY_J]='J',[KEY_K]='K',[KEY_L]='L',
                                [KEY_SEMICOLON]=':',[KEY_APOSTROPHE]='"',
                                [KEY_Z]='Z',[KEY_X]='X',[KEY_C]='C',[KEY_V]='V',[KEY_B]='B',
                                [KEY_N]='N',[KEY_M]='M',[KEY_COMMA]='<',[KEY_DOT]='>',[KEY_SLASH]='?',[KEY_SPACE]=' '
                            };
                            if (ev.code < 64) {
                                char ch = shift_down ? map_shift[ev.code] : map_norm[ev.code];
                                if (caps_lock) {
                                    if (ch >= 'a' && ch <= 'z') ch -= 32;
                                    else if (ch >= 'A' && ch <= 'Z') ch += 32;
                                }
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
    if (strcmp(entries[selected].id, "android") == 0 || strcmp(entries[selected].os_type, "android") == 0) {
        gpt_switch_slot(disk_path, "android-once", true);
    }
    if (entries[selected].action_cmd[0]) {
        if (entries[selected].needs_luks && luks_pass[0])
            setenv("GOOGLEBOOT_LUKS_PASS", luks_pass, 1);
        execl("/bin/sh", "sh", "-c", entries[selected].action_cmd, (char *)NULL);
    }
#endif
    return 0;
}
