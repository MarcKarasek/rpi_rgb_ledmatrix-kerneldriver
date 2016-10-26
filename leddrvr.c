/*
 *  leddrvr.cc - Led Panel Driver Kernel Module.
 */
#include <linux/module.h>   /* Needed by all modules */
#include <linux/kernel.h>   /* Needed for KERN_INFO */
#include <linux/proc_fs.h>  /* Proc FS filesystem */
#include <linux/fs.h> /* Device File code*/
#include <linux/cdev.h> /* Character Device - Led is a char device */
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <mach/platform.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include "led.h"
#include "kmod_common.h"
#include "cie1931.h"


/*
 * Our parameters which can be set at load time.
 */
 enum {
  kBitPlanes = 11  // maximum usable bitplanes.
};
int led_major =   LED_MAJOR;
int led_minor =   0;
int rows = 32;
int chain = 1;
bool large_display = false;
int write_cycles = 0;
int board = 2;
int help = 0;

// Parameters below are only for imalive()
int pwm_bits = kBitPlanes;
bool do_luminance_correct = true;
int double_rows;
int columns;


// module/device stuff
#define LED_DEVICE_NAME "gpioleddrvr"

struct led_dev {
	uint32_t alive_counter;  // Counter to pause between R/G/B Alive Screens
	struct mutex alive_mutex; /* mutual exclusion semaphore for i'm alive */
	struct cdev cdev;	  /* Char device structure		*/
};
int devno;

struct class *led_class = NULL;
struct device *leddevice = NULL;

struct led_dev *led_device = NULL;

int led_nr_devs = LED_NR_DEVS;	/* number of bare led devices */

static const long kBaseTimeNanos = 200;

volatile uint32_t *freeRunTimer = NULL; // GPIO may override on startup

module_param(led_major, int, S_IRUGO);
module_param(led_minor, int, S_IRUGO);
module_param(pwm_bits, int, S_IRUGO);
module_param(rows, int, S_IRUGO);
module_param(chain, int, S_IRUGO);
module_param(write_cycles, int, S_IRUGO);
module_param(large_display, bool, S_IRUGO);
module_param(do_luminance_correct, bool, S_IRUGO);
module_param(board, int, S_IRUGO);
module_param(help, int, S_IRUGO);

MODULE_AUTHOR("Marc Karasek");
MODULE_LICENSE("GPL v2");


// GPIO setup macros. Always use INP_GPIO(x) before using OUT_GPIO(x) or SET_GPIO_ALT(x,y)
#define INP_GPIO(g) *(gpio_port_+((g)/10)) &= ~(7<<(((g)%10)*3))
#define OUT_GPIO(g) *(gpio_port_+((g)/10)) |=  (1<<(((g)%10)*3))
#define SET_GPIO_ALT(g,a) *(gpio+(((g)/10))) |= (((a)<=3?(a)+4:(a)==4?3:2)<<(((g)%10)*3))

/*static*/ const uint32_t kValidBits
= ((1 <<  0) | (1 <<  1) | // Revision 1 accessible
   (1 <<  2) | (1 <<  3) | // Revision 2 accessible
   (1 <<  4) | (1 <<  7) | (1 << 8) | (1 <<  9) |
   (1 << 10) | (1 << 11) | (1 << 14) | (1 << 15)| (1 <<17) | (1 << 18)|
   (1 << 22) | (1 << 23) | (1 << 24) | (1 << 25)| (1 << 27)
// Add support for A+/B+!
 | (1 <<  5) | (1 <<  6) | (1 << 12) | (1 << 13) | (1 << 16) |
   (1 << 19) | (1 << 20) | (1 << 21) | (1 << 26)
);


uint32_t output_bits_ = 0;
volatile uint32_t *gpio_port_ = NULL;
uint32_t writeCycles = 0;

dev_t led_dev = 0;

static bool initialized = false;

struct task_struct *alive;
union IoBits *bitplane_buffer_ptr;

struct resource * mem_gpio;
char *gpio_map;
uint32_t gpio_base;

// Set the bits that are '1' in the output. Leave the rest untouched.
void SetBits(uint32_t value) {
#ifdef GPIO_SYSFS
    uint32_t b;
    for (b = 0; b <= 27; ++b)
        if (value & (1 << b))
            gpio_set_value(b, true);
#else
    volatile uint8_t i = writeCycles;
    do {
      gpio_port_[0x1C / sizeof(uint32_t)] = value;
    } while(--i);
#endif
}

// Clear the bits that are '1' in the output. Leave the rest untouched.
void ClearBits(uint32_t value) {
#ifdef GPIO_SYSFS
    uint32_t b;
    for (b = 0; b <= 27; ++b)
        if (value & (1 << b))
            gpio_set_value(b, false);
#else
    volatile uint8_t i = writeCycles;
    do {
      gpio_port_[0x28 / sizeof(uint32_t)] = value;
    } while(--i);
#endif
}


// Write all the bits of "value" mentioned in "mask". Leave the rest untouched.
void WriteMaskedBits(uint32_t value, uint32_t mask) {
    // Writing a word is two operations. The IO is actually pretty slow, so
    // this should probably  be unnoticable.
    ClearBits(~value & mask);
    SetBits(value & mask);
  }

void Write(uint32_t value) { WriteMaskedBits(value, output_bits_); }

union IoBits * ValueAt(int double_row, int column, int bit)
{
  return &bitplane_buffer_ptr[ double_row * (columns * kBitPlanes) + bit * columns + column ];
}

uint32_t InitOutputs(uint32_t outputs)
{
    uint32_t b;
#ifdef GPIO_SYSFS
    static bool gpiooff;
    for (b = 0; b <= 27; ++b)
    {
        if (outputs & (1 << b))
        {
            if(gpio_is_valid(b))
            {
                gpio_request(b, "sysfs");
                gpio_direction_output(b, gpiooff);
                gpio_export(b, false);
            }
        }
    }
    output_bits_ = outputs;
    return output_bits_;
#else

  if (gpio_port_ == NULL) {
    printk(KERN_EMERG  "Attempt to init outputs but initialized.\n");
    return 0;
  }
  outputs &= kValidBits;   // Sanitize input.
  output_bits_ = outputs;
  for (b = 0; b <= 27; ++b) {
    if (outputs & (1 << b)) {
      INP_GPIO(b);   // for writing, we first need to set as input.
      OUT_GPIO(b);
    }
  }
  return output_bits_;
#endif
}

void InitGPIO( void )
{
  // Tell GPIO about all bits we intend to use.
  union IoBits b;
  uint32_t result;
  b.raw = 0;
#ifdef ADAFRUIT_RGBMATRIX_HAT
  b.bits.output_enable = 1;
  b.bits.clock = 1;
#else
  b.bits.output_enable_rev1 = b.bits.output_enable_rev2 = 1;
  b.bits.clock_rev1 = b.bits.clock_rev2 = 1;
#endif

  b.bits.strobe = 1;
  b.bits.r1 = b.bits.g1 = b.bits.b1 = 1;
  b.bits.r2 = b.bits.g2 = b.bits.b2 = 1;
#ifdef ADAFRUIT_RGBMATRIX_HAT
  b.bits.a = b.bits.b = b.bits.c = b.bits.d = 1;
#else
  b.bits.row = 0x0f;
#endif
  // Initialize outputs, make sure that all of these are supported bits.
  result = InitOutputs(b.raw);
#ifdef LED_DEBUG
  printk(KERN_EMERG "Result: 0x%X v 0x%X\n", result, b.raw);
#endif
}


int gpio_init(void)
{
#ifndef GPIO_SYSFS
  if(board == 2) { // Raspberry Pi 2?
    gpio_base   = 0x3F000000 + 0x200000; // GPIO base addr for Pi 2
    writeCycles = 2;

  // All other Pi Version here
  }
  else
  {
    gpio_base   = 0x20000000 + 0x200000; // " for Pi 1
    writeCycles = 1;
  }
#endif
// On PI the GPIO region is already requested by the bcm2835_gpiomem driver
// So we will skip the request/release and just ioremap it.
#ifdef BCMGPIO_NOT
  mem_gpio = request_mem_region(gpio_base, BLOCK_SIZE, "PiGPIO");
  if(mem_gpio == NULL)
  {
      printk(KERN_EMERG "Failed to allocate gpio resource\n");
      return 1;
  }
#endif

#ifdef GPIO_SYSFS
  printk(KERN_EMERG "No ioremap using gpio_sysfs instead\n");
#else
  gpio_map = ioremap(gpio_base, BLOCK_SIZE);
  if (gpio_map == NULL)
  {
        printk(KERN_EMERG "Failed to ioremap gpio\n");
        return 1;
  }

  gpio_port_ = (volatile uint32_t *)gpio_map;
#endif

  return 0;

}

/*
 * Open and close
 */

int led_open(struct inode *inode, struct file *filp)
{
    if(initialized == false)
    {
#ifdef LED_DEBUG
        // Any stuff needed to be done for open goes here
        printk(KERN_EMERG "Led Driver Opened\n");
#endif
    }
    initialized = true;
    return 0;          /* success */
}

int led_release(struct inode *inode, struct file *filp)
{
    // Turn off anything we need to turn off.
    initialized = false;
#ifdef LED_DEBUG
    printk(KERN_EMERG "Led Driver Closed\n");
#endif
	return 0;
}

/*
 * The ioctl() implementation -- This is were the work gets done!
 */
long led_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{

	int err = 0;
	int retval = 0;
    union IoBits * valueat_ret;

	/*
	 * extract the type and number bitfields, and don't decode
	 * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
	 */
	if (_IOC_TYPE(cmd) != LED_IOC_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd) > LED_IOC_MAXNR) return -ENOTTY;
	/*
	 * the direction is a bitmask, and VERIFY_WRITE catches R/W
	 * transfers. `Type' is user-oriented, while
	 * access_ok is kernel-oriented, so the concept of "read" and
	 * "write" is reversed
	 */
	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err =  !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
	if (err) return -EFAULT;

	switch(cmd) {
	  case LED_CLRBITS:
          // grab the data
          retval = __get_user(value, (int __user *)arg);
          if(retval)
          {
              printk(KERN_EMERG  "Parameter Error: LED_CLRBITS\n");
              break;
          }
          ClearBits(value);
          break;

	  case LED_SETBITS: /* Get: arg is pointer to result */
          // grab the data
          retval = __get_user(value, (int __user *)arg);
          if(retval)
          {
              printk(KERN_EMERG  "Parameter Error: LED_SETBITS\n");
              break;
          }
          SetBits(value);
          break;

    case LED_WRMSKBITS:
          // Grab data
          retval = copy_from_user(&set_bits_vals, (void __user *)arg, sizeof(struct set_bits));
          if(retval)
          {
              printk(KERN_EMERG  "Parameter Error: LED_WRMSKBITS %d\n", retval);
              break;
          }
          WriteMaskedBits(set_bits_vals.value, set_bits_vals.mask);
          break;


	  case LED_VALUEAT:
          // Grab data
          retval = copy_from_user(&vat, (void __user *)arg, sizeof(struct value_at));
          if(retval)
          {
              printk(KERN_EMERG  "Parameter Error: LED_VALUEAT %d\n", retval);
              break;
          }
          valueat_ret = ValueAt(vat.dblrow, vat.column, vat.bit);
          // Put the value back in the struct.
          memcpy(&(vat.iobits), valueat_ret, sizeof(union IoBits));
          // Send it back to the user
          retval = copy_to_user((int __user *)arg, &vat, sizeof(struct value_at));
          if(retval)
          {
              printk(KERN_EMERG  "Copy to User %d\n", retval);
          }
          break;

	  default:  /* redundant, as cmd was checked against MAXNR */
		return -ENOTTY;
	}
	return retval;

}

//*************************************************************************
// Everything from here down to the module init() function is for the imalive
// function.  This is not needed for the functioning of the driver.

// This function in userland can do FP Math. In kernel land we cannot.
// So for the imalive() we use a hardcoded cie[] table.  This is generated
// form the cie1931.py python scipt.  Baseo on the values used in the userland
// application.
uint16_t MapColor(uint8_t c)
{
#ifdef INVERSE_RGB_DISPLAY_COLORS
#  define COLOR_OUT_BITS(x) (x) ^ 0xffff
#else
#  define COLOR_OUT_BITS(x) (x)
#endif
  if (do_luminance_correct)
  {
    // Do the lookup in the generated cie1931 table
    return COLOR_OUT_BITS(cie[c]);
  } else {
    enum {shift = kBitPlanes - 8};  //constexpr; shift to be left aligned.
    return COLOR_OUT_BITS((shift > 0) ? (c << shift) : (c >> -shift));
  }
#undef COLOR_OUT_BITS
}

void Clear( void )
{
#ifdef INVERSE_RGB_DISPLAY_COLORS
  Fill(0, 0, 0);
#else
  memset(bitplane_buffer_ptr, 0,
         sizeof(Io_Bits) * double_rows * columns * kBitPlanes);
#endif
}

void Fill(uint8_t r, uint8_t g, uint8_t b)
{
    int x, col, row;
    uint16_t mask;
    union IoBits plane_bits;
    union IoBits *row_data;
    const uint16_t red   = MapColor(r);
    const uint16_t green = MapColor(g);
    const uint16_t blue  = MapColor(b);

  for (x = kBitPlanes - pwm_bits; x < kBitPlanes; ++x)
  {
    mask = (uint16_t)(1 << x);
    plane_bits.raw = 0;
    plane_bits.bits.r1 = plane_bits.bits.r2 = (red & mask) == mask;
    plane_bits.bits.g1 = plane_bits.bits.g2 = (green & mask) == mask;
    plane_bits.bits.b1 = plane_bits.bits.b2 = (blue & mask) == mask;
    for (row = 0; row < double_rows; ++row)
    {
      row_data = ValueAt(row, 0, x);
      for (col = 0; col < columns; ++col)
      {
        (row_data++)->raw = plane_bits.raw;
      }
    }
  }
}

static void sleep_nanos(long nanos)
{
  volatile int i;
  if (nanos > 28000) {
      ndelay(nanos);
  } else {
    // The following loop is determined empirically
    for(i = nanos >> 3; i--; );
  }
}


void DumpToMatrix( void )
{
  union IoBits color_clk_mask;   // Mask of bits we need to set while clocking in.
  union IoBits row_mask;
  union IoBits clock, output_enable, strobe, row_address;
  uint8_t d_row;
  int pwm_to_show;
  int b, col;
  union IoBits *row_data;
  union IoBits out;

  color_clk_mask.bits.r1 = color_clk_mask.bits.g1 = color_clk_mask.bits.b1 = 1;
  color_clk_mask.bits.r2 = color_clk_mask.bits.g2 = color_clk_mask.bits.b2 = 1;
#ifdef ADAFRUIT_RGBMATRIX_HAT
  color_clk_mask.bits.clock = 1;
#else
  color_clk_mask.bits.clock_rev1 = color_clk_mask.bits.clock_rev2 = 1;
#endif


#ifdef ADAFRUIT_RGBMATRIX_HAT
  row_mask.bits.a = row_mask.bits.b = row_mask.bits.c = row_mask.bits.d = 1;
#else
  row_mask.bits.row = 0x0f;
#endif

#ifdef ADAFRUIT_RGBMATRIX_HAT
  clock.bits.clock = 1;
  output_enable.bits.output_enable = 1;
#else
  clock.bits.clock_rev1 = clock.bits.clock_rev2 = 1;
  output_enable.bits.output_enable_rev1 = 1;
  output_enable.bits.output_enable_rev2 = 1;
#endif
  strobe.bits.strobe = 1;

  pwm_to_show = pwm_bits;  // Local copy, might change in process.
  for (d_row = 0; d_row < double_rows; ++d_row) {
#ifdef ADAFRUIT_RGBMATRIX_HAT
    row_address.bits.a = d_row;
    row_address.bits.b = d_row >> 1;
    row_address.bits.c = d_row >> 2;
    row_address.bits.d = d_row >> 3;
#else
    row_address.bits.row = d_row;
#endif
    WriteMaskedBits(row_address.raw, row_mask.raw);  // Set row address

    // Rows can't be switched very quickly without ghosting, so we do the
    // full PWM of one row before switching rows.
    for (b = kBitPlanes - pwm_to_show; b < kBitPlanes; ++b) {
      row_data = ValueAt(d_row, 0, b);
      // We clock these in while we are dark. This actually increases the
      // dark time, but we ignore that a bit.
      for (col = 0; col < columns; ++col) {
        memcpy( &out, row_data, sizeof(Io_Bits));
        row_data++;
        WriteMaskedBits(out.raw, color_clk_mask.raw);  // col + reset clock
        SetBits(clock.raw);               // Rising edge: clock color in.
      }

      ClearBits(color_clk_mask.raw);    // clock back to normal.

      SetBits(strobe.raw);   // Strobe in the previously clocked in row.
      ClearBits(strobe.raw);

      // Now switch on for the sleep time necessary for that bit-plane.
      ClearBits(output_enable.raw);
      sleep_nanos(kBaseTimeNanos << b);
      SetBits(output_enable.raw);
    }
  }
}

#ifdef IMALIVE
// thread to be called to tell world driver is alive
int imalive(void *unused)
{
#ifdef LED_DEBUG
    printk(KERN_EMERG "imalive running\n");
#endif
    while(!kthread_should_stop())
    {
        DumpToMatrix();
        mutex_lock(&(led_device->alive_mutex));
        led_device->alive_counter ++;
        mutex_unlock(&(led_device->alive_mutex));
    }
#ifdef LED_DEBUG
    printk(KERN_EMERG "imalive stopped\n");
#endif
    return 0;

}

bool alive_check (void)
{
    bool cont;
    mutex_lock(&(led_device->alive_mutex));
    if(led_device->alive_counter > 0x100)
    {
        cont = true;
        led_device->alive_counter = 0;
    }
    else
        cont = false;
    mutex_unlock(&(led_device->alive_mutex));
    return cont;
}

struct file_operations led_fops = {
	.owner =    THIS_MODULE,
	.llseek =   NULL, // No reason to seek
	.read =     NULL, // No read for led
	.write =    NULL, // No write for led
	.unlocked_ioctl =    led_ioctl,
	.open =     led_open,
	.release =  led_release,
};
#endif // #ifdef IMALIVE

int init_module(void)
{
	int result;

    printk(KERN_INFO "Led Panel Driver 1.0\n");
    {
        if(help)
        {
            printk(KERN_EMERG "Module Parameters :\n");
            printk(KERN_EMERG "=================\n" );
            printk(KERN_EMERG "led_major Specify a major number for the device.  Default is to ask for a dynamic one\n");
            printk(KERN_EMERG "led_minor Specify the minor dev number to use. Default is 0\n");
            printk(KERN_EMERG "rows : Number of rows in the display. Must be 16 or 32 (Default 32)\n");
            printk(KERN_EMERG "chain : Number of chained LED Panels (Default 1)\n");
            printk(KERN_EMERG "write_cycles (Default 0)\n");
            printk(KERN_EMERG "large_display : true = 32 x 4: false 32 x 1 (Default false)\n");
            printk(KERN_EMERG "********************************************************************************************\n");
            printk(KERN_EMERG "Parameters below are only fo the imalive() funciton. They are not used in the normal driver.\n");
            printk(KERN_EMERG "********************************************************************************************\n");
            printk(KERN_EMERG "pwm_bits Specify the pwm bits to use must be between 1-11\n");
            printk(KERN_EMERG "do_luminance_correct (Default true\n");
            printk(KERN_EMERG "board : 1 = Pi1 Rev2 ,  2 = Pi 2 (default) \n");
            printk(KERN_EMERG "help : Print this help\n");
            printk(KERN_EMERG "=================\n" );
            printk(KERN_EMERG "NOTE: kernel Module is not loaded at this time!\n");
            return 1;
        }
    }

/*
 * Get a range of minor numbers to work with, asking for a dynamic
 * major unless directed otherwise at load time.
 */
	if (led_major) {
		led_dev = MKDEV(led_major, led_minor);
		result = register_chrdev_region(led_dev, led_nr_devs, "gpioleddrvr");
	} else {
		result = alloc_chrdev_region(&led_dev, led_minor, led_nr_devs, "gpioleddrvr");
		led_major = MAJOR(led_dev);
	}
	if (result < 0) {
		printk(KERN_EMERG "gpioleddrvr: can't get major %d\n", led_major);
		return result;
	}

    // Create the class -- first step in creating the /dev device.
    led_class = class_create(THIS_MODULE, LED_DEVICE_NAME);
	if (IS_ERR(led_class))
    {
		result = PTR_ERR(led_class);
        unregister_chrdev_region(led_dev, 1);
		return result;
    }

    // Malloc a region of memory for the shared struct.
    led_device = kmalloc(led_nr_devs * sizeof(struct led_dev), GFP_KERNEL);
	if (!led_device) {
		result = -ENOMEM;
        class_destroy(led_class);
        unregister_chrdev_region(led_dev, 1);
		return result;
	}
    memset(led_device, 0, led_nr_devs * sizeof(struct led_dev));

    // get a dev number for the device
    devno = MKDEV(led_major, led_minor);
    // init the device
    cdev_init(&led_device->cdev, &led_fops);
	led_device->cdev.owner = THIS_MODULE;
	led_device->cdev.ops = &led_fops;
    // Add it to the system
	result = cdev_add (&led_device->cdev, devno, 1);
	if (result)
    {
		printk(KERN_EMERG "Error cdev_add() %d ", result);
        class_destroy(led_class);
        unregister_chrdev_region(led_dev, 1);
        return result;
    }

    // And the final most important step -- tell the kernel to create the /dev/gpioleddriver device.
    leddevice = device_create(led_class, NULL, /* no parent device */
                           devno, NULL, /* no additional data */
                           LED_DEVICE_NAME );
	if (IS_ERR(leddevice))
    {
		result = PTR_ERR(leddevice);
		printk(KERN_WARNING "Error %d while trying to create %s",
			result, LED_DEVICE_NAME);
        cdev_del(&led_device->cdev);
        class_destroy(led_class);
        unregister_chrdev_region(led_dev, 1);
        return result;
    }

    // Hardware init -- Uses params passed on insmod line.
    {
        // Assume 4 - 32 x 32 Display
        if(large_display)
        {
            rows = 32;
            chain = 4;
        }

        if  ( (rows != 16) && (rows != 32) )
        {
            printk(KERN_EMERG "Rows can either be 16 or 32\n");
            result = 1;
            goto fail;
        }

        if (chain < 1)
        {
            printk(KERN_EMERG "Chain outside usable range\n");
            result = 1;
            goto fail;
        }

        if (chain > 8)
        {
            printk(KERN_EMERG "That is a long chain. Expect some flicker.\n");
        }

        // Check for anything but default Pi1 Rev2
        if (board !=2 )
        {
            if ( (board > 2) || (board < 0) )
            {
                printk(KERN_EMERG "Invalid Board Type Must be one of:\n  1 = Pi1 ,  2 = Pi 2 (default)\n");
                result = 1;
                goto fail;
            }
        }

        if(pwm_bits != kBitPlanes)
        {
            if ((pwm_bits < 1) || pwm_bits > 11)
            {
                printk(KERN_EMERG "pwm_bits out of range (1-11)\n");
                result = 1;
                goto fail;
            }
            else
                printk(KERN_EMERG "pwm_bits chnaged to %d\n", pwm_bits);
        }

        // initialize the GPIO lines for the Led Driver
        if (gpio_init())
        {
            printk(KERN_EMERG "gpio_init failed\n");
            result = 1;
            goto fail;
        }
// open up syslog
#ifdef DEBUG
        openlog("LedDriver", LOG_NDELAY, LOG_USER );
#endif
// Imalive()
#ifdef IMALIVE
        // used by APIs to set/clear bits
        double_rows = rows /2;
        columns = chain * 32;

        // The frame-buffer is organized in bitplanes.
        // Highest level (slowest to cycle through) are double rows.
        // For each double-row, we store pwm-bits columns of a bitplane.
        // Each bitplane-column is pre-filled IoBits, of which the colors are set.
        // Of course, that means that we store unrelated bits in the frame-buffer,
        // but it allows easy access in the critical section.
        //  NOTE:  This is only used for imalive(), once we are loaded it is freed up..
        bitplane_buffer_ptr = kmalloc(sizeof(Io_Bits) * double_rows * columns * kBitPlanes, GFP_KERNEL);

        // Clear out the buffer
        Clear();
#endif
        // Setup the GPIOs
        InitGPIO();

        // If writecycles has been set on insmod line then change it,
        // it has been set based on board in InitGPIO()
        if( write_cycles)
            writeCycles = write_cycles;

// Imalive()
#ifdef IMALIVE
        alive = kthread_run(&imalive,NULL,"imalive");

        // This is the mutex used for imalive().
        // Once we are down with imalive() it is destroyed..
        mutex_init( &(led_device->alive_mutex) );

        // Fill with Red
#ifdef LED_DEBUG
        printk(KERN_EMERG "Red\n");
#endif
        Fill(0xff, 0x00, 0x00);
        while(!alive_check());
        Clear();
        while(!alive_check());

        // Fill with Green
#ifdef LED_DEBUG
        printk(KERN_EMERG "Green\n");
#endif
        Fill(0x00, 0xff, 0x00);
        while(!alive_check());
        Clear();
        while(!alive_check());

        // Fill with Blue
#ifdef LED_DEBUG
        printk(KERN_EMERG "Blue\n");
#endif
        Fill(0x00, 0x00, 0xFF);
        while(!alive_check());
        while(!alive_check());
        Clear();
        while(!alive_check());

        // Free Up resources from imalive

        // kill the thread
        kthread_stop(alive);

        // Free up the buffer used by imalive
        kfree(bitplane_buffer_ptr);

        // Destroy the mutex
        mutex_destroy( &(led_device->alive_mutex) );

#endif // #ifdef IMALIVE
    }
    return 0;

// failure condition.. Once we get far enough along in init.
// We need to unwind the same things.  So here we are...
fail:
    device_destroy(led_class, MKDEV(led_major, led_minor));
    cdev_del(&led_device->cdev);
    class_destroy(led_class);
    unregister_chrdev_region(led_dev, 1);
    return result;
}

void cleanup_module(void)
{
#ifndef GPIO_SYSFS
    iounmap(gpio_map);
// On PI2 the GPIO region is already requested by the bcm2835_gpiomem driver
// So we will skip the request/release and just ioremap it.
#ifdef BCMGPIO_NOT
    release_mem_region(gpio_base, BLOCK_SIZE);
#endif
#endif

    // Unwind and remove the /dev/gpioleddrvr device from the system
    device_destroy(led_class, MKDEV(led_major, led_minor));
    cdev_del(&led_device->cdev);
    kfree(led_device);
    class_destroy(led_class);
    // Unregister the char device
	unregister_chrdev_region(led_dev, 1);

    // Let the world know we are gone!
    printk(KERN_EMERG "Led Driver Module Unloaded\n");

}


