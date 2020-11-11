/*
 * By: Oleg Silkin
 * Computer Networking - 477 - HW2 
 * FTP Server implementation
 * 
 * This is a single-threaded server implmentation which listens to new connections on 
 * Port 4200. 
 * 
 * The server will listen to as many connections as specified, though is currently 
 * set to work with a single connection before exiting. This is set with the MAX_CONNECTION_COUNT macro.
 * 
 * The commands provided to this server are listed as follows:  
 	- user	*requires login*
	- pass	*requires login*
	- pwd   *requires login*
	- cd	 *requires login*
    - ls 	*requires login*
    - rm    *requires login*
    - mkdir 	*requires login*
    - rmdir 	*requires login*
    - stat 
    - send  	*requires login*	
    - recv		*requires login*
    - quit 	
    - help 

 * 
 * The server uses a state transition system in order to ensure proper execution of the functions occurs
 * along with enforcing permission settings. 
 * 
 * Each connection will take one of three possible states: LOGGED_OUT, LOGGING_IN, and LOGGED_IN. 
 * When LOGGED_OUT, the commands available are limited to those which allow the user to fetch information 
 * about the system they are using, but without having any read access to the rest of the system. 
 * 
 * The LOGGING_IN state is entered once the user successfully runs the USER command in which the username 
 * provided by the user is validated and known to be an existing user within the system.
 * In this state, the user can either successfully provide a password to the corresponding user 
 * using the PASS command which will transition the session into a LOGGED_IN state. 
 * If the PASS command fails, the session will fallback to LOGGED_OUT. 
 * 
 * The LOGGED_IN state provides the user with access to the full suite of FTP commands. 
 * 
 * Commands that access the file system excluding send/recv/ls/stat are all implemented as 
 * calls to their corresponding functions from the POSIX standard `unistd.h` header, which 
 * allows the FTP server to have standardized error tracking using the errno.h header. 
 * 
 * The exception to this is the `ls` command which contains too much data to be transferred over the 
 * control connection and is therefore implemented as a file transfer similar to RECV. 
 * 
 * One implementation details is that `pwd` sends the current directory through 
 * the control connection, however the maximum length of a UNIX path is 4096 according to the POSIX standard
 * which exceeds the 1500B MTU on a TCP socket. 
 * 
 * If the user starts recursing deeper into the directories so that the path exceeds, it will 
 * cause the control connection buffer to overflow 
 *  
 * This exposes the system to having its users identified, however for the sake of this server 
 * security is not a priority otherwise commands would be sent through an encrypted state and 
 * the 600 series of FTP commands would be utilized. 
 * 
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

#include <sys/stat.h> /* for using the stat() command to obtain statistics on a file */

#include <netdb.h>

#include <limits.h> /* for PATH_MAX */

#include <string.h>

#include <stdlib.h> /* for malloc/calloc/free */

#include <stdio.h> /* for printf()/scanf() IO functions */

#include <unistd.h> /* for close(), getcwd(),  */

#include <stdbool.h> /* includes macros for boolean datatypes and values instead of relying on int */

#include <dirent.h> /* for using directory functions */

#include <errno.h> /* for error handling */

/* stores the error status from system functions, to be used for the code return logic  */
extern int errno;

#define SERVER_FTP_PORT 4200
#define DATA_CONNECTION_FTP_PORT 4201

/* Error and OK codes */
#define OK 0
#define ER_INVALID_HOST_NAME - 1
#define ER_CREATE_SOCKET_FAILED - 2
#define ER_BIND_FAILED - 3
#define ER_CONNECT_FAILED - 4
#define ER_SEND_FAILED - 5
#define ER_RECEIVE_FAILED - 6
#define ER_ACCEPT_FAILED - 7


/******************* FTP response codes***********/
/* 
 * these are defined at the top as macros and then copied into replyMsg when the 
 *	server is making a response 	
 */


/* used to indicate the file status over the data connection */
#define OK_OPENING_DATA_SOCK "150 File status okay; about to open data connection.\r\n"

#define OK_SERVER_SUCC "200 The requested action has been successfully completed.\r\n"
#define OK_HELP "214 List of server commands is as follows:\r\n"
#define OK_CLOSING_CONTROL_CONN "221 Service closing control connection.\r\n"
#define OK_LOGIN_SUCC "230 User logged in\r\n"
#define OK_FILE_ACTION_COMPLETE "250 Requested file action okay, completed.\r\n"
#define OK_CHANGE_DIRECTORY "257 changed directory to '%s'\r\n"
#define OK_PRINT_WORKING_DIRECTORY "257 '%s' is your current directory\r\n"
#define OK_TRANSFER_SUCC_CLOSE_DATA "226 Closing data connection. Requested file action successful.\r\n"

#define OK_USERNAME_NEED_PWORD "331 password required for %s\r\n"

#define ERR_CANT_OPEN_CONNECTION "425 Can't open data connection.\r\n"
#define ERR_TRANSFER_ABORTED "426 Connection closed; transfer aborted\r\n"
#define ERR_INTERNAL_ERROR "451 Requested action aborted. Local error in processing.\r\n"
#define ERR_INVALID_LOGIN "430 Invalid username or password\r\n"

#define ERR_COMMAND_NOT_FOUND "500 Syntax error, command unrecognized and the requested action did not take place. This may include errors such as command line too long.\r\n"
#define ERR_PARAM_SYNTAX_ERR "501 Syntax error in parameters or arguments.\r\n"
#define ERR_MISSING_ARGUMENT "501 No argument provided for command.\r\n"
#define ERR_NOT_LOGGED_IN "530 Not Logged In.\r\n"
#define ERR_FILE_UNAVAILABLE "550 Requested action not taken. File unavailable (e.g., file not found, no access).\r\n"

#define MAX_CONNECTION_COUNT 1

/* 
	Login state is treated as a State transition system, the three possible 
	states are either logged out, logging in, or logged in. 
*/

/* only allows help, quit, stat, user, and pass */
#define SESH_LOGGED_OUT 0 

/* This state is like LOGGED_OUT except the state will either 
	move to LOGGED_IN if the PASS command is successful, 
	otherwise it will move back to LOGGED_OUT. 

	Using any of the commands besides PASS will cause this state to transition back to LOGGED_OUT
*/
#define SESH_LOGGING_IN 1	

/* In this state, the user has full access to the commands provided by the FTP server 
	and is required in order to establish the data connection
*/ 
#define SESH_LOGGED_IN 2



/* Set the hardcoded USERNAME_LENGTH macro depending on the operating system being used
	e.g. Linux systems can tolerate a username of 32 Bytes in length
		 whereas Solaris systems only allow 8-Byte long usernames
*/ 
#if defined(__linux__) || defined(__gnu_linux__)
#define USERNAME_LENGTH 32 /* Linux max username length set to 32 */
#elif defined(sun) || defined(__sun)
#if defined(__SunOS_5_11)
#define USERNAME_LENGTH 12 /* SunOS 5.11 can handle usernames 12 chars in len, however an override must be made */
#else
#define USERNAME_LENGTH 8 /* standard SunOS has a maximum of 8 technically */
#endif
#elif defined(_WIN32) || defined(_WIN16) || defined(_WIN64) || defined(__WINDOWS__) || defined(__TOS_WIN__) || defined(__WIN32__)
#define USERNAME_LENGTH 20
#elif(defined(__APPLE__) && defined(__MACH__)) || defined(macintosh)
#define USERNAME_LENGTH 20
#elif defined(__unix__) || defined(__unix)
#define USERNAME_LENGTH 8 /* generic unix definition */
#endif

/* ON a UNIX system, the max length of a path is 4096 characters */
#define WORKINGPATH_MAX_LENGTH 4096
#define BUFFER_LENGTH 4096 /* this needs to be reduced back to 1024 and pwd be sent over the data connection */

/* Define a macro for the hostname */
#define SERVER_HOSTNAME "localhost" /* set to localhost when working from a local system */


/* list of the implemented commands,  
	server compares `cmd` variable against this list to validate the command 
*/ 
const char * CMD_LIST[] = {
    "user", 
    "pass",
    "pwd", 
    "cd", 
    "ls",
    "rm",
    "mkdir",
    "rmdir",
    "stat",
    "send",
    "recv",
    "quit", 
    "help", 
};
#define NUM_CMDS 13

/* These are the commands that may be used without a user being logged in */
const char * GUEST_CMDS[] = {
    "user",
    "pass",
    "stat",
    "help",
    "quit"
};
#define NUM_GUEST_CMDS 5

/* These are the commands that require a non-NULL argument */
const char * REQUIRE_ARGS[] = {
    "user",
    "pass",
    "cd",
    "mkdir",
    "rmdir",
    "send",
    "recv"
};
#define NUM_REQUIRE_ARGS 7

/* set the FTP server users here */
const char * USERNAMES[] = {
    "oleg",
    "guest",
    "ken",
    "doug",
    "jared"
};

/* password lists with corresponding indices */
const char * PASSWORDS[] = {
    "password",
    "guest",
    "FactsAndLogic5",
    "dragons",
    "bballer23",
};
#define NUM_USERS 5

/* Function prototypes */
int svcInitServer(int * s);
int sendMessage(int s, char * msg, int msgSize);
int receiveMessage(int s, char * buffer, int bufferSize, int * msgSize);
int clntConnect(char * serverName, int * );


/* List of all global variables */
char userCmd[BUFFER_LENGTH]; /* user typed ftp command line received from client */
char cmd[BUFFER_LENGTH]; /* ftp command (without argument) extracted from userCmd */
char argument[BUFFER_LENGTH]; /* argument (without ftp command) extracted from userCmd */
char replyMsg[BUFFER_LENGTH]; /* buffer to send reply message to client */

/* stores the working directory of the calling process so it can be reinitialized between connections */ 
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

int main(const int argc,
    const char ** argv) {
    /* List of local varibale */

    int msgSize; /* Size of msg received in octets (bytes) */
    int listenSocket; /* listening server ftp socket for client connect request */
    int ccSocket; /* Control connection socket - to be used in all client communication */
    int data_socket; /* data connection socket */
    int status;
    char buffer[100]; /* data buffer used for reading/writing file over a TCP connection */
    int connection_no = 0; /* for exiting after a set amount of connections */

    /* login settings */
    //bool require_login = false;

    /* stores the working directory so the process can reset after every call */
    if (getcwd(working_dir, PATH_MAX) == NULL) {
        perror("[server]: couldn't write directory to working_dir\r\n");
		exit(errno);
    }
	printf("[server]: working_dir: %s\r\n", working_dir);

    /*
     * NOTE: without \r\r\n at the end of format string in printf,
     * UNIX will buffer (not flush)
     * output to display and you will not see it on monitor.
     */
    printf("[server]: Started execution of server ftp\r\n");

    /* Initialize the server and store the socket in listenSocket */
	printf("[server]: Initializing server on TCP Socket\r\n");
    status = svcInitServer( & listenSocket);
    if (status != 0) {
        printf("[server]: Exiting server ftp due to svcInitServer returned error\r\n");
        exit(status);
    }

    printf("[server]: FTP Server is listening for connections on socket %d\r\n", listenSocket);

    /* wait until connection request comes from client ftp */
    while (connection_no < MAX_CONNECTION_COUNT) {

        /* track whether or not we should be logging out */
        bool should_quit = false;

        /* login information, required for establishing data connection if --require-login flag is set */
        int logged_in = SESH_LOGGED_OUT; /* stores the login state of the user session */
        char username[USERNAME_LENGTH]; /* pointer to the username */

        /* used for directory switching */
        char current_dir[PATH_MAX];

        /* reset the working directory to where the program runs */
        if (chdir(working_dir) != 0) {
            printf("[server]: unable to reset current_dir to %s\r\n", working_dir);
            printf("[server]: current_dir: %s\r\n", current_dir);
        }

        /* reset the current_dir variable */
        strcpy(current_dir, working_dir);
        printf(" -> %s\r\n", current_dir);

        ccSocket = accept(listenSocket, NULL, NULL);

        printf("[server]: Came out of accept() function \r\n");

        if (ccSocket < 0) {
            perror("[server]: cannot accept connection:");
            printf("[server]: Server ftp is terminating after closing listen socket.\r\n");
            close(listenSocket); /* close listen socket */
            return (ER_ACCEPT_FAILED); // error exist
        }

        printf("[server] Connected to client, sending a welcome message \r\n");

        /* attempt to send the welcome message, if it breaks then close the connection
         	and continue onto the next iteration of the loop */
        sprintf(replyMsg, "220\r\n~~Welcome to Oleg's FTP Server! Drinks are on the house!~~\r\n");
        if (send(ccSocket, replyMsg, strlen(replyMsg) + 1, 0) < 0) {
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

            char temp[BUFFER_LENGTH]; /* the user command will be copied into this buffer so it can be parsed without breaking the original */
            char * ptr = NULL; /* pointer to be used when processing strtok */
            char * arg = NULL; /* pointer to process & validate the argument */

            /* For validating the user's command */
            bool is_valid_cmd = false;
            bool requires_args = false;
            bool valid_perms = false;


            /* Receive client ftp commands until */
            status = receiveMessage(ccSocket, userCmd, sizeof(userCmd), & msgSize);
            if (status < 0) {
                printf("[server] Failed to receive the message. Closing control connection \r\n");
                printf("[server] Server ftp is terminating.\r\n");
                break;
            }


            /*
             *	Parse the input received from the client
             */
            strcpy(temp, userCmd); /* Store a copy of the passed commadn into the temp array */
            strcpy(replyMsg, ""); /* clear out the reply message */
            
			/* extract cmd and argument into their respective buffers */
			ptr = strtok(temp, " ");
            if (ptr != NULL) {
                strcpy(cmd, ptr);
				if ((arg = strtok(NULL, "\0")) != NULL) 
                	strcpy(argument, arg);
            	
            } else {
                printf("[server] No command received!\r\n");
                continue;
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
                    if (logged_in == SESH_LOGGED_IN)
                        valid_perms = true;

                    /*
                    	make sure that the user can't access modifying functions
                       if they aren't logged in
                    */
                    else {
                        /* the state is either LOGGED_OUT or LOGGING_IN so check to make sure that
                        	the command entered is one of the guest commands
                        */

                        for (size_t k = 0; k < NUM_GUEST_CMDS; k++) {

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

                    if (!requires_args || (requires_args && arg != NULL)) {
                        is_valid_cmd = true;
                    } else {
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
                const char * error_msg = NULL; /* stores the error pointer from strerror(errno) */

				/* 
				 * sets the SESH_LOGGING_IN state and stores the username that the client passed in. 
				 * The username isn't actually checked here, that happens during the PASS phase. 
				 * 
				 * Oleg Silkin implemented this command
				 */
                if (strcmp(cmd, "user") == 0) {
                    /* set the login state */
                    logged_in = SESH_LOGGING_IN;
                    strcpy(username, argument);
                    sprintf(replyMsg, "331 Please specify the password for '%s'\r\n", username);
                }

                /* 
				 * PASS accepts a password as an argument, and will only work during the 
				 * SESH_LOGGING_IN state. 
				 * 
				 * The username passed in from earlier is checked against the list of existing 
				 * usernames, if a match is found it gets checked against the password at the corresponding index. 
				 * 
				 * If a match exists, then the SESH_LOGGED_IN state is set, otherwise the 
				 * state falls back to SESH_LOGGED_OUT
				 * 
 				 * Oleg Silkin implemented this command

				 */
                else if (strcmp(cmd, "pass") == 0) {
                    if (logged_in == SESH_LOGGING_IN) {
                        char * password = argument;
                        /* we need to iterate and find the password to the corresponding
                        	username to compare it against what the user passed in */
                        for (size_t k = 0; k < NUM_USERS; k++) {
                            if (strcmp(username, USERNAMES[k]) == 0) {
                                if (strcmp(PASSWORDS[k], password) == 0) {
                                    logged_in = SESH_LOGGED_IN;
                                    break;
                                }
                            }
                        }
                        /* check if the login was successful */
                        if (logged_in == SESH_LOGGED_IN) 
                            strcpy(replyMsg, OK_LOGIN_SUCC);

                        /* set the failstate */
                        else {
                            printf("[server]: unsucessful login, check username or password\r\n");
                            strcpy(replyMsg, ERR_INVALID_LOGIN);
                            logged_in = SESH_LOGGED_OUT;
                        }
                    }

                    /* incorrect session state to be calling PASS */
                    else {
                        printf("[server]: called pass before user\r\n");
                        strcpy(replyMsg, "503 Called user before pass\r\n");
                    }
                }

                /*
				 * Returns a list of the supported commands and their arguments
				 * 
				 * Oleg Silkin implemented this command
				 * 
				*/
                else if (strcmp(cmd, "help") == 0) {
                    // valgrind should scrutinze this
					strcpy(replyMsg, "214 Command List:\r\n"
						"user <username>\r\n"
						"pass <password>\r\n"
						"mkdir <directory>\r\n"
						"rmdir <directory>\r\n"
						"cd <directory>\r\n"
						"rm <filename>\r\n"
						"ls\r\n"
						"pwd\r\n"
						"send <filepath>\r\n"
						"recv <filepath>\r\n"
						"stat\r\n"
						"quit\r\n"
						"help\r\n"
					);
                } 
				
				/**
				 * Closes the connection between the client and the server
				 * the buffers are flushed and the connection state is reset 
				 * 
				 * A quit flag is set so that we can perform necessary function calls later
				 * 
				 * Oleg Silkin implemented this command
				 */
				else if (strcmp(cmd, "quit") == 0) {
                    /* logout here */
                    if (logged_in) {
                        fflush(stdout);
                        strcpy(username, "");
                        logged_in = SESH_LOGGED_OUT;
                    }
                    strcpy(replyMsg, OK_CLOSING_CONTROL_CONN);
                    should_quit = true;
                } 

				/* Returns returns the connection status
				
				  Oleg Silkin implemented this command
				 */
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

			   /**
			    * Returns the working directory of the current process. 
				* If the working directory is over 1500 bytes in length then 
				* part of the send command will not be sent and result in a buffer overflow. 
			    * 
				* Oleg Silkin implemented this command

			    */
                else if (strcmp(cmd, "pwd") == 0) {
					char * pstr = NULL;
					if ((pstr = getcwd(current_dir, PATH_MAX)) == NULL) {
						printf("[server]: couldn't write the working directory to buf\r\n");
						strcpy(replyMsg, ERR_INTERNAL_ERROR);
					} 
					else {
						sprintf(replyMsg, OK_PRINT_WORKING_DIRECTORY, current_dir);
					}
                } 
				

				/**
				 * Changes the working directory of the current process 
				 * to whatever is specified in `argument`. 
				 * 
				 * If the argument is invalid, the function errors out and copies the error 
				 * message into the replyMsg
				 * 
				 * Oleg Silkin implemented this command
				 * 
				 */
				else if (strcmp(cmd, "cd") == 0) {
                    cmd_status = chdir(argument);

                    /* change directory was successful */
                    if (cmd_status == 0) {
                        getcwd(current_dir, PATH_MAX);
                        sprintf(replyMsg, OK_CHANGE_DIRECTORY, current_dir);
                        printf("[server]: client changed directory to '%s'\r\n", current_dir);
                    } else {
                        error_msg = strerror(errno);
                        strcpy(replyMsg, ERR_INTERNAL_ERROR);
                        printf("[server]: replymsg: %s\r\n", replyMsg);
                        fprintf(stderr, "[server]: cannot cd into %s: %s\r\n", argument, error_msg);
                    }
                } 
				
				/**
				 * Creates a new directory specified by `argument`. 
				 * 
				 * If any errors arise within the function call, they are stored 
				 * into the error_msg pointer and provided to the user 
				 * 
				 * By default, all directories are created with full read, write, and execute access to both the user and user group,
				 * however all others receive only read & execute access
				 * 
				 * Oleg Silkin implemented this command
				 */
				else if (strcmp(cmd, "mkdir") == 0) {
                    /* User + Group can read, write, and execute in directory, others may only read & execute*/
                    mode_t directory_mode = 0775;
                    cmd_status = mkdir(argument, directory_mode);
                    if (cmd_status == 0) {
                        strcpy(replyMsg, "212 Successfully created directory\r\n");
                    }
                    /* errno is set */
                    else if (cmd_status == -1) {
                        error_msg = strerror(errno);
                        fprintf(stderr, "[server]: could not create directory %s: %s\r\n", argument, error_msg);
                        strcpy(replyMsg, ERR_FILE_UNAVAILABLE);
                    }
                } 
				
				/**
				 * Removes the directory specified by argument
				 * 
				 * Error messages are retrieved using streeror and errno 
				 * and provided to the client. 
				 * 
				 * rmdir cannot delete directories that contain more than dot and two dots. 
				 * attempting to delete a non-empty directory will result in an error
				 * 
				 * rmdir cannot delete files either, which will result in an error 
				 * 
				 * There are other unix-related errors associated, such as attempting 
				 * to delete a hard-linked directory, having too long of a filename, etc. 
				 * these are all provided by strerror(errno)
				 * 
				 * 
				 * Oleg Silkin implemented this command
				 */
				else if (strcmp(cmd, "rmdir") == 0) {
                    cmd_status = rmdir(argument);
                    if (cmd_status == 0) {
                        strcpy(replyMsg, OK_FILE_ACTION_COMPLETE);
                    }

                    /* status is -1 and errno is set so we return system information */
                    else {
                        error_msg = strerror(errno);
                        fprintf(stderr, "[server]: failed to remove dir \"%s\": %s\r\n", argument, error_msg);
                        strcpy(replyMsg, ERR_FILE_UNAVAILABLE);

                    }
                }

                /**
                 * Removes any file or directory specified by argument. 
                 * 
				 * Note: this is a very powerful command which will also delete DIRECTORIES, so 
				 * this function needs to be treated carefully  
				 * 
				 * Oleg Silkin implemented this command
                 */
                else if (strcmp(cmd, "rm") == 0) {
                    cmd_status = remove(argument);
                    if (cmd_status == 0) {
                        strcpy(replyMsg, OK_FILE_ACTION_COMPLETE);
                    } else {
                        error_msg = strerror(errno);
                        fprintf(stderr, "[server]: couldn't remove \"%s\": %s\r\n", argument, error_msg);
                        strcpy(replyMsg, ERR_FILE_UNAVAILABLE);
                    }
                }

			   /**
				*  Sends the server-sided file to the client specified within argument. 
				*	This function provides a follow-up call to the client to notify the status 
				* 	of the data connection, whether the file exists, etc.
				*
				*	 Regardless of execution state, a message MUST be sent to the control 
		 		*	connection so that the client doesn't hang
				*  
				* Oleg Silkin implemented this command

			    */
                else if (strcmp(cmd, "recv") == 0) {
                    /* establish the data connection */
                    status = clntConnect(SERVER_HOSTNAME, & data_socket);
                    if (status != OK) {
                        fprintf(stderr, "[server]: Unable to establish data connection\r\n");
                        strcpy(replyMsg, ERR_CANT_OPEN_CONNECTION);
                    } else {
                        /* attempt to open the filestream to read from */
                        FILE * fd = NULL; /* for accessing the file descriptor */
                        fd = fopen(argument, "r");

                        if (fd != NULL) {
                            /* inform the client that the file is open and we can proceed */
                            strcpy(replyMsg, OK_OPENING_DATA_SOCK);
                            status = sendMessage(ccSocket, replyMsg, strlen(replyMsg) + 1);
                            int bytesread = 0;
                            /* read from the file and write to the data connection 
                            	until there's nothing left to write 
                             */
                            while (!feof(fd)) {
                                bytesread = fread(buffer, sizeof(char), sizeof(buffer), fd);
                                sendMessage(data_socket, buffer, bytesread);
                            }
                            strcpy(replyMsg, OK_TRANSFER_SUCC_CLOSE_DATA);
                            fclose(fd);
                        }
                        /* we have the data connection open but we were unable to open the file */
                        else {
                            fprintf(stderr, "[server]: couldn't open file %s\r\n", argument);
                            strcpy(replyMsg, ERR_FILE_UNAVAILABLE);
                            status = sendMessage(ccSocket, replyMsg, strlen(replyMsg) + 1);
                        }
                        close(data_socket);
                        strcpy(replyMsg, OK_FILE_ACTION_COMPLETE);
                    }
                }

                /**
                 * Send command receives a file from the client. 
				 * 
				 * Like recv, this sends a message based on whether or not 
				 * the server is able to receive the file itself. 
				 * 
				 * The argument passed will be a path from the client's machine
				 * and the file is located at the end. 
				 * 
				 * If the argument is a path containing directories, the filename
				 * will be separated and used to create a copy under a matching filename
				 * in the same directory as the server's working directory 
				 * 
				 * Oleg Silkin implemented this command
                 */
                else if (strcmp(cmd, "send") == 0) {

                    /* establish the data connection */
                    status = clntConnect(SERVER_HOSTNAME, & data_socket);
                    if (status != OK) {
                        fprintf(stderr, "[server]: Unable to establish data connection\r\n");
                        strcpy(replyMsg, ERR_CANT_OPEN_CONNECTION);
                        sendMessage(ccSocket, replyMsg, strlen(replyMsg) + 1);
                    } else {
                        /* extract filename from the user's path */
                        char * fname = NULL;
                        if (strrchr(argument, '/'))
                            fname = strrchr(argument, '/') + 1;
                        else
                            fname = argument;
                        /* attempt to open the filestream to read from */
                        FILE * fd = NULL; /* for accessing the file descriptor */
                        fd = fopen(fname, "w");

                        if (fd != NULL) {
                            /* inform the client that the file is open and we can proceed */
                            strcpy(replyMsg, OK_OPENING_DATA_SOCK);
                            status = sendMessage(ccSocket, replyMsg, strlen(replyMsg) + 1);
                            do {
                                status = receiveMessage(data_socket, buffer, sizeof(buffer), & msgSize);
                                fwrite(buffer, sizeof(char), msgSize, fd);
                            } while ((msgSize > 0) && (status == 0));
                            fclose(fd);
                            strcpy(replyMsg, OK_TRANSFER_SUCC_CLOSE_DATA);
                        }
                        /* we have the data connection open but we were unable to open the file */
                        else {
                            fprintf(stderr, "[server]: couldn't open file %s\r\n", fname);
                            strcpy(replyMsg, ERR_FILE_UNAVAILABLE);
                            status = sendMessage(ccSocket, replyMsg, strlen(replyMsg) + 1);
                        }
                        close(data_socket);
                        strcpy(replyMsg, OK_FILE_ACTION_COMPLETE);
                    }
                } 
				
				/**
				 * List command which provides a list of the current directory's contents 
				 * and sends them through the client's data connection. 
				 * 
				 * Since this uses the recv implementation, the user expects to receive 
				 * a message from the FTP server and therefore this happens in every branch
				 * 
				 * Oleg Silkin implemented this command
				 * 
				 */
				else if (strcmp(cmd, "ls") == 0) {

                    /* establish the data connection */
                    status = clntConnect(SERVER_HOSTNAME, & data_socket);
                    if (status != OK) {
                        fprintf(stderr, "[server]: Unable to establish data connection\r\n");
                        strcpy(replyMsg, ERR_CANT_OPEN_CONNECTION);
						sendMessage(ccSocket, replyMsg, strlen(replyMsg) + 1);
                    }

                    /* We can communicate with the client */
                    else {
                        /* read the contents of the local directory and write them into a file */
                        cmd_status = system("ls -al > /tmp/lsoutput.txt");
                        if (status == 0) {
                            /* attempt to open the filestream to read from */
                            FILE * fd = NULL; /* for accessing the file descriptor */
                            fd = fopen("/tmp/lsoutput.txt", "r");
                            if (fd != NULL) {
                                /* inform the client that the file is open and we can proceed */
                                strcpy(replyMsg, OK_OPENING_DATA_SOCK);
                                status = sendMessage(ccSocket, replyMsg, strlen(replyMsg) + 1);
                                /* read from the file and write to the data connection 
                                	until there's nothing left to write 
                                */
                                int bytesread = 0;
                                while (!feof(fd)) {
                                    bytesread = fread(buffer, sizeof(char), sizeof(buffer), fd);
                                    sendMessage(data_socket, buffer, bytesread);
                                }
                                strcpy(replyMsg, OK_TRANSFER_SUCC_CLOSE_DATA);
                                fclose(fd);
                            }
                        } else {
                            strcpy(replyMsg, ERR_INTERNAL_ERROR);
                            perror(strerror(errno));
                        }
                        remove("/tmp/lsoutput.tmp");
                        close(data_socket);
                        strcpy(replyMsg, OK_FILE_ACTION_COMPLETE);
                    }
                }

                /* At this point the client entered a command that has not been implemented
				 */
                else {
                    fprintf(stderr, "[server]: command '[%s]' not implemented\r\n", cmd);
                    strcpy(replyMsg, "202 Command not implemented\r\n");
                }
            } 
			
			/* the client provided a problematic input */
			else {
                if (strlen(replyMsg) == 0 || strcmp(replyMsg, "") == 0) {
                    /* the command isn't recognized */
					fprintf(stderr, "[server]: client provided an empty command or the replyMsg is empty\r\n");
                    strcpy(replyMsg, ERR_COMMAND_NOT_FOUND);
                }
            }
			/* Added 1 to include NULL character in */
            /* the reply string strlen does not count NULL character */
            /* clear out the reply string for the next run */
            status = sendMessage(ccSocket, replyMsg, strlen(replyMsg) + 1);
            if (status < 0) {
                printf("[server] received negative return status when sending a message: %d\r\n", status);
                should_quit = true;
            }

        }
        while (!should_quit);

        printf("[server] Closing control connection socket.\r\n");
        close(ccSocket); /* Close client control connection socket */

        // increment the connection number for now
        connection_no++;
    }
    printf("[server] Stopping listen on socket %d... ", listenSocket);
    fflush(stdout);
    close(listenSocket); /*close listen socket */
    printf("[server] done\r\n");

	/* print any error into the stderr stream */
    if (status != 0)
        fprintf(stderr, "[server] Server exited with non-zero status code: %d\r\n", status);

    return (status);
}

int clntConnect(
    char * serverName, /* server IP address in dot notation (input) */
    int * s /* control connection socket number (output) */
) {
    int sock; /* local variable to keep socket number */
    int option = 1; /* socket option to reuse the address */
    struct sockaddr_in clientAddress; /* local client IP address */
    struct sockaddr_in serverAddress; /* server IP address */
    struct hostent * serverIPstructure; /* host entry having server IP address in binary */

    /* Get IP address os server in binary from server name (IP in dot natation) */
    if ((serverIPstructure = gethostbyname(serverName)) == NULL) {
        printf("%s is unknown server. \n", serverName);
        return (ER_INVALID_HOST_NAME); /* error return */
    }

    /* Create control connection socket */
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("cannot create socket ");
        return (ER_CREATE_SOCKET_FAILED); /* error return */
    }

    /* set the address to be reused so there are no timeout errors */
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, & option, sizeof(option));

    /* initialize client address structure memory to zero */
    memset((char * ) & clientAddress, 0, sizeof(clientAddress));

    /* Set local client IP address, and port in the address structure */
    clientAddress.sin_family = AF_INET; /* Internet protocol family */
    clientAddress.sin_addr.s_addr = htonl(INADDR_ANY); /* INADDR_ANY is 0, which means */
    /* let the system fill client IP address */
    clientAddress.sin_port = 0; /* With port set to 0, system will allocate a free port */
    /* from 1024 to (64K -1) */

    /* Associate the socket with local client IP address and port */
    if (bind(sock, (struct sockaddr * ) & clientAddress, sizeof(clientAddress)) < 0) {
        perror("cannot bind");
        close(sock);
        return (ER_BIND_FAILED); /* bind failed */
    }

    /* Initialize serverAddress memory to 0 */
    memset((char * ) & serverAddress, 0, sizeof(serverAddress));

    /* Set ftp server ftp address in serverAddress */
    serverAddress.sin_family = AF_INET;
    memcpy((char * ) & serverAddress.sin_addr, serverIPstructure -> h_addr,
        serverIPstructure -> h_length);
    serverAddress.sin_port = htons(DATA_CONNECTION_FTP_PORT);

    /* Connect to the server */
    if (connect(sock, (struct sockaddr * ) & serverAddress, sizeof(serverAddress)) < 0) {
        perror("Cannot connect to server ");
        close(sock); /* close the control connection socket */
        return (ER_CONNECT_FAILED); /* error return */
    }

    /* Store listen socket number to be returned in output parameter 's' */
    * s = sock;

    return (OK); /* successful return */
} // end of clntConnect() */

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

int svcInitServer(
    int * s /*Listen socket number returned from this function */
) {
    int sock;
    struct sockaddr_in svcAddr;
    int qlen;

    /*create a socket endpoint */
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("cannot create socket");
        return (ER_CREATE_SOCKET_FAILED);
    }

    /*initialize memory of svcAddr structure to zero. */
    memset((char * ) & svcAddr, 0, sizeof(svcAddr));

    /* initialize svcAddr to have server IP address and server listen port#. */
    printf("Initializing server on port %d\r\r\n", SERVER_FTP_PORT);
    svcAddr.sin_family = AF_INET;
    svcAddr.sin_addr.s_addr = htonl(INADDR_ANY); /* server IP address */
    svcAddr.sin_port = htons(SERVER_FTP_PORT); /* server listen port # */

    /* bind (associate) the listen socket number with server IP and port#.
     * bind is a socket interface function
     */
    if (bind(sock, (struct sockaddr * ) & svcAddr, sizeof(svcAddr)) < 0) {
        perror("cannot bind");
        close(sock);
        return (ER_BIND_FAILED); /* bind failed */
    }

    /*
     * Set listen queue length to 1 outstanding connection request.
     * This allows 1 outstanding connect request from client to wait
     * while processing current connection request, which takes time.
     * It prevents connection request to fail and client to think server is down
     * when in fact server is running and busy processing connection request.
     */
    qlen = 1;

    /*
     * Listen for connection request to come from client ftp.
     * This is a non-blocking socket interface function call,
     * meaning, server ftp execution does not block by the 'listen' funcgtion call.
     * Call returns right away so that server can do whatever it wants.
     * The TCP transport layer will continuously listen for request and
     * accept it on behalf of server ftp when the connection requests comes.
     */

    listen(sock, qlen); /* socket interface function call */

    /* Store listen socket number to be returned in output parameter 's' */
    * s = sock;

    return (OK); /*successful return */
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
    int s, /* socket to be used to send msg to client */
    char * msg, /* buffer having the message data */
    int msgSize /* size of the message/data in bytes */
) {
    int i;

    /* Print the message to be sent byte by byte as character */
    for (i = 0; i < msgSize; i++) {
        printf("%c", msg[i]);
    }
    printf("\r\r\n");

    if ((send(s, msg, msgSize, 0)) < 0) /* socket interface call to transmit */ {
        perror("unable to send ");
        return (ER_SEND_FAILED);
    }

    return (OK); /* successful send */
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

int receiveMessage(
    int s, /* socket */
    char * buffer, /* buffer to store received msg */
    int bufferSize, /* how large the buffer is in octet */
    int * msgSize /* size of the received msg in octet */
) {
    int i;

    * msgSize = recv(s, buffer, bufferSize, 0); /* socket interface call to receive msg */

    if ( * msgSize < 0) {
        perror("unable to receive");
        return (ER_RECEIVE_FAILED);
    }

    /* Print the received msg byte by byte */
    for (i = 0; i < * msgSize; i++) {
        printf("%c", buffer[i]);
    }
    printf("\r\r\n");

    return (OK);
}