
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>

#include "subcontexts.h"
#include "subcontexts_registry.h"

#include "russ_common_code/c/russ_todo.h"



int main(int argc, char **argv)
{
	/* argv sanity check */
	if (argc != 2)
	{
		fprintf(stderr, "SYNTAX: %s <socket_filename>\n", argv[0]);
		return 1;
	}


	/* attach to the subcontexts registry */
	sc_registry_funcs *registry = subcontexts_registry_bootstrap();
	if (registry == NULL)
	{
		fprintf(stderr, "%s: Could not attach to the subcontexts registry.\n", argv[0]);
		return 2;
	}


	/* attach to the IPC subcontext */
	/* TODO: remove this; do it automatically! */
	FILE *ipc_fp = fopen("/tmp/subcontexts_ipc.sc", "r+");
	if (ipc_fp == NULL)
	{
		fprintf(stderr, "%s: Could not open the backing file for the IPC subcontext.\n", argv[0]);
		return 2;
	}

	sc *ipc_sc = sc_join(fileno(ipc_fp));
	if (ipc_sc == NULL)
	{
		fprintf(stderr, "%s: Could not attach to the IPC subcontexts.\n", argv[0]);
		return 2;
	}


	/* attach to the functions of the IPC service */
	int (*socket )(int,int,int);
	int (*bind   )(int,const struct sockaddr*, socklen_t);
	int (*listen )(int,int);
	int (*accept )(int,struct sockaddr*, socklen_t*);
	int (*connect)(int,const struct sockaddr*, socklen_t);
	int (*send   )(int,const void *,size_t,int);
	int (*recv   )(int,      void *,size_t,int);

	socket  = registry->find("ipc:sc_ipc_socket(int,int,int)");
	bind    = registry->find("ipc:sc_ipc_bind(int,const struct sockaddr*,socklen_t)");
	listen  = registry->find("ipc:sc_ipc_listen(int,int)");
	accept  = registry->find("ipc:sc_ipc_accept(int,struct sockaddr*,socklen_t*)");
	connect = registry->find("ipc:sc_ipc_connect(int,const struct sockaddr*,socklen_t)");
	send    = registry->find("ipc:sc_ipc_send(int,const void*,size_t,int)");
	recv    = registry->find("ipc:sc_ipc_recv(int,void*,size_t,int)");

	printf("%s: socket=%p bind=%p listen=%p accept=%p connect=%p send=%p recv=%p\n", argv[0], socket,bind,listen,accept,connect,send,recv);
	fflush(NULL);

	if (socket == NULL || bind   == NULL ||
	    listen == NULL || accept == NULL || connect == NULL ||
	    send   == NULL || recv   == NULL)
	{
		fprintf(stderr, "Could not attach to the IPC service.\n");
		return 3;
	}


	/* create a socket, listen for a connection, and then we're done */
	int serv = socket(AF_UNIX, SOCK_STREAM, SOCK_DGRAM);
	if (serv == -1)
	{
		fprintf(stderr, "%s: Could not create a socket.\n", argv[0]);
		return 3;
	}

	struct sockaddr_un addr;
	socklen_t          addr_len = sizeof(addr);

	if (strlen(argv[1]) >= sizeof(addr.sun_path))
	{
		fprintf(stderr, "ERROR: The length (%d) of the filename '%s' is longer than the max pathname allowed by the sockaddr_un struct, which is %d\n", (int)strlen(argv[1]), argv[1], (int)sizeof(addr.sun_path)-1);
		return 2;
	}

	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, argv[1]);

	if (bind(serv, (struct sockaddr*)&addr, addr_len) != 0)
	{
		fprintf(stderr, "%s: Could not bind() the socket to the filename.\n", argv[0]);
		return 3;
	}

	if (listen(serv, 5) != 0)
	{
		fprintf(stderr, "%s: Could not set the listen() state for the socket.\n", argv[0]);
		return 3;
	}

	int sock = accept(serv, NULL,NULL);
	if (sock < 0)
	{
		fprintf(stderr, "%s: Could not accept() a new connection.\n", argv[0]);
		return 3;
	}


	


TODO();
}


