/*********************************************************************
  This is a library for Brose Flipdot Matrix displays

  These displays use I2C to communicate, 2 are required to  
  interface

  Using the Adafruit gfx library. Big thanx, Adafruit Industries!

  -------------------------------------------------------------------------
  
  The MIT License (MIT)
  Copyright © 2019 <copyright Diederich Kroeske>
  
  Permission is hereby granted, free of charge, to any person obtaining a 
  copy of this software and associated documentation files (the “Software”), 
  to deal in the Software without restriction, including without limitation 
  the rights to use, copy, modify, merge, publish, distribute, sublicense, 
  and/or sell copies of the Software, and to permit persons to whom the 
  Software is furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in 
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL 
  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN 
  THE SOFTWARE.

*********************************************************************/


#ifdef __AVR__
  #include <avr/pgmspace.h>
#elif defined(ESP8266)
 #include <pgmspace.h>
#else
 #define pgm_read_byte(addr) (*(const unsigned char *)(addr))
#endif

#if !defined(__ARM_ARCH) && !defined(ENERGIA) && !defined(ESP8266)
 #include <util/delay.h>
#endif

#include <stdlib.h>
#include <Adafruit_GFX.h>
#include "brose_fp_gfx.h"

const int I2C_SCL_PIN = D1;
const int I2C_SDA_PIN = D2;

#define swap(a, b) { int16_t t = a; a = b; b = t; }

uint8_t buffer[2][DISPLAY_WIDTH * DISPLAY_HEIGHT / 8];
//uint8_t display_buffer[DISPLAY_WIDTH * DISPLAY_HEIGHT / 8] = {0,};
uint8_t *displayPtr;
uint8_t *bufferPtr;

BroseFlipDot_28x16::BroseFlipDot_28x16() : Adafruit_GFX(DISPLAY_WIDTH, DISPLAY_HEIGHT) {
  
  displayPtr = buffer[0];
  bufferPtr = buffer[1];
  memset(bufferPtr, 0x00, (DISPLAY_WIDTH * DISPLAY_HEIGHT/8));
  memset(displayPtr, 0xFF, (DISPLAY_WIDTH * DISPLAY_HEIGHT/8));

  //Wire.begin(I2C_SDA_Pin, I2C_SCL_Pin);
  brzo_i2c_setup(I2C_SDA_PIN, I2C_SCL_PIN, 200);
  i2c_tx(0x20, 0x00);
  i2c_tx(0x21, 0x00);
  i2c_tx(0x22, 0x00);
}  

//
//
//
void BroseFlipDot_28x16::directDrawBuffer( const uint8_t *buffer ) {
  memcpy(bufferPtr, buffer, (DISPLAY_WIDTH * DISPLAY_HEIGHT/8));
}


//
//
//
void BroseFlipDot_28x16::drawPixel(int16_t x, int16_t y, uint16_t color) {
  if ((x < 0) || (x >= width()) || (y < 0) || (y >= height()))
    return;

  // check rotation, move pixel around if necessary
  switch (getRotation()) {
  case 1:
    swap(x, y);
    x = WIDTH - x - 1;
    break;
  case 2:
    x = WIDTH - x - 1;
    y = HEIGHT - y - 1;
    break;
  case 3:
    swap(x, y);
    y = HEIGHT - y - 1;
    break;
  }

  // x is which column
  switch (color)
  {
    case BLACK:   *(bufferPtr + x + (y/8)*DISPLAY_WIDTH) |=  (1 << (y&7)); break;
    case WHITE:   *(bufferPtr + x + (y/8)*DISPLAY_WIDTH) &= ~(1 << (y&7)); break;
    case INVERSE: *(bufferPtr + x + (y/8)*DISPLAY_WIDTH) ^=  (1 << (y&7)); break;
  }
}

//
//
// 
void BroseFlipDot_28x16::invertDisplay(void) {
  for( uint8_t y = 0; y < (DISPLAY_WIDTH * DISPLAY_HEIGHT / 8); y++ ) {
    (*(bufferPtr+y))^=0xFF;
    yield();
  }  
}

//
//
//
void BroseFlipDot_28x16::display(void) {

  for( uint8_t y = 0; y < (DISPLAY_WIDTH * DISPLAY_HEIGHT / 8); y++ ) {
    uint8_t row = 8 * (y / DISPLAY_WIDTH);
    uint8_t colomn = y % DISPLAY_WIDTH;
    
    uint8_t new_pixel = *(bufferPtr + y);
    uint8_t prev_pixel = *(displayPtr + y);

    if( prev_pixel != new_pixel ) {
      for (uint8_t b = 0; b < 8; b++) {
        flipDot(((new_pixel >> b) & 0x01), row + b, colomn);
      }
    }

    // Update displayPtr with new
    *(displayPtr + y) = new_pixel;
    
    // Prevent WD reset
    yield();
  }

  // Switch off all fp2800 drivers
  i2c_tx(0x20, 0x00);
  i2c_tx(0x21, 0x00);
  i2c_tx(0x22, 0x00);
}

//
//
//
void BroseFlipDot_28x16::clearDisplay(void) {
  memset(bufferPtr, 0x00, (DISPLAY_WIDTH * DISPLAY_HEIGHT/8));
}

//
//
//
void BroseFlipDot_28x16::setDisplay(void) {
  memset(bufferPtr, 0xFF, (DISPLAY_WIDTH * DISPLAY_HEIGHT/8));
}

//
//
//
void BroseFlipDot_28x16::flipDot(boolean set, uint8_t row, uint8_t col) {

    uint8_t row_lut[] = {
      0x02, 0x01, 0x03, 0x04, 0x06, 0x05, 0x07, 0x0A, 0x09, 0x0B, 0x0C, 0x0E, 0x0D, 0xFF,
      0x02, 0x01, 0x03, 0x04, 0x06, 0x05, 0x07, 0x0A, 0x09, 0x0B, 0x0C, 0x0E, 0x0D, 0xFF,
    };

    uint8_t col_lut[] = {
      (4 << 2) | 0x00, (2 << 2) | 0x00, (6 << 2) | 0x00, (1 << 2) | 0x00, (5 << 2) | 0x00, (3 << 2) | 0x00, (7 << 2) | 0x00,
      (4 << 2) | 0x02, (2 << 2) | 0x02, (6 << 2) | 0x02, (1 << 2) | 0x02, (5 << 2) | 0x02, (3 << 2) | 0x02, (7 << 2) | 0x02,
      (4 << 2) | 0x01, (2 << 2) | 0x01, (6 << 2) | 0x01, (1 << 2) | 0x01, (5 << 2) | 0x01, (3 << 2) | 0x01, (7 << 2) | 0x01,
      (4 << 2) | 0x03, (2 << 2) | 0x03, (6 << 2) | 0x03, (1 << 2) | 0x03, (5 << 2) | 0x03, (3 << 2) | 0x03, (7 << 2) | 0x03,
    };

    uint8_t reg_data = row_lut[row];
    uint8_t de_set = 0x30;
    uint8_t de_clr = 0x28;

    if( row >= 14 ) {
        de_set = 0xC0;
        de_clr = 0x88;
    }

    /* 
     * Get from LUT a0, a1, a2 and b1 for fp2800a (i2c addr: 0x22)
     * (b0 is used to select clr.)
     * 
     */
    uint8_t a0a1 = (reg_data & 0x03) >> 0;
    uint8_t a2 = (reg_data & 0x04) >> 2;
    uint8_t b1 = (reg_data & 0x08) >> 3;

    /* 
     *  Set fp2800a (i2c addr = 0x22) to select row.
     */
    uint8_t b;
    if(set) {
      b = de_set | a0a1 | (b1 << 2); // E = 1, D = 1, b0 = 0;
    }
    else {
      b = de_clr | a0a1 | (b1 << 2); // E = 1, D = 0, b0 = 1;
    }
    i2c_tx(0x22, b);

    /*
     * Set fp2800a (i2c addr = 0x01) to select colomn.
     * (P7 is used to select a2 for row select).
     *
     */
    uint8_t c;
    if(set) {
      c = (a2 << 7) | (0 << 6) | (col_lut[col%28] << 1); // D = 0
    }
    else {
      c = (a2 << 7) | (1 << 6) | (col_lut[col%28] << 1); // D = 1
    }
    i2c_tx(0x21, c);

    // Strobe colomn (i2c addr = 0x00) (E pin 1->0)
    for(uint8_t idx = 0; idx < 1; idx++) {
      
      switch( col / 28) {
        case 0:
          i2c_tx(0x20, 0xFE); // Panel 1: 1111 1110
          break;
        case 1:
          i2c_tx(0x20, 0xFD); // Panel 2: 1111 1101
          break;
        case 2:
          i2c_tx(0x20, 0xFB); // Panel 3: 1111 1011
          break;
        case 3:
          i2c_tx(0x20, 0xF7); // Panel 4: 1111 0111
          break;
      }
      delayMicroseconds(600);

      switch( col / 28) {
        case 0:
          i2c_tx(0x20, 0x7E); // Panel 1: 1111 1110
          break;
        case 1:
          i2c_tx(0x20, 0x7D); // Panel 2: 1111 1101
          break;
        case 2:
          i2c_tx(0x20, 0x7B); // Panel 3: 1111 1011
          break;
        case 3:
          i2c_tx(0x20, 0x77); // Panel 5: 1111 0111
          break;
      }
      //delayMicroseconds(400);
    }
}

void BroseFlipDot_28x16::i2c_tx(uint8_t addr, uint8_t data) {
  brzo_i2c_start_transaction(addr, 400);
  brzo_i2c_write(&data, 1, false);
  brzo_i2c_end_transaction();
}
