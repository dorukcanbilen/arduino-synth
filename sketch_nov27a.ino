
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// LCD configuration
LiquidCrystal_I2C lcd(0x27, 20, 4);

// Four potentiometers
const byte pins[] = { A1, A2, A3, A0 };

float smooth[4];
int val[4];

// Rotary encoder
const byte CLK = 2;
const byte DT = 3;
const byte SW = 4;

volatile int ticks = 0;
volatile byte prevAB;

const int8_t transitions[] = {
  0, -1, 1, 0,
  1, 0, 0, -1,
  -1, 0, 0, 1,
  0, 1, -1, 0
};

void encoderISR() {
  byte ab = (digitalRead(CLK) << 1)
            | digitalRead(DT);

  ticks += transitions[(prevAB << 2) | ab];
  prevAB = ab;
}

// Eight notes, two characters each
char notes[17] = "C E G B A G E D ";

// Serial communication
char buffer[64];
byte used = 0;

// Sequencer state
int selected = 0;
int playhead = 0;

bool playing = false;
bool editing = false;

// Button debounce
bool rawButton = HIGH;
bool stableButton = HIGH;

unsigned long changed = 0;
unsigned long lastUpdate = 0;


// RECEIVE DATA FROM BROWSER

void receiveLine() {

  // Receive sequence notes
  if (buffer[0] == 'N' && strlen(buffer) == 18 && buffer[1] == ',') {

    memcpy(notes, buffer + 2, 16);
    notes[16] = 0;
  }

  // Receive sequencer state
  if (buffer[0] == 'S' && buffer[1] == ',') {

    int p, s, e;

    if (sscanf(buffer + 2,
               "%d,%d,%d",
               &p, &s, &e)
        == 3) {

      playing = p;
      selected = constrain(s, 0, 7);
      editing = e;
    }
  }

  // Receive current playback step
  if (buffer[0] == 'P' && buffer[1] == ',') {

    playhead = constrain(
      atoi(buffer + 2), 0, 7);
  }
}


// SETUP

void setup() {

  Serial.begin(115200);

  lcd.init();
  lcd.backlight();

  pinMode(CLK, INPUT_PULLUP);
  pinMode(DT, INPUT_PULLUP);
  pinMode(SW, INPUT_PULLUP);

  prevAB = (digitalRead(CLK) << 1)
           | digitalRead(DT);

  attachInterrupt(
    digitalPinToInterrupt(CLK),
    encoderISR,
    CHANGE);

  attachInterrupt(
    digitalPinToInterrupt(DT),
    encoderISR,
    CHANGE);

  for (byte i = 0; i < 4; i++) {
    smooth[i] = analogRead(pins[i]);
    val[i] = round(smooth[i] * 100 / 1023);
  }
}


// MAIN LOOP

void loop() {

  // Read incoming browser messages
  while (Serial.available()) {

    char c = Serial.read();

    if (c == '\n') {

      buffer[used] = 0;
      receiveLine();
      used = 0;

    } else if (c != '\r' && used < sizeof(buffer) - 1) {

      buffer[used++] = c;
    }
  }


  // ROTARY ENCODER ROTATION

  noInterrupts();

  int detents = ticks / 4;
  ticks -= detents * 4;

  interrupts();

  // Keep selection and note editing available during playback
  if (detents) {
    Serial.print("E,");
    Serial.println(detents);
  }


  // ROTARY ENCODER BUTTON

  bool b = digitalRead(SW);

  if (b != rawButton) {
    rawButton = b;
    changed = millis();
  }

  if (millis() - changed >= 25 && b != stableButton) {

    stableButton = b;

    if (b == LOW) {
      Serial.println("B");
    }
  }


  // UPDATE EVERY 50 MILLISECONDS

  if (millis() - lastUpdate < 50) {
    return;
  }

  lastUpdate = millis();


  // READ AND SMOOTH POTENTIOMETERS

  for (byte i = 0; i < 4; i++) {

    long sum = 0;

    analogRead(pins[i]);

    for (byte j = 0; j < 16; j++) {
      sum += analogRead(pins[i]);
    }

    smooth[i] +=
      (sum / 16.0 - smooth[i]) * 0.15;

    float n = smooth[i] * 100 / 1023;

    if (abs(n - val[i]) > 0.7) {
      val[i] = constrain(
        (int)round(n), 0, 100);
    }
  }

  int pitch = round(
                smooth[0] * 200 / 1023)
              - 100;


  // SEND KNOB VALUES TO BROWSER

  Serial.print(pitch);
  Serial.print(',');

  Serial.print(val[1]);
  Serial.print(',');

  Serial.print(val[2]);
  Serial.print(',');

  Serial.println(val[3]);


  // UPDATE LCD

  char row[21];

  lcd.noCursor();
  lcd.noBlink();

  // ROW 1: Eight sequencer notes
  lcd.setCursor(0, 0);
  lcd.print(notes);
  lcd.print("    ");

  // ROW 2: Playback marker, separate from the selected note
  lcd.setCursor(0, 1);
  lcd.print("                    ");
  if (playing) {
    lcd.setCursor(2 * playhead, 1);
    lcd.print("^");
  }

  // ROW 3: Pitch and cutoff
  snprintf(
    row,
    sizeof(row),
    "PIT:%+4d  CUT:%3d  ",
    pitch,
    val[1]);

  lcd.setCursor(0, 2);
  lcd.print(row);

  // ROW 4: Tempo and LFO
  snprintf(
    row,
    sizeof(row),
    "BPM:%3d   LFO:%3d  ",
    40 + 2 * val[2],
    val[3]);

  lcd.setCursor(0, 3);
  lcd.print(row);


  // SEQUENCER CURSOR

  lcd.setCursor(
    2 * selected,
    0);

  lcd.cursor();

  // Blink only during note editing
  if (editing) {
    lcd.blink();
  }
}
