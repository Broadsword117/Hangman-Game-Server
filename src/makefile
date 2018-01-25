src = $(server.c)
obj = $(src:.c=.o)

LDFLAGS = -Wall -pthread -std=c99

myprog: $(obj)
	$(CC) -o $@ $^ $(LDFLAGS)

.PHONY: clean
clean:
	rm -f $(obj) myprog
