CC = gcc

# -lsocket is required on the Unix server
#  CFLAGS = -lsocket -lnsl -w

WFLAGS = -Wall -Wextra -Wpedantic -g
CFLAGS = -lnsl $(WFLAGS)


all: clean clientftp serverftp

serverftp: serverftp.c dynamic_string.c
	# create a testfile for the server to hold 
	echo "She's in love with who I am Back in high school, I used to bus it to the dance (Yeah) Now I hit the FBO with duffels in my hands I did half a Xan, 13 hours 'til I land Had me out like a light, ayy, yeah Like a light, ayy, yeah  [Verse 3: Drake & Travis Scott] Like a light, ayy, slept through the flight, ayy Knocked for the night, ayy " > server/jamba_juice.txt
	$(CC) serverftp.c dynamic_string.c -o server/serverftp $(CFLAGS)
	

clientftp: clientftp.c dynamic_string.c serverftp
	# create a testfile for the client to use 
	echo "GITHUB_API_KEY=23fa3430SA12SD231SD23" > client/api_key.txt 
	$(CC) clientftp.c dynamic_string.c -o client/clientftp $(CFLAGS)



tester: dynamic_string.c
	$(CC) tester.c dynamic_string.c -o test $(WFLAGS) 

clean:
	- rm client/clientftp 2> /dev/null
	# create a testfile in the client directory 

	- rm server/serverftp 2> /dev/null
	# create a testfile in the 

