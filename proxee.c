// proxee - a lightweight web proxy by KST software, 2005!
// ---------------------------------------------------------------------------------------------

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <sys/stat.h>
#include <syslog.h>
#include <string.h>



// DEFINES
// ---------------------------------------------------------------------------------------------

#define BUF_SIZE     1024

#define FAIL 0
#define PASS 1



// FUNCTION PROTOTYPES
// ---------------------------------------------------------------------------------------------

int proxee_HTTP (char *request, int sockfd);                 // handle an HTTP GET request
int resolve_host (char *hostname, char *ip_address);         // puts the ip address of the *hostname into *ip_address

int daemon_init();                   // daemon initialization
void daemon_run (int port);
void daemon_cleanup();               // cleanup on daemon finish

void sigterm_handler (void);         // deals with SIGTERM


             
// MAIN PROGRAM
// ---------------------------------------------------------------------------------------------

int main (int argc, char *argv[]) {
  syslog (LOG_NOTICE, "(pid %d)", getpid ());              // whack the process id into the syslog just for info
  
  (void) signal (SIGTERM, (void *)sigterm_handler);        // chain term signal to cleanup function
 
  // initialise the daemon
  
  if (daemon_init() != 0) {
    printf ("initialisation failed, exiting");
    exit (1);                                              // quit if init fails
  }
  
  syslog(LOG_NOTICE,"starting daemon");
  daemon_run (atoi ("9876"));                              // start daemon loop!
  
  daemon_cleanup ();

  return 0;
} 



// FUNCTION DEFINITIONS
// ---------------------------------------------------------------------------------------------


// proxee_HTTP ()
// send a request to the remote host, and send the response back to the client that requested (sockfd)

int proxee_HTTP (char *request, int sockfd) {
  struct sockaddr_in address;
  struct sockaddr_in la;
  int len, webserver_sockfd, bytes_read, result;
  char hostname[BUF_SIZE], ip_address[16], *first_slash, *second_slash;
  char input_buf[BUF_SIZE];

  syslog(LOG_NOTICE,"proxee_HTTP running");
  //syslog(LOG_NOTICE, request);                        // print the request into the syslog


  // resolve host

  strcpy (hostname, strchr (request, '/')+1);
  strcpy (hostname, strtok (hostname, "/"));


  if (resolve_host (hostname, ip_address) == -1) {
    syslog(LOG_NOTICE,"could not resolve host %s", hostname);
    return -1;
  }


  // grab a socket for the web server

  syslog(LOG_NOTICE,"Connecting to host %s, ip address %s, port 80", hostname, ip_address);

  webserver_sockfd = socket(AF_INET, SOCK_STREAM, 0);
  
  if (webserver_sockfd < 1) {
    syslog(LOG_NOTICE, "error binding to socket");
    return -2;
  }


  // connect to webserver on port 80
  
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = inet_addr(ip_address);
  address.sin_port = htons(80);
  len = sizeof(address);
  
  if (connect (webserver_sockfd, (struct sockaddr *)&address, len) < 0) {
    syslog(LOG_NOTICE,"error connecting to host %s, ip address %s, port 80", hostname, ip_address);
    return -3;
  }
  

  // send request to remote host

  if ( (send (webserver_sockfd, request, BUF_SIZE, 0)) == -1) {
    syslog(LOG_NOTICE,"error sending to host %s, ip address %s, port 80", hostname, ip_address);
    return -4;
  }

  
  // loop
  //   receive data from remote host
  //   forward received data to requesting client (sock_fd)

  do { 
    bytes_read = recv (webserver_sockfd, input_buf, BUF_SIZE, 0);
    
    if (bytes_read >= 0) {
      result = send (sockfd, input_buf, bytes_read, 0);
      //syslog(LOG_NOTICE,"bytes_read = %d, result = %d", bytes_read, result); 
    }
  } while (result > 0); //bytes_read == BUF_SIZE);
  
 
  syslog(LOG_NOTICE,"closing connection to host %s, ip address %s", hostname, ip_address);


  // close the connection to the webserver

  close (webserver_sockfd);


  return 0;    // success
}


// resolve_host ()
// returns the ip address of the specified hostname

int resolve_host (char *hostname, char *ip_address) {
  struct hostent *hp;
  struct sockaddr_in from;
  //  char result[255];
  
  memset(&from, 0, sizeof(struct sockaddr_in));
  from.sin_family = AF_INET;
  hp = gethostbyname(hostname);
  
  if (hp == NULL) {
    strcpy(ip_address,"unknown");
    return -1;   // failed
  }
  else {
    memcpy(&from.sin_addr,hp->h_addr,hp->h_length);
    strcpy(ip_address,inet_ntoa(from.sin_addr));
  }
    
  return 0;    // success
}



// daemon_init ()
// forks off a child process, like a real daemon

int daemon_init (void) { 
  pid_t pid;                                                        // process id for child
  pid_t sid;                                                        // session id for child (else it just becomes an orphan over time)

  syslog (LOG_NOTICE, "initialising daemon");
  
  pid = fork();                                                     // fork off the child process.. or try to
  

  // check the pid to see if it was a success

  if (pid < 0) {
    syslog (LOG_ERR, "error forking off child process, exiting");
    return 1;
  }

  if (pid > 0) {
    syslog (LOG_NOTICE, "killing parent process (pid %d)", getpid ());
    exit (0);
  }
  

  // strange, maybe the parent process should do all these checks before forking
  // and not just letting the child process do it, but then, is this way cleaner
  // and more self contained?
  // 
  // this is now the CHILD process code.
  
  syslog (LOG_NOTICE, "child process forked (pid %d)", getpid()); 
   
  umask(0);                                                       // setup a umask of nothing. i.e 0777
  

  // Create a new SID for the child process, creates a new process group etc or something
  
  sid = setsid();
  
  if (sid < 0) {	
    syslog(LOG_ERR, "unable to get a session ID, exiting");
    return 1;
  }
  
  
  // now we change the current working directory to something.. sensible
  
  if ((chdir("/")) < 0) {
    syslog (LOG_ERR, "unable to change current working directory, exiting");
    return 1;
  }
  
  
  // Close out the standard file descriptors
  // they are a security risk and don't need to be open for a daemon
  
  close (STDIN_FILENO);
  close (STDOUT_FILENO);
  close (STDERR_FILENO);
  

  // ignore terminal based signals
  
  signal (SIGTTOU, SIG_IGN);
  signal (SIGTTIN, SIG_IGN);
  signal (SIGTSTP, SIG_IGN);
  

  // ignore nohup too? I heard it was good to, to prevent any 'grandchildren' wtf?
  
  signal(SIGHUP, SIG_IGN);

    
  // if we are here, init ran ok because nothing b0rked

  syslog (LOG_NOTICE, "initialisation complete");

  return 0;
}


// daemon_run ()
// runs the daemon, accepts connections on the specified port

void daemon_run (int port) {
  int server_sockfd, client_sockfd;
  int server_len, client_len;
  struct sockaddr_in server_address;
  struct sockaddr_in client_address;
  int result;
  fd_set readfds, testfds;
  unsigned char input_buf[BUF_SIZE];
  pid_t pid;


  // Grab a socket

  server_sockfd = socket (AF_INET, SOCK_STREAM, 0);

  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = htonl (INADDR_ANY);
  server_address.sin_port = htons (port);
  server_len = sizeof (server_address);


  // bind

  syslog(LOG_NOTICE, "binding to socket");

  if (bind (server_sockfd, (struct sockaddr *)&server_address, server_len) != 0) {
    syslog(LOG_NOTICE, "error binding to socket");
    daemon_cleanup ();
    exit (1);
  }
  

  // Create a connection queue and initialise  readfds  to handle input from   server_sockfd

  listen (server_sockfd, 5);

  syslog (LOG_NOTICE, "listening for connections on port %d", port);
  
  FD_ZERO (&readfds);
  FD_SET (server_sockfd, &readfds);
  
  
  // now wait for clients and requests

  while (1) { 
    int fd, nread;

    testfds = readfds;
    
    result = select (FD_SETSIZE, &testfds, (fd_set *)0, (fd_set *)0, (struct timeval *) 0);
    
    if (result < 1) {
      perror ("server!");
      exit (1);
    }


    // Once activity is detected, find which descriptor its on by using FD_ISSET
      
    for (fd = 0; fd < FD_SETSIZE; fd++) {
      if (FD_ISSET (fd, &testfds)) {
	/*
	  if the activity is on  server_sockfd  its a request for a new conenction
	  add the associated  client_sockfd  to the descriptor set
	*/
	      
	if (fd == server_sockfd) {
	  client_len = sizeof (client_address);
	  client_sockfd = accept (server_sockfd, (struct sockaddr *)&client_address, &client_len);
	  
	  FD_SET (client_sockfd, &readfds);
	  
	  syslog (LOG_NOTICE, "adding client on fd: %d", client_sockfd);
	}


	/*
	  if it isnt the server then it must be client activity
	  if closed is received then the client mustve gone away
	  close the connection to the client
	  remove client from descriptor set
	  
	  otherwise, serve the client..
	*/
	
	else {
	  ioctl (fd, FIONREAD, &nread);
	  
	  if (nread == 0) {
	    close (fd);
	    FD_CLR (fd, &readfds);
	    
	    syslog(LOG_NOTICE, "removing client on fd: %d", fd);
	  }
	  else { 
	    // serve client on fd..  give stuff -- do whatever here..!
	    
	    if ( (pid = fork ()) == 0 ) {
	      // we are the child

	      syslog (LOG_NOTICE, "child process forked (pid %d)", getpid());
	      syslog (LOG_NOTICE, "serving client on fd: %d", fd);
	      read (fd, input_buf, BUF_SIZE);

	      if (proxee_HTTP (input_buf, fd) == -1) {
		syslog(LOG_NOTICE, "error connecting to server");
		// send a sorry page to the requesting client;
	      }

	      syslog (LOG_NOTICE, "killing child process (pid %d)", getpid()); 
	      exit (0);    // kill child
	    }
  
	  }
	}
      }
    } 
  }
  
  return;
}


// daemon_cleanup ()
// tidy up and exit gracefully.

void daemon_cleanup (void) {
  syslog(LOG_NOTICE, "cleaning up and exiting daemon");
  fcloseall ();
  exit (0);
}


// signal handlers
// --------------------------------------------------------------------------------

void sigterm_handler (void) {  
  syslog (1,"SIGTERM caught - cleaning up and exiting.\n");
  (void) signal(SIGTERM, SIG_DFL);   // reset to default signal handler
  
  // run the daemons cleanup function
  daemon_cleanup();
  
  exit(0);
}
