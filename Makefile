CC = gcc
CFLAGS = -O3 -march=native -fPIC -Wall -Wextra
LDFLAGS = -shared

TARGET = libchess.so
SRC = chess_engine.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

clean:
	rm -f $(TARGET)

.PHONY: all clean
