CC = gcc
CFLAGS = -Wall -Wextra -O3 -std=c11 -pthread -D_GNU_SOURCE
LDFLAGS = -pthread

# Libraries
LIBS = -lm

# GPU rendering (OpenGL)
GPU_LIBS = -lGL -lGLU

# Font rendering (FreeType) - optional
# Check if FreeType is available
HAVE_FREETYPE := $(shell pkg-config --exists freetype2 && echo yes || echo no)
ifeq ($(HAVE_FREETYPE),yes)
    CFLAGS += -DHAVE_FREETYPE
    FONT_LIBS = $(shell pkg-config --libs freetype2)
else
    FONT_LIBS =
endif

# Platform detection
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    CFLAGS += -DLINUX
    LIBS += $(GPU_LIBS) $(FONT_LIBS) -lX11 -lutil
    # Xrandr is optional - check if available
    ifneq ($(shell pkg-config --exists xrandr 2>/dev/null && echo yes),)
        LIBS += -lXrandr
    endif
endif
ifeq ($(UNAME_S),Darwin)
    CFLAGS += -DMACOS
    LIBS += -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo
    FONT_LIBS = -lfreetype2
endif

# Source files
SRCDIR = src
OBJDIR = obj
SOURCES = $(wildcard $(SRCDIR)/*.c)
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

# Target
TARGET = nierterm

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS) $(LIBS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) $(TARGET)

install: $(TARGET)
	cp $(TARGET) /usr/local/bin/

