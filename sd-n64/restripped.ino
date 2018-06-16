/*
 * Copyright (c) 2009 Andrew Brown
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

// Credit goes to rcombs for making the real nitty gritty stuff, I just programmed in the higher level things


/* PREPROCESSOR */


#include "pins_arduino.h"
#include "crc_table.h"
#include <SPI.h>
#include <SD.h>

// This is the Chip Select (CS) pin, which is pin 5 on the Feather 32u4 with the 3.5" TFT FeatherWing
#define SD_SS_PIN 5
// This is the LED, which is built-in at pin 13 on the Feather
#define STATUS_PIN 13

// If you don't know what this means why are you here
#define SERIAL_BAUD_RATE 9600

#define N64_PIN A5
#define N64_HIGH DDRB &= ~0x01
#define N64_LOW DDRB |= 0x01
#define N64_QUERY (PINB & 0x01)

#define LED_HIGH DDRB &= ~0x20
#define LED_LOW DDRB |= 0x20

#define INPUT_BUFFER_SIZE 16

// 10 ms
#define INPUT_BUFFER_UPDATE_TIMEOUT 10


/* INITIALIZATION */


// command byte from n64
static unsigned char n64_command;
// raw dump of the remainder of the receive data
static char n64_raw_dump[14];

// Transmission data buffer
static unsigned char n64_buffer[5];

// Waits for a signal from the N64, and when received reads it into n64_command and n64_raw_dump
static void get_n64_command();
// Simple enough, sends data to the N64
static void n64_send();

// Buffers. This uses 2 buffers for redundancy
static unsigned long *inputBuffer;
static bool bufferALoaded, bufferBLoaded;
static bool bufferAInUse, bufferBInUse;
static int bufferEndPos;
static bool bufferOneMore;
static long bufferPos;
static void updateInputBuffer();

// SD interface / TAS reading
static File m64File;
static bool m64OpenSuccess = false;
static bool openM64();
static bool selected = false;

static bool finished = false;

// Frame counters
static unsigned long numFrames = 0, curFrame = 0;

// These are reused for counting files when listing the directory to save space
// Only one interpretation is used at any given time
#define dirPos curFrame
#define numFiles numFrames


/* UTILITY FUNCTIONS */


// Checks if file extension is .m64 or .M64
// TODO: Instead of using this for getM64Name, use this to filter the directory
static bool extMatches (const char *name) {
  int len = strlen(name);
  
  // ".m64" is 4 characters, so the file cannot possibly be valid if it's less than or equal to 4
  if (len <= 4)
    return false;
  
  return !strcmp_P(name + len - 4, PSTR(".m64")) && !strcmp_P(name + len - 4, PSTR(".M64"));
}

// Finds the .m64 in the directory
// This reuses n64_raw_dump like dirPos/curFrame
// TODO: Bind this to touchscreen instead
static void getM64Name () {
  File dir = SD.open(F("/"));
  int pos = 0;
  
  for (m64File = dir.openNextFile(); m64File; m64File = dir.openNextFile()) {
    const char *name = m64File.name();
    
    if (!m64File.isDirectory() && extMatches(name) && name[0] != "_")
      strcpy(n64_raw_dump, name);
    
    m64File.close();
  }
  
  dir.close();
}

void logFrame () {
  // Joystick display goes here!
}


/* MAIN CODE */


void setup () {
  Serial.begin(SERIAL_BAUD_RATE);
  while (!Serial.available()); // Wait to receive input
  
  Serial.println(F("Starting...")); // TFT too (white on black)
  
  // LED on pin 13
  digitalWrite(STATUS_PIN, LOW);
  pinMode(STATUS_PIN, OUTPUT);
  
  // N64 communication pin
  digitalWrite(N64_PIN, LOW);
  pinMode(N64_PIN, INPUT);
  
  // Initialize SD card
  if (!SD.begin(SD_SS_PIN)) {
    m64OpenSuccess = false;
    Serial.println(F("SD card initialization failed!")); // TFT red on black
    return;
  }
  
  Serial.println(F("SD card initialization succeeded.")); // TFT green on black
  
  // Buffer setup
  bufferALoaded = false;
  bufferBLoaded = false;
  bufferAInUse = false;
  bufferBInUse = false;
  bufferEndPos = -1;
  bufferOneMore = true;
  bufferPos = -1;
  
  Serial.println(F("Initialization done.")); // TFT green on black
  
  getM64Name();
  
  Serial.print(F("File selected: ")); // TFT green on black
  Serial.println(n64_raw_dump); // TFT white on black
}

static void selectLoop () {
  // TODO: Hook this up to TFT
  selected = true;
  
  openM64();
  updateInputBuffer();
  
  Serial.println(F("Setup completed.")); // TFT green on black
}

void mainLoop () {
  unsigned char data, addr;
  unsigned long updateTime;
  
  if (!m64OpenSuccess) {
    Serial.println(F("Stopping program.")); // TFT red on black
    for (;;);
  }
  
  // Block until we're sure the line is idle
  for (int idle_wait = 32; idle_wait > 0; idle_wait --) {
    if (!N64_QUERY)
      idle_wait = 32;
  }
  
  // Disable interrupts
  noInterrupts();
  
  // Block until we get a command from the N64
  get_n64_command();
  
  // 0x00 | identify (also 0xFF)
  // 0x01 | status
  // 0x02 | read
  // 0x03 | write
  
  switch (n64_command) {
    case 0x00:
    case 0xFF:
      // return 0x050001 if we have a Rumble Pak (we pretend we do)
      // return 0x050002 if we don't
      // 0xFF is sent from Super Mario 64 and Shadows of the Empire.
      n64_buffer[0] = 0x05;
      n64_buffer[1] = 0x00;
      n64_buffer[2] = 0x01;
      
      // Send the data
      n64_send(n64_buffer, 3, 0);
      
      // Re-enable interrupts
      interrupts();
      
      Serial.println(F("Identified as controller")); // TFT green on black
      
      break;
    case 0x01:
      // This is where we send out input data
      
      if (finished && !bufferOneMore)
        // If the TAS is finished, clear the buffer
        *(long*)n64_buffer = 0;
      else
        // If not, continue
        *(long*)n64_buffer = *(inputBuffer + bufferPos);
      
      n64_send(n64_buffer, 4, 0);
      interrupts();
      
      if (finished)
        break;
      
      logFrame();
      
      // Update the input buffer but make sure it doesn't take too long
      updateTime = micros();
      updateInputBuffer();
      
      // If it took too long, log it
      updateTime = micros() - updateTime;
      
      if (updateTime > INPUT_BUFFER_UPDATE_TIMEOUT * 1000) {
        Serial.print(F("Input buffer update took too long (")); // TFT red on black
        Serial.print(updateTime / 1000);
        Serial.println(F(" ms)"));
      }
      
      // Increment frame counter
      curFrame ++;
      
      break;
    case 0x02:
      // Controller read. We're pretending to have a Rumble Pak.
      // If the read address is 0x8000 (only thing it should be),
      // return 32 0x80 and a CRC (0xB8) to tell the N64 we have a Rumble Pak.
      
      memset(n64_buffer, 0x80, 32);
      n64_buffer[32] = 0xB8;
      
      n64_send(n64_buffer, 33, 1);
      interrupts();
      
      Serial.println(F("Handled read.")); // TFT white on black
      
      break;
    case 0x03:
      // Controller write. We always need to respond with at least 1 CRC byte.
      // If the write is to set 0xC000 to 0x01, Rumble Pak is activated.
      // All other write addresses are ignored.
      
      // Decode the first data byte (byte 4) and write it into data
      data = 0;
      data |= (n64_raw_dump[16] != 0) << 7;
      data |= (n64_raw_dump[17] != 0) << 6;
      data |= (n64_raw_dump[18] != 0) << 5;
      data |= (n64_raw_dump[19] != 0) << 4;
      data |= (n64_raw_dump[20] != 0) << 3;
      data |= (n64_raw_dump[21] != 0) << 2;
      data |= (n64_raw_dump[22] != 0) << 1;
      data |= (n64_raw_dump[23] != 0);
      
      // Get the CRC byte, but invert it because we pretend to have a memory card
      n64_buffer[0] = crc_repeating_table[data] ^ 0xFF;
      
      n64_send(n64_buffer, 1, 1);
      interrupts();
      
      Serial.println(F("Handled write.")); // TFT white on black
      
      // Finished time-critical code
      // TODO: decode the address (bytes 1 and 2) to see if it was 0xC000 and get ready to RUMBLE
      
      break;
    default:
      interrupts();
      
      Serial.print(F("Received unknown command byte 0x")); // TFT red on black
      Serial.println(n64_command, HEX);
      
      break;
  }
}

void loop () {
  if (!selected)
    return selectLoop();
  mainLoop();
}

static bool openM64 () {
  // http://tasvideos.org/EmulatorResources/Mupen/M64.html
  // Important addresses:
  // 0x000: Signature     Constant string 4D 36 34 1A   (4 bytes)
  // 0x004: Version       Mupen64 version, usually 03 00 00 00  (4 bytes)
  // 0x018: Number of input polls   Approximately the number of frames  (4 bytes)
  // 0x200: Start of input for versions <3
  // 0x400: Start of input for version 3
  
  // Each input is 4 bytes. The first 2 are flags for buttons, the next 2 are signed joystick X and Y.
  
  char signature[4];
  int version;
  
  // Open the file
  Serial.print(F("Opening file ")); // TFT white on black
  Serial.print(n64_raw_dump);
  Serial.println(F("..."));
  
  m64File = SD.open(n64_raw_dump);
  
  // Error check
  if (!m64File) {
    Serial.println(F("Error in opening file.")); // TFT red on black
    return false;
  }
  
  // Get signature and version
  if (m64File.read(signature, 4) != 4 || m64File.read(&version, 4) != 4) {
    Serial.println(F("Failed to read signature.")); // TFT red on black
    m64File.close();
    return false;
  }
  
  // Verify signature
  if (memcmp(signature, "M64\x1A", 4) != 0) {
    Serial.println(F("Invalid signature.")); // TFT red on black
    m64File.close();
    return false;
  }
  
  Serial.print(F("Mupen64 version: ")); // TFT white on black
  Serial.println(version);
  
  m64File.seek(0x018);
  
  // Get (presumed) number of frames
  // NOTE: It doesn't matter if this is the real number of frames or not,
  // since frames are only really meaningful for display. For all practical purposes, this is
  if (m64File.read(&numFrames, 4) != 4) {
    Serial.println(F("Failed to read frame count.")); // TFT red on black
    m64File.close();
    return false;
  }
  
  // Move to start of input
  if (version < 3)
    m64File.seek(0x200);
  else
    m64File.seek(0x400);
  
  // Final check
  if (!m64File.available()) {
    Serial.println(F("No input data found, does the TAS play back on Mupen64?")); // TFT red on black
    m64File.close();
    return false;
  }
  
  // Allocate the input buffer
  inputBuffer = malloc(INPUT_BUFFER_SIZE * 4);
  if (!inputBuffer) {
    Serial.println(F("Failed to allocate input buffer.")); // TFT red on black
    m64File.close();
    return false;
  }
  
  Serial.println(F("File opened successfully.")); // TFT green on black
  
  m64OpenSuccess = true;
  
  // If there's not as much input data in the M64 as expected, cut it down to that size
  numFrames = min(numFrames, (m64File.size() - m64File.position()) / 4);
  
  return true;
}

static void updateInputBuffer () {
  // Read (as in red) bytes from the M64
  int readBytes = 0;
  
  // First check
  if (bufferPos == -1) {
    // Initially, both buffers are not in use
    bufferPos = 0;
    bufferAInUse = false;
    bufferBInUse = false;
  } else {
    // Increment index into buffer
    bufferPos ++;
    
    // Wrap it around so we can repeat inputs
    if (bufferPos >= INPUT_BUFFER_SIZE) {
      bufferPos = 0;
    }
    
    // Check which of bufferA and bufferB is in use
    // They each make up half the input buffer
    if (bufferPos < INPUT_BUFFER_SIZE / 2) {
      // It's bufferA
      
      // Check if it's loaded
      if (!bufferALoaded)
        Serial.println(F("No new input was loaded in buffer A.")); // TFT red on black
      
      if (!bufferAInUse)
        bufferBLoaded = false;
      
      bufferAInUse = true;
      bufferBInUse = false;
    } else {
      // It's bufferB
      
      // Check if it's loaded
      if (!bufferBLoaded)
        Serial.println(F("No new input was loaded in buffer B.")); // TFT red on black
      
      if (!bufferBInUse)
        bufferALoaded = false;
      
      bufferAInUse = false;
      bufferBInUse = true;
    }
  }
  
  // If we're at the end of the buffer, exit
  // bufferEndPos is the position in the buffer of the end of the file
  if (bufferEndPos != -1) {
    if (!bufferOneMore) {
      Serial.println(F("Finished playing TAS.")); // TFT green on black
      finished = true;
    } else if (bufferPos == bufferEndPos)
      bufferOneMore = false;
    
    return;
  }
  
  // Check if the file is done
  if (!m64File.available()) {
    // Set end position
    if (!bufferAInUse) {
      // End of buffer B is the end of the whole buffer
      bufferEndPos = INPUT_BUFFER_SIZE - 1;
    } else if (!bufferBInUse) {
      // End of buffer A is in the middle
      bufferEndPos = INPUT_BUFFER_SIZE / 2 - 1;
    } else {
      // uwotm8
      Serial.println(F("Da fuq")); // TFT red on black
      
      // End immediately
      bufferEndPos = 0;
      finished = true;
    }
    
    // Close file
    m64File.close();
    return;
  }
  
  // Load bytes into buffer A/B if buffer A/B isn't in use
  // Whoever wrote this is smart
  if (!bufferALoaded && !bufferAInUse) {
    // Read bytes
    // readBytes is the length of the read data
    readBytes = m64File.read(inputBuffer, INPUT_BUFFER_SIZE * 2);
    
    // If there is no read data, it failed
    if (readBytes == 0) {
      Serial.println(F("Failed to read inputs. (Is SPI management bad?)")); // TFT red on black
    } else {
      bufferALoaded = true;
      
      // Check if the read data isn't the normal size, which means it must be the end of the file
      if (readBytes != INPUT_BUFFER_SIZE * 2) {
        // Set end position
        bufferEndPos = readBytes / 4 - 1;
        m64File.close();
      }
    }
  } else if (!bufferBLoaded && !bufferBInUse) {
    // Read bytes
    readBytes = m64File.read(inputBuffer + (INPUT_BUFFER_SIZE / 2), INPUT_BUFFER_SIZE * 2);
    
    // If there is no read data, it failed
    if (readBytes == 0) {
      Serial.println(F("Failed to read inputs. (Recoverable, is SPI management bad?)")); // TFT red on black
    } else {
      bufferBLoaded = true;
      
      // Check if the read data isn't the normal size, which means it must be the end of the file
      if (readBytes != INPUT_BUFFER_SIZE * 2) {
        // Set end position
        bufferEndPos = INPUT_BUFFER_SIZE / 2 + readBytes / 4 - 1;
        m64File.close();
      }
    }
  }
}

// Sends data to the N64
static void n64_send (unsigned char *buffer, char length, bool wide_stop) {
  // monkaS
  asm volatile ("; Starting N64 Send Routine");
  
  // Number of bits to go
  char bits;
  
  // Insanely precise, don't mess with this
  asm volatile ("; Starting outer for loop");
  
  outer_loop: {
    asm volatile ("; Starting inner for loop");
    bits = 8;
    
    inner_loop: {
      // Starting to send a bit
      asm volatile ("; Setting line to low");
      N64_LOW;
      
      asm volatile ("; Branching");
      
      if (*buffer >> 7) {
        asm volatile ("; Bit is a 1");
        
        // For a 1 bit, stay low for 1 microsecond (us) then go high for 3 us
        // This is the 1 us
        asm volatile ("nop\nnop\nnop\nnop\nnop\n");
        
        asm volatile ("; Setting line to high");
        N64_HIGH;
        
        // This is actually 2 us, we add on the common 1 us after
        asm volatile (
          "nop\nnop\nnop\nnop\nnop\n"
          "nop\nnop\nnop\nnop\nnop\n"
          "nop\nnop\nnop\nnop\nnop\n"
          "nop\nnop\nnop\nnop\nnop\n"
          "nop\nnop\nnop\nnop\nnop\n"
          "nop\nnop\nnop\nnop\nnop\n"
        );
        
      } else {
        asm volatile ("; Bit is a 0");
        
        // For a 0 bit, stay low for 3 us then go high for 1 us
        asm volatile (
          "nop\nnop\nnop\nnop\nnop\n"
          "nop\nnop\nnop\nnop\nnop\n"
          "nop\nnop\nnop\nnop\nnop\n"
          "nop\nnop\nnop\nnop\nnop\n"
          "nop\nnop\nnop\nnop\nnop\n"
          "nop\nnop\nnop\nnop\nnop\n"
          "nop\n"
        );
        
        asm volatile ("; Setting line to high");
        N64_HIGH;
        
        asm volatile ("; End of conditional branch, need to wait 1 us more before next bit");
      }
      
      // The line is always high at this point, where we wait 1 us
      
      asm volatile ("; Finishing inner loop body");
      
      // Decrement counter for leftover bitfs
      bits --;
      
      // If we still have some more to go, wait 1 us on high
      if (bits != 0) {
        asm volatile (
          "nop\nnop\nnop\nnop\nnop\n"
          "nop\nnop\nnop\nnop\n"
        );
        
        asm volatile ("; Next byte");
        *buffer <<= 1;
        
        goto inner_loop;
      }
    }
    
    asm volatile ("; Continuing outer loop");
    
    // All this takes exactly 1 us so no nops are needed
    
    length --;
    
    if (length != 0) {
      buffer ++;
      goto outer_loop;
    }
  }
  
  // Send a stop signal
  asm volatile ("nop\nnop\nnop\nnop\n");
  N64_LOW;
  
  asm volatile (
    "nop\nnop\nnop\nnop\nnop\n"
    "nop\nnop\nnop\nnop\nnop\n"
    "nop\n"
  );
  
  if (wide_stop) {
    asm volatile (
      ";another 1 us for extra wide stop bit\n"
      "nop\nnop\nnop\nnop\nnop\n"
      "nop\nnop\nnop\nnop\nnop\n"
      "nop\nnop\nnop\nnop\n"
    );
  }
  
  N64_HIGH;
}
