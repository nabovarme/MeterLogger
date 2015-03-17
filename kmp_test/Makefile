HEADERS = kmp.h kmp_test.h
OBJECTS = kmp.o kmp_test.o
TARGET = kmp_test
LIBS = -lm
CC = gcc
CFLAGS = -g -Wall

default: $(TARGET)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

kmp_test: $(OBJECTS)
	gcc $(OBJECTS) -o $@

clean:
	-rm -f $(OBJECTS)
	-rm -f $(TARGET)