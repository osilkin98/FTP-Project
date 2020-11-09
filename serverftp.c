/*
 * NOTE: Starting homework #2, add more comments here describing the overall function
 * performed by server ftp program
 * This includes, the list of ftp commands processed by server ftp.
 *
 */

#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h> /* for using the stat() command to obtain statistics on a file */
#include <netdb.h>
#include <limits.h> /* for PATH_MAX */
#include <string.h>
#include <stdlib.h> /* for malloc/calloc/free */
#include <stdio.h> /* for printf()/scanf() IO functions */
#include <unistd.h> /* for close(), getcwd(),  */
#include <stdbool.h> /* includes macros for boolean datatypes and values instead of relying on int */
#include "dynamic_string.h" /* for dynamic concatenation of strings */
#include <dirent.h> /* for using directory functions */
#include <errno.h> /* for error handling */

/* stores the error status from system functions, to be used for the code return logic  */
extern int errno;


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
#define ERR_MISSING_ARGUMENT "501 No argument provided for command.\r\n"
#define ERR_FAILED_TO_LOGIN "530 Failed to login\r\n"
#define ERR_NOT_LOGGED_IN "530 Not Logged In.\r\n"
#define ERR_INTERNAL_ERROR "451 Requested action aborted. Local error in processing.\r\n"

#define MAX_CONNECTION_COUNT 1

/* login state */
#define SESH_LOGGED_OUT 0
#define SESH_LOGGING_IN 1
#define SESH_LOGGED_IN 2

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

/* ON a UNIX system, the max length of a path is 4096 characters */
#define WORKINGPATH_MAX_LENGTH 4096
#define BUFFER_LENGTH 4096  /* this needs to be reduced back to 1024 and pwd be sent over the data connection */

/* Define a macro for the hostname */
#define SERVER_HOSTNAME "localhost"  /* set to localhost when working from a local system */


/* Commands to implement:
// 	<user>	username
	<pass> 	password
//	<help>
	<mkdir>	[directory...]
	<rmdir>	[directory...]
	<cd> directory
	<rm> filepath
	<ls> [-alfh] [directory]
	<pwd>
	<send> filename
	<recv> filename
//	<quit>
	<stat> [-f] filepath
*/


/* will add more commands as we go on */
const char *CMD_LIST[] = {
	"user",	// implemented
	"pass",
	"pwd", //
	"cd", //
	"ls",
	"rm",
	"mkdir",
	"rmdir",
	"stat",
	"send",
	"recv",
	"quit", //
	"help", //
};
#define NUM_CMDS 13

const char *GUEST_CMDS[] = {
	"user", "pass", "stat", "help", "quit"
};
#define NUM_GUEST_CMDS 5


const char *REQUIRE_ARGS[] = {
	"user", "pass", "cd", "mkdir", "rmdir", "send", "recv"
};
#define NUM_REQUIRE_ARGS 7

/* set the FTP server users here */
const char *USERNAMES[] = {
	"oleg",
	"guest",
	"ken",
	"doug",
	"jared"
};

const char *PASSWORDS[] = {
	"password",
	"guest",
	"FactsAndLogic5",
	"dragons",
	"bballer23",
};

#define NUM_USERS 5


/* Function prototypes */
int svcInitServer(int *s);
int sendMessage (int s, char *msg, int  msgSize);
int receiveMessage(int s, char *buffer, int  bufferSize, int *msgSize);
int clntConnect(char *serverName, int *);


/* Returns information about a specific command, or lists the available
	commands if nothing is passed in */
char * help(char *replyStr);

/* Checks if the user credentials are correct, returns 1 if successful, 0 otherwise
	Obviously this is not cryptographically secure and should not be treated as such
*/
int login(const char *user, const char *pword, const int allow_guest, int *logged_in);

/* Copies current directory into cd_buffer, and a reply message into replyStr */
char * get_cwd(char *replyStr, char *cd_buffer);


/* Lists the files within the filepath specified by dir_buffer and copies them into replyStr */
char * ls_dir(char *replyStr, char *dir_buffer, size_t buf_size);

/* List of all global variables */
char userCmd[BUFFER_LENGTH];	/* user typed ftp command line received from client */
char cmd[BUFFER_LENGTH];		/* ftp command (without argument) extracted from userCmd */
char argument[BUFFER_LENGTH];	/* argument (without ftp command) extracted from userCmd */
char replyMsg[BUFFER_LENGTH];       /* buffer to send reply message to client */

char working_dir[PATH_MAX];



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
	int data_socket;  /* data connection socket */
	int status;
	int connection_no = 0; /* for exiting after a set amount of connections */

	/* login settings */
	//bool require_login = false;


	/* stores the working directory so the process can reset after every call */
	if(getcwd(working_dir, PATH_MAX) == NULL) {
		perror("[server]: couldn't write directory to working_dir\r\n");
	}

	/* display the originating working directory */
	printf("[server]: working_dir: %s\r\n", working_dir);


	/* Parse arguments */
	if (argc > 1) {
		for (int i = 1; i < argc; i++) {
			/*
			if(strcmp(argv[i], "--require-login") == 0) {
				require_login = true;
				printf("[server]: starting with logins required\r\n");
			}
			*/
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


	/* Initialize the server and store the socket in listenSocket */
	status=svcInitServer(&listenSocket);
	if(status != 0)
	{
		printf("[server]: Exiting server ftp due to svcInitServer returned error\r\n");
		exit(status);
	}


	printf("[server]: FTP Server is listening for connections on socket %d\r\n", listenSocket);


	/* wait until connection request comes from client ftp */
	while(connection_no < MAX_CONNECTION_COUNT) {


		/* track whether or not we should be logging out */
		bool should_quit = false;

		/* login information, required for establishing data connection if --require-login flag is set */
		int logged_in = SESH_LOGGED_OUT; /* stores the login state of the user session */
		char username[USERNAME_LENGTH]; /* pointer to the username */

		/* used for directory switching */
		char current_dir[PATH_MAX];


		/* reset the working directory to where the program runs */
		if(chdir(working_dir) != 0) {
			printf("[server]: unable to reset current_dir to %s\r\n", working_dir);
			printf("[server]: current_dir: %s\r\n", current_dir);
		}

		/* reset the current_dir variable */
		strcpy(current_dir, working_dir);
		printf(" -> %s\r\n", current_dir);

		ccSocket = accept(listenSocket, NULL, NULL);

		printf("[server]: Came out of accept() function \r\n");

		if(ccSocket < 0)
		{
			perror("[server]: cannot accept connection:");
			printf("[server]: Server ftp is terminating after closing listen socket.\r\n");
			close(listenSocket);  /* close listen socket */
			return (ER_ACCEPT_FAILED);  // error exist
		}

		printf("[server] Connected to client, sending a welcome message \r\n");

		/* attempt to send the welcome message, if it breaks then close the connection
		 	and continue onto the next iteration of the loop */
		sprintf(replyMsg, "\r\n~~220 Welcome to Oleg's FTP Server! Drinks are on the house!~~\r\n");
		if(send(ccSocket, replyMsg, strlen(replyMsg) + 1, 0) < 0) {
			fprintf(stderr, "[server]: failed to send welcome message!\r\n");
			printf("[server]: closing control connection\r\n");
			close(ccSocket);
			connection_no++;
			continue;
		}


		/* Receive and process ftp commands from client until quit command.
		* On receiving quit command, send reply to client and
			* then close the control connection socket "ccSocket".
		*/
		do {

			char temp[BUFFER_LENGTH];  /* the user command will be copied into this buffer so it can be parsed without breaking the original */
			char *ptr = NULL;   /* pointer to be used when processing strtok */
			char *arg = NULL;	/* pointer to process & validate the argument */

			/* For validating the user's command */
			bool is_valid_cmd = false;
			bool requires_args = false;
			bool valid_perms = false;

			/* Receive client ftp commands until */
			status=receiveMessage(ccSocket, userCmd, sizeof(userCmd), &msgSize);
			if(status < 0)
			{
				printf("[server] Failed to receive the message. Closing control connection \r\n");
				printf("[server] Server ftp is terminating.\r\n");
				break;
			}

			/*
				Parse the input received from the client
			*/

			strcpy(temp, userCmd); /* Store a copy of the passed commadn into the temp array */
			strcpy(replyMsg, ""); /* clear out the reply message */


			/* get the command from the temp buffer */
			ptr = strtok(temp, " ");

			/* get the command */
			if (ptr != NULL){
				strcpy(cmd, ptr);
				printf("[server]: received command [%s]\r\n", cmd);
			} else {
				printf("[server] No command received!\r\n");
				continue;
			}

			/* get the argument if any */
			if ((arg = strtok(NULL, "\0")) != NULL) {
				strcpy(argument, arg);
				printf("[server]: Obtained arg: \"%s\"\r\n", argument);
			}

			/*
				Check that the entered command is within the
				list of supported commands
				and that it has an argument if required
			*/
			for (size_t i = 0; i < NUM_CMDS; i++) {

				/* the command is supported by the server, but we need to check
					that the user provided an argument if required
				*/
				if (strcmp(cmd, CMD_LIST[i]) == 0) {

					/* user can access all commands if logged in  */
					if (logged_in == SESH_LOGGED_IN) {
						valid_perms = true;
					}


					/*
						make sure that the user can't access modifying functions
					   if they aren't logged in
					*/
					else {
						/* the state is either LOGGED_OUT or LOGGING_IN so check to make sure that
							the command entered is one of the guest commands
						*/

						for(size_t k = 0; k < NUM_GUEST_CMDS; k++) {

							/* user is accessing one of the allowed commands */
							if (strcmp(GUEST_CMDS[k], CMD_LIST[i]) == 0) {
								valid_perms = true;
								break;
							}
						}
						/* break here if the user doesn't have the necessary permissions */
						if (!valid_perms) {
							printf("[server]: user tried accessing %s without being logged in\r\n", cmd);
							strcpy(replyMsg, "530 You need to be logged in to use this command\r\n");
							break;
						}
					}

					/*
						at this point it's established that the user has valid permissions
						so we can safely proceed without issue
					*/

					for (size_t j = 0; j < NUM_REQUIRE_ARGS; j++) {
						if (strcmp(cmd, REQUIRE_ARGS[j]) == 0) {
							requires_args = true;
						}
					}

					if(!requires_args || (requires_args && arg != NULL)) {
						printf("[server]: command %s is valid\r\n", cmd);
						is_valid_cmd = true;
					}

					else {
						printf("[server]: no args provided for %s\r\n", cmd);
						strcpy(replyMsg, ERR_MISSING_ARGUMENT);
					}
					break;
				}
			}

			/*********** COMMANDS ********************/

			/* Commands to implement:
			// 	<user>	username
			//	<pass> 	password
			//	<help>
				<mkdir>	[directory...]
				<rmdir>	[directory...]
			//	<cd> directory
				<rm> filepath
				<ls> [-alfh] [directory]
			//	<pwd>
				<send> filename
				<recv> filename
			//	<quit>
				<stat> [-f] filepath

			*/




			if (is_valid_cmd) {

				int cmd_status = 0; /* Stores the return status from system() calls */
				const char *error_msg = NULL; /* stores the error pointer from strerror(errno) */

				if (strcmp(cmd, "user") == 0) {
					/* set the login state */
					logged_in = SESH_LOGGING_IN;

					/* store the username we're attempting to login with
						NOTE: we don't care if it's right or not, that'll be decided in PASS
					*/
					strcpy(username, argument);

					/* get the username */
					printf("[server]: client trying to login with username '%s'\r\n", argument);
					sprintf(replyMsg, "331 Please specify the password for '%s'\r\n", username);
				}

				/* password command, session state should be set to logging in,
					otherwise we break */
				else if (strcmp(cmd, "pass") == 0) {
					if(logged_in == SESH_LOGGING_IN) {

						char *password = argument; 

						/* we need to iterate and find the password to the corresponding
							username to compare it against what the user passed in */
						for (size_t k = 0; k < NUM_USERS; k++) {

							/* here the password either matches or it doesn't */
							if(strcmp(username, USERNAMES[k]) == 0) {

								/* successful login */
								if (strcmp(PASSWORDS[k], password) == 0) {

									/* set the session state to logged in */
									logged_in = SESH_LOGGED_IN;
									break;
								}
							}
						}
						/* check if the login was successful */
						if (logged_in == SESH_LOGGED_IN) {
							printf("[server]: user '%s' logged in.\r\n", username);
							sprintf(replyMsg, "230 Successfully logged in as '%s'\r\n", username);
						}

						/* set the failstate */
						else {
							printf("[server]: unsucessful login, check username or password\r\n");
							strcpy(replyMsg, "430 Invalid username or password\r\n");
							logged_in = SESH_LOGGED_OUT;
						}
					}

					/* incorrect session state to be calling PASS */
					else {
						printf("[server]: called pass before user\r\n");
						strcpy(replyMsg, "503 Called user before pass\r\n");
					}
				}


				/* basic command matching, future version should either use a switch or a prefix trie */
				else if(strcmp(cmd, "help") == 0) {
					// valgrind should scrutinze this
					help(replyMsg);
				}

				else if(strcmp(cmd, "quit") == 0) {
					/* logout here */
					if(logged_in) {
						printf("[server]: logging out... ");
						fflush(stdout);
						strcpy(username, "");
						logged_in = SESH_LOGGED_OUT;
						printf("done\r\n");
					}
					strcpy(replyMsg, OK_CLOSING_CONTROL_CONN);
					should_quit = true;
				}

				else if (strcmp(cmd, "stat") == 0) {
					sprintf(replyMsg, "211 Type: ASCII, Mode: Stream\r\n");
				}

				/* Filesystem functions:
				 	there are two classes of functions: passive and modifying
					passive functions: only require read permission,
					modifying functions: require a non-guest user session to write to the file system

					passive commands:
						- ls
						- cd
						- pwd
						- stat
						- get/recv

					modifying commands:
						- rm
						- rmdir
						- mkdir
						- put/send

				*/

				else if (strcmp(cmd, "pwd") == 0) {
					get_cwd(replyMsg, current_dir);
				}

				else if (strcmp(cmd, "cd") == 0) {
						cmd_status = chdir(argument);

						/* change directory was successful */
						if (cmd_status == 0) {
							strcpy(replyMsg, "200 change directory successful\r\n");
							getcwd(current_dir, PATH_MAX);
							printf("[server]: client changed directory to '%s'\r\n", current_dir);
						}

						else {
							error_msg = strerror(errno);
							strcpy(replyMsg, ERR_INTERNAL_ERROR);
							printf("[server]: replymsg: %s\r\n", replyMsg);
							fprintf(stderr, "[server]: cannot cd into %s: %s\r\n", argument, error_msg);
						}
				}


				else if (strcmp(cmd, "mkdir") == 0) {

					/* User + Group can read, write, and execute in directory, others may only read & execute*/
					mode_t directory_mode = 0775;

					cmd_status = mkdir(argument, directory_mode);

					if(cmd_status == 0) {
						printf("[server]: Created directory \"%s\" with mode %04o\r\n", argument, directory_mode);
						strcpy(replyMsg, "212 Successfully created directory\r\n");
					}


					/* errno is set */
					else if (cmd_status == -1) {
						error_msg = strerror(errno);
						fprintf(stderr, "[server]: could not create directory %s: %s\r\n", argument, error_msg);
						strcpy(replyMsg, "550 ");
						strcat(replyMsg, error_msg);
						strcat(replyMsg, "\r\n");
					}
				}

				else if (strcmp(cmd, "rmdir") == 0) {
					printf("[server]: attempting to remove directory \"%s\"\r\n", argument);

					cmd_status = rmdir(argument);
					if (cmd_status == 0) {
						printf("[server]: successfully removed directory \"%s\"\r\n", argument);
						strcpy(replyMsg, "200 directory removed\r\n");
					}

					/* status is -1 and errno is set so we return system information */
					else {
						error_msg = strerror(errno);
						fprintf(stderr, "[server]: failed to remove dir \"%s\": %s\r\n", argument, error_msg);
						strcpy(replyMsg, "550 ");
						strcat(replyMsg, error_msg);
						strcat(replyMsg, "\r\n");

					}
				}

				/* this deletes files as well as directories */
				else if (strcmp(cmd, "rm") == 0) {
					printf("[server]: removing name \"%s\"\r\n", argument);
					cmd_status = remove(argument);

					if (cmd_status == 0) {
						printf("[server]: successfully removed \"%s\"\r\n", argument);
						strcpy(replyMsg, "250 file successfully removed\r\n");
					}

					else {
						error_msg = strerror(errno);
						fprintf(stderr, "[server]: couldn't remove \"%s\": %s\r\n", argument, error_msg);
						strcpy(replyMsg, "550 ");
						strcat(replyMsg, error_msg);
						strcat(replyMsg, "\r\n");
					}
				}

				/*
					Server receives "recv" command, the client wants to receive a file.

					The server now replies with the status of the file. Either it exists, or it doesnt.
					If the file doesn't exist, then we simply respond with
				*/
				else if (strcmp(cmd, "recv") == 0) {

					/* server needs to connect to client real quick */
					// int data_status = clntConnect(SERVER_HOSTNAME, &data_socket);
					/*
					if (status != 0) {
						fprintf(stderr, "[server]: received error code when connecting: %d\r\n", data_status);
						strcpy(replyMsg, "425 Can't open data connection\r\n");
					}

					else {

					}
					*/

					/*
					FILE *file = NULL;

					file = fopen(argument, "r");
					if (file != NULL) {

					}

					cmd_status = fclose(file);
					if(cmd_status == EOF) {
						fprintf(stderr, "[server] couldn't close file \"%s\"\r\n", argument);
					}
					*/
				}
												/* At this point our command isn't implemented */
				else {
					printf("[server]: command '[%s]' not implemented\r\n", cmd);
					strcpy(replyMsg, "202 Command not implemented\r\n");
				}

				/*
					currently ls will break because
					directories overflow the input buffer
					and the client doesn't know that there's more data to listen for
				*/

				/* only prints the outputs of the current directory
				else if (strcmp(cmd, "ls") == 0) {
					/*
					DIR *working_dir_stream;
					struct dirent *myfile;
					// struct stat mystat;  // for filesize

					// iterate through the directory stream & append all filenames to replymessage
					working_dir_stream = opendir(current_dir);

					// make sure the stream is open before trying to read from it
					if (working_dir_stream != NULL) {
						strcat(replyMsg, "200 Command OK\r\n");

						/* keep track of how much has been written so we don't
							write to space we're not allowed to
						size_t written_length = strlen(replyMsg);
						while((myfile = readdir(working_dir_stream)) != NULL &&
							(strlen(replyMsg) + strlen(myfile->d_name) + 2 < BUFFER_LENGTH) )
						{
							/* make sure we have enough space in the buffer
							if (written_length + strlen(myfile->d_name) + 2 < BUFFER_LENGTH) {
								strcat(replyMsg, myfile->d_name);
								strcat(replyMsg, "\r\n");
								written_length = strlen(replyMsg);
								printf("[%d]: %s\r\n", written_length, myfile->d_name);
							} else {
								printf("[server]: No more length in list buffer; flushing directory stream\r\n");
								closedir(working_dir_stream);
							}
						}
						/* close the stream
						closedir(working_dir_stream);
					}
					else {
						printf("[server]: could not open directory \"%s\"\r\n", current_dir);
						strcpy(replyMsg, ERR_INTERNAL_ERROR);

					}

				}*/

			}

			else {
				if (strlen(replyMsg) == 0 || strcmp(replyMsg, "") == 0) { 
					/* the command isn't recognized */
					strcpy(replyMsg, ERR_COMMAND_NOT_FOUND);
				}
			}
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
				should_quit = true;
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
char * help(char *reply_str) {
	char *response;
	string_init(&response);
	string_set(&response, OK_HELP);

	for (size_t i = 0; i < NUM_CMDS; i++) {
		dyn_concat(&response, CMD_LIST[i]);
		dyn_concat(&response, "\r\n");
	}
	strcpy(reply_str, response);
	string_free(&response);
	return reply_str;
}


char * get_cwd(char *reply, char *cd_buffer) {
	/* to check if the getcwd call worked */
	char *pstr = NULL;

	if ((pstr = getcwd(cd_buffer, PATH_MAX)) == NULL) {
		printf("[server]: couldn't write the working directory to buf\r\n");
		strcpy(reply, ERR_INTERNAL_ERROR);
	} else {
		char *temp = NULL; /* dynamic string to be writing to */
		string_init(&temp);
		string_set(&temp, "257 \"");

		/* copy the contents from this segment into the dynamic string */
		dyn_concat(&temp, cd_buffer);
		dyn_concat(&temp, "\" is your current location\r\n");

		/* copy formatted string into the reply */
		strcpy(reply, temp);

		/* release the working data */
		string_free(&temp);
	}
	return reply;
}


