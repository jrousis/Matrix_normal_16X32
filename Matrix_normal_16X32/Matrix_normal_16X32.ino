// Visual Micro is in vMicro>General>Tutorial Mode
//
/*
    Name:       Matrix_normal_16X32.ino

    16X16 modules project extended to MODULE_Y = 2 (32 pixel height).
    Supports up to 4 small-font lines (5x7 / 7x7) or 2 double lines
    (Big_font), or mixed (e.g. 1 double + 2 small).

    Protocol: M3 Serial Protocol of Rousis Systems.
    Line separator Hex[05], each line followed by its own function byte.
    Function byte: bit7=bold(ignored), bit6=flash, bit5=double line,
                   bits5-0: 00=hold, 01=slide, 03=auto.

    Based on:   Normal_16X16.ino (9/3/2023)
    Author:     ROUSIS_FACTORY\user
*/

#define MODULE_X 5
#define MODULE_Y 2
#define SCAN_TYPE STATIC_SCAN
#define PIXELS_X (MODULE_X * 16)
#define PIXELS_Y (MODULE_Y * 16)

#define BAUD_RATE 250000    //9600

#define MAX_LINES 4          // 4 slots of 8px each (32px total)
#define LINE_BUF  128        // max characters per line

#define DRIVER_PIN_EN 26
#define PHOTO_SAMPLES 60

#define TIMEOUT_MESSAGE false
#define MESSAGE_TIMEOUT_S (5UL * 60UL * 1000UL) // 5 minutes in milliseconds

#include <Arduino.h>
#include <Ds1302.h>
#include <RousisMatrix16_Static.h>
#include <fonts/Big_font.h>
#include <fonts/Big_font_2.h>
#include <fonts/SystemFont5x7_greek.h>
#include <fonts/greek_big_7x7.h>
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

#include <Preferences.h>
Preferences prefs;

#include <OneWire.h>
#include <DallasTemperature.h>

// DS18B20 temperature sensor
#define TEMP_PIN 18              // GPIO 32 is RTC CLK - use 33 instead!
#define TEMP_SAMPLE_DELAY 5000   // new reading every 5 s
#define TEMP_CONVERSION_MS 800   // 12-bit conversion needs ~750 ms

OneWire oneWire(TEMP_PIN);
DallasTemperature sensors(&oneWire);

float temperatureC = 0.0;
bool  temp_ready = false;        // valid reading available
bool  temp_requested = false;    // conversion in progress
unsigned long temp_delay = 0;
unsigned long temp_req_time = 0;

// DS1302 Real Time Clock (ENA, CLK, DAT)
// New board pinout: RTC ENA on GPIO 4, RS485 direction moved to GPIO 5.
#define RTC_PIN_ENA 4
#define RTC_PIN_CLK 32
#define RTC_PIN_DAT 2
Ds1302 rtc(RTC_PIN_ENA, RTC_PIN_CLK, RTC_PIN_DAT);

const char Company[] = { "Rousis LTD" };
const char Device[] = { "Matrix 32" };
const char Version[] = { "V.3.1    " };
const char Init_start[] = { "ROUSIS SYSTEMS" };
static char receive_packet[512] = { 0 };   // enlarged: 4 lines of text

// Photo sensor on GPIO 36
#define PHOTO_SAMPLE_DELAY 1000
const int photoPin = 36;
uint8_t photoValue = 0;
uint8_t brightness = 255;
int sample_metter = -1;
bool bright_sens = true;
uint8_t patern_index = 0;
unsigned long photo_delay = 0;
unsigned long message_timeout = 0;
bool timeout_flag = false;
uint8_t brigthsamples[PHOTO_SAMPLES];

uint16_t _cnt_byte = 0;
uint16_t packet_cnt = 0;
uint8_t Address = 1;
byte Select_font = '0';
bool messages_enable = false;
bool test_enable = false;
bool flash_on = false;

// ---------- per-line state (shared with TaskFlash) ----------
char     line_txt[MAX_LINES][LINE_BUF] = { 0 };
uint8_t  line_cnt[MAX_LINES] = { 0 };      // characters per line
uint16_t line_ctr[MAX_LINES] = { 0 };      // centered x start
uint8_t  line_y[MAX_LINES] = { 0 };        // y position of line
uint8_t  line_dbl[MAX_LINES] = { 0 };      // 1 = double (big font, 2 slots)
uint8_t  line_flash[MAX_LINES] = { 0 };    // 1 = flashing
uint8_t  line_font[MAX_LINES] = { 0 };     // '0' or '1' font per line
uint8_t  line_rtc[MAX_LINES] = { 0 };  // 0=text, 1=clock (DA), 2=date (DB), 3=temperature (DC)
uint8_t  lines_used = 0;                   // how many lines in current page
// -------------------------------------------------------------

static inline uint8_t bcd2dec(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }

RousisMatrix16_Static myLED(MODULE_X, MODULE_Y);

#define LED_PIN 15
#define TEST_PIN 22
#define RS485_PIN_DIR 5   // new board: IO5 (GPIO 4 is now RTC ENA)
#define RXD2 16
#define TXD2 17

HardwareSerial rs485(1);
#define RS485_WRITE 1
#define RS485_READ  0

TaskHandle_t Task0;
TaskHandle_t Task1;

// select the correct font for a line before drawing / measuring
void select_line_font(uint8_t l) {
    if (line_dbl[l]) {
        if (line_font[l] == '1') { myLED.selectFont(Big_font_2); }
        else                     { myLED.selectFont(Big_font); }
    }
    else {
        if (line_font[l] == '1') { myLED.selectFont(greek_big_7x7); }
        else                     { myLED.selectFont(SystemFont5x7_greek); }
    }
}

// fill an RTC/sensor placeholder line with current time/date/temperature text
void fill_rtc_line(uint8_t l) {
    char buf[12];
    if (line_rtc[l] == 3) {                      // temperature (DC)
        if (temp_ready) {
			// for degree symbol, use 0xB0 (°) in the font
            snprintf(buf, sizeof(buf), "%.1f%cC", temperatureC, 0xB0);
        }
        else {
            snprintf(buf, sizeof(buf), "--.-%cC", 0xB0);
        }
    }
    else {
        Ds1302::DateTime now;
        rtc.getDateTime(&now);
        if (line_rtc[l] == 1) {
            snprintf(buf, sizeof(buf), "%02u:%02u", now.hour, now.minute);
        }
        else {
            snprintf(buf, sizeof(buf), "%02u/%02u/%02u", now.day, now.month, now.year);
        }
    }
    line_cnt[l] = 0;
    for (uint8_t k = 0; buf[k] && k < (LINE_BUF - 1); k++) {
        line_txt[l][line_cnt[l]++] = buf[k];
    }
    line_txt[l][line_cnt[l]] = 0;
}

// set the RTC from 7 protocol BCD bytes: ss mm hh dw dd MM yy
void set_rtc_bcd(uint8_t* tb) {
    Ds1302::DateTime dt;
    dt.second = bcd2dec(tb[0]);
    dt.minute = bcd2dec(tb[1]);
    dt.hour   = bcd2dec(tb[2]);
    dt.dow    = bcd2dec(tb[3]);
    dt.day    = bcd2dec(tb[4]);
    dt.month  = bcd2dec(tb[5]);
    dt.year   = bcd2dec(tb[6]);
    rtc.setDateTime(&dt);
    Serial.printf("RTC set: %02u/%02u/%02u %02u:%02u:%02u\n",
                  dt.day, dt.month, dt.year, dt.hour, dt.minute, dt.second);
}

// redraw a single line in place (used for live clock updates)
void redraw_line(uint8_t l) {
    select_line_font(l);
    uint8_t space_px = line_dbl[l] ? 2 : 1;
    uint16_t len = 0;
    for (size_t a = 0; a < line_cnt[l]; a++) {
        len += myLED.charWidth(line_txt[l][a]) + space_px;
    }
    line_ctr[l] = (len - 1) < PIXELS_X ? (PIXELS_X - (len - 1)) / 2 : 0;
    uint8_t h = line_dbl[l] ? 16 : 8;
    myLED.drawFilledBox(0, line_y[l], PIXELS_X, line_y[l] + h - 1, GRAPHICS_OFF);
    myLED.drawString(line_ctr[l], line_y[l], line_txt[l], line_cnt[l], line_dbl[l] ? 2 : 1);
}

// hold delay that refreshes RTC lines every second (breakable like breakable_delay)
void rtc_hold(uint16_t delay_s) {
    myLED.stop_flag = false;
    unsigned long end_time = millis() + (unsigned long)delay_s * 1000;
    char prev[MAX_LINES][12] = { 0 };
    for (uint8_t l = 0; l < lines_used; l++) {
        if (line_rtc[l]) { strncpy(prev[l], line_txt[l], 11); }
    }
    while (millis() < end_time) {
        if (myLED.stop_flag == true) { break; }
        delay(200);
        bool changed = false;
        for (uint8_t l = 0; l < lines_used; l++) {
            if (!line_rtc[l]) { continue; }
            fill_rtc_line(l);
            if (strncmp(prev[l], line_txt[l], 11) != 0) {
                strncpy(prev[l], line_txt[l], 11);
                redraw_line(l);
                changed = true;
            }
        }
        if (changed) {
            while (myLED.display_out_flag) { vTaskDelay(1); }
            myLED.scanDisplay();
        }
    }
}

void setup()
{
    Serial.begin(115200);
    rs485.begin(BAUD_RATE, SERIAL_8N1, RXD2, TXD2);
    pinMode(TEST_PIN, INPUT_PULLUP);
    pinMode(RS485_PIN_DIR, OUTPUT);
    digitalWrite(RS485_PIN_DIR, RS485_READ);
    while (!Serial);
    Serial.println(__FILE__);
    Serial.println(__DATE__);

    Serial.println("Start initializing...");

    rtc.init();
    if (rtc.isHalted()) {
        Serial.println("RTC halted - setting default time 00:00:00 01/01/26");
        Ds1302::DateTime dt = { 26, 1, 1, Ds1302::DOW_THU, 0, 0, 0 }; // y m d dow h m s
        rtc.setDateTime(&dt);
    }

    sensors.begin();
    sensors.setResolution(12);              // 0.0625 C resolution
    sensors.setWaitForConversion(false);    // non-blocking mode
    Serial.print("DS18B20 sensors found: ");
    Serial.println(sensors.getDeviceCount());

    myLED.displayEnable();
    myLED.selectFont(SystemFont5x7_greek); //font1

    while (digitalRead(TEST_PIN) == LOW)
    {
        display_test_paterns();
        delay(2000);
    }

    Photo_sample();
    Serial.print("First sample brightness: ");
    Serial.println(brightness);

    Serial.println("Initialize LED matrix display");
    delay(100);

    Serial.println("Display Initial Message");

    pinMode(DRIVER_PIN_EN, OUTPUT);
    digitalWrite(DRIVER_PIN_EN, LOW);
    myLED.clearDisplay();

    myLED.drawString(0, 0, Company, 10, 1);
    myLED.drawString(0, 8, Device, 10, 1);
    myLED.drawString(0, 16, Version, 10, 1);
    myLED.scanDisplay();
    delay(3000);
    myLED.clearDisplay();
    myLED.drawString(0, 0, Init_start, sizeof(Init_start), 1);
    myLED.scanDisplay();

    delay(100);

    if (load_message_packet()) {
        Serial.println("Restored last message - will display");
    }

    xTaskCreatePinnedToCore(Task0code, "Task0", 10000, NULL, 0, &Task0, 0);
    delay(100);
    xTaskCreatePinnedToCore(TaskFlash, "Task1", 10000, NULL, 1, &Task1, 1);
    delay(100);

    if (TIMEOUT_MESSAGE)
    {
        message_timeout = millis();
    }
}

void Task0code(void* pvParameters) {
    char b = 0;
    uint8_t bright = 0;
    uint16_t cnt_page_byte = 0;
    uint8_t delay_page = 1;
    uint8_t space_px = 1;

    myLED.clearDisplay();
    myLED.selectFont(SystemFont5x7_greek);
    myLED.drawString(0, 0, "Ready", 7, 1);
    myLED.scanDisplay();

    for (;;) {
        if (!test_enable && !timeout_flag) {
            messages_enable = true;
        }

        cnt_page_byte = 0;
        while (messages_enable && cnt_page_byte < packet_cnt && packet_cnt)
        {
            b = receive_packet[cnt_page_byte++];
            if (b != 0x55) { messages_enable = false; }
            b = receive_packet[cnt_page_byte++];
            if (b != 0xAA) { messages_enable = false; }
            b = receive_packet[cnt_page_byte++];
            if (b != 0x00 && b != Address) { messages_enable = false; } // check Address
            b = receive_packet[cnt_page_byte++];
            if (b != 0xA1) { messages_enable = false; }
            b = receive_packet[cnt_page_byte++];
            if (b != 0x02) { messages_enable = false; }

            bool page_en = true;
            while (messages_enable && page_en && cnt_page_byte < packet_cnt)
            {
                b = receive_packet[cnt_page_byte++];
                if (b == 0xE0)
                {
                    bright = receive_packet[cnt_page_byte++];
                }

                if (b == 4) {
                    page_en = false;
                    messages_enable = false;
                    break;
                }

                if (b == 1)
                {
                    // ---------- reset page state ----------
                    delay_page = 1; Select_font = '0';
                    uint8_t funt[MAX_LINES] = { 0 };
                    for (uint8_t l = 0; l < MAX_LINES; l++) {
                        line_flash[l] = 0; line_dbl[l] = 0;
                        line_cnt[l] = 0; line_font[l] = '0';
                        line_rtc[l] = 0;
                        for (uint8_t k = 0; k < LINE_BUF; k++) { line_txt[l][k] = 0; }
                    }
                    uint8_t cur = 0;   // current line index

                    delay_page = receive_packet[cnt_page_byte++] & 0x0f;

                    // function byte of line 1
                    uint8_t function_byte = receive_packet[cnt_page_byte++];
                    funt[0] = function_byte & 0x1F;
                    if (function_byte & 0b00100000) { line_dbl[0] = 1; }
                    if (function_byte & 0b01000000) { line_flash[0] = 1; }

                    // ---------- read text of all lines ----------
                    b = 0xff;
                    while (messages_enable && cnt_page_byte < packet_cnt && b != 0)
                    {
                        b = receive_packet[cnt_page_byte++];
                        switch (b)
                        {
                        case 0xd6: // font select for current line
                            if (receive_packet[cnt_page_byte] == '0' || receive_packet[cnt_page_byte] == '1')
                            {
                                Select_font = receive_packet[cnt_page_byte++];
                                line_font[cur] = Select_font;
                            }
                            else if (b && line_cnt[cur] < (LINE_BUF - 1)) {
                                line_txt[cur][line_cnt[cur]++] = b;
                            }
                            break;
                        case 0xda: // insert clock (time) - line content from RTC
                            line_rtc[cur] = 1;
                            break;
                        case 0xdb: // insert date - line content from RTC
                            line_rtc[cur] = 2;
                            break;
                        case 0xdc: // insert temperature - line content from temperature sensor
                            line_rtc[cur] = 3;
                            break;
                        case 0x05: // line separator -> next line + its function byte
                            if (cur < (MAX_LINES - 1)) { cur++; }
                            function_byte = receive_packet[cnt_page_byte++];
                            funt[cur] = function_byte & 0x1F;
                            if (function_byte & 0b00100000) { line_dbl[cur] = 1; }
                            if (function_byte & 0b01000000) { line_flash[cur] = 1; }
                            line_font[cur] = Select_font; // inherit last font unless changed
                            break;
                        default:
                            if (b && line_cnt[cur] < (LINE_BUF - 1))
                            {
                                line_txt[cur][line_cnt[cur]++] = b;
                            }
                            break;
                        }
                    }
                    lines_used = cur + 1;

                    // ---------- layout: assign y per line, 4 slots of 8px ----------
                    // small line at slot s: y = s*8  -> 0, 8, 16, 24
                    // double line: y = s*8, occupies 2 slots (16px tall)
                    uint8_t slot = 0;
                    for (uint8_t l = 0; l < lines_used; l++)
                    {
                        if (line_dbl[l]) {
                            if (slot > (MAX_LINES - 2)) { line_cnt[l] = 0; continue; } // no room
                            line_y[l] = slot * 8;
                            slot += 2;
                        }
                        else {
                            if (slot > (MAX_LINES - 1)) { line_cnt[l] = 0; continue; } // no room
                            line_y[l] = slot * 8;
                            slot += 1;
                        }
                    }

                    Serial.println("........................................");
                    Serial.print("Lines used: "); Serial.println(lines_used);
                    for (uint8_t l = 0; l < lines_used; l++)
                    {
                        Serial.print("L"); Serial.print(l + 1);
                        Serial.print(" y="); Serial.print(line_y[l]);
                        Serial.print(" dbl="); Serial.print(line_dbl[l]);
                        Serial.print(" flash="); Serial.print(line_flash[l]);
                        Serial.print(" chars="); Serial.print(line_cnt[l]);
                        Serial.print(" txt=\""); Serial.print(line_txt[l]);
                        Serial.println("\"");
                    }

                    if (bright) { myLED.displayBrightness((255 / 16) * bright); }

                    // ---------- measure & center each line ----------
                    uint16_t line_len[MAX_LINES] = { 0 };
                    bool line_scroll[MAX_LINES] = { false };
                    bool any_text = false;
                    bool any_scroll = false;
                    bool any_rtc = false;
                    for (uint8_t l = 0; l < lines_used; l++)
                    {
                        if (line_rtc[l]) { fill_rtc_line(l); any_rtc = true; }
                        if (!line_cnt[l]) { continue; }
                        any_text = true;
                        select_line_font(l);
                        space_px = line_dbl[l] ? 2 : 1;
                        line_len[l] = 0;
                        for (size_t a = 0; a < line_cnt[l]; a++)
                        {
                            line_len[l] += myLED.charWidth(line_txt[l][a]) + space_px;
                        }
                        line_ctr[l] = (line_len[l] - 1) < PIXELS_X ?
                                      (PIXELS_X - (line_len[l] - 1)) / 2 : 0;
                        if (line_len[l] > PIXELS_X || funt[l] == 1)
                        {
                            line_scroll[l] = true;
                            line_flash[l] = 0;  // scrolling line cannot flash
                            any_scroll = true;
                        }
                    }

                    // ---------- draw ----------
                    if (any_text)
                    {
                        myLED.clearDisplay();
                        delay_page++;

                        // static lines first
                        for (uint8_t l = 0; l < lines_used; l++)
                        {
                            if (!line_cnt[l] || line_scroll[l]) { continue; }
                            select_line_font(l);
                            myLED.drawString(line_ctr[l], line_y[l], line_txt[l],
                                             line_cnt[l], line_dbl[l] ? 2 : 1);
                        }
                        myLED.scanDisplay();

                        // scrolling lines (blocking), one after the other
                        for (uint8_t l = 0; l < lines_used; l++)
                        {
                            if (!line_cnt[l] || !line_scroll[l]) { continue; }
                            select_line_font(l);
                            myLED.scrollingString(0, line_y[l], line_txt[l],
                                                  line_cnt[l],
                                                  line_dbl[l] ? 2 : 1, delay_page);
                        }

                        if (!any_scroll)
                        {
                            if (any_rtc) { rtc_hold(delay_page); }
                            else { breakable_delay((delay_page) * 1000); }
                        }
                    }
                }
            }
        }
        vTaskDelay(10);
    }
}

void TaskFlash(void* pvParameters) {
    for (;;) {
        if (flash_on)
        {
            // redraw flashing lines
            for (uint8_t l = 0; l < lines_used; l++)
            {
                if (!line_flash[l] || !line_cnt[l]) { continue; }
                select_line_font(l);
                myLED.drawString(line_ctr[l], line_y[l], line_txt[l],
                                 line_cnt[l], line_dbl[l] ? 2 : 1);
                while (myLED.display_out_flag) { vTaskDelay(1); }
                myLED.scanDisplay();
            }
            flash_on = false;
        }
        else {
            // blank the region of each flashing line
            for (uint8_t l = 0; l < lines_used; l++)
            {
                if (!line_flash[l] || !line_cnt[l]) { continue; }
                uint8_t h = line_dbl[l] ? 16 : 8;
                myLED.drawFilledBox(0, line_y[l], PIXELS_X, line_y[l] + h - 1, GRAPHICS_OFF);
                while (myLED.display_out_flag) { vTaskDelay(1); }
                myLED.scanDisplay();
            }
            flash_on = true;
        }
        // we need delay 500 ms to not occupy all the CPU time
        vTaskDelay(500);
    }
}

// Add the main program code into the continuous loop() function
void loop()
{
    uint8_t header_cnt[4] = { 0,0,0,0 };
    bool loop_stop = false;
    unsigned long startedWaiting = millis();
    uint16_t receiving_count = 0;

    if (rs485.available())
    {
        uint8_t unstruction = 0;
        byte get_byte;
        do {
            get_byte = rs485.read();
            if (get_byte == 0xCA)
            {
                _cnt_byte = 0; packet_cnt = 0;
                loop_stop = true;
                while (millis() - startedWaiting <= 300 && loop_stop) {
                    if (rs485.available())
                    {
                        header_cnt[_cnt_byte++] = rs485.read();
                        if (_cnt_byte >= 4)
                        {
                            unstruction = 1;
                            _cnt_byte = header_cnt[2] << 8;
                            _cnt_byte |= header_cnt[3];
                            packet_cnt = _cnt_byte;
                            if (packet_cnt > sizeof(receive_packet)) {
                                packet_cnt = sizeof(receive_packet);
                            }
                            delay(10);
                            get_byte = 0;
                            loop_stop = false;
                        }
                        startedWaiting = millis();
                    }
                }
            }

            if (get_byte == 0x01 && unstruction == 0x01) // receive the main packet
            {
                _cnt_byte--;
                while (millis() - startedWaiting <= 300)
                {
                    if (rs485.available())
                    {
                        if (receiving_count < sizeof(receive_packet)) {
                            receive_packet[receiving_count++] = rs485.read();
                        }
                        else {
                            rs485.read(); // discard overflow
                        }
                        _cnt_byte--;
                        if (_cnt_byte == 0)
                        {
                            packet_cnt--;
                            for (size_t i = (packet_cnt); i < (sizeof(receive_packet) - 1); i++)
                            {
                                receive_packet[i] = 0xff;
                            }
                            unstruction = 0xA1;

                            // ---- DEBUG: hex dump of received packet ----
                            Serial.println("Received_packet:");
                            uint8_t line_br = 0;
                            for (size_t i = 0; i < packet_cnt; i++)
                            {
                                Serial.print((uint8_t)receive_packet[i], HEX);
                                line_br++;
                                if (line_br > 31) { line_br = 0; Serial.println(); }
                                else { Serial.print(" "); }
                            }
                            Serial.println();
                            Serial.println("_________________________________");
                            // --------------------------------------------

                            delay(10);

                            // A2 = Set Clock packet: consume here, not a message
                            if ((uint8_t)receive_packet[3] == 0xA2 && packet_cnt >= 11)
                            {
                                set_rtc_bcd((uint8_t*)&receive_packet[4]);
                                Replay_OK();
                                packet_cnt = 0; // nothing for the message parser
                                break;
                            }

                            Replay_OK();
                            save_message_packet();        // persist to NVS
                            if (TIMEOUT_MESSAGE) { message_timeout = millis(); }
                            myLED.stop_flag = true;
                            timeout_flag = false;
                            messages_enable = false;
                            break;
                        }
                        startedWaiting = millis();
                    }
                }
            }
            else if (get_byte == 0x01)
            {
                _cnt_byte = 2;
                while (millis() - startedWaiting <= 300) {
                    if (rs485.available())
                    {
                        get_byte = rs485.read();
                        if (_cnt_byte == 2 && get_byte == 0x55)
                        {
                            _cnt_byte = 3;
                        }
                        else if (_cnt_byte == 3 && get_byte == 0xAA)
                        {
                            _cnt_byte = 4;
                        }
                        else if (_cnt_byte == 4 && (Address == get_byte || get_byte == 0))
                        {
                            _cnt_byte = 5;
                        }
                        else if (_cnt_byte == 5)
                        {
                            unstruction = get_byte;
                            _cnt_byte = 0;
                            startedWaiting = millis();
                            break;
                        }
                        startedWaiting = millis();
                    }
                }

                if (unstruction == 0xA2) { // instruction set clock (BCD: ss mm hh dw dd MM yy, FF)
                    uint8_t tb[7] = { 0 };
                    uint8_t got = 0;
                    while (millis() - startedWaiting <= 300 && got < 8) {
                        if (rs485.available()) {
                            uint8_t v = rs485.read();
                            if (got < 7) { tb[got] = v; }
                            got++; // 8th byte is the FF terminator
                            startedWaiting = millis();
                        }
                    }
                    if (got >= 7) {
                        set_rtc_bcd(tb);
                        delay(10);
                        Replay_OK();                            
                    }
                    _cnt_byte = 0;
                    unstruction = 0;
                }

                if (unstruction == 0xA3) { // instruction receive loop
                    delay(100);
                    char Sent_Pckt[] = { 0xAA, 0x55, '4', 'L' };
                    char Sign_ID[] = { "/Matrix80X32/M3/V7.2/Rousis" };
                    digitalWrite(RS485_PIN_DIR, RS485_WRITE);
                    for (size_t i = 0; i < sizeof(Sent_Pckt); i++)
                    {
                        rs485.write(Sent_Pckt[i]);
                    }
                    for (size_t i = 0; i < sizeof(Sign_ID); i++)
                    {
                        rs485.write(Sign_ID[i]);
                    }
                    rs485.flush();
                    digitalWrite(RS485_PIN_DIR, RS485_READ);
                    _cnt_byte = 0;
                    unstruction = 0;
                }

                if (unstruction == 0xAF) { // instruction test
                    test_enable = true;
                    display_test_paterns();
                    delay(500);

                    Replay_OK();
                    test_enable = false;
                    _cnt_byte = 0;
                    unstruction = 0;
                }
            }
        } while (rs485.available() > 0);
    }

    Temp_sample();

    if ((millis() - photo_delay) > PHOTO_SAMPLE_DELAY)
    {
        Photo_sample();
        photo_delay = millis();
    }

#if TIMEOUT_MESSAGE
    const unsigned long now = millis();
    if (messages_enable && (now - message_timeout) > MESSAGE_TIMEOUT_S)
    {
        myLED.stop_flag = true;
        messages_enable = false;
        timeout_flag = true;

        for (uint8_t l = 0; l < MAX_LINES; l++) { line_flash[l] = 0; line_dbl[l] = 0; }

        myLED.clearDisplay();
        myLED.scanDisplay();

        message_timeout = now;

        Serial.println(F("Message timeout - Clear display"));
    }
#endif

    delay(10);
}

void Replay_OK(void) {
    char Sent_Pckt[] = { 0xAA, 0x55,'O', 'K', '!' };
    digitalWrite(RS485_PIN_DIR, RS485_WRITE);
    for (size_t i = 0; i < sizeof(Sent_Pckt); i++)
    {
        rs485.write(Sent_Pckt[i]);
    }
    rs485.flush();
    digitalWrite(RS485_PIN_DIR, RS485_READ);
}

void Photo_sample() {
    if (!bright_sens)
    {
        return;
    }

    photoValue = (255 / 4095.0) * analogRead(photoPin);
    photoValue = 255 - photoValue; // Invert the value for brightness

    if (sample_metter == -1) {
        sample_metter = 0;
        brightness = photoValue;
        if (!brightness) { brightness = 10; }
        myLED.displayBrightness(brightness);
        Serial.println();
        Serial.print("First Sensor brightness = ");
        Serial.println(brightness);
    }

    if (sample_metter < PHOTO_SAMPLES)
    {
        brigthsamples[sample_metter++] = photoValue;
        return;
    }
    else {
        int sum_smpl = 0;
        for (size_t i = 0; i < PHOTO_SAMPLES; i++)
        {
            sum_smpl += brigthsamples[i];
        }
        brightness = sum_smpl / PHOTO_SAMPLES;
        sample_metter = 0;
        if (!brightness) { brightness = 10; }
        myLED.displayBrightness(brightness);

        Serial.println();
        Serial.print("New average brightness: ");
        Serial.println(brightness);
    }
}

void display_test_paterns() {
    myLED.clearDisplay();
    if (patern_index == 0) {
        myLED.drawFilledBox(0, 0, PIXELS_X - 1, PIXELS_Y - 1, GRAPHICS_ON);
        myLED.scanDisplay();
        patern_index++;
        return;
    }
    else if (patern_index == 1) {
        for (size_t x = 0; x < PIXELS_X; x += 2)
        {
            for (size_t y = 0; y < PIXELS_Y; y++)
            {
                myLED.writePixel(x, y, 1);
            }
        }
        myLED.scanDisplay();
        patern_index++;
        return;
    }
    else if (patern_index == 2) {
        for (size_t x = 1; x < PIXELS_X; x += 2)
        {
            for (size_t y = 0; y < PIXELS_Y; y++)
            {
                myLED.writePixel(x, y, 1);
            }
        }
        myLED.scanDisplay();
        patern_index = 3;
        return;
    }
    else if (patern_index == 3) {
        for (size_t y = 0; y < PIXELS_Y; y += 2)
        {
            for (size_t x = 0; x < PIXELS_X; x++)
            {
                myLED.writePixel(x, y, 1);
            }
        }
        myLED.scanDisplay();
        patern_index++;
        return;
    }
    else if (patern_index == 4) {
        for (size_t y = 1; y < PIXELS_Y; y += 2)
        {
            for (size_t x = 0; x < PIXELS_X; x++)
            {
                myLED.writePixel(x, y, 1);
            }
        }
        myLED.scanDisplay();
        patern_index = 0;
        return;
    }
}

//Function for breakable message delay
void breakable_delay(uint16_t delay_time) {
    myLED.stop_flag = false;
    unsigned long start_time = millis() + delay_time;
    while (millis() < start_time) {
        if (myLED.stop_flag == true) {
            break;
        }
        delay(10);
    }
}

void Temp_sample() {
    // phase 1: start a conversion every TEMP_SAMPLE_DELAY
    if (!temp_requested && (millis() - temp_delay) > TEMP_SAMPLE_DELAY) {
        sensors.requestTemperatures();   // returns immediately (async)
        temp_requested = true;
        temp_req_time = millis();
        temp_delay = millis();
    }
    // phase 2: collect the result after conversion time
    if (temp_requested && (millis() - temp_req_time) >= TEMP_CONVERSION_MS) {
        float t = sensors.getTempCByIndex(0);
        if (t != DEVICE_DISCONNECTED_C && t > -55.0 && t < 125.0) {
            temperatureC = t;
            temp_ready = true;
        }
        else {
            temp_ready = false;
            Serial.println("DS18B20: no valid reading");
        }
        temp_requested = false;
    }
}

// save the current A1 message packet to NVS (skips identical content)
void save_message_packet() {
    if ((uint8_t)receive_packet[3] != 0xA1) { return; }   // only messages, not A2/A3/AF

    prefs.begin("matrix", false);
    // avoid rewriting the same packet (reduces flash wear)
    uint16_t old_len = prefs.getUShort("len", 0);
    if (old_len == packet_cnt) {
        static char tmp[sizeof(receive_packet)];
        size_t got = prefs.getBytes("pkt", tmp, old_len);
        if (got == old_len && memcmp(tmp, receive_packet, old_len) == 0) {
            prefs.end();
            return;                                       // identical, nothing to do
        }
    }
    prefs.putUShort("len", packet_cnt);
    prefs.putBytes("pkt", receive_packet, packet_cnt);
    prefs.end();
    Serial.printf("Packet saved to NVS (%u bytes)\n", packet_cnt);
}

// restore the last message packet from NVS; returns true if a valid packet was loaded
bool load_message_packet() {
    prefs.begin("matrix", true);
    uint16_t len = prefs.getUShort("len", 0);
    if (len < 6 || len > sizeof(receive_packet)) { prefs.end(); return false; }
    size_t got = prefs.getBytes("pkt", receive_packet, len);
    prefs.end();
    if (got != len) { return false; }

    for (size_t i = len; i < (sizeof(receive_packet) - 1); i++) {
        receive_packet[i] = 0xff;                         // same padding as the receiver
    }
    packet_cnt = len;
    Serial.printf("Packet restored from NVS (%u bytes)\n", len);
    return true;
}