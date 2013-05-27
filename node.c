#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include <mhash.h>

#define NODE_DOMAIN  INADDR_ANY
#define NODE_PORT    4444
#define NODE_BACKLOG 300
#define NODE_WORKERS 300

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

node_t *create_node() {
	
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
		
		//~ printf( "Worker, got event %u, connection type %i\n", (int) event, connection_type );
		
		send( event->socket, "HTTP/1.1 200 OK\r\n\r\nOK", 21, 0 );
		
		destroy_connection( node, event->socket );
		
		free( event );
		
	};
	
	return node_pointer;
	
};

int main( int argc, char **argv, char **env ) {
	
	node_t *node = create_node();
	
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
			destroy_events( node, client_socket );
			destroy_connection( node, client_socket );
			continue;
		};
		
		if( -1 == add_connection( node, connection ) ) {
			return 1;
		};
		
		if( -1 == enqueue_event( node, event ) ) {
			return 1;
		};
		
	};
	
	return 0;
	
};
