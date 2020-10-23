/*
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
#include <stdlib.h> /* for malloc/calloc/free */
#include <stdio.h> /* for printf()/scanf() IO functions */
#include <unistd.h> /* for close() */
#include "dynamic_string.h"

#define SERVER_FTP_PORT 4200


/* Error and OK codes */
#define OK 0
#define ER_INVALID_HOST_NAME -1
#define ER_CREATE_SOCKET_FAILED -2
#define ER_BIND_FAILED -3
#define ER_CONNECT_FAILED -4
#define ER_SEND_FAILED -5
#define ER_RECEIVE_FAILED -6
#define ER_ACCEPT_FAILED -7


/* Function prototypes */

int svcInitServer(int *s);
int sendMessage (int s, char *msg, int  msgSize);
int receiveMessage(int s, char *buffer, int  bufferSize, int *msgSize);

/* will add more commands as we go on */
const char *CMD_LIST[] = {"help", "quit"};
#define NUM_CMDS 2


/* Returns information about a specific command, or lists the available 
	commands if nothing is passed in */
char * help(const argc, const char **);

/* List of all global variables */

char userCmd[1024];	/* user typed ftp command line received from client */
char cmd[1024];		/* ftp command (without argument) extracted from userCmd */
char argument[1024];	/* argument (without ftp command) extracted from userCmd */
char replyMsg[1024];       /* buffer to send reply message to client */


/*
 * main
 *
 * Function to listen for connection request from client
 * Receive ftp command one at a time from client
 * Process received command
 * Send a reply message to the client after processing the command with staus of
 * performing (completing) the command
 * On receiving QUIT ftp command, send reply to client and then close all sockets
 *
 * Parameters
 * argc		- Count of number of arguments passed to main (input)
 * argv  	- Array of pointer to input parameters to main (input)
 *		   It is not required to pass any parameter to main
 *		   Can use it if needed.
 *
 * Return status
 *	0			- Successful execution until QUIT command from client 
 *	ER_ACCEPT_FAILED	- Accepting client connection request failed
 *	N			- Failed stauts, value of N depends on the command processed
 */

int main(const int argc, const char ** argv)
{
	/* List of local varibale */

	int msgSize;        /* Size of msg received in octets (bytes) */
	int listenSocket;   /* listening server ftp socket for client connect request */
	int ccSocket;        /* Control connection socket - to be used in all client communication */
	int status;


	/*
	 * NOTE: without \n at the end of format string in printf,
         * UNIX will buffer (not flush)
	 * output to display and you will not see it on monitor.
	*/
	printf("Started execution of server ftp\n");


	 /*initialize server ftp*/
	printf("Initialize ftp server\n");	/* changed text */

	status=svcInitServer(&listenSocket);
	if(status != 0)
	{
		printf("Exiting server ftp due to svcInitServer returned error\n");
		exit(status);
	}


	printf("[server] FTP Server is listening for connections on socket %d\n", listenSocket);

	/* wait until connection request comes from client ftp */
	ccSocket = accept(listenSocket, NULL, NULL);

	printf("[server] Came out of accept() function \n");

	if(ccSocket < 0)
	{
		perror("[server] cannot accept connection:");
		printf("[server] Server ftp is terminating after closing listen socket.\n");
		close(listenSocket);  /* close listen socket */
		return (ER_ACCEPT_FAILED);  // error exist
	}

	printf("[server] Connected to client, calling receiveMsg to get ftp cmd from client \n");


	/* Receive and process ftp commands from client until quit command.
 	 * On receiving quit command, send reply to client and 
         * then close the control connection socket "ccSocket". 
	 */
	do
	{
	    /* Receive client ftp commands until */
 	    status=receiveMessage(ccSocket, userCmd, sizeof(userCmd), &msgSize);
	    if(status < 0)
	    {
			printf("[server] Failed to receive the message. Closing control connection \n");
			printf("[server] Server ftp is terminating.\n");
			break;
	    }


	   /* Separate command and argument from userCmd */

		char temp[1024];          //tmp array
		char *ptr;

		/* dynamically-allocated argument array */
		char **args = NULL; 
		size_t nargs = 0, args_cap = 1; 
		args = malloc(sizeof(char *) * args_cap);

		/* store the user command into a temp array */ 
		strcpy(temp, userCmd); 
		ptr = strtok(temp, " ");

		/* get the command */
        if (ptr != NULL){
            strcpy(cmd, ptr);       
        } else {
            printf("[server] No command received!\n");
            continue;
        }

		/* store each token within a dynamic array  */
		while((ptr = strtok(NULL, " ")) != NULL) {
			/* expand dong */
			if(args_cap <= nargs) {
				/* grow the array geometrically */
				args_cap *= 2;
				args = realloc(args, sizeof(char *) * args_cap);
			}

			/* store the pointer and increment the arg count */
			args[nargs] = ptr;
			nargs++; 
		}

		if(strcmp(cmd, "help") == 0) {
			// valgrind should scrutinze this
			char *helpstr = help(nargs, args);
			strcpy(replyMsg, helpstr);
			string_free(&helpstr);
		}

		/* reset the args list */ 
		free(args);
		args = NULL;


	    /*
 	     * ftp server sends only one reply message to the client for 
	     * each command received in this implementation.
	     */
	    /* strcpy(replyMsg,"200 cmd okay\n"); */
		/* Should have appropriate reply msg starting HW2 */


	    status=sendMessage(ccSocket,replyMsg,strlen(replyMsg) + 1);	/* Added 1 to include NULL character in */
				/* the reply string strlen does not count NULL character */
		/* clear out the reply string for the next run */
		strcpy(replyMsg, "");

	    if(status < 0)
	    {
		break;  /* exit while loop */
	    }
	}
	while(strcmp(cmd, "quit") != 0);

	printf("[server] Closing control connection socket.\n");
	close (ccSocket);  /* Close client control connection socket */

	printf("[server] Stopping listen on socket %d... ", ccSocket);
	fflush(stdout);
	close(listenSocket);  /*close listen socket */
	printf("[server] done\n");

	if (status != 0) 
		printf("[server] Server exited with non-zero status code: %d\n", status);

	return (status);
}


/*
 * svcInitServer
 *
 * Function to create a socket and to listen for connection request from client
 *    using the created listen socket.
 *
 * Parameters
 * s		- Socket to listen for connection request (output)
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

	/*create a socket endpoint */
	if( (sock=socket(AF_INET, SOCK_STREAM,0)) <0)
	{
		perror("cannot create socket");
		return(ER_CREATE_SOCKET_FAILED);
	}

	/*initialize memory of svcAddr structure to zero. */
	memset((char *)&svcAddr,0, sizeof(svcAddr));

	/* initialize svcAddr to have server IP address and server listen port#. */
	printf("Initializing server on port %d\n", SERVER_FTP_PORT);
	svcAddr.sin_family = AF_INET;
	svcAddr.sin_addr.s_addr=htonl(INADDR_ANY);  /* server IP address */
	svcAddr.sin_port=htons(SERVER_FTP_PORT);    /* server listen port # */

	/* bind (associate) the listen socket number with server IP and port#.
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
	int    s,	/* socket to be used to send msg to client */
	char   *msg, 	/* buffer having the message data */
	int    msgSize 	/* size of the message/data in bytes */
	)
{
	int i;


	/* Print the message to be sent byte by byte as character */
	for(i=0; i<msgSize; i++)
	{
		printf("%c",msg[i]);
	}
	printf("\n");

	if((send(s, msg, msgSize, 0)) < 0) /* socket interface call to transmit */
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
 *	R_RECEIVE_FAILED	- Receiving msg failed
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


/**************** FTP COMMANDS **************/
char * help(const int arg_count, const char **args_list) {
	char *response;
	string_init(&response);
	string_set(&response, "List of available commands:\n");

	for (size_t i = 0; i < NUM_CMDS; i++) {
		dyn_concat(&response, CMD_LIST[i]);
		dyn_concat(&response, "\n");
	}

	return response; 
}


