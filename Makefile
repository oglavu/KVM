CXX := g++
CC  := gcc
LD  := ld

SRC_DIR   := src
TEST_DIR  := test
BUILD_DIR := build
BIN_DIR   := bin

HV_BIN := $(BIN_DIR)/mini_hypervisor.a

CXXFLAGS    := -O2 -g -Wall -Wextra
LDFLAGS_HV  := -lpthread

GUEST_CFLAGS := -m64 -ffreestanding -fno-pic -Wall -Wextra

# Discover tests
TEST_DIRS  := $(sort $(wildcard $(TEST_DIR)/test*))
TEST_NAMES := $(notdir $(TEST_DIRS))
TEST_TARGETS := $(TEST_NAMES)

GUEST_IMGS := $(addprefix $(BIN_DIR)/,$(addsuffix .img,$(TEST_NAMES)))
DEFAULT_GUEST_LD := guest.ld

.PRECIOUS: $(BUILD_DIR)/%/guest.o 	# saves intermediary .o guest images
.PHONY: all hypervisor tests clean list $(TEST_TARGETS)

all: hypervisor tests

list:
	@echo "Hypervisor: $(HV_BIN)"
	@echo "Tests:"
	@printf "  %s\n" $(TEST_TARGETS)

$(BIN_DIR) $(BUILD_DIR):
	mkdir -p $@

hypervisor: $(HV_BIN)
	@echo "\033[32m[Hypervisor]\033[0m Compilation finished successfully!"

$(HV_BIN): $(SRC_DIR)/mini_hypervisor.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS_HV)

tests: $(GUEST_IMGS)

# Individual test targets
$(TEST_TARGETS): %: $(BIN_DIR)/%.img

$(BUILD_DIR)/%/guest.o: $(TEST_DIR)/%/guest.c | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(GUEST_CFLAGS) -c $< -o $@

$(BIN_DIR)/%.img: $(BUILD_DIR)/%/guest.o | $(BIN_DIR)
	@ldscript="$(TEST_DIR)/$*/guest.ld"; \
	if [ ! -f "$$ldscript" ]; then \
		ldscript="$(DEFAULT_GUEST_LD)"; \
	fi; \
	echo "LD  $@ (using $$ldscript)"; \
	$(LD) -T "$$ldscript" "$(BUILD_DIR)/$*/guest.o" -o "$@" \
	&& echo "\033[32m[$*]\033[0m Compilation finished successfully!"

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)