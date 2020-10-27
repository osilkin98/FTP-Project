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

/* succ hahaha */ 
#define OK_SERVER_SUCC "200 The requested action has been successfully completed.\r\n"
#define OK_HELP "214 List of server commands is as follows:\r\n"
#define OK_CLOSING_CONTROL_CONN "221 Service closing control connection.\r\n"
#define OK_USERNAME_NEED_PWORD "331 password required for %s\r\n"

#define ERR_COMMAND_NOT_FOUND "500 Syntax error, command unrecognized and the requested action did not take place. This may include errors such as command line too long.\r\n"
#define ERR_PARAM_SYNTAX_ERR "501 Syntax error in parameters or arguments.\r\n"
#define ERR_FAILED_TO_LOGIN "530 Failed to login\r\n"
#define ERR_NOT_LOGGED_IN "530 Not Logged In.\r\n"


/* login state */
#define SESH_LOGGED_OUT 0
#define SESH_USER 1
#define SESH_GUEST 2

#define USER_GUEST "guest"

/* set the username length based on what each UNIX system defines */

#if defined(__linux__) || defined(__gnu_linux__)
	#define USERNAME_LENGTH 32  /* Linux max username length set to 32 */
#elif defined(sun) || defined(__sun)
	#if defined(__SunOS_5_11)
		#define USERNAME_LENGTH 12  /* SunOS 5.11 can handle usernames 12 chars in len, however an override must be made */ 
	#else
		#define USERNAME_LENGTH 8  /* standard SunOS has a maximum of 8 technically */ 
	#endif
#elif defined(_WIN32) || defined(_WIN16) || defined(_WIN64) || defined(__WINDOWS__) || defined(__TOS_WIN__) || defined(__WIN32__)
	#define USERNAME_LENGTH 20
#elif (defined(__APPLE__) && defined(__MACH__)) || defined(macintosh)
	#define USERNAME_LENGTH 20
#elif defined(__unix__) || defined(__unix)
	#define USERNAME_LENGTH 8 /* generic unix definition */ 
#endif

/* will add more commands as we go on */
const char *CMD_LIST[] = {"help", "quit", "user"};
#define NUM_CMDS 3


/* Function prototypes */
int svcInitServer(int *s);
int sendMessage (int s, char *msg, int  msgSize);
int receiveMessage(int s, char *buffer, int  bufferSize, int *msgSize);



/* Returns information about a specific command, or lists the available 
	commands if nothing is passed in */
char * help(const argc, const char **, char *replyStr);

/* Checks if the user credentials are correct, returns 1 if successful, 0 otherwise 
	Obviously this is not cryptographically secure and should not be treated as such
*/
int login(const char *user, const char *pword, const int allow_guest, int *logged_in);

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


	int require_login = 0; 
	int allow_guest = 0;

	/* Parse arguments */
	if (argc > 1) {
		for (int i = 1; i < argc; i++) {
			if(strcmp(argv[i], "--require-login") == 0) {
				require_login = 1;
				printf("[server]: starting with logins required\r\n");
			}

			if(strcmp(argv[i], "--allow-guest") == 0) {
				allow_guest = 1;
				printf("[server]: guest logins enabled, guest account: %s\r\n", USER_GUEST);
			}
		}
	}

	/*
	 * NOTE: without \r\r\n at the end of format string in printf,
         * UNIX will buffer (not flush)
	 * output to display and you will not see it on monitor.
	*/
	printf("[server]: Started execution of server ftp\r\n");


	 /*initialize server ftp*/
	printf("[server]: Initialize ftp server\r\n");	/* changed text */

	status=svcInitServer(&listenSocket);
	if(status != 0)
	{
		printf("[server]: Exiting server ftp due to svcInitServer returned error\r\n");
		exit(status);
	}


	printf("[server]: FTP Server is listening for connections on socket %d\r\n", listenSocket);


	int connection_no = 0;

	/* wait until connection request comes from client ftp */
	while(connection_no < 3) {

		int should_quit = 0;
		int logged_in = 0;
		char username[USERNAME_LENGTH]; 


		ccSocket = accept(listenSocket, NULL, NULL);


		printf("[server]: Came out of accept() function \r\n"); 

		if(ccSocket < 0)
		{
			perror("[server]: cannot accept connection:");
			printf("[server]: Server ftp is terminating after closing listen socket.\r\n");
			close(listenSocket);  /* close listen socket */
			return (ER_ACCEPT_FAILED);  // error exist
		}

		printf("[server] Connected to client, calling receiveMsg to get ftp cmd from client \r\n");


		/* Receive and process ftp commands from client until quit command.
		* On receiving quit command, send reply to client and 
			* then close the control connection socket "ccSocket". 
		*/
		do {

			/* Receive client ftp commands until */
			status=receiveMessage(ccSocket, userCmd, sizeof(userCmd), &msgSize);
			if(status < 0)
			{
				printf("[server] Failed to receive the message. Closing control connection \r\n");
				printf("[server] Server ftp is terminating.\r\n");
				break;
			}

			/* clear out the replyMsg */
			strcpy(replyMsg, "");


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
				printf("[server] No command received!\r\n");
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

			printf("[server]: Received cmd: [%s] ", cmd);
			for(size_t i = 0; i < nargs; i++) {
				printf("[%s] ", args[i]);
			}
			printf("\r\n");
			fflush(stdout);

			/* basic command matching, future version should either use a switch or a prefix trie */ 
			if(strcmp(cmd, "help") == 0) {
				// valgrind should scrutinze this
				help(nargs, args, replyMsg);
			} else if(strcmp(cmd, "quit") == 0) {
				/* logout here */
				if(logged_in) {
					printf("[server]: logging out... ");
					fflush(stdout);
					strcpy(username, "");
					logged_in = SESH_LOGGED_OUT;
					printf("done\r\n");
				}
				strcpy(replyMsg, OK_CLOSING_CONTROL_CONN);
				should_quit = 1;
			} else if (strcmp(cmd, "user") == 0) {
				/* get the username */ 
				if (nargs == 0) {
					printf("[server] user did not provide a username\r\n");
					strcpy(replyMsg, ERR_PARAM_SYNTAX_ERR);
				} else {
					char *_user = args[0];
					printf("[server]: '%s' is attempting to login\r\n", _user);
					if(login(_user, NULL, allow_guest, &logged_in)) {
						strcpy(username, _user);
						printf("[server]: '%s' has logged in\r\n", username);
						strcpy(replyMsg, "230 Successful login\r\n");
					} else {
						printf("[server]: unsuccessful login attempt for %s\r\n", _user);
						strcpy(replyMsg, ERR_FAILED_TO_LOGIN);
					}
				}


			} else {
				/* the command isn't recognized */
				strcpy(replyMsg, ERR_COMMAND_NOT_FOUND);
			}


			/* reset the args list */ 
			free(args);
			args = NULL;

			/*
			* ftp server sends only one reply message to the client for 
			* each command received in this implementation.
			*/
			/* strcpy(replyMsg,"200 cmd okay\r\r\n"); */
			/* Should have appropriate reply msg starting HW2 */


			;	/* Added 1 to include NULL character in */
					/* the reply string strlen does not count NULL character */
			/* clear out the reply string for the next run */
			status=sendMessage(ccSocket,replyMsg,strlen(replyMsg) + 1);

			if(status < 0) {
				printf("[server] received negative return status when sending a message: %d\r\n", status);
				should_quit = 1;
			}

		}
		while(!should_quit);

		printf("[server] Closing control connection socket.\r\n");
		close (ccSocket);  /* Close client control connection socket */
		
		// increment the connection number for now
		connection_no++;
	}
	printf("[server] Stopping listen on socket %d... ", listenSocket);
	fflush(stdout);
	close(listenSocket);  /*close listen socket */
	printf("[server] done\r\n");

	if (status != 0) 
		printf("[server] Server exited with non-zero status code: %d\r\n", status);

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
	printf("Initializing server on port %d\r\r\n", SERVER_FTP_PORT);
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
	printf("\r\r\n");

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
	printf("\r\r\n");

	return (OK);
}


/**************** FTP COMMANDS **************/
char * help(const int arg_count, const char **args_list, char *reply_str) {
	char *response;
	string_init(&response);
	string_set(&response, OK_HELP);

	for (size_t i = 0; i < NUM_CMDS; i++) {
		dyn_concat(&response, CMD_LIST[i]);
		dyn_concat(&response, "\r\r\r\n");
	}
	strcpy(reply_str, response);
	string_free(&response);
	return reply_str; 
}


/* TODO: implement user sessions */
int login(const char *username, const char *pword, const int allow_guest, int *logged_in) {
	if(allow_guest) {
		printf("[server] logged in as %s\r\n", USER_GUEST);
		*logged_in = SESH_GUEST;
	} else {
		printf("[server] user login not implemented yet\r\n");
		*logged_in = SESH_LOGGED_OUT;
	}
	return *logged_in;
}