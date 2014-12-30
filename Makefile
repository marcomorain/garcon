
PREFIX ?= /usr/local
TARGET = garcon
LIBS = -lm
CFLAGS = -D_GNU_SOURCE -std=gnu99 -Wall -Wextra # -Werror -Os
LDFLAGS = -D_GNU_SOURCE -std=gnu99
# CFLAGS = -D_POSIX_C_SOURCE=200112L -std=c99 -Wall -Wextra # -Werror -Os
INC = -Ideps

.PHONY: default all clean install uninstall

default: $(TARGET)
all: default

OBJECTS = $(patsubst %.c, %.o, $(wildcard *.c) $(wildcard deps/*/*.c))
HEADERS = $(wildcard *.h)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -Wall $(LIBS) -o $@

clean:
	-rm -f *.o
	-rm -f deps/*/*.o
	-rm -f $(TARGET)

install: $(TARGET)
	cp -f $(TARGET) $(PREFIX)/bin/$(TARGET)

uninstall:
	rm -f $(PREFIX)/bin/$(TARGET)
