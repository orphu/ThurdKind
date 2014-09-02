// UX-LAB firmware 
// Animates a LPD8806 based RGB LED strip with patterns. Based on the example
// by Adafruit! 
//
// REQUIRES TIMER1 LIBRARY (included with Teensy install): http://www.arduino.cc/playground/Code/Timer1
// ALSO REQUIRES LPD8806 LIBRARY (not included): https://github.com/adafruit/LPD8806

#include <avr/pgmspace.h>
#include "SPI.h"
#include "LPD8806.h"
#include "TimerOne.h"

// ---------------------------------------------------------------------------
// Config section

// Pins are based on Teensy 2.0
// Button pin.
const int buttonPin = 4;

// Onboard LED pin.
const int ledPin = 11;

// Declare the number of pixels in strand; 32 = 32 pixels in a row.  The
// LED strips have 32 LEDs per meter, but you can extend or cut the strip.
const int numPixels = 6;

// Maximum frustration level. This will translate to the number of color bands.
const int maxFrustration = 6; 

// ---------------------------------------------------------------------------
// Global variables

// A variable to store button state.
int buttonState = LOW; 
int oldButtonState = LOW; 

// Used for debouncing the button input.
long lastDebounceTime = 0;  // The last time the output pin was toggled.
long debounceDelay = 50;    // The debounce time; increase if the output flickers.

// Instantiate LED strip; arguments are the total number of pixels in strip,
// the data pin number and clock pin number:
//LPD8806 strip = LPD8806(numPixels, dataPin, clockPin);

// You can also use hardware SPI for ultra-fast writes by omitting the data
// and clock pin arguments.  This is faster, but the data and clock are then
// fixed to very specific pin numbers: on Arduino 168/328, data = pin 11,
// clock = pin 13.  On Mega, data = pin 51, clock = pin 52. On teensy
// data = pin B2, clock = pin B1.
LPD8806 strip = LPD8806(numPixels);

// Principle of operation: at any given time, the LEDs depict an image or
// animation effect (referred to as the "back" image throughout this code).
// Periodically, a transition to a new image or animation effect (referred
// to as the "front" image) occurs.  During this transition, a third buffer
// (the "alpha channel") determines how the front and back images are
// combined; it represents the opacity of the front image.  When the
// transition completes, the "front" then becomes the "back," a new front
// is chosen, and the process repeats.
byte imgData[2][numPixels * 3], // Data for 2 strips worth of imagery.
alphaMask[numPixels],           // Alpha channel for compositing images.
fxIdx[3];                       // Effect # for back & front images + alpha.
int  fxVars[3][50],             // Effect instance variables. (explained later)
tCounter   = -1,                // Countdown to next transition. Start with no hold and trasition right away.
transitionTime = 40,            // Duration (in frames) of current transition.
frustration = 0;                // Current frustration level. Use to color the strip in effect 00.

// function prototypes, leave these be :)
void renderEffect00(byte idx);  // Whole strip color based on frsutration.
void renderEffect01(byte idx);  // All "black" (LEDs off). Used as a tranisition target for the fade.
void renderAlpha00(void);       // Simple cross fade transition.   
void callback();
byte gamma(byte x);
long hsv2rgb(long h, byte s, byte v);

// List of image effect and alpha channel rendering functions; the code for
// each of these appears later in this file.  Just a few to start with...
// simply append new ones to the appropriate list here:
void (*renderEffect[])(byte) = {
  renderEffect00,
  renderEffect01
},
(*renderAlpha[])(void)  = {
  renderAlpha00 
};

// ---------------------------------------------------------------------------

void setup() {
  // initialize the LED pin as an output.
  pinMode(ledPin, OUTPUT);      
  
  // initialize the pushbutton pin as an input.
  pinMode(buttonPin, INPUT);     
  
  // Start up the LED strip.  Note that strip.show() is NOT called here --
  // the callback function will be invoked immediately when attached, and
  // the first thing the calback does is update the strip.
  strip.begin();

  // Initialize random number generator from a floating analog input.
  randomSeed(analogRead(0));
  
  // Clear image data.
  memset(imgData, 0, sizeof(imgData));
  
  // Clear instance variables data.
  memset(fxVars, 0, sizeof(fxVars));

  // Setup render effects. 00 is the color version, 01 is the "off" version.
  fxIdx[0] = 0;
  fxIdx[1] = sizeof(renderEffect[0]);
  // Only one transition function.
  fxIdx[2] = 0;

  // Timer1 is used so the strip will update at a known fixed frame rate.
  // Each effect rendering function varies in processing complexity, so
  // the timer allows smooth transitions between effects (otherwise the
  // effects and transitions would jump around in speed...not attractive).
  Timer1.initialize();
  Timer1.attachInterrupt(callback, 1000000 / 60); // 60 frames/second
}

void loop() {
  // read the state of the button into a local variable.
  int reading = digitalRead(buttonPin);
  
  // The onboard LED shows the state of the button at all times.
  digitalWrite(ledPin, reading); 
  
  // If the button is depressed, reset the hold time and light the strip.
  if (reading == HIGH) {
    tCounter = -40;
  }
  
  // We need to "debounce" the button input to make sure we increment
  // color cleanly. 
  //
  // Check to see if you just pressed the button 
  // (i.e. the input went from LOW to HIGH),  and you've waited 
  // long enough since the last press to ignore any noise:  

  // If the switch changed, due to noise or pressing:
  if (reading != oldButtonState) {
    // reset the debouncing timer.
    lastDebounceTime = millis();
  }
  
  if ((millis() - lastDebounceTime) > debounceDelay) {
    // whatever the reading is at, it's been there for longer
    // than the debounce delay, so take it as the actual current state:

    // if the button state has changed, save it to buttonState.
    if (reading != buttonState) {
      buttonState = reading;

      // Increase the frustration level if the button is pressed.
      if (buttonState == HIGH) {
        frustration = (frustration >= maxFrustration) ? maxFrustration : frustration + 1;
        // re-initialize the render effect to pick up the new color.
        fxVars[0][0] = 0; 
      } 
    }
  }
  
  // Remember the last reading for the next iteration.
  oldButtonState = reading;
}

// Timer1 interrupt handler.  Called at equal intervals; 60 Hz by default.
void callback() {
  // Very first thing here is to issue the strip data generated from the
  // *previous* callback.  It's done this way on purpose because show() is
  // roughly constant-time, so the refresh will always occur on a uniform
  // beat with respect to the Timer1 interrupt.  The various effects
  // rendering and compositing code is not constant-time, and that
  // unevenness would be apparent if show() were called at the end.
  strip.show();

  // Local variables.
  byte *backPtr = &imgData[0][0], r, g, b;
  int  i;

  // Always render back image based on current effect index:
  (*renderEffect[fxIdx[0]])(0);

  // Front render and composite only happen during transitions...
  if(tCounter > 0) {
    // Transition in progress
    byte *frontPtr = &imgData[1][0];
    int  alpha, inv;

    // Render front image and alpha mask based on current effect indices...
    (*renderEffect[fxIdx[1]])(1);
    (*renderAlpha[fxIdx[2]])();

    // ...then composite front over back:
    for(i=0; i<numPixels; i++) {
      alpha = alphaMask[i] + 1; // 1-256 (allows shift rather than divide)
      inv   = 257 - alpha;      // 1-256 (ditto)
      // r, g, b are placed in variables (rather than directly in the
      // setPixelColor parameter list) because of the postincrement pointer
      // operations -- C/C++ leaves parameter evaluation order up to the
      // implementation; left-to-right order isn't guaranteed.
      r = gamma((*frontPtr++ * alpha + *backPtr++ * inv) >> 8);
      g = gamma((*frontPtr++ * alpha + *backPtr++ * inv) >> 8);
      b = gamma((*frontPtr++ * alpha + *backPtr++ * inv) >> 8);
      strip.setPixelColor(i, r, g, b);
    }
  } 
  else {
    // No transition in progress; just show back image
    for(i=0; i<numPixels; i++) {
      // See note above re: r, g, b vars.
      r = gamma(*backPtr++);
      g = gamma(*backPtr++);
      b = gamma(*backPtr++);
      strip.setPixelColor(i, r, g, b);
    }
  }

  // Count up to next transition (or end of current one):
  tCounter++;
  if(tCounter == 0) { // Transition start
    fxVars[2][0] = 0; // Initialize alpha transition.
  } 
  else if(tCounter >= transitionTime) { // End transition
    tCounter = transitionTime; // park in effect 01 until we see a new button press.
    frustration = 0; // cool down the frustration level to zero.
  }
}

// ---------------------------------------------------------------------------
// Image effect rendering functions.  Each effect is generated parametrically
// (that is, from a set of numbers, usually randomly seeded).  Because both
// back and front images may be rendering the same effect at the same time
// (but with different parameters), a distinct block of parameter memory is
// required for each image.  The 'fxVars' array is a two-dimensional array
// of integers, where the major axis is either 0 or 1 to represent the two
// images, while the minor axis holds 50 elements -- this is working scratch
// space for the effect code to preserve its "state."  The meaning of each
// element is generally unique to each rendering effect, but the first element
// is most often used as a flag indicating whether the effect parameters have
// been initialized yet.  When the back/front image indexes swap at the end of
// each transition, the corresponding set of fxVars, being keyed to the same
// indexes, are automatically carried with them.

// Solid color from green to red depending on frustration level.
void renderEffect00(byte idx) {
  // Only needs to be rendered once, when effect is initialized:
  if(fxVars[idx][0] == 0) {
    // Color starts at green, and transitiions to red depensing on frustration level.
    // Brightness is controlled by the value param. 128 is full bright. 
    long color = hsv2rgb(460 - frustration*(460/maxFrustration), 255, 96);
    byte *ptr = &imgData[idx][0];
    for(int i=0; i<numPixels; i++) {
      *ptr++ = color >> 16; 
      *ptr++ = color >> 8; 
      *ptr++ = color;
    }
    fxVars[idx][0] = 1; // Effect initialized
  }
}

// Render all black pixels (LEDs off). Used for a fade target
void renderEffect01(byte idx) {
  // Only needs to be rendered once, when effect is initialized:
  if(fxVars[idx][0] == 0) {
    byte *ptr = &imgData[idx][0];
    for(int i=0; i<numPixels; i++) {
      *ptr++ = 0; 
      *ptr++ = 0; 
      *ptr++ = 0;
    }
    fxVars[idx][0] = 1; // Effect initialized
  }
}


// ---------------------------------------------------------------------------
// Alpha channel effect rendering functions.  Like the image rendering
// effects, these are typically parametrically-generated...but unlike the
// images, there is only one alpha renderer "in flight" at any given time.
// So it would be okay to use local static variables for storing state
// information...but, given that there could end up being many more render
// functions here, and not wanting to use up all the RAM for static vars
// for each, a third row of fxVars is used for this information.

// Simplest alpha effect: fade entire strip over duration of transition.
void renderAlpha00(void) {
  byte fade = 255L * tCounter / transitionTime;
  for(int i=0; i<numPixels; i++) alphaMask[i] = fade;
}

// ---------------------------------------------------------------------------
// Assorted fixed-point utilities below this line.  Not real interesting.

// Gamma correction compensates for our eyes' nonlinear perception of
// intensity.  It's the LAST step before a pixel value is stored, and
// allows intermediate rendering/processing to occur in linear space.
// The table contains 256 elements (8 bit input), though the outputs are
// only 7 bits (0 to 127).  This is normal and intentional by design: it
// allows all the rendering code to operate in the more familiar unsigned
// 8-bit colorspace (used in a lot of existing graphics code), and better
// preserves accuracy where repeated color blending operations occur.
// Only the final end product is converted to 7 bits, the native format
// for the LPD8806 LED driver.  Gamma correction and 7-bit decimation
// thus occur in a single operation.
PROGMEM prog_uchar gammaTable[]  = {
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,
  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,
  2,  2,  2,  2,  2,  3,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,
  4,  4,  4,  4,  5,  5,  5,  5,  5,  6,  6,  6,  6,  6,  7,  7,
  7,  7,  7,  8,  8,  8,  8,  9,  9,  9,  9, 10, 10, 10, 10, 11,
  11, 11, 12, 12, 12, 13, 13, 13, 13, 14, 14, 14, 15, 15, 16, 16,
  16, 17, 17, 17, 18, 18, 18, 19, 19, 20, 20, 21, 21, 21, 22, 22,
  23, 23, 24, 24, 24, 25, 25, 26, 26, 27, 27, 28, 28, 29, 29, 30,
  30, 31, 32, 32, 33, 33, 34, 34, 35, 35, 36, 37, 37, 38, 38, 39,
  40, 40, 41, 41, 42, 43, 43, 44, 45, 45, 46, 47, 47, 48, 49, 50,
  50, 51, 52, 52, 53, 54, 55, 55, 56, 57, 58, 58, 59, 60, 61, 62,
  62, 63, 64, 65, 66, 67, 67, 68, 69, 70, 71, 72, 73, 74, 74, 75,
  76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91,
  92, 93, 94, 95, 96, 97, 98, 99,100,101,102,104,105,106,107,108,
  109,110,111,113,114,115,116,117,118,120,121,122,123,125,126,127
};

// This function (which actually gets 'inlined' anywhere it's called)
// exists so that gammaTable can reside out of the way down here in the
// utility code...didn't want that huge table distracting or intimidating
// folks before even getting into the real substance of the program, and
// the compiler permits forward references to functions but not data.
inline byte gamma(byte x) {
  return pgm_read_byte(&gammaTable[x]);
}

// Fixed-point colorspace conversion: HSV (hue-saturation-value) to RGB.
// This is a bit like the 'Wheel' function from the original strandtest
// code on steroids.  The angular units for the hue parameter may seem a
// bit odd: there are 1536 increments around the full color wheel here --
// not degrees, radians, gradians or any other conventional unit I'm
// aware of.  These units make the conversion code simpler/faster, because
// the wheel can be divided into six sections of 256 values each, very
// easy to handle on an 8-bit microcontroller.  Math is math, and the
// rendering code elsehwere in this file was written to be aware of these
// units.  Saturation and value (brightness) range from 0 to 255.
long hsv2rgb(long h, byte s, byte v) {
  byte r, g, b, lo;
  int  s1;
  long v1;

  // Hue
  h %= 1536;           // -1535 to +1535
  if(h < 0) h += 1536; //     0 to +1535
  lo = h & 255;        // Low byte  = primary/secondary color mix
  switch(h >> 8) {     // High byte = sextant of colorwheel
  case 0 : 
    r = 255     ; 
    g =  lo     ; 
    b =   0     ; 
    break; // R to Y
  case 1 : 
    r = 255 - lo; 
    g = 255     ; 
    b =   0     ; 
    break; // Y to G
  case 2 : 
    r =   0     ; 
    g = 255     ; 
    b =  lo     ; 
    break; // G to C
  case 3 : 
    r =   0     ; 
    g = 255 - lo; 
    b = 255     ; 
    break; // C to B
  case 4 : 
    r =  lo     ; 
    g =   0     ; 
    b = 255     ; 
    break; // B to M
  default: 
    r = 255     ; 
    g =   0     ; 
    b = 255 - lo; 
    break; // M to R
  }

  // Saturation: add 1 so range is 1 to 256, allowig a quick shift operation
  // on the result rather than a costly divide, while the type upgrade to int
  // avoids repeated type conversions in both directions.
  s1 = s + 1;
  r = 255 - (((255 - r) * s1) >> 8);
  g = 255 - (((255 - g) * s1) >> 8);
  b = 255 - (((255 - b) * s1) >> 8);

  // Value (brightness) and 24-bit color concat merged: similar to above, add
  // 1 to allow shifts, and upgrade to long makes other conversions implicit.
  v1 = v + 1;
  return (((r * v1) & 0xff00) << 8) |
    ((g * v1) & 0xff00)       |
    ( (b * v1)           >> 8);
}

// The fixed-point sine and cosine functions use marginally more
// conventional units, equal to 1/2 degree (720 units around full circle),
// chosen because this gives a reasonable resolution for the given output
// range (-127 to +127).  Sine table intentionally contains 181 (not 180)
// elements: 0 to 180 *inclusive*.  This is normal.

PROGMEM prog_char sineTable[181]  = {
  0,  1,  2,  3,  5,  6,  7,  8,  9, 10, 11, 12, 13, 15, 16, 17,
  18, 19, 20, 21, 22, 23, 24, 25, 27, 28, 29, 30, 31, 32, 33, 34,
  35, 36, 37, 38, 39, 40, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,
  52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67,
  67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 77, 78, 79, 80, 81,
  82, 83, 83, 84, 85, 86, 87, 88, 88, 89, 90, 91, 92, 92, 93, 94,
  95, 95, 96, 97, 97, 98, 99,100,100,101,102,102,103,104,104,105,
  105,106,107,107,108,108,109,110,110,111,111,112,112,113,113,114,
  114,115,115,116,116,117,117,117,118,118,119,119,120,120,120,121,
  121,121,122,122,122,123,123,123,123,124,124,124,124,125,125,125,
  125,125,126,126,126,126,126,126,126,127,127,127,127,127,127,127,
  127,127,127,127,127
};
