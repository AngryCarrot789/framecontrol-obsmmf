#define WIN32_MEAN_AND_LEAN

#include <Windows.h>
#include <obs-module.h>
#include <obs.h>

#include <graphics/image-file.h>
#include <util/platform.h>

#define TEXT_MMF_CAPTURE obs_module_text("MMFCapture")
#define TEXT_MAP_NAME obs_module_text("MapName")

#define CAPTURE_ID "fc_mmf_capture"
#define SETTINGS_MAP_NAME "map_name"
#define SETTINGS_INITIAL_WIDTH "initial_width"
#define SETTINGS_INITIAL_HEIGHT "initial_height"
#define LL_AS_U32(longlong) (uint32_t) ((longlong) & 0xFFFFFFFF)

struct MEMMAPFILE_HEADER {
    uint8_t is_valid;
    uint32_t width;
    uint32_t height;
    // uint64_t time_writter;
};

struct mmf_capture {
    obs_source_t* source;

    char* mapFileName;
    uint32_t initialWidth;
    uint32_t initialHeight;

    HANDLE mappingHandle;
    struct MEMMAPFILE_HEADER lastReadHeader;
    uint64_t lastImageSize;

    bool compatibility;
    gs_texture_t* texture;
    gs_texture_t* extra_texture;
    HDC hdc;
    HBITMAP bmp, old_bmp;
    BYTE* bits;

    float timeSinceLastReopenMapping;

    // struct dc_capture data;
};

typedef struct mmf_capture mmf_capture_t;

static inline void init_texture(struct mmf_capture* capture, uint32_t w, uint32_t h) {
    if (capture->compatibility) {
        capture->texture = gs_texture_create(w, h, GS_BGRA, 1, NULL, GS_DYNAMIC);
    }
    else {
        capture->texture = gs_texture_create_gdi(w, h);
        if (capture->texture) {
            capture->extra_texture = gs_texture_create(w, h, GS_BGRA, 1, NULL, 0);
            if (!capture->extra_texture) {
                blog(LOG_WARNING, "[fc-mmf] Failed to create textures");
                gs_texture_destroy(capture->texture);
                capture->texture = NULL;
            }
        }
    }

    if (!capture->texture) {
        blog(LOG_WARNING, "[fc-mmf] Failed to create textures");
        return;
    }
}

static inline void destroy_texture(struct mmf_capture* capture) {
    if (capture->hdc) {
        SelectObject(capture->hdc, capture->old_bmp);
        DeleteDC(capture->hdc);
        DeleteObject(capture->bmp);
    }

    obs_enter_graphics();
    if (capture->extra_texture)
        gs_texture_destroy(capture->extra_texture);
    if (capture->texture)
        gs_texture_destroy(capture->texture);
    obs_leave_graphics();
}

static const char* mmf_capture_getname(void* unused) {
    UNUSED_PARAMETER(unused);
    return TEXT_MMF_CAPTURE;
}

static inline void close_mmf(struct mmf_capture* capture) {
    if (capture->mappingHandle) {
        CloseHandle(capture->mappingHandle);
        capture->mappingHandle = 0;
    }
}

static void mmf_capture_defaults(obs_data_t* settings) {
    obs_data_set_default_string(settings, SETTINGS_MAP_NAME, "my_mapped_file");
    obs_data_set_default_int(settings, SETTINGS_INITIAL_WIDTH, 200);
    obs_data_set_default_int(settings, SETTINGS_INITIAL_HEIGHT, 200);
}

static void load_capture_settings(struct mmf_capture* capture, obs_data_t* settings) {
    capture->initialHeight = (uint32_t) (obs_data_get_int(settings, SETTINGS_INITIAL_WIDTH) & 0xFFFFFFFF);
    capture->initialHeight = (uint32_t) (obs_data_get_int(settings, SETTINGS_INITIAL_HEIGHT) & 0xFFFFFFFF);
    capture->mapFileName = (char*) obs_data_get_string(settings, SETTINGS_MAP_NAME);
}

static int open_mmf(struct mmf_capture* capture) {
    size_t length;
    if (!capture->mapFileName || (length = strlen(capture->mapFileName)) < 1) {
        capture->lastReadHeader.is_valid = 0;
        return 0;
    }

    wchar_t* wtext = (wchar_t*) bzalloc(length + 1);
    mbstowcs(wtext, capture->mapFileName, length + 1);//Plus null
    capture->mappingHandle = OpenFileMapping(FILE_MAP_READ, FALSE, wtext);
    return capture->mappingHandle != NULL ? 1 : 0;
}

static void mmf_settings_updated(void* data, obs_data_t* settings) {
    struct mmf_capture* capture = data;
    close_mmf(capture);
    destroy_texture(capture);
    memset(&capture->lastReadHeader, 0, sizeof(struct MEMMAPFILE_HEADER));
    capture->lastImageSize = 0;
    load_capture_settings(capture, settings);
    open_mmf(capture);
}

static void* mmf_capture_create(obs_data_t* settings, obs_source_t* source) {
    struct mmf_capture* capture;

    capture = bzalloc(sizeof(struct mmf_capture));
    capture->source = source;

    load_capture_settings(capture, settings);
    open_mmf(capture);
    return capture;
}

static void mmf_capture_destroy(void* data) {
    struct mmf_capture* capture = data;
    close_mmf(capture);
    destroy_texture(capture);
    bfree(capture);
}

static void DrawBitmapPixels(HDC hdc, int width, int height, int bpp, const void* pixels) {
    // Create a compatible memory DC
    HDC memDC = CreateCompatibleDC(hdc);

    // Create a compatible bitmap
    HBITMAP bitmap = CreateCompatibleBitmap(hdc, width, height);

    // Select the bitmap into the memory DC
    HGDIOBJ oldBitmap = SelectObject(memDC, bitmap);

    // Set the bitmap bits
    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(BITMAPINFO));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = bpp;
    bmi.bmiHeader.biCompression = BI_RGB;
    SetDIBits(memDC, bitmap, 0, height, pixels, &bmi, DIB_RGB_COLORS);

    // BitBlt the memory DC to the target HDC (drawing the bitmap onto the target)
    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);

    // Clean up resources
    SelectObject(memDC, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDC);
}

static void mmf_capture_tick(void* data, float seconds) {
    UNUSED_PARAMETER(seconds);
    struct mmf_capture* capture = data;

    if (!obs_source_showing(capture->source)) {
        return;
    }

    if (!capture->mappingHandle) {
        capture->timeSinceLastReopenMapping += seconds;
        if (capture->timeSinceLastReopenMapping < 1.0f) {
            return;
        }

        capture->timeSinceLastReopenMapping = 0.0f;
        if (!open_mmf(capture)) {
            return;
        }
    }
    else {
        capture->timeSinceLastReopenMapping = 0.0f;
    }

    // Map the file into memory
    LPVOID mappedAddress = MapViewOfFile(capture->mappingHandle, FILE_MAP_READ, 0, 0, 128);
    if (mappedAddress == NULL) {
        return;
    }

    struct MEMMAPFILE_HEADER header = *(struct MEMMAPFILE_HEADER*) mappedAddress;
    UnmapViewOfFile(mappedAddress);
    if (!header.is_valid) {
        return;
    }

    uint64_t size = (uint64_t) header.width * (uint64_t) header.height * 4;
    char* bmpAddress = (char*) MapViewOfFile(capture->mappingHandle, FILE_MAP_READ, 0, 0, size + 128);
    if (bmpAddress == NULL) {
        return;
    }

    bmpAddress += 128;

    if (!gs_gdi_texture_available())
        capture->compatibility = true;

    obs_enter_graphics();
    if (capture->lastImageSize != size || !capture->texture || gs_texture_get_width(capture->texture) != header.width || gs_texture_get_height(capture->texture) != header.height) {
        destroy_texture(capture);
        init_texture(capture, header.width, header.height);
        if (capture->compatibility && capture->texture) {
            BITMAPINFO bi = {0};
            BITMAPINFOHEADER* bih = &bi.bmiHeader;
            bih->biSize = sizeof(BITMAPINFOHEADER);
            bih->biBitCount = 32;
            bih->biWidth = (long) (header.width & 0xFFFFFFFF);
            bih->biHeight = -(long) (header.height & 0xFFFFFFFF); // Must flip for some reason
            bih->biPlanes = 1;

            const HDC hdc = CreateCompatibleDC(NULL);
            if (hdc) {
                const HBITMAP bmp = CreateDIBSection(capture->hdc, &bi, DIB_RGB_COLORS, (void**) &capture->bits, NULL, 0);
                if (bmp) {
                    capture->hdc = hdc;
                    capture->bmp = bmp;
                    capture->old_bmp = SelectObject(capture->hdc, capture->bmp);
                }
                else {
                    DeleteDC(hdc);
                }
            }
        }
    }

    if (capture->texture != NULL) {
        capture->lastReadHeader = header;
        HDC textureDc;
        if (capture->compatibility) {
            textureDc = capture->hdc;
        }
        else {
            textureDc = gs_texture_get_dc(capture->texture);
        }

        if (!textureDc) {
            blog(LOG_WARNING, "[fc-mmf] Failed to get texture DC");
        }
        else {
            DrawBitmapPixels(textureDc, (int) (header.width & 0xFFFFFFFF), (int) (header.height & 0xFFFFFFFF), 32, bmpAddress);
            UnmapViewOfFile(bmpAddress);

            if (capture->compatibility) {
                gs_texture_set_image(capture->texture, capture->bits, header.width * 4, false);
            }
            else {
                gs_texture_release_dc(capture->texture);
            }
        }
    }

    obs_leave_graphics();
}

static void mmf_capture_render(void* data, gs_effect_t* fx) {
    struct mmf_capture* capture = data;

    if (!obs_source_showing(capture->source) || !capture->texture) {
        return;
    }

    bool texcoords_centered = obs_source_get_texcoords_centered(capture->source);
    gs_texture_t* texture = capture->texture;
    const bool compatibility = capture->compatibility;
    bool linear_sample = compatibility;
    if (!linear_sample && !texcoords_centered) {
        gs_texture_t* const extra_texture = capture->extra_texture;
        gs_copy_texture(extra_texture, texture);
        texture = extra_texture;
        linear_sample = true;
    }

    const char* tech_name = "Draw";
    float multiplier = 1.f;
    switch (gs_get_color_space()) {
        case GS_CS_SRGB_16F:
        case GS_CS_709_EXTENDED:
            if (!linear_sample)
                tech_name = "DrawSrgbDecompress";
            break;
        case GS_CS_709_SCRGB:
            if (linear_sample)
                tech_name = "DrawMultiply";
            else
                tech_name = "DrawSrgbDecompressMultiply";
            multiplier = obs_get_video_sdr_white_level() / 80.f;
    }

    gs_effect_t* effect = obs_get_base_effect(OBS_EFFECT_OPAQUE);
    gs_technique_t* tech = gs_effect_get_technique(effect, tech_name);
    gs_eparam_t* image = gs_effect_get_param_by_name(effect, "image");

    const bool previous = gs_framebuffer_srgb_enabled();
    gs_enable_framebuffer_srgb(linear_sample);
    gs_enable_blending(false);

    if (linear_sample)
        gs_effect_set_texture_srgb(image, texture);
    else
        gs_effect_set_texture(image, texture);

    gs_eparam_t* multiplier_param = gs_effect_get_param_by_name(effect, "multiplier");
    gs_effect_set_float(multiplier_param, multiplier);

    const uint32_t flip = compatibility ? GS_FLIP_V : 0;
    const size_t passes = gs_technique_begin(tech);
    for (size_t i = 0; i < passes; i++) {
        if (gs_technique_begin_pass(tech, i)) {
            gs_draw_sprite(texture, flip, 0, 0);
            gs_technique_end_pass(tech);
        }
    }

    gs_technique_end(tech);
    gs_enable_blending(true);
    gs_enable_framebuffer_srgb(previous);

    UNUSED_PARAMETER(effect);
}

static uint32_t mmf_capture_width(void* data) {
    struct mmf_capture* capture = data;
    if (!capture->lastReadHeader.is_valid) {
        return capture->initialWidth;
    }

    return capture->lastReadHeader.width;
}

static uint32_t mmf_capture_height(void* data) {
    struct mmf_capture* capture = data;
    if (!capture->lastReadHeader.is_valid) {
        return capture->initialHeight;
    }
    return capture->lastReadHeader.height;
}

static obs_properties_t* mmf_capture_properties(void* unused) {
    UNUSED_PARAMETER(unused);

    obs_properties_t* props = obs_properties_create();
    obs_properties_add_text(props, SETTINGS_MAP_NAME, "Map Name", OBS_TEXT_DEFAULT);
    obs_properties_add_text(props, "map_name_info", "The map name is the same as what you put in the FrameControl MMF output", OBS_TEXT_INFO);

    // obs_properties_add_bool(props, "compatibility", TEXT_COMPATIBILITY);
    // obs_properties_add_bool(props, "capture_cursor", TEXT_CAPTURE_CURSOR);

    return props;
}

struct obs_source_info mmf_capture_info = {
    .id = CAPTURE_ID,
    .type = OBS_SOURCE_TYPE_INPUT,
    // .output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW |
    //         OBS_SOURCE_DO_NOT_DUPLICATE | OBS_SOURCE_SRGB,
    .output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_DO_NOT_DUPLICATE | OBS_SOURCE_SRGB,
    .get_name = mmf_capture_getname,
    .create = mmf_capture_create,
    .destroy = mmf_capture_destroy,
    .video_render = mmf_capture_render,
    .video_tick = mmf_capture_tick,
    .update = mmf_settings_updated,
    .get_width = mmf_capture_width,
    .get_height = mmf_capture_height,
    .get_defaults = mmf_capture_defaults,
    .get_properties = mmf_capture_properties,
    .icon_type = OBS_ICON_TYPE_WINDOW_CAPTURE,
};
