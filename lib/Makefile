# Creating RGB matrix library
# When you link this library with your binary, you need to add -lrt -lm -lpthread
# So
#   -lrgbmatrix
##
OBJECTS=led-matrix.o framebuffer.o thread.o bdf-font.o graphics.o
TARGET=librgbmatrix.a

#gpio.o

# If you see that your display is inverse, you might have a matrix variant
# has uses inverse logic for the RGB bits. Attempt this
#DEFINES+=-DINVERSE_RGB_DISPLAY_COLORS

DEFINES+=-DADAFRUIT_RGBMATRIX_HAT

INCDIR=../include
CXXFLAGS=-Wall -O3 -g $(DEFINES) $(WEBDEF)

CXXFLAGS+=-DDEMO_CNVS_SND

$(TARGET) : $(OBJECTS)
	ar rcs $@ $^

led-matrix.o: led-matrix.cc $(INCDIR)/led-matrix.h
thread.o : thread.cc $(INCDIR)/thread.h

%.o : %.cc
	$(CXX) -I$(INCDIR) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJECTS) $(TARGET)
