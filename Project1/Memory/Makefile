C = gcc             # default is CC = cc
CFLAGS = -g -Wall   # default is CFLAGS = [blank]
CPPFLAGS =          # default is CPPFLAGS = [blank]
LDFLAGS = -lpthread -lvirt # default is LDFLAGS = [blank]

# default compile command: $(CC) $(CFLAGS) $(CPPFLAGS) -c -o <foo>.o <foo>.c

all: Memory_coordinator

Memory_coordinator: Memory_coordinator.c
	$(CC) -o $@ $< $(CFLAGS) $(LDFLAGS)
# default linking command: $(CC) $(LDFLAGS) <foo>.o -o <foo>

clean:
	$(RM) -f *.o Memory_coordinator
