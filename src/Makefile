##
## Make nfqueue-loadbalancer binaries
##
## Targets;
##  help - This printout
##  all (default) - Build liblbutil.a in $(LIBD) (/tmp/$(USER)/lib)
##  clean - Remove files from $(LIBD)
##  test - Build the lib and test programs, run unit-tests
##
## Examples;
##  make -j8
##  make -j8 test
##  make -j8 O=.
##

# https://www.google.se/search?q=recursive+make+harmful

O ?= /tmp/$(USER)/nfqlb
X ?= $(O)/nfqlb/nfqlb
LIB ?= $(O)/lib/libnfqlb.a
CPPFLAGS += -Ilib
CFLAGS += -Wall
LDFLAGS += -L$(O)/lib -lnfqlb -lrt -lmnl -lnetfilter_queue

DIRS := $(O)/lib/test $(O)/nfqlb
SRC := $(wildcard nfqlb/*.c)
LIB_SRC := $(wildcard lib/*.c)
OBJ := $(SRC:%.c=$(O)/%.o)
LIB_OBJ := $(LIB_SRC:%.c=$(O)/%.o)

$(O)/%.o : %.c
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

.PHONY: all
all: $(DIRS) $(X)

$(X): $(LIB) $(OBJ)
	$(CC) -o $(X) $(OBJ) $(LDFLAGS)

$(LIB): $(LIB_OBJ)
	@rm -f $(LIB)
	ar rcs $(LIB) $(LIB_OBJ)


$(DIRS):
	@mkdir -p $(DIRS)

.PHONY: clean
clean:
	rm -f $(X) $(LIB) $(OBJ) $(LIB_OBJ) $(TEST_OBJ)

.PHONY: help
help:
	@grep '^##' $(lastword $(MAKEFILE_LIST)) | cut -c3-

