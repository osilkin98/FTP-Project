/* 
 * By: Oleg Silkin
 * Computer Networking - 477 - HW2
 * 
 * Client FTP program
 *
 * This program connects to the FTP Unix Server
 * 
 * The client connects to the implemented FTP server running on port 4200 and 
 * communicates with it through the control connection. 
 * User enters input through STDIN and the user input is interpreted as <cmd><space><argument> 
 * where <space> is any series of whitespace character separating text. 
 * 
 * Input is tokenized only once so any amount of spaces after the start of <argument> is considered to be 
 * part of the argument, unless it is not followed by anymore text. 
 * 
 * FTP Server is using a state transition system to maintain login state, so the 
 * client uses this information before initiating file transfer through the data connection, and the server does the same. 
 * 
 * The implemented commands are:
    user <username>
	pass <password>
	mkdir <directory>
	rmdir <directory>
	cd <directory>
	rm <filename>
	ls
	pwd
	send <filepath>
	recv <filepath>
	stat
	quit
	help
 * 
 * The commands available to the user before logging in are help, quit, user, pass, and stat.
 * 
 * send/recv/ls are implemented to use a data connection, 
 * and require an additional message to be sent through the control connection
 * to dictate the program's control flow. 
 * 
 * Server's reply codes are extracted from the response and used to dictate what should happen on the client end,
 * which stops us from opening unwanted/unutilized data connections with the server. 
 * 
 * 
 * NOTE: Starting homework #2, add more comments here describing the overall function
 * performed by server ftp program
 * This includes, the list of ftp commands processed by server ftp.
 *
 */

#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h> 
#include <stdlib.h>
#include <stdbool.h> 

/* use localhost for this because all execution will take place locally */
#define FTP_SERVER_ADDRESS "localhost"

#define SERVER_FTP_PORT 4200
#define DATA_CONNECTION_FTP_PORT 4201

#define PROMPT "ftp> " 

/* Error and OK codes */
#define OK 0
#define ER_INVALID_HOST_NAME -1
#define ER_CREATE_SOCKET_FAILED -2
#define ER_BIND_FAILED -3
#define ER_CONNECT_FAILED -4
#define ER_SEND_FAILED -5
#define ER_RECEIVE_FAILED -6

#define FTP_SUCCESSFUL_LOGIN 230
#define FTP_FILE_OK_OPENING_DATA_SOCK 150 	/* the requested server file is okay and a data connection is initating */

#define FTP_FILE_UNAVAILBLE 550

/* Function prototypes */

int clntConnect(char	*serverName, int *s);
int sendMessage (int s, char *msg, int  msgSize);
int receiveMessage(int s, char *buffer, int  bufferSize, int *msgSize);


#define BUFFER_SIZE 4096
#define FILE_BUFFER_SIZE 100

/* List of all global variables */

char userCmd[BUFFER_SIZE];	/* user typed ftp command line read from keyboard */
char cmd[BUFFER_SIZE];		/* ftp command extracted from userCmd */
char argument[BUFFER_SIZE];	/* argument extracted from userCmd */
char replyMsg[BUFFER_SIZE];    /* buffer to receive reply message from server */


/*
 * main
 *
 * Function connects to the ftp server using clntConnect function.
 * Reads one ftp command in one line from the keyboard into userCmd array.
 * Sends the user command to the server.
 * Receive reply message from the server.
 * On receiving reply to QUIT ftp command from the server,
 * close the control connection socket and exit from main
 *
 * Parameters
 * argc		- Count of number of arguments passed to main (input)
 * argv  	- Array of pointer to input parameters to main (input)
 *		   It is not required to pass any parameter to main
 *		   Can use it if needed.
 *
 * Return status
 *	OK	- Successful execution until QUIT command from client 
 *	N	- Failed status, value of N depends on the function called or cmd processed
 */

int main(void)
{
	/* List of local varibale */
	int client_listen_socket;  /* client socket to listen for connections on */
	int data_socket; 	/* Data Connection socket - to be used in all server communications */
	int ccSocket;	/* Control connection socket - to be used in all client communication */
	int msgSize;	/* size of the reply message received from the server */
	int status;  /* store the return status from standard functions */
	char buff[FILE_BUFFER_SIZE]; /* file buffer for writing/reading from the TCP socket */
	bool logged_in = false;  /* store login state from FTP server */

	 /* Connect to client ftp*/
	status=clntConnect("localhost", &ccSocket);
	if(status != 0)
	{
		printf("[client]: Connection to server failed, exiting main. \n");
		return (status);
	}

	/* We connected to the server, now we need to receive the welcome message */ 
	/* We connected to the server so lets receive the welcome message */
	status = receiveMessage(ccSocket, replyMsg, sizeof(replyMsg), &msgSize);



	/* Now that the client connected to the FTP server, 
		we can initialize the clientside data connection */
	status = svcInitServer(&client_listen_socket);
	if (status != 0) 
	{
		fprintf(stderr, "[client]: could not start data connection server\r\n");
		exit(status);
	}

	/* 
	 * Read an ftp command with argument, if any, in one line from user into userCmd.
	 * Copy ftp command part into ftpCmd and the argument into arg array.
 	 * Send the line read (both ftp cmd part and the argument part) in userCmd to server.
	 * Receive reply message from the server.
	 * until quit command is typed by the user.
	 */

	do
	{
		char temp[1024]; /* temp array to store buffer */
		char *ptr = NULL;   	/* string pointer to help parse through input */ 	
		int ftp_code = 0;	/* integer to store the FTP server response code */ 

		/* read user input */
		do {
			/* displays "ftp> " or whatever other prompt */
			printf("%s",PROMPT);

        /* read at most BUFFER_SIZE bytes from the `stdin` stream and write to userCmd */   
		} while((ptr = fgets(userCmd, BUFFER_SIZE, stdin)) == NULL); 

		/* Replace any newline characters with a null terminator */ 
		for(register size_t i = 0; i < strlen(userCmd); i++) {
			if(userCmd[i] == '\n') {
				userCmd[i] = '\0';
				break;
			}
		}

		/* place the command into a temp buffer so we can tokenize it */
		strcpy(temp, userCmd);
		ptr = strtok(temp, " ");
		if (ptr != NULL) {
			/* extract the user command and argument */
			strcpy(cmd, ptr); 
			if ((ptr = strtok(NULL, " "))) 
				strcpy(argument, ptr);

		}

		else {
			fprintf(stderr, "[client]: no command was entered\r\n");
			continue;
		}

		/* If our command is send, we need to make sure that the file exists before we send it */

		if (strcmp(cmd, "send") == 0) {
			FILE *checkfile = NULL;
			if ((checkfile = fopen(argument, "r")) == NULL) {
				printf("[client]: Error: file '%s' does not exist, check your spelling and try again\r\n", argument);
				/* reiterate; dont send anything to the server */ 
				continue;
			} 
			fclose(checkfile);

		} 



		/* send the userCmd to the server */
		status = sendMessage(ccSocket, userCmd, strlen(userCmd)+1);
		if(status != OK)
		{
		    break;
		}

		/* 
			Extra communication is required between client and server during 
			recv/send flow control, we need to know that the file is available prior 
			to sending it 
		*/ 
		if(logged_in && strcmp(cmd, "recv") == 0) {
			if (argument == NULL) {
				printf("[client]: no argument provided, skipping\r\n");
			}
			else {
	
				/* We need to get the file's name so we don't write to the same directory */
				char *fname = NULL;
				if (strrchr(argument, '/')) 
					fname = strrchr(argument, '/') + 1;
				else 
					fname = argument; 
				
				/* accept data connection */ 
				data_socket = accept(client_listen_socket, NULL, NULL);
				if (data_socket < 0) {
					fprintf(stderr, "[client]: could not connect to server\r\n");
					break;
				} 
				/* receive the status from the server */
				status = receiveMessage(ccSocket, replyMsg, sizeof(replyMsg), &msgSize);				
				clntExtractReplyCode(replyMsg, &ftp_code);
				if (ftp_code == FTP_FILE_OK_OPENING_DATA_SOCK) {
					/* open the output file to be written to */
					FILE *outfile = fopen(fname, "w");
					if(outfile != NULL) {
						/* write until there's nothing left */
						do {
							status = receiveMessage(data_socket, buff, sizeof(buff), &msgSize);
							fwrite(buff, sizeof(char), msgSize, outfile);
						} while((msgSize > 0) && (status == 0));
						fclose(outfile);	/* close file stream */
					} 
					else {
						fprintf(stderr, "[client]: Couldn't open file descriptor for '%s'\r\n", fname);
					}
				} 
				
				else {
					printf("[client]: server responded with status code %d\r\n", ftp_code);
				}
				close(data_socket); /* close the data connection */
			}
		}
		
		/* send requires having been logged into the server and so a check is used to prevent
		 	unnecessary data connections from taking place */
		if(logged_in && strcmp(cmd, "send") == 0) {
			data_socket = accept(client_listen_socket, NULL, NULL);
			if (data_socket < 0) {
				fprintf(stderr, "[client]: could not connect to server\r\n");
				break;
			} 
			/* receive the status from the server */
			status = receiveMessage(ccSocket, replyMsg, sizeof(replyMsg), &msgSize);				
			clntExtractReplyCode(replyMsg, &ftp_code);
			/*
				we already know that the file in argument exists, so we just need to 
				make sure that the server can proceed with the write properly 
				
			*/
			if(ftp_code == FTP_FILE_OK_OPENING_DATA_SOCK) {
				FILE *outfile = fopen(argument, "r");
				int bytesread = 0;
				while(!feof(outfile)) {
					bytesread = fread(buff, sizeof(char), sizeof(buff), outfile);
					sendMessage(data_socket, buff, bytesread);
				}
				fclose(outfile);
			} 
			else {
				printf("[client]: server responded with status code %d\r\n", ftp_code);
			}
			close(data_socket);
		}


		/* LS requires login so the client shouldn't blindly open a data connection */
		if(logged_in && strcmp(cmd, "ls") == 0) {
			/* accept data connection */ 
			data_socket = accept(client_listen_socket, NULL, NULL);
			if (data_socket < 0) {
				fprintf(stderr, "[client]: could not connect to server\r\n");
				break;
			} 
			/* receive the status from the server */
			status = receiveMessage(ccSocket, replyMsg, sizeof(replyMsg), &msgSize);				
			clntExtractReplyCode(replyMsg, &ftp_code);
			if (ftp_code == FTP_FILE_OK_OPENING_DATA_SOCK) {
				char const* tmp_name = "lsoutput.tmp";
				/* open the output file to be written to */
				FILE *outfile = fopen(tmp_name, "w");
				if(outfile != NULL) {
					/* write until there's nothing left */
					do {
						status = receiveDataMessage(data_socket, buff, sizeof(buff), &msgSize);
						fwrite(buff, sizeof(char), msgSize, outfile);
					} while((msgSize > 0) && (status == 0));
					fclose(outfile);	/* close file stream */
					outfile = fopen(tmp_name, "r");
					char s;
					while((s=fgetc(outfile))!=EOF) {
   						printf("%c",s);
					}
					printf("\n");
					fclose(outfile);
					remove(tmp_name);
				} 
				else {
					printf("[client]: could not open file %s\r\n", tmp_name);
					fprintf(stderr, "[client]: Couldn't open file descriptor for '%s'\r\n", tmp_name);
				}
			} 
			else {
				printf("[client]: server responded with status code %d\r\n", ftp_code);
			}
			close(data_socket); /* close the data connection */
		}
		/* Receive reply message from the the server */
		status = receiveMessage(ccSocket, replyMsg, sizeof(replyMsg), &msgSize);
		clntExtractReplyCode(replyMsg, &ftp_code);
		
		/* handle the server response codes */ 
		switch (ftp_code)
		{
			case FTP_SUCCESSFUL_LOGIN:	/* code 230, can be either logged in or logged out */
				logged_in = !logged_in;	/* flip the login state */ 
				break;
			default:
				break;
		}
		if(status != OK)
		{
		    break;
		}

	}
	while (strcmp(cmd, "quit") != 0);

	/* 
		Terminate socket connections 
	*/
	close(client_listen_socket);
	close(ccSocket);  /* close control connection socket */
	return (status);

}  /* end main() */


/*
 * clntConnect
 *
 * Function to create a socket, bind local client IP address and port to the socket
 * and connect to the server
 *
 * Parameters
 * serverName	- IP address of server in dot notation (input)
 * s		- Control connection socket number (output)
 *
 * Return status
 *	OK			- Successfully connected to the server
 *	ER_INVALID_HOST_NAME	- Invalid server name
 *	ER_CREATE_SOCKET_FAILED	- Cannot create socket
 *	ER_BIND_FAILED		- bind failed
 *	ER_CONNECT_FAILED	- connect failed
 */


int clntConnect (
	char *serverName, /* server IP address in dot notation (input) */
	int *s 		  /* control connection socket number (output) */
	)
{
	int sock;	/* local variable to keep socket number */

	struct sockaddr_in clientAddress;  	/* local client IP address */
	struct sockaddr_in serverAddress;	/* server IP address */
	struct hostent	   *serverIPstructure;	/* host entry having server IP address in binary */


	/* Get IP address os server in binary from server name (IP in dot natation) */
	if((serverIPstructure = gethostbyname(serverName)) == NULL)
	{
		printf("%s is unknown server. \n", serverName);
		return (ER_INVALID_HOST_NAME);  /* error return */
	}

	/* Create control connection socket */
	if((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		perror("cannot create socket ");
		return (ER_CREATE_SOCKET_FAILED);	/* error return */
	}

	/* initialize client address structure memory to zero */
	memset((char *) &clientAddress, 0, sizeof(clientAddress));

	/* Set local client IP address, and port in the address structure */
	clientAddress.sin_family = AF_INET;	/* Internet protocol family */
	clientAddress.sin_addr.s_addr = htonl(INADDR_ANY);  /* INADDR_ANY is 0, which means */
						 /* let the system fill client IP address */
	clientAddress.sin_port = 0;  /* With port set to 0, system will allocate a free port */
			  /* from 1024 to (64K -1) */

	/* Associate the socket with local client IP address and port */
	if(bind(sock,(struct sockaddr *)&clientAddress,sizeof(clientAddress))<0)
	{
		perror("cannot bind");
		close(sock);
		return(ER_BIND_FAILED);	/* bind failed */
	}


	/* Initialize serverAddress memory to 0 */
	memset((char *) &serverAddress, 0, sizeof(serverAddress));

	/* Set ftp server ftp address in serverAddress */
	serverAddress.sin_family = AF_INET;
	memcpy((char *) &serverAddress.sin_addr, serverIPstructure->h_addr, 
			serverIPstructure->h_length);
	serverAddress.sin_port = htons(SERVER_FTP_PORT);

	/* Connect to the server */
	if (connect(sock, (struct sockaddr *) &serverAddress, sizeof(serverAddress)) < 0)
	{
		perror("Cannot connect to server ");
		close (sock); 	/* close the control connection socket */
		return(ER_CONNECT_FAILED);  	/* error return */
	}


	/* Store listen socket number to be returned in output parameter 's' */
	*s=sock;

	return(OK); /* successful return */
}  // end of clntConnect() */

/*
 * svcInitServer
 *
 * Function to create a socket and to listen for connection request from client
 *    using the created listen socket.
 *
 * Parameters
 * s		- Socket to listen for connection request (output)
 * port		- Port number this server will listen on
 * 
 * Return status
 *	OK			- Successfully created listen socket and listening
 *	ER_CREATE_SOCKET_FAILED	- socket creation failed
 */

int svcInitServer (
	int *s 		/*Listen socket number returned from this function */
	)
{
	int sock;
	struct sockaddr_in svcAddr;
	int qlen;

	/*create a socket endpoint, check for errors, less than 0 value indicates error hit */
	if((sock=socket(AF_INET, SOCK_STREAM,0)) <0)
	{
		perror("cannot create socket");
		return(ER_CREATE_SOCKET_FAILED);
	}

	/*initialize memory of svcAddr structure to zero. */
	memset((char *)&svcAddr,0, sizeof(svcAddr));

	/* initialize svcAddr to have server IP address and server listen port#. */
	svcAddr.sin_family = AF_INET;
	svcAddr.sin_addr.s_addr=htonl(INADDR_ANY);  /* server IP address */
	svcAddr.sin_port=htons(DATA_CONNECTION_FTP_PORT);    /* server listen port # */

	/*
	 * bind (associate) the listen socket number with server IP and port#.
	 * bind is a socket interface function
	 */
	if(bind(sock,(struct sockaddr *)&svcAddr,sizeof(svcAddr))<0)
	{
		perror("cannot bind");
		close(sock);
		return(ER_BIND_FAILED);	/* bind failed */
	}

	/*
	 * Set listen queue length to 1 outstanding connection request.
	 * This allows 1 outstanding connect request from client to wait
	 * while processing current connection request, which takes time.
	 * It prevents connection request to fail and client to think server is down
	 * when in fact server is running and busy processing connection request.
	 */
	qlen=1;


	/*
	 * Listen for connection request to come from client ftp.
	 * This is a non-blocking socket interface function call,
	 * meaning, server ftp execution does not block by the 'listen' funcgtion call.
	 * Call returns right away so that server can do whatever it wants.
	 * The TCP transport layer will continuously listen for request and
	 * accept it on behalf of server ftp when the connection requests comes.
	 */

	listen(sock,qlen);  /* socket interface function call */

	/* Store listen socket number to be returned in output parameter 's' */
	*s=sock;

	return(OK); /*successful return */
}




/*
 * sendMessage
 *
 * Function to send specified number of octet (bytes) to client ftp
 *
 * Parameters
 * s		- Socket to be used to send msg to client (input)
 * msg  	- Pointer to character arrary containing msg to be sent (input)
 * msgSize	- Number of bytes, including NULL, in the msg to be sent to client (input)
 *
 * Return status
 *	OK		- Msg successfully sent
 *	ER_SEND_FAILED	- Sending msg failed
 */

int sendMessage(
	int s, 		/* socket to be used to send msg to client */
	char *msg, 	/*buffer having the message data */
	int msgSize 	/*size of the message/data in bytes */
	)
{
	int i;


	/* Print the message to be sent byte by byte as character */
	for(i=0;i<msgSize;i++)
	{
		printf("%c",msg[i]);
	}
	printf("\n");

	if((send(s,msg,msgSize,0)) < 0) /* socket interface call to transmit */
	{
		perror("unable to send ");
		return(ER_SEND_FAILED);
	}

	return(OK); /* successful send */
}


/*
 * receiveMessage
 *
 * Function to receive message from client ftp
 *
 * Parameters
 * s		- Socket to be used to receive msg from client (input)
 * buffer  	- Pointer to character arrary to store received msg (input/output)
 * bufferSize	- Maximum size of the array, "buffer" in octent/byte (input)
 *		    This is the maximum number of bytes that will be stored in buffer
 * msgSize	- Actual # of bytes received and stored in buffer in octet/byes (output)
 *
 * Return status
 *	OK			- Msg successfully received
 *	ER_RECEIVE_FAILED	- Receiving msg failed
 */

int receiveMessage (
	int s, 		/* socket */
	char *buffer, 	/* buffer to store received msg */
	int bufferSize, /* how large the buffer is in octet */
	int *msgSize 	/* size of the received msg in octet */
	)
{
	int i;

	*msgSize=recv(s,buffer,bufferSize,0); /* socket interface call to receive msg */

	if(*msgSize<0)
	{
		perror("unable to receive");
		return(ER_RECEIVE_FAILED);
	}


	/* Print the received msg byte by byte */
	for(i=0;i<*msgSize;i++)
	{
		printf("%c", buffer[i]);
	}
	printf("\n");
	
	return (OK);
}


/*
 * receiveDataMessage
 *
 * Used to receive data from the server quietly; this way I dont have to refactor receiveMessage
 *
 * Parameters
 * s		- Socket to be used to receive msg from client (input)
 * buffer  	- Pointer to character arrary to store received msg (input/output)
 * bufferSize	- Maximum size of the array, "buffer" in octent/byte (input)
 *		    This is the maximum number of bytes that will be stored in buffer
 * msgSize	- Actual # of bytes received and stored in buffer in octet/byes (output)
 *
 * Return status
 *	OK			- Msg successfully received
 *	ER_RECEIVE_FAILED	- Receiving msg failed
 */

int receiveDataMessage (
	int s, 		/* socket */
	char *buffer, 	/* buffer to store received msg */
	int bufferSize, /* how large the buffer is in octet */
	int *msgSize 	/* size of the received msg in octet */
	)
{

	*msgSize=recv(s,buffer,bufferSize,0); /* socket interface call to receive msg */

	if(*msgSize<0)
	{
		perror("unable to receive");
		return(ER_RECEIVE_FAILED);
	}

	return (OK);
}




/*
 * clntExtractReplyCode
 *
 * Function to extract the three digit reply code 
 * from the server reply message received.
 * It is assumed that the reply message string is of the following format
 *      ddd  text
 * where ddd is the three digit reply code followed by or or more space.
 *
 * Parameters
 *	buffer	  - Pointer to an array containing the reply message (input)
 *	replyCode - reply code number (output)
 *
 * Return status
 *	OK	- Successful (returns always success code
 */

int clntExtractReplyCode (
	char	*buffer,    /* Pointer to an array containing the reply message (input) */
	int	*replyCode  /* reply code (output) */
	)
{
	/* extract the codefrom the server reply message */
   sscanf(buffer, "%d", replyCode);

   return (OK);
}  // end of clntExtractReplyCode()

