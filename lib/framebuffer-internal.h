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
#ifndef RPI_RGBMATRIX_FRAMEBUFFER_INTERNAL_H
#define RPI_RGBMATRIX_FRAMEBUFFER_INTERNAL_H

#include "led-matrix.h"

namespace rgb_matrix {
// Internal representation of the frame-buffer that as well can
// write itself to GPIO.
// Our internal memory layout mimicks as much as possible what needs to be
// written out.
class RGBMatrix::Framebuffer {
public:
  Framebuffer(int rows, int columns);
  ~Framebuffer();

  // Initialize GPIO bits for output.
  static void InitGPIO(GPIO *io);

  // Set PWM bits used for output. Default is 11, but if you only deal with
  // simple comic-colors, 1 might be sufficient. Lower require less CPU.
  // Returns boolean to signify if value was within range.
  bool SetPWMBits(uint8_t value);
  uint8_t pwmbits() { return pwm_bits_; }

  // Map brightness of output linearly to input with CIE1931 profile.
  void set_luminance_correct(bool on) { do_luminance_correct_ = on; }
  bool luminance_correct() const { return do_luminance_correct_; }


#ifdef LED_SCKT_INTERFACE
    // Cmd to stop server from Client
    void KillSrvr(string host, unsigned short port);
    // Send UDP PAcket to sync client with server for options
    bool SyncSrvr(string host, unsigned short port, struct net_parameters * params);
    // Disconnect from server but leave it running for next client
    void DCSrvr(string host, unsigned short port);
    // We are either Dumping the Matrix to a TCP Socket or GPIOs
    void DumpToWeb(string host, unsigned short port);
    // open TCP connection. One time event
    void OpenTCPConnection(string host, unsigned short port);
    // Close the TCP by unsetting the flag
    void CloseTCP(string host, unsigned short port);
#else
    void DumpToMatrix(int fd);
#endif

  // Canvas-inspired methods, but we're not implementing this interface to not
  // have an unnecessary vtable.
  inline int width() const { return columns_; }
  inline int height() const { return rows_; }
  void SetPixel(int x, int y, uint8_t red, uint8_t green, uint8_t blue);
  void Clear();
  void Fill(uint8_t red, uint8_t green, uint8_t blue);

private:

#ifdef LED_SCKT_INTERFACE
  // Create The UDP Socket used as a control channel
  UDPSocket sock;

  // Create the TCP Socket to send data on
  TCPSocket tcpsock;

  struct net_value nval;
  int value;
  struct set_bits set_bits_vals;

#endif

  // Map color
  inline uint16_t MapColor(uint8_t c);

  const int rows_;     // Number of rows. 16 or 32.
  const int columns_;  // Number of columns. Number of chained boards * 32.

  uint8_t pwm_bits_;   // PWM bits to display.
  bool do_luminance_correct_;

  const int double_rows_;
  const uint8_t row_mask_;

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
    } bits;
#endif
    uint32_t raw;
    IoBits() : raw(0) {}
  };

  // The frame-buffer is organized in bitplanes.
  // Highest level (slowest to cycle through) are double rows.
  // For each double-row, we store pwm-bits columns of a bitplane.
  // Each bitplane-column is pre-filled IoBits, of which the colors are set.
  // Of course, that means that we store unrelated bits in the frame-buffer,
  // but it allows easy access in the critical section.
  IoBits *bitplane_buffer_;
  inline IoBits *ValueAt(int double_row, int column, int bit);
};
}  // namespace rgb_matrix
#endif // RPI_RGBMATRIX_FRAMEBUFFER_INTERNAL_H
