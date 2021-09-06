##
## Make an extended "nfqlb" binary
##
## Targets;
##  help - This printout
##  (default) - Build a dynamically linked binary
##  static - Build a statically linked binary. Requires local libnetfilter_queue
##  clean - Remove built files
##
## Variables;
##  O - Output directory
##  X - Executable
##  NFQLB_DIR - The nfqlb release directory
##  NFQD - The libnetfilter_queue directory
##
## Examples;
##  make -j8 NFQLB_DIR=$HOME/lib
##  make -j8 NFQLB_DIR=$HOME/lib X=$HOME/nfqlb
##  make -j8 NFQLB_DIR=$HOME/lib \
##    NFQD=/tmp/$USER/nfqlb/libnetfilter_queue-1.0.3/sys/usr/local static
##


NFQLB_DIR ?= ../..
O ?= /tmp/$(USER)/nfqlb
X ?= $(O)/nfqlb
SRC := $(wildcard *.c)
OBJ := $(SRC:%.c=$(O)/%.o)
XCFLAGS := -I$(NFQLB_DIR)/include
DIRS := $(O)

$(O)/%.o : %.c
	$(CC) -c $(XCFLAGS) $(CFLAGS) -pthread -Wall $< -o $@

$(X): $(OBJ)
	$(MAKE) -C $(NFQLB_DIR)/src LDFLAGS="$(OBJ)"

.PHONY: static
static: $(OBJ)
	$(MAKE) -C $(NFQLB_DIR)/src LDFLAGS="$(OBJ)" static

$(OBJ): | $(DIRS)
$(DIRS):
	@mkdir -p $(DIRS)

.PHONY: clean
clean:
	rm -f $(X) $(OBJ)

.PHONY: help
help:
	@grep '^##' $(lastword $(MAKEFILE_LIST)) | cut -c3-
