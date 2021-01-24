# rpi_rgb_ledmatrix-kerneldriver
Controlling one or a chain of 32x32 or 16x32 RGB LED displays using Raspberry Pi GPIO

Kernel Module and User Applications

Kernel Module -
    Can be compiled to use ioremap or sysfs to access the GPIO on the Rasp Pi (Both tested)
    It will also flash the display all red, then green then blue on load to let you know it is alive and the HW is working.
    On module load will flash the dispaly red/green/blue to let you know it is functional and your HW works (imalive)

Demo Application -
    Setup to use /dev/gpioleddriver ioctl calls to access Raspberry PI GPIOs and write to Led Display.
    No sudo needed to run this.

Web Server / Client Added.
Web Server runs on rasp PI along with Kernel Module.
Client can run locally (localhost 127.0.0.1) or on another Linux / Pi

Tested with localhost and display is present but not crisp.
Remote client dispaly is partially there but garbled.

Compiled and testesd with the following configuration:

Linux version 4.1.20-v7+ (dc4@dc4-XPS13-9333) (gcc version 4.9.3 (crosstool-NG crosstool-ng-1.22.0-88-g8460611) ) #867 SMP Wed Mar 23 20:12:32 GMT 2016

gcc (Raspbian 4.9.2-10) 4.9.2

<hr>

Step by Step Instructions on how to use this code.
1. You will need to setup your Rasp Pi with gcc toolchain and kernel headers.

2. Compiling the kernel module.
    run "make"
    This will build the leddriver.ko kernel module
    You can then insmod this module:  sudo insmod leddriver.ko
    If this is working properly, you should see the led matrix flash red, green and blue.  

3. Compiling the ledapp
    run "make -f Makfefile.ledapp"
    This will build the led-matrix application
    You will need to run tis as sudo or change the permissions on /dev/gpioleddrvr
    "sudo ./led-matrix"
    This will bring up a menu of demo apps to run on the display.

4. Compiling the web app
    run "make -f Makefile.webapp"
    This will build two apps, ledwebsrvr and web-matrix
    Start the server with "sudo ./ledwebsrvr <port>"
    Start the web app with "./web-matrix -s 127.0.0.1:<port>"
    This will start the webapp:
    
