# Stuff you might want to configure.

CFLAGS      = -W -Wall
INSTALL_DIR = /usr/local

# Stuff you probably don't want to configure.

CXX         = g++
LIB_LDFLAGS = --shared -nostartfiles
LIB_LDADD   = -ldl

BIN_TMPL    = shaper.in
BIN         = shaper

LIB         = libshaper.so
LIB_SOURCES = libshaper.cpp

all: $(LIB) $(BIN)

$(LIB): $(LIB_SOURCES)
	$(CXX) $(CFLAGS) -o $(LIB) $(LIB_SOURCES) $(LIB_LDFLAGS) $(LIB_LDADD)

$(BIN):
	sed "s,LIB,$(INSTALL_DIR)/lib/$(LIB)," $(BIN_TMPL) > $(BIN)
	chmod 0750 shaper

install: all
	install $(LIB) $(INSTALL_DIR)/lib/$(LIB)
	install $(BIN) $(INSTALL_DIR)/bin/$(BIN)

clean:
	rm -f $(LIB) $(BIN)
