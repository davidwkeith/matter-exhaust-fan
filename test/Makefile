# Host-side unit tests for the portable core logic — no ESP-IDF required.
#   cd test && make

CC ?= cc
CFLAGS = -Wall -Wextra -Werror -std=c11 \
         -I../core/input/include \
         -I../core/dimmer/include \
         -I../core/mua/include

SRCS = test_main.c \
       ../core/input/rocker.c \
       ../core/input/channel.c \
       ../core/input/reset_chord.c \
       ../core/dimmer/dimmer_curve.c \
       ../core/dimmer/ramp.c \
       ../core/mua/mua.c

all: run_tests
	./run_tests

run_tests: $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

.PHONY: all clean
clean:
	rm -f run_tests
