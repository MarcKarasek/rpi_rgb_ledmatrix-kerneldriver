Directions on how to compile the kernel module for Rasp PI 1/2/+

A good primer on kernel modules in general can be found at:
https://www.kernel.org/doc/Documentation/kbuild/modules.txt

In order to compile the kernel module you must have the kernel source installed that
oorresponds to the kernel you are running on the Rasp Pi.  For this code the compile
was done on a Raspbian system.

You will need to follow the instructions at the following website:
https://github.com/notro/rpi-source/wiki

Another good wwebsite with some additional info on gettign kernel headers:

http://stackoverflow.com/questions/20167411/how-to-compile-a-kernel-module-for-raspberry-pi


Once you have the kernel source/headers installed you are ready to compile the kernel module and the application.

Compiling the module:

make -f Makefile


Compiling the Applications:

make -f Makefile.ledapp


Compiled and testesd with the following configuration:

Linux version 4.1.20-v7+ (dc4@dc4-XPS13-9333) (gcc version 4.9.3 (crosstool-NG crosstool-ng-1.22.0-88-g8460611) ) #867 SMP Wed Mar 23 20:12:32 GMT 2016

gcc (Raspbian 4.9.2-10) 4.9.2
