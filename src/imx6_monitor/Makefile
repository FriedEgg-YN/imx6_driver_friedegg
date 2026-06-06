APP_NAME := imx6-monitor
APP_SRC := imx6_monitor.c
APP_CFLAGS ?= -Wall -Wextra -O2
APP_LDLIBS ?= -ljpeg -lpthread

.PHONY: all clean

all: $(APP_NAME)

$(APP_NAME): $(APP_SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(APP_CFLAGS) $< -o $@ $(LDFLAGS) $(APP_LDLIBS)

clean:
	rm -f $(APP_NAME)
