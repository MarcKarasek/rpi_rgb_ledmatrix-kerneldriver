#include "./webinterface/PracticalSocket.h"      // For UDPSocket and SocketException
#include <iostream>          // For cout and cerr
#include <cstdlib>           // For atoi()

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <math.h>
#include <asm/ioctl.h>
#include <sys/ioctl.h>
#include <linux/fs.h> /* Device File code*/

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>

#include "kmod_common.h"
#include "web_defines.h"
#include "cie1931.h"

using namespace std;

const int CMDMAX = 525;     // Biggest Buffer should be 500.. pad it a little

struct net_parameters client_params;
union IoBits *net_bitplane_buffer_ptr;
#ifdef RCV_BUFFER
union IoBits *net_bitplane_rcv_buffer_ptr;
#endif
struct set_bits *pset_bits_vals;
int *pval;

struct set_bits set_bits_vals;
int value;
int columns;
static const long kBaseTimeNanos = 100;
//static const long kBaseTimeNanos = 200;
pthread_t rcv_canvas_thread;
pthread_t canvas_thread;
int ret;

unsigned short LedSrvrPort;

enum {
  kBitPlanes = 11  // maximum usable bitplanes.
};
int double_rows;

// Filedescriptor for the /dev/gpioleddrvr
int fd;

void SetGPIO(void)
{
  // Open up the GPIO Led Device Driver
  fd = open("/dev/gpioleddrvr", O_RDWR);
  if (fd == -1)
  {
      printf("Error Opening Led Driver /dev/gpioleddrvr %x ", errno);
      exit(1);
  }
}

// Sleep function (used a lot) maybe this should be in a common hdr or something..
static void sleep_nanos(long nanos) {
  if (nanos > 28000) {
      struct timespec sleep_time = { 0, nanos - 20000 };
      nanosleep(&sleep_time, NULL);
  } else {
    // The following loop is determined empirically
    for(volatile int i = nanos >> 3; i--; );
  }
}

union IoBits * ValueAt(int double_row, int column, int bit)
{
  return &net_bitplane_buffer_ptr[ double_row * (columns * kBitPlanes) + bit * columns + column ];
}

// Function to Dump Canvas to local driver
void DumpToMatrix(int fd)
{
  IoBits color_clk_mask;   // Mask of bits we need to set while clocking in.
  color_clk_mask.bits.r1 = color_clk_mask.bits.g1 = color_clk_mask.bits.b1 = 1;
  color_clk_mask.bits.r2 = color_clk_mask.bits.g2 = color_clk_mask.bits.b2 = 1;
#ifdef ADAFRUIT_RGBMATRIX_HAT
  color_clk_mask.bits.clock = 1;
#else
  color_clk_mask.bits.clock_rev1 = color_clk_mask.bits.clock_rev2 = 1;
#endif

  IoBits row_mask;
#ifdef ADAFRUIT_RGBMATRIX_HAT
  row_mask.bits.a = row_mask.bits.b = row_mask.bits.c = row_mask.bits.d = 1;
#else
  row_mask.bits.row = 0x0f;
#endif

  IoBits clock, output_enable, strobe, row_address;
#ifdef ADAFRUIT_RGBMATRIX_HAT
  clock.bits.clock = 1;
  output_enable.bits.output_enable = 1;
#else
  clock.bits.clock_rev1 = clock.bits.clock_rev2 = 1;
  output_enable.bits.output_enable_rev1 = 1;
  output_enable.bits.output_enable_rev2 = 1;
#endif
  strobe.bits.strobe = 1;

  const int pwm_to_show = client_params.pwm_bits;  // Local copy, might change in process.
  for (uint8_t d_row = 0; d_row < double_rows; ++d_row) {
#ifdef ADAFRUIT_RGBMATRIX_HAT
    row_address.bits.a = d_row;
    row_address.bits.b = d_row >> 1;
    row_address.bits.c = d_row >> 2;
    row_address.bits.d = d_row >> 3;
#else
    row_address.bits.row = d_row;
#endif

    // Set row address
    set_bits_vals.mask = row_mask.raw;
    set_bits_vals.value = row_address.raw;
    ioctl(fd, LED_WRMSKBITS,&set_bits_vals);

    // Rows can't be switched very quickly without ghosting, so we do the
    // full PWM of one row before switching rows.
    for (int b = kBitPlanes - pwm_to_show; b < kBitPlanes; ++b) {
      IoBits *row_data = ValueAt(d_row, 0, b);

      // We clock these in while we are dark. This actually increases the
      // dark time, but we ignore that a bit.
      for (int col = 0; col < columns; ++col) {
        const IoBits &out = *row_data++;

        // col + reset clock
        set_bits_vals.mask = color_clk_mask.raw;
        set_bits_vals.value = out.raw;
        ioctl(fd, LED_WRMSKBITS,&set_bits_vals);

        // Rising edge: clock color in.
        value = clock.raw;
        ioctl(fd, LED_SETBITS,&value);
      }
      // clock back to normal.
      value = color_clk_mask.raw;
      ioctl(fd, LED_CLRBITS,&value);

      // Strobe in the previously clocked in row.
      value = strobe.raw;
      ioctl(fd, LED_SETBITS,&value);

      value = strobe.raw;
      ioctl(fd, LED_CLRBITS,&value);

      // Now switch on for the sleep time necessary for that bit-plane.
      value = output_enable.raw;
      ioctl(fd, LED_CLRBITS,&value);

      sleep_nanos(kBaseTimeNanos << b);
      value = output_enable.raw;
      ioctl(fd, LED_SETBITS,&value);
    }
  }
}

// TCP client handling function
void HandleTCPClient(TCPSocket *tcpsock) {
  int recvMsgSize;

#ifdef RCV_BUFFER
  while ((recvMsgSize = tcpsock->recv(net_bitplane_rcv_buffer_ptr, NET_BUFFER)) > 0) {} // Zero means end of transmission
#else
  while ((recvMsgSize = tcpsock->recv(net_bitplane_buffer_ptr, NET_BUFFER)) > 0) {} // Zero means end of transmission
#endif
  if(recvMsgSize != 0)
      cout<<"Buffer Rcv Error"<<endl;

  delete tcpsock;
}

// Thread to receive packets via TCP from Client
void *rcv_cnvs_thread(void * arg)
{
    cout<<"rcv_cnvs_thread enter"<<endl;
    try {
        TCPServerSocket servSock(LedSrvrPort);     // Server Socket object
        for (;;) {   // Run forever
             HandleTCPClient(servSock.accept());       // Wait for a client to connect
#ifdef RCV_BUFFER
             // Copy the rcved buffer to the real one.
             memcpy(net_bitplane_buffer_ptr, net_bitplane_rcv_buffer_ptr, NET_BUFFER);
             memset(net_bitplane_rcv_buffer_ptr, 0x00, NET_BUFFER);
#endif
        }
    } catch (SocketException &e) {
        cerr << e.what() << endl;
        exit(1);
    }
    pthread_exit(NULL);
}


// thread to dump the canvas sent by the client to the Led Display
void *cnvs_thread(void * arg)
{
    cout<<"cnvs_thread enter"<<endl;
    for(;;)
    {
        DumpToMatrix(fd);
//        usleep(100* 1000);
    }
    pthread_exit(NULL);
}

// We use a hardcoded cie[] table.  This is generated
// from the cie1931.py python scipt.  Baseo on the values used in the original demo.
uint16_t MapColor(uint8_t c)
{
#ifdef INVERSE_RGB_DISPLAY_COLORS
#  define COLOR_OUT_BITS(x) (x) ^ 0xffff
#else
#  define COLOR_OUT_BITS(x) (x)
#endif
  if (client_params.do_luminance_correct)
  {
    // Do the lookup in the generated cie1931 table
    return COLOR_OUT_BITS(cie[c]);
  } else {
    enum {shift = kBitPlanes - 8};  //constexpr; shift to be left aligned.
    return COLOR_OUT_BITS((shift > 0) ? (c << shift) : (c >> -shift));
  }
#undef COLOR_OUT_BITS
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

  for (x = kBitPlanes - client_params.pwm_bits; x < kBitPlanes; ++x)
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

void Clear( void )
{
#ifdef INVERSE_RGB_DISPLAY_COLORS
  Fill(0, 0, 0);
#else
  memset(net_bitplane_buffer_ptr, 0,
         sizeof(IoBits) * double_rows * columns * kBitPlanes);
#endif
}


int main(int argc, char *argv[]) {

  if (argc != 2) {                  // Test for correct number of parameters
    cerr << "Usage: " << argv[0] << " <Server Port>" << endl;
    exit(1);
  }

  LedSrvrPort = atoi(argv[1]);     // First arg:  local port

  SetGPIO();

  try {
    UDPSocket sock(LedSrvrPort);

    char LedCMDBuff[CMDMAX];         // Buffer for rcv cmd
    int recvCmdSz;                  // Size of received message
    string sourceAddress;             // Address of datagram source
    unsigned short sourcePort;        // Port of datagram source
    cout << "Starting Server " << endl;

    // Run Until ^C or Kill Message from Client..
    for (;;)
    {
      // Block until receive message from a client
      recvCmdSz = sock.recvFrom(LedCMDBuff, CMDMAX, sourceAddress, sourcePort);
      // Check for an empty frame if we get one just loop back to the recvFrom() call
      if (recvCmdSz == 0)
      {
          continue;
      }

 //     cout << "Received packet from " << sourceAddress << ":"<< sourcePort << endl;

      switch(LedCMDBuff[0])
      {
      case NET_WRMSKBITS :
          // cout<<"NET_WRMSKBITS"<<endl;
          pset_bits_vals = (struct set_bits *)(&LedCMDBuff[0]);
          // cout<<pset_bits_vals->value<<endl;
          // cout<<pset_bits_vals->mask<<endl;
          ioctl(fd, LED_WRMSKBITS, pset_bits_vals);
          break;
      case NET_SETBITS :
          // cout<<"NET_SETBITS"<<endl;
          pval = (int *)(&LedCMDBuff[4]);
          // cout<<*pval<<endl;
          ioctl(fd, LED_SETBITS,pval);
          break;
      case NET_CLRBITS :
          // cout<<"NET_CLRBITS"<<endl;
          pval = (int *)(&LedCMDBuff[4]);
          // cout<<*pval<<endl;
          ioctl(fd, LED_CLRBITS,pval);
          break;
      case NET_CLIENT_DC :
          cout<<"DC Server -- Rcvd DC Cmd from Client"<<endl;
          Clear();
          // Wait for the threads to exit
          cout<<"Wait on threads"<<endl;
          pthread_cancel(canvas_thread);
          pthread_cancel(rcv_canvas_thread);
          pthread_join(canvas_thread, NULL);
          pthread_join(rcv_canvas_thread, NULL);
          cout<<"All threads exited"<<endl;
          // Reset thread_exit and shutdownsrvr variables
          delete [] net_bitplane_buffer_ptr;
#ifdef RCV_BUFFER
          delete [] net_bitplane_rcv_buffer_ptr;
#endif
          break;
      case NET_KILLSRVR :
          cout<<"Stopping Server -- Rcvd Stop Cmd from Client"<<endl;
          Clear();
          // Wait for the threads to exit
          cout<<"Wait on threads"<<endl;
          pthread_cancel(canvas_thread);
          pthread_cancel(rcv_canvas_thread);
          pthread_join(canvas_thread, NULL);
          pthread_join(rcv_canvas_thread, NULL);
          cout<<"All threads exited"<<endl;
          delete [] net_bitplane_buffer_ptr;
#ifdef RCV_BUFFER
          delete [] net_bitplane_rcv_buffer_ptr;
#endif
          close(fd);
          exit(0);
          break;
      case NET_INIT_PARAMS :
          cout<<"Setting Client Parameters"<<endl;
          // Copy the client parameters to our local copy...
          memcpy(&client_params, (&LedCMDBuff[0]), sizeof(net_parameters));
          cout<<"sizeof net_parameters "<<sizeof(net_parameters)<<endl;
          cout<<"runtime_seconds = "<<client_params.runtime_seconds<<endl;
          cout<<"rows = "<<client_params.rows<<endl;
          cout<<"chain = "<<client_params.chain<<endl;
          cout<<"scroll_ms = "<<client_params.scroll_ms<<endl;
          cout<<"pwm_bits = "<<client_params.pwm_bits<<endl;
          cout<<"large_display = "<<client_params.large_display<<endl;
          cout<<"do_luminance_correct = "<<client_params.do_luminance_correct<<endl;

          double_rows = (client_params.rows / 2);
          columns = client_params.chain * 32;

           if (!(client_params.pwm_bits >= 0))
           {
               client_params.pwm_bits = kBitPlanes;
           }
          cout<<"pwm_bits = "<<client_params.pwm_bits<<endl;
          // The frame-buffer is organized in bitplanes.
          // Highest level (slowest to cycle through) are double rows.
          // For each double-row, we store pwm-bits columns of a bitplane.
          // Each bitplane-column is pre-filled IoBits, of which the colors are set.
          // Of course, that means that we store unrelated bits in the frame-buffer,
          // but it allows easy access in the critical section.
          //  NOTE:  This is the buffer that is copied into from the packets received via TCP
          net_bitplane_buffer_ptr = new IoBits [double_rows * columns * kBitPlanes];
#ifdef RCV_BUFFER
          net_bitplane_rcv_buffer_ptr = new IoBits [double_rows * columns * kBitPlanes];
#endif
          // Clear out the buffer
          Clear();

          cout<<"Create Threads for Canvas->GPIO\n"<<endl;
          ret = pthread_create( &canvas_thread, NULL, cnvs_thread, NULL);
          if(ret)
          {
              cout<<"pthread_create error cnvs_thread"<<ret<<endl;
              return -1;
          }
          // set priority for Canvas Thread to 99 (Max priority)
          {
              struct sched_param p;
              p.sched_priority = 99;
              pthread_setschedparam(canvas_thread, SCHED_FIFO, &p);
          }

          ret = pthread_create( &rcv_canvas_thread, NULL, rcv_cnvs_thread, NULL);
          if(ret)
          {
              cout<<"pthread_create error rcv_cnvs_thread"<<ret<<endl;
              return -1;
          }
          break;
      default :
          cout << "Invalid Net Cmd 1" << LedCMDBuff[0] << endl;
          break;
      }
    }
  } catch (SocketException &e) {
    cerr << e.what() << endl;
    exit(1);
  }

  return 0;
}
