#include "led_driver.h"
#include "pixel_encode.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"

static spi_device_handle_t s_spi;
static uint8_t s_spi_buf[RING_SPI_BUF_SIZE];

void led_driver_init() {
    spi_bus_config_t bus = {};
    bus.mosi_io_num    = PIN_RING_DATA;
    bus.miso_io_num    = -1;
    bus.sclk_io_num    = PIN_RING_CLK_DUMMY;
    bus.quadwp_io_num  = -1;
    bus.quadhd_io_num  = -1;
    bus.max_transfer_sz = RING_SPI_BUF_SIZE;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev = {};
    dev.clock_speed_hz = RING_SPI_CLK_HZ;
    dev.mode           = 0;
    dev.spics_io_num   = -1;
    dev.queue_size     = 1;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &s_spi));

    ledc_timer_config_t timer = {};
    timer.speed_mode      = LEDC_LOW_SPEED_MODE;
    timer.timer_num       = LEDC_TIMER_0;
    timer.duty_resolution = PWM_RESOLUTION;
    timer.freq_hz         = PWM_FREQ_HZ;
    timer.clk_cfg         = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t cw = {};
    cw.gpio_num   = PIN_CW_PWM;
    cw.speed_mode = LEDC_LOW_SPEED_MODE;
    cw.channel    = PWM_CHANNEL_CW;
    cw.timer_sel  = LEDC_TIMER_0;
    cw.duty       = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&cw));

    ledc_channel_config_t ww = cw;
    ww.gpio_num = PIN_WW_PWM;
    ww.channel  = PWM_CHANNEL_WW;
    ESP_ERROR_CHECK(ledc_channel_config(&ww));
}

void led_driver_show(const CRGB* leds, uint16_t count) {
    memset(s_spi_buf, 0, RING_SPI_LEAD_BYTES);
    pixel_encode(leds, count, s_spi_buf + RING_SPI_LEAD_BYTES,
                 RING_SPI_BUF_SIZE - RING_SPI_LEAD_BYTES);
    spi_transaction_t t = {};
    t.length    = RING_SPI_BUF_SIZE * 8;
    t.tx_buffer = s_spi_buf;
    spi_device_polling_transmit(s_spi, &t);
}

void led_driver_set_cw(uint8_t level) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL_CW, level);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL_CW);
}

void led_driver_set_ww(uint8_t level) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL_WW, level);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL_WW);
}

void led_driver_off() {
    static const CRGB black[RING_NUM_LEDS] = {};
    led_driver_show(black, RING_NUM_LEDS);
    led_driver_set_cw(0);
    led_driver_set_ww(0);
}
