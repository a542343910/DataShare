// gcc -pthread -lmhash node.c -o node

#include <stdlib.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

typedef struct client_t {
	char privileges;
	int socket;
	struct sockaddr_in *address;
} client_t;



pthread_mutex_t threads_count_mutex = PTHREAD_MUTEX_INITIALIZER;

int threads_count;

void *client_connection( void *client_pointer ) {
	
	client_t *client = ( client_t * ) client_pointer;
	
	
	
	
	
	
	//~ shutdown( client->socket, SHUT_RDWR );
	close( client->socket );
	//~ free( client->address );
	//~ free( client );
	
	pthread_mutex_lock( &threads_count_mutex );
	threads_count--;
	pthread_mutex_unlock( &threads_count_mutex );
	
	return client_pointer;
};



int node_socket;

char node_shutdown = 0;

void signal_handler( int sig ) {
	node_shutdown = 1;
	if( node_socket ) {
		//~ shutdown(node_socket, SHUT_RDWR);
		close(node_socket);
	};
};

int main( int argc, char *argv[], char *env[] ) {
	
	if( SIG_ERR == signal(SIGINT, signal_handler) ) {
		perror("Failed to bind handler for SIGINT");
		exit(1);
	};
	
	node_socket = socket(AF_INET, SOCK_STREAM, 0);
	
	if( -1 == node_socket ) {
		perror("Failed to create Node socket");
		exit(1);
	};
	
	struct sockaddr_in node_address;
	node_address.sin_family = AF_INET;
	node_address.sin_addr.s_addr = INADDR_ANY;
	node_address.sin_port = htons(4444);
	
	if( -1 == bind( node_socket, ( struct sockaddr * ) &node_address, sizeof( node_address ) ) ) {
		perror("Failed to bind Node socket");
		exit(1);
	};
	
	if( -1 == listen( node_socket, 300 ) ) {
		perror("Failed to listen on Node socket");
		exit(2);
	};
	
	size_t client_address_size = sizeof( struct sockaddr );
	
	while( ! node_shutdown ) {
		
		client_t client;
		
		client.socket = accept( node_socket, ( struct sockaddr * ) &client.address, &client_address_size );
		
		if( -1 == client.socket ) {
			perror("Failed to accept client");
			continue;
		};
		
		if( threads_count > 300 ) {
			printf("Too many clients (%u), refusing to serve client %u\n", threads_count, client.socket);
			// ASK_OTHER should be implemented here
			continue;
		};
		
		pthread_t thread;
		
		if( 0 != pthread_create( &thread, NULL, client_connection, &client ) ) {
			perror("Failed to create thread for client");
			continue;
		};
		
		if( 0 != pthread_detach( thread ) ) {
			perror("Failed to detach client thread");
			continue;
		};
		
		pthread_mutex_lock( &threads_count_mutex );
		threads_count++;
		pthread_mutex_unlock( &threads_count_mutex );
		
		printf( "Client %u accepted, gone to thread %u. Current threads count is: %u\n", client.socket, (int) thread, threads_count );
		
	};
	
	return 0;
	
};
