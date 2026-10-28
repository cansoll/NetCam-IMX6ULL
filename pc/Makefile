CC = gcc
CFLAGS = -Wall -O2 -pthread
LIBS = -ljpeg -lSDL2 -lpthread

TARGET = pc_preview
SRCS = main.c receiver.c jpeg_decode.c

all:
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LIBS)

clean:
	rm -f $(TARGET)