// Header file for any special led defines/etc..

#ifndef _LED_H_
#define _LED_H_

//Turn this on for some debug printks
//#define LED_DEBUG 1
// Turns on the imalive() code.  Will flash the panel red/green/blue each for approx 2sec on module load
#define IMALIVE

#ifndef LED_MAJOR
#define LED_MAJOR 0   /* dynamic major by default */
#endif

#ifndef LED_NR_DEVS
#define LED_NR_DEVS 1    /* If you have more than 1 display */
#endif


#endif /* _LED_H_ */
