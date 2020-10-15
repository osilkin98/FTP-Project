CC = gcc

# -lsocket is required on the Unix server
#  CFLAGS = -lsocket -lnsl -w

WFLAGS = -Wall -Wextra -Wpedantic -g
CFLAGS = -lnsl $(WFLAGS)


all: clean clientftp serverftp

serverftp: serverftp.c dynamic_string.c
	$(CC) serverftp.c dynamic_string.c -o serverftp $(CFLAGS)

clientftp: clientftp.c dynamic_string.c serverftp
	$(CC) clientftp.c dynamic_string.c -o clientftp $(CFLAGS)



tester: dynamic_string.c
	$(CC) tester.c dynamic_string.c -o test $(WFLAGS) 

clean:
	- rm clientftp 2> /dev/null
	- rm serverftp 2> /dev/null

