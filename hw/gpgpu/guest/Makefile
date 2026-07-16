ROOT_DIR := $(CURDIR)
BUILD_DIR ?= $(ROOT_DIR)/build

DRIVER_DIR := $(ROOT_DIR)/gpgpu_driver
RUNTIME_DIR := $(ROOT_DIR)/gpgpu_runtime
KERNEL_DIR := $(ROOT_DIR)/gpgpu_kernel
SAMPLES_DIR := $(ROOT_DIR)/samples

KDIR ?= /lib/modules/$(shell uname -r)/build
CC ?= gcc
AR ?= ar
OBJCOPY ?= objcopy

CFLAGS ?= -O2 -g -Wall -Wextra
CPPFLAGS += -I$(DRIVER_DIR) -I$(RUNTIME_DIR) -I$(KERNEL_DIR)

KERNEL_CC ?= gcc
KERNEL_CFLAGS ?= -march=rv32imf_zicsr -mabi=ilp32f -c

RUNTIME_OBJ := $(BUILD_DIR)/runtime/gpgpu.o
RUNTIME_LIB := $(BUILD_DIR)/runtime/libgpgpu.a

KERNEL_OBJ := $(BUILD_DIR)/kernels/VecAdd.o
KERNEL_BIN := $(BUILD_DIR)/kernels/VecAdd.bin

SAMPLE_INFO := $(BUILD_DIR)/samples/info_test
SAMPLE_MMAP := $(BUILD_DIR)/samples/mmap_test
SAMPLE_MEMCPY := $(BUILD_DIR)/samples/memcpy_test
SAMPLE_LAUNCH := $(BUILD_DIR)/samples/launch_test
SAMPLE_VECADD := $(BUILD_DIR)/samples/vecadd_test

.PHONY: all driver runtime kernels samples clean driver-clean clean-source run-info run-mmap run-memcpy run-launch run-vecadd

all: driver runtime kernels samples

driver:
	@mkdir -p $(BUILD_DIR)/driver
	$(MAKE) -C $(KDIR) M=$(DRIVER_DIR) modules
	cp $(DRIVER_DIR)/gpgpu_driver.ko $(BUILD_DIR)/driver/
	$(MAKE) -C $(KDIR) M=$(DRIVER_DIR) clean

runtime: $(RUNTIME_LIB)

$(RUNTIME_OBJ): $(RUNTIME_DIR)/gpgpu.c $(RUNTIME_DIR)/gpgpu.h $(DRIVER_DIR)/uapi.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(RUNTIME_LIB): $(RUNTIME_OBJ)
	$(AR) rcs $@ $^

kernels: $(KERNEL_BIN)

$(KERNEL_OBJ): $(KERNEL_DIR)/VecAdd.S
	@mkdir -p $(dir $@)
	$(KERNEL_CC) $(KERNEL_CFLAGS) $< -o $@

$(KERNEL_BIN): $(KERNEL_OBJ)
	$(OBJCOPY) -O binary -j .text $< $@

samples: $(SAMPLE_INFO) $(SAMPLE_MMAP) $(SAMPLE_MEMCPY) $(SAMPLE_LAUNCH) $(SAMPLE_VECADD)

$(SAMPLE_INFO): $(SAMPLES_DIR)/info_test.c $(DRIVER_DIR)/uapi.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

$(SAMPLE_MMAP): $(SAMPLES_DIR)/mmap_test.c $(DRIVER_DIR)/uapi.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

$(SAMPLE_MEMCPY): $(SAMPLES_DIR)/memcpy_test.c $(RUNTIME_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(RUNTIME_LIB) -o $@

$(SAMPLE_LAUNCH): $(SAMPLES_DIR)/launch_test.c $(RUNTIME_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(RUNTIME_LIB) -o $@

$(SAMPLE_VECADD): $(SAMPLES_DIR)/vecadd_test.c $(RUNTIME_LIB) $(KERNEL_DIR)/VecAdd.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(RUNTIME_LIB) -o $@

run-info: $(SAMPLE_INFO)
	$(SAMPLE_INFO)

run-mmap: $(SAMPLE_MMAP)
	$(SAMPLE_MMAP)

run-memcpy: $(SAMPLE_MEMCPY)
	$(SAMPLE_MEMCPY)

run-launch: $(SAMPLE_LAUNCH)
	$(SAMPLE_LAUNCH)

run-vecadd: $(SAMPLE_VECADD) $(KERNEL_BIN)
	cd $(BUILD_DIR)/samples && ./vecadd_test

driver-clean:
	$(MAKE) -C $(KDIR) M=$(DRIVER_DIR) clean

clean-source: driver-clean
	rm -f $(SAMPLES_DIR)/info_test $(SAMPLES_DIR)/mmap_test
	rm -f $(SAMPLES_DIR)/memcpy_test $(SAMPLES_DIR)/launch_test $(SAMPLES_DIR)/vecadd_test

clean: clean-source
	rm -rf $(BUILD_DIR)
