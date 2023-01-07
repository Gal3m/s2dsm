
CC = gcc
CFLAGS += -pthread -lrt
FILE =  s2dsm

$(FILE): $(FILE).c
all: $(FILE)
	$(CC) $(CFLAGS) -o $(FILE) $(FILE).c 

clean:
	rm -f  $(FILE)


.PHONY: all clean
