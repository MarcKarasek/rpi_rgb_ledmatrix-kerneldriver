#ifndef _WEBDEFINES_H_
#define _WEBDEFINES_H_

// For string
#include <string>


#define NET_CLRBITS     0x10
#define NET_SETBITS     0x11
#define NET_WRMSKBITS   0x12
#define NET_KILLSRVR    0x13
#define NET_INIT_PARAMS 0x14
#define NET_SENDCANVAS  0x15
#define NET_CLIENT_DC   0x16
#define NET_TCPSTOP     0x17

#define NET_BUFFER  22528
#define NET_BUFFER_LARGE 360448


struct net_parameters
{
    unsigned char  netopcode;
    unsigned char rsvd[3];
    int runtime_seconds;
    int demo;
    int rows;
    int chain;
    int scroll_ms;
    int pwm_bits;
    bool large_display;
    bool do_luminance_correct;
//    unsigned short port;
//    std::string host;

};

struct net_canvas
{
    unsigned char netopcode;
    unsigned char buffer;
    unsigned char canvas_packet[500];
};

#endif // #define _WEBDEFINES_H_

