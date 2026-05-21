CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lsqlite3
TARGET = server

SRCS = main.c server.c http.c handler.c database.c auth.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all clean
