#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <mhash.h>

#define NODE_ADDRESS "127.0.0.1"
#define NODE_PORT     4448

#define NODE_READ_KEY  "01234567890123456789012345678901"
#define NODE_WRITE_KEY "01234567890123456789012345678902"
#define NODE_NODE_KEY  "01234567890123456789012345678903"

#define KEY_LENGTH 32
#define HASH_LENGTH 64

int main( int argc, char **argv, char **env ) {
	
	int node_socket = socket( AF_INET, SOCK_STREAM, 0 );
	
	if( -1 == node_socket ) {
		perror( "Failed to create node socket" );
		return 1;
	};
	
	struct sockaddr_in node_address;
	
	node_address.sin_family = AF_INET;
	node_address.sin_port   = htons( NODE_PORT );
	node_address.sin_addr.s_addr = inet_addr( NODE_ADDRESS );
	
	if( -1 == connect( node_socket, ( struct sockaddr * ) &node_address, sizeof( node_address ) ) ) {
		perror( "Failed to connect to node" );
		return 1;
	};
	
	int got_bytes = 0;
	
	unsigned char challenge_length;
	
	if( -1 == ( got_bytes = recv( node_socket, &challenge_length, 1, 0 ) ) ) {
		perror( "Failed to get challenge length from node" );
		close( node_socket );
		return 1;
	};
	
	printf( "Challenge length: %u; got bytes: %i\n", challenge_length, got_bytes );
	
	char *challenge = malloc( challenge_length );
	
	if( 0 == challenge ) {
		perror( "Failed to allocate memory for node challenge" );
		close( node_socket );
		return 1;
	};
	
	if( -1 == ( got_bytes = recv( node_socket, challenge, challenge_length, 0 ) ) ) {
		perror( "Failed to read challenge from node" );
		free( challenge );
		close( node_socket );
		return 1;
	};
	
	printf( "Challenge:\n" );
	
	unsigned char i = 0;
	
	for( ; i < challenge_length; i++ ) {
		printf( "%02X ", (unsigned char ) challenge[i] );
	};
	
	printf( "\ngot bytes: %i\n", got_bytes );
	
	char buffer[HASH_LENGTH];
	
	MHASH hasher = mhash_hmac_init( MHASH_SHA512, NODE_READ_KEY, KEY_LENGTH, mhash_get_hash_pblock( MHASH_SHA512 ) );
	mhash( hasher, challenge, challenge_length );
	mhash_hmac_deinit( hasher, buffer );
	
	printf( "Responding with:\n" );
	
	for( i = 0; i < HASH_LENGTH; i++ ) {
		printf( "%02X ", ( unsigned char ) buffer[i] );
	};
	
	printf( "\n" );
	
	if( -1 == ( got_bytes =  send( node_socket, buffer, HASH_LENGTH, 0 ) ) ) {
		perror( "Failed to send response to node" );
		free( challenge );
		close( node_socket );
		return 1;
	};
	
	//~ printf( "Debug: %i, %s [bytes: %i]\n", recv( node_socket, buffer, 8, 0 ), buffer, got_bytes );
	
	char *my_challenge = "119272736t363yu2hjhd7fs8d7f967sd7fdhfdyf6d7";
	unsigned char my_challenge_length = strlen(my_challenge);
	
	if( -1 == send( node_socket, &my_challenge_length, 1, 0 ) ) {
		perror( "Failed to send my challenge length" );
		return 1;
	};
	
	if( -1 == send( node_socket, my_challenge, my_challenge_length, 0 ) ) {
		perror("Failed to send challenge");
		return 1;
	};
	
	if( -1 == recv( node_socket, buffer, HASH_LENGTH, 0 ) ) {
		perror( "No resp." );
		return 1;
	};
	
	
	for( i = 0; i < HASH_LENGTH; i++ ) {
		printf( "%02X ", ( unsigned char ) buffer[i] );
	};
	printf( "\n" );
	
	
	return 0;
	
};
