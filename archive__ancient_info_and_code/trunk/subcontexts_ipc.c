
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <memory.h>
#include <sys/mman.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ucontext.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "subcontexts.h"
#include "subcontexts_registry.h"

#include "russ_common_code/c/russ_todo.h"
#include "russ_common_code/c/russ_int_typedefs_linux.h"
#include "russ_common_code/c/russ_locks.h"
#include "russ_common_code/c/russ_dumplocalmaps.h"
#include "russ_common_code/c/russ_dumplocalfilelist.h"



struct sc_ipc_buf;
typedef struct sc_ipc_sockState
{
	struct sc_ipc_sockState *nextSock;
	int sockNo;

	int type;
#define SC_IPC_NEW    1
#define SC_IPC_BIND   2
#define SC_IPC_LISTEN 3
#define SC_IPC_ACCEPT 4
#define SC_IPC_OPEN   5

	union {
	  struct {
		/* by my experiments, the sizeof(addr.sun_path) for a
		 * sockaddr_un is 108 bytes.  This is intentionally excessive,
		 * but we'll also confirm that fact in the dynamic code.
		 */
		char addr[256];
	  } bind;

	  struct {
		/* so far, we don't need any variables for this state */
	  } listen;

	  struct {
		/* when a new connection comes in, a pointer to that socket
		 * is placed here; the accept()ing thread uses futexes to wait
		 * for this value to change.
		 *
		 * If this value is already non-NULL when a new connection
		 * arrives, then the second blocked thread waits on the
		 * value to change (just like an accept()ing thread would);
		 * when the server finally gets around to accept()ing the
		 * first connection, the second waiter will post his own socket
		 * here.  (Arbitrarily many threads could be blocked in this
		 * fashion, of course.)
		 */
TODO: implement the logic above
		struct sc_ipc_sockState *pending_connection;
	  } accept;

	  struct {
		/* this is the other end of a connected socket */
		struct sc_ipc_sockState *sibling;
	  } open;
	};

	struct sc_ipc_buf *recv_buffer;
} sc_ipc_sockState;

typedef struct sc_ipc_buf
{
	/* these two fields are established when the buffer is allocated, and
	 * never change.
	 */
	char *bufBase;
	int   bufLen;

	/* this field reflects the current state; it is initialized to zero,
	 * and gradually increases until it equals bufLen (at which point this
	 * buffer is freed)
	 */
	int bufPos;

	struct sc_ipc_buf *next;
} sc_ipc_buf;


int               nextSockNo        = 1;
sc_ipc_sockState *activeSocket_list = NULL;



/* primary functions, exposed through the subcontext registry */
static int sc_ipc_socket (int domain, int type, int protocol);
static int sc_ipc_bind   (int socket, const struct sockaddr *addr, socklen_t addrlen);
static int sc_ipc_listen (int socket, int backlog);
static int sc_ipc_accept (int socket,       struct sockaddr *address, socklen_t *addrlen);
static int sc_ipc_connect(int socket, const struct sockaddr *address, socklen_t  addrlen);
static int sc_ipc_send   (int socket, const void *buffer, size_t length, int flags);
static int sc_ipc_recv   (int socket,       void *buffer, size_t length, int flags);



/* helper functions used in this file */
static sc_ipc_sockState *findSockState(int socket);



/* this is the main() function, called by subcontexts init after the subcontext
 * has been initialized
 */
int subcontext_main(sc *mySc, int argc, char **argv)
{
	int rc;
	int i;


	sc_registry_funcs *registry_funcs = subcontexts_registry_bootstrap();
	if (registry_funcs == NULL)
	{
		fprintf(stderr, "IPC subcontext: init failed: Could not connect to the subcontexts registry.\n");
		return 2;
	}

	printf("registry_funcs=%p\n", registry_funcs);
	fflush(NULL);

	printf("registry_funcs->post=%p\n", registry_funcs->post);
	printf("registry_funcs->find=%p\n", registry_funcs->find);
	fflush(NULL);

	if (registry_funcs->post("ipc:sc_ipc_socket(int,int,int)", &sc_ipc_socket) != 0)
	{
TODO();
		
	}

	if (registry_funcs->post("ipc:sc_ipc_bind(int,const struct sockaddr*,socklen_t)", &sc_ipc_bind) != 0)
	{
TODO();
	}

	if (registry_funcs->post("ipc:sc_ipc_listen(int,int)", &sc_ipc_listen) != 0)
	{
TODO();
	}

	if (registry_funcs->post("ipc:sc_ipc_accept(int,struct sockaddr*,socklen_t*)", &sc_ipc_listen) != 0)
	{
TODO();
	}

	if (registry_funcs->post("ipc:sc_ipc_connect(int,const struct sockaddr*,socklen_t)", &sc_ipc_listen) != 0)
	{
TODO();
	}

	if (registry_funcs->post("ipc:sc_ipc_send(int,const void*,size_t,int)", &sc_ipc_send) != 0)
	{
TODO();
	}

	if (registry_funcs->post("ipc:sc_ipc_recv(int,void*,size_t,int)", &sc_ipc_recv) != 0)
	{
TODO();
	}


	printf("Entering the infinite spin loop...\n");
	while (1)
		sleep(1);

	/* we never get here */
	assert(false);
}



int sc_ipc_socket(int domain, int type, int protocol)
{

// TODO: locks

	sc_ipc_sockState *sock = sc_malloc(getCurSc(), sizeof(*sock));
	if (sock == NULL)
		return -1;

	memset(sock, 0, sizeof(*sock));

	sock->nextSock = activeSocket_list;
	activeSocket_list = sock;

	sock->sockNo = nextSockNo;
	nextSockNo++;

	sock->type = SC_IPC_NEW;

	return sock->sockNo;
}


int sc_ipc_bind(int socket, const struct sockaddr *addr, socklen_t addrlen)
{

// TODO: locks

	sc_ipc_sockState *sockState = findSockState(socket);
	if (sockState == NULL)
		return -1;
	if (sockState->type != SC_IPC_NEW)
		return -1;

	struct sockaddr_un *addr_un;
	if (addrlen != sizeof(*addr_un))
		return -1;
	addr_un = (struct sockaddr_un*)addr;

	int name_len = strnlen(addr_un->sun_path, sizeof(addr_un->sun_path));
	if (name_len == sizeof(addr_un->sun_path))
		return -1;   // overflow of the buffer!
	if (name_len >= sizeof(sockState->bind.addr))
		return -1;   // name fits in sun_path, but that is too large
                             // for our buffer.

	/* someday, we'll probably post the socket into the real host
	 * filesystem...but for now, this is a purely virtual, purely private
	 * namespace.  And maybe, in the meantime, we'll eventually add
	 * duplicate-name detection.  But we don't do it here.
	 */
	sockState->type = SC_IPC_BIND;
	strcpy(sockState->bind.addr, addr_un->sun_path);

	return 0;
}


int sc_ipc_listen(int socket, int backlog)
{

// TODO: locks

	sc_ipc_sockState *sockState = findSockState(socket);
	if (sockState == NULL)
		return -1;
	if (sockState->type != SC_IPC_BIND)
		return -1;

	/* we ignore the 'backlog' argument for now.  Will we change this
	 * design later?  I don't know.
	 */

	sockState->type = SC_IPC_LISTEN;

	return 0;
}


int sc_ipc_accept (int socket, struct sockaddr *address, socklen_t *addrlen)
{
	if (address != NULL || addrlen != NULL)
	{
		fprintf(stderr, "%s(): This function does not implement accept-address-restriction.\n", __func__);
		return -1;
	}


// TODO: locks

TODO();
}


int sc_ipc_connect(int socket, const struct sockaddr *address, socklen_t addrlen)
{

// TODO: locks

TODO();
}


int sc_ipc_send(int socket, const void *buffer, size_t length, int flags)
{

// TODO: locks

TODO();
}


int sc_ipc_recv(int socket, void *buffer, size_t length, int flags)
{

// TODO: locks

TODO();
}



static sc_ipc_sockState *findSockState(int socket)
{

// TODO: locks

	sc_ipc_sockState *cur;
	for (cur  = activeSocket_list; cur != NULL; cur  = cur->nextSock)
		if (cur->sockNo == socket)
			return cur;
	return NULL;
}


