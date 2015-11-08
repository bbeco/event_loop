#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h> /* memset */
#include <sys/time.h>
#include <sys/select.h>
#include <stdio.h>
#include <stdlib.h>

#define	WANT_READ 1
#define WANT_WRITE 4
#define WANT_DIE 8

#define RECV_BUF_DIM 512
#define SEND_BUF_DIM 512

struct task {
	struct task *next;
	int fd;
	int what_io; //WANT_READ WANT_DIE
	int (*handler)(struct task* t);
	void *private;
};

struct server_data {
	struct task *t_head;
};

typedef enum {READ_MSG, SEND_MSG, DIE}status;

struct client_data {
	status what_next;
	char recv_buf[RECV_BUF_DIM];
	char send_buf[SEND_BUF_DIM];
};

struct task *
new_task()
{
	return (struct task *)calloc(1, sizeof(struct task));
}

/* No need to pass the head of the queue for address, since the first task in 
 * the list is the listening task, always present.
 */
int
event_loop(struct task *t)
{
	int n, ret, max_fd;
	fd_set r, w;
	struct task *c, *next;
	char call_me;
	for (;;) {
		FD_ZERO(&r);
		FD_ZERO(&w);
		max_fd = -1;
		for (c = t; c != NULL; c = c->next) {
			next = c->next;
			if (c->what_io == WANT_READ)
				FD_SET(c->fd, &r);
			if (c->what_io == WANT_WRITE)
				FD_SET(c->fd, &w);
			//removing dying tasks
			if (next) {
				if (next->what_io == WANT_DIE) {
					c->next = next->next;
					free(next);
				}
				next = next->next;
			}
			if (c->what_io != 0 && c->fd > max_fd)
				max_fd = c->fd;
		}

		n = select(max_fd + 1, &r, &w, NULL, NULL);
		for (c = t; c != NULL; c = c->next) {
			call_me = 0;
			if (c->fd <= 0)
				continue;
			if (c->what_io == WANT_READ && FD_ISSET(c->fd, &r))
				call_me = 1;
			if (c->what_io == WANT_WRITE && FD_ISSET(c->fd, &w))
				call_me = 1;
			if (call_me != 0) {
				ret = c->handler(c);
				if (ret < 0) {
					c->what_io = WANT_DIE;
					if (c->private) {
						free(c->private);
					}
					close(c->fd);
					c->fd = 0;
				}
			}
		}
	}
}

int
client_handler(struct task *t)
{
	status *what_next = &((struct client_data *)t->private)->what_next;
	char *buf = ((struct client_data *)t->private)->recv_buf;
	int len = 10; //FIXME select correct size
	int ret;
	switch (*what_next) {
	case READ_MSG:
#ifdef DEBUG
		printf("Incoming message\n");
#endif
		ret = recv(t->fd, buf, len, 0);
		if (ret < 0) {
			fprintf(stderr, "E: recv()\n");
			return 1;
		}
		if (ret == 0)
			return 1;
		buf[ret]= '\0';
		printf("%s", buf);
		*what_next = SEND_MSG;
		t->what_io = WANT_WRITE;
		return 0;
	case SEND_MSG:
		buf = "ciao\n";
		len = strlen(buf);
		ret = send(t->fd, buf, len, 0);
		if (ret < 0) {
			fprintf(stderr, "E: send()\n");
			return 1;
		}
		return -1;
	}
}

int
accept_new_client(struct task *t)
{
	int val, new_fd;
	struct task* c, *last;
	struct server_data *server;
	struct sockaddr_in addr;
	struct client_data *c_data;
	int addr_len = sizeof(addr);
	
	server = (struct server_data *)t->private;
	new_fd = accept(t->fd, (struct sockaddr *)&addr, &addr_len);
#ifdef DEBUG
	printf("Connection attempt\n");
#endif
	if (new_fd >= 0) {
		c = new_task();
		c->fd = new_fd;
		val = fcntl(c->fd, F_GETFL);
		fcntl(c->fd, F_SETFL, val | O_NONBLOCK);
		c->what_io = WANT_READ;
		c->handler = client_handler;
		c->private = (struct client_data *)calloc(1, sizeof(struct client_data));
		c_data = (struct client_data *)c->private;
		c_data->what_next = READ_MSG;
		
		//adding the new client to the list of client
		for (last = t; last->next != NULL; last = last->next);
		last->next = c;
		return 0;
	}
#ifdef DEBUG
	fprintf(stderr, "E: accept()\n");
#endif
	return -1;
}

int
main(int argc, char *argv[])
{
	int ret, val;
	struct task *l = new_task();
	struct server_data *server;
	l->private = calloc(1, sizeof(struct server_data));
	server = (struct server_data *)l->private;
	l->fd = socket(AF_INET, SOCK_STREAM, 0);
	if (l->fd < 0)  {
		perror("E: socket()");
		exit(1);
	}
	val = 1;
	ret = setsockopt(l->fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
	if( ret < 0)
	{
		perror("E: setsockopt()");
		exit(1);
	}
	ret = setsockopt(l->fd, SOL_SOCKET, SO_REUSEPORT, &val, sizeof(val));
	if( ret < 0)
	{
		perror("E: setsockopt()");
		exit(1);
	}
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(1234);
	addr.sin_addr.s_addr = INADDR_ANY;
	ret = bind(l->fd, (struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0) {
		perror("E: bind()");
		exit(1);
	}
	
	ret = listen(l->fd, 100);
	if (ret < 0) {
		perror("E: listen()");
		exit(1);
	}
	
	val = fcntl(l->fd, F_GETFL);
	fcntl(l->fd, F_SETFL, val | O_NONBLOCK);
	
	l->what_io = WANT_READ;
	l->handler = accept_new_client;
	
	//adding the server task to the list of tasks
	server->t_head = l;
	event_loop(server->t_head);
	return 0;
}