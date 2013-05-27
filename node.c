#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include <mhash.h>
#include <string.h>
#include <sys/stat.h>

#define NODE_DOMAIN  INADDR_ANY
#define NODE_PORT    4448
#define NODE_BACKLOG 300
#define NODE_WORKERS 300

#define NODE_READ_KEY  "01234567890123456789012345678901"
#define NODE_WRITE_KEY "01234567890123456789012345678902"
#define NODE_NODE_KEY  "01234567890123456789012345678903"

#define NODE_MIN_KEY_LENGTH 32
#define NODE_MAX_KEY_LENGTH 256

#define NODE_CHALLENGE_MIN_LENGTH 32
#define NODE_CHALLENGE_MAX_LENGTH 255

#define NODE_HASH_LENGTH 64

#define NODE_CONNECTION_READONLY  1
#define NODE_CONNECTION_READWRITE 2
#define NODE_CONNECTION_NODE      3

#define NODE_DATA_PATH "./"

/*
 * Authorization:
 * - when client connected, send CHALLENGE (random string)
 * - read RESPONSE[HASH_LENGTH]
 * - if CHALLENGE+READ_KEY == RESPONSE
 *     send read-only
 * - if CHALLENGE+WRITE_KEY == RESPONSE
 *     send read-write
 * - if CHALLENGE+NODE_KEY == RESPONSE
 *     send node-connection
 * - else - destroy connection
 * - read CLIENT_CHALLENGE
 * - respond with CLIENT_CHALLENGE+CLIENT_KEY (READ/WRITE/NODE)
 * - read access rights
 * 
 * */

typedef struct connection_t {
	int socket;
	int type;
	struct connection_t *left;
	struct connection_t *right;
} connection_t;

typedef struct event_t {
	int socket;
	int type;
	struct event_t *left;
	struct event_t *right;
} event_t;

typedef struct node_t {
	event_t         *event;
	pthread_mutex_t *event_mutex;
	pthread_cond_t  *event_condition;
	connection_t    *connection;
	pthread_mutex_t *connection_mutex;
	
	char *read_key;
	int   read_key_length;
	
	char *write_key;
	int   write_key_length;
	
	char *node_key;
	int   node_key_length;
	
} node_t;

connection_t *create_connection( int socket, int type ) {
	
	if( -1 == socket ) {
		fprintf( stderr, "create_connection: socket == -1\n" );
		return 0;
	};
	
	connection_t *connection = malloc( sizeof( connection_t ) );
	
	if( 0 == connection ) {
		perror( "Failed to allocate memory while creating connection" );
		close( socket );
		return 0;
	};
	
	connection->socket = socket;
	connection->type   = type;
	
	connection->left   = 0;
	connection->right  = 0;
	
	return connection;
	
};

event_t *create_event( int socket, int type ) {
	
	if( -1 == socket ) {
		fprintf( stderr, "create_connection: socket == -1\n" );
	};
	
	event_t *event = malloc( sizeof( event_t ) );
	
	if( 0 == event ) {
		perror( "Failed to allocate memory while creating event" );
		return 0;
	};
	
	event->socket = socket;
	event->type   = type;
	
	event->left   = 0;
	event->right  = 0;
	
	return event;
	
};

int enqueue_event( node_t *node, event_t *event ) {
	
	if( 0 == node ) {
		fprintf( stderr, "enqueue_event: node == 0" );
		return 0;
	};
	
	if( 0 == event ) {
		fprintf( stderr, "enqueue_event: event == 0" );
		return 0;
	};
	
	if( 0 == node->event_mutex ) {
		fprintf( stderr, "enqueue_event: node->event_mutex == 0" );
		return 0;
	};
	
	if( 0 == node->event_condition ) {
		fprintf( stderr, "enqueue_event: node->event_condition == 0" );
		return 0;
	};
	
	if( 0 != pthread_mutex_lock( node->event_mutex ) ) {
		perror( "Failed to lock event mutex while enqueueing event" );
		return -1;
	};
	
	if( 0 == node->event ) {
		event->left  = event;
		event->right = event;
		node->event  = event;
	} else {
		event->left              = node->event;
		event->right             = node->event->right;
		node->event->right->left = event;
		node->event->right       = event;
	};
	
	if( 0 != pthread_cond_signal( node->event_condition ) ) {
		perror( "Failed to signal about new event while enqueueing event" );
	};
	
	if( 0 != pthread_mutex_unlock( node->event_mutex ) ) {
		perror( "Failed to unlock event mutex while enqueueing event" );
		return -1;
	};
	
	return 0;
	
};

event_t *dequeue_event( node_t *node ) {
	
	if( 0 == node ) {
		fprintf( stderr, "enqueue_event: node == 0" );
		return 0;
	};
	
	if( 0 == node->event_mutex ) {
		fprintf( stderr, "enqueue_event: node->event_mutex == 0" );
		return 0;
	};
	
	if( 0 == node->event_condition ) {
		fprintf( stderr, "enqueue_event: node->event_condition == 0" );
		return 0;
	};
	
	if( 0 != pthread_mutex_lock( node->event_mutex ) ) {
		perror( "Failed to lock event mutex while dequeueing event" );
		return 0;
	};
	
	while( 0 == node->event ) {
		pthread_cond_wait( node->event_condition, node->event_mutex );
	};
	
	event_t *event;
	
	if( ( node->event == node->event->right ) && ( node->event->right == node->event->left ) ) {
		
		event       = node->event;
		node->event = 0;
		
	} else {
		
		event                          = node->event->left;
		node->event->left->left->right = node->event;
		node->event->left              = node->event->left->left;
		
	};
	
	if( 0 != pthread_mutex_unlock( node->event_mutex ) ) {
		perror( "Failed to unlock event mutex while dequeueing event" );
		return 0;
	};
	
	return event;
	
};

int get_connection_type( node_t *node, int socket ) {
	
	if( 0 == node ) {
		fprintf( stderr, "get_connection_type: node == 0" );
		return -1;
	};
	
	if( -1 == socket ) {
		fprintf( stderr, "get_connection_type: socket == -1" );
		return -1;
	};
	
	if( 0 == node->connection_mutex ) {
		fprintf( stderr, "get_connection_type: node->connection_mutex == 0" );
		return -1;
	};
	
	if( 0 != pthread_mutex_lock( node->connection_mutex ) ) {
		perror( "Failed to lock connection mutex while getting connection type" );
		return -2;
	};
	
	if( 0 == node->connection ) {
		return -1;
	};
	
	connection_t *current = node->connection->right;
	
	int type = -1;
	
	do {
		
		if( current->socket == socket ) {
			type = current->type;
			break;
		};
		
		current = current->right;
		
	} while( current != node->connection );
	
	if( 0 != pthread_mutex_unlock( node->connection_mutex ) ) {
		perror( "Failed to unlock connection mutex while getting connection type" );
		return -2;
	};
	
	return type;
	
};

int set_connection_type( node_t *node, int socket, int type ) {
	
	if( 0 == node ) {
		fprintf( stderr, "set_connection_type: node == 0" );
		return 0;
	};
	
	if( -1 == socket ) {
		fprintf( stderr, "set_connection_type: socket == -1" );
		return 0;
	};
	
	if( 0 == node->connection_mutex ) {
		fprintf( stderr, "get_connection_type: node->connection_mutex == 0" );
		return 0;
	};
	
	if( 0 != pthread_mutex_lock( node->connection_mutex ) ) {
		perror( "Failed to lock connection mutex while setting connection type" );
		return -1;
	};
	
	if( 0 == node->connection ) {
		return 0;
	};
	
	connection_t *current = node->connection->right;
	
	do {
		
		if( current->socket == socket ) {
			current->type = type;
			break;
		};
		
		current = current->right;
		
	} while( current != node->connection );
	
	if( 0 != pthread_mutex_unlock( node->connection_mutex ) ) {
		perror( "Failed to unlock connection mutex while setting connection type" );
		return -1;
	};
	
	return 0;
	
};

int destroy_events( node_t *node, int socket ) {
	
	if( 0 == node ) {
		fprintf( stderr, "destroy_events: node == 0" );
		return 0;
	};
	
	if( -1 == socket ) {
		fprintf( stderr, "destroy_events: socket == -1" );
		return 0;
	};
	
	if( 0 == node->event_mutex ) {
		fprintf( stderr, "destroy_events: node->event_mutex == 0" );
		return 0;
	};
	
	if( 0 != pthread_mutex_lock( node->event_mutex ) ) {
		perror( "Failed to lock event mutex while destroying events" );
		return -1;
	};
	
	int count = 0;
	
	if( node->event ) {
		
		event_t *current = node->event->right;
		
		do {
			
			if( current->socket == socket ) {
				
				if( current->left == current->right ) {
					
					free( node->event );
					node->event = 0;
					count++;
					break;
					
				} else {
					
					current->left->right = current->right;
					current->right->left = current->left;
					
					free( current );
					
				};
				
				count++;
				
			};
			
		} while( current != node->event );
		
	};
	
	if( 0 != pthread_mutex_unlock( node->event_mutex ) ) {
		perror( "Failed to unlock event mutex while destroying event" );
		return -1;
	};
	
	return count;
	
};

int destroy_connection( node_t *node, int socket ) {
	
	if( 0 == node ) {
		fprintf( stderr, "destroy_connection: node == 0" );
		return -1;
	};
	
	if( -1 == socket ) {
		fprintf( stderr, "destroy_connection: socket == -1" );
		return -1;
	};
	
	if( 0 == node->connection_mutex ) {
		fprintf( stderr, "destroy_connection: node->connection_mutex == 0" );
		return -1;
	};
	
	if( 0 != pthread_mutex_lock( node->connection_mutex ) ) {
		perror( "Failed to lock connection mutex while destroying connection" );
		return -2;
	};
	
	int destroyed = -1;
	
	if( 0 != node->connection ) {
		
		connection_t *current = node->connection->right;
		
		do {
			
			if( current->socket == socket ) {
				
				close( socket );
				
				if( current->left == current->right ) {
					free( node->connection );
					node->connection = 0;
				} else {
					current->left->right = current->right;
					current->right->left = current->left;
					free( current );
				};
				
				destroyed = 0;
				
				break;
				
			};
			
		} while( current != node->connection );
		
	};
	
	if( 0 != pthread_mutex_unlock( node->connection_mutex ) ) {
		perror( "Failed to unlock connection mutex while destroying connection" );
		return -2;
	};
	
	if( -1 == destroy_events( node, socket ) ) {
		return -2;
	};
	
	return destroyed;
	
};

int add_connection( node_t *node, connection_t *connection ) {
	
	if( 0 == node ) {
		fprintf( stderr, "add_connection: node == 0" );
		return 0;
	};
	
	if( 0 == connection ) {
		fprintf( stderr, "add_connection: connection == 0" );
		return 0;
	};
	
	if( 0 == node->connection_mutex ) {
		fprintf( stderr, "add_connection: node->connection_mutex == 0" );
		return 0;
	};
	
	if( 0 != pthread_mutex_lock( node->connection_mutex ) ) {
		perror( "Failed to lock node connection mutex while adding connection" );
		return -1;
	};
	
	if( 0 == node->connection ) {
		connection->left  = connection;
		connection->right = connection;
		node->connection  = connection;
	} else {
		connection->left              = node->connection;
		connection->right             = node->connection->right;
		node->connection->right->left = connection;
		node->connection->right       = connection;
	};
	
	if( 0 != pthread_mutex_unlock( node->connection_mutex ) ) {
		perror( "Failed to lock node connection mutex while adding connection" );
		return -1;
	};
	
	return 0;
	
};

node_t *create_node( char *read_key, char *write_key, char *node_key ) {
	
	if( 0 == read_key ) {
		fprintf( stderr, "No read-key provided\n" );
		return 0;
	};
	
	if( 0 == write_key ) {
		fprintf( stderr, "No write-key provided\n" );
		return 0;
	};
	
	if( 0 == node_key ) {
		fprintf( stderr, "No node-key provided\n" );
		return 0;
	};
	
	int read_key_length  = strlen( read_key );
	int write_key_length = strlen( write_key );
	int node_key_length  = strlen( node_key );
	
	if( NODE_MIN_KEY_LENGTH > read_key_length ) {
		fprintf( stderr, "Read key should be at least %i characters long", NODE_MIN_KEY_LENGTH );
		return 0;
	};
	
	if( NODE_MAX_KEY_LENGTH < read_key_length ) {
		fprintf( stderr, "Read key should not be longer than %i characters", NODE_MAX_KEY_LENGTH );
		return 0;
	};
	
	if( NODE_MIN_KEY_LENGTH > write_key_length ) {
		fprintf( stderr, "Write key should be at least %i characters long", NODE_MIN_KEY_LENGTH );
		return 0;
	};
	
	if( NODE_MAX_KEY_LENGTH < write_key_length ) {
		fprintf( stderr, "Write key should not be longer than %i characters", NODE_MAX_KEY_LENGTH );
		return 0;
	};
	
	if( NODE_MIN_KEY_LENGTH > node_key_length ) {
		fprintf( stderr, "Node key should be at least %i characters long", NODE_MIN_KEY_LENGTH );
		return 0;
	};
	
	if( NODE_MAX_KEY_LENGTH < node_key_length ) {
		fprintf( stderr, "Node key should not be longer than %i characters", NODE_MAX_KEY_LENGTH );
		return 0;
	};
	
	node_t *node = malloc( sizeof( node_t ) );
	
	if( 0 == node ) {
		perror( "Failed to allocate memory for node" );
		return 0;
	};
	
	node->event_mutex = malloc( sizeof( pthread_mutex_t ) );
	
	if( 0 == node->event_mutex ) {
		perror( "Failed to allocate memory for node event mutex" );
		free( node );
		return 0;
	};
	
	node->event_condition = malloc( sizeof( pthread_cond_t ) );
	
	if( 0 == node->event_condition ) {
		perror( "Failed to allocate memory for node event condition" );
		pthread_mutex_destroy( node->event_mutex );
		free( node->event_mutex );
		free( node );
		return 0;
	};
	
	node->connection_mutex = malloc( sizeof( pthread_mutex_t ) );
	
	if( 0 == node->connection_mutex ) {
		perror( "Failed to allocate memory for node connection mutex" );
		pthread_mutex_destroy( node->event_mutex );
		pthread_cond_destroy( node->event_condition );
		free( node->event_mutex );
		free( node->event_condition );
		free( node );
		return 0;
	};
	
	node->event      = 0;
	node->connection = 0;
	
	node->read_key   = read_key;
	node->write_key  = write_key;
	node->node_key   = node_key;
	
	node->read_key_length   = read_key_length;
	node->write_key_length  = write_key_length;
	node->node_key_length   = node_key_length;
	
	return node;
	
};

int init_node_socket( int domain, int port, int backlog ) {
	
	int node_socket = socket( AF_INET, SOCK_STREAM, 0 );
	
	if( -1 == node_socket ) {
		perror( "Failed to create node socket" );
		return -1;
	};
	
	struct sockaddr_in node_address;
	
	node_address.sin_family      = AF_INET;
	node_address.sin_port        = port;
	node_address.sin_addr.s_addr = domain;
	
	if( -1 == bind( node_socket, ( struct sockaddr * ) &node_address, sizeof( node_address ) ) ) {
		perror( "Failed to bind node socket" );
		close( node_socket );
		return -1;
	};
	
	if( -1 == listen( node_socket, backlog ) ) {
		perror( "Failed to listen on node socket" );
		close( node_socket );
		return -1;
	};
	
	return node_socket;
	
};

int init_master_socket( int domain, int port ) {
	
	int master_socket = socket( AF_INET, SOCK_STREAM, 0 );
	
	if( -1 == master_socket ) {
		perror( "Failed to create master socket" );
		return -1;
	};
	
	struct sockaddr_in master_address;
	
	master_address.sin_family      = AF_INET;
	master_address.sin_port        = port;
	master_address.sin_addr.s_addr = domain;
	
	if( -1 == connect( master_socket, ( struct sockaddr * ) &master_address, sizeof( master_address ) ) ) {
		perror( "Failed to connect to master node" );
		close( master_socket );
		return -1;
	};
	
	return master_socket;
	
};

pthread_t *init_workers( int count, void *( worker_routine )( void * ), node_t *node ) {
	
	if( 1 > count ) {
		fprintf( stderr, "create_workers: count == %i, count < 1\n", count );
		return 0;
	};
	
	if( 0 == node ) {
		fprintf( stderr, "create_workers: node == 0");
		return 0;
	};
	
	if( 0 != pthread_setcancelstate( PTHREAD_CANCEL_ENABLE, 0 ) ) {
		perror( "Failed to make threads (workers) cancelable" );
		return 0;
	};
	
	if( 0 != pthread_setcanceltype( PTHREAD_CANCEL_ASYNCHRONOUS, 0 ) ) {
		perror( "Failed to make threads (workers) cancelable asynchronously" );
		return 0;
	};
	
	pthread_t *workers = malloc( sizeof( pthread_t ) * count );
	
	int i = 0;
	
	for( ; i < count; i++ ) {
		
		if( 0 != pthread_create( &workers[i], 0, worker_routine, node ) ) {
			
			perror( "Failed to create thread (worker)" );
			
			i--;
			
			for( ; i >= 0; i-- ) {
				pthread_cancel( workers[i] );
			};
			
			free( workers );
			
			return 0;
			
		};
		
	};
	
	return workers;
	
};

unsigned char compare( char *this, char *that, unsigned int length ) {
	unsigned int i = 0;
	for( ; i < length; i++ ) {
		if( this[i] ^ that[i] ) {
			return 0;
		};
	};
	return 1;
};

char *socket_read( node_t *node, int socket, unsigned char length ) {
	
	if( 0 == node ) {
		fprintf( stderr, "socket_read: node == 0\n" );
		return 0;
	};
	
	if( -1 == socket ) {
		fprintf( stderr, "socket_read: socket == -1\n" );
		return 0;
	};
	
	if( 0 == length ) {
		fprintf( stderr, "socket_read: length == 0\n" );
		return 0;
	};
	
	char *data = malloc( length );
	
	if( 0 == data ) {
		perror( "Failed to allocate memory for buffer while reading from socket" );
		return 0;
	};
	
	if( length != recv( socket, data, length, 0 ) ) {
		free( data );
		destroy_connection( node, socket );
		return 0;
	};
	
	return data;
	
};

int socket_write( node_t *node, int socket, char *data, unsigned char length ) {
	
	if( 0 == node ) {
		fprintf( stderr, "socket_write: node == 0\n" );
		return 0;
	};
	
	if( -1 == socket ) {
		fprintf( stderr, "socket_write: socket == -1\n" );
		return 0;
	};
	
	if( 0 == data ) {
		fprintf( stderr, "socket_write: data == 0\n" );
		return 0;
	};
	
	if( 0 == length ) {
		fprintf( stderr, "socket_write: length == 0\n" );
		return 0;
	};
	
	if( length != send( socket, data, length, 0 ) ) {
		destroy_connection( node, socket );
		return -1;
	};
	
	return 0;
	
};

char *create_challenge( unsigned char length ) {
	
	if( 0 == length ) {
		fprintf( stderr, "create_challenge: length == 0\n" );
		return 0;
	};
	
	char *challenge = malloc( length );
	
	if( 0 == challenge ) {
		perror( "Failed to allocate memory for challenge" );
		return 0;
	};
	
	unsigned char i = 0;
	
	for( ; i < length; i++ ) {
		challenge[i] = rand() % 255;
	};
	
	return challenge;
	
};

void hex_dump( char *data, unsigned int length ) {
	unsigned int i = 0;
	for( ; i < length; i++ ) {
		printf( "%02X ", ( unsigned char ) data[i] );
	};
	printf( "\n" );
};

int authentification_finish( node_t *node, int socket, char *key, unsigned char key_length ) {
	
	if( 0 == node ) {
		fprintf( stderr, "authentification_finish: node == 0\n" );
		return 0;
	};
	
	if( -1 == socket ) {
		fprintf( stderr, "authentification_finish: socket == -1\n" );
		return 0;
	};
	
	if( 0 == key ) {
		fprintf( stderr, "authentification_finish: key == 0\n" );
		return 0;
	};
	
	if( 0 == key_length ) {
		fprintf( stderr, "authentification_finish: key_length == 0\n" );
		return 0;
	};
	
	char *buffer = socket_read( node, socket, 1 );
	
	if( 0 == buffer ) {
		destroy_connection( node, socket );
		return 0;
	};
	
	unsigned char challenge_length = ( unsigned char ) buffer[0];
	
	free( buffer );
	
	if( 0 == challenge_length ) {
		return -1;
	};
	
	char *challenge = socket_read( node, socket, challenge_length);
	
	if( 0 == challenge ) {
		return -1;
	};
	
	MHASH hasher;
	
	char hash[NODE_HASH_LENGTH];
	
	hasher = mhash_hmac_init( MHASH_SHA512, key, key_length, mhash_get_hash_pblock( MHASH_SHA512 ) );
	mhash( hasher, challenge, challenge_length );
	mhash_hmac_deinit( hasher, hash );
	
	if( -1 == socket_write( node, socket, hash, NODE_HASH_LENGTH ) ) {
		free( challenge );
		return -1;
	};
	
	return 0;
	
};

void client_authentification( node_t *node, event_t *event ) {
	
	if( 0 == node ) {
		fprintf( stderr, "client_authentification: node == 0\n" );
		return;
	};
	
	if( 0 == event ) {
		fprintf( stderr, "client_authentification: event == 0\n" );
		return;
	};
	
	unsigned char challenge_length = NODE_CHALLENGE_MIN_LENGTH + rand() % ( NODE_CHALLENGE_MAX_LENGTH - NODE_CHALLENGE_MIN_LENGTH );
	
	if( -1 == socket_write( node, event->socket, ( char * ) &challenge_length, 1 ) ) {
		return;
	};
	
	char *challenge = create_challenge( challenge_length );
	
	if( 0 == challenge ) {
		destroy_connection( node, event->socket );
		return;
	};
	
	if( -1 == socket_write( node, event->socket, challenge, challenge_length ) ) {
		free( challenge );
		return;
	};
	
	char *response = socket_read( node, event->socket, NODE_HASH_LENGTH );
	
	if( 0 == response ) {
		return;
	};
	
	char buffer[NODE_HASH_LENGTH];
	
	MHASH hasher;
	
	hasher = mhash_hmac_init( MHASH_SHA512, node->read_key, node->read_key_length, mhash_get_hash_pblock( MHASH_SHA512 ) );
	mhash( hasher, challenge, challenge_length );
	mhash_hmac_deinit( hasher, buffer );
	
	if( compare( response, buffer, NODE_HASH_LENGTH ) ) {
		
		free( challenge );
		free( response );
		
		if( -1 == authentification_finish( node, event->socket, node->read_key, node->read_key_length ) ) {
			return;
		};
		
		if( -1 == set_connection_type( node, event->socket, NODE_CONNECTION_READONLY ) ) {
			destroy_connection( node, event->socket );
			return;
		};
		
		return;
		
	};
	
	hasher = mhash_hmac_init( MHASH_SHA512, node->write_key, node->write_key_length, mhash_get_hash_pblock( MHASH_SHA512 ) );
	mhash( hasher, challenge, challenge_length );
	mhash_hmac_deinit( hasher, buffer );
	
	if( compare( response, buffer, NODE_HASH_LENGTH ) ) {
		
		free( challenge );
		free( response );
		
		if( -1 == authentification_finish( node, event->socket, node->write_key, node->write_key_length ) ) {
			return;
		};
		
		if( -1 == set_connection_type( node, event->socket, NODE_CONNECTION_READWRITE ) ) {
			destroy_connection( node, event->socket );
			return;
		};
		
	};
	hasher = mhash_hmac_init( MHASH_SHA512, node->node_key, node->node_key_length, mhash_get_hash_pblock( MHASH_SHA512 ) );
	mhash( hasher, challenge, challenge_length );
	mhash_hmac_deinit( hasher, buffer );
	
	if( compare( response, buffer, NODE_HASH_LENGTH ) ) {
		
		free( challenge );
		free( response );
		
		if( -1 == authentification_finish( node, event->socket, node->node_key, node->node_key_length ) ) {
			return;
		};
		
		if( -1 == set_connection_type( node, event->socket, NODE_CONNECTION_READWRITE ) ) {
			destroy_connection( node, event->socket );
			return;
		};
		
	};
	
	destroy_connection( node, event->socket );
	
};

char *unpack_hash( char *hash ) {
	
	if( 0 == hash ) {
		fprintf( stderr, "unpack_hash: hash == 0\n" );
		return 0;
	};
	
	char *hash_hex = malloc( NODE_HASH_LENGTH * 2 + 1 );
	
	if( 0 == hash_hex ) {
		perror( "Failed to allocate memory for unpacked hash" );
		return 0;
	};
	
	hash_hex[ NODE_HASH_LENGTH * 2 ] = 0;
	
	unsigned int i = 0;
	
	for( ; i < NODE_HASH_LENGTH; i++ ) {
		sprintf( &hash_hex[ i * 2 ], "%02X", ( unsigned char ) hash[i] );
	};
	
	return hash_hex;
	
};

void client_get_chunk( node_t *node, event_t *event ) {
	
	if( 0 == node ) {
		fprintf( stderr, "client_get_chunk: node == 0\n" );
		return;
	};
	
	if( 0 == event ) {
		fprintf( stderr, "client_get_chunk: event == 0\n" );
		return;
	};
	
	char *chunk_hash = socket_read( node, event->socket, NODE_HASH_LENGTH );
	
	if( 0 == chunk_hash ) {
		return;
	};
	
	char *chunk_hash_hex = unpack_hash( chunk_hash );
	
	char *chunk_path = strcat( NODE_DATA_PATH, chunk_hash_hex );
	
	struct stat chunk_stat;
	
	if( -1 == stat( chunk_path, &chunk_stat ) ) {
		char response = 0;
		socket_write( node, event->socket, &response, 1 );
		// proxy to neighbours
		return;
	};
	
	FILE *chunk_file = fopen( chunk_path, "r" );
	
	if( 0 == chunk_file ) {
		perror( "Failed to open chunk file for reading" );
		destroy_connection( node, event->socket );
		return;
	};
	
	char *chunk_contents = malloc( chunk_stat.st_size );
	
	if( 0 == chunk_contents ) {
		perror( "Failed to allocate memory for chunk contents" );
		fclose( chunk_file );
		destroy_connection( node, event->socket );
		return;
	};
	
	if( 1 != fread( chunk_contents, chunk_stat.st_size, 1, chunk_file ) ) {
		free( chunk_contents );
		fclose( chunk_file );
		destroy_connection( node, event->socket );
		return;
	};
	
	fclose( chunk_file );
	
	unsigned int chunk_given = 0;
	
	while( chunk_stat.st_size ) {
		
		unsigned char part_size = chunk_stat.st_size > 255 ? 255 : chunk_stat.st_size;
		
		if( -1 == socket_write( node, event->socket, &chunk_contents[chunk_given], part_size ) ) {
			free( chunk_contents );
			return;
		};
		
		chunk_given += part_size;
		
	};
	
};

void *worker_routine( void *node_pointer ) {
	
	if( 0 == node_pointer ) {
		fprintf( stderr, "worker_routine: node_pointer == 0\n" );
		return 0;
	};
	
	node_t *node = ( node_t * ) node_pointer;
	
	for( ; ; ) {
		
		event_t *event = dequeue_event( node );
		
		int connection_type = get_connection_type( node, event->socket );
		
		if( -2 == connection_type ) {
			return node_pointer;
		};
		
		if( -1 == connection_type ) {
			free( event );
			continue;
		};
		
		if( -1 == event->socket ) {
			free( event );
			continue;
		};
		
		switch( event->type ) {
			case 0: client_authentification( node, event ); break; // auth client-node
			case 1: break; // auth node-node
			case 2: client_get_chunk( node, event ); break;
			
			
			default: destroy_connection( node, event->socket );
		};
		
		free( event );
		
	};
	
	return node_pointer;
	
};

int main( int argc, char **argv, char **env ) {
	
	node_t *node = create_node( NODE_READ_KEY, NODE_WRITE_KEY, NODE_NODE_KEY );
	
	if( 0 == node ) {
		return 1;
	};
	
	pthread_t *workers = init_workers( NODE_WORKERS, worker_routine, node );
	
	if( 0 == workers ) {
		return 1;
	};
	
	int node_socket = init_node_socket( NODE_DOMAIN, htons( NODE_PORT ), NODE_BACKLOG );
	
	if( -1 == node_socket ) {
		return 1;
	};
	
	for( ; ; ) {
		
		int client_socket = accept( node_socket, 0, 0 );
		
		if( -1 == client_socket ) {
			perror( "Failed to accept client connection" );
			continue;
		};
		
		connection_t *connection = create_connection( client_socket, 0 );
		
		if( 0 == connection ) {
			continue;
		};
		
		event_t *event = create_event( client_socket, 0 );
		
		if( 0 == event ) {
			destroy_connection( node, client_socket );
			continue;
		};
		
		if( -1 == add_connection( node, connection ) ) {
			close( node_socket );
			return 1;
		};
		
		if( -1 == enqueue_event( node, event ) ) {
			close( node_socket );
			return 1;
		};
		
	};
	
	return 0;
	
};
