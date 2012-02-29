CFLAGS += -Wall
CFLAGS += `pkg-config --cflags gstreamer-0.10`
LDFLAGS += `pkg-config --libs gstreamer-0.10`

all: decoder sandboxed-player

decoder: decoder.o libsandbox.o
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

sandboxed-player: sandboxed-player.o
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@
