#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <strings.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <sys/mman.h>

static int page_size;
struct allocated_memory
{
	uint64_t address;
	int size;
};

void *client(void *client_ptr)
{
	int socket_desc;
	ssize_t len;
	socklen_t addrlen;
	int port;
	int numberof_pages;

	port = *((int *) client_ptr);

	struct sockaddr_in servaddr;
	char *server_addr = "127.0.0.1";

	socket_desc = socket(AF_INET, SOCK_DGRAM, 0);
	if (socket_desc == -1)
	{
		printf("Could not create socket\n");
	}

	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = INADDR_ANY;
	servaddr.sin_port = htons(port);

	page_size = sysconf(_SC_PAGE_SIZE);
	FILE * file;
	file = fopen("thefirstone", "r");
	if (!file)
	{
		file = fopen("thefirstone", "w");
		fclose(file);
		while(1)
		{
			do 
			{ 
				printf("How many pages would you like to allocate? "); 
				scanf("%d", &numberof_pages); 
			}while (numberof_pages < 0); 

			void *ptr = mmap(NULL, numberof_pages*page_size, PROT_READ|PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

			printf("Start Address of Allocated Memory: %p\n", ptr);
			printf("Page Size: %d\n", page_size);

			struct allocated_memory allmem;
			allmem.address=(uint64_t)ptr;
			allmem.size=numberof_pages*page_size;

			printf("Sending value [0x%lx,%d] on port %d\n", allmem.address,allmem.size, port);

			int rets = sendto(socket_desc, &allmem, sizeof(allmem), 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
			if (rets == -1)
				printf("error in send\n");  
		} 
		close(socket_desc);
	}
	else
	{
		fclose(file);
		remove("thefirstone");
	}
	return NULL;
}

void *server(void *server_ptr)
{
	int socket_desc;
	int isbind;
	int port = *((int *) server_ptr);
	struct sockaddr_in servaddr;
	int serverLen=sizeof(servaddr);

	socket_desc = socket(AF_INET, SOCK_DGRAM, 0);
	if (socket_desc == -1)
	{
		printf("Could not create socket\n");
	}

	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = INADDR_ANY;
	servaddr.sin_port = htons(port);

	isbind = bind(socket_desc, (struct sockaddr *)&servaddr, sizeof(servaddr));

	if(isbind == -1) {
		printf("Could not bind a socket\n");
	}

	struct sockaddr_in clientaddr;
	ssize_t len;
	socklen_t clientaddr_len = sizeof(clientaddr);
	struct allocated_memory allmem;
	while(1)
	{
		if (-1 == (len = recvfrom(socket_desc, &allmem, sizeof(allmem), 0,(struct sockaddr *)&clientaddr, &clientaddr_len)))
		{
			printf("Error in receive\n");
		}
			printf("\nReceived Memory info: 0x%lx\n", allmem.address);

			void *ptr = mmap((void*)allmem.address, allmem.size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS |MAP_FIXED, -1, 0);
			
			printf("Suppose to allocate: %d\n", allmem.size);
			printf("Start Address of Allocated Memory: %p\n", ptr);
	}
	close(socket_desc);
	return NULL;
}



int main(int argc, char const *argv[])
{
	pthread_t client_thread;
	pthread_t server_thread;
	int write, listen;
	if (argc != 3)
	{
		printf("Usage: [Listen on port] [Write to port]\n");
		return 1;
	}

	if (argc == 3)
	{
		char *a,*b;
		listen = strtol(argv[1], &a, 10);
		write = strtol(argv[2], &b, 10);


		printf("Listen on port: [%d], Write to port: [%d] \n", listen, write);


		if(pthread_create(&server_thread, NULL, server, (void *) &listen)) {
			printf("Error creating thread\n");
			return 1;
		}
		if(pthread_create(&client_thread, NULL, client,(void *) &write)) {
			printf("Error creating thread\n");
			return 1;
		}
		if(pthread_join(client_thread, NULL)) {
			printf("Error joining thread\n");
			return 2;
		}
		if(pthread_join(server_thread, NULL)) {
			printf("Error joining thread\n");
			return 2;
		}
	}
	return 0;
}

