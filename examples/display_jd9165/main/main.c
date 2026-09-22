/*
 * Minimal JD9165 MIPI-DSI bring-up for GUITION JC1060P470C_I_W_Y
 * Target: ESP-IDF 5.5.x, ESP32-P4 rev 1.0 / 1.3
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_jd9165.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

static const char *TAG = "display_jd9165";

/* --- Board pins (JC1060P470C_I_W_Y) --- */
#define LCD_RST_GPIO           GPIO_NUM_27   /* try GPIO5 if black screen */
#define LCD_BK_LIGHT_GPIO      GPIO_NUM_23
#define MIPI_DSI_PHY_LDO_CH    3
#define MIPI_DSI_PHY_LDO_MV    2500
#define LCD_H_RES              1024
#define LCD_V_RES              600
#define LCD_BIT_PER_PIXEL      16

#define BK_LEDC_TIMER          LEDC_TIMER_0
#define BK_LEDC_MODE           LEDC_LOW_SPEED_MODE
#define BK_LEDC_CHANNEL        LEDC_CHANNEL_0
#define BK_LEDC_DUTY_RES       LEDC_TIMER_10_BIT
#define BK_LEDC_FREQ_HZ        5000

static esp_lcd_panel_handle_t s_panel = NULL;
static esp_lcd_dsi_bus_handle_t s_dsi_bus = NULL;
static esp_ldo_channel_handle_t s_ldo_mipi = NULL;

static esp_err_t backlight_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode = BK_LEDC_MODE,
        .duty_resolution = BK_LEDC_DUTY_RES,
        .timer_num = BK_LEDC_TIMER,
        .freq_hz = BK_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_cfg), TAG, "ledc timer");

    ledc_channel_config_t ch_cfg = {
        .gpio_num = LCD_BK_LIGHT_GPIO,
        .speed_mode = BK_LEDC_MODE,
        .channel = BK_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BK_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&ch_cfg), TAG, "ledc channel");
    return ESP_OK;
}

static esp_err_t backlight_set(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    uint32_t duty = (1023 * percent) / 100;
    ESP_RETURN_ON_ERROR(ledc_set_duty(BK_LEDC_MODE, BK_LEDC_CHANNEL, duty), TAG, "set duty");
    ESP_RETURN_ON_ERROR(ledc_update_duty(BK_LEDC_MODE, BK_LEDC_CHANNEL), TAG, "update duty");
    return ESP_OK;
}

static esp_err_t display_init(void)
{
    /* 1) Power MIPI-DSI PHY */
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = MIPI_DSI_PHY_LDO_CH,
        .voltage_mv = MIPI_DSI_PHY_LDO_MV,
    };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &s_ldo_mipi), TAG, "LDO ch3");
    ESP_LOGI(TAG, "MIPI DSI PHY LDO ch%d @ %d mV", MIPI_DSI_PHY_LDO_CH, MIPI_DSI_PHY_LDO_MV);

    /* 2) DSI bus (2-lane) */
    esp_lcd_dsi_bus_config_t bus_cfg = JD9165_PANEL_BUS_DSI_2CH_CONFIG();
    /* Guition demos often use ~550–900 Mbps; start with component default, lower if unstable */
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_cfg, &s_dsi_bus), TAG, "dsi bus");

    /* 3) DBI panel IO */
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_dbi_io_config_t dbi_cfg = JD9165_PANEL_IO_DBI_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(s_dsi_bus, &dbi_cfg, &io), TAG, "dbi io");

    /* 4) DPI timing 1024x600 @ ~60 Hz, RGB565 */
    esp_lcd_dpi_panel_config_t dpi_cfg = JD9165_1024_600_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565);
    dpi_cfg.num_fbs = 1;
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 0)
    dpi_cfg.flags.use_dma2d = true;
#endif

    jd9165_vendor_config_t vendor_cfg = {
        .mipi_config = {
            .dsi_bus = s_dsi_bus,
            .dpi_config = &dpi_cfg,
        },
    };

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_RST_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .vendor_config = &vendor_cfg,
    };

    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_jd9165(io, &panel_cfg, &s_panel), TAG, "jd9165");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "init");
    /* Some panels need display on explicitly */
    esp_lcd_panel_disp_on_off(s_panel, true);

    ESP_LOGI(TAG, "Display init OK (%dx%d RGB565)", LCD_H_RES, LCD_V_RES);
    return ESP_OK;
}

/* Fill full screen with solid RGB565 color (slow CPU path — ok for bring-up) */
static void fill_color(uint16_t color)
{
    const size_t lines = 20;
    size_t buf_pixels = LCD_H_RES * lines;
    uint16_t *buf = heap_caps_malloc(buf_pixels * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!buf) {
        buf = heap_caps_malloc(buf_pixels * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    }
    if (!buf) {
        ESP_LOGE(TAG, "no mem for fill");
        return;
    }
    for (size_t i = 0; i < buf_pixels; i++) {
        buf[i] = color;
    }
    for (int y = 0; y < LCD_V_RES; y += lines) {
        int h = lines;
        if (y + h > LCD_V_RES) {
            h = LCD_V_RES - y;
        }
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, LCD_H_RES, y + h, buf);
    }
    free(buf);
}

void app_main(void)
{
    ESP_LOGI(TAG, "JC1060P470C_I_W_Y display test (IDF 5.5.x)");

    ESP_ERROR_CHECK(backlight_init());
    ESP_ERROR_CHECK(backlight_set(80));
    ESP_ERROR_CHECK(display_init());

    const uint16_t colors[] = {
        0xF800, /* red */
        0x07E0, /* green */
        0x001F, /* blue */
        0xFFFF, /* white */
        0x0000, /* black */
    };
    const char *names[] = { "RED", "GREEN", "BLUE", "WHITE", "BLACK" };

    while (1) {
        for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); i++) {
            ESP_LOGI(TAG, "fill %s", names[i]);
            fill_color(colors[i]);
            vTaskDelay(pdMS_TO_TICKS(1500));
        }
    }
}
