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
