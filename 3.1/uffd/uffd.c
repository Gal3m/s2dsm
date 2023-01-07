/* userfaultfd_demo.c

   Licensed under the GNU General Public License version 2 or later.
*/
#define _GNU_SOURCE
#include <sys/types.h>
#include <stdio.h>
#include <linux/userfaultfd.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <poll.h>

#define errExit(msg)    do { perror(msg); exit(EXIT_FAILURE);	\
	} while (0)

static int page_size;

static void *
fault_handler_thread(void *arg)
{
	
	/*   
struct uffd_msg {
               __u8  event;            // Type of event 
               ...
               union {
                   struct {
                       __u64 flags;    // Flags describing fault 
                       __u64 address;  // Faulting address 
                   } pagefault;
                   struct {            // Since Linux 4.11 
                       __u32 ufd;      // Userfault file descriptor
                                        //  of the child process 
                   } fork;

                   struct {            // Since Linux 4.11 
                       __u64 from;     // Old address of remapped area 
                       __u64 to;       // New address of remapped area 
                       __u64 len;      // Original mapping length 
                   } remap;

                   struct {            // Since Linux 4.11 
                       __u64 start;    // Start address of removed area 
                       __u64 end;      // End address of removed area 
                   } remove;
                   ...
               } arg;

              // Padding fields omitted 
           } __packed;
		   */
		   
	static struct uffd_msg msg;   /* Data read from userfaultfd */
	static int fault_cnt = 0;     /* Number of faults so far handled */
	long uffd;                    /* userfaultfd file descriptor */
	static char *page = NULL;
	struct uffdio_copy uffdio_copy;
	ssize_t nread;

	uffd = (long) arg;

	/* [H1]
	The program has two thead. This is the second thread which  handles page-fault events.
	 In the bellowing, it creates a page with size 4096 that will be copied into the faulting region.
	 
	 NOTE: I described mmp and its structure in main section.
	 
	 On success, mmap() returns a pointer to the mapped area and copy it to page parameter.
	 On error, the value MAP_FAILED (that is, (void *) -1) is returned, and the line : errExit("mmap") would be exceuted. 
	 */
	if (page == NULL) {
		page = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
			    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (page == MAP_FAILED)
			errExit("mmap");
	}

	/* [H2]
	
	The first line is an infinite loop which wnats to handle incoming events on the userfaultfd file descriptor.
	In each loop iteration, the second thread first calls poll() to check the state of the file descriptor, and then reads an event from the file descriptor.
	
	 */
	for (;;) {

		/* 
		struct pollfd {
               int   fd;   //  file descriptor for an open file
               short events;  // requested events -a bit mask specifying the events the application is interested in for the file descriptorfd
               short revents;    //returned events -is an output parameter, filled by the kernel with the events that actually occurred.
           };
		   */
		struct pollfd pollfd;
		int nready;

		/* [H3]
		poll function :wait for some event on a file descriptor. It means it waits for one of a set of file descriptors to become ready to perform I/O.
		Its structure:
		int poll(struct pollfd *fds, nfds_t nfds, int timeout);
		 fd: contains a file descriptor for an open file  ---> here is userfaultfd which is the file descriptor.
		 nfds: The caller should specify the number of items in the fds array in nfds.
		 timeout:  The timeout argument specifies the number of milliseconds that poll() 
		 should block waiting for a file descriptor to become ready.--> here is -1 that shows an infinite timeout
		 
		 
		 On success, poll() returns a nonnegative value which is the
       number of elements in the pollfds whose revents fields have been
       set to a nonzero value (indicating an event or an error).  A
       return value of zero indicates that the system call timed out
       before any file descriptors became read.
	   So, two prinf lines would be executed. likes this line:
	    poll() returns: nready = 1; POLLIN = 1; POLLERR = 0
		
		
       On error, -1 is returned, and errno is set to indicate the cause of the error.
	   So, if error occurres, nready would be -1 and errExit("poll") excute and then exit.
		 */
		
		pollfd.fd = uffd;
		pollfd.events = POLLIN; // menas there is data to read
		nready = poll(&pollfd, 1, -1);
		if (nready == -1)
			errExit("poll");

		printf("\nfault_handler_thread():\n");
		printf("    poll() returns: nready = %d; "
                       "POLLIN = %d; POLLERR = %d\n", nready,
                       (pollfd.revents & POLLIN) != 0,
                       (pollfd.revents & POLLERR) != 0);

		/* [H4]
		       ssize_t read(int fd, void *buf, size_t count);
		Read(): attempts to read up to count bytes (sizeof(msg)) from file descriptor fd (uffd)into the buffer starting at buf (msg).
		 
		SO ,  an event is read from the userfaultfd.
		
		 On success, the number of bytes read is returned
		 
		 On error, -1 is returned. and this line would be exceuted:
		 errExit("read");
		 
		 a read() with a count of 0 returns zero and has no other effects. and these two lines would be executed:
		  printf("EOF on userfaultfd!\n");
			exit(EXIT_FAILURE);
			
		 
		 */
		nread = read(uffd, &msg, sizeof(msg));
		if (nread == 0) {
			printf("EOF on userfaultfd!\n");
			exit(EXIT_FAILURE);
		}

		if (nread == -1)
			errExit("read");

		/* [H5]
		 if event type would not be a page-fault event,
		 fprintf would print the message of Unexpected event on userfaultfd and exit.
		 */
		if (msg.event != UFFD_EVENT_PAGEFAULT) {
			fprintf(stderr, "Unexpected event on userfaultfd\n");
			exit(EXIT_FAILURE);
		}

		/* [H6]
		 if event type would  be a page-fault event,
		 these fllowing lines would be exceuted.
		 //pagefault.flags
              A bit mask of flags that describe the event.
			  
		 //pagefault.address
              The address that triggered the page fault.
			  
		 The output would be like this:
		   UFFD_EVENT_PAGEFAULT event: flags = 0; address = 7f077fb94000

		 
		 */
		printf("    UFFD_EVENT_PAGEFAULT event: ");
		printf("flags = %llx; ", msg.arg.pagefault.flags);
		printf("address = %llx\n", msg.arg.pagefault.address);

		/* [H7]
		
		void *memset(void *str, int c, size_t n)
		memset(): copies the character c (an unsigned char) to the first n characters of the string pointed to, by the argument str.
		
		Note: The c is passed as an int, but the function fills the block of memory using the unsigned char conversion of c.
		
		In the following line, at first, the first line with using memset copy the page pointed to by 'page' into the faulting region. 
		Vary the contents that are copied in with using 'fault_cnt % 20' and plusing it each time with 'fault_cnt++',
		so that it is more obvious that each fault is handled separately.
		
				  
		 */
		memset(page, 'A' + fault_cnt % 20, page_size);
		fault_cnt++;

		/* [H8]
		
		In the following lines the program wants to  handle page faults in units of pages:
		
		uffdio_copy:
		struct uffdio_copy {
               __u64 dst;    // Destination of copy 
               __u64 src;    // Source of copy 
               __u64 len;    // Number of bytes to copy 
               __u64 mode;   // Flags controlling behavior of copy 
               __s64 copy;   // Number of bytes copied, or negated error 
           };
	   PAGE_SIZE is 4096, then in 32-bit binary:
		rounding the address to the nearest 4096-byte page address
         PAGE_SIZE      = 00000000000000000001000000000000b
         PAGE_SIZE - 1  = 00000000000000000000111111111111b
        ~(PAGE_SIZE - 1) = 11111111111111111111000000000000b
		
		So,the expression " msg.arg.pagefault.address & ~(page_size - 1) " 
		will turn lower bits of address into zeros, rounding the address to the nearest 4096-byte page address.
		 
		 */
		uffdio_copy.src = (unsigned long) page;
		uffdio_copy.dst = (unsigned long) msg.arg.pagefault.address &
			~(page_size - 1);
		uffdio_copy.len = page_size;
		uffdio_copy.mode = 0;
		uffdio_copy.copy = 0;

		/* [H9]
			In the the bellowing line, atomically copy a continuous memory chunk into the userfault 
			registered range (through above lines) and optionally wake up the blocked thread 
			(unless uffdio_copy.mode & UFFDIO_COPY_MODE_DONTWAKE is set).. 
		on error,errExit("ioctl-UFFDIO_COPY" would excut and exit.
		 */
		if (ioctl(uffd, UFFDIO_COPY, &uffdio_copy) == -1)
			errExit("ioctl-UFFDIO_COPY");

		/* [H10]
		on success, the following line is printed which shows the number of bytes copied (4096 : page_
		size)
		 */
		printf("        (uffdio_copy.copy returned %lld)\n",
                       uffdio_copy.copy);
	}
}

int main(int argc, char *argv[])
{
	long uffd;          /* userfaultfd file descriptor */
	char *addr;         /* Start of region handled by userfaultfd */
	unsigned long len;  /* Length of region handled by userfaultfd */
	pthread_t thr;      /* ID of thread that handles page faults */
	struct uffdio_api uffdio_api; //Enable operation of the userfaultfd and perform API handshake.
	/*
	a uffdio_api structure, defined
       as:

           struct uffdio_api {
               __u64 api;      //  Requested API version (input) 
               __u64 features; //   Requested features (input/output) 
               __u64 ioctls;   //   Available ioctl() operations (output) 
           };

	*/
	struct uffdio_register uffdio_register; //Register a memory address range with the userfaultfd object. 
	int s;
	int l;

	/* [M1]
	 * argc is the count of arguments, and argv is an array of the strings.
	 The program itself is the first argument(argv[0]) and argc is always at least 1. 
	 
	 The program takes one command-line argument, which is the number of pages that will 
	 be created in a mapping whose page faults will be handled via userfaultfd. 
	 
	 The line '(argc != 2 )' checks whether the program is run with one command-line argument or not.
	 of not, progtma would print the message "Usage: ./uffd num-pages" and then exit.
	 */
	if (argc != 2) {
		fprintf(stderr, "Usage: %s num-pages\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	/* [M2]
	sysconf - get configuration information at run time. And sysconf(_SC_PAGE_SIZE) is  equivalent to getpagesize() which gets the current page size.
	strtoul function converts the initial part of  argv[1] in str to an unsigned long int value according to the given base which is zero in this program. 
	the value page_size is  system's default page size which 4096. (you can find its value with this command (getconf PAGE_SIZE)
	
	Finally, based on the input as argumnt for num-pages the value of len would be determind. 
	for example for input= 1: len would 4096 which is Virtual Memory PAGESIZE
	for input =2  len would be 8192 --> Virtual Memory PAGESIZE * 2 ....
	So, it determines the length of region handled by userfaultfd
	
	*/
	page_size = sysconf(_SC_PAGE_SIZE);
	len = strtoul(argv[1], NULL, 0) * page_size;
	
	
	/* [M3]
	 * At the first line, userfaultfd object is created and enabled. 
	
	syscall calls userfaultfd system call, because Glibc does not provide a wrapper for this system call
	userfaultfd: it creates a file descriptor for handling page faults in user space (stored in uffd).
	
	O_CLOEXEC flag:  Enables the close-on-exec flag for the new file descriptor. It means that if a new program would be run with exec(), 
	it wouldn't has the uffd resource. It often use in multi-threaded programs for avoiding a race.
	
	O_NONBLOCK:  Enables non-blocking operation for the userfaultfd object.
	( Neither the open() nor any subsequent I/O operations on the file descriptor which is returned will cause the calling process to wait.)
	When the O_NONBLOCK flag is enabled in the associated open file description, 
	the userfaultfd file descriptor can be monitored with poll), select(), and epoll(). 
	   
	
	O_NONBLOCK | O_NONBLOCK: bitwise ORed of these two flag
	userfaultfd: On error, -1 is returned, so errExit("userfaultfd") would be exceuted.
	
	errExit description: it is analogous to that between _exit() and exit(): unlike errExit(), this
   function does not flush stdout and calls _exit(2) to terminate the process 
   (rather than exit(), which would cause exit handlers to be invoked).
   These differences make this function especially useful in a library
   function that creates a child process that must then terminate
   because of an error: the child must terminate without flushing
   stdio buffers that were partially filled by the caller and without
   invoking exit handlers that were established by the caller.
	 */
	 
	 
	uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
	if (uffd == -1)
		errExit("userfaultfd");

	/* [M4]

	 uffdio_api.api = UFFD_AP -->  The api field denotes the API version requested by the application. So, it specify the read/POLLIN protocol userland intends to speak on the UFFD and the uffdio_api.features userland requires
	 
	 uffdio_api.features = 0 --> For Linux kernel versions before 4.11, the features field must be
      initialized to zero before the call to UFFDIO_API, and zero (i.e., no feature bits) 
	  is placed in the features field by the kernel upon return from ioctl().
	   
	   ioctl: create a file descriptor for handling page faults in user space. 
	   Its structure: 
	    -int ioctl(int fd, int cmd, *argp)
		*fd is a file descriptor referring to a userfaultfd object --> here referring to the  file descriptor of userfaultfd.
		* cmd is one of the commands listed below --> here is UFFDIO_API which enables operation of the userfaultfd and perform API handshake.
		* argp is argument is a pointer to a uffdio_api structure --> here points to uffdio_api.
		
		if ioctl encounters with an error and can not create a file descriptor returns -1 and the line errExit("ioctl-UFFDIO_API") would be excuted.
		
		
	 */
	uffdio_api.api = UFFD_API;
	uffdio_api.features = 0;
	if (ioctl(uffd, UFFDIO_API, &uffdio_api) == -1)
		errExit("ioctl-UFFDIO_API");

	/* [M5]
	 mmap: map or unmap files or devices into memory
	 it means it creates a new mapping in the virtual address space of the calling process. 
	 -------------------------------------------
	 its structure:
	 void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
	 -addr: the starting address for the new mapping
	 -length:argument specifies the length of the mapping
	 -prot: escribes the desired memory protection of the mapping, It is either PROT_NONE or the bitwise OR of one or more flags
	 -flags: determines whether updates to the mapping are visible to other processes mapping the same region 
	 and whether updates are carried through to the underlying file
	 fd: the file descriptor--> the contents of a file mapping referred to by the file descriptor fd.
	 offset: offset must be a multiple of the page size as returned by sysconf(_SC_PAGE_SIZE).
	 --------------------------------------------

	 argumnets: 
	 Null: it is the best way to create the mapping more portable
	 len: it determines the length of region handled by userfaultfd which is used for specifying the length of the mapping
	 PROT_READ| PROT_WRITE: Pages may be read or write
	 MAP_PRIVATE:Create a private copy-on-write mapping.
	 MAP_ANONYMOUS: The mapping is not backed by any file; its contents are initialized to zero.	
	 fd: If MAP_ANONYMOUS is specified, the implementation requires fd to be -1 
	 offset: If MAP_ANONYMOUS is specified, the offset argument should be zero.
	 
	 
	 So, mmp creates a private anonymous mapping. The memory will be demand-zero paged--that is, not yet allocated. 
	It will be allocated via the userfaultfd.
	
	On success, mmap() returns a pointer to the mapped area and the the pointer address would be printed for ex: 0x7fab741f4000
	 On error, the value MAP_FAILED (that is, (void *) -1) is returned, and the line : errExit("mmap") would be exceuted. 


//Currently only MAP_PRIVATE | MAP_ANONYMOUS is supported. 
//Newer kernels (4.11+) allow userfaultfd for hugetlbfs and shared memory.	 
	 */
	addr = mmap(NULL, len, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (addr == MAP_FAILED)
		errExit("mmap");

	printf("Address returned by mmap() = %p\n", addr);



	/* [M6]
	 the application should register memory address ranges using the UFFDIO_REGISTER
     
	So, the bellowing lines register the memory range of the mapping that it has just created for
     handling by the userfaultfd object.
	 
	 for mode: UFFDIO_REGISTER_MODE_MISSING--->  Track page faults on missing pages.
	 So	with setting the mode, it is requested to track missing pages (i.e., pages that have not yet been faulted in)		  
		
	for calling ioctl, the third parameter is a pointer to a uffdio_register structure,
       defined as:

           struct uffdio_range {
               __u64 start;    //Start of range 
               __u64 len;      // Length of range (bytes) 
           };

           struct uffdio_register {
               struct uffdio_range range;
               __u64 mode;     // Desired mode of operation (input) 
               __u64 ioctls;   // Available ioctl() operations (output)
           };
		   
		if ioctl encounters with an error and can not create a file descriptor returns -1 and the line errExit("ioctl-UFFDIO_REGISTER") would be excuted.   
	 */
	uffdio_register.range.start = (unsigned long) addr;
	uffdio_register.range.len = len;
	uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING;
	if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) == -1)
		errExit("ioctl-UFFDIO_REGISTER");



	/* [M7]
	 In the following lines, a thread is created to process the userfaultfd events
	 
	 pthread_create - create a new thread in the calling process
	 Its structure:
	 int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*start_routine) (void *), void *arg);
						  
		thread: Before returning, a successful call to pthread_create() stores
       the ID of the new thread in the buffer pointed to by thread; // thr finally has the ID of the new thread
	   
	    attr: it points to a pthread_attr_t structure whose contents are used at
		thread creation time to determine attributes for the new thread
		
	    If attr is NULL (same as here) ,then the thread is created with default attributes.
	   
	   start_routine: The new thread starts execution by invoking
       start_routine();  // here, fault_handler_thread is called
	   arg is passed as the sole argument of start_routine(). // uffd is the sole argument of fault_handler_thread
	   
	   On success, pthread_create() returns 0;
	   on error, it returns an error number, and the contents of *thread are undefined.
	   
	   So if the execution ecounters with an error at first s is set to errno and errExit("pthread_create") would be exceuted.
	 */
	s = pthread_create(&thr, NULL, fault_handler_thread, (void *) uffd);
	if (s != 0) {
		errno = s;
		errExit("pthread_create");
	}



	/*
	 * [U1]
	 
	 
	   
	The main thread then walks through the pages of the mapping
    fetching bytes from successive pages.  Because the pages have not
    yet been accessed, the first access of a byte in each page will
    trigger a page-fault event on the userfaultfd file descriptor.
	So, in the following lines, main thread touches memory in the 
	ranges of (0-4096), touching locations 1024 bytes apart. 
	Each of the page-fault events is handled by the second thread
	 For ex:
	 The output:
	#1. Read address 0x7f0787b2c400 in main(): A
	#1. Read address 0x7f0787b2c800 in main(): A
	So we see that every 1024 byte is read, because of "l += 1024".
	This will trigger userfaultfd events for all pages in the region. 
	 
	 */
	printf("-----------------------------------------------------\n");
	l = 0x0;
	while (l < len) {
		char c = addr[l];
		printf("#1. Read address %p in main(): ", addr + l);
		printf("%c\n", c);
		l += 1024;
	}

	/*
	 * [U2]
	 In the following lines the same memory access would be happen but page faults do not happen in this time.
	 Becuase the pages have been loaded into memory. the outou like this:

	fault_handler_thread():
		poll() returns: nready = 1; POLLIN = 1; POLLERR = 0
		UFFD_EVENT_PAGEFAULT event: flags = 0; address = 7f1a45f56000 //these lines show that the page-fault has occured.
	#1. Read address 0x7f1a45f56000 in main(): A
	#1. Read address 0x7f1a45f56400 in main(): A
	#1. Read address 0x7f1a45f56800 in main(): A
	#1. Read address 0x7f1a45f56c00 in main(): A
	-----------------------------------------------------
	#2. Read address 0x7f1a45f56000 in main(): A
	#2. Read address 0x7f1a45f56400 in main(): A
	#2. Read address 0x7f1a45f56800 in main(): A
	#2. Read address 0x7f1a45f56c00 in main(): A
	-----------------------------------------------------

	 we can see that the line 'poll() returns: nready = 1; POLLIN = 1; POLLERR = 0' which is printed  in the fault_handler_thread doese not printed for section 2.
	 This verify my assertion.
	 */
	printf("-----------------------------------------------------\n");
	l = 0x0;
	while (l < len) {
		char c = addr[l];
		printf("#2. Read address %p in main(): ", addr + l);
		printf("%c\n", c);
		l += 1024;
	}

	/*
	 * [U3]
		int madvise(void *addr, size_t length, int advice);
	 madvise(): This system call is used to give advice or directions to the kernel 
	 about the address range beginning at address addr and with size length bytes.
	 
	 MADV_DONTNEED: Do not expect access in the near future. After a successful MADV_DONTNEED operation,
	 the semantics of memory access in the specified region are changed.
	 as we see in the output again page fault occurs, becuase the page has removed from the memory.
	 

		fault_handler_thread():
			poll() returns: nready = 1; POLLIN = 1; POLLERR = 0
			UFFD_EVENT_PAGEFAULT event: flags = 0; address = 7f1a45f56000
		#3. Read address 0x7f1a45f56000 in main(): B
		#3. Read address 0x7f1a45f56400 in main(): B
		#3. Read address 0x7f1a45f56800 in main(): B
		#3. Read address 0x7f1a45f56c00 in main(): B
		-----------------------------------------------------


	 */
	printf("-----------------------------------------------------\n");
	if (madvise(addr, len, MADV_DONTNEED)) {
		errExit("fail to madvise");
	}
	l = 0x0;
	while (l < len) {
		char c = addr[l];
		printf("#3. Read address %p in main(): ", addr + l);
		printf("%c\n", c);
		l += 1024;
	}

	/*
	 * [U4]
	 But, this time, becuase they have been loaded into memory again ,we see that the page fault doese not occure.
	

		-----------------------------------------------------
		#4. Read address 0x7f1a45f56000 in main(): B
		#4. Read address 0x7f1a45f56400 in main(): B
		#4. Read address 0x7f1a45f56800 in main(): B
		#4. Read address 0x7f1a45f56c00 in main(): B
		-----------------------------------------------------


	 */
	printf("-----------------------------------------------------\n");
	l = 0x0;
	while (l < len) {
		char c = addr[l];
		printf("#4. Read address %p in main(): ", addr + l);
		printf("%c\n", c);
		l += 1024;
	}

	/*
	 * [U5]
	 In this section becuase we call madvise with MADV_DONTNEED option, we see that page fault would occure.

	fault_handler_thread():
		poll() returns: nready = 1; POLLIN = 1; POLLERR = 0
		UFFD_EVENT_PAGEFAULT event: flags = 1; address = 7f1a45f56000
	#5. write address 0x7f1a45f56000 in main(): @
	#5. write address 0x7f1a45f56400 in main(): @
	#5. write address 0x7f1a45f56800 in main(): @
	#5. write address 0x7f1a45f56c00 in main(): @
	-----------------------------------------------------

	with memset function, the program will put @ character into the first 1024 bytes of the block of memory pointed by addr+l.
	At the first time, l=0 and each time pluse with 1024 until to smaller that 4096.
	
	 */
	printf("-----------------------------------------------------\n");
	if (madvise(addr, len, MADV_DONTNEED)) {
		errExit("fail to madvise");
	}
	l = 0x0;
	while (l < len) {
		memset(addr+l, '@', 1024);
		printf("#5. write address %p in main(): ", addr + l);
		printf("%c\n", addr[l]);
		l += 1024;
	}

	/*
	 * [U6]
	Then, again these pages would be access, but this time they have been load and changed their content, so page fault does not occure.
	-----------------------------------------------------
	#6. Read address 0x7f1a45f56000 in main(): @
	#6. Read address 0x7f1a45f56400 in main(): @
	#6. Read address 0x7f1a45f56800 in main(): @
	#6. Read address 0x7f1a45f56c00 in main(): @
	-----------------------------------------------------

	 */
	printf("-----------------------------------------------------\n");
	l = 0x0;
	while (l < len) {
		char c = addr[l];
		printf("#6. Read address %p in main(): ", addr + l);
		printf("%c\n", c);
		l += 1024;
	}

	/*
	 * [U7]
	 Then, again these pages would be access for changing its content again, but they exist in the memory and so page fault does not occure.
	 
		-----------------------------------------------------
		#7. write address 0x7f1a45f56000 in main(): ^
		#7. write address 0x7f1a45f56400 in main(): ^
		#7. write address 0x7f1a45f56800 in main(): ^
		#7. write address 0x7f1a45f56c00 in main(): ^
		-----------------------------------------------------

	 */
	printf("-----------------------------------------------------\n");
	l = 0x0;
	while (l < len) {
		memset(addr+l, '^', 1024);
		printf("#7. write address %p in main(): ", addr + l);
		printf("%c\n", addr[l]);
		l += 1024;
	}

	/*
	 * [U8]
	 Then, again these pages would be access for read, but they have been load and changed their content, so page fault does not occure.
	 
		-----------------------------------------------------
		#8. Read address 0x7f1a45f56000 in main(): ^
		#8. Read address 0x7f1a45f56400 in main(): ^
		#8. Read address 0x7f1a45f56800 in main(): ^
		#8. Read address 0x7f1a45f56c00 in main(): ^
		-----------------------------------------------------
		then program would exit successfuly.
	 */
	printf("-----------------------------------------------------\n");
	l = 0x0;
	while (l < len) {
		char c = addr[l];
		printf("#8. Read address %p in main(): ", addr + l);
		printf("%c\n", c);
		l += 1024;
	}

	exit(EXIT_SUCCESS);
}
