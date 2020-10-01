CC = gcc

# -lsocket is required on the Unix server
#  CFLAGS = -lsocket -lnsl -w

CFLAGS = -lnsl -w


all: client server

server: serverftp.c 
	$(CC) serverftp.c -o serverftp $(CFLAGS)

client: clientftp.c server
	$(CC) clientftp.c -o clientftp $(CFLAGS)


clean:
	rm clientftp serverftp
