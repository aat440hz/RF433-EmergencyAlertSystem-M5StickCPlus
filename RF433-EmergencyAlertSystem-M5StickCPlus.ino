#include <driver/rmt.h>
#include "M5StickCPlus.h"

#define RF433RX

#define RMT_TX_CHANNEL  RMT_CHANNEL_0
#define RMT_RX_CHANNEL  RMT_CHANNEL_1
#define RTM_TX_GPIO_NUM 32
#define RTM_RX_GPIO_NUM 33
#define RTM_BLOCK_NUM   1

#define RMT_CLK_DIV   80 /*!< RMT counter clock divider */
#define RMT_1US_TICKS (80000000 / RMT_CLK_DIV / 1000000)
#define RMT_1MS_TICKS (RMT_1US_TICKS * 1000)

rmt_item32_t rmtbuff[2048];

#define T0H 670
#define T1H 320
#define T0L 348
#define T1L 642

#define RMT_CODE_H \
    { 670, 1, 320, 0 }
#define RMT_CODE_L \
    { 348, 1, 642, 0 }
#define RMT_START_CODE0 \
    { 4868, 1, 2469, 0 }
#define RMT_START_CODE1 \
    { 1647, 1, 315, 0 }

#define LED_PIN 10 // Replace with the actual GPIO pin number of the LED

// Define color constants
#define TFT_RED   M5.Lcd.color565(255, 0, 0)
#define TFT_YELLOW M5.Lcd.color565(255, 255, 0)
#define TFT_BLACK  M5.Lcd.color565(0, 0, 0)

void initRMT() {
#ifndef RF433RX
    rmt_config_t txconfig;
    txconfig.rmt_mode                 = RMT_MODE_TX;
    txconfig.channel                  = RMT_TX_CHANNEL;
    txconfig.gpio_num                 = gpio_num_t(RTM_TX_GPIO_NUM);
    txconfig.mem_block_num            = RTM_BLOCK_NUM;
    txconfig.tx_config.loop_en        = false;
    txconfig.tx_config.carrier_en     = false;
    txconfig.tx_config.idle_output_en = true;
    txconfig.tx_config.idle_level     = rmt_idle_level_t(0);
    txconfig.clk_div                  = RMT_CLK_DIV;

    ESP_ERROR_CHECK(rmt_config(&txconfig));
    ESP_ERROR_CHECK(rmt_driver_install(txconfig.channel, 0, 0));
#else
    rmt_config_t rxconfig;
    rxconfig.rmt_mode            = RMT_MODE_RX;
    rxconfig.channel             = RMT_RX_CHANNEL;
    rxconfig.gpio_num            = gpio_num_t(RTM_RX_GPIO_NUM);
    rxconfig.mem_block_num       = 6;
    rxconfig.clk_div             = RMT_CLK_DIV;
    rxconfig.rx_config.filter_en = true;
    rxconfig.rx_config.filter_ticks_thresh =
        200 * RMT_1US_TICKS;
    rxconfig.rx_config.idle_threshold = 3 * RMT_1MS_TICKS;

    ESP_ERROR_CHECK(rmt_config(&rxconfig));
    ESP_ERROR_CHECK(rmt_driver_install(rxconfig.channel, 2048, 0));
#endif
}

uint8_t data[6] = {0xAA, 0x55, 0x01, 0x02, 0x03, 0x04};

void send(uint8_t* buff, size_t size) {
    rmtbuff[0] = (rmt_item32_t){RMT_START_CODE0};
    rmtbuff[1] = (rmt_item32_t){RMT_START_CODE1};
    for (int i = 0; i < size; i++) {
        uint8_t mark = 0x80;
        for (int n = 0; n < 8; n++) {
            rmtbuff[2 + i * 8 + n] = ((buff[i] & mark))
                                         ? ((rmt_item32_t){RMT_CODE_H})
                                         : ((rmt_item32_t){RMT_CODE_L});
            mark >>= 1;
        }
    }
    for (int i = 0; i < 8; i++) {
        ESP_ERROR_CHECK(rmt_write_items(RMT_TX_CHANNEL, rmtbuff, 42, false));
        ESP_ERROR_CHECK(rmt_wait_tx_done(RMT_TX_CHANNEL, portMAX_DELAY));
    }
}

void pagerBeep(int frequency, int duration, int pause) {
    int startTime = millis();
    while (millis() - startTime < duration) {
        // Turn on the LED
        digitalWrite(LED_PIN, HIGH);

        // Beep the buzzer
        M5.Beep.tone(frequency);

        delay(pause);

        // Turn off the LED
        digitalWrite(LED_PIN, LOW);

        M5.Beep.mute();
        delay(pause);
    }
}

void setup() {
    M5.begin();
    // Comment out the display-related lines
    M5.Lcd.setRotation(1);
    M5.Lcd.fillRect(0, 0, 320, 240, TFT_BLACK);
    M5.Lcd.fillRect(0, 0, 320, 20, M5.Lcd.color565(38, 38, 38));
    M5.Lcd.setTextColor(TFT_WHITE);
    M5.Lcd.drawString("Emergency Alert System", 20, 2, 2);
    
    // Configure the LED pin as an output and initialize it to OFF
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
    
    pinMode(RTM_TX_GPIO_NUM, OUTPUT);
    pinMode(RTM_RX_GPIO_NUM, INPUT);
    initRMT();
    delay(100);
    M5.Lcd.setCursor(5, 100);
    M5.Lcd.setTextSize(1);

#ifndef RF433RX
    M5.Lcd.print("Click Btn A for help");
#endif
}

int parsedData(rmt_item32_t* item, size_t size, uint8_t* dataptr,
               size_t maxsize) {
    if (size < 4) return -1;
    int cnt        = 0;
    uint8_t data   = 0;
    uint8_t bitcnt = 0, hex_cnt = 0;
    if (((item[0].level0 == 0)) && (item[0].duration0 > 2300) &&
        (item[0].duration0 < 2600)) {
        rmt_item32_t dataitem;
        dataitem.level0    = 1;
        dataitem.level1    = 0;
        dataitem.duration0 = item[0].duration1;
        do {
            cnt++;
            dataitem.duration1 = item[cnt].duration0;
            if (cnt > 1) {
                if (((dataitem.duration0 + dataitem.duration1) < 1100) &&
                    ((dataitem.duration0 + dataitem.duration1) > 800)) {
                    data <<= 1;
                    if (dataitem.duration0 > dataitem.duration1) {
                        data += 1;
                    }

                    bitcnt++;
                    if (bitcnt >= 8) {
                        if (hex_cnt >= maxsize) {
                            return hex_cnt;
                        }
                        dataptr[hex_cnt] = data;
                        data             = 0;
                        hex_cnt++;
                        bitcnt = 0;
                    }
                } else {
                    return hex_cnt;
                }
            }
            dataitem.duration0 = item[cnt].duration1;
        } while (cnt < size);
    }
    return hex_cnt;
}

void loop() {
#ifndef RF433RX
    if (M5.BtnA.wasPressed()) {
        send(data, 6);
    }
#else
    int revicecnt = 0;

    RingbufHandle_t rb = nullptr;
    rmt_get_ringbuf_handle(RMT_RX_CHANNEL, &rb);
    rmt_rx_start(RMT_RX_CHANNEL, true);
    while (rb) {
        size_t rx_size = 0;
        rmt_item32_t* item = (rmt_item32_t*)xRingbufferReceive(rb, &rx_size, 500);
        if (item != nullptr) {
            if (rx_size != 0) {
                uint8_t databuff[256];
                int size = parsedData(item, rx_size, databuff, 255);
                if ((size >= 5) && (databuff[0] == 0xAA) &&
                    (databuff[1] == 0x55) && (databuff[2] == 0x01) &&
                    (databuff[3] == 0x02) && (databuff[4] == 0x03)) {
                    // Clear the screen to black
                    M5.Lcd.fillScreen(TFT_BLACK);

                    // Draw a smaller red circle in the center of the screen
                    int centerX = M5.Lcd.width() / 2;
                    int centerY = M5.Lcd.height() / 2;
                    int circleRadius = 50;
                    M5.Lcd.fillCircle(centerX, centerY, circleRadius, TFT_RED);

                    // Display the word "ALERT!" above the circle
                    M5.Lcd.setTextColor(TFT_WHITE);
                    M5.Lcd.setTextDatum(MC_DATUM);  // Center the text
                    M5.Lcd.setTextSize(3);  // Adjust text size as needed
                    M5.Lcd.drawString("ALERT!", centerX, centerY - circleRadius - 20, 2);

                    revicecnt++;

                    // Beep like a pager when data is received
                    pagerBeep(1000, 10000, 500); // Beep at 1000Hz for x minutes with a 500ms pause
                }
            }
            vRingbufferReturnItem(rb, (void*)item);
        } else {
            if (revicecnt != 0) {
            }
            revicecnt = 0;
        }
    }
    rmt_rx_stop(RMT_RX_CHANNEL);
#endif

    delay(10);
    M5.update();
}
