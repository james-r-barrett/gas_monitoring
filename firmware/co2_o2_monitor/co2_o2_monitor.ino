#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include "esp_lcd_touch_axs5106l.h"
#include <SD_MMC.h>
#include <DFRobot_OxygenSensor.h>
#define O2_I2C_ADDRESS ADDRESS_3
DFRobot_OxygenSensor oxygen;
bool o2Available = false;
float lastO2 = 0.0;

// --- Pi Network & Sensor Config ---
const char* PI_SSID = "GasMonitor";
const char* PI_PASS = "set yours here";
const char* PI_HOST = "192.168.50.1";
const int   PI_PORT = 5000;
char sensorID[32]   = "sensor1";
char location[32]   = "chamber1";
bool piConnected    = false;
long localTimeOffset = 0;

// ============================================================
//  CO2 Monitor — Display + Touch Cal + WiFi Dashboard
// ============================================================

// --- Display ---
#define GFX_BL   46
#define LCD_RST  40

Arduino_DataBus *bus = new Arduino_ESP32SPI(45, 21, 38, 39);
Arduino_GFX *gfx = new Arduino_ST7789(
  bus, LCD_RST, 0, false,
  172, 320,
  34, 0, 34, 0);

// --- Touch ---
#define Touch_SDA  42
#define Touch_SCL  41
#define Touch_RST  47
#define Touch_INT  48

// --- K30 UART ---
#define RX_PIN 44
#define TX_PIN 43
HardwareSerial K30Serial(1);

// --- WiFi AP ---
const char* AP_SSID = "CO2Monitor";
const char* AP_PASS = "co2monitor123";
WebServer server(80);

// --- Timing ---
#define WARMUP_MS_DEFAULT     10000
#define READ_INTERVAL_DEFAULT 6000
#define CO2_GOOD_DEFAULT      450

unsigned long WARMUP_MS    = WARMUP_MS_DEFAULT;
unsigned long READ_INTERVAL = READ_INTERVAL_DEFAULT;
int           CO2_GOOD      = CO2_GOOD_DEFAULT;
int flagIndex = 0;

#define CMD_TIMEOUT   2000

#define CAL_LOG_FILE "/cal_log.csv"
#define SETTINGS_FILE "/settings.json"
#define CAL_FILE "/lastcal.txt"

// --- History buffer ---
#define HISTORY_SIZE  100000
int*           co2History    = nullptr;
unsigned long* timeHistory   = nullptr;
float*         o2History     = nullptr;
unsigned long* o2TimeHistory = nullptr;
int           historyIndex = 0;
int           historyCount = 0;
int           o2HistoryCount    = 0;
int           o2HistoryIndex    = 0;
unsigned long GRAPH_WINDOW_S = 1800;

// --- K30 commands ---
byte K30_READ[7]   = {0xFE, 0x44, 0x00, 0x08, 0x02, 0x9F, 0x25};
byte K30_UNLOCK[8] = {0xFE, 0x06, 0x00, 0x00, 0x00, 0x00, 0x9D, 0xC5};
byte CAL_400[8]    = {0xFE, 0x06, 0x00, 0x01, 0x7C, 0x06, 0x6C, 0xC7};
byte CAL_0[8]      = {0xFE, 0x06, 0x00, 0x01, 0x7C, 0x07, 0xAD, 0x07};
byte ABC_OFF[8]    = {0xFE, 0x06, 0x00, 0x1F, 0x00, 0x00, 0xAC, 0x03};

// --- Colours ---
#define COL_TITLE      0x07FF
#define COL_UNIT       0x7BEF
#define COL_GOOD       0x07e0
#define COL_WARN       0xffe0
#define COL_BAD        0xf800
#define COL_WHITE      RGB565_WHITE
#define COL_BTN        0x2945
#define COL_BTN_ACTIVE 0x07FF

// --- Web UI Palette translated to RGB565 ---
#define COL_BG       0x10C3  // #15181c (Main dark background)
#define COL_CO2      0x073F  // #00e5ff (Cyan/Neon Blue primary accent)
#define COL_O2       0xFD00  // Keep your O2 color, or use a specific hex converted
#define COL_TEXT     0xFFFF  // #ffffff (White text)
#define COL_MUTED    0x7BEF  // #777777 (Muted gray for units and axes)
#define COL_BORDER   0x2945  // #2a2a2a (Subtle divider lines)
#define COL_DANGER   0xF206  // #f44336 (Red for delete/danger)
#define BLACK   0x0000
#define WHITE   0xFFFF
#define GREEN   0x07E0
#define RED     0xF800
#define BLUE    0x001F

// --- Layout (landscape 320x172) ---
#define SCREEN_W   320
#define SCREEN_H   172
#define TITLE_H      0
#define BAR_H        4
#define MAIN_X       0
#define MAIN_W       SCREEN_W

// --- State ---
bool          calibrating       = false;
int           lastCO2           = 0;
unsigned long lastReadTime      = 0;
unsigned long startTime         = 0;
unsigned long browserTimeOffset = 0;
unsigned long readDuration = 0;

// --- View mode ---
int           graphMode         = 0;  // 0=reading, 1=CO2 graph, 2=O2 graph
unsigned long lastTouchTime     = 0;
#define TOUCH_DEBOUNCE_MS 400

// --- Min/max tracking ---
int           sessionMin        = 32767;
int           sessionMax        = -32768;

// --- Calibrant logs ---
String lastCalType = "";
unsigned long lastCalUnix = 0;

// --- SD Card ---
bool          sdAvailable       = false;
const char*   SD_FILE           = "/co2_log.csv";
bool timeSynced = false; 
bool warningDrawn = false; // Prevents the screen from flickering by drawing the warning only once

int consecutiveFailures = 0;
int prevCO2 = -1;
float prevO2 = -1;

// --- Device name ---
char deviceName[32]  = "CO2 Monitor";
char apSSID[32]      = "CO2Monitor";
char apPass[32]      = "co2monitor123";

void getLogFilename(char* out, size_t len) {
  if (browserTimeOffset == 0) {
    strncpy(out, "/fallback.csv", len);
    return;
  }
  unsigned long now = browserTimeOffset + (millis() - startTime) / 1000;
  getLogFilenameFromUnix(now, out, len);
}

void getLogFilenameFromUnix(unsigned long unix, char* out, size_t len) {
  unsigned long t = unix / 60 / 60 / 24;
  unsigned long days = t;
  int year = 1970;
  while (true) {
    bool leap = (year%4==0 && (year%100!=0 || year%400==0));
    int diy = leap ? 366 : 365;
    if (days < (unsigned long)diy) break;
    days -= diy; year++;
  }
  bool leap = (year%4==0 && (year%100!=0 || year%400==0));
  int monthDays[] = {31,leap?29:28,31,30,31,30,31,31,30,31,30,31};
  int month = 0;
  while (days >= (unsigned long)monthDays[month]) { days -= monthDays[month]; month++; }
  snprintf(out, len, "/%04d-%02d.csv", year, month+1);
}


// ============================================================
//  DISPLAY INIT
// ============================================================

void lcd_reg_init(void) {
  static const uint8_t init_operations[] = {
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x11,
    END_WRITE,
    DELAY, 120,

    BEGIN_WRITE,
    WRITE_C8_D16, 0xDF, 0x98, 0x53,
    WRITE_C8_D8,  0xB2, 0x23,

    WRITE_COMMAND_8, 0xB7,
    WRITE_BYTES, 4,
    0x00, 0x47, 0x00, 0x6F,

    WRITE_COMMAND_8, 0xBB,
    WRITE_BYTES, 6,
    0x1C, 0x1A, 0x55, 0x73, 0x63, 0xF0,

    WRITE_C8_D16, 0xC0, 0x44, 0xA4,
    WRITE_C8_D8,  0xC1, 0x16,

    WRITE_COMMAND_8, 0xC3,
    WRITE_BYTES, 8,
    0x7D, 0x07, 0x14, 0x06, 0xCF, 0x71, 0x72, 0x77,

    WRITE_COMMAND_8, 0xC4,
    WRITE_BYTES, 12,
    0x00, 0x00, 0xA0, 0x79, 0x0B, 0x0A, 0x16, 0x79, 0x0B, 0x0A, 0x16, 0x82,

    WRITE_COMMAND_8, 0xC8,
    WRITE_BYTES, 32,
    0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28, 0x28, 0x26, 0x25, 0x17,
    0x12, 0x0D, 0x04, 0x00, 0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28,
    0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,

    WRITE_COMMAND_8, 0xD0,
    WRITE_BYTES, 5,
    0x04, 0x06, 0x6B, 0x0F, 0x00,

    WRITE_C8_D16, 0xD7, 0x00, 0x30,
    WRITE_C8_D8,  0xE6, 0x14,
    WRITE_C8_D8,  0xDE, 0x01,

    WRITE_COMMAND_8, 0xB7,
    WRITE_BYTES, 5,
    0x03, 0x13, 0xEF, 0x35, 0x35,

    WRITE_COMMAND_8, 0xC1,
    WRITE_BYTES, 3,
    0x14, 0x15, 0xC0,

    WRITE_C8_D16, 0xC2, 0x06, 0x3A,
    WRITE_C8_D16, 0xC4, 0x72, 0x12,
    WRITE_C8_D8,  0xBE, 0x00,
    WRITE_C8_D8,  0xDE, 0x02,

    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 3,
    0x00, 0x02, 0x00,

    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 3,
    0x01, 0x02, 0x00,

    WRITE_C8_D8, 0xDE, 0x00,
    WRITE_C8_D8, 0x35, 0x00,
    WRITE_C8_D8, 0x3A, 0x05,

    WRITE_COMMAND_8, 0x2A,
    WRITE_BYTES, 4,
    0x00, 0x22, 0x00, 0xCD,

    WRITE_COMMAND_8, 0x2B,
    WRITE_BYTES, 4,
    0x00, 0x00, 0x01, 0x3F,

    WRITE_C8_D8, 0xDE, 0x02,

    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 3,
    0x00, 0x02, 0x00,

    WRITE_C8_D8, 0xDE, 0x00,
    WRITE_C8_D8, 0x36, 0x00,
    WRITE_COMMAND_8, 0x21,
    END_WRITE,

    DELAY, 10,

    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x29,
    END_WRITE
  };
  bus->batchOperation(init_operations, sizeof(init_operations));
}

// ============================================================
//  DISPLAY HELPERS
// ============================================================
void drawTrendIcon(int x, int y, int size, const char* trend, uint16_t color) {
  gfx->setTextColor(color);

  if (trend[0] == '^') {
    // ▲ Up triangle
    gfx->fillTriangle(
      x, y,                     // top
      x - size, y + size,      // bottom left
      x + size, y + size,      // bottom right
      color
    );
  }
  else if (trend[0] == 'v') {
    // ▼ Down triangle
    gfx->fillTriangle(
      x, y + size,             // bottom
      x - size, y,             // top left
      x + size, y,             // top right
      color
    );
  }
  else {
    // — Stable (small horizontal line)
    gfx->drawFastHLine(x - size, y + size/2, size * 2, color);
  }
}

void drawButton(int x, int y, int w, int h, const char* l1, const char* l2, const char* l3, uint16_t bg, uint16_t fg) {
  // Background — leave a small margin for rounded look
  gfx->fillRect(x, y, w, h, COL_BG);
  gfx->fillRoundRect(x + 8, y + 6, w - 16, h - 12, 6, bg);
  gfx->drawRoundRect(x + 8, y + 6, w - 16, h - 12, 6, 0x3186);

  gfx->setTextColor(fg);
  int lineH1 = 8;   // size 1
  int lineH2 = 16;  // size 2
  int totalH = lineH1 + lineH2 + lineH1 + 4;
  int startY = y + 6 + ((h - 12) - totalH) / 2;

  gfx->setTextSize(1);
  int tw = strlen(l1) * 6;
  gfx->setCursor(x + (w - tw) / 2, startY);
  gfx->print(l1);

  gfx->setTextSize(2);
  tw = strlen(l2) * 12;
  gfx->setCursor(x + (w - tw) / 2, startY + lineH1 + 2);
  gfx->print(l2);

  gfx->setTextSize(1);
  tw = strlen(l3) * 6;
  gfx->setCursor(x + (w - tw) / 2, startY + lineH1 + lineH2 + 4);
  gfx->print(l3);
}

void drawFlag(int x, int y) {
  int fw = 18;
  int fh = 12;
  
  if (flagIndex == 0) {
    // 🇲🇱 Mali
    gfx->fillRect(x, y, fw/3, fh, 0x25a8);
    gfx->fillRect(x + fw/3, y, fw/3, fh, 0xfe84);
    gfx->fillRect(x + 2*(fw/3), y, fw/3, fh, 0xc8c6);
  } 
  else if (flagIndex == 1) {
    // 🇰🇷 South Korea
    gfx->fillRect(x, y, fw, fh, 0xFFFF);
    gfx->fillCircle(x + fw/2, y + fh/2, 3, 0xF800);
    for(int i = -3; i <= 3; i++) {
      for(int j = 0; j <= 3; j++) {
        if (i*i + j*j <= 9) gfx->drawPixel(x + fw/2 + i, y + fh/2 + j, 0x001F);
      }
    }
    gfx->drawLine(x+2, y+2, x+4, y+4, 0x0000);
    gfx->drawLine(x+fw-3, y+2, x+fw-5, y+4, 0x0000);
    gfx->drawLine(x+2, y+fh-3, x+4, y+fh-5, 0x0000);
    gfx->drawLine(x+fw-3, y+fh-3, x+fw-5, y+fh-5, 0x0000);
  } 
  else if (flagIndex == 2) {
    // 🏴󠁧󠁢󠁥󠁮󠁧󠁿 England
    gfx->fillRect(x, y, fw, fh, 0xFFFF);
    gfx->fillRect(x + fw/2 - 1, y, 2, fh, 0xF800);
    gfx->fillRect(x, y + fh/2 - 1, fw, 2, 0xF800);
  } 
  else if (flagIndex == 3) {
    // 🇧🇷 Itaim Bibi
    uint16_t green = 0x24c6;
    uint16_t white = 0xFFFF;
    uint16_t grey  = 0xC618; 
    uint16_t blue  = 0x001F;

    gfx->fillRect(x, y + fh/2, fw, fh/2, blue);
    gfx->fillCircle(x + fw/2, y + fh/2, fw/4, blue);
    gfx->fillRect(x, y + fh/4, fw, fh/2, white);
    gfx->fillCircle(x + fw/2, y + fh/2, fw/4 - 1, grey);
    gfx->fillRect(x, y + fh/2, fw, fh/4, blue);
    gfx->fillRect(x, y, fw, fh/4, green);
    gfx->fillRect(x + fw/3, y, fw/3, fh/8, green);
  }
}

void drawStaticUI() {
  gfx->fillScreen(COL_BG);
  // Pi connection indicator dot
  gfx->fillCircle(SCREEN_W - 14, 14, 4, piConnected ? COL_GOOD : COL_BAD);

  // Draw title underline last so nothing overwrites it
  gfx->drawFastHLine(0, TITLE_H, SCREEN_W, 0x4208); // (Or COL_BORDER)
}

void displayWarmup(int secondsLeft) {
  gfx->fillRect(MAIN_X, TITLE_H, MAIN_W, SCREEN_H - TITLE_H, COL_BG);
  gfx->drawFastHLine(0, TITLE_H, SCREEN_W, 0x4208);
  gfx->setTextColor(COL_UNIT);
  gfx->setTextSize(2);
  int tw = 13 * 12;
  gfx->setCursor(MAIN_X + (MAIN_W - tw) / 2, 50);
  gfx->print("Warming up...");
  gfx->setTextSize(5);
  gfx->setTextColor(COL_TITLE);
  String s = String(secondsLeft) + "s";
  tw = s.length() * 30;
  gfx->setCursor(MAIN_X + (MAIN_W - tw) / 2, 85);
  gfx->print(s);
}

#define GAP_SMALL 4

void displayReading(int co2) {
  if (!timeSynced) return;
  if (graphMode != 0) return;

  // Clear main area
  gfx->fillRect(0, TITLE_H, SCREEN_W, SCREEN_H - TITLE_H, COL_BG);

  int startX = 24; 
  int gap = 8;     
  int rightMargin = 24; 
  int trendIconW = 12;  

  // ==========================================
  //  PRE-CALCULATE LAYOUT 
  // ==========================================
  
  String numStr = String(co2);
  int co2NumW = numStr.length() * 30; // Size 5 roughly 30px per char
  int ppmW = 3 * 12;                  // Size 2 roughly 12px per char
  int co2EndX = startX + co2NumW + gap + ppmW + gap + 4 + trendIconW;

  int o2EndX = startX;
  char o2numBuf[8];
  if (o2Available) {
    snprintf(o2numBuf, sizeof(o2numBuf), "%.2f", lastO2);
    int o2NumW = strlen(o2numBuf) * 24; // Size 4 roughly 24px per char
    int pctW = 1 * 12; 
    o2EndX = startX + o2NumW + gap + pctW + gap + 4 + trendIconW;
  }

  // --- DYNAMIC WIDTH FOR GRAPHS ---
  int maxEndX = (co2EndX > o2EndX) ? co2EndX : o2EndX;
  int sparkX = maxEndX + 16; 
  int sparkW = SCREEN_W - rightMargin - sparkX; 
  if (sparkW < 20) sparkW = 20; 
  int sparkH = 28; 

  // --- FIXED POSITION FOR SYSTEM INFO ---
  int sysInfoX = 186; 

  // ==========================================
  //  DRAWING
  // ==========================================

  int contentTop = TITLE_H;

  // --- NEW PRECISION LAYOUT MATH ---
  int labelToValGap = 24; // Consistent gap for BOTH sections
  int co2ValH = 40;       // Height of size 5 text & sparkline
  int o2ValH  = 32;       // Height of size 4 text & sparkline
  
  int co2BlockH = labelToValGap + co2ValH; 
  int o2BlockH  = labelToValGap + o2ValH;  
  int blockSpacing = 30;  // Total gap between bottom of CO2 and top of O2

  int totalH = co2BlockH + blockSpacing + o2BlockH;

  // Center the whole group vertically
  int startY = TITLE_H + ((SCREEN_H - TITLE_H - totalH) / 2);

  // Define exact Y coordinates
  int co2Top    = startY;
  int valY      = co2Top + labelToValGap;
  int co2Bottom = co2Top + co2BlockH;
  
  int midY   = co2Bottom + (blockSpacing / 2); // Perfectly centered divider
  int o2Top  = co2Bottom + blockSpacing;
  int o2ValY = o2Top + 16;

  // --- SYSTEM INFO (Top Right) ---
  gfx->setTextSize(1);
  gfx->setTextColor(COL_TITLE);
  gfx->setCursor(sysInfoX, contentTop + 8); 
  gfx->print(deviceName);
  
  gfx->setTextColor(COL_MUTED);
  gfx->setCursor(sysInfoX, contentTop + 20); 
  gfx->print("192.168.4.1");
  
  drawFlag(SCREEN_W - rightMargin - 18, contentTop + 8);

  // --- CO2 SECTION ---
  gfx->setTextSize(1.5);
  gfx->setTextColor(COL_MUTED); 
  gfx->setCursor(startX, co2Top);
  gfx->print("CARBON DIOXIDE");

  gfx->setTextSize(5); 
  gfx->setTextColor(COL_CO2);
  gfx->setCursor(startX, valY);
  gfx->print(numStr);

  gfx->setTextSize(2); 
  gfx->setTextColor(COL_MUTED);
  int unitX = startX + co2NumW + gap;
  int unitY = valY + (40 - 16); 
  gfx->setCursor(unitX, unitY);
  gfx->print("ppm");

  int trendX = unitX + ppmW + gap + 4;
  int trendY = unitY + 4; 

  const char* trend = "-";
  if (prevCO2 >= 0) {
    if (co2 > prevCO2 + 5) trend = "^";
    else if (co2 < prevCO2 - 5) trend = "v";
  }
  drawTrendIcon(trendX, trendY, 5, trend, COL_CO2);

  int co2SparkY = valY + 40 - sparkH; 
  drawCO2Sparkline(sparkX, co2SparkY, sparkW, sparkH);

  // Floating horizontal divider line (Now perfectly centered)
  gfx->drawFastHLine(startX, midY+2, SCREEN_W - (startX * 2), COL_BORDER);

  // --- O2 SECTION ---
  gfx->setTextSize(1);
  gfx->setTextColor(COL_MUTED);
  gfx->setCursor(startX, o2Top);
  gfx->print("OXYGEN");

  if (o2Available) {
    gfx->setTextSize(4); 
    gfx->setTextColor(COL_O2);
    gfx->setCursor(startX, o2ValY);
    gfx->print(o2numBuf);

    int o2NumW = strlen(o2numBuf) * 24;
    gfx->setTextSize(2);
    gfx->setTextColor(COL_MUTED);
    int o2unitX = startX + o2NumW + gap;
    int o2unitY = o2ValY + (32 - 16); 
    gfx->setCursor(o2unitX, o2unitY);
    gfx->print("%");

    int pctW = 1 * 12; 
    int o2trendX = o2unitX + pctW + gap + 4;
    int o2trendY = o2unitY + 4;

    const char* o2Trend = "-";
    if (prevO2 >= 0) {
      if (lastO2 > prevO2 + 0.05) o2Trend = "^";
      else if (lastO2 < prevO2 - 0.05) o2Trend = "v";
    }
    drawTrendIcon(o2trendX, o2trendY, 5, o2Trend, COL_O2);

    int o2SparkY = o2ValY + 32 - sparkH; 
    drawO2Sparkline(sparkX, o2SparkY, sparkW, sparkH);

  } else {
    gfx->setTextSize(4);
    gfx->setTextColor(COL_BORDER); 
    gfx->setCursor(startX, o2ValY); // Uses the new consistent gap variable
    gfx->print("--.--");
  }

  prevCO2 = co2;
  prevO2 = lastO2;
}

// Progress bar — call each loop tick, shows time to next reading
void drawProgressBar() {
  if (graphMode != 0) return;
  int barY = SCREEN_H - BAR_H;
  unsigned long elapsed = millis() - lastReadTime;
  int filled = 0;

  // Make sure we have a valid active wait time to avoid division by zero
  if (READ_INTERVAL > readDuration) {
    unsigned long activeWait = READ_INTERVAL - readDuration;
    
    // Only start filling the bar AFTER the reading process has finished
    if (elapsed > readDuration) {
      unsigned long visualElapsed = elapsed - readDuration;
      filled = (int)(visualElapsed * (long)SCREEN_W / activeWait);
    }
  }

  // Constrain bounds just to be safe
  if (filled > SCREEN_W) filled = SCREEN_W;
  if (filled < 0) filled = 0;
  
  // Fill (Cyan accent)
  if (filled > 0) {
    gfx->fillRect(0, barY, filled, BAR_H, COL_CO2); 
  }

  // Track (muted background)
  if (SCREEN_W > filled) {
    gfx->fillRect(filled, barY, SCREEN_W - filled, BAR_H, 0x2104); // Very dark gray
  }
}

void drawCO2Sparkline(int x, int y, int w, int h) {
  if (historyCount < 2) return;

  // =========================
  // 1. Define time window
  // =========================
  int lastIndex = (historyIndex - 1 + HISTORY_SIZE) % HISTORY_SIZE;
  unsigned long tLast = timeHistory[lastIndex];
  unsigned long windowStart = (tLast > GRAPH_WINDOW_S)
    ? tLast - GRAPH_WINDOW_S
    : 0;

  // =========================
  // 2. First pass: count valid points, min/max, and find first time
  // =========================
  const int SPARK_MAX = 120;
  int step = max(1, historyCount / SPARK_MAX);
  
  int minVal = 999999;
  int maxVal = -999999;
  int validCount = 0;
  unsigned long tFirstValid = tLast; // Track the exact start time of the visible graph

  for (int i = 0; i < historyCount; i++) {
    int idx = (historyIndex - historyCount + i + HISTORY_SIZE) % HISTORY_SIZE;

    if (timeHistory[idx] < windowStart) continue;

    // Capture the time of the very first point that falls inside our window
    if (validCount == 0) tFirstValid = timeHistory[idx];

    int v = co2History[idx];

    if (v < minVal) minVal = v;
    if (v > maxVal) maxVal = v;

    validCount++;
  }

  if (validCount < 2) return;

  if (maxVal - minVal < 10) {
    minVal -= 5;
    maxVal += 5;
  }

  int range = maxVal - minVal;
  if (range == 0) range = 1;

  // =========================
  // 3. Second pass: draw ONLY windowed points based on TIME
  // =========================
  int prevPx = -1, prevPy = -1;
  unsigned long tRange = tLast - tFirstValid;
  if (tRange == 0) tRange = 1; // Prevent divide by zero

  for (int i = 0; i < historyCount; i += step) {
    int idx = (historyIndex - historyCount + i + HISTORY_SIZE) % HISTORY_SIZE;

    if (timeHistory[idx] < windowStart) continue;

    int val = co2History[idx];

    // Calculate X position based on where this point's timestamp falls in the time range
    int px = x + ((timeHistory[idx] - tFirstValid) * w) / tRange;
    
    int py = y + h - ((val - minVal) * h) / range;
    py = constrain(py, y, y + h);

    if (prevPx != -1) {
      gfx->drawLine(prevPx, prevPy, px, py, COL_CO2);
      gfx->drawLine(prevPx, prevPy + 1, px, py + 1, COL_CO2);
    }

    prevPx = px;
    prevPy = py;
  }
}

void drawO2Sparkline(int x, int y, int w, int h) {
  if (o2HistoryCount < 2) return;

  // =========================
  // 1. Define time window
  // =========================
  int lastIndex = (o2HistoryIndex - 1 + HISTORY_SIZE) % HISTORY_SIZE;
  unsigned long tLast = o2TimeHistory[lastIndex];
  unsigned long windowStart = (tLast > GRAPH_WINDOW_S)
    ? tLast - GRAPH_WINDOW_S
    : 0;

  // =========================
  // 2. First pass: count valid points, min/max, and find first time
  // =========================
  const int SPARK_MAX = 120;
  int step = max(1, o2HistoryCount / SPARK_MAX);

  float minVal = 999999.0;
  float maxVal = -999999.0;
  int validCount = 0;
  unsigned long tFirstValid = tLast; // Track the exact start time of the visible graph

  for (int i = 0; i < o2HistoryCount; i++) {
    int idx = (o2HistoryIndex - o2HistoryCount + i + HISTORY_SIZE) % HISTORY_SIZE;

    if (o2TimeHistory[idx] < windowStart) continue;

    // Capture the time of the very first point that falls inside our window
    if (validCount == 0) tFirstValid = o2TimeHistory[idx];

    float v = o2History[idx];

    if (v < minVal) minVal = v;
    if (v > maxVal) maxVal = v;

    validCount++;
  }

  if (validCount < 2) return;

  // Keep the specific O2 scaling rules
  if (maxVal - minVal < 0.5) {
    minVal -= 0.25;
    maxVal += 0.25;
  }

  float range = maxVal - minVal;
  if (range == 0.0) range = 1.0;

  // =========================
  // 3. Second pass: draw ONLY windowed points based on TIME
  // =========================
  int prevPx = -1, prevPy = -1;
  unsigned long tRange = tLast - tFirstValid;
  if (tRange == 0) tRange = 1; // Prevent divide by zero

  for (int i = 0; i < o2HistoryCount; i += step) {
    int idx = (o2HistoryIndex - o2HistoryCount + i + HISTORY_SIZE) % HISTORY_SIZE;

    if (o2TimeHistory[idx] < windowStart) continue;

    float val = o2History[idx];

    // Calculate X position based on where this point's timestamp falls in the time range
    int px = x + ((o2TimeHistory[idx] - tFirstValid) * w) / tRange;

    // Ensure we multiply by (float)h so the float division isn't truncated
    int py = y + h - ((val - minVal) * (float)h) / range;
    py = constrain(py, y, y + h); // Keep it safely inside the drawing box

    if (prevPx != -1) {
      gfx->drawLine(prevPx, prevPy, px, py, COL_O2);
      gfx->drawLine(prevPx, prevPy + 1, px, py + 1, COL_O2);
    }

    prevPx = px;
    prevPy = py;
  }
}

void drawGraphView() {
  gfx->fillScreen(COL_BG);

  if (historyCount < 2) {
    gfx->setTextColor(COL_CO2);
    gfx->setTextSize(1);
    gfx->setCursor(40, 80);
    gfx->print("Not enough data yet");
    return;
  }

  const int GX = 38;
  const int GY = 20;
  const int GW = SCREEN_W - GX - 14;  // 14px right margin prevents label clipping
  const int GH = SCREEN_H - GY - 20;

  int n = min(historyCount, HISTORY_SIZE);
  int startIdx = (historyCount < HISTORY_SIZE) ? 0 : historyIndex;

  unsigned long tLast  = timeHistory[(startIdx + n - 1) % HISTORY_SIZE];
  // trim to graph window
  unsigned long windowStart = (tLast > GRAPH_WINDOW_S) ? tLast - GRAPH_WINDOW_S : 0;
  while (n > 1 && timeHistory[startIdx % HISTORY_SIZE] < windowStart) {
    startIdx = (startIdx + 1) % HISTORY_SIZE;
    n--;
  }

  unsigned long tFirst  = timeHistory[startIdx % HISTORY_SIZE];
  unsigned long tRange2 = (tLast > tFirst) ? (tLast - tFirst) : 1;

  int vMin = 32767, vMax = -32768;
  for (int i = 0; i < n; i++) {
    int v = co2History[(startIdx + i) % HISTORY_SIZE];
    if (v < vMin) vMin = v;
    if (v > vMax) vMax = v;
  }
  vMin = max(0, vMin - 50);
  vMax = vMax + 50;
  int vRange = (vMax > vMin) ? (vMax - vMin) : 1;

  // Y axis line
  gfx->drawFastVLine(GX, GY, GH, COL_UNIT);

  // Y axis ticks and labels — size 1 to match O2 graph
  gfx->setTextSize(1);
  gfx->setTextColor(COL_UNIT);
  int tickStep = max(50, ((vRange / 3) / 50) * 50);
  int firstTick = ((vMin / tickStep) + 1) * tickStep;
  for (int v = firstTick; v <= vMax; v += tickStep) {
    int y = GY + GH - (int)((long)(v - vMin) * GH / vRange);
    // grid line
    gfx->drawFastHLine(GX, y, GW, 0x18C3);
    // label — right aligned to GX with small gap
    char lbl[8]; snprintf(lbl, sizeof(lbl), "%d", v);
    int lw = strlen(lbl) * 6;
    gfx->setCursor(GX - lw - 2, y - 3);
    gfx->print(lbl);
  }

  // X axis labels
  gfx->setTextColor(COL_UNIT);
  for (int i = 0; i <= 4; i++) {
    unsigned long t = tFirst + (tRange2 * i / 4);
    int px = GX + (int)((t - tFirst) * (long)GW / tRange2);
    int secs = (int)(t - tFirst);
    char tbuf[8];
    if (secs < 60) snprintf(tbuf, sizeof(tbuf), "%ds", secs);
    else           snprintf(tbuf, sizeof(tbuf), "%dm", secs / 60);
    int lw = strlen(tbuf) * 6;
    int cx = px - lw / 2;
    if (cx < GX) cx = GX;
    if (cx + lw > GX + GW) cx = GX + GW - lw;  // clamp to plot right edge
    gfx->setCursor(cx, GY + GH + 4);
    gfx->print(tbuf);
  }

  // Plot
  int prevX = -1, prevY = -1;
  for (int i = 0; i < n; i++) {
    int idx = (startIdx + i) % HISTORY_SIZE;
    int v   = co2History[idx];
    unsigned long t = timeHistory[idx];
    int px = GX + (int)((t - tFirst) * (long)GW / tRange2);
    int py = GY + GH - (int)((long)(v - vMin) * GH / vRange);
    py = constrain(py, GY, GY + GH);
    uint16_t lc = COL_CO2;
    if (prevX >= 0) gfx->drawLine(prevX, prevY, px, py, lc);
    prevX = px; prevY = py;
  }

  // Current value top right
  gfx->setTextSize(2);
  gfx->setTextColor(COL_CO2);
  char cur[12];
  snprintf(cur, sizeof(cur), "%d ppm", lastCO2);
  int curW = strlen(cur) * 12;
  gfx->setCursor(SCREEN_W - curW - 4, 4);
  gfx->print(cur);

  gfx->setTextSize(1);
  gfx->setTextColor(0x3186);
  gfx->setCursor(2, 4);
  gfx->print("tap: O2 >");
}

void displayStatus(const char* msg, uint16_t colour) {
  if (graphMode != 0) return;
  gfx->fillRect(MAIN_X, TITLE_H, MAIN_W, SCREEN_H - TITLE_H, COL_BG);
  gfx->drawFastHLine(0, TITLE_H, SCREEN_W, 0x4208);
  gfx->setTextColor(colour);
  gfx->setTextSize(2);
  int textW = strlen(msg) * 12;
  if (textW > MAIN_W - 4) {
    gfx->setTextSize(1);
    textW = strlen(msg) * 6;
  }
  int x = MAIN_X + max(0, (MAIN_W - textW) / 2);
  gfx->setCursor(x, TITLE_H + (SCREEN_H - TITLE_H) / 2 - 8);
  gfx->print(msg);
}

void drawO2GraphView() {
  gfx->fillScreen(COL_BG);

  if (o2HistoryCount < 2) {
    gfx->setTextColor(COL_UNIT);
    gfx->setTextSize(1);
    gfx->setCursor(40, 80);
    gfx->print("Not enough data yet");
    return;
  }

  const int GX = 38;
  const int GY = 20;
  const int GW = SCREEN_W - GX - 14;
  const int GH = SCREEN_H - GY - 20;

  int n = min(o2HistoryCount, HISTORY_SIZE);
  int startIdx = (o2HistoryCount < HISTORY_SIZE) ? 0 : o2HistoryIndex;

  unsigned long tLast  = o2TimeHistory[(startIdx + n - 1) % HISTORY_SIZE];
  unsigned long windowStart = (tLast > GRAPH_WINDOW_S) ? tLast - GRAPH_WINDOW_S : 0;
  while (n > 1 && o2TimeHistory[startIdx % HISTORY_SIZE] < windowStart) {
    startIdx = (startIdx + 1) % HISTORY_SIZE;
    n--;
  }

  unsigned long tFirst = o2TimeHistory[startIdx % HISTORY_SIZE];
  unsigned long tRange = (tLast > tFirst) ? (tLast - tFirst) : 1;

  float vMin = 100.0, vMax = -100.0;
  for (int i = 0; i < n; i++) {
    float v = o2History[(startIdx + i) % HISTORY_SIZE];
    if (v < vMin) vMin = v;
    if (v > vMax) vMax = v;
  }
  vMin = max(0.0f, vMin - 0.5f);  // 0.5% padding
  vMax = vMax + 0.5f;
  float vRange = (vMax > vMin) ? (vMax - vMin) : 1.0f;

  // Y axis line
  gfx->drawFastVLine(GX, GY, GH, COL_UNIT);

  // Y axis ticks and labels
  gfx->setTextSize(1);
  gfx->setTextColor(COL_UNIT);
  // Pick a sensible tick step based on range
  float tickStep = 0.5f;
  if (vRange <= 1.0f)  tickStep = 0.2f;
  if (vRange <= 0.4f)  tickStep = 0.1f;
  if (vRange >= 4.0f)  tickStep = 1.0f;
  float firstTick = ceil(vMin / tickStep) * tickStep;
  for (float v = firstTick; v <= vMax; v += tickStep) {
    int y = GY + GH - (int)((v - vMin) / vRange * GH);
    gfx->drawFastHLine(GX, y, GW, 0x18C3);
    char lbl[8]; snprintf(lbl, sizeof(lbl), "%.1f%%", v);
    int lw = strlen(lbl) * 6;
    gfx->setCursor(GX - lw - 2, y - 3);
    gfx->print(lbl);
  }

  // X axis labels
  gfx->setTextColor(COL_UNIT);
  for (int i = 0; i <= 4; i++) {
    unsigned long t = tFirst + (tRange * i / 4);
    int px = GX + (int)((t - tFirst) * (long)GW / tRange);
    int secs = (int)(t - tFirst);
    char tbuf[8];
    if (secs < 60) snprintf(tbuf, sizeof(tbuf), "%ds", secs);
    else           snprintf(tbuf, sizeof(tbuf), "%dm", secs / 60);
    int lw = strlen(tbuf) * 6;
    int cx = px - lw / 2;
    if (cx < GX) cx = GX;
    if (cx + lw > GX + GW) cx = GX + GW - lw;
    gfx->setCursor(cx, GY + GH + 4);
    gfx->print(tbuf);
  }

  // 20.9% reference line — only draw if within visible range
  if (20.9f > vMin && 20.9f < vMax) {
    int refY = GY + GH - (int)((20.9f - vMin) / vRange * GH);
    for (int x = GX; x < GX + GW; x += 6)
      gfx->drawPixel(x, refY, 0x3186);
  }

  // Plot
  int prevX = -1, prevY = -1;
  for (int i = 0; i < n; i++) {
    int idx = (startIdx + i) % HISTORY_SIZE;
    float v = o2History[idx];
    unsigned long t = o2TimeHistory[idx];
    int px = GX + (int)((t - tFirst) * (long)GW / tRange);
    float clamped = constrain(v, vMin, vMax);
    int py = GY + GH - (int)((clamped - vMin) / vRange * GH);
    uint16_t lc = COL_O2;
    if (prevX >= 0) gfx->drawLine(prevX, prevY, px, py, lc);
    prevX = px; prevY = py;
  }

  // Current value top right
  gfx->setTextSize(2);
  gfx->setTextColor(COL_O2);
  char cur[10];
  snprintf(cur, sizeof(cur), "%.2f%%", lastO2);
  int curW = strlen(cur) * 12;
  gfx->setCursor(SCREEN_W - curW - 4, 4);
  gfx->print(cur);

  gfx->setTextSize(1);
  gfx->setTextColor(0x3186);
  gfx->setCursor(2, 4);
  gfx->print("tap: back >");
}

// ============================================================
//  SD CARD
// ============================================================
void drawSyncWarning() {
  int rightMargin = 24; 
  if (warningDrawn) return;

  gfx->fillScreen(COL_BG);

  // --- Custom title bar for this screen ---
  const int SW_TITLE_H = 28;
  gfx->fillRect(0, 0, SCREEN_W, SW_TITLE_H, 0x2800);
  gfx->setTextColor(COL_TITLE);
  gfx->setTextSize(2);
  gfx->setCursor(24, 7);
  gfx->setTextColor(0xF800);
  gfx->print("TIME SYNC REQUIRED");
  drawFlag(SCREEN_W - rightMargin - 18, (SW_TITLE_H - 12) / 2);
  gfx->drawFastHLine(0, SW_TITLE_H, SCREEN_W, 0x4208);

  // --- Instruction Card (Shifted Up) ---
  int cardX = 8;
  int cardY = SW_TITLE_H + 6; // Moved up closer to the title bar
  int cardW = SCREEN_W - 16;
  int cardH = SCREEN_H - cardY - 30; // Leave exact room at bottom for banner
  gfx->drawRoundRect(cardX, cardY, cardW, cardH, 4, 0x2945);

  int x = cardX + 10;
  int y = cardY + 7;

  // Step 1
  gfx->setTextSize(1);
  gfx->setTextColor(COL_MUTED);
  gfx->setCursor(x, y);
  gfx->print("1. Connect to WiFi");
  y += 12;

  gfx->setTextSize(2);
  gfx->setTextColor(COL_WHITE);
  gfx->setCursor(x, y);
  gfx->print(apSSID);
  y += 18;

  gfx->setTextSize(1);
  gfx->setTextColor(COL_MUTED);
  gfx->setCursor(x, y);
  gfx->print("Password: ");
  gfx->setTextColor(COL_WHITE);
  gfx->print(apPass);
  y += 15;

  // Divider
  gfx->drawFastHLine(x, y, cardW - 20, 0x2945);
  y += 7;

  // Step 2
  gfx->setTextSize(1);
  gfx->setTextColor(COL_MUTED);
  gfx->setCursor(x, y);
  gfx->print("2. Open browser and go to address:");
  y += 12;

  gfx->setTextSize(2);
  gfx->setTextColor(COL_CO2);
  gfx->setCursor(x, y);
  gfx->print("192.168.4.1");
  y += 20;

  gfx->setTextSize(1);
  gfx->setTextColor(COL_MUTED);
  gfx->setCursor(x, y);
  gfx->print("Timesync and backdating happens automatically");

  // --- Warning banner (Anchored to Bottom) ---
  int bannerH = 25;
  int bannerY = SCREEN_H - bannerH;
  gfx->fillRect(0, bannerY, SCREEN_W, bannerH, 0x0127);
  gfx->drawFastHLine(0, bannerY, SCREEN_W, 0x4208);
  gfx->setTextSize(1);
  gfx->setTextColor(COL_TITLE);
  int warnW = 39 * 6; // 39 characters * 6px per char
  gfx->setCursor((SCREEN_W - warnW) / 2, bannerY + 6);
  gfx->print("Logs temporarily saving to fallback.csv");

  warningDrawn = true;
}

void initSD() {
  SD_MMC.setPins(16, 15, 17, 18, 13, 14);  // CLK, CMD, D0, D1, D2, D3
  if (!SD_MMC.begin("/sdcard", false)) {
    Serial.println("SD mount failed");
    sdAvailable = false;
    return;
  }
  if (SD_MMC.cardType() == CARD_NONE) {
    Serial.println("No SD card");
    sdAvailable = false;
    return;
  }
  sdAvailable = true;
  Serial.printf("SD mounted, %.1f MB free\n",
    (float)(SD_MMC.totalBytes() - SD_MMC.usedBytes()) / (1024*1024));

  Serial.println("SD: ready, header written on first read");
}

void loadSettings() {
  if (!sdAvailable) return;
  if (!SD_MMC.exists(SETTINGS_FILE)) return;
  File f = SD_MMC.open(SETTINGS_FILE, FILE_READ);
  if (!f) return;
  String json = f.readString();
  Serial.println("=== loading settings.json ===");
  Serial.println(json);
  Serial.println("=============================");
  Serial.printf("deviceName=%s apSSID=%s apPass=%s\n", deviceName, apSSID, apPass);
  f.close();

  // Simple key extraction without a JSON library
  auto extractLong = [&](const char* key, unsigned long def) -> unsigned long {
    String k = "\"" + String(key) + "\":";
    int idx = json.indexOf(k);
    if (idx < 0) return def;
    return json.substring(idx + k.length()).toInt();
  };

  auto extractStr = [&](const char* key, char* out, size_t len) {
    String k = "\"" + String(key) + "\":";
    int idx = json.indexOf(k);
    if (idx < 0) return;
    int start = idx + k.length();
    // skip any whitespace including space after colon
    while (start < (int)json.length() && json[start] == ' ') start++;
    // expect opening quote
    if (json[start] != '"') return;
    start++;  // skip opening quote
    int end = json.indexOf("\"", start);
    if (end < 0) return;
    json.substring(start, end).toCharArray(out, len);
  };

  extractStr("device_name", deviceName, sizeof(deviceName));
  extractStr("ap_ssid",     apSSID,     sizeof(apSSID));
  extractStr("ap_pass",     apPass,     sizeof(apPass));
  extractStr("sensor_id",   sensorID,   sizeof(sensorID));
  extractStr("location",    location,   sizeof(location));

  Serial.printf("After extract: deviceName=%s apSSID=%s apPass=%s\n", deviceName, apSSID, apPass);

  READ_INTERVAL = extractLong("read_interval", READ_INTERVAL_DEFAULT);
  WARMUP_MS     = extractLong("warmup_ms",     WARMUP_MS_DEFAULT);
  CO2_GOOD      = extractLong("co2_good",      CO2_GOOD_DEFAULT);
  GRAPH_WINDOW_S = extractLong("graph_window", 1800);
  flagIndex = extractLong("flag_index", 0);

  Serial.printf("Settings loaded: read_interval=%lu warmup=%lu co2_good=%d\n",
                READ_INTERVAL, WARMUP_MS, CO2_GOOD);
}

void saveSettings() {
  if (!sdAvailable) return;
  File f = SD_MMC.open(SETTINGS_FILE, FILE_WRITE);
  if (!f) {
    Serial.println("Failed to open settings.json for writing");
    return;
  }
  
  f.printf("{\n"
           "  \"read_interval\": %lu,\n"
           "  \"warmup_ms\": %lu,\n"
           "  \"co2_good\": %d,\n"
           "  \"device_name\": \"%s\",\n"
           "  \"ap_ssid\": \"%s\",\n"
           "  \"ap_pass\": \"%s\",\n"
           "  \"graph_window\": %lu,\n"
           "  \"flag_index\": %d,\n"
           "  \"sensor_id\": \"%s\",\n"
           "  \"location\": \"%s\"\n"
           "}",
           READ_INTERVAL, WARMUP_MS, CO2_GOOD,
           deviceName, apSSID, apPass,
           GRAPH_WINDOW_S, flagIndex,
           sensorID, location);
           
  f.close();
  Serial.println("Settings successfully saved to SD card!");
}

void saveCalRecord(const char* calType) {
  if (!sdAvailable) return;
  unsigned long unix = browserTimeOffset + (millis() - startTime) / 1000;

  // Update in-memory state
  lastCalType = String(calType);
  lastCalUnix = unix;

  // Save last cal for quick reload on boot
  File f = SD_MMC.open(CAL_FILE, FILE_WRITE);
  if (f) {
    f.printf("%s,%lu\n", calType, unix);
    f.close();
  }

  // Append to cal log
  bool isNew = !SD_MMC.exists(CAL_LOG_FILE);
  File log = SD_MMC.open(CAL_LOG_FILE, FILE_APPEND);
  if (!log) return;
  if (isNew) log.println("datetime,unix,cal_type,co2_reading_ppm");

  // Format datetime
  unsigned long t = unix;
  int s  = t % 60; t /= 60;
  int m  = t % 60; t /= 60;
  int h  = t % 24; t /= 24;
  unsigned long days = t;
  int year = 1970;
  while (true) {
    bool leap = (year%4==0 && (year%100!=0 || year%400==0));
    int diy = leap ? 366 : 365;
    if (days < (unsigned long)diy) break;
    days -= diy; year++;
  }
  bool leap = (year%4==0 && (year%100!=0 || year%400==0));
  int monthDays[] = {31,leap?29:28,31,30,31,30,31,31,30,31,30,31};
  int month = 0;
  while (days >= (unsigned long)monthDays[month]) { days -= monthDays[month]; month++; }
  char buf[24];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
           year, month+1, (int)days+1, h, m, s);

  log.printf("%s,%lu,%s,%d\n", buf, unix, calType, lastCO2);
  log.close();
  Serial.printf("Cal logged: %s at %s, CO2 was %d ppm\n", calType, buf, lastCO2);
}

void loadCalRecord() {
  if (!sdAvailable) return;
  if (!SD_MMC.exists(CAL_FILE)) return;
  File f = SD_MMC.open(CAL_FILE, FILE_READ);
  if (!f) return;
  String line = f.readStringUntil('\n');
  f.close();
  line.trim();
  int comma = line.indexOf(',');
  if (comma < 0) return;
  lastCalType = line.substring(0, comma);
  lastCalUnix = line.substring(comma + 1).toInt();
  Serial.printf("Cal loaded: %s at %lu\n", lastCalType.c_str(), lastCalUnix);
}

#define TIME_FILE "/lasttime.txt"

void saveTimeToSD(unsigned long unixTime) {
  if (!sdAvailable) return;
  File f = SD_MMC.open(TIME_FILE, FILE_WRITE);
  if (!f) return;
  f.printf("%lu\n", unixTime);
  f.close();
}

void loadTimeFromSD() {
  if (!sdAvailable) return;
  File f = SD_MMC.open(TIME_FILE, FILE_READ);
  if (!f) return;
  String line = f.readStringUntil('\n');
  f.close();
  unsigned long savedUnix = line.toInt();
  if (savedUnix < 1700000000UL) return; // sanity check — reject obviously wrong values
  // Offset: saved time is approximately "now" at boot
  // It will be slightly behind but correct to within the powered-off duration
  unsigned long uptimeSecs = (millis() - startTime) / 1000;
  browserTimeOffset = savedUnix - uptimeSecs;
  Serial.printf("Loaded time from SD: %lu\n", savedUnix);
}

uint64_t getSdUsedBytes() {
  if (!sdAvailable) return 0;
  uint64_t used = 0;
  File root = SD_MMC.open("/");
  if (!root) return 0;

  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      used += file.size();  // sum all files
    }
    file = root.openNextFile();
  }
  return used;
}


int countCsvReadings(const char* fname) {
  if(!sdAvailable) return 0;
  File f = SD_MMC.open(fname, FILE_READ);
  if(!f) return 0;
  int count = 0;
  f.seek(0); // ensure start
  while(f.available()){
    String line = f.readStringUntil('\n');
    if(line.length() > 1) count++;
  }
  f.close();
  return max(0, count-1); // subtract header
}

String getSDInfo() {
  if (!sdAvailable) return "{}";

  uint64_t total = SD_MMC.totalBytes();
  uint64_t used  = getSdUsedBytes();
  float pct = (float)used / total * 100.0;

  String json = "{";
  json += "\"used\":" + String(used);
  json += ",";
  json += "\"total\":" + String(total);
  json += ",";
  json += "\"percent\":" + String(pct,3);
  json += "}";

  return json;
}

// Cache for file list to avoid re-counting large files every call
struct FileCache {
  String name;
  size_t size;
  int readings;
};
static FileCache fileCache[10];
static int fileCacheCount = 0;

int getCachedReadingCount(const String& fullName, size_t currentSize) {
  for (int i = 0; i < fileCacheCount; i++) {
    if (fileCache[i].name == fullName && fileCache[i].size == currentSize) {
      Serial.printf("Cache HIT: %s (%d readings)\n", fullName.c_str(), fileCache[i].readings);
      return fileCache[i].readings;
    }
  }
  Serial.printf("Cache MISS: %s — counting lines...\n", fullName.c_str());
  int count = countCsvReadings(fullName.c_str());
  Serial.printf("Cache MISS done: %d readings\n", count);
  // Update cache
  bool found = false;
  for (int i = 0; i < fileCacheCount; i++) {
    if (fileCache[i].name == fullName) {
      fileCache[i].size = currentSize;
      fileCache[i].readings = count;
      found = true;
      break;
    }
  }
  if (!found && fileCacheCount < 10) {
    fileCache[fileCacheCount++] = {fullName, currentSize, count};
  }
  return count;
}

String getLogFileListJSON() {
  if (!sdAvailable) return "[]";
  String json = "[";
  File root = SD_MMC.open("/");
  if (!root) return "[]";
  File file = root.openNextFile();
  bool first = true;
  while (file) {
    String name = file.name();
    if (!file.isDirectory() && name.endsWith(".csv")
        && !name.endsWith("lastcal.csv")) {
      if (!first) json += ",";
      first = false;
      String fullName = name.startsWith("/") ? name : "/" + name;
      json += "{\"name\":\"" + fullName + "\"}";
    }
    file = root.openNextFile();
  }
  json += "]";
  return json;
}

File logFileHandle;
char logFileHandleName[32] = "";

void sdWriteReading(int co2, float o2, unsigned long uptimeSecs) {
  if (!sdAvailable) return;
  char filename[32];
  getLogFilename(filename, sizeof(filename));

  // If filename changed (e.g. month rollover) close old handle
  if (strncmp(logFileHandleName, filename, sizeof(filename)) != 0) {
    if (logFileHandle) logFileHandle.close();
    logFileHandleName[0] = '\0';
  }

  // Open if not already open
  if (!logFileHandle) {
    bool isNew = !SD_MMC.exists(filename);
    logFileHandle = SD_MMC.open(filename, FILE_APPEND);
    if (!logFileHandle) return;
    strncpy(logFileHandleName, filename, sizeof(logFileHandleName));
    if (isNew) {
      logFileHandle.println("datetime,uptime_s,co2_ppm,o2_pct");
      if (strncmp(filename, "/fallback.csv", 13) == 0) {
        logFileHandle.printf("#boot_unix=%lu\n", browserTimeOffset);
      }
    }
  }

  if (browserTimeOffset > 0) {
    unsigned long unix = browserTimeOffset + uptimeSecs;
    unsigned long t = unix;
    int s  = t % 60; t /= 60;
    int m  = t % 60; t /= 60;
    int h  = t % 24; t /= 24;
    unsigned long days = t;
    int year = 1970;
    while (true) {
      bool leap = (year%4==0 && (year%100!=0 || year%400==0));
      int diy = leap ? 366 : 365;
      if (days < (unsigned long)diy) break;
      days -= diy; year++;
    }
    bool leap = (year%4==0 && (year%100!=0 || year%400==0));
    int monthDays[] = {31,leap?29:28,31,30,31,30,31,31,30,31,30,31};
    int month = 0;
    while (days >= (unsigned long)monthDays[month]) { days -= monthDays[month]; month++; }
    char buf[24];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             year, month+1, (int)days+1, h, m, s);
    logFileHandle.printf("%s,%lu,%d,%.2f\n", buf, uptimeSecs, co2, o2);
  } else {
    logFileHandle.printf(",%lu,%d,%.2f\n", uptimeSecs, co2, o2);
  }
  logFileHandle.flush();  // ensure data written without closing
}

void updateFallbackBootUnix() {
  if (!sdAvailable) return;
  if (!SD_MMC.exists("/fallback.csv")) return;

  File src = SD_MMC.open("/fallback.csv", FILE_READ);
  if (!src) return;
  File tmp = SD_MMC.open("/fb_hdr_tmp.csv", FILE_WRITE);
  if (!tmp) { src.close(); return; }

  bool patchedHeader = false;
  while (src.available()) {
    String line = src.readStringUntil('\n');
    line.trim();
    if (!patchedHeader && line.startsWith("#boot_unix=")) {
      // Replace with correct value now that we know it
      tmp.printf("#boot_unix=%lu\n", browserTimeOffset);
      patchedHeader = true;
    } else {
      tmp.println(line);
    }
  }
  src.close();
  tmp.close();

  SD_MMC.remove("/fallback.csv");
  File s = SD_MMC.open("/fb_hdr_tmp.csv", FILE_READ);
  File d = SD_MMC.open("/fallback.csv", FILE_WRITE);
  if (s && d) {
    while (s.available()) d.write(s.read());
    s.close(); d.close();
  }
  SD_MMC.remove("/fb_hdr_tmp.csv");
  Serial.printf("SD: fallback boot_unix updated to %lu\n", browserTimeOffset);
}

void backdateFallbackLog() {
  if (!sdAvailable) return;
  if (!SD_MMC.exists("/fallback.csv")) return;

  File in = SD_MMC.open("/fallback.csv", FILE_READ);
  if (!in) return;

  // Read the boot_unix from the embedded header comment
  unsigned long fallbackBootUnix = 0;
  bool firstLine = true;
  bool secondLine = true;

  // Peek at first two lines to find #boot_unix
  String header1 = in.readStringUntil('\n'); // "datetime,uptime_s,co2_ppm"
  String header2 = in.readStringUntil('\n'); // "#boot_unix=XXXXXXXXXX"
  header2.trim();
  if (header2.startsWith("#boot_unix=")) {
    fallbackBootUnix = header2.substring(11).toInt();
  }

  // If boot_unix is 0 or missing, we can't safely backdate — leave file alone
  if (fallbackBootUnix == 0) {
    in.close();
    Serial.println("SD: fallback.csv has no boot_unix — skipping backdate");
    return;
  }

  File out = SD_MMC.open("/redate_tmp.csv", FILE_WRITE);
  if (!out) { in.close(); return; }

  out.println("datetime,uptime_s,co2_ppm");

  while (in.available()) {
    String line = in.readStringUntil('\n');
    line.trim();
    if (line.length() < 2) continue;
    if (line.startsWith("#")) continue; // skip comment lines

    int c1 = line.indexOf(',');
    int c2 = line.indexOf(',', c1 + 1);
    if (c1 < 0 || c2 < 0) continue;

    unsigned long uptimeSecs = line.substring(c1 + 1, c2).toInt();
    int co2 = line.substring(c2 + 1).toInt();

    // Use the fallback file's own boot reference, not current browserTimeOffset
    unsigned long unix = fallbackBootUnix + uptimeSecs;
    unsigned long t = unix;
    int s  = t % 60; t /= 60;
    int m  = t % 60; t /= 60;
    int h  = t % 24; t /= 24;
    unsigned long days = t;
    int year = 1970;
    while (true) {
      bool leap = (year%4==0 && (year%100!=0 || year%400==0));
      int diy = leap ? 366 : 365;
      if (days < (unsigned long)diy) break;
      days -= diy; year++;
    }
    bool leap = (year%4==0 && (year%100!=0 || year%400==0));
    int monthDays[] = {31,leap?29:28,31,30,31,30,31,31,30,31,30,31};
    int month = 0;
    while (days >= (unsigned long)monthDays[month]) { days -= monthDays[month]; month++; }
    char buf[24];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             year, month+1, (int)days+1, h, m, s);
    out.printf("%s,%lu,%d\n", buf, uptimeSecs, co2);
  }

  in.close();
  out.close();

  // Safe to delete original and move temp to dated file
  SD_MMC.remove("/fallback.csv");
  char datedFile[32];
  getLogFilenameFromUnix(fallbackBootUnix, datedFile, sizeof(datedFile));

  if (SD_MMC.exists(datedFile)) {
    File dst = SD_MMC.open(datedFile, FILE_APPEND);
    File src = SD_MMC.open("/redate_tmp.csv", FILE_READ);
    if (dst && src) {
      bool skipHeader = true;
      while (src.available()) {
        String line = src.readStringUntil('\n');
        line.trim();
        if (skipHeader) { skipHeader = false; continue; }
        if (line.length() > 1) dst.println(line);
      }
      src.close(); dst.close();
    }
    SD_MMC.remove("/redate_tmp.csv");
  } else {
    File src = SD_MMC.open("/redate_tmp.csv", FILE_READ);
    File dst = SD_MMC.open(datedFile, FILE_WRITE);
    if (src && dst) {
      while (src.available()) dst.write(src.read());
      src.close(); dst.close();
    }
    SD_MMC.remove("/redate_tmp.csv");
  }
  Serial.printf("SD: fallback.csv backdated into %s\n", datedFile);
}


// ============================================================
//  K30
// ============================================================

int readK30() {
  while (K30Serial.available()) K30Serial.read();
  K30Serial.write(K30_READ, 7);
  delay(500);
  unsigned long start = millis();
  while (K30Serial.available() < 7) {
    if (millis() - start > CMD_TIMEOUT) return -1;
    delay(10);
  }
  byte r[7];
  K30Serial.readBytes(r, 7);
  if (r[0] != 0xFE || r[1] != 0x44) return -1;
  int16_t co2 = (int16_t)((r[3] << 8) | r[4]);
  return co2;
}

void storeReading(int co2, float o2, unsigned long uptimeSecs) {
  co2History[historyIndex]  = co2;
  o2History[historyIndex]   = o2;
  timeHistory[historyIndex] = uptimeSecs;
  historyIndex = (historyIndex + 1) % HISTORY_SIZE;
  if (historyCount < HISTORY_SIZE) historyCount++;
  if (co2 >= 0) {
    if (co2 < sessionMin) sessionMin = co2;
    if (co2 > sessionMax) sessionMax = co2;
  }
  sdWriteReading(co2, o2, uptimeSecs);
}

void storeO2Reading(float o2, unsigned long uptimeSecs) {
  if (!o2Available) return;
  o2History[historyIndex]  = o2;  // shares same index as co2/time
  o2HistoryCount = min(o2HistoryCount + 1, HISTORY_SIZE);
}

bool confirmDialog(const char* msg) {
  // Draw overlay
  gfx->fillRoundRect(MAIN_X + 10, 30, MAIN_W - 20, 110, 8, 0x1A1A2E);
  gfx->drawRoundRect(MAIN_X + 10, 30, MAIN_W - 20, 110, 8, COL_TITLE);

  // Message
  gfx->setTextColor(COL_WHITE);
  gfx->setTextSize(2);
  int tw = strlen(msg) * 12;
  gfx->setCursor(MAIN_X + (MAIN_W - tw) / 2, 48);
  gfx->print(msg);

  // Subtitle
  gfx->setTextColor(COL_UNIT);
  gfx->setTextSize(1);
  tw = 13 * 6; // "Are you sure?" = 13 chars at size 1
  gfx->setCursor(MAIN_X + (MAIN_W - tw) / 2, 72);
  gfx->print("Are you sure?");

  // OK button (green)
  gfx->fillRoundRect(MAIN_X + 16, 96, 90, 34, 6, COL_BG);
  gfx->drawRoundRect(MAIN_X + 16, 96, 90, 34, 6, COL_GOOD);
  gfx->setTextColor(COL_GOOD);
  gfx->setTextSize(2);
  tw = 2 * 12; // "OK" = 2 chars at size 2
  gfx->setCursor(MAIN_X + 16 + (90 - tw) / 2, 106);
  gfx->print("OK");

  // Cancel button (red)
  gfx->fillRoundRect(MAIN_X + 126, 96, 90, 34, 6, COL_BG);
  gfx->drawRoundRect(MAIN_X + 126, 96, 90, 34, 6, COL_BAD);
  gfx->setTextColor(COL_BAD);
  gfx->setTextSize(2);
  tw = 6 * 12; // "Cancel" = 6 chars at size 2
  gfx->setCursor(MAIN_X + 126 + (90 - tw) / 2, 106);
  gfx->print("Cancel");

  // Wait for touch
  unsigned long timeout = millis() + 15000; // auto-cancel after 15s
  while (millis() < timeout) {
    server.handleClient();
    bsp_touch_read();
    touch_data_t td;
    if (bsp_touch_get_coordinates(&td)) {
      int tx = td.coords[0].x;
      int ty = td.coords[0].y;
      delay(50); // debounce

      // Confirm zone
      if (tx >= MAIN_X + 16 && tx <= MAIN_X + 106 && ty >= 96 && ty <= 130)
        return true;
      // Cancel zone
      if (tx >= MAIN_X + 126 && tx <= MAIN_X + 216 && ty >= 96 && ty <= 130)
        return false;
    }
    delay(10);
  }
  return false; // timed out
}

void sendCalibration(byte* cmd, const char* label) {
  calibrating = true;
  displayStatus(label, COL_WARN);
  Serial.print("Calibrating: "); Serial.println(label);

  while (K30Serial.available()) K30Serial.read();
  K30Serial.write(K30_UNLOCK, 8);
  delay(200);
  while (K30Serial.available()) K30Serial.read();

  K30Serial.write(cmd, 8);
  delay(500);
  while (K30Serial.available()) K30Serial.read();

  unsigned long settle = millis();
  while (millis() - settle < 8000) {
    while (K30Serial.available()) K30Serial.read();
    delay(100);
  }

  displayStatus("Done! Reading...", COL_GOOD);
  delay(1000);

  lastReadTime = 0;
  saveCalRecord(label);
  calibrating  = false;
}

// ============================================================
//  WEB SERVER
// ============================================================
void handleApiData() {
  String json = "{";
  json += "\"co2\":" + String(lastCO2) + ",";

  // ================================
  // CO2 DATA (windowed + capped)
  // ================================
  int start = (historyCount < HISTORY_SIZE) ? 0 : historyIndex;

  unsigned long tLast = (historyCount > 0) ?
    timeHistory[(start + historyCount - 1) % HISTORY_SIZE] : 0;

  unsigned long windowStart = (tLast > GRAPH_WINDOW_S) ? tLast - GRAPH_WINDOW_S : 0;

  int sendStart = start;
  int sendCount = historyCount;

  // Trim to graph window
  while (sendCount > 1 && timeHistory[sendStart % HISTORY_SIZE] < windowStart) {
    sendStart = (sendStart + 1) % HISTORY_SIZE;
    sendCount--;
  }

  // Cap for browser performance
  const int MAX_WEB_POINTS = HISTORY_SIZE;

  // Cap for browser performance
  if (sendCount > MAX_WEB_POINTS) {
    sendStart = (sendStart + sendCount - MAX_WEB_POINTS) % HISTORY_SIZE;
    sendCount = MAX_WEB_POINTS;
  }

  // Send times
  json += "\"times\":[";
  for (int i = 0; i < sendCount; i++) {
    int idx = (sendStart + i) % HISTORY_SIZE;
    if (i > 0) json += ",";
    // Send as Unix timestamp if we have a valid offset, otherwise uptime
    if (browserTimeOffset > 0) {
      json += String(browserTimeOffset + timeHistory[idx]);
    } else {
      json += String(timeHistory[idx]);
    }
  }

  // Send values
  json += "],\"values\":[";
  for (int i = 0; i < sendCount; i++) {
    int idx = (sendStart + i) % HISTORY_SIZE;
    if (i > 0) json += ",";
    json += String(co2History[idx]);
  }
  json += "],";

  // ================================
  // METADATA
  // ================================
  json += "\"last_cal_type\":\"" + lastCalType + "\",";
  json += "\"last_cal_unix\":" + String(lastCalUnix) + ",";
  json += "\"read_interval\":" + String(READ_INTERVAL) + ",";
  json += "\"graph_window\":" + String(GRAPH_WINDOW_S) + ",";
  json += "\"pi_connected\":" + String(piConnected ? "true" : "false") + ",";
  json += "\"times_are_unix\":" + String(timeSynced ? "true" : "false") + ",";

  // ================================
  // O2 CURRENT VALUE
  // ================================
  if (o2Available && !isnan(lastO2)) {
    json += "\"o2\":" + String(lastO2, 2) + ",";
  } else {
    json += "\"o2\":null,";
  }

  // ================================
  // O2 DATA (same logic)
  // ================================
  int o2start = (o2HistoryCount < HISTORY_SIZE) ? 0 : o2HistoryIndex;

  unsigned long o2tLast = (o2HistoryCount > 0) ?
    o2TimeHistory[(o2start + o2HistoryCount - 1) % HISTORY_SIZE] : 0;

  unsigned long o2WindowStart = (o2tLast > GRAPH_WINDOW_S) ? o2tLast - GRAPH_WINDOW_S : 0;

  int o2sendStart = o2start;
  int o2sendCount = o2HistoryCount;

  // Trim
  while (o2sendCount > 1 && o2TimeHistory[o2sendStart % HISTORY_SIZE] < o2WindowStart) {
    o2sendStart = (o2sendStart + 1) % HISTORY_SIZE;
    o2sendCount--;
  }

  // Cap
  if (o2sendCount > MAX_WEB_POINTS) {
    o2sendStart = (o2sendStart + o2sendCount - MAX_WEB_POINTS) % HISTORY_SIZE;
    o2sendCount = MAX_WEB_POINTS;
  }

  // Send O2 times
  json += "\"o2times\":[";
  for (int i = 0; i < o2sendCount; i++) {
    int idx = (o2sendStart + i) % HISTORY_SIZE;
    if (i > 0) json += ",";
    if (browserTimeOffset > 0) {
      json += String(browserTimeOffset + o2TimeHistory[idx]);
    } else {
      json += String(o2TimeHistory[idx]);
    }
  }

  // Send O2 values
  json += "],\"o2values\":[";
  for (int i = 0; i < o2sendCount; i++) {
    int idx = (o2sendStart + i) % HISTORY_SIZE;
    if (i > 0) json += ",";
    json += String(o2History[idx], 2);
  }
  json += "],";

  json += "\"o2_available\":" + String(o2Available ? "true" : "false");
  json += "}";

  server.send(200, "application/json", json);
}

void handleSetTime() {
  if (server.hasArg("t")) {
    unsigned long browserUnix = server.arg("t").toInt();
    unsigned long uptimeSecs  = (millis() - startTime) / 1000;

    long newOffset = (long)browserUnix - (long)uptimeSecs;

    if (!timeSynced || labs(newOffset - browserTimeOffset) < 300) {
      browserTimeOffset = newOffset;
      timeSynced = true;
      warningDrawn = false;
      drawStaticUI();

      saveTimeToSD(browserUnix);
      updateFallbackBootUnix();
      backdateFallbackLog();

      Serial.printf("Time set via browser: %lu\n", browserUnix);
    } else {
      Serial.println("Rejected browser time (too large jump)");
    }
  }

  server.send(200, "text/plain", "OK");
}

void handleDownload() {
  if (!server.hasArg("file")) {
    server.send(400, "text/plain", "Missing file");
    return;
  }

  String filename = server.arg("file");
  File f = SD_MMC.open(filename);
  if (!f) {
    server.send(404, "text/plain", "File not found");
    return;
  }

  server.sendHeader("Content-Disposition", "attachment; filename=" + filename);
  server.streamFile(f, "text/csv");
  f.close();
}

void handleRoot() {
  String html = F(R"rawhtml(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>CO2 Monitor</title>
<style>
:root {
  --co2-color: #00e5ff;
  --o2-color:  #ffb74d;
  --bg-main: #0a0a0f;
  --bg-card: #12121a;
  --bg-input: #1e1e2e;
  --border-color: #1e1e2e;
  --border-input: #333;
  --text-main: #e0e0e0;
  --text-muted: #888;
}

* { box-sizing: border-box; margin: 0; padding: 0; }

body {
  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
  background: var(--bg-main);
  color: var(--text-main);
  min-height: 100vh;
  padding: 20px;
  max-width: 600px; /* Constrain width on large screens */
  margin: 0 auto;
}

h1 {
  text-align: center;
  color: #ffffff;
  font-size: 1.4em;
  letter-spacing: 2px;
  text-transform: uppercase;
  margin-bottom: 20px;
}

h3 {
  text-align: left;
  color: #ffffff;
  font-size: 1em;
  margin-bottom: 8px;
  margin-top: 20px;
}

/* --- Cards & Layout --- */
.card {
  background: var(--bg-card);
  border: 1px solid var(--border-color);
  border-radius: 12px;
  padding: 20px;
  margin-bottom: 16px;
}

.center-text { text-align: center; }

.row {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.col {
  display: flex;
  flex-direction: column;
  gap: 12px;
  margin-bottom: 16px;
}

/* --- Readings --- */
.reading-box {
  display: flex;
  flex-direction: column;
  gap: 14px;
  align-items: center;
}

.sensor-block {
  width: 100%;
  max-width: 260px;
}

.divider {
  height: 1px;
  width: 60%;
  background: #222;
  margin: 4px auto;
}

.reading-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.reading-label {
  font-size: 0.75em;
  color: #666;
  letter-spacing: 1px;
  text-transform: uppercase;
  margin-bottom: 2px;
}

.reading-value {
  display: flex;
  align-items: baseline;
  gap: 6px;
}

.co2-value {
  font-variant-numeric: tabular-nums;
  font-size: clamp(2.5em, 8vw, 5em);
  font-weight: 700;
  line-height: 1.1;
  text-align: center;
  width: auto;
  white-space: nowrap;
}

.unit {
  font-size: 1.2em;
  color: #666;
  letter-spacing: 0.5px;
}

.trend {
  font-size: 1.8em;
  min-width: 24px;
  text-align: right;
  color: #aaa;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: transform 0.15s ease, color 0.2s ease, opacity 0.2s ease;
  opacity: 0.85;
}

/* --- Charts --- */
.chart-label { 
  font-size: 0.75em; 
  color: #555; 
  margin-bottom: 8px; 
  text-transform: uppercase; 
  letter-spacing: 1px; 
}
canvas { width: 100%; height: 220px; display: block; }

/* --- Forms & Buttons --- */
.button {
  background: #15181c;
  color: #ffffff;
  border: 1px solid #444; /* Softened from #b5b5b5 */
  border-radius: 8px;
  padding: 10px 16px;
  font-weight: 600;
  font-size: 0.9em;
  margin: 4px;
  cursor: pointer;
  transition: background 0.3s, border-color 0.3s;
}

.button:hover { background: #00e5ff22; border-color: var(--co2-color); }
.button:active { background: #00e5ff44; }

.form-control {
  background: var(--bg-input);
  color: var(--text-main);
  border: 1px solid var(--border-input);
  border-radius: 6px;
  padding: 6px 10px;
  width: 140px;
  outline: none;
  transition: border-color 0.2s;
}

.form-control:focus {
  border-color: var(--co2-color);
}

.form-label { color: #ccc; font-size: 0.9em; }

/* --- Dynamic Logs (Injected via JS) --- */
.log-item {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 6px;
  background: var(--bg-card);
  padding: 8px 12px;
  border-radius: 8px;
  border: 1px solid var(--border-color);
}

.log-title { color: #ccc; line-height: 1.2; }
.log-subtitle { font-size: 0.85em; color: var(--text-muted); }
.log-actions { display: flex; gap: 6px; }

/* --- Modifiers for the standard .button class --- */
.button.small {
  padding: 6px 12px;
  font-size: 0.8em;
  margin: 0;
}

.button.primary {
  border-color: var(--co2-color);
  color: var(--co2-color);
}
.button.primary:hover {
  background: #00e5ff22;
}

.button.danger {
  border-color: #f44336;
  color: #f44336;
}
.button.danger:hover {
  background: #f4433622;
}

.updated { text-align: center; font-size: 0.7em; color: #555; margin-top: 8px; }
.cal-info { margin-top: 12px; font-size: 0.85em; color: var(--text-muted); }
</style>
</head>
<body>

<h1 id="main_title" style="margin-bottom: 2px;">CO<sub>2</sub> Monitor</h1>
<div id="pi-status" style="text-align:center; font-size:0.8em; margin-bottom:8px;">○ Pi</div>

<div class="card">
  <div class="reading-box">
    <div class="sensor-block">
      <div class="reading-label">CO₂</div>
      <div class="reading-row">
        <div class="reading-value">
          <div class="co2-value" id="co2val">--</div>
          <span class="unit">ppm</span>
        </div>
        <div id="co2trend" class="trend">·</div>
      </div>
    </div>
    
    <div class="divider"></div>
    
    <div class="sensor-block">
      <div class="reading-label">O₂</div>
      <div class="reading-row">
        <div class="reading-value">
          <div class="co2-value" id="o2val">--</div>
          <span class="unit">%</span>
        </div>
        <div id="o2trend" class="trend">·</div>
      </div>
    </div>
  </div>
</div>

<div class="card">
  <div class="chart-label" id="co2chartlabel">CO₂ — last 30 min</div>
  <canvas id="chart"></canvas>
</div>

<div class="card">
  <div class="chart-label" id="o2chartlabel">O₂ — last 30 min</div>
  <canvas id="o2chart"></canvas>
</div>

<h3>Calibrate CO<sub>2</sub> sensor</h3>
<div class="card center-text">
  <button class="button" onclick="calibrate(400)">400 ppm</button>
  <button class="button" onclick="calibrate(0)">0 ppm</button>
  <div id="calinfo" class="cal-info">Last calibration: unknown</div>
</div>

<h3>Calibration log</h3>
<div id="callog">Loading...</div>

<h3>Settings</h3>
<div class="card">
  <div class="col">
    <div class="row">
      <label class="form-label">Graph window</label>
      <select id="set_graph_window" class="form-control">
        <option value="60">1 min</option>
        <option value="120">2 min</option>
        <option value="900">15 min</option>
        <option value="1800">30 min</option>
        <option value="2700">45 min</option>
        <option value="3600">60 min</option>
        <option value="7200">2 hr</option>
        <option value="21600">6 hr</option>
        <option value="43200">12 hr</option>
        <option value="86400">24 hr</option>
        <option value="172800">48 hr</option>
        <option value="345600">96 hr</option>
      </select>
    </div>
    <div class="row">
      <label class="form-label">Read interval</label>
      <select id="set_read_interval" class="form-control">
        <option value="1000">1s</option>
        <option value="2000">2s</option>
        <option value="5000">5s</option>
        <option value="10000">10s</option>
        <option value="30000">30s</option>
        <option value="60000">60s</option>
        <option value="300000">5m</option>
        <option value="1800000">30m</option>
      </select>
    </div>
    
    <div class="row">
      <label class="form-label">Display Flag</label>
      <select id="set_flag_index" class="form-control">
        <option value="0">🇲🇱 Mali</option>
        <option value="1">🇰🇷 South Korea</option>
        <option value="2">🏴󠁧󠁢󠁥󠁮󠁧󠁿 England</option>
        <option value="3">🏢 Itaim Bibi</option>
      </select>
    </div>
    </div>
  <button class="button" style="width: 100%" onclick="saveSettings()">Save settings</button>
</div>

<h3>Network settings</h3>
<div class="card">
  <div class="col">
    <div class="row">
      <label class="form-label">Device Name</label>
      <input id="set_device_name" type="text" disabled class="form-control" maxlength="31">
    </div>
    <div class="row">
      <label class="form-label">Location</label>
      <input id="set_location" type="text" disabled class="form-control" maxlength="31">
    </div>
    <div class="row">
      <label class="form-label">WiFi SSID</label>
      <input id="set_ap_ssid" type="text" disabled class="form-control">
    </div>
    <div class="row">
      <label class="form-label">WiFi password</label>
      <input id="set_ap_pass" type="password" disabled class="form-control">
    </div>
  </div>
  <button class="button" style="width: 100%" onclick="handleNetSettings()">Unlock & Save</button>
</div>

<h3>Logs</h3>
<div id="logfiles">Loading...</div>

<div class="updated" id="updated" style="margin-top:20px;">Updating...</div>

<script>
// --- JavaScript Canvas & Data Logic Remains Mostly Unchanged ---
let prevCO2js = null;
let prevO2js  = null;
let lastCO2Trend = '·';
let lastO2Trend  = '·';
let lastCO2TrendColor = '#aaa';
let lastO2TrendColor  = '#aaa';
let netUnlocked = false;
let logsUnlocked = false;
const canvas = document.getElementById('chart');
const ctx    = canvas.getContext('2d');
const o2canvas = document.getElementById('o2chart');
const o2ctx    = o2canvas.getContext('2d');

function downsampleEveryNth(times, values, maxPoints) {
  const n = times.length;

  if (n <= maxPoints) return { times, values };

  const step = Math.ceil(n / maxPoints);

  const tOut = [];
  const vOut = [];

  for (let i = 0; i < n; i += step) {
    tOut.push(times[i]);
    vOut.push(values[i]);
  }

  return { times: tOut, values: vOut };
}

function resizeCanvas() {
  const dpr = window.devicePixelRatio || 1;
  for (const [c, cx] of [[canvas, ctx], [o2canvas, o2ctx]]) {
    const cssWidth  = c.parentElement.clientWidth || 300;
    const cssHeight = 220;
    c.style.width  = cssWidth + 'px';
    c.style.height = cssHeight + 'px';
    c.width  = cssWidth * dpr;
    c.height = cssHeight * dpr;
    cx.resetTransform();
    cx.scale(dpr, dpr);
    c._w = cssWidth;
    c._h = cssHeight;
  }
}

resizeCanvas();
window.addEventListener('load', resizeCanvas);
window.addEventListener('resize', resizeCanvas);

function co2Colour(v) { return '#00e5ff'; }
function o2Colour() { return '#ffb74d'; }

function drawChart(times, values, readIntervalMs = 6000, timesAreUnix = false) {
  const W = canvas._w; const H = canvas._h;
  ctx.clearRect(0, 0, W, H);

  const stylePad = 20; const leftExtra = 10; const rightExtra = 15; const bottomExtra = 20;
  const pad = { top: 20, left: stylePad + leftExtra, right: stylePad + rightExtra, bottom: bottomExtra + 10 };
  const cW = W - pad.left - pad.right; const cH = H - pad.top - pad.bottom;

  // Draw background frame
  ctx.beginPath(); ctx.strokeStyle = '#555'; ctx.lineWidth = 1.5;
  ctx.moveTo(pad.left, pad.top); ctx.lineTo(pad.left, pad.top + cH);
  ctx.moveTo(pad.left, pad.top + cH); ctx.lineTo(pad.left + cW, pad.top + cH);
  ctx.stroke();

  const minV = Math.max(0, Math.min(...values) - 50);
  const maxV = Math.max(...values) + 50;
  const vRange = maxV - minV || 1;
  const minT = times[0], maxT = times[times.length - 1];
  const tRange = maxT - minT || 1;

  function xp(t) { return pad.left + (t - minT) / tRange * cW; }
  function yp(v) { return pad.top + cH - (v - minV) / vRange * cH; }

  if (values.length < 2) {
    ctx.fillStyle = '#888'; ctx.font = '14px sans-serif'; ctx.textAlign = 'center';
    ctx.fillText('Collecting data...', W/2, H/2);
    return;
  }

  // Y-Axis Labels
  const tickStep = Math.max(50, Math.ceil(vRange / 5 / 50) * 50);
  const firstTick = Math.ceil(minV / tickStep) * tickStep;
  ctx.font = '11px sans-serif'; ctx.textAlign = 'right';
  
  for (let v = firstTick; v <= maxV; v += tickStep) {
    const y = yp(v);
    ctx.beginPath(); ctx.setLineDash([2,4]); ctx.strokeStyle = '#222'; ctx.lineWidth = 1;
    ctx.moveTo(pad.left, y); ctx.lineTo(pad.left + cW, y); ctx.stroke(); ctx.setLineDash([]);
    ctx.fillStyle = '#aaa'; ctx.fillText(v, pad.left - 6, y + 4);
  }

  // --- MATCHED X-AXIS STYLING FROM O2 ---
  ctx.fillStyle = '#aaa'; ctx.textAlign = 'center'; ctx.font = '11px sans-serif';
  const totalSecs = maxT - minT;
  const targetStep = totalSecs / 5; // Aim for exactly ~5 labels
  
  const niceSteps = [30, 60, 120, 300, 600, 900, 1800, 3600, 7200, 14400, 21600, 43200, 86400];
  let labelStepSecs = niceSteps[niceSteps.length - 1];
  for (let step of niceSteps) {
    if (step >= targetStep) { labelStepSecs = step; break; }
  }

  const firstLabel = Math.ceil(minT / labelStepSecs) * labelStepSecs;
  for (let t = firstLabel; t <= maxT; t += labelStepSecs) {
    const px = xp(t);
    if (px < pad.left + 10 || px > pad.left + cW - 10) continue;
    
    // 1. Draw the downward Tick Mark
    ctx.beginPath();
    ctx.strokeStyle = '#555';
    ctx.lineWidth = 1.5;
    ctx.moveTo(px, pad.top + cH);
    ctx.lineTo(px, pad.top + cH + 5);
    ctx.stroke();

    // 2. Prepare the Label Text
    let label;
    if (timesAreUnix) {
      const d = new Date(t * 1000);
      const totalHours = totalSecs / 3600;
      if (totalHours < 24) {
        label = d.toLocaleTimeString([], {hour: '2-digit', minute: '2-digit'});
      } else {
        label = d.toLocaleDateString([], {month: 'short', day: 'numeric'}) + ' ' + d.getHours() + 'h';
      }
    } else {
      const rel = t - minT;
      if (rel < 120) label = rel + 's';
      else if (rel < 3600) label = Math.round(rel/60) + 'm';
      else label = Math.round(rel/3600) + 'h';
    }
    
    // 3. Draw the Text (shifted down)
    ctx.fillText(label, px, H - 4);
  }

  // Draw Gradient Fill
  const grad = ctx.createLinearGradient(0, pad.top, 0, pad.top + cH);
  grad.addColorStop(0, '#00e5ff18'); grad.addColorStop(1, '#00e5ff00');
  ctx.beginPath(); ctx.moveTo(xp(times[0]), yp(values[0]));
  for (let i = 1; i < values.length; i++) ctx.lineTo(xp(times[i]), yp(values[i]));
  ctx.lineTo(xp(times[times.length-1]), yp(minV)); ctx.lineTo(xp(times[0]), yp(minV));
  ctx.closePath(); ctx.fillStyle = grad; ctx.fill();

  // Draw CO2 Line
  for (let i = 1; i < values.length; i++) {
    ctx.beginPath(); ctx.strokeStyle = co2Colour(); ctx.lineWidth = 2;
    ctx.moveTo(xp(times[i-1]), yp(values[i-1])); ctx.lineTo(xp(times[i]), yp(values[i]));
    ctx.stroke();
  }
}

function drawO2Chart(times, values, readIntervalMs = 6000, timesAreUnix = false) {
  const W = o2canvas._w; const H = o2canvas._h;
  o2ctx.clearRect(0, 0, W, H);

  const pad = { top: 20, left: 20 + 8, right: 20 + 15, bottom: 20 + 10 };
  const cW = W - pad.left - pad.right; const cH = H - pad.top - pad.bottom;

  // Draw background frame
  o2ctx.beginPath(); o2ctx.strokeStyle = '#555'; o2ctx.lineWidth = 1.5;
  o2ctx.moveTo(pad.left, pad.top); o2ctx.lineTo(pad.left, pad.top + cH);
  o2ctx.moveTo(pad.left, pad.top + cH); o2ctx.lineTo(pad.left + cW, pad.top + cH);
  o2ctx.stroke();

  if (values.length < 2) {
    o2ctx.fillStyle = '#888'; o2ctx.font = '14px sans-serif'; o2ctx.textAlign = 'center';
    o2ctx.fillText('Collecting O\u2082 data...', W/2, H/2);
    return;
  }

  const minV = Math.max(0, Math.min(...values) - 0.5);
  const maxV = Math.max(...values) + 0.5;
  const vRange = maxV - minV || 1;
  const minT = times[0], maxT = times[times.length-1];
  const tRange = maxT - minT || 1;

  function xp(t) { return pad.left + (t - minT) / tRange * cW; }
  function yp(v) { return pad.top + cH - (v - minV) / vRange * cH; }

  // Y-Axis Labels
  o2ctx.font = '11px sans-serif'; o2ctx.textAlign = 'right';
  const o2Range = maxV - minV;
  const o2TickStep = o2Range <= 0.4 ? 0.1 : o2Range <= 1.0 ? 0.2 : o2Range <= 2.0 ? 0.5 : 1.0;
  const o2FirstTick = Math.ceil(minV / o2TickStep) * o2TickStep;
  
  for (let v = o2FirstTick; v <= maxV; v += o2TickStep) {
    const y = yp(v);
    o2ctx.beginPath(); o2ctx.setLineDash([2,4]); o2ctx.strokeStyle = '#222'; o2ctx.lineWidth = 1;
    o2ctx.moveTo(pad.left, y); o2ctx.lineTo(pad.left + cW, y); o2ctx.stroke(); o2ctx.setLineDash([]);
    o2ctx.fillStyle = '#aaa'; o2ctx.fillText(v.toFixed(1), pad.left - 4, y + 4);
  }

  // 20.9% Reference line
  if (20.9 > minV && 20.9 < maxV) {
    o2ctx.beginPath(); o2ctx.setLineDash([4,4]); o2ctx.strokeStyle = '#33557788'; o2ctx.lineWidth = 1;
    o2ctx.moveTo(pad.left, yp(20.9)); o2ctx.lineTo(pad.left + cW, yp(20.9)); o2ctx.stroke(); o2ctx.setLineDash([]);
  }

  // --- X-AXIS: DYNAMIC LABELS & TICKS (Max ~5 labels) ---
  o2ctx.fillStyle = '#aaa'; o2ctx.textAlign = 'center'; o2ctx.font = '11px sans-serif';
  const totalSecs = maxT - minT;
  const targetStep = totalSecs / 5; // Divide the graph width by 5 to find ideal gap
  
  // Array of "clean" time intervals to snap to (in seconds)
  const niceSteps = [30, 60, 120, 300, 600, 900, 1800, 3600, 7200, 14400, 21600, 43200, 86400];
  let labelStepSecs = niceSteps[niceSteps.length - 1];
  for (let step of niceSteps) {
    if (step >= targetStep) { labelStepSecs = step; break; }
  }

  const firstLabel = Math.ceil(minT / labelStepSecs) * labelStepSecs;
  for (let t = firstLabel; t <= maxT; t += labelStepSecs) {
    const px = xp(t);
    if (px < pad.left + 10 || px > pad.left + cW - 10) continue;
    
    // 1. Draw the downward Tick Mark
    o2ctx.beginPath();
    o2ctx.strokeStyle = '#555';
    o2ctx.lineWidth = 1.5;
    o2ctx.moveTo(px, pad.top + cH);
    o2ctx.lineTo(px, pad.top + cH + 5); // Drops 5 pixels below the axis line
    o2ctx.stroke();

    // 2. Prepare the Label Text
    let label;
    if (timesAreUnix) {
      const d = new Date(t * 1000);
      const totalHours = totalSecs / 3600;
      if (totalHours < 24) {
        label = d.toLocaleTimeString([], {hour: '2-digit', minute: '2-digit'});
      } else {
        label = d.toLocaleDateString([], {month: 'short', day: 'numeric'}) + ' ' + d.getHours() + 'h';
      }
    } else {
      const rel = t - minT;
      if (rel < 120) label = rel + 's';
      else if (rel < 3600) label = Math.round(rel/60) + 'm';
      else label = Math.round(rel/3600) + 'h';
    }
    
    // 3. Draw the Text (Shifted down slightly to clear the tick mark)
    o2ctx.fillText(label, px, pad.top + cH + 25);
  }

  // Draw Gradient Fill
  const grad = o2ctx.createLinearGradient(0, pad.top, 0, pad.top + cH);
  grad.addColorStop(0, '#ffb74d18'); grad.addColorStop(1, '#ffb74d00');
  o2ctx.beginPath(); o2ctx.moveTo(xp(times[0]), yp(Math.min(Math.max(values[0], minV), maxV)));
  for (let i = 1; i < values.length; i++) o2ctx.lineTo(xp(times[i]), yp(Math.min(Math.max(values[i], minV), maxV)));
  o2ctx.lineTo(xp(times[times.length-1]), yp(minV)); o2ctx.lineTo(xp(times[0]), yp(minV));
  o2ctx.closePath(); o2ctx.fillStyle = grad; o2ctx.fill();

  // Draw O2 Line
  for (let i = 1; i < values.length; i++) {
    const v = values[i];
    o2ctx.beginPath(); o2ctx.strokeStyle = o2Colour(); o2ctx.lineWidth = 2;
    o2ctx.moveTo(xp(times[i-1]), yp(Math.min(Math.max(values[i-1], minV), maxV)));
    o2ctx.lineTo(xp(times[i]),   yp(Math.min(Math.max(v, minV), maxV))); o2ctx.stroke();
  }
}

async function calibrate(ppm) {
  // 1. Set specific instructions based on the button pressed
  let environmentInfo = "";
  if (ppm === 400) {
    environmentInfo = "(compressed air [see caveats!] or outside)";
  } else if (ppm === 0) {
    environmentInfo = "(nitrogen or 0 ppm compressed air cylinder)";
  }

  // 2. Trigger the browser's confirmation prompt
  const msg = `Are you sure you want to calibrate the sensor to ${ppm} ppm? Is the sensor stable in ${environmentInfo}? This cannot be undone.`;
  if (!confirm(msg)) {
    return; // Stop here if the user clicked "Cancel"
  }

  // 3. Proceed with calibration if they clicked "OK"
  const url = ppm === 400 ? '/cal/400' : '/cal/0';
  try {
    const res = await fetch(url);
    if (res.ok) alert('Calibration completed: ' + ppm + ' ppm');
  } catch(e) { alert('Error sending calibration'); }
}

let timeSent = false;
async function update() {
  try {
    // 1. Fetch the data FIRST
    const r = await fetch('/api/data');
    if (!r.ok) throw new Error("API error");
    const d = await r.json();

    // 2. NEW LOGIC: Only fallback to phone time if the Pi failed!
    if (d.times_are_unix === false && !timeSent) {
      try {
        const now = Math.floor(Date.now() / 1000);
        const tzOffsetSec = new Date().getTimezoneOffset() * -60;
        await fetch(`/settime?t=${now}&o=${tzOffsetSec}`);
        timeSent = true;
        setTimeout(update, 500);
        return;
      } catch(e) { console.log("Fallback time sync failed"); }
    }

    const piEl = document.getElementById('pi-status');
    if (piEl && typeof d.pi_connected !== 'undefined') {
      piEl.textContent = d.pi_connected ? '● Pi connected' : '○ Pi disconnected';
      piEl.style.color = d.pi_connected ? '#4caf50' : '#555';
    }
    
    const co2El = document.getElementById('co2val');
    const co2TrendEl = document.getElementById('co2trend');
    if (typeof d.co2 === "number") {
      co2El.textContent = d.co2;
      co2El.style.color = co2Colour(d.co2);
      if (prevCO2js !== null && d.co2 !== prevCO2js) {
        const diff = d.co2 - prevCO2js;
        if      (diff >   5) { lastCO2Trend = '▴'; lastCO2TrendColor = co2Colour(); }
        else if (diff <  -5) { lastCO2Trend = '▾'; lastCO2TrendColor = co2Colour(); }
        else                 { lastCO2Trend = '·'; lastCO2TrendColor = '#aaa'; }
        co2TrendEl.style.transform = 'scale(1.2)';
        setTimeout(() => co2TrendEl.style.transform = 'scale(1)', 120);
        prevCO2js = d.co2;
      } else if (prevCO2js === null) {
        prevCO2js = d.co2;
      }
      co2TrendEl.textContent = lastCO2Trend;
      co2TrendEl.style.color = lastCO2TrendColor;
    } else {
      co2El.textContent = '--'; co2El.style.color = '#666'; co2TrendEl.textContent = '·';
    }

    const o2El = document.getElementById('o2val');
    const o2TrendEl = document.getElementById('o2trend');
    if (d.o2_available && typeof d.o2 === "number") {
      o2El.textContent = d.o2.toFixed(2);
      o2El.style.color = o2Colour();
      if (prevO2js !== null && d.o2 !== prevO2js) {
        const diff = d.o2 - prevO2js;
        if      (diff >  0.05) { lastO2Trend = '▴'; lastO2TrendColor = o2Colour(); }
        else if (diff < -0.05) { lastO2Trend = '▾'; lastO2TrendColor = o2Colour(); }
        else                   { lastO2Trend = '·'; lastO2TrendColor = '#aaa'; }
        prevO2js = d.o2;
      } else if (prevO2js === null) {
        prevO2js = d.o2;
      }
      o2TrendEl.textContent = lastO2Trend;
      o2TrendEl.style.color = lastO2TrendColor;
    } else {
      o2El.textContent = '--'; o2El.style.color = '#444'; o2TrendEl.textContent = '·';
    }

    resizeCanvas();
    const intervalSec = (d.read_interval || 6000) / 1000;
    const windowSecs = d.graph_window || 1800;
    const windowLabel = windowSecs >= 7200 ? (windowSecs/3600)+'h' :
                        windowSecs >= 60   ? (windowSecs/60)+'m'   : windowSecs+'s';
    
    document.getElementById('co2chartlabel').textContent = `CO₂ — last ${windowLabel} · ${intervalSec}s interval`;
    
    const timesAreUnix = d.times_are_unix === true;
    const lastT = (d.times && d.times.length > 0) ? d.times[d.times.length - 1] : 0;
    const cutoff = lastT - windowSecs;
    let co2Times  = (d.times  || []).filter((_, i) => d.times[i] >= cutoff);
    let co2Values = (d.values || []).filter((_, i) => d.times[i] >= cutoff);

    ({ times: co2Times, values: co2Values } =
      downsampleEveryNth(co2Times, co2Values, 1200));
    let o2Times  = (d.o2times  || []).filter((_, i) => d.o2times[i] >= cutoff);
    let o2Values = (d.o2values || []).filter((_, i) => d.o2times[i] >= cutoff);

    ({ times: o2Times, values: o2Values } =
      downsampleEveryNth(o2Times, o2Values, 1200));
    
    drawChart(co2Times, co2Values, d.read_interval || 6000, timesAreUnix);
    
    if (d.o2_available) {
      document.getElementById('o2chartlabel').textContent = `O₂ — last ${windowLabel} · ${intervalSec}s interval`;
      drawO2Chart(o2Times, o2Values, d.read_interval || 6000, timesAreUnix);
    } else {
      document.getElementById('o2chartlabel').textContent = 'O₂ — sensor unavailable';
      o2ctx.clearRect(0, 0, o2canvas._w, o2canvas._h);
    }

    document.getElementById('updated').textContent = 'Last updated: ' + new Date().toLocaleTimeString();
    if (d.last_cal_type && d.last_cal_unix > 0) {
      const calDate = new Date(d.last_cal_unix * 1000).toLocaleString();
      document.getElementById('calinfo').textContent = `Last calibration: ${d.last_cal_type} — ${calDate}`;
    }

  } catch(e) { console.log("Update error:", e); }
}

async function loadLogs(){
  const r = await fetch('/api/logs');
  const files = await r.json();
  let html = "";

  // Check if the files array is empty (or undefined)
  if (!files || files.length === 0) {
    // Add your placeholder HTML here
    html = `
      <p style="color:#444; font-size:0.85em;">No logs available to display.</p>
      </div>`;
  } else {
    // If there are logs, map through them as normal
    files.forEach(f => {
      const short = f.name.split('/').pop();
      html += `
      <div class="log-item">
        <div>
          <div class="log-title">${short}</div>
          <div class="log-subtitle">${f.name.split('/').pop()}</div>
        </div>
        <div class="log-actions">
          <button class="button small primary" onclick="downloadLog('${encodeURIComponent(f.name)}')">Download</button>
          <button class="button small danger" onclick="deleteLog('${encodeURIComponent(f.name)}')">Delete</button>
        </div>
      </div>`;
    });
  }
  
  document.getElementById("logfiles").innerHTML = html;
}

async function deleteLog(name){
  // Check if the user has already unlocked the delete function
  if (!logsUnlocked) {
    const pass = prompt("Enter admin password to delete logs:");
    if (pass === "admin123") {
      logsUnlocked = true;
    } else {
      alert("Incorrect password");
      return; // Stop execution if the password is wrong
    }
  }

  // Double-check before actual deletion
  if(!confirm("Are you sure you want to delete this log file?")) return;

  await fetch("/delete?file="+name);
  loadLogs(); // Refresh the list
}

async function loadSettings() {
  const r = await fetch('/api/settings');
  const s = await r.json();
  
  // Update the page title if it exists
  if (s.device_name) {
      document.title = s.device_name;
      document.getElementById('main_title').textContent = s.device_name; 
  }
  
  // Fill the standard settings card
  document.getElementById('set_read_interval').value = s.read_interval;
  document.getElementById('set_graph_window').value = s.graph_window || 1800;
  document.getElementById('set_flag_index').value = s.flag_index || 0;
  
  // Fill the Network settings card
  document.getElementById('set_device_name').value = s.device_name || s.sensor_id || '';
  document.getElementById('set_location').value     = s.location   || '';
  document.getElementById('set_ap_ssid').value      = s.ap_ssid    || '';
  document.getElementById('set_ap_pass').value      = s.ap_pass    || '';
}

async function saveSettings() {
  const ri = document.getElementById('set_read_interval').value;
  const gw = document.getElementById('set_graph_window').value;
  
  // NEW: Grab the flag value
  const fi = document.getElementById('set_flag_index').value; 
  
  // NEW: Append flag_index to the URL so the ESP32 receives it
  const r = await fetch(`/api/settings?read_interval=${ri}&graph_window=${gw}&flag_index=${fi}`, {method:'POST'});
  
  if (r.ok) alert('Settings saved');
}

async function saveNetSettings() {
  const dn  = document.getElementById('set_device_name').value;
  const loc = document.getElementById('set_location').value;
  const ss  = document.getElementById('set_ap_ssid').value;
  const sp  = document.getElementById('set_ap_pass').value;
  
  const r = await fetch(
    `/api/settings?device_name=${encodeURIComponent(dn)}&sensor_id=${encodeURIComponent(dn)}&location=${encodeURIComponent(loc)}&ap_ssid=${encodeURIComponent(ss)}&ap_pass=${encodeURIComponent(sp)}`,
    {method:'POST'}
  );
  if (r.ok) alert('Settings saved — reboot device to apply WiFi changes');
}

function handleNetSettings() {
  if (!netUnlocked) {
    const pass = prompt("Enter admin password:");
    if (pass === "admin123") {
      netUnlocked = true;
      // Unlock all 4 boxes in the network card!
      document.getElementById("set_device_name").disabled = false;
      document.getElementById("set_location").disabled = false;
      document.getElementById("set_ap_ssid").disabled = false;
      document.getElementById("set_ap_pass").disabled = false;
      alert("Settings unlocked — press again to save");
    } else {
      alert("Incorrect password");
    }
    return;
  }
  saveNetSettings();
}

async function loadCalLog() {
  const r = await fetch('/api/callogs');
  const entries = await r.json();
  if (entries.length === 0) {
    document.getElementById('callog').innerHTML = '<p style="color:#444; font-size:0.85em;">No calibrations recorded yet</p>';
    return;
  }
  
  let html = '';
  [...entries].reverse().slice(0, 3).forEach(e => {
    const typeLabel = e.type.includes('400') ? '400 ppm' : '0 ppm';
    const typeCol   = e.type.includes('400') ? '#4caf50' : '#00e5ff';
    // Refactored to use classes
    html += `
    <div class="log-item">
      <div class="log-subtitle">
        <span style="color:${typeCol}; font-weight:600;">${typeLabel}</span>
        &nbsp;—&nbsp;${e.dt}
        <span style="color:#555; margin-left:8px;">CO₂ was ${e.co2} ppm</span>
      </div>
    </div>`;
  });
  document.getElementById('callog').innerHTML = html;
}

function downloadLog(name){
  window.location.href = "/download?file=" + name;
}

update();
loadLogs();
loadSettings();
loadCalLog();
setInterval(update, 1000);
</script>
</body>
</html>
)rawhtml");

server.send(200, "text/html", html);
}

// ============================================================
//  CALIBRATION ENDPOINTS
// ============================================================

void handleCal400() {
  sendCalibration(CAL_400, "Calibrating 400 ppm");
  server.send(200, "text/plain", "OK");
}

void handleCal0() {
  sendCalibration(CAL_0, "Calibrating 0 ppm");
  server.send(200, "text/plain", "OK");
}

// ============================================================
//  SETUP
// ============================================================

void setup() {
  Serial.begin(115200);

  // Allocate history buffers in PSRAM
  co2History    = (int*)           ps_malloc(HISTORY_SIZE * sizeof(int));
  timeHistory   = (unsigned long*) ps_malloc(HISTORY_SIZE * sizeof(unsigned long));
  o2History     = (float*)         ps_malloc(HISTORY_SIZE * sizeof(float));
  o2TimeHistory = (unsigned long*) ps_malloc(HISTORY_SIZE * sizeof(unsigned long));

  if (!co2History || !timeHistory || !o2History || !o2TimeHistory) {
    Serial.println("FATAL: PSRAM allocation failed");
    // Flash display red and halt
    gfx->fillScreen(RGB565_RED);
    while(1) delay(1000);
  }

  // Zero initialise
  memset(co2History,    0, HISTORY_SIZE * sizeof(int));
  memset(timeHistory,   0, HISTORY_SIZE * sizeof(unsigned long));
  memset(o2History,     0, HISTORY_SIZE * sizeof(float));
  memset(o2TimeHistory, 0, HISTORY_SIZE * sizeof(unsigned long));

  Serial.printf("PSRAM allocated: %.2f MB used of %.2f MB total\n",
    (float)(HISTORY_SIZE * (sizeof(int) + sizeof(unsigned long) + sizeof(float) + sizeof(unsigned long))) / (1024*1024),
    (float)ESP.getPsramSize() / (1024*1024));

  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);
  gfx->begin();
  lcd_reg_init();
  gfx->setRotation(1);
  drawStaticUI();

    // SD card
  displayStatus("Mounting SD...", COL_WARN);
  initSD();
  delay(1000);
  loadTimeFromSD();
  loadSettings();
  loadCalRecord();
  if (sdAvailable) {
    displayStatus("SD mounted!", COL_GOOD);
  } else {
    displayStatus("No SD - RAM only", COL_WARN);
  }
  delay(500);

  displayStatus("Trying Pi connection...", COL_WARN);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSSID, apPass);
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());

  // Connect to Pi network
  int maxRetries = 3;
  bool piConnected = false;

  for (int attempt = 1; attempt <= maxRetries; attempt++) {
    Serial.printf("\nConnecting to Pi network (Attempt %d of %d)", attempt, maxRetries);
    
    // Clear previous WiFi state to ensure a clean connection attempt
    WiFi.disconnect(); 
    delay(100); 
    
    WiFi.begin(PI_SSID, PI_PASS);
    
    int sta_attempts = 0;
    // Wait up to 25 seconds (50 * 500ms) per attempt
    while (WiFi.status() != WL_CONNECTED && sta_attempts < 10) {
      delay(500);
      Serial.print(".");
      sta_attempts++;
    }

    // Check if this attempt was successful
    if (WiFi.status() == WL_CONNECTED) {
      piConnected = true;
      Serial.printf("\nPi network connected, IP: %s\n", WiFi.localIP().toString().c_str());

      displayStatus("Syncing time...", COL_WARN);
      HTTPClient http;
      String timeUrl = "http://" + String(PI_HOST) + ":" + String(PI_PORT) + "/time";
      http.begin(timeUrl);
      http.setTimeout(3000);
      int code = http.GET();
      if (code == 200) {
        String body = http.getString();
        int commaIdx = body.indexOf(',');
        unsigned long piUnix;
        long piOffset = 0;
        if (commaIdx != -1) {
          piUnix   = body.substring(0, commaIdx).toInt();
          piOffset = body.substring(commaIdx + 1).toInt();
        } else {
          piUnix = body.toInt();
        }
        if (piUnix > 1700000000UL) {
          unsigned long uptimeSecs = (millis() - startTime) / 1000;
          browserTimeOffset = piUnix - uptimeSecs;
          localTimeOffset   = piOffset;
          timeSynced        = true;
          saveTimeToSD(piUnix);
          Serial.printf("Time synced from Pi: %lu (offset: %ld)\n", piUnix, piOffset);
          displayStatus("Time synced!", COL_GOOD);
          delay(1000);
        }
      }
      http.end();
      break; // Exit the retry loop early because we are connected!
    } else {
      if (attempt < maxRetries) {
        delay(2000); // Wait 2 seconds before trying again
      }
    }
  }
  Serial.println("\nPi network unavailable — local mode only");
  displayStatus("Pi connect failed, trying local", COL_GOOD);

  // 2. CRITICAL FIX: Kill the Station mode entirely if Pi connection failed
  if (!piConnected) {
    Serial.println("Reverting to AP Mode only to stabilize hotspot.");
    WiFi.disconnect(true); // Stop the station from scanning
    WiFi.mode(WIFI_AP);    // Tell the radio to ONLY act as an Access Point
  }

  server.on("/",          handleRoot);
  server.on("/api/data",  handleApiData);
  server.on("/api/settings", handleApiSettings);
  server.on("/cal/400",   handleCal400);
  server.on("/cal/0",     handleCal0);
  server.on("/settime",   handleSetTime);
  server.on("/api/logs", HTTP_GET, [](){
    server.send(200, "application/json", getLogFileListJSON());
  });

  server.on("/api/settings", HTTP_GET, [](){
    String json = "{";
    json += "\"read_interval\":" + String(READ_INTERVAL) + ",";
    json += "\"warmup_ms\":"     + String(WARMUP_MS)     + ",";
    json += "\"co2_good\":"      + String(CO2_GOOD) + ",";
    json += "\"device_name\":\"" + String(deviceName) + "\",";
    json += "\"ap_ssid\":\""     + String(apSSID)    + "\",";
    json += "\"ap_pass\":\""     + String(apPass)    + "\"";
    json += ",\"graph_window\":" + String(GRAPH_WINDOW_S);
    json += ",\"flag_index\":" + String(flagIndex);
    json += "}";
    server.send(200, "application/json", json);
  });

  server.on("/api/settings", HTTP_POST, [](){
    if (server.hasArg("read_interval"))
      READ_INTERVAL = server.arg("read_interval").toInt();
    if (server.hasArg("warmup_ms"))
      WARMUP_MS = server.arg("warmup_ms").toInt();
    if (server.hasArg("co2_good"))
      CO2_GOOD = server.arg("co2_good").toInt();
    if (server.hasArg("device_name"))
      server.arg("device_name").toCharArray(deviceName, sizeof(deviceName));
    if (server.hasArg("ap_ssid"))
      server.arg("ap_ssid").toCharArray(apSSID, sizeof(apSSID));
    if (server.hasArg("ap_pass"))
      server.arg("ap_pass").toCharArray(apPass, sizeof(apPass));
    if (server.hasArg("graph_window"))
      GRAPH_WINDOW_S = server.arg("graph_window").toInt();
    if (server.hasArg("flag_index")) {
      flagIndex = server.arg("flag_index").toInt();
      
      // Instantly redraw the screen to show the new flag
      drawStaticUI();
      if (graphMode == 0) displayReading(lastCO2);
      else if (graphMode == 1) drawGraphView();
      else if (graphMode == 2) drawO2GraphView();
    }
    saveSettings();
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  });

  server.on("/api/callogs", HTTP_GET, [](){
    if (!sdAvailable || !SD_MMC.exists(CAL_LOG_FILE)) {
      server.send(200, "application/json", "[]");
      return;
    }
    File f = SD_MMC.open(CAL_LOG_FILE, FILE_READ);
    if (!f) { server.send(200, "application/json", "[]"); return; }

    String json = "[";
    bool first = true;
    bool header = true;
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (header) { header = false; continue; }
      if (line.length() < 2) continue;

      // Parse: datetime,unix,cal_type,co2_ppm
      int c1 = line.indexOf(',');
      int c2 = line.indexOf(',', c1+1);
      int c3 = line.indexOf(',', c2+1);
      if (c1<0 || c2<0 || c3<0) continue;

      String dt      = line.substring(0, c1);
      String calType = line.substring(c2+1, c3);
      String co2     = line.substring(c3+1);

      if (!first) json += ",";
      first = false;
      json += "{\"dt\":\"" + dt + "\",\"type\":\"" + calType + "\",\"co2\":" + co2 + "}";
    }
    json += "]";
    f.close();
    server.send(200, "application/json", json);
  });

  server.on("/download", HTTP_GET, [](){
    if (!server.hasArg("file")) {
      server.send(400, "text/plain", "Missing file");
      return;
    }
    String filename = server.arg("file");
    if (!filename.startsWith("/")) filename = "/" + filename;
    File f = SD_MMC.open(filename);
    if (!f) {
      server.send(404, "text/plain", "File not found");
      return;
    }
    String shortName = filename.substring(filename.lastIndexOf('/') + 1);
    server.sendHeader("Content-Disposition", "attachment; filename=" + shortName);
    server.streamFile(f, "text/csv");
    f.close();
  });

  server.on("/delete", HTTP_GET, [](){
    if (!server.hasArg("file")) {
      server.send(400,"text/plain","missing file");
      return;
    }
    String f = server.arg("file");
    if (SD_MMC.remove(f)) {
      server.send(200,"text/plain","deleted");
    } else {
      server.send(500,"text/plain","delete failed");
    }
  });

  server.begin();
  Serial.println("Web server started");

  displayStatus("192.168.4.1 ready", COL_GOOD);
  delay(1000);

  Wire.begin(Touch_SDA, Touch_SCL);
  delay(500);  // let I2C bus settle before probing
  bsp_touch_init(&Wire, Touch_RST, Touch_INT, 1, SCREEN_W, SCREEN_H);

  displayStatus("Init O2 sensor...", COL_WARN);
  for (int i = 0; i < 5; i++) {
    if (oxygen.begin(O2_I2C_ADDRESS)) {
      o2Available = true;
      Serial.println("O2 sensor found");
      break;
    }
    Serial.printf("O2 not found, attempt %d/5\n", i + 1);
    delay(500);
  }
  if (!o2Available) Serial.println("O2 sensor not available — continuing without");

  startTime = millis();

  K30Serial.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  delay(500);
  while (K30Serial.available()) K30Serial.read();
  K30Serial.write(ABC_OFF, 8);
  delay(300);
  while (K30Serial.available()) K30Serial.read();
  Serial.println("ABC disabled");

  if (timeSynced) {
    // Pi gave us time — proceed straight to warmup
    for (int i = WARMUP_MS / 1000; i > 0; i--) {
      server.handleClient();
      displayWarmup(i);
      delay(1000);
    }
    drawStaticUI();
    displayStatus("Ready!", COL_GOOD);
  }
  lastReadTime = 0;
}

void handleApiSettings() {
  if (server.method() == HTTP_POST) {
    if (server.hasArg("read_interval")) READ_INTERVAL  = server.arg("read_interval").toInt();
    if (server.hasArg("graph_window"))  GRAPH_WINDOW_S = server.arg("graph_window").toInt();
    if (server.hasArg("warmup_ms"))     WARMUP_MS      = server.arg("warmup_ms").toInt();
    if (server.hasArg("co2_good"))      CO2_GOOD       = server.arg("co2_good").toInt();
    if (server.hasArg("device_name")) {
      server.arg("device_name").toCharArray(deviceName, sizeof(deviceName));
      strncpy(sensorID, deviceName, sizeof(sensorID));
    }
    if (server.hasArg("sensor_id")) {
      server.arg("sensor_id").toCharArray(sensorID, sizeof(sensorID));
      strncpy(deviceName, sensorID, sizeof(deviceName));
    }
    if (server.hasArg("ap_ssid"))    server.arg("ap_ssid").toCharArray(apSSID,    sizeof(apSSID));
    if (server.hasArg("ap_pass"))    server.arg("ap_pass").toCharArray(apPass,    sizeof(apPass));
    if (server.hasArg("location"))   server.arg("location").toCharArray(location, sizeof(location));
    if (server.hasArg("flag_index")) {
      flagIndex = server.arg("flag_index").toInt();
      drawStaticUI();
      if (graphMode == 0) displayReading(lastCO2);
      else if (graphMode == 1) drawGraphView();
      else if (graphMode == 2) drawO2GraphView();
    }
    saveSettings();
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  } else {
    String json = "{";
    json += "\"read_interval\":"  + String(READ_INTERVAL)  + ",";
    json += "\"graph_window\":"   + String(GRAPH_WINDOW_S) + ",";
    json += "\"warmup_ms\":"      + String(WARMUP_MS)      + ",";
    json += "\"co2_good\":"       + String(CO2_GOOD)       + ",";
    json += "\"flag_index\":"     + String(flagIndex)      + ",";
    json += "\"device_name\":\"" + String(deviceName)      + "\",";
    json += "\"ap_ssid\":\""     + String(apSSID)          + "\",";
    json += "\"ap_pass\":\""     + String(apPass)          + "\",";
    json += "\"sensor_id\":\""   + String(sensorID)        + "\",";
    json += "\"location\":\""    + String(location)        + "\",";
    json += "\"time_synced\":"   + String(timeSynced ? "true" : "false");
    json += "}";
    server.send(200, "application/json", json);
  }
}

void pushReadingToPi(int co2) {
  if (WiFi.status() != WL_CONNECTED) {
    piConnected = false;
    return;
  }

  HTTPClient http;
  String url = "http://" + String(PI_HOST) + ":" + String(PI_PORT) + "/sensor-data";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(3000);

  unsigned long uptimeSecs = (millis() - startTime) / 1000;
  unsigned long unixNow = (browserTimeOffset > 0) ? browserTimeOffset + uptimeSecs : 0;

  String payload = "{";
  payload += "\"sensor_id\":\"" + String(sensorID) + "\",";
  payload += "\"location\":\""  + String(location)  + "\",";
  payload += "\"co2\":"         + String(co2)        + ",";
  payload += "\"uptime\":"      + String(uptimeSecs);
  if (o2Available) {
    payload += ",\"o2\":"       + String(lastO2, 2);
  }
  if (unixNow > 0) {
    payload += ",\"timestamp\":" + String(unixNow);
  }
  payload += "}";

  int responseCode = http.POST(payload);

  if (responseCode == 200) {
    if (!piConnected) {
      // Just reconnected — allow status display to refresh
      warningDrawn = false;
    }
    piConnected = true;
    Serial.println("Pi: data sent OK");
    // Mid-session time sync if not yet synced
    if (!timeSynced) {
      HTTPClient httpTime;
      String timeUrl = "http://" + String(PI_HOST) + ":" + String(PI_PORT) + "/time";
      httpTime.begin(timeUrl);
      httpTime.setTimeout(2000);
      if (httpTime.GET() == 200) {
        String body = httpTime.getString();
        int commaIdx = body.indexOf(',');
        unsigned long piUnix;
        long piOffset = 0;
        if (commaIdx != -1) {
          piUnix   = body.substring(0, commaIdx).toInt();
          piOffset = body.substring(commaIdx + 1).toInt();
        } else {
          piUnix = body.toInt();
        }
        if (piUnix > 1700000000UL) {
          unsigned long ups = (millis() - startTime) / 1000;
          browserTimeOffset = piUnix - ups;
          localTimeOffset   = piOffset;
          timeSynced        = true;
          saveTimeToSD(piUnix);
          Serial.printf("Time synced from Pi mid-session: %lu\n", piUnix);
        }
      }
      httpTime.end();
    }
  } else {
    piConnected = false;
    Serial.printf("Pi: send failed (HTTP %d)\n", responseCode);
    Serial.println("Payload was: " + payload); // Helps debug 400 errors!
  }
  http.end();
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  server.handleClient();
// --- THE GATEKEEPER ---
  if (!timeSynced) {
    drawSyncWarning();
  }

  // Serial UI test commands
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if      (cmd == "ui")            drawStaticUI();
    else if (cmd == "warm")          displayWarmup(15);
    else if (cmd.startsWith("co2:")) displayReading(cmd.substring(4).toInt());
    else if (cmd.startsWith("status:"))  displayStatus(cmd.substring(7).c_str(), COL_GOOD);
    else if (cmd.startsWith("statusw:")) displayStatus(cmd.substring(8).c_str(), COL_WARN);
    else if (cmd.startsWith("statusb:")) displayStatus(cmd.substring(8).c_str(), COL_BAD);
  }

  // Touch
  bsp_touch_read();
  touch_data_t touch_data;
  if (bsp_touch_get_coordinates(&touch_data) && !calibrating) {
    unsigned long now = millis();

    // Only process touch if the debounce time has passed to prevent double-skips
    if (now - lastTouchTime > TOUCH_DEBOUNCE_MS) {
      lastTouchTime = now;

      if (graphMode != 0) {
        // We are on a graph. Cycle to the next view.
        graphMode = (graphMode + 1) % 3;
        if (graphMode == 0) {
          drawStaticUI();
          displayReading(lastCO2);
        } else if (graphMode == 1) {
          if (timeSynced) {
            drawGraphView();
          } else {
            graphMode = 0;
            drawStaticUI();
            displayReading(lastCO2);
          }
        } else {
          if (timeSynced) {
            drawO2GraphView();
          } else {
            graphMode = 0;
            drawStaticUI();
            displayReading(lastCO2);
          }
        }
      } else {
        // We are on the main reading view.
        if (timeSynced) {
          graphMode = 1;
          drawGraphView();
        } else {
          drawSyncWarning();
        }
      }
    }
  }

  // K30 read
  if (!calibrating && millis() - lastReadTime >= READ_INTERVAL) {
    lastReadTime = millis();
    int co2 = readK30();
    unsigned long uptimeSecs = (millis() - startTime) / 1000;
    if (o2Available) {
      lastO2 = oxygen.getOxygenData(10);
      Serial.printf("O2: %.2f %%\n", lastO2);
      o2History[o2HistoryIndex]  = lastO2;
      o2TimeHistory[o2HistoryIndex] = uptimeSecs;
      o2HistoryIndex = (o2HistoryIndex + 1) % HISTORY_SIZE;
      if (o2HistoryCount < HISTORY_SIZE) o2HistoryCount++;
    }
    if (co2 != -1) {
      lastCO2 = co2;
      consecutiveFailures = 0;
      storeReading(co2, lastO2, uptimeSecs);
      pushReadingToPi(co2);
      Serial.print("CO2: "); Serial.print(co2); Serial.println(" ppm");

      if (graphMode == 1) {
        drawGraphView();
      } else {
        displayReading(co2);
      }
    } else {
      consecutiveFailures++;
      Serial.println("CO2 read failed");
      if (consecutiveFailures >= 3) {
        displayStatus("CO2 disconnected", COL_WARN);
      }
    }
    // NEW: Calculate exactly how long those reads took
    readDuration = millis() - lastReadTime; 
  }

  // Progress bar — always animates in reading view
  drawProgressBar();

  static unsigned long lastDotDraw = 0;
  static bool lastPiState = false;
  if (graphMode == 0 && (millis() - lastDotDraw >= 1000 || lastPiState != piConnected)) {
    lastDotDraw  = millis();
    lastPiState  = piConnected;
    gfx->fillCircle(SCREEN_W - 14, 14, 4, piConnected ? COL_GOOD : COL_BAD);
  }

  delay(10);
}
