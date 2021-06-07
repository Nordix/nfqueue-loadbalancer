##
## Make nfqueue-loadbalancer lib and executable
##
## Targets;
##  help - This printout
##  all (default) - Build the lib and the executable
##  test - Build the lib and test programs and run them
##  test_progs - Build the lib and test programs
##  clean - Remove built files
##
## Beside the usual CFLAGS and LDFLAGS some usable variables;
##  O - The output directory. Default /tmp/$USER/nfqlb
##  X - The executable.  Default $(O)/nfqlb/nfqlb
##
## Examples;
##  make -j8
##  make -j8 clean
##  make -j8 CFLAGS=-DSANITY_CHECK test
##  make -j8 X=/tmp/my-nfqlb
##  make -j8 O=.       # (you *can*, but don't do that!)
##

# https://www.google.se/search?q=recursive+make+harmful
# https://stackoverflow.com/questions/4440500/depending-on-directories-in-make

O ?= /tmp/$(USER)/nfqlb
X ?= $(O)/nfqlb/nfqlb
LIB ?= $(O)/lib/libnfqlb.a

DIRS := $(O)/lib/test $(O)/nfqlb
SRC := $(wildcard nfqlb/*.c)
LIB_SRC := $(wildcard lib/*.c)
OBJ := $(SRC:%.c=$(O)/%.o)
LIB_OBJ := $(LIB_SRC:%.c=$(O)/%.o)
NFQD := $(O)/libnetfilter_queue-1.0.3/sys/usr/local

$(O)/%.o : %.c
	$(CC) -c $(CFLAGS) -pthread -Wall -Ilib $< -o $@

.PHONY: all static
all: $(X)
static: $(X)
# https://stackoverflow.com/questions/47905554/segmentation-fault-appears-when-i-use-shared-memory-only-from-statically-build-p
static: LDFLAGS := -static -L$(NFQD)/lib -Wl,--whole-archive -lpthread -Wl,--no-whole-archive
static: CFLAGS := -I$(NFQD)/include

$(X): $(LIB) $(OBJ)
	$(CC) -o $(X) $(OBJ) $(LDFLAGS) -pthread -L$(O)/lib -lnetfilter_queue -lnfqlb -lrt -lmnl
$(OBJ): | $(DIRS)
$(LIB): $(LIB_OBJ)
	@rm -f $(LIB)
	ar rcs $(LIB) $(LIB_OBJ)
$(LIB_OBJ): | $(DIRS)

.PHONY: test test_progs
$(O)/lib/test/% : lib/test/%.c
	$(CC) $(CFLAGS) -Wall -Ilib -pthread $< -o $@ -L$(O)/lib -lnfqlb -lrt
TEST_SRC := $(wildcard lib/test/*-test.c)
TEST_PROGS := $(TEST_SRC:%.c=$(O)/%)
$(TEST_PROGS): $(LIB)
test_progs: $(TEST_PROGS)
test: $(TEST_PROGS)
	@$(foreach p,$(TEST_PROGS),echo $(p);$(p);)

$(DIRS):
	@mkdir -p $(DIRS)

.PHONY: clean
clean:
	rm -f $(X) $(LIB) $(OBJ) $(LIB_OBJ) $(TEST_OBJ) $(TEST_PROGS)

.PHONY: help
help:
	@grep '^##' $(lastword $(MAKEFILE_LIST)) | cut -c3-
	@echo "Binary:"
	@echo "  $(X)"
	@echo "Test programs:"
	@$(foreach p,$(TEST_PROGS),echo "  $(p)";)
