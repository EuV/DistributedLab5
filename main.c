#include "la5.h"

timestamp_t lamportTime = 0;

timestamp_t get_lamport_time() {
	return lamportTime;
}


void incLamportTime() {
	lamportTime++;
}


void setMaxLamportTime( timestamp_t msgTime ) {
	if( lamportTime < msgTime ) {
		lamportTime = msgTime;
	}
}


int main( int argc, char* argv[] ) {

	EventsLog = open( evengs_log, O_CREAT | O_TRUNC | O_WRONLY | O_APPEND, 0777 );
	PipesLog = open( pipes_log, O_CREAT | O_TRUNC | O_WRONLY | O_APPEND, 0777 );

	int procTotal;
	bool isMutex = false;
	getNumberOfProcessAndMutex( argc, argv, &procTotal, &isMutex );

	createFullyConnectedTopology( procTotal );

	makePipeLog( procTotal );

	makeChildren( procTotal, isMutex );

	return 0;
}


void childProcess( Process* proc ) {

	closeUnusedPipes( proc );

	// STARTED
	proc -> started += 1;
	incLamportTime();
	Message startedMsg;
	fillMessage( &startedMsg, proc, STARTED );
	makeIPCLog( startedMsg.s_payload );
	send_multicast( proc, &startedMsg );


	// Receive STARTED
	while( proc -> started != proc -> total ) {
		defaultCSExtendedCycle( proc );
	}
	sprintf( LogBuf, log_received_all_started_fmt, get_lamport_time(), proc -> localId );
	makeIPCLog( LogBuf );


	// Payload
	int totalIterations = proc -> localId * 5;
	for( int i = 1; i <= totalIterations; i ++ ) {
		sprintf( LogBuf, log_loop_operation_fmt, proc -> localId, i, totalIterations );

		if( proc -> isMutex ) request_cs( proc );

		print( LogBuf );

		if( proc -> isMutex ) release_cs( proc );
	}


	// DONE
	proc -> done += 1;
	incLamportTime();
	Message doneMsg;
	fillMessage( &doneMsg, proc, DONE );
	makeIPCLog( doneMsg.s_payload );
	send_multicast( proc, &doneMsg );


	// Receive DONE
	while( proc -> done != proc -> total ) {
		defaultCSExtendedCycle( proc );
	}
	sprintf( LogBuf, log_received_all_done_fmt, get_lamport_time(), proc -> localId );
	makeIPCLog( LogBuf );

	closeTheOtherPipes( proc );
}


int request_cs( const void* self ) {
	Process* proc = ( Process* ) self;

	// CS_REQUEST
	incLamportTime();
	Message requestMsg;
	fillMessage( &requestMsg, proc, CS_REQUEST );
	send_multicast( proc, &requestMsg );

	proc -> selfRequestTime = get_lamport_time();

	proc -> replied = 1;
	while( proc -> replied != proc -> total ) {
		defaultCSExtendedCycle( proc );
	}

	return 0;
}


void defaultCSExtendedCycle( Process* proc ) {

	Message msg;
	receive_any( proc, &msg );

	setMaxLamportTime( msg.s_header.s_local_time );
	incLamportTime();

	switch( msg.s_header.s_type ) {
		case STARTED: {
			proc -> started += 1;
			break;
		}
		case CS_REQUEST: {

			// Immediately answer to the requesting process in the following cases:
			// * There is no CS-request in this process at all
			// * Request time in the requesting process has less value than in this one
			// * Request times are equal, but local id of this process is greater than the other one
			// Otherwise put its id into the deferred-reply array

			if( proc -> selfRequestTime == OUT_OF_CS ||
				msg.s_header.s_local_time < proc -> selfRequestTime ||
				( msg.s_header.s_local_time == proc -> selfRequestTime &&
					proc -> msgAuthor < proc -> localId ) ) {

				// CS_REPLY
				incLamportTime();
				Message replyMsg;
				fillMessage( &replyMsg, proc, CS_REPLY );
				send( proc, proc -> msgAuthor, &replyMsg );
			} else {
				proc -> DR[ proc -> msgAuthor ] = 1;
			}

			break;
		}
		case CS_REPLY: {
			proc -> replied += 1;
			break;
		}
		case DONE: {
			proc -> done += 1;
			break;
		}
	}
}


int release_cs( const void* self ) {
	Process* proc = ( Process* ) self;
	proc -> selfRequestTime = OUT_OF_CS;

	for( int id = 0; id <= MAX_PROCESS_ID; id++ ) {
		if( proc -> DR[ id ] == 1 ) {

			// CS_REPLY
			incLamportTime();
			Message replyMsg;
			fillMessage( &replyMsg, proc, CS_REPLY );
			send( proc, id, &replyMsg );

			proc -> DR[ id ] = 0;
		}
	}

	return 0;
}


void parentProcess( Process* const proc ) {

	closeUnusedPipes( proc );

	// Receive STARTED
	receiveAll( proc, STARTED, proc -> total );
	sprintf( LogBuf, log_received_all_started_fmt, get_lamport_time(), proc -> localId );
	makeIPCLog( LogBuf );

	// Receive DONE
	receiveAll( proc, DONE, proc -> total );
	sprintf( LogBuf, log_received_all_done_fmt, get_lamport_time(), proc -> localId );
	makeIPCLog( LogBuf );

	waitForChildren();

	closeTheOtherPipes( proc );
}


void receiveAll( Process* const proc, const MessageType msgType, const int expectedNumber ) {

	Message incomingMsg;
	int counter = 0;

	while ( counter != expectedNumber ) {

		int result = receive_any( proc, &incomingMsg );

		setMaxLamportTime( incomingMsg.s_header.s_local_time );
		incLamportTime();

		// Skip CS-messages in the parent process
		if( proc -> localId == PARENT_ID &&
			result == IPC_SUCCESS &&
			( incomingMsg.s_header.s_type == CS_REQUEST ||
				incomingMsg.s_header.s_type == CS_REPLY ||
				incomingMsg.s_header.s_type == CS_RELEASE ) ) {
			continue;
		}

		if( result == IPC_SUCCESS && incomingMsg.s_header.s_type == msgType ) {
			counter++;
		} else {
			printf( "Receive error in Process %d\n", getpid() );
			exit( 1 );
		}
	}
}


void getNumberOfProcessAndMutex( int argc, char* const argv[], int* numberOfProcess, bool* isMutex ) {

	opterr = 0;
	*numberOfProcess = 0;

	static struct option longOpt[] = {
		{ "mutexl", 0, 0, 'm' },
		{ 0, 0, 0, 0 }
	};

	int opt, optIndex = 0;
	while ( ( opt =  getopt_long( argc, argv, "p:", longOpt, &optIndex ) ) != -1 ) {
		switch ( opt ) {
		case 'p':
			*numberOfProcess = atoi( optarg );
			break;
		case 'm':
			*isMutex = true;
			break;
		}
	}

	if ( *numberOfProcess == 0 || *numberOfProcess > MAX_PROCESS_ID ) {
		printf( "Set the default value for the number of child process: %d\n", NUMBER_OF_PROCESS );
		*numberOfProcess = NUMBER_OF_PROCESS;
	}
}


void createFullyConnectedTopology( const int procTotal ) {
	for ( int row = 0; row <= procTotal; row++ ) {
		for ( int col = 0; col <= procTotal; col++ ) {
			if ( row == col ) continue;
			pipe( Pipes[ row ][ col ] );
			fcntl( Pipes[ row ][ col ][ READ ], F_SETFL, O_NONBLOCK );
			fcntl( Pipes[ row ][ col ][ WRITE ], F_SETFL, O_NONBLOCK );
		}
	}
}


void closeUnusedPipes( const Process* const proc ) {
	for ( int row = PARENT_ID; row <= proc -> total; row++ ) {
		for ( int col = PARENT_ID; col <= proc -> total; col++ ) {
			if ( row == col ) continue;
			if ( row == proc -> localId ) {
				close( Pipes[ row ][ col ][ READ ] );
			} else {
				close( Pipes[ row ][ col ][ WRITE ] );
				if ( col != proc -> localId ) {
					close( Pipes[ row ][ col ][ READ ] );
				}
			}
		}
	}
}


void closeTheOtherPipes( const Process* const proc ) {
	for ( int rowOrCol = PARENT_ID; rowOrCol <= proc -> total; rowOrCol++ ) {
		if ( rowOrCol == proc -> localId ) continue;
		close( Pipes[ proc -> localId ][ rowOrCol ][ WRITE ] );
		close( Pipes[ rowOrCol ][ proc -> localId ][ READ ] );
	}
}


void makeChildren( const int procTotal, const bool isMutex ) {
	for ( int i = 1; i <= procTotal; i++ ) {
		Process process = { procTotal, PARENT_ID, isMutex, 0, 0, 0, 0, OUT_OF_CS };
		if ( fork() == 0 ) {
			process.localId = i;
			childProcess( &process );
			break; // To avoid fork() in a child
		} else if ( i == procTotal ) { // The last child has been created
			parentProcess( &process );
		}
	}
}


void waitForChildren() {
	int status;
	pid_t pid;
	while ( ( pid = wait( &status ) ) != -1 ) {
		// printf( "Process %d has been done with exit code %d\n", pid, status );
		if( WIFSIGNALED( status ) ) fprintf( stderr, "!!! Interrupted by signal %d !!!\n", WTERMSIG( status ) );
	}
}


void fillMessage( Message * msg, const Process* const proc, const MessageType msgType ) {
	switch( msgType ) {
		case STARTED:
			sprintf( msg -> s_payload, log_started_fmt, get_lamport_time(), proc -> localId, getpid(), getppid(), 0 );
			break;
		case CS_REQUEST:
		case CS_REPLY:
		case CS_RELEASE:
			break;
		case DONE:
			sprintf( msg -> s_payload, log_done_fmt, get_lamport_time(), proc -> localId, 0 );
			break;
		default:
			sprintf( msg -> s_payload, "Unsupported type of message\n" );
			break;
	}
	msg -> s_header.s_type = msgType;
	msg -> s_header.s_magic = MESSAGE_MAGIC;
	msg -> s_header.s_local_time = get_lamport_time();
	msg -> s_header.s_payload_len = strlen( msg -> s_payload );
}


void makePipeLog( const int procTotal ) {
	char buf[ 10 ];
	for ( int row = 0; row <= procTotal; row++ ) {
		for ( int col = 0; col <= procTotal; col++ ) {
			sprintf( buf, "| %3d %3d ", Pipes[ row ][ col ][ READ ], Pipes[ row ][ col ][ WRITE ] );
			write( PipesLog, buf, strlen( buf ) );
		}
		write( PipesLog, "|\n", 2 );
	}
}


void makeIPCLog( const char * const buf ) {
	write( STDOUT_FILENO, buf, strlen( buf ) );
	write( EventsLog, buf, strlen( buf ) );
}
