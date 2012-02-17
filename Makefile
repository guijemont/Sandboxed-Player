CFLAGS += -Wall
CFLAGS += `pkg-config --cflags gstreamer-0.10`
LDFLAGS += `pkg-config --libs gstreamer-0.10`

all: decoder safeplayer

decoder: decoder.o libsandbox.o
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@

safeplayer: safeplayer.o
	$(CC) $(CFLAGS) $^ $(LDFLAGS) -o $@
