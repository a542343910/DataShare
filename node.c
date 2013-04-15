// gcc node.c -o node -pthread -lmhash

#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <mhash.h>

#define NODE_LISTEN_ADDRESS INADDR_ANY
#define NODE_LISTEN_PORT 4444
#define NODE_LISTEN_QUEUE 300
#define NODE_MAX_THREADS_COUNT 300

#define NODE_KEY "12345678901234567890123456789011"
#define WRITE_KEY "12345678901234567890123456789012"
#define READ_KEY "12345678901234567890123456789013"

typedef struct client_t {
	unsigned char privileges;
	int socket;
	pthread_t thread;
	struct sockaddr_in address;
} client_t;

char node_shutdown = 0;
int node_socket = 0;
unsigned int threads_counter = 0;
pthread_mutex_t threads_count_mutex = PTHREAD_MUTEX_INITIALIZER;

int threads_count_get() {
	unsigned int threads_count;
	if( 0 != pthread_mutex_lock( &threads_count_mutex ) ) {
		perror("Failed to lock threads counter mutex (getting value)");
		node_shutdown = 1;
		return 0;
	};
	threads_count = threads_counter;
	if( 0 != pthread_mutex_unlock( &threads_count_mutex ) ) {
		perror("Failed to unlock threads counter mutex (getting value)");
		node_shutdown = 1;
		return 0;
	};
	return threads_count;
};

void threads_count_add( char add ) {
	if( 0 != pthread_mutex_lock( &threads_count_mutex ) ) {
		perror("Failed to lock threads counter mutex (setting value)");
		node_shutdown = 1;
		return;
	};
	threads_counter += add;
	if( 0 != pthread_mutex_unlock( &threads_count_mutex ) ) {
		perror("Failed to unlock threads counter mutex (setting value)");
		node_shutdown = 1;
		return;
	};
};

void signal_handler( int signal ) {
	node_shutdown = 1;
	if( node_socket ) {
		if( -1 == close(node_socket) ) {
			perror("Failed to close node socket (catched signal)");
			if( 0 != shutdown(node_socket, SHUT_RDWR) ) {
				perror("Failed to shutdown node socket (catched signal)");
			};
		};
	};
};

unsigned char compare( char *this, char *that, unsigned char length ) {
	unsigned char i = 0;
	for( ; i < length; i++ ) {
		if( this[i] != that[i] ) {
			return 0;
		};
	};
	return i == length;
};

unsigned char auth( client_t *client ) {
	
	client->privileges = 0;
	
	char challenge[32];
	
	if( 32 != recv( client->socket, &challenge, 32, 0 ) ) {
		client->privileges = 0;
		return client->privileges;
	};
	
	char response[32];
	
	MHASH sha256 = mhash_hmac_init( MHASH_SHA256, NODE_KEY, 32, mhash_get_hash_pblock(MHASH_SHA256) );
	mhash(sha256, challenge, 32);
	mhash_deinit(sha256, response);
	
	if( 32 != send( client->socket, response, 32, 0 ) ) {
		client->privileges = 0;
		return client->privileges;
	};
	
	unsigned char i = 0;
	
	for( ; i <= 32; i++ ) {
		challenge[i] = rand() % 255;
	};
	
	if( 32 != send( client->socket, challenge, 32, 0 ) ) {
		client->privileges = 0;
		return client->privileges;
	};
	
	if( 32 != recv( client->socket, &response, 32, 0 ) ) {
		client->privileges = 0;
		return client->privileges;
	};
	
	char buffer[32];
	
	sha256 = mhash_hmac_init( MHASH_SHA256, NODE_KEY, 32, mhash_get_hash_pblock(MHASH_SHA256) );
	mhash(sha256, challenge, 32);
	mhash_deinit(sha256, buffer);
	
	if( compare( buffer, response, 32 ) ) {
		// client is NODE
		client->privileges = 3;
		return client->privileges;
	};
	
	sha256 = mhash_hmac_init( MHASH_SHA256, WRITE_KEY, 32, mhash_get_hash_pblock(MHASH_SHA256) );
	mhash(sha256, challenge, 32);
	mhash_deinit(sha256, buffer);
	
	if( compare( buffer, response, 32 ) ) {
		// client may WRITE
		client->privileges = 2;
		return client->privileges;
	};
	
	sha256 = mhash_hmac_init( MHASH_SHA256, READ_KEY, 32, mhash_get_hash_pblock(MHASH_SHA256) );
	mhash(sha256, challenge, 32);
	mhash_deinit(sha256, buffer);
	
	if( compare( buffer, response, 32 ) ) {
		// client may READ
		client->privileges = 1;
		return client->privileges;
	};
	
	// client has no correct keys, should be rejected
	return client->privileges;
	
};

void *client_connection( void *client_pointer ) {
	
	client_t *client = ( client_t * ) client_pointer;
	
	if( auth(client) ) {
		printf( "Client %u, thread %u: access granted, level %u\n", client->socket, (int) client->thread, client->privileges );
		// process commands
	};
	
	printf( "Client %u, thread %u: access denied, no correct keys\n", client->socket, (int) client->thread );
	
	if( -1 == close( client->socket ) ) {
		perror("Failed to close client socket (end of thread)");
		if( 0 != shutdown( client->socket, SHUT_RDWR ) ) {
			perror("Failed to shutdown client socket (end of thread)");
		};
	};
	
	threads_count_add(-1);
	
	return client_pointer;
	
};

int main( int argc, char *argv[], char *env[] ) {
	
	if( SIG_ERR == signal( SIGINT, signal_handler ) ) {
		perror("Failed to assign SIGINT signal handler");
		return 1;
	};
	
	node_socket = socket(AF_INET, SOCK_STREAM, 0);
	
	if( -1 == node_socket ) {
		perror("Failed create node socket");
		return 1;
	};
	
	struct sockaddr_in node_address;
	node_address.sin_family = AF_INET;
	node_address.sin_port = htons(NODE_LISTEN_PORT);
	node_address.sin_addr.s_addr = NODE_LISTEN_ADDRESS;
	
	socklen_t sockaddr_in_size = sizeof(node_address);
	
	if( -1 == bind( node_socket, ( struct sockaddr *) &node_address, sockaddr_in_size ) ) {
		perror("Failed to bind node socket");
		return 1;
	};
	
	if( -1 == listen(node_socket, NODE_LISTEN_QUEUE) ) {
		perror("Failed to start listening on node socket");
		return 1;
	};
	
	if( 0 != pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0) ) {
		perror("Failed to set threads cancelable");
		return 1;
	};
	
	if( 0 != pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, 0) ) {
		perror("Failed to make threads cancelable asyncronously");
		return 1;
	};
	
	int thread_count = 0;
	
	while( ! node_shutdown ) {
		
		client_t client;
		
		client.socket = accept( node_socket, ( struct sockaddr * ) &client.address, &sockaddr_in_size );
		
		if( -1 == client.socket ) {
			perror("Failed to accept client connection");
			continue;
		};
		
		thread_count = threads_count_get();
		
		if( thread_count > NODE_MAX_THREADS_COUNT ) {
			printf("Refusing to serve client %u, current threads counter is: %u\n", client.socket, thread_count);
			// ASK_ANOTHER should be implemented here
			continue;
		};
		
		threads_count_add(1);
		
		if( 0 != pthread_create( &client.thread, 0, client_connection, ( void * ) &client ) ) {
			perror("Failed to create thread for client connection");
			threads_count_add(-1);
			continue;
		};
		
		if( 0 != pthread_detach( client.thread ) ) {
			perror("Failed to detach thread for client connection");
			if( 0 != pthread_cancel(client.thread) ) {
				perror("Failed to kill undetached thread");
				return 1;
			};
			threads_count_add(-1);
			continue;
		};
		
		printf("Client %u accepted, gone to thread: %u. Current threads count is %u\n", client.socket, (int) client.thread, thread_count + 1);
		
	};
	
	return 0;
	
};
