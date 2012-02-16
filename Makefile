CFLAGS += -Wall
CFLAGS += `pkg-config --cflags gstreamer-0.10`
LDFLAGS += `pkg-config --libs gstreamer-0.10`

decoder: decoder.o libsandbox.o
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@
