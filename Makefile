CC ?= cc
STD = -std=c11
WARN = -Wall -Wextra
OPT ?= -O2
CFLAGS ?= $(STD) $(WARN) $(OPT) -g
CPPFLAGS = -Isrc -Ithird_party/cjson -D_POSIX_C_SOURCE=200809L
LDLIBS = -lcurl -lm

# Vendored third-party code is not held to our own warning level.
THIRDPARTY_CFLAGS = $(STD) -O2 -w

BUILD_DIR = build
TARGET = repoq

SRCS = src/main.c src/http.c src/json_parse.c src/output.c
OBJS = $(SRCS:src/%.c=$(BUILD_DIR)/%.o)
CJSON_OBJ = $(BUILD_DIR)/cJSON.o

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJS) $(CJSON_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(CJSON_OBJ): third_party/cjson/cJSON.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(THIRDPARTY_CFLAGS) $(CPPFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

PREFIX ?= /usr/local
MANDIR ?= $(PREFIX)/share/man/man1

install: $(TARGET)
	install -Dm755 $(TARGET) $(PREFIX)/bin/$(TARGET)
	install -Dm644 man/repoq.1 $(MANDIR)/repoq.1

uninstall:
	rm -f $(PREFIX)/bin/$(TARGET)
	rm -f $(MANDIR)/repoq.1
