##
## Make nfqueue-loadbalancer lib and executable
##
## Targets;
##  help - This printout
##  all (default) - Build the lib and the executable
##  test - Build the lib and test programs, run unit-tests
##  clean - Remove built files
##
## Beside the usual CFLAGS and LDFLAGS some usable variables;
##  O - The output directory. Default /tmp/$USER/nfqlb
##  X - The executable.  Default $(O)/nfqlb/nfqlb
##
## Examples;
##  make -j8
##  make -j8 test
##  make -j8 X=/tmp/nfqlb
##  make -j8 O=.       # (you *can*, but don't do that!)
##

# https://www.google.se/search?q=recursive+make+harmful

O ?= /tmp/$(USER)/nfqlb
X ?= $(O)/nfqlb/nfqlb
LIB ?= $(O)/lib/libnfqlb.a

DIRS := $(O)/lib/test $(O)/nfqlb
SRC := $(wildcard nfqlb/*.c)
LIB_SRC := $(wildcard lib/*.c)
OBJ := $(SRC:%.c=$(O)/%.o)
LIB_OBJ := $(LIB_SRC:%.c=$(O)/%.o)

$(O)/%.o : %.c
	$(CC) -c $(CFLAGS) -Wall -Ilib $< -o $@

.PHONY: all
all: $(DIRS) $(X)

$(X): $(LIB) $(OBJ)
	$(CC) -o $(X) $(OBJ) $(LDFLAGS) -L$(O)/lib -lnfqlb -lrt -lmnl -lnetfilter_queue

$(LIB): $(LIB_OBJ)
	@rm -f $(LIB)
	ar rcs $(LIB) $(LIB_OBJ)

$(DIRS):
	@mkdir -p $(DIRS)

.PHONY: test test_progs
$(O)/lib/test/% : lib/test/%.c
	$(CC) $(CFLAGS) -Wall -Ilib $< -o $@ -L$(O)/lib -lnfqlb -lrt
TEST_SRC := $(wildcard lib/test/*-test.c)
TEST_PROGS := $(TEST_SRC:%.c=$(O)/%)
test_progs: $(LIB) $(TEST_PROGS)
test: $(LIB) $(TEST_PROGS)
	@$(foreach p,$(TEST_PROGS),echo $(p);$(p);)


.PHONY: clean
clean:
	rm -f $(X) $(LIB) $(OBJ) $(LIB_OBJ) $(TEST_OBJ)

.PHONY: help
help:
	@grep '^##' $(lastword $(MAKEFILE_LIST)) | cut -c3-

