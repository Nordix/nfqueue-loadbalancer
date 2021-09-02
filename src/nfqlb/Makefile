##
## Make a "nfqlb" binary from a release
##
## Targets;
##  help - This printout
##  all (default) - Build a dynamically linked binary
##  static - Build a statically linked binary. Requires local libnetfilter_queue
##  clean - Remove built files
##
## The nfqlb release is defined by NFQLB_DIR. Example;
##  make -j8 NFQLB_DIR=/tmp/$USER/nfqlb-0.3.0
##

NFQLB_DIR ?= ..
O ?= /tmp/$(USER)/nfqlb
X ?= $(O)/nfqlb/nfqlb
SRC := $(wildcard *.c)
OBJ := $(SRC:%.c=$(O)/%.o)
NFQD := $(O)/libnetfilter_queue-1.0.3/sys/usr/local
XCFLAGS := -I$(NFQLB_DIR)/include
XLDFLAGS := -L$(NFQLB_DIR)/lib

$(O)/%.o : %.c
	$(CC) -c $(XCFLAGS) $(CFLAGS) -pthread -Wall $< -o $@

.PHONY: all static
all: $(X)
static: $(X)
# https://stackoverflow.com/questions/47905554/segmentation-fault-appears-when-i-use-shared-memory-only-from-statically-build-p
static: XLDFLAGS := -static -L$(NFQD)/lib -Wl,--whole-archive -lpthread -Wl,--no-whole-archive -L$(NFQLB_DIR)/lib
static: XCFLAGS := -I$(NFQD)/include -I$(NFQLB_DIR)/include

$(X): $(LIB) $(OBJ)
	$(CC) -o $(X) $(OBJ) $(LDFLAGS) $(XLDFLAGS) -pthread  -lnfqlb -lnetfilter_queue -lrt -lmnl
$(OBJ): | $(DIRS)

$(DIRS):
	@mkdir -p $(DIRS)

.PHONY: clean
clean:
	rm -f $(X) $(OBJ)

.PHONY: help
help:
	@grep '^##' $(lastword $(MAKEFILE_LIST)) | cut -c3-
