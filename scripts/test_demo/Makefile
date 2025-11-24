# LKL VMX Test Program Makefile
# Supports both static and dynamic compilation

CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99
LDFLAGS = 
STATIC_LDFLAGS = -static

# Target names
TARGET_DYNAMIC = lkl_vmx_test
TARGET_STATIC = lkl_vmx_test_static

# Default target
all: $(TARGET_DYNAMIC) $(TARGET_STATIC)

# Dynamic compilation
$(TARGET_DYNAMIC): test.c
	$(CC) $(CFLAGS) -o $(TARGET_DYNAMIC) test.c $(LDFLAGS)

# Static compilation
$(TARGET_STATIC): test.c
	$(CC) $(CFLAGS) -o $(TARGET_STATIC) test.c $(STATIC_LDFLAGS)

# Install targets
install: $(TARGET_STATIC)
	cp $(TARGET_STATIC) ../lkl_vmx_test
	chmod +x ../lkl_vmx_test

# Clean targets
clean:
	rm -f $(TARGET_DYNAMIC) $(TARGET_STATIC) ../lkl_vmx_test

# Help target
help:
	@echo "Available targets:"
	@echo "  all          - Build both dynamic and static versions"
	@echo "  dynamic      - Build dynamic version only"
	@echo "  static       - Build static version only"
	@echo "  install      - Install static version to parent directory"
	@echo "  clean        - Remove all generated files"
	@echo "  help         - Show this help message"

# Alias targets
dynamic: $(TARGET_DYNAMIC)
static: $(TARGET_STATIC)

# Phony targets
.PHONY: all dynamic static install clean help