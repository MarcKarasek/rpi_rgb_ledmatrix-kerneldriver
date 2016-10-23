#ifndef _KMODCOMMON_H_
#define _KMODCOMMON_H_


#define DEVICE_PATH "/dev/gpioleddrvr"

  union IoBits {
#ifdef ADAFRUIT_RGBMATRIX_HAT
    struct {
      // These reflect the GPIO mapping. The Revision1 and Revision2 boards
      // have different GPIO mappings for 0/1 vs 3/4. Just use both.
      unsigned int unused1 : 4;             // 0-3
      unsigned int output_enable : 1;       // 4
      unsigned int r1 : 1;                  // 5
      unsigned int b1 : 1;                  // 6
      unsigned int unused2 : 5;             // 7-11
      unsigned int r2 : 1;                  // 12
      unsigned int g1 : 1;                  // 13
      unsigned int unused3 : 2;             // 14-15
      unsigned int g2 : 1;                  // 16
      unsigned int clock : 1;               // 17
      unsigned int unused4 : 2;             // 18-19
      unsigned int d : 1;                   // 20
      unsigned int strobe : 1;              // 21
      unsigned int a : 1;                   // 22
      unsigned int b2 : 1;                  // 23
      unsigned int unused5 : 2;             // 24-25
      unsigned int b : 1;                   // 26
      unsigned int c : 1;                   // 27
      unsigned int unused6 : 4;             // 28 - 31
    } bits;
#else
    struct {
      // These reflect the GPIO mapping. The Revision1 and Revision2 boards
      // have different GPIO mappings for 0/1 vs 3/4. Just use both.
      unsigned int output_enable_rev1 : 1;  // 0
      unsigned int clock_rev1 : 1;          // 1
      unsigned int output_enable_rev2 : 1;  // 2
      unsigned int clock_rev2  : 1;         // 3
      unsigned int strobe : 1;              // 4
      unsigned int unused2 : 2;             // 5..6
      unsigned int row : 4;                 // 7..10
      unsigned int unused3 : 6;             // 11..16
      unsigned int r1 : 1;                  // 17
      unsigned int g1 : 1;                  // 18
      unsigned int unused4 : 3;
      unsigned int b1 : 1;                  // 22
      unsigned int r2 : 1;                  // 23
      unsigned int g2 : 1;                  // 24
      unsigned int b2 : 1;                  // 25
      unsigned int unused5 : 6;              // 26-31
    } bits;
#endif
    uint32_t raw;
  } Io_Bits;


// Pass in set_pixel vlaues.
// This will be used for both the SetPixel() and Fill() APIs.
struct set_bits
{
    // netopcode only used for Web Client
    unsigned char  netopcode;
    unsigned char rsvd[3];
    uint32_t value;
    uint32_t mask;
};

struct set_bits set_bits_vals;

struct value_at
{
    union IoBits  iobits;
    int dblrow;
    int column;
    int bit;
};

struct value_at vat;

struct net_value
{
    unsigned char  netopcode;
    unsigned char rsvd[3];
    int value;
};

struct net_value nval;

int value;

/*
 * Ioctl definitions
 */

/* Use 'l' as magic number */
#define LED_IOC_MAGIC  'C'

#define LED_WRMSKBITS   _IOW(LED_IOC_MAGIC,  1, struct set_bits)
#define LED_CLRBITS     _IOW(LED_IOC_MAGIC,  2, int)
#define LED_SETBITS     _IOW(LED_IOC_MAGIC,  3, int)
#define LED_VALUEAT     _IOWR(LED_IOC_MAGIC, 4, struct value_at)
#define LED_IOC_MAXNR 4

#define NET_CLRBITS 0x10
#define NET_SETBITS 0x11
#define NET_WRMSKBITS 0x12

#endif //_KMODCOMMON_H_
