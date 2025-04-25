CC = gcc
CFLAGS = -Wall -Wextra -Werror
LDFLAGS = 

TARGET = sshell
SRCS = sshell.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)
	rm -f *~

.PHONY: all clean 