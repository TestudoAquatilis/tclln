CC=gcc
OPTFLAGS=-O2
CFLAGS=-c -Wall -std=gnu99 $(OPTFLAGS)
LDFLAGS=$(OPTFLAGS)

LIBS=tcl glib-2.0

CFLAGS+=$(shell pkg-config --cflags $(LIBS))
LDFLAGS+=$(shell pkg-config --libs $(LIBS))

SOURCES=main.c tclln.c linenoise.c
LSOURCES=tclln.c linenoise.c
EXECUTABLE=tcllnsh
LIBRARY=libtclln.so

OBJDIR=obj
LOBJDIR=libobj
OBJECTS=$(SOURCES:%.c=$(OBJDIR)/%.o)
LOBJECTS=$(LSOURCES:%.c=$(LOBJDIR)/%.o)
DEPS=$(SOURCES:%.c=$(OBJDIR)/%.d)
LDEPS=$(LSOURCES:%.c=$(LOBJDIR)/%.d)

.PHONY: lib all
all: $(SOURCES) $(EXECUTABLE)
lib: $(LSOURCES) $(LIBRARY)

-include $(OBJECTS:.o=.d)
-include $(LOBJECTS:.o=.d)

$(LIBRARY): $(LOBJECTS) Makefile
	$(CC) -shared $(LDFLAGS) $(LOBJECTS) -o $@

$(LOBJDIR)/%.o: %.c Makefile | $(LOBJDIR)
	$(CC) -MM $(CFLAGS) -fpic $*.c > $(LOBJDIR)/$*.d
	sed -i -e "s/\\(.*\\.o:\\)/$(LOBJDIR)\\/\\1/" $(LOBJDIR)/$*.d
	$(CC) $(CFLAGS) -fpic $*.c -o $(LOBJDIR)/$*.o

$(EXECUTABLE): $(OBJECTS) Makefile
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

$(OBJDIR)/%.o: %.c Makefile | $(OBJDIR)
	$(CC) -MM $(CFLAGS) $*.c > $(OBJDIR)/$*.d
	sed -i -e "s/\\(.*\\.o:\\)/$(OBJDIR)\\/\\1/" $(OBJDIR)/$*.d
	$(CC) $(CFLAGS) $*.c -o $(OBJDIR)/$*.o

$(LOBJDIR):
	mkdir -p $(LOBJDIR)

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -f $(EXECUTABLE) $(LIBRARY) $(OBJECTS) $(DEPS) $(LOBJECTS) $(LDEPS)
	rm -rf $(OBJDIR) $(LOBJDIR)

memcheck: all
	valgrind --leak-check=full --suppressions=memcheck/suppress_libtcl.supp ./$(EXECUTABLE)
