CC = gcc

# -lsocket is required on the Unix server
#  CFLAGS = -lsocket -lnsl -w

CFLAGS = -lnsl -w


all: clean clientftp serverftp

serverftp: serverftp.c
	$(CC) serverftp.c -o serverftp $(CFLAGS)

clientftp: clientftp.c serverftp
	$(CC) clientftp.c -o clientftp $(CFLAGS)


clean:
	- rm clientftp 2> /dev/null
	- rm serverftp 2> /dev/null

