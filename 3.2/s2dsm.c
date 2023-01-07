#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h> 
#include <strings.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <pthread.h>
#include <sys/mman.h>
#include <linux/userfaultfd.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <poll.h>
#include <sys/types.h>
#include <fcntl.h>

#define errExit(msg)    do { perror(msg); exit(EXIT_FAILURE); \
							   } while (0)


void clear(){
	system("clear");
}

struct page_track
{
	uint64_t start_address;
};
struct page_track pages[100];
struct pairing_info
{
	uint64_t start_address;
	int page_size;
	int number_of_pages;
};
struct params {
	int uffd;
	long page_size;
	uint64_t start_address;
};
enum page_status {
	PAGE_INVALID,
	PAGE_MODIFIED
};

int socket_desc_client, socket_desc_server;
char action;
int action_pagenum;
int number_of_pages=0;
int  page_size;
enum page_status *page_status;

void red()
{
	printf("\033[1;31m");
}

void reset()
{
	printf("\033[0m");
}

void *fault_handler_thread(void *arg)
{
	printf("***salam fault***");
	static struct uffd_msg msg;   /* Data read from userfaultfd */
	static int fault_cnt = 0;     /* Number of faults so far handled */
	
	long uffd;                    /* userfaultfd file descriptor */
	char *page1 = NULL;
	struct uffdio_copy uffdio_copy;
	ssize_t nread;

	uffd = (long) arg; 

	if (page1 == NULL) {
		page1 = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (page1 == MAP_FAILED)
			errExit("mmap");
	}

	for (;;) {
		struct pollfd pollfd;
		int nready;
		pollfd.fd = uffd;
		pollfd.events = POLLIN;
		nready = poll(&pollfd, 1, -1);
		if (nready == -1)
			errExit("poll");

		nread = read(uffd, &msg, sizeof(msg));
		if (nread == 0) {
			printf("EOF on userfaultfd!\n");
			exit(EXIT_FAILURE);
		}

		if (nread == -1)
			errExit("read");

		if (msg.event != UFFD_EVENT_PAGEFAULT) {
			fprintf(stderr, "Unexpected event on userfaultfd\n");
			exit(EXIT_FAILURE);
		}

		red();
		printf("\n[%p] PAGEFAULT\n",msg.arg.pagefault.address);
		reset();
		
		uffdio_copy.src = (unsigned long) page1;
		uffdio_copy.dst = (unsigned long) msg.arg.pagefault.address & ~(page_size- 1);
		uffdio_copy.len = page_size;
		uffdio_copy.mode = 0;
		uffdio_copy.copy = 0;

		if (ioctl(uffd, UFFDIO_COPY, &uffdio_copy) == -1)
			errExit("ioctl-UFFDIO_COPY");
	}
}

void keep_track_of_pages(uint64_t address,int count)
{
	for (int i= 0; i < count; i++,address+=page_size)
	{
		pages[i].start_address = address;
	}
}
void userinput(struct pairing_info pinfo)
{
	char *address_from;
	char *address_to;
	char *point;
	char user_message[100] = {0};
	while(1)
	{
		printf("\n> Which command should I run? (r:read, w:write):");
		scanf(" %c", &action);
		getchar();

		printf("\n> For which page? (0-%d, or -1 for all):",number_of_pages - 1);
		scanf("%d", &action_pagenum);
		getchar();

		while ((action_pagenum > (number_of_pages-1))||(action_pagenum<-1))
		{ 
			printf("Invalid page number\n");
			printf("\n> For which page? (0-%d, or -1 for all):",number_of_pages - 1);
			scanf("%d", &action_pagenum);
			getchar();
		}

		if (action_pagenum == -1)
		{
			address_from = (char *)pinfo.start_address;
			address_to = (char *)(pinfo.start_address+(((pinfo.page_size) * (pinfo.number_of_pages))-1));
		}
		else
		{
			address_from = (char *)(pinfo.start_address +(action_pagenum * pinfo.page_size));
			address_to = (char *)(address_from+(pinfo.page_size-1));
		}

		switch(action)
		{
		case 'r': case 'R':

			if (action_pagenum == -1)
			{
				for(int i=0; i<number_of_pages;i++)
				{
					point =  (char*)pages[i].start_address;
					if (*point == (int)0)
					{
						printf("[*] Page %i \nNo Data\n", i);
					}
					else
					{
						printf("[*] Page %i\n %s\n",i,point);
					}
				}
			}
			else if (action_pagenum < number_of_pages)
			{
				point =  (char*)pages[action_pagenum].start_address;
				if (*point == (int)0)
				{
				printf("[*] Page %i\nNo Data\n",action_pagenum);
				}
				else {
					printf("[*] Page %i \n %s\n",action_pagenum, point);
				}
			}
			break;
		case 'w': case 'W':
			printf("\n> Type your new message:");
			fgets(user_message, 100, stdin);
			/*while(address_from < address_to) 
			{
				memcpy(address_from,user_message,strlen(user_message));
				address_from[strlen(user_message)] = '\n' ;
				address_from += pinfo.page_size;
			}*/

			 if (action_pagenum == -1)
			{
				for(int i=0; i<number_of_pages;i++)
				{
					memcpy(pages[i].start_address,user_message,strlen(user_message));
				}
			 }
			 else if(action_pagenum < number_of_pages)
			 {
				 memcpy(pages[action_pagenum].start_address,user_message,strlen(user_message));
			 }

			break;
		default:
			printf("\n**Invalid Command**\n");
			break;
			}
		}
	}
	int client(int port)
	{
		int socket_desc;
		int isconnected;
		socklen_t addrlen;
		int read_len;
		int numberof_pages;
		struct pairing_info pinfo_received;

		struct sockaddr_in servaddr;
		char *server_addr = "127.0.0.1";

		long uffd; 
		pthread_t thr; 
		struct uffdio_api uffdio_api;
		struct uffdio_register uffdio_register;
		struct params param;
		int s;

		socket_desc = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (socket_desc == -1)
		{
			printf("Could not create socket\n");
			close(socket_desc);
			return -1;
		}

		bzero(&servaddr, sizeof(servaddr));
		servaddr.sin_family = AF_INET;
		servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
		servaddr.sin_port = htons(port);
		printf("Connecting to port %d\n",port);

		isconnected = connect(socket_desc, (struct sockaddr *)&servaddr, sizeof(servaddr));
		if (isconnected == -1)
		{
			printf("Could not connect to server on port %d\n",port);
			close(socket_desc);
			return -1;
		}

		page_size = sysconf(_SC_PAGE_SIZE);
		printf("***Waiting for pairing***");

		read_len = read(socket_desc, &pinfo_received, sizeof(pinfo_received));
		if (read_len == -1)
		{
			printf("Could not read data from socket\n");
			close(socket_desc);
			return -1;
		}
		int len = (page_size * pinfo_received.number_of_pages);
		number_of_pages = pinfo_received.number_of_pages;
		printf("\nReceived Memory info, start address %p and length %d\n", (void *)pinfo_received.start_address, len);

		void *ptr = mmap((void*)pinfo_received.start_address, len, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS |MAP_FIXED, -1, 0);

		if(ptr == MAP_FAILED)
		{
			printf("Mmap failed to allocate memory\n");
			close(socket_desc);
			return -1;
		}
		else
		{
			printf("Pairing with server at address %p with length %d\n", ptr, len);
			keep_track_of_pages((uint64_t)ptr,number_of_pages);
			uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);

			if (uffd == -1)
				printf("userfaultfd\n");

			uffdio_api.api = UFFD_API;
			uffdio_api.features = 0;
			if (ioctl(uffd, UFFDIO_API, &uffdio_api) == -1)
				printf("ioctl-UFFDIO_API\n");


			uffdio_register.range.start = (unsigned long)pinfo_received.start_address;
			uffdio_register.range.len = len;
			uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING;
			if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) == -1)
				printf("ioctl-UFFDIO_REGISTER\n");

			pthread_create(&thr, NULL, fault_handler_thread, (void *) uffd);
			sleep(1);
			userinput(pinfo_received);
			//pthread_join(thr, NULL);
		}
		//close(socket_desc);

		socket_desc_client=socket_desc;
		return 1;
	}

	int server(int port)
	{
		int socket_desc;
		int isbinded, islistened, isestablished, write_ret;
		struct sockaddr_in servaddr;
		int serverLen=sizeof(servaddr);

		struct pairing_info pinfo_write;
		struct params param;

		long uffd; 
		pthread_t thr; 
		struct uffdio_api uffdio_api;
		struct uffdio_register uffdio_register;
		int s;


		socket_desc = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (socket_desc == -1)
		{
			printf("Could not create socket\n");
			close(socket_desc);
			return -1;
		}

		bzero(&servaddr, sizeof(servaddr));
		servaddr.sin_family = AF_INET;
		servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
		servaddr.sin_port = htons(port);

		isbinded = bind(socket_desc, (struct sockaddr *)&servaddr, sizeof(servaddr));
		if(isbinded == -1)
		{
			printf("Could not bind a socket\n");
			close(socket_desc);
			return -1;
		}

		islistened = listen(socket_desc, 16);
		if (islistened == -1)
		{
			printf("Could not listen on the port\n");
			close(socket_desc);
			return -1;
		}

		printf("Waiting for a client connections\n");

		isestablished = accept(socket_desc, NULL, NULL);
		if (isestablished == -1) {
			printf("Server accept failed");
			close(socket_desc);
			return -1;
		}
		printf("Connection is established with client\n");

		do 
		{ 
			printf("How many pages would you like to allocate? "); 
			scanf("%d", &number_of_pages); 
		}while(number_of_pages < 0); 

		page_size = sysconf(_SC_PAGE_SIZE);
		int len = page_size * number_of_pages;
		void *ptr = mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

		printf("Start Address of Allocated Memory: %p\n", ptr);
		printf("Page Size: %d\n", page_size);

		pinfo_write.start_address = (uint64_t)ptr;
		pinfo_write.page_size = page_size;
		pinfo_write.number_of_pages = number_of_pages;

		keep_track_of_pages((uint64_t)ptr,number_of_pages);

		write_ret = write(isestablished, &pinfo_write , sizeof(pinfo_write));
		if (write_ret < 0) {
			printf("Could not write");
			close(socket_desc);
			return -1;
		}
		else
		{
			uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);

			if (uffd == -1)
				printf("userfaultfd\n");

			uffdio_api.api = UFFD_API;
			uffdio_api.features = 0;
			if (ioctl(uffd, UFFDIO_API, &uffdio_api) == -1)
				printf("ioctl-UFFDIO_API\n");

			uffdio_register.range.start = (unsigned long) ptr;
			uffdio_register.range.len = len;
			uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING;
			if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) == -1)
				printf("ioctl-UFFDIO_REGISTER\n");


			pthread_create(&thr, NULL, fault_handler_thread,  (void *) uffd);
			sleep(1);
			//pthread_join(thr, NULL);
			userinput(pinfo_write);
		}




		socket_desc_server = isestablished;
		close(socket_desc);
		return 1;
	}



	int main(int argc, char const *argv[])
	{
		int write, listen;
		int thefirstone;
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
			thefirstone = client(write);

			if (thefirstone < 0)
			{
				thefirstone = server(listen);
			}
		}

		return 0;
	}

