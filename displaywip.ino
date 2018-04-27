// Pins
#define LED_PIN 13

// Serial
#define SERIAL_BAUD_RATE 9600

// Control characters used in display
#define FORM_FEED 12
#define END_OF_TEXT 4

// Button flag masks
#define D_UP_BUTTON 0x0001
#define D_DOWN_BUTTON 0x0002
#define D_LEFT_BUTTON 0x0004
#define D_RIGHT_BUTTON 0x0008

#define START_BUTTON 0x0010
#define A_BUTTON 0x0020
#define B_BUTTON 0x0040
#define Z_BUTTON 0x0080

#define C_UP_BUTTON 0x0100
#define C_DOWN_BUTTON 0x0200
#define C_LEFT_BUTTON 0x0400
#define C_RIGHT_BUTTON 0x0800

#define L_BUTTON 0x1000
#define R_BUTTON 0x2000

byte joystickX = 0xFF;
byte joystickY = 0x00;

unsigned short buttons = 0x2AA5;

char buttonMask (unsigned short mask) {
  return buttons & mask ? '*' : ' ';
}

void printControllerDisp () {
  int theta = round(atan2(joystickY - 0x7F, joystickX - 0x7F) * 4 / PI);
  int led = (abs(joystickX - 0x7F) + abs(joystickY - 0x7F) - 0x7F) / 2;
  
  analogWrite(LED_PIN, led); // LED pin
  
  Serial.write(FORM_FEED); // Form Feed to clear the terminal
  
  Serial.print("          L [");
  Serial.print(buttonMask(L_BUTTON));
  
  Serial.print("]         S [");
  Serial.print(buttonMask(START_BUTTON));
  
  Serial.print("]       R [");
  Serial.print(buttonMask(R_BUTTON));
  Serial.println("]");
  
  Serial.print(joystickX);
  Serial.print(", ");
  Serial.print(joystickY);

  int joystickXDigits = joystickX ? floor(log10(joystickX)) + 1 : 1;
  int joystickYDigits = joystickY ? floor(log10(joystickY)) + 1 : 1;
  
  // Pad to 8 chars
  for (int i = joystickXDigits + joystickYDigits; i < 6; i ++) {
    Serial.print(" ");
  }

  Serial.print("               LED: ");
  Serial.println(led);
  
  Serial.print(
    theta == 1 ? "  /" :
    theta == 2 ? " | " :
    theta == 3 ? "\\  " : "   "
  );
  
  Serial.print("      D^ [");
  Serial.print(buttonMask(D_UP_BUTTON));
  
  Serial.print("]         A [");
  Serial.print(buttonMask(A_BUTTON));
  
  Serial.print("]      C^ [");
  Serial.print(buttonMask(C_UP_BUTTON));
  Serial.println("]");
  
  Serial.print(
    theta == 0 ? " ._" :
    abs(theta) == 4 ? "_. " : " . "
  );
  
  Serial.print("   D< [");
  Serial.print(buttonMask(D_LEFT_BUTTON));
  
  Serial.print("]   [");
  Serial.print(buttonMask(D_RIGHT_BUTTON));
  
  Serial.print("] D>   B [");
  Serial.print(buttonMask(B_BUTTON));
  
  Serial.print("]   C< [");
  Serial.print(buttonMask(C_LEFT_BUTTON));
  
  Serial.print("]   [");
  Serial.print(buttonMask(C_RIGHT_BUTTON));
  Serial.println("] C>");
  
  Serial.print(
    theta == -3 ? "/  " :
    theta == -2 ? " | " :
    theta == -1 ? "  \\" : "   "
  );
  
  Serial.print("      Dv [");
  Serial.print(buttonMask(D_DOWN_BUTTON));
  
  Serial.print("]         Z [");
  Serial.print(buttonMask(Z_BUTTON));
  
  Serial.print("]      Cv [");
  Serial.print(buttonMask(C_DOWN_BUTTON));
  Serial.println("]");

  Serial.write(END_OF_TEXT); // EOT to end terminal buffering
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  
  Serial.begin(SERIAL_BAUD_RATE);
}

void loop() {
  double joystickTheta = atan2(joystickY - 0x7F, joystickX - 0x7F);
  
  joystickX = 0x7F * cos(joystickTheta + 0.03) + 0x7F;
  joystickY = 0x7F * sin(joystickTheta + 0.03) + 0x7F;
  
  printControllerDisp();

  delay(30);
}
