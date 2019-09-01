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
#ifndef _BROSE_FLIPDOT_H
#define _BROSE_FLIPDOT_H

#if ARDUINO >= 100
 #include "Arduino.h"
 #define WIRE_WRITE Wire.write
#else
 #include "WProgram.h"
  #define WIRE_WRITE Wire.send
#endif

#if defined(__SAM3X8E__)
 typedef volatile RwReg PortReg;
 typedef uint32_t PortMask;
 #define HAVE_PORTREG
#elif defined(ARDUINO_ARCH_SAMD)
// not supported
#elif defined(ESP8266) || defined(ARDUINO_STM32_FEATHER)
  typedef volatile uint32_t PortReg;
  typedef uint32_t PortMask;
#else
  typedef volatile uint8_t PortReg;
  typedef uint8_t PortMask;
 #define HAVE_PORTREG
#endif

#include <brzo_i2c.h>
#include <Adafruit_GFX.h>


#define BLACK 1
#define WHITE 0
#define INVERSE 2

static const uint8_t DISPLAY_WIDTH = 28 * 4;
static const uint8_t DISPLAY_HEIGHT = 16;

class BroseFlipDot_28x16 : public Adafruit_GFX {
 
 public:

    BroseFlipDot_28x16();  

    void clearDisplay(void);
    void setDisplay(void);
    void invertDisplay(void);
    void display();

    void directDrawBuffer( const uint8_t *buffer );
    void drawPixel(int16_t x, int16_t y, uint16_t color);

  // inline void drawFastVLineInternal(int16_t x, int16_t y, int16_t h, uint16_t color) __attribute__((always_inline));
  // inline void drawFastHLineInternal(int16_t x, int16_t y, int16_t w, uint16_t color) __attribute__((always_inline));

  private:
    void i2c_tx(uint8_t addr, uint8_t data);
    void flipDot(boolean set, uint8_t row, uint8_t col);

};

#endif
