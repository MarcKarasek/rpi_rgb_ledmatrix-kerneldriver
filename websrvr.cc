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

#include "kmod_common.h"

const int CMDMAX = 15;     // Longest Led CMD is 12 so add a little buffer..

struct set_bits *pset_bits_vals;
int *pval;

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


int main(int argc, char *argv[]) {

  if (argc != 2) {                  // Test for correct number of parameters
    cerr << "Usage: " << argv[0] << " <Server Port>" << endl;
    exit(1);
  }

  unsigned short LedSrvrPort = atoi(argv[1]);     // First arg:  local port

  SetGPIO();

  try {
    UDPSocket sock(LedSrvrPort);

    char LedCMDBuff[CMDMAX];         // Buffer for rcv cmd
    int recvCmdSz;                  // Size of received message
    string sourceAddress;             // Address of datagram source
    unsigned short sourcePort;        // Port of datagram source
    cout << "Starting Server " << endl;

    // Run Until ^C
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
      case NET_KILLSRVR :
          cout<<"Stopping Server -- Rcvd Stop CMd from Client"<<endl;
          close(fd);
          exit(0);
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

