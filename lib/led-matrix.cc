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

#ifdef LED_SCKT_INTERFACE
#include "../webinterface/PracticalSocket.h"      // For UDPSocket and SocketException
#endif

#include "../kmod_common.h"
#ifdef LED_SCKT_INTERFACE
#include "../web_defines.h"
#endif
#include "led-matrix.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <iostream>

#define SHOW_REFRESH_RATE 0

#if SHOW_REFRESH_RATE
# include <stdio.h>
# include <sys/time.h>
#endif

#include "thread.h"
#include "framebuffer-internal.h"


#ifndef LED_SCKT_INTERFACE
#define DEVICE_PATH "/dev/gpioleddrvr"
#endif

namespace rgb_matrix {

// Pump pixels to screen. Needs to be high priority real-time because jitter
class RGBMatrix::UpdateThread : public Thread {
public:
  UpdateThread(RGBMatrix *matrix) : running_(true), matrix_(matrix) {}

  void Stop() {
    MutexLock l(&mutex_);
    running_ = false;
  }

  virtual void Run() {
    while (running()) {
#if SHOW_REFRESH_RATE
      struct timeval start, end;
      gettimeofday(&start, NULL);
#endif
      matrix_->UpdateScreen();
#if SHOW_REFRESH_RATE
      gettimeofday(&end, NULL);
      int64_t usec = ((uint64_t)end.tv_sec * 1000000 + end.tv_usec)
        - ((int64_t)start.tv_sec * 1000000 + start.tv_usec);
      printf("\b\b\b\b\b\b\b\b%6.1fHz", 1e6 / usec);
#endif
    }
  }

private:
  inline bool running() {
    MutexLock l(&mutex_);
    return running_;
  }

  Mutex mutex_;
  bool running_;
  RGBMatrix *const matrix_;
};
#ifdef LED_SCKT_INTERFACE
RGBMatrix::RGBMatrix(int rows, int chained_displays)
  : frame_(new Framebuffer(rows, 32 * chained_displays)),
    updater_(NULL) {
}
#else
RGBMatrix::RGBMatrix(int rows, int chained_displays)
  : frame_(new Framebuffer(rows, 32 * chained_displays)),
    fd_(0), updater_(NULL) {
}
#endif
RGBMatrix::~RGBMatrix() {

#ifndef LED_SCKT_INTERFACE
  updater_->Stop();
  updater_->WaitStopped();
  delete updater_;
#endif

  frame_->Clear();
#ifdef LED_SCKT_INTERFACE
// No need to clear the display.. if we are exiting we have cleared it before this, if needed
#else
  frame_->DumpToMatrix(fd_);
#endif
  delete frame_;
#ifndef LED_SCKT_INTERFACE
  close(fd_);
#endif
}

#ifdef LED_SCKT_INTERFACE
// Web Led Client Code
void RGBMatrix::SetNetInterface(string host, unsigned short port, struct net_parameters * params ) {

    // Set Server and Port to use for Web Interface
    host_.assign(host);
    port_ = port;
    cout<<"Server Address "<<host_<<" Port "<<port_<<endl;

    frame_->SyncSrvr(host, port, params);
#ifndef DEMO_CNVS_SND
    // For Web Interface we do the canvas update in each demo thread.
    // We send the canvas to the server at the end of each loop of the demo.
    updater_ = new UpdateThread(this);
    updater_->Start(99);  // Whatever we get :)
#endif
}
// GPIO code
#else
bool RGBMatrix::SetGPIO(void) {
  // Open up the GPIO Led Device Driver
  fd_ = open("/dev/gpioleddrvr", O_RDWR);
  if (fd_ == -1)
  {
      printf("Error Opening Led Driver /dev/gpioleddrvr %x ", errno);
      return false;  //Problem with Driver just return.
  }
  updater_ = new UpdateThread(this);
  updater_->Start(99);  // Whatever we get :)
  return true;
}
#endif

bool RGBMatrix::SetPWMBits(uint8_t value) { return frame_->SetPWMBits(value); }
uint8_t RGBMatrix::pwmbits() { return frame_->pwmbits(); }

// Map brightness of output linearly to input with CIE1931 profile.
void RGBMatrix::set_luminance_correct(bool on) {
  frame_->set_luminance_correct(on);
}
bool RGBMatrix::luminance_correct() const { return frame_->luminance_correct(); }
#ifdef LED_SCKT_INTERFACE
void RGBMatrix::UpdateScreen() {
    frame_->DumpToWeb(host_, port_);
}
#else
void RGBMatrix::UpdateScreen() { frame_->DumpToMatrix(fd_); }
#endif

// -- Implementation of RGBMatrix Canvas: delegation to ContentBuffer
int RGBMatrix::width() const { return frame_->width(); }
int RGBMatrix::height() const { return frame_->height(); }
void RGBMatrix::SetPixel(int x, int y,
                         uint8_t red, uint8_t green, uint8_t blue) {
  frame_->SetPixel(x, y, red, green, blue);
}
void RGBMatrix::Clear() { return frame_->Clear(); }
#ifdef LED_SCKT_INTERFACE
void RGBMatrix::StopSrvr() { return frame_->KillSrvr(host_, port_); }
void RGBMatrix::DisConnSrvr() { return frame_->DCSrvr(host_, port_); }
void RGBMatrix::SendCnvs() { return frame_->DumpToWeb(host_, port_); }
void RGBMatrix::CloseTCPConn() { return frame_->CloseTCP(host_, port_); }
#endif
void RGBMatrix::Fill(uint8_t red, uint8_t green, uint8_t blue) {
  frame_->Fill(red, green, blue);
  }
}
// end namespace rgb_matrix
