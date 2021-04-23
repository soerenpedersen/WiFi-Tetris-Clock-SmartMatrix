/*******************************************************************
    Tetris clock that fetches its time Using the EzTimeLibrary
    For use with rorosaurus' esp32-hub75-driver PCB: https://github.com/rorosaurus/esp32-hub75-driver
 *                                                                 *
    Originally written by Brian Lough
    YouTube: https://www.youtube.com/brianlough
    Tindie: https://www.tindie.com/stores/brianlough/
    Twitter: https://twitter.com/witnessmenow

    Modified for SmartMatrix by Søren Pedersen
 *******************************************************************/
#include <WiFi.h>
#include <TetrisMatrixDraw.h>
#include <ezTime.h>
//SmartMatrix - DO NOT change the order of imports!
#define USE_ADAFRUIT_GFX_LAYERS
#include <MatrixHardware_ESP32_V0.h>
#include <SmartMatrix.h>

// ---- Stuff to configure ----
// Initialize Wifi connection to the router
char ssid[] = "SSID";     // your network SSID (name)
char password[] = "PASSWORD"; // your network key

//Brightness of display
#define BRIGHTNESS 255

// Set a timezone using the following list
// https://en.wikipedia.org/wiki/List_of_tz_database_time_zones
#define MYTIMEZONE "Europe/Copenhagen"

// Sets whether the clock should be 12 hour format or not.
bool twelveHourFormat = false;

// If this is set to false, the number will only change if the value behind it changes
// e.g. the digit representing the least significant minute will be replaced every minute,
// but the most significant number will only be replaced every 10 minutes.
// When true, all digits will be replaced every minute.
bool forceRefresh = true;
// -----------------------------

//SmartMatrix display configuration
#define GPIOPINOUT ESP32_FORUM_PINOUT   // For rorosaurus' excellent driver board: https://github.com/rorosaurus/esp32-hub75-driver
#define COLOR_DEPTH 24                  // Choose the color depth used for storing pixels in the layers: 24 or 48 (24 is good for most sketches - If the sketch uses type `rgb24` directly, COLOR_DEPTH must be 24)
const uint16_t kMatrixWidth = 64;       // Set to the width of your display, must be a multiple of 8
const uint16_t kMatrixHeight = 32;      // Set to the height of your display
const uint8_t kRefreshDepth = 24;       // Tradeoff of color quality vs refresh rate, max brightness, and RAM usage.  36 is typically good, drop down to 24 if you need to.  On Teensy, multiples of 3, up to 48: 3, 6, 9, 12, 15, 18, 21, 24, 27, 30, 33, 36, 39, 42, 45, 48.  On ESP32: 24, 36, 48
const uint8_t kDmaBufferRows = 4;       // known working: 2-4, use 2 to save RAM, more to keep from dropping frames and automatically lowering refresh rate.  (This isn't used on ESP32, leave as default)
const uint8_t kPanelType = SM_PANELTYPE_HUB75_32ROW_MOD16SCAN;   // Choose the configuration that matches your panels.  See more details in MatrixCommonHub75.h and the docs: https://github.com/pixelmatix/SmartMatrix/wiki
const uint32_t kMatrixOptions = (SM_HUB75_OPTIONS_NONE);        // see docs for options: https://github.com/pixelmatix/SmartMatrix/wiki

SMARTMATRIX_ALLOCATE_BUFFERS(matrix, kMatrixWidth, kMatrixHeight, kRefreshDepth, kDmaBufferRows, kPanelType, kMatrixOptions);
const uint8_t kBackgroundLayerOptions = (SM_BACKGROUND_OPTIONS_NONE);
SMARTMATRIX_ALLOCATE_BACKGROUND_LAYER(backgroundLayer, kMatrixWidth, kMatrixHeight, COLOR_DEPTH, kBackgroundLayerOptions);

portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
hw_timer_t * timer = NULL;
hw_timer_t * animationTimer = NULL;

TetrisMatrixDraw tetris(backgroundLayer); // Main clock
TetrisMatrixDraw tetris2(backgroundLayer); // The "M" of AM/PM
TetrisMatrixDraw tetris3(backgroundLayer); // The "P" or "A" of AM/PM

Timezone myTZ;
unsigned long oneSecondLoopDue = 0;

bool showColon = true;
volatile bool finishedAnimating = false;
bool displayIntro = true;

String lastDisplayedTime = "";
String lastDisplayedAmPm = "";

// This method is for controlling the tetris library draw calls
void animationHandler()
{
  // Not clearing the display and redrawing it when you
  // dont need to improves how the refresh rate appears
  if (!finishedAnimating  ) {
    backgroundLayer.fillScreen(0);

    if (displayIntro) {
      finishedAnimating = tetris.drawText(1, 21);
    }
    else {
      if (twelveHourFormat) {
        // Place holders for checking are any of the tetris objects
        // currently still animating.
        bool tetris1Done = false;
        bool tetris2Done = false;
        bool tetris3Done = false;

        tetris1Done = tetris.drawNumbers(-6, 26, showColon);
        tetris2Done = tetris2.drawText(56, 25);

        // Only draw the top letter once the bottom letter is finished.
        if (tetris2Done) {
          tetris3Done = tetris3.drawText(56, 15);
        }
        finishedAnimating = tetris1Done && tetris2Done && tetris3Done;
      }

      else {
        finishedAnimating = tetris.drawNumbers(2, 26, showColon);
      }
    }
    backgroundLayer.swapBuffers(false);
  }
}

void drawIntro(int x = 0, int y = 0)
{
  tetris.drawChar("P", x, y, tetris.tetrisCYAN);
  tetris.drawChar("o", x + 5, y, tetris.tetrisMAGENTA);
  tetris.drawChar("w", x + 11, y, tetris.tetrisYELLOW);
  tetris.drawChar("e", x + 17, y, tetris.tetrisGREEN);
  tetris.drawChar("r", x + 22, y, tetris.tetrisBLUE);
  tetris.drawChar("e", x + 27, y, tetris.tetrisRED);
  tetris.drawChar("d", x + 32, y, tetris.tetrisWHITE);
  tetris.drawChar(" ", x + 37, y, tetris.tetrisMAGENTA);
  tetris.drawChar("b", x + 42, y, tetris.tetrisYELLOW);
  tetris.drawChar("y", x + 47, y, tetris.tetrisGREEN);
}

void drawConnecting(int x = 0, int y = 0)
{
  tetris.drawChar("C", x, y, tetris.tetrisCYAN);
  tetris.drawChar("o", x + 5, y, tetris.tetrisMAGENTA);
  tetris.drawChar("n", x + 11, y, tetris.tetrisYELLOW);
  tetris.drawChar("n", x + 17, y, tetris.tetrisGREEN);
  tetris.drawChar("e", x + 22, y, tetris.tetrisBLUE);
  tetris.drawChar("c", x + 27, y, tetris.tetrisRED);
  tetris.drawChar("t", x + 32, y, tetris.tetrisWHITE);
  tetris.drawChar("i", x + 37, y, tetris.tetrisMAGENTA);
  tetris.drawChar("n", x + 42, y, tetris.tetrisYELLOW);
  tetris.drawChar("g", x + 47, y, tetris.tetrisGREEN);
}

void setup() {
  Serial.begin(115200);

  // Attempt to connect to Wifi network:
  Serial.print("Connecting Wifi: ");
  Serial.println(ssid);

  // Set WiFi to station mode and disconnect from an AP if it was Previously
  // connected
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Do not set up display before WiFi connection
  // as it will crash! (Not tested with SmartMatrix)

  // Intialise display library
  matrix.addLayer(&backgroundLayer);
  matrix.begin();
  matrix.setBrightness(BRIGHTNESS);
  backgroundLayer.fillScreen(0);
  backgroundLayer.swapBuffers(false);

  // "connecting"
  drawConnecting(5, 10);

  // Setup EZ Time
  setDebug(INFO);
  waitForSync();

  Serial.println();
  Serial.println("UTC:             " + UTC.dateTime());

  myTZ.setLocation(F(MYTIMEZONE));
  Serial.print(F("Time in your set timezone:         "));
  Serial.println(myTZ.dateTime());

  // "Powered By"
  backgroundLayer.fillScreen(0);
  backgroundLayer.swapBuffers(false);
  drawIntro(6, 12);
  delay(2000);

  // Start the Animation Timer
  tetris.setText("ESPRESSIF");
  animationTimer = timerBegin(1, 80, true);
  timerAttachInterrupt(animationTimer, &animationHandler, true);
  timerAlarmWrite(animationTimer, 100000, true);
  timerAlarmEnable(animationTimer);

  // Wait for the animation to finish
  while (!finishedAnimating)
  {
    delay(10); //waiting for intro to finish
  }
  delay(2000);
  finishedAnimating = false;
  displayIntro = false;
  tetris.scale = 2;
}

void setMatrixTime() {
  String timeString = "";
  String AmPmString = "";
  if (twelveHourFormat) {
    // Get the time in format "1:15" or 11:15 (12 hour, no leading 0)
    // Check the EZTime Github page for info on
    // time formatting
    timeString = myTZ.dateTime("g:i");

    //If the length is only 4, pad it with
    // a space at the beginning
    if (timeString.length() == 4) {
      timeString = " " + timeString;
    }

    //Get if its "AM" or "PM"
    AmPmString = myTZ.dateTime("A");
    if (lastDisplayedAmPm != AmPmString) {
      Serial.println(AmPmString);
      lastDisplayedAmPm = AmPmString;
      // Second character is always "M"
      // so need to parse it out
      tetris2.setText("M", forceRefresh);

      // Parse out first letter of String
      tetris3.setText(AmPmString.substring(0, 1), forceRefresh);
    }
  } else {
    // Get time in format "01:15" or "22:15"(24 hour with leading 0)
    timeString = myTZ.dateTime("H:i");
  }

  // Only update Time if its different
  if (lastDisplayedTime != timeString) {
    Serial.println(timeString);
    lastDisplayedTime = timeString;
    tetris.setTime(timeString, forceRefresh);

    // Must set this to false so animation knows
    // to start again
    finishedAnimating = false;
  }
}

void handleColonAfterAnimation() {

  // It will draw the colon every time, but when the colour is black it
  // should look like its clearing it.
  uint16_t colour =  showColon ? tetris.tetrisWHITE : tetris.tetrisBLACK;
  // The x position that you draw the tetris animation object
  int x = twelveHourFormat ? -6 : 2;
  // The y position adjusted for where the blocks will fall from
  // (this could be better!)
  int y = 26 - (TETRIS_Y_DROP_DEFAULT * tetris.scale);
  tetris.drawColon(x, y, colour);
  backgroundLayer.swapBuffers(false);
}

void loop() {
  unsigned long now = millis();
  if (now > oneSecondLoopDue) {
    // We can call this often, but it will only
    // update when it needs to
    setMatrixTime();
    showColon = !showColon;

    // To reduce flicker on the screen we stop clearing the screen
    // when the animation is finished, but we still need the colon to
    // to blink
    if (finishedAnimating) {
      handleColonAfterAnimation();
    }
    oneSecondLoopDue = now + 1000;
  }
}
