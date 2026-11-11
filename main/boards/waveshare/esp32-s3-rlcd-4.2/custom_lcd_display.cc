#include <vector>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <esp_lcd_panel_io.h>
#include <esp_log.h>
#include <esp_err.h>
#include "custom_lcd_display.h"
#include "lcd_display.h"
#include "lvgl_theme.h"
#include "esp_lvgl_port.h"
#include "assets/lang_config.h"
#include "settings.h"
#include "config.h"
#include "board.h"

void CustomLcdDisplay::Lvgl_flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * color_p)
{
    assert(disp != NULL);
    CustomLcdDisplay *Disp = (CustomLcdDisplay *)lv_display_get_user_data(disp);
    uint16_t *buffer = (uint16_t *)color_p;
  	for(int y = area->y1; y <= area->y2; y++)
  	{
  	 	for(int x = area->x1; x <= area->x2; x++) 
  	 	{
  	 	   	uint8_t color = (*buffer < 0x7fff) ? ColorBlack : ColorWhite;
  	 	   	Disp->RLCD_SetPixel(x,y,color);
  	 	   	buffer++;
  	 	}
  	}
  	Disp->RLCD_Display();
	lv_disp_flush_ready(disp);
}

CustomLcdDisplay::CustomLcdDisplay(esp_lcd_panel_io_handle_t panel_io,
esp_lcd_panel_handle_t panel,
int width, 
int height, 
int offset_x, 
int offset_y,
bool mirror_x, 
bool mirror_y, 
bool swap_xy,
spi_display_config_t spiconfig,
spi_host_device_t spi_host) : LcdDisplay(panel_io, panel, width, height),
mosi_(spiconfig.mosi),
scl_(spiconfig.scl), 
dc_(spiconfig.dc), 
cs_(spiconfig.cs), 
rst_(spiconfig.rst), 
width_(width), 
height_(height)
{
	ESP_LOGI(TAG, "Initialize SPI");
	esp_err_t        ret;
    spi_bus_config_t buscfg   = {};
    int              transfer = width_ * height_;
    buscfg.miso_io_num                   = -1;
    buscfg.mosi_io_num                   = mosi_;
    buscfg.sclk_io_num                   = scl_;
    buscfg.quadwp_io_num                 = -1;
    buscfg.quadhd_io_num                 = -1;
    buscfg.max_transfer_sz               = transfer;
    ret                                  = spi_bus_initialize(spi_host, &buscfg, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(ret);
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.dc_gpio_num = static_cast<gpio_num_t>(dc_);
    io_config.cs_gpio_num = static_cast<gpio_num_t>(cs_);
    io_config.pclk_hz = 40 * 1000 * 1000;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    io_config.spi_mode = 0;
    io_config.trans_queue_depth = 7;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)spi_host, &io_config, &io_handle));
    gpio_config_t gpio_conf = {};
    gpio_conf.intr_type     = GPIO_INTR_DISABLE;
    gpio_conf.mode          = GPIO_MODE_OUTPUT;
    gpio_conf.pin_bit_mask  = (0x1ULL << rst_);
    gpio_conf.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    gpio_conf.pull_up_en    = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));
    Set_ResetIOLevel(1);

    DisplayLen                = transfer >> 3; //(1byte 8ipex)
    DispBuffer                = (uint8_t *) heap_caps_malloc(DisplayLen, MALLOC_CAP_SPIRAM);
    assert(DispBuffer);
	PixelIndexLUT = (uint16_t (*)[300])heap_caps_malloc(transfer * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
	PixelBitLUT   = (uint8_t (*)[300])heap_caps_malloc(transfer * sizeof(uint8_t), MALLOC_CAP_SPIRAM);
    assert(PixelIndexLUT);
    assert(PixelBitLUT);
    if(width_ == 400) {
        InitLandscapeLUT();
    } else {
        InitPortraitLUT();
    }

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority   = 2;
    port_cfg.timer_period_ms = 50;
    lvgl_port_init(&port_cfg);
    lvgl_port_lock(0);

    display_ = lv_display_create(width, height); /* 以水平和垂直分辨率（像素）进行基本初始化 */
    lv_display_set_flush_cb(display_, Lvgl_flush_cb);
    lv_display_set_user_data(display_, this);
	size_t lvgl_buffer_size = LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB565) * transfer;
	uint8_t *lvgl_buffer1 = (uint8_t *) heap_caps_malloc(lvgl_buffer_size, MALLOC_CAP_SPIRAM);
    assert(lvgl_buffer1);
	lv_display_set_buffers(display_, lvgl_buffer1, NULL, lvgl_buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    ESP_LOGI(TAG, "RLCD init");
    RLCD_Init();

    lvgl_port_unlock();
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    // Note: SetupUI() should be called by Application::Initialize(), not in constructor
    // to ensure lvgl objects are created after the display is fully initialized.
}

CustomLcdDisplay::~CustomLcdDisplay() {
}

void CustomLcdDisplay::InitPortraitLUT() {
    uint16_t W4 = width_ >> 2;
    for (uint16_t y = 0; y < height_; y++)
    {
        uint16_t byte_y = y >> 1;
        uint8_t  local_y = y & 1;
        for (uint16_t x = 0; x < width_; x++)
        {
            uint16_t byte_x = x >> 2;
            uint8_t  local_x = x & 3;

            uint32_t index = byte_y * W4 + byte_x;
            uint8_t bit = 7 - ((local_x << 1) | local_y);

            PixelIndexLUT[x][y] = index;
            PixelBitLUT  [x][y] = (1 << bit);
        }
    }
}

void CustomLcdDisplay::InitLandscapeLUT() {
    uint16_t H4 = height_ >> 2;
    for (uint16_t y = 0; y < height_; y++)
    {
        uint16_t inv_y = height_ - 1 - y;
        uint16_t block_y = inv_y >> 2;
        uint8_t  local_y  = inv_y & 3;
        for (uint16_t x = 0; x < width_; x++)
        {
            uint16_t byte_x = x >> 1;
            uint8_t  local_x = x & 1;

            uint32_t index = byte_x * H4 + block_y;
            uint8_t bit = 7 - ((local_y << 1) | local_x);

            PixelIndexLUT[x][y] = index;
            PixelBitLUT  [x][y] = (1 << bit);
        }
    }
}

void CustomLcdDisplay::Set_ResetIOLevel(uint8_t level) {
    gpio_set_level((gpio_num_t) rst_, level ? 1 : 0);
}

void CustomLcdDisplay::RLCD_SendCommand(uint8_t Reg) {
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io_handle, Reg, NULL, 0));
}

void CustomLcdDisplay::RLCD_SendData(uint8_t Data) {
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io_handle, -1, &Data, 1));
}

void CustomLcdDisplay::RLCD_Sendbuffera(uint8_t *Data, int len) {
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(io_handle, -1, Data, len));
}

void CustomLcdDisplay::RLCD_Reset(void) {
    Set_ResetIOLevel(1);
    vTaskDelay(pdMS_TO_TICKS(50));
    Set_ResetIOLevel(0);
    vTaskDelay(pdMS_TO_TICKS(20));
    Set_ResetIOLevel(1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

void CustomLcdDisplay::RLCD_ColorClear(uint8_t color) {
    memset(DispBuffer, color, DisplayLen);
}

void CustomLcdDisplay::RLCD_Init() {
    RLCD_Reset();

    RLCD_SendCommand(0xD6);  // NVM Load Control
	RLCD_SendData(0x17);
	RLCD_SendData(0x02);

	RLCD_SendCommand(0xD1); //Booster Enable
	RLCD_SendData(0x01);

	RLCD_SendCommand(0xC0); //Gate Voltage Control
	RLCD_SendData(0x11);   
	RLCD_SendData(0x04);   

	RLCD_SendCommand(0xC1); //VSHP Setting
	RLCD_SendData(0x69);
	RLCD_SendData(0x69);
	RLCD_SendData(0x69);
	RLCD_SendData(0x69);

	RLCD_SendCommand(0xC2);
	RLCD_SendData(0x19);
	RLCD_SendData(0x19);
	RLCD_SendData(0x19);
	RLCD_SendData(0x19);

	RLCD_SendCommand(0xC4);
	RLCD_SendData(0x4B);
	RLCD_SendData(0x4B);
	RLCD_SendData(0x4B);
	RLCD_SendData(0x4B);

	RLCD_SendCommand(0xC5);
	RLCD_SendData(0x19);
	RLCD_SendData(0x19);
	RLCD_SendData(0x19);
	RLCD_SendData(0x19);

	RLCD_SendCommand(0xD8);
	RLCD_SendData(0x80);
	RLCD_SendData(0xE9);

	RLCD_SendCommand(0xB2);
	RLCD_SendData(0x02);

	RLCD_SendCommand(0xB3);
	RLCD_SendData(0xE5);
	RLCD_SendData(0xF6);
	RLCD_SendData(0x05);
	RLCD_SendData(0x46);
	RLCD_SendData(0x77);
	RLCD_SendData(0x77);
	RLCD_SendData(0x77);
	RLCD_SendData(0x77);
	RLCD_SendData(0x76);
	RLCD_SendData(0x45);

	RLCD_SendCommand(0xB4);
	RLCD_SendData(0x05);
	RLCD_SendData(0x46);
	RLCD_SendData(0x77);
	RLCD_SendData(0x77);
	RLCD_SendData(0x77);
	RLCD_SendData(0x77);
	RLCD_SendData(0x76);
	RLCD_SendData(0x45);

	RLCD_SendCommand(0x62);
	RLCD_SendData(0x32);
	RLCD_SendData(0x03);
	RLCD_SendData(0x1F);

	RLCD_SendCommand(0xB7);
	RLCD_SendData(0x13);

	RLCD_SendCommand(0xB0);
	RLCD_SendData(0x64);

	RLCD_SendCommand(0x11); 
	vTaskDelay(pdMS_TO_TICKS(200));     
	RLCD_SendCommand(0xC9);
	RLCD_SendData(0x00);

	RLCD_SendCommand(0x36);
	RLCD_SendData(0x48); 

	RLCD_SendCommand(0x3A);
	RLCD_SendData(0x11); 

	RLCD_SendCommand(0xB9);
	RLCD_SendData(0x20);

	RLCD_SendCommand(0xB8);
	RLCD_SendData(0x29);

	RLCD_SendCommand(0x21);

	RLCD_SendCommand(0x2A); 
	RLCD_SendData(0x12);
	RLCD_SendData(0x2A);

	RLCD_SendCommand(0x2B); 
	RLCD_SendData(0x00);
	RLCD_SendData(0xC7);

	RLCD_SendCommand(0x35);
	RLCD_SendData(0x00);

	RLCD_SendCommand(0xD0);
	RLCD_SendData(0xFF);

	RLCD_SendCommand(0x38);
	RLCD_SendCommand(0x29);

    RLCD_ColorClear(ColorWhite);
}

void CustomLcdDisplay::RLCD_SetPixel(uint16_t x, uint16_t y, uint8_t color) {
    uint32_t idx = PixelIndexLUT[x][y];
    uint8_t  mask = PixelBitLUT[x][y];

    uint8_t *p = &DispBuffer[idx];

    if (color)
        *p |= mask;
    else
        *p &= ~mask;
}

void CustomLcdDisplay::RLCD_Display() {
    RLCD_SendCommand(0x2A);     // Column Address Set
  	RLCD_SendData(0x12);
  	RLCD_SendData(0x2A);

  	RLCD_SendCommand(0x2B);     // Page Address Set
  	RLCD_SendData(0x00);
  	RLCD_SendData(0xC7);

  	RLCD_SendCommand(0x2c);     // Page Address Set

	RLCD_Sendbuffera(DispBuffer,DisplayLen);
}

// ===================== Jarvis data screens =====================
// Server-driven "custom" WS messages rendered as a fullscreen overlay.
// Panel is 400x300 (landscape). Layout zones:
//   title  : y 8..40   (single line, ellipsized)
//   value  : y 60..190 (big, scaled)
//   label  : y 200..240 (single line, only if different from title)
//   graph  : plot y 70..230, time labels at y 250
// The overlay is dropped as soon as normal chat content arrives.

void CustomLcdDisplay::DestroyDataOverlay() {
    if (data_overlay_ != nullptr) {
        if (lv_obj_is_valid(data_overlay_)) {
            lv_obj_del(data_overlay_);
        }
        data_overlay_ = nullptr;
    }
    if (content_ != nullptr) {
        lv_obj_clear_flag(content_, LV_OBJ_FLAG_HIDDEN);
    }
    if (bottom_bar_ != nullptr) {
        lv_obj_clear_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
    }
}

lv_obj_t* CustomLcdDisplay::CreateDataOverlay(const char* title) {
    DestroyDataOverlay();
    if (content_ != nullptr) {
        lv_obj_add_flag(content_, LV_OBJ_FLAG_HIDDEN);
    }
    if (bottom_bar_ != nullptr) {
        lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
    }
    auto theme = static_cast<LvglTheme*>(GetTheme());
    lv_obj_t* overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, theme->background_color(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(overlay);

    const lv_font_t* font = theme->text_font() ? theme->text_font()->font() : nullptr;
    lv_obj_t* title_label = lv_label_create(overlay);
    lv_obj_set_style_text_color(title_label, theme->text_color(), 0);
    if (font) {
        lv_obj_set_style_text_font(title_label, font, 0);
    }
    lv_label_set_text(title_label, title);
    lv_obj_set_width(title_label, 360);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 10);
    return overlay;
}

static const lv_font_t* jc_text_font(CustomLcdDisplay* self) {
    auto theme = static_cast<LvglTheme*>(self->GetTheme());
    return theme->text_font() ? theme->text_font()->font() : nullptr;
}

static lv_obj_t* jc_label(lv_obj_t* parent, CustomLcdDisplay* self, const char* text,
                          int opa_pct) {
    auto disp_theme = static_cast<LvglTheme*>(self->GetTheme());
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_color(l, disp_theme->text_color(), 0);
    lv_obj_set_style_text_opa(l, (lv_opa_t)(255 * opa_pct / 100), 0);
    if (jc_text_font(self)) {
        lv_obj_set_style_text_font(l, jc_text_font(self), 0);
    }
    lv_label_set_text(l, text);
    return l;
}

void CustomLcdDisplay::ShowValueCard(const char* title, const char* label,
                                     const char* value, const char* unit) {
    DisplayLockGuard lock(this);
    if (!setup_ui_called_) return;
    lv_obj_t* overlay = CreateDataOverlay(title);

    std::string full = value;
    if (unit && strlen(unit) > 0) {
        full += " ";
        full += unit;
    }
    lv_obj_t* val = jc_label(overlay, this, full.c_str(), 100);
    lv_obj_set_style_transform_scale(val, 640, 0);  // x2.5
    lv_obj_align(val, LV_ALIGN_TOP_MID, 0, 78);

    // Only show the sub-label when it adds information (not a duplicate title)
    if (label && strlen(label) > 0 && strcmp(label, title) != 0) {
        lv_obj_t* sub = jc_label(overlay, this, label, 70);
        lv_obj_set_width(sub, 360);
        lv_label_set_long_mode(sub, LV_LABEL_LONG_DOT);
        lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 210);
    }
    data_overlay_ = overlay;
}

void CustomLcdDisplay::ShowValueGrid(const char* title,
                                     const std::vector<std::string>& labels,
                                     const std::vector<std::string>& values) {
    DisplayLockGuard lock(this);
    if (!setup_ui_called_) return;
    lv_obj_t* overlay = CreateDataOverlay(title);
    size_t n = labels.size() < values.size() ? labels.size() : values.size();
    if (n > 4) n = 4;
    // 2 columns x 2 rows inside 400x300, below the title
    for (size_t i = 0; i < n; ++i) {
        int cx = (n == 1) ? 0 : ((i % 2 == 0) ? -95 : 95);
        int cy = (n <= 2) ? 0 : ((i < 2) ? -35 : 55);
        lv_obj_t* val = jc_label(overlay, this, values[i].c_str(), 100);
        lv_obj_set_style_transform_scale(val, 448, 0);  // x1.75
        lv_obj_align(val, LV_ALIGN_TOP_MID, cx, 95 + cy);

        lv_obj_t* lab = jc_label(overlay, this, labels[i].c_str(), 70);
        lv_obj_set_width(lab, 170);
        lv_label_set_long_mode(lab, LV_LABEL_LONG_DOT);
        lv_obj_align(lab, LV_ALIGN_TOP_MID, cx, 145 + cy);
    }
    data_overlay_ = overlay;
}

void CustomLcdDisplay::ShowGraph(const char* title, const std::vector<float>& points,
                                 const std::vector<std::string>& labels, const char* unit) {
    DisplayLockGuard lock(this);
    if (!setup_ui_called_) return;
    if (points.size() < 2) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f", points.empty() ? 0.0f : points.back());
        ShowValueCard(title, "", buf, unit);
        return;
    }
    lv_obj_t* overlay = CreateDataOverlay(title);
    auto theme = static_cast<LvglTheme*>(GetTheme());

    // plot area inside 400x300: x 40..360, y 70..230
    const int plot_x = 40, plot_w = 320;
    const int plot_y = 70, plot_h = 160;

    float vmin = points[0], vmax = points[0];
    for (float p : points) {
        if (p < vmin) vmin = p;
        if (p > vmax) vmax = p;
    }
    if (vmax - vmin < 1e-6f) { vmax += 0.5f; vmin -= 0.5f; }

    static lv_point_precise_t line_pts[64];
    const size_t total = points.size();
    const size_t n = total < 64 ? total : 64;
    for (size_t i = 0; i < n; ++i) {
        size_t idx = (n == total) ? i : (i * (total - 1)) / (n - 1);
        float p = points[idx];
        line_pts[i].x = plot_x + (int)(((float)idx / (float)(total - 1)) * plot_w);
        line_pts[i].y = plot_y + plot_h - (int)(((p - vmin) / (vmax - vmin)) * plot_h);
    }

    lv_obj_t* line = lv_line_create(overlay);
    lv_line_set_points(line, line_pts, n);
    lv_obj_set_style_line_width(line, 3, 0);
    lv_obj_set_style_line_color(line, theme->text_color(), 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    lv_obj_set_pos(line, 0, 0);

    // min/max captions at the right edge of the plot
    if (jc_text_font(this)) {
        char buf[24];
        auto mx = jc_label(overlay, this, "", 50);
        snprintf(buf, sizeof(buf), "%.1f", vmax);
        lv_label_set_text(mx, buf);
        lv_obj_align(mx, LV_ALIGN_TOP_MID, 160, plot_y - 4);
        auto mn = jc_label(overlay, this, "", 50);
        snprintf(buf, sizeof(buf), "%.1f", vmin);
        lv_label_set_text(mn, buf);
        lv_obj_align(mn, LV_ALIGN_TOP_MID, 160, plot_y + plot_h - 16);
    }

    // first / last time labels + unit
    if (!labels.empty()) {
        auto t0 = jc_label(overlay, this, labels.front().c_str(), 70);
        lv_obj_align(t0, LV_ALIGN_TOP_LEFT, 40, 240);
        auto t1 = jc_label(overlay, this, labels.back().c_str(), 70);
        lv_obj_align(t1, LV_ALIGN_TOP_RIGHT, -40, 240);
    }
    if (unit && strlen(unit) > 0) {
        auto u = jc_label(overlay, this, unit, 70);
        lv_obj_align(u, LV_ALIGN_TOP_MID, 0, 240);
    }
    data_overlay_ = overlay;
}

// The data overlay stays up while Jarvis talks (assistant text) and through
// idle cleanup; only a NEW USER UTTERANCE dismisses it, so the graph/value
// remains visible until the user speaks again.
void CustomLcdDisplay::SetChatMessage(const char* role, const char* content) {
    if (role != nullptr && strcmp(role, "user") == 0) {
        DestroyDataOverlay();
    }
    LcdDisplay::SetChatMessage(role, content);
}

void CustomLcdDisplay::ClearChatMessages() {
    // Keep the data overlay: idle cleanup must not hide the graph.
    LcdDisplay::ClearChatMessages();
}


// ---- Weather icon: drawn with LVGL primitives (no weather glyph exists in
// any font on the device: the emoji font only has 21 emotion faces and the
// Material Symbols subset has no weather icons).
static void jc_draw_sun(lv_obj_t* parent, int cx, int cy, int r, lv_color_t c) {
    lv_obj_t* core = lv_obj_create(parent);
    lv_obj_set_size(core, r * 2, r * 2);
    lv_obj_set_style_radius(core, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(core, c, 0);
    lv_obj_set_style_bg_opa(core, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(core, 0, 0);
    lv_obj_set_pos(core, cx - r, cy - r);
    // rays
    for (int i = 0; i < 8; ++i) {
        lv_obj_t* ray = lv_obj_create(parent);
        lv_obj_set_size(ray, 3, r / 2);
        lv_obj_set_style_bg_color(ray, c, 0);
        lv_obj_set_style_bg_opa(ray, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(ray, 0, 0);
        lv_obj_set_style_radius(ray, 2, 0);
        lv_obj_set_pos(ray, cx - 1, cy - r - r / 2 - 4);
        lv_obj_set_style_transform_pivot_x(ray, 1, 0);
        lv_obj_set_style_transform_pivot_y(ray, r + r / 2 + 4, 0);
        lv_obj_set_style_transform_rotation(ray, i * 450, 0);  // 0.1 deg units
    }
}

static void jc_draw_cloud(lv_obj_t* parent, int cx, int cy, int w, lv_color_t c) {
    const int h = w * 0.55;
    struct { int dx, dy, d; } puffs[] = {
        {-w / 3, 0, (int)(w * 0.52)}, {0, -h / 4, (int)(w * 0.62)},
        {w / 3, 0, (int)(w * 0.50)}, {-w / 6, h / 5, (int)(w * 0.45)},
        {w / 6, h / 5, (int)(w * 0.45)},
    };
    for (auto& p : puffs) {
        lv_obj_t* o = lv_obj_create(parent);
        lv_obj_set_size(o, p.d, p.d);
        lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(o, c, 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(o, 0, 0);
        lv_obj_set_pos(o, cx + p.dx - p.d / 2, cy + p.dy - p.d / 2);
    }
}

static void jc_draw_rain(lv_obj_t* parent, int cx, int cy, int w, lv_color_t c, int drops) {
    for (int i = 0; i < drops; ++i) {
        lv_obj_t* d = lv_obj_create(parent);
        lv_obj_set_size(d, 3, 12);
        lv_obj_set_style_bg_color(d, c, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(d, 0, 0);
        lv_obj_set_style_radius(d, 2, 0);
        lv_obj_set_pos(d, cx - w / 3 + i * (w / (drops > 1 ? drops - 1 : 1)), cy);
    }
}

static void jc_draw_snow(lv_obj_t* parent, int cx, int cy, int w, lv_color_t c, int flakes) {
    for (int i = 0; i < flakes; ++i) {
        lv_obj_t* f = lv_obj_create(parent);
        lv_obj_set_size(f, 7, 7);
        lv_obj_set_style_radius(f, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(f, c, 0);
        lv_obj_set_style_bg_opa(f, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(f, 0, 0);
        lv_obj_set_pos(f, cx - w / 3 + i * (w / (flakes > 1 ? flakes - 1 : 1)), cy);
    }
}

static void jc_draw_bolt(lv_obj_t* parent, int cx, int cy, lv_color_t c) {
    static lv_point_precise_t pts[] = {{10, 0}, {-4, 16}, {4, 16}, {-8, 32}};
    lv_obj_t* l = lv_line_create(parent);
    lv_line_set_points(l, pts, 4);
    lv_obj_set_style_line_width(l, 5, 0);
    lv_obj_set_style_line_color(l, c, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_set_pos(l, cx, cy);
}

static void jc_draw_fog(lv_obj_t* parent, int cx, int cy, int w, lv_color_t c) {
    for (int i = 0; i < 3; ++i) {
        lv_obj_t* l = lv_obj_create(parent);
        lv_obj_set_size(l, w - i * 12, 4);
        lv_obj_set_style_bg_color(l, c, 0);
        lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(l, 0, 0);
        lv_obj_set_style_radius(l, 2, 0);
        lv_obj_set_pos(l, cx - (w - i * 12) / 2, cy + i * 11);
    }
}

// condition string -> icon drawing
static void jc_draw_weather_icon(lv_obj_t* parent, const char* cond, int cx, int cy,
                                 lv_color_t fg) {
    std::string c = cond ? cond : "";
    if (c == "sunny" || c == "clear-night") {
        jc_draw_sun(parent, cx, cy, 26, fg);
    } else if (c == "partlycloudy") {
        jc_draw_sun(parent, cx - 18, cy - 12, 18, fg);
        jc_draw_cloud(parent, cx + 8, cy + 6, 62, fg);
    } else if (c == "cloudy") {
        jc_draw_cloud(parent, cx - 10, cy - 8, 56, fg);
        jc_draw_cloud(parent, cx + 12, cy + 4, 66, fg);
    } else if (c == "rainy" || c == "pouring") {
        jc_draw_cloud(parent, cx, cy - 14, 66, fg);
        jc_draw_rain(parent, cx, cy + 20, 62, fg, c == "pouring" ? 5 : 3);
    } else if (c == "snowy" || c == "snowy-rainy") {
        jc_draw_cloud(parent, cx, cy - 14, 66, fg);
        jc_draw_snow(parent, cx, cy + 20, 62, fg, 4);
    } else if (c == "lightning" || c == "lightning-rainy") {
        jc_draw_cloud(parent, cx, cy - 16, 66, fg);
        jc_draw_bolt(parent, cx - 6, cy + 8, fg);
    } else if (c == "fog") {
        jc_draw_fog(parent, cx, cy - 10, 70, fg);
    } else if (c == "windy" || c == "windy-variant") {
        jc_draw_fog(parent, cx, cy - 8, 70, fg);
    } else {
        jc_draw_cloud(parent, cx, cy, 66, fg);
    }
}

// Weather screen — explicit layout in a 400x300 panel (landscape).
//   title   y 10..34   (left aligned, small)
//   icon    x 20..150, y 50..180
//   temp    x 165..385, y 55..135 (scaled, left aligned so it cannot clip)
//   cond    x 165..385, y 140..170
//   details y 200..232 (centered, 1 line)
//   forecast y 240..272 (centered, 1 line)
void CustomLcdDisplay::ShowWeather(const char* title, const char* icon, const char* value,
                                   const char* unit, const char* label, const char* details) {
    DisplayLockGuard lock(this);
    if (!setup_ui_called_) return;
    lv_obj_t* overlay = CreateDataOverlay(title);
    auto theme = static_cast<LvglTheme*>(GetTheme());
    lv_color_t fg = theme->text_color();
    const lv_font_t* font = jc_text_font(this);

    // icon, drawn with primitives (no weather glyphs exist in the fonts)
    jc_draw_weather_icon(overlay, icon, 84, 118, fg);

    // big temperature: place at an explicit position, left-aligned, and let
    // LVGL size the label (transform_scale does not change the layout box,
    // so aligning a scaled label is unreliable).
    std::string full = value;
    if (unit && strlen(unit) > 0) {
        full += " ";
        full += unit;
    }
    lv_obj_t* val = lv_label_create(overlay);
    lv_obj_set_style_text_color(val, fg, 0);
    if (font) lv_obj_set_style_text_font(val, font, 0);
    lv_obj_set_style_transform_pivot_x(val, 0, 0);   // scale from top-left
    lv_obj_set_style_transform_pivot_y(val, 0, 0);
    lv_obj_set_style_transform_scale(val, 576, 0);   // x2.25
    lv_label_set_text(val, full.c_str());
    lv_obj_set_pos(val, 168, 62);

    if (label && strlen(label) > 0) {
        lv_obj_t* lab = lv_label_create(overlay);
        lv_obj_set_style_text_color(lab, fg, 0);
        lv_obj_set_style_text_opa(lab, 220, 0);
        if (font) lv_obj_set_style_text_font(lab, font, 0);
        lv_label_set_text(lab, label);
        lv_obj_set_width(lab, 215);
        lv_label_set_long_mode(lab, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(lab, 168, 140);
    }

    // details / forecast: one line each, centered, ellipsized inside the panel
    if (details && strlen(details) > 0) {
        // details may carry a newline separating details from the forecast
        std::string d = details;
        std::string second;
        auto nl = d.find('\n');
        if (nl != std::string::npos) {
            second = d.substr(nl + 1);
            d = d.substr(0, nl);
        }
        lv_obj_t* det = lv_label_create(overlay);
        lv_obj_set_style_text_color(det, fg, 0);
        lv_obj_set_style_text_opa(det, 160, 0);
        if (font) lv_obj_set_style_text_font(det, font, 0);
        lv_obj_set_style_text_align(det, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(det, d.c_str());
        lv_obj_set_width(det, 380);
        lv_label_set_long_mode(det, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(det, 10, 206);

        if (!second.empty()) {
            lv_obj_t* fc = lv_label_create(overlay);
            lv_obj_set_style_text_color(fc, fg, 0);
            lv_obj_set_style_text_opa(fc, 200, 0);
            if (font) lv_obj_set_style_text_font(fc, font, 0);
            lv_obj_set_style_text_align(fc, LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_text(fc, second.c_str());
            lv_obj_set_width(fc, 380);
            lv_label_set_long_mode(fc, LV_LABEL_LONG_DOT);
            lv_obj_set_pos(fc, 10, 240);
        }
    }
    data_overlay_ = overlay;
}
