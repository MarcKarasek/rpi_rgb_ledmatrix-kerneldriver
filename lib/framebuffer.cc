// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// Copyright (C) 2013 Henner Zeller <h.zeller@acm.org>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation version 2.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://gnu.org/licenses/gpl-2.0.txt>

// The framebuffer is the workhorse: it represents the frame in some internal
// format that is friendly to be dumped to the matrix quickly. Provides methods
// to manipulate the content.

#ifdef LED_SCKT_INTERFACE
    #include "../webinterface/PracticalSocket.h"      // For UDPSocket and SocketException
#endif

#include "../kmod_common.h"

#ifdef LED_SCKT_INTERFACE
    #include "../web_defines.h"
    struct net_canvas net_cnvs;
#endif

#include "framebuffer-internal.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <asm/ioctl.h>
#include <sys/ioctl.h>
#include <iostream>          // For cout and cerr

namespace rgb_matrix {
enum {
  kBitPlanes = 11  // maximum usable bitplanes.
};

struct set_bits set_bits_vals;
int value;

static const long kBaseTimeNanos = 200;

// Flag set when we open the TCP connection for the first time
// This is done on first call to DumpToWeb() API.
static bool tcpopen_flag = false;

volatile uint32_t *freeRunTimer = NULL; // GPIO may override on startup

#ifndef LED_SCKT_INTERFACE
static void sleep_nanos(long nanos) {
  if (nanos > 28000) {
    if(freeRunTimer) { // Pi 2?
      // nanosleep() is ALL MESSED UP on Pi 2.  Instead we'll poll the
      // free-running timer.  Spinning like this would clobber the single-
      // core Pi 1 (hence nanosleep below), but seems OK-ish on the Pi 2.
      // nanosleep() would still be preferred, but we might need to wait
      // for an OS update with decent timer resolution.
      uint32_t micros    = nanos / 1000,
               startTime = *freeRunTimer;
      while((*freeRunTimer - startTime) < micros);
    } else {
      // On Pi 1, nanosleep() is only mildly messed up, with a ~20 usec
      // offset that must be compensated for.  For very short intervals
      // (<30 uS, below) an idle counter loop is used instead.
      struct timespec sleep_time = { 0, nanos - 20000 };
      nanosleep(&sleep_time, NULL);
    }
  } else {
    // The following loop is determined empirically
    for(volatile int i = nanos >> 3; i--; );
  }
}
#endif

RGBMatrix::Framebuffer::Framebuffer(int rows, int columns)
  : rows_(rows), columns_(columns),
    pwm_bits_(kBitPlanes), do_luminance_correct_(true),
    double_rows_(rows / 2), row_mask_(double_rows_ - 1) {
  bitplane_buffer_ = new IoBits [double_rows_ * columns_ * kBitPlanes];
  Clear();
}

RGBMatrix::Framebuffer::~Framebuffer() {
  delete [] bitplane_buffer_;
}

bool RGBMatrix::Framebuffer::SetPWMBits(uint8_t value) {
  if (value < 1 || value > kBitPlanes)
    return false;
  pwm_bits_ = value;
  return true;
}

inline RGBMatrix::Framebuffer::IoBits *
RGBMatrix::Framebuffer::ValueAt(int double_row, int column, int bit) {
  return &bitplane_buffer_[ double_row * (columns_ * kBitPlanes)
                            + bit * columns_
                            + column ];
}

// Do CIE1931 luminance correction and scale to output bitplanes
static uint16_t luminance_cie1931(uint8_t c) {
  float out_factor = ((1 << kBitPlanes) - 1);
  float v = c * 100.0 / 255.0;
  return out_factor * ((v <= 8) ? v / 902.3 : pow((v + 16) / 116.0, 3));
}

static uint16_t *CreateLuminanceCIE1931LookupTable() {
  uint16_t *result = new uint16_t [ 256 ];
  for (int i = 0; i < 256; ++i)
    result[i] = luminance_cie1931(i);
  return result;
}

inline uint16_t RGBMatrix::Framebuffer::MapColor(uint8_t c) {
#ifdef INVERSE_RGB_DISPLAY_COLORS
#  define COLOR_OUT_BITS(x) (x) ^ 0xffff
#else
#  define COLOR_OUT_BITS(x) (x)
#endif

  if (do_luminance_correct_) {
    // We're leaking this table. So be it :)
    static uint16_t *luminance_lookup = CreateLuminanceCIE1931LookupTable();
    return COLOR_OUT_BITS(luminance_lookup[c]);
  } else {
    enum {shift = kBitPlanes - 8};  //constexpr; shift to be left aligned.
    return COLOR_OUT_BITS((shift > 0) ? (c << shift) : (c >> -shift));
  }

#undef COLOR_OUT_BITS
}

void RGBMatrix::Framebuffer::Clear() {
#ifdef INVERSE_RGB_DISPLAY_COLORS
  Fill(0, 0, 0);
#else
  memset(bitplane_buffer_, 0,
         sizeof(*bitplane_buffer_) * double_rows_ * columns_ * kBitPlanes);
#endif
}

void RGBMatrix::Framebuffer::Fill(uint8_t r, uint8_t g, uint8_t b) {
  const uint16_t red   = MapColor(r);
  const uint16_t green = MapColor(g);
  const uint16_t blue  = MapColor(b);

  for (int b = kBitPlanes - pwm_bits_; b < kBitPlanes; ++b) {
    uint16_t mask = 1 << b;
    IoBits plane_bits;
    plane_bits.raw = 0;
    plane_bits.bits.r1 = plane_bits.bits.r2 = (red & mask) == mask;
    plane_bits.bits.g1 = plane_bits.bits.g2 = (green & mask) == mask;
    plane_bits.bits.b1 = plane_bits.bits.b2 = (blue & mask) == mask;
    for (int row = 0; row < double_rows_; ++row) {
      IoBits *row_data = ValueAt(row, 0, b);
      for (int col = 0; col < columns_; ++col) {
        (row_data++)->raw = plane_bits.raw;
      }
    }
  }
}

void RGBMatrix::Framebuffer::SetPixel(int x, int y,
                                      uint8_t r, uint8_t g, uint8_t b) {
  if (x < 0 || x >= columns_ || y < 0 || y >= rows_) return;

  const uint16_t red   = MapColor(r);
  const uint16_t green = MapColor(g);
  const uint16_t blue  = MapColor(b);

  const int min_bit_plane = kBitPlanes - pwm_bits_;
  IoBits *bits = ValueAt(y & row_mask_, x, min_bit_plane);
  if (y < double_rows_) {   // Upper sub-panel.
    for (int b = min_bit_plane; b < kBitPlanes; ++b) {
      const uint16_t mask = 1 << b;
      bits->bits.r1 = (red & mask) == mask;
      bits->bits.g1 = (green & mask) == mask;
      bits->bits.b1 = (blue & mask) == mask;
      bits += columns_;
    }
  } else {
    for (int b = min_bit_plane; b < kBitPlanes; ++b) {
      const uint16_t mask = 1 << b;
      bits->bits.r2 = (red & mask) == mask;
      bits->bits.g2 = (green & mask) == mask;
      bits->bits.b2 = (blue & mask) == mask;
      bits += columns_;
    }
  }
}

#ifdef LED_SCKT_INTERFACE
// Disconnect from the server and tell server to stop
void RGBMatrix::Framebuffer::KillSrvr(string host, unsigned short port)
{
        nval.netopcode = NET_KILLSRVR;
        nval.value = 0;
        sock.sendTo(&nval, sizeof(net_value), host, port);
}
// Disconnect from server but leave it running for next client
void RGBMatrix::Framebuffer::DCSrvr(string host, unsigned short port)
{
        nval.netopcode = NET_CLIENT_DC;
        nval.value = 0;
        sock.sendTo(&nval, sizeof(net_value), host, port);
}
  // Set the paramters for this Client Application with the Web Server
  // These are used in the WebSrvr when dumping to the GPIO.
bool RGBMatrix::Framebuffer::SyncSrvr(string host, unsigned short port, struct net_parameters * params)
{
      if (host.empty())
      {
          cout<<"Need to set the host:port"<<endl;
          return false;
      }
      // Send on UDP the Control Parameters.
      sock.sendTo(params, sizeof(net_parameters), host, port);
      return true;


}
// One time call to open the socket to the server..
void RGBMatrix::Framebuffer::OpenTCPConnection(string host, unsigned short port) {
#if 0
try {
      // Establish the TCP Connection for data
      tcpsock.connect(host, port);
      cout<<"TCP Connection Established "<<host<<" "<<port<<endl;

    }   catch(SocketException &e) {
        cerr << e.what() << endl;
        cout << "TCP Send Error" << endl;
    }
#endif
}
// Turn off the flag that for TCP connect()
void RGBMatrix::Framebuffer::CloseTCP(string host, unsigned short port) {

    nval.netopcode = NET_TCPSTOP;
    nval.value = 0;
    sock.sendTo(&nval, sizeof(net_value), host, port);
    tcpopen_flag = false;
}
#endif

#ifdef LED_SCKT_INTERFACE
void RGBMatrix::Framebuffer::DumpToWeb(string host, unsigned short port) {

try {

    if(!tcpopen_flag) {
        // Establish the TCP Connection for data
        tcpsock.connect(host, port);
        cout<<"TCP Connection Established "<<host<<" "<<port<<endl;
        tcpopen_flag = true;
    }

    // Send the current buffer to server
    tcpsock.send(bitplane_buffer_, NET_BUFFER);

    // Destructor closes the socket
  } catch(SocketException &e) {
    cerr << e.what() << endl;
    cout << "TCP Send Error" << endl;
  }

}
#else
void RGBMatrix::Framebuffer::DumpToMatrix(int fd) {
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

  const int pwm_to_show = pwm_bits_;  // Local copy, might change in process.
  for (uint8_t d_row = 0; d_row < double_rows_; ++d_row) {
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
      for (int col = 0; col < columns_; ++col) {
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
#endif // #ifdef LED_SCKT_INTERFACE
}  // namespace rgb_matrix
