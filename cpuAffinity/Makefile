CC = gcc
CFLAGXX = -std=c99 -g3
TARGET = main

all: $(TARGET)

main: process.c
	$(CC) $(CFLAGXX) $< -o $@

.PHONY: clean
clean:
	@echo removing binary and object files ...
	-rm -rf
