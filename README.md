# ThurdKind #

This controls an LDP8806 RGB light strip in patterns resembling the UFOs from 
some movie about aliens and mashed potatoes. This is based on the examples
from Adafruit's LDP8806 library (which this requires). 

https://github.com/adafruit/LPD8806

This was written with the teensy 2.0 microcontroller in mind, but should work
with others. By default it uses 86 LEDs, because this is what fit around my 
quadcopter. Some of the effects are tuned to run with this length, and in the
shape of the machine I put this on. 

It will cycle through several effects:

* Color fade. All LEDs a solid color. Slightly modified from the example. 
It will pick a nice HSV color, but leave out the saturation and only lower
the value a little to keep the colors nice and bright. 

* Chaser. A simple color chase with variable direction and width. 

* Sine wave chase. Again, slightly modified from the original to shift the hue up and down with amplitude to give more variety. They also move faster. 

* Close Encounter. Mimics the mother ship. 

* Quad colors. Each arm of the quad cycles through colors.

* Quad sine chase. A radial pulsing color chase, with a random sparkly effect. 
