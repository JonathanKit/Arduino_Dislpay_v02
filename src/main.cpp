// ----------------------------
// Standard Libraries
// ----------------------------

#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <Wire.h>
#include <SPI.h>

// ----------------------------
// Additional Libraries - each one of these will need to be installed.
// ----------------------------

#include <ArduinoSpotify.h>
// Library for connecting to the Spotify API
// Install from Github
// https://github.com/witnessmenow/arduino-spotify-api

#include <ArduinoJson.h>
// Library used for parsing Json from the API responses
// Search for "Arduino Json" in the Arduino Library manager
// https://github.com/bblanchon/ArduinoJson

#include <FastLED.h>
// Just needed for EVERY_N_MILLISECONDS :/. I don't find a better solution to call the functions
// every n ms. this can be a TODO

// ----------MATRIX-----------------
// Creates a second buffer for backround drawing (doubles the required RAM)
//#define PxMATRIX_double_buffer true
#include <PxMatrix.h>

// uncomment this define for debug messages
#define DEBUG_APP = 0

// Pins for LED MATRIX

#define P_LAT 22
#define P_A 19
#define P_B 23
#define P_C 18
#define P_D 5
#define P_E 15
#define P_OE 16

// power supply configuration
// pin for the power supply standby flag
const int outputPinPowerSupply = 32;

hw_timer_t * timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

#define MATRIX_WIDTH 64
#define MATRIX_HEIGHT 64

PxMATRIX display(64,64,P_LAT, P_OE,P_A,P_B,P_C,P_D,P_E);

// This defines the 'on' time of the display is us. The larger this number,
// the brighter the display. If too large the ESP will crash
uint8_t display_draw_time=10; //my default is 10; 30-60 is usually fine
int timer_alarm = 2000;  //default is 2000

void IRAM_ATTR display_updater(){
  // Increment the counter and set the time of ISR
  portENTER_CRITICAL_ISR(&timerMux);
  display.display(display_draw_time);
  portEXIT_CRITICAL_ISR(&timerMux);
}

void display_update_enable(bool is_enable)
{
  if (is_enable)
  {
    timer = timerBegin(0, 80, true);
    timerAttachInterrupt(timer, &display_updater, true);
    timerAlarmWrite(timer, 2000, true);
    timerAlarmEnable(timer);
  }
  else
  {
    timerDetachInterrupt(timer);
    timerAlarmDisable(timer);
  }
}

//-------------APP SPECIFIC------------------------------
const int CYCLIC_PRINT_MS = 50;
const int TEXT_COLOR = 0xFFFF;
const int BACKGROUND_COLOR = 0x0000;
const int TRACK_PROCESS_COLOR = 0x0333;
const int TRACK_PROCESS_BACKGROUND_COLOR = 0x8000;
const int HIGHT_SONG_TITLE = 1;
const int HIGHT_SONG_AUTHOR = 11;


// Audio Features
const short MINIMUM_TEMPO_TO_VISUALIZE = 50;
const short MAXIMUM_TEMPO_TO_VISUALIZE = 200;
const short NUMBER_FEATURES_TO_DRAW = 6;
const short START_LINE_AUDIO_FEATURES = 21;
const short BAR_WIDTH = 7;
const int BAR_FOREGROUND_COLOR = 0x07E0;
const int BAR_BACKGROUND_COLOR = 0x0000;


//-------------SPOTIFY---------------

// If you want to enable some extra debugging
// uncomment the "#define SPOTIFY_DEBUG" in ArduinoSpotify.h


#include <ArduinoSpotifyCert.h>

const int wifiTimeoutMs = 5000;
const char ssid[] = "---";                                 // your network SSID (name)
const char password[] = "---";                   // your network password
const char clientId[] = "---";       // Your client ID of your spotify APP
const char clientSecret[] = "---";   // Your client Secret of your spotify APP (Do Not share this!)

// Country code, including this is advisable
#define SPOTIFY_MARKET "DE"
#define SPOTIFY_REFRESH_TOKEN "---"

WiFiClientSecure client;
ArduinoSpotify spotify(client, clientId, clientSecret, SPOTIFY_REFRESH_TOKEN);

CurrentlyPlaying currentlyPlaying;
CurrentlyPlaying currentlyPlayingErrorCheck;
AudioFeatures audioFeatures;

class ScrollText
{
  public:
    ScrollText(uint8_t ypos_in, const char* text_in);

    void setText(const char* text_in);
    void moveOneFrame(const char* text);

  private:
    char text[80];
    uint8_t ypos;
    uint16_t text_length;

    int xpos_scrolltext = 0;
    int timer_delay_scroll_text_ms = 0;
    // describes the delay when starting and ending to scroll
    const short scrolling_delay_ms = 2000;
    const int text_color = TEXT_COLOR;
    const int background_color = BACKGROUND_COLOR;

    short state = 1;
};

ScrollText::ScrollText(uint8_t ypos_in, const char* text_in):
 ypos(ypos_in) {
   strcpy(text, text_in);
   text_length = strlen(text_in);
}

void ScrollText::setText(const char* text_in) {
  if (strcoll(text_in, text) != 0) {
    xpos_scrolltext = 0;
    timer_delay_scroll_text_ms = 0;
    strcpy(text, text_in);
    text_length = strlen(text);
  }
}

void ScrollText::moveOneFrame(const char* text) {
  display.setTextWrap(false);  // we don't wrap text so it scrolls nicely
  display.setTextColor(text_color, background_color); 
  setText(text);
  yield();

  switch(state) {
    //waiting before moving the text
    case 1:
      timer_delay_scroll_text_ms += CYCLIC_PRINT_MS;
      if (timer_delay_scroll_text_ms > scrolling_delay_ms) {
        timer_delay_scroll_text_ms = 0;
        state = 2;
      }
      break;
    // moving the text
    case 2:
      xpos_scrolltext--;
      if(text_length*6 + xpos_scrolltext <= MATRIX_WIDTH) {
        state = 3;
      }
      break;
    //waiting before start moving the text from the beginning again
    case 3:
      timer_delay_scroll_text_ms += CYCLIC_PRINT_MS;
      if (timer_delay_scroll_text_ms > scrolling_delay_ms) {
        timer_delay_scroll_text_ms = 0;
        xpos_scrolltext = 0;
        state = 1;
      }
  }
  display.setCursor(xpos_scrolltext, ypos);
  display.println(text);
}

// pre declaration
void slowUpdate();

void setPowerSupplyPower(bool power) {
  if (power) {
    Serial.println("Set power supply power ON");
    // set LOW to pin, because it is set to ground to the enable power
    digitalWrite(outputPinPowerSupply, LOW);
  } else {
    Serial.println("Set power supply power OFF");

    digitalWrite(outputPinPowerSupply, HIGH);
  }
  
  
}

void connectToWifiAndSpotifyAuth() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.setAutoReconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    
    Serial.println("");
    // Wait for connection
    int counterMs = 0;
    while (WiFi.status() != WL_CONNECTED)
    {
        counterMs += 500;
        delay(500);
        Serial.print(".");
        if (counterMs >= wifiTimeoutMs) {
          Serial.print(" Failed to connect. Retry");
          connectToWifiAndSpotifyAuth();
        }
    }
  }
  
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  client.setCACert(spotify_server_cert);

  Serial.println("Refreshing Access Tokens");
  if (!spotify.refreshAccessToken())
  {
      Serial.println("Failed to get access tokens");
  }else{
    Serial.println("Spotify access working");
  }
}

void updateSpotifyInfo() {
  unsigned long now = millis();
  currentlyPlaying = spotify.getCurrentlyPlaying(SPOTIFY_MARKET);
  Serial.print("Current song: ");
  if (currentlyPlaying.error) {
    Serial.println("Error, no song currently played by Spotify");
    display.clearDisplay();


    //WiFi.reconnect();

  }else {
    Serial.println(currentlyPlaying.trackName);
    #ifdef DEBUG_APP
      Serial.print("Current song(shortened track): ");
      Serial.println(currentlyPlaying.shortTrackName);
    #endif
  }
  #ifdef DEBUG_APP
    Serial.print("Duration of Spotify API call in ms: ");
    Serial.println(millis() - now);
  #endif
}


// print the configuration screen, when something went wrong or when being in setup
void printStartScreen() {
  display.clearDisplay();
  display.setCursor(0,0);
  display.print("SpotifyDisp");
  display.setCursor(0,10);
  display.print("WiFi yes");
  display.setCursor(0,18);
  display.print("Spotify no");
}

//_----------------Setup--------------
void setup() {
  Serial.begin(9600);
  Serial.print("Start Setup");

  // pins configuration
  pinMode(outputPinPowerSupply, OUTPUT);
  setPowerSupplyPower(true);

  // ----------Display----------------------------------------------------
  Serial.println("Setting up Matrix visuals");
  // Define your display layout here, e.g. 1/8 step, and optional SPI pins begin(row_pattern, CLK, MOSI, MISO, SS)
  display.begin(32);
  // Helps to reduce display update latency on larger displays
  display.setFastUpdate(false);
  // Rotate display
  //display.setRotate(true);
  // Flip display
  //display.setFlip(true);
  // Set the brightness of the panels (default is 255)
  //display.setBrightness(50);
  delay(100);

  connectToWifiAndSpotifyAuth();

  // Display printing is only possible after WiFi is conencted
  printStartScreen();
  display_update_enable(true);

  slowUpdate();
  display.clearDisplay();
  Serial.println("Finished Setup");
}

//-----------------Global variables-------------
ScrollText scrolltext_title = ScrollText(HIGHT_SONG_TITLE, "Title");
ScrollText scrolltext_artist = ScrollText(HIGHT_SONG_AUTHOR, "Artist");


//-----------------FUNCTIONS--------------------


void printCurrentlyPlayingToSerial(CurrentlyPlaying currentlyPlaying)
{
    if (!currentlyPlaying.error)
    {
        Serial.println("--------- Currently Playing ---------");

        Serial.print("Is Playing: ");
        if (currentlyPlaying.isPlaying)
        {
            Serial.println("Yes");
        }
        else
        {
            Serial.println("No");
        }

        Serial.print("Track: ");
        Serial.println(currentlyPlaying.trackName);
        Serial.print("Track URI: ");
        Serial.println(currentlyPlaying.trackUri);
        Serial.println();

        Serial.print("Artist: ");
        Serial.println(currentlyPlaying.firstArtistName);
        Serial.print("Artist URI: ");
        Serial.println(currentlyPlaying.firstArtistUri);
        Serial.println();

        Serial.print("Album: ");
        Serial.println(currentlyPlaying.albumName);
        Serial.print("Album URI: ");
        Serial.println(currentlyPlaying.albumUri);
        Serial.println();

        long progress = currentlyPlaying.progressMs; // duration passed in the song
        long duration = currentlyPlaying.duraitonMs; // Length of Song
        Serial.print("Elapsed time of song (ms): ");
        Serial.print(progress);
        Serial.print(" of ");
        Serial.println(duration);
        Serial.println();
        float precentage;
        if (duration <= 0) {
          precentage = 0;
        } else {
          precentage = ((float)progress / (float)duration) * 100;
        }
        int clampedPrecentage = (int)precentage;
        Serial.print("<");
        for (int j = 0; j < 50; j++)
        {
            if (clampedPrecentage >= (j * 2))
            {
                Serial.print("=");
            }
            else
            {
                Serial.print("-");
            }
        }
        Serial.println(">");
        Serial.println();
        Serial.println("------------------------");
    }
}


void printSongProcess() {
  
    //Show song progress
    long progress = currentlyPlaying.progressMs; // duration passed in the song
    long duration = currentlyPlaying.duraitonMs; // Length of Song
    float percentage;
    if (duration <= 0) {
      percentage = 0;
    } else {
      percentage = ((float)progress / (float)duration);
    }
    float progress_float = (float)MATRIX_WIDTH * percentage;
    int clamped_progress = (int)progress_float;

    const short startingX = 0;
    const short startingY = 9;

    display.drawFastHLine(startingX, startingY, clamped_progress, TRACK_PROCESS_COLOR);
    display.drawFastHLine(clamped_progress, startingY, MATRIX_WIDTH - clamped_progress, TRACK_PROCESS_BACKGROUND_COLOR);
}

// print a char to the display with clearing the row
void printToDisplay(char* char_to_print) {
  // dont wrap so we only clear the current line
  display.setTextWrap(false);
  if (sizeof(char_to_print) > 20) {
    Serial.println("Eror: This print to display is longer than 20 characters");
  }
  char char_to_print_plus_spaces[50];
  strncpy(char_to_print_plus_spaces, char_to_print, sizeof(char_to_print_plus_spaces));
  strncat(char_to_print_plus_spaces, "            ", (sizeof(char_to_print_plus_spaces)) );
  display.setTextColor(TEXT_COLOR, BACKGROUND_COLOR);
  display.print(char_to_print_plus_spaces);
}

//Logic to handle different title length
//short title is just displayed
//a long title is displayed scrolling
char old_track[80];
char old_artist[80];
void printTitleAndAuthor() {
  //print title
  if (strlen(currentlyPlaying.shortTrackName) < 11) {
    // only print track if its differnet to the old track
    if (strcmp (old_track, currentlyPlaying.shortTrackName) != 0) {
      strncpy(old_track, currentlyPlaying.shortTrackName, sizeof(old_track));
      display.setCursor(0, HIGHT_SONG_TITLE);
      printToDisplay(currentlyPlaying.shortTrackName);
    }
  } else {
    strncpy(old_track, currentlyPlaying.shortTrackName, sizeof(old_track));
    scrolltext_title.moveOneFrame(currentlyPlaying.shortTrackName);
  }

  // print artist
  if (strlen(currentlyPlaying.shortFirstArtistName) < 11) {
    // only print track if its differnet to the old track
    if (strcmp (old_track, currentlyPlaying.shortFirstArtistName) != 0) {
      strncpy(old_artist, currentlyPlaying.shortFirstArtistName, sizeof(old_artist));
      display.setCursor(0,HIGHT_SONG_AUTHOR);
      printToDisplay(currentlyPlaying.shortFirstArtistName);
    }
  } else {
    strncpy(old_artist, currentlyPlaying.shortFirstArtistName, sizeof(old_artist));
    scrolltext_artist.moveOneFrame(currentlyPlaying.shortFirstArtistName);
  }
}

void printAllInfo() {
  if (!currentlyPlaying.error) {
    if(currentlyPlaying.isPlaying) {
      printTitleAndAuthor();
      printSongProcess();
    } else {
      display.setCursor(0,1);
      char error[] = "no song";
      printToDisplay(error);
    }
  }
  else {
    display.clearDisplay();
    display.setCursor(2,0);
    char error[] = "Error";
    printToDisplay(error);
  }
}

short getAudioFeatureByIndex(short index) {
  switch (index) {
    case 0:
      if (currentlyPlaying.trackPopularity <= 0) {
        return 0;
      }
      return (short)((1.0f / (100.0f / currentlyPlaying.trackPopularity)) * MATRIX_WIDTH);
    case 1:
      if (audioFeatures.tempo <= MINIMUM_TEMPO_TO_VISUALIZE) {
        return 0;
      } else {
      float percentage = 1.0f / ((MAXIMUM_TEMPO_TO_VISUALIZE - MINIMUM_TEMPO_TO_VISUALIZE) /
                         (audioFeatures.tempo - MINIMUM_TEMPO_TO_VISUALIZE));
      return (short)(percentage * MATRIX_WIDTH);
      }
    case 2:
      return (short)(audioFeatures.danceability*MATRIX_WIDTH);
    case 3:
      return (short)(audioFeatures.energy*MATRIX_WIDTH);
    case 4:
      return (short)(audioFeatures.valence*MATRIX_WIDTH);
    case 5:
      return (short)(audioFeatures.speechiness*MATRIX_WIDTH);
    case 6:
      return (short)(audioFeatures.instrumentalness*MATRIX_WIDTH);
    case 7:
      return (short)(audioFeatures.acousticness*MATRIX_WIDTH);
    case 8:
      return (short)(audioFeatures.liveness*MATRIX_WIDTH);
    default:
      Serial.println("Error: Index out of bound.Audio feature is not defined");
      return 0;
  }
}

void updateAndPrintAudioFeatures() {
  if (currentlyPlaying.error) {
    Serial.println("Error, no song currently played, so no audio features extracted");
  } else {
    audioFeatures = spotify.getAudioFeatures(SPOTIFY_MARKET, currentlyPlaying.trackId);
    for (short index = 0; index < NUMBER_FEATURES_TO_DRAW; index++) {
      short bar_danceability = getAudioFeatureByIndex(index);
      for(short wi = 0; wi < BAR_WIDTH; wi++) {
        // line index is the start line + the index of the freature times BAR_WIDTH + the current round of painting one bar
        short line_index = START_LINE_AUDIO_FEATURES + index * BAR_WIDTH + wi;
        display.drawFastHLine(0, line_index, bar_danceability, BAR_FOREGROUND_COLOR);
        display.drawFastHLine(bar_danceability, line_index,
                              MATRIX_WIDTH - bar_danceability, BAR_BACKGROUND_COLOR);
      }
    }
  }
}

void updateTime(){
  if(currentlyPlaying.error){
    return;
  }
  if (currentlyPlaying.isPlaying) {
    currentlyPlaying.progressMs += CYCLIC_PRINT_MS;
  }
}

void updateWhenSongIsOver() {
  if(currentlyPlaying.error) {
    return;
  }
  if (currentlyPlaying.isPlaying) {
    if (currentlyPlaying.progressMs >= currentlyPlaying.duraitonMs) {
      updateSpotifyInfo();
      updateAndPrintAudioFeatures();
    }
  }
}

void updatePowerSupplyPower() {
  if (currentlyPlaying.error) {
    setPowerSupplyPower(false);
  } else {
    setPowerSupplyPower(true);
  }
}

void slowUpdate() {
  updateSpotifyInfo();
  // TODO(jh) currently unused, the system is turned on via power supply switch
  // updatePowerSupplyPower();
  //updateAndPrintAudioFeatures();
}

void fastUpdate() {
  updateTime();
  updateWhenSongIsOver();
  printAllInfo();
}

void loop() {
  
  EVERY_N_MILLISECONDS(10000) {slowUpdate();}
  EVERY_N_MILLISECONDS(CYCLIC_PRINT_MS) {fastUpdate();}

}