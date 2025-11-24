CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11
# Uncomment to use system VFIO header (if available)
# CFLAGS += -DUSE_SYSTEM_VFIO_HEADER
LDFLAGS = 

TARGET = vfio_test
SOURCES = vfio_test.c

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES) $(LDFLAGS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	cp $(TARGET) /usr/local/bin/

.PHONY: all clean install

