#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

char root_directory[256];
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
int flag = 0;

int start_server(int *PORT_NUMBER)
{
      // structs to represent the server and client
      struct sockaddr_in server_addr,client_addr;
      char page_requested[100];
      int sock; // socket descriptor
      char full_path[256];
      int i = 0;
      char ch;
	  char content_header[256];
	                       // buffer to read data into
	  char request[1024];
	  char *reply = NULL;
      FILE* myFilePtr = NULL;
	  int bytes_received, fd, sin_size, bytes_sent;
	  int reply_size, ret, bytes_read, total_reply_size;
	  int total_errors = 0;
	  int total_successes = 0;
	  int total_bytes_sent = 0;

	  if ((pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) != 0) {
		perror("Failed to enable cancel state on server thread");
		exit(1); 
	  }

      // 1. socket: creates a socket descriptor that you later use to
	  // make other system calls
      if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("Socket");
		exit(1);
      }
      int temp;
      if (setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,&temp,sizeof(int)) == -1) {
		perror("Setsockopt");
		exit(1);
      }

      // configure the server
      server_addr.sin_port = htons(*PORT_NUMBER); // specify port number
      server_addr.sin_family = AF_INET;
      server_addr.sin_addr.s_addr = INADDR_ANY;
      bzero(&(server_addr.sin_zero),8);

      // 2. bind: use the socket and associate it with the port number
      if (bind(sock, (struct sockaddr *)&server_addr,
			   sizeof(struct sockaddr)) == -1) {
		perror("Unable to bind");
		exit(1);
      }

      // 3. listen: indicates that we want to listen to the port to which we
	  // bound; second arg is number of allowed connections
      if (listen(sock, 1) == -1) {
		perror("Listen");
		exit(1);
      }

      // once you get here, the server is set up and about to start listening
      printf("\nServer configured to listen on port %d\n", *PORT_NUMBER);
      fflush(stdout);

      // 4. accept: wait here until we get a connection on that port
      while(1){

		pthread_mutex_lock(&lock);
		if(flag == 1){
          pthread_mutex_unlock(&lock);
          break;
        }
        pthread_mutex_unlock(&lock);

		sin_size = sizeof(struct sockaddr_in);
		fd = accept(sock, (struct sockaddr *)&client_addr,
					(socklen_t *)&sin_size);
		printf("Server got a connection from (%s, %d)\n",
			   inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

		// 5. recv: read incoming message into buffer
		bytes_received = recv(fd,request, 1024, 0);
		// null-terminate the string
		request[bytes_received] = '\0';
		i = 4;

		while(request[i] != ' '){
			page_requested[i - 4] = request[i];
			i++;
		}
		page_requested[i - 4] = '\0';

		printf("%s\n", page_requested);
		bzero(full_path, 256);
		bzero(content_header, 256);
		strcpy(full_path, root_directory);
		strcat(full_path, page_requested);

		printf("%s\n", full_path);

		/* Return statistics if requested */
		if (!strcmp(page_requested, "/stats")) {
			asprintf(&reply,"HTTP/1.1 200 OK\n"
				   "Content-Type: text/html\n\n"
				   "Page requests handled successfully: %d\n"
				   "Page requests failed: %d\n"
				   "Total bytes sent: %d\n",
				   total_successes, total_errors, total_bytes_sent);
			total_reply_size = strlen(reply);
		} else {

			myFilePtr = fopen(full_path, "r");
			if(myFilePtr == NULL) {
				if (errno == ENOENT) { /* the requested page was nit found*/
					printf("File not found %s\n", full_path);
					asprintf(&reply, "HTTP/1.1 404 Not Found\r\n\r\nSorry, "
							"could not find %s\n\n", page_requested);
				} else { /*Error in opening file */
					perror("Internal Server error");
					asprintf(&reply,"HTTP/1.1 500 "
							"Internal Server Error\r\n\r\n"
							"Internal Server Error\n\n");
				}
				total_reply_size = strlen(reply);
				total_errors++;
			} else {
				/*Find the file size */
				ret = fseek(myFilePtr, 0 ,SEEK_END);
				if (ret == -1) {
					perror("Internal Server error");
					asprintf(&reply,"HTTP/1.1 500 "
							"Internal Server Error\r\n\r\n"
							"Internal Server Error\n\n");
					total_reply_size = strlen(reply);
					total_errors++;
					goto send_out;
				}

				reply_size = ftell(myFilePtr);
				if (reply_size == -1) {
					perror("Internal Server error");
					asprintf(&reply,"HTTP/1.1 500 "
							"Internal Server Error\r\n\r\n"
							"Internal Server Error\n\n");
					total_reply_size = strlen(reply);
					total_errors++;
					goto send_out;
				}
				rewind(myFilePtr);

				/* Create Image header if requested */
				if (!strcmp(&page_requested[strlen(page_requested) - 4]
							,".jpg")) {
					sprintf(content_header, "HTTP/1.1 200 OK\r\n"
							"Content-Length: %d\r\n"
							"Content-Type: image/jpg\r\n\r\n", reply_size);
				} else { /* Create text header by default */
					sprintf(content_header, "HTTP/1.1 200 OK\r\n"
							"Content-Length: %d\r\n"
							"Content-Type: text/html\r\n\r\n", reply_size);
				}

				total_reply_size = reply_size + strlen(content_header) + 1;

				reply = calloc(1 , total_reply_size);
				if (reply == NULL) {
					perror("Failed to allocate memory for reply");
					exit(1);
				}

				/*Add header to reply*/
				strcpy(reply, content_header);

				/*read file contents into reply after header*/
				bytes_read = fread(reply + strlen(content_header), 1,
					  reply_size, myFilePtr);
				if (bytes_read != reply_size ) {
					free(reply);
					perror("Internal Server error");
					asprintf(&reply,"HTTP/1.1 500 "
							"Internal Server Error\r\n\r\n"
							"Internal Server Error\n\n");

					total_reply_size = strlen(reply);
					total_errors++;
					goto send_out;
				}

				reply[total_reply_size - 1] = '\0';
				total_successes++;
			}
		}

 send_out:
		if (reply == NULL) {
			perror("Failed to allocate memory for reply");
			exit(1);
		}

		// 6. send: send the message over the socket
		// note that the second argument is a char*, and the third is the
		// number of chars
		bytes_sent = send(fd, reply, total_reply_size, 0);
		if (bytes_sent == -1) {
			perror("Failed to send reply");
			exit(1);
		}

		total_bytes_sent += bytes_sent;
		printf("Server sent message: %s\n", reply);

		// 7. close: close the socket connection
		close(fd);
		if (myFilePtr) {
			fclose(myFilePtr);
			myFilePtr = NULL;
		}
		if (reply) {
			free(reply);
			reply = NULL;
		}
    }
	  close(sock);
      printf("Server closed connection\n");
      return 0;
}

void* input(){
  char input[256];

  while(1){
    bzero(input, 256);
    printf("Enter Input.\n");
    scanf("%s", input);
    if (!strcmp(input, "q")) {
      pthread_mutex_lock(&lock);
      flag = 1;
      pthread_mutex_unlock(&lock);
      break;  //quit on "q"
    }
  }
  pthread_exit(NULL);
}

int main(int argc, char *argv[])
{
  int i = 0;
  void *r;
  int *PORT_NUMBER;
  int pn;
  pthread_t t1, t2;

  // check the number of arguments
  if (argc != 3)
    {
      printf("\nUsage: port_number root_directory\n");
      exit(0);
    }

  PORT_NUMBER = malloc(sizeof(int));
  if (PORT_NUMBER == NULL) {
	perror("Failed to allocate memory");
	exit(1);
  }

  *PORT_NUMBER =  atoi(argv[1]);
  strcpy(root_directory, argv[2]);
  pthread_mutex_init(&lock, NULL);
  r = (void*) &start_server;
  pthread_create(&t1, NULL, r, PORT_NUMBER);
  pthread_create(&t2, NULL, &input, NULL);
  pthread_join(t2, NULL);
  pthread_cancel(t1);
  pthread_join(t1, NULL);
  pthread_mutex_destroy(&lock);
}
