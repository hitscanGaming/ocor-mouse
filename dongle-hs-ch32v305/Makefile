# CH32V305GBU6 high-speed USB bridge firmware
# Toolchain: WCH/xPack riscv-none-elf-gcc (containerized; see .github/actions/setup-ch32/)

PROJECT  := dongle-hs
MCU_DEF  := CH32V30x_D8C
TOOLCHAIN_PREFIX ?= riscv-none-elf-

CC      := $(TOOLCHAIN_PREFIX)gcc
OBJCOPY := $(TOOLCHAIN_PREFIX)objcopy
SIZE    := $(TOOLCHAIN_PREFIX)size

BUILD_DIR ?= build

INCLUDES := -I Core -I User -I Peripheral/inc -I Debug

# Newer xpack riscv-none-elf-gcc (14.x) requires Zicsr and Zifencei extensions
# to be explicitly listed for `fence.i` and CSR instructions used by the WCH HAL.
ARCH_FLAGS := -march=rv32imafc_zicsr_zifencei -mabi=ilp32f

CFLAGS  := $(ARCH_FLAGS) -msmall-data-limit=8 \
           -mno-save-restore -Os -fmessage-length=0 \
           -fsigned-char -ffunction-sections -fdata-sections \
           -Wall -g \
           -D$(MCU_DEF) $(INCLUDES)

ASFLAGS := $(ARCH_FLAGS) -x assembler-with-cpp -g

LDFLAGS := $(ARCH_FLAGS) \
           -T Ld/Link.ld -nostartfiles \
           -Xlinker --gc-sections -Wl,-Map,$(BUILD_DIR)/$(PROJECT).map \
           --specs=nano.specs --specs=nosys.specs

C_SRCS  := $(wildcard Core/*.c) \
           $(wildcard Peripheral/src/*.c) \
           $(wildcard User/*.c) \
           $(wildcard Debug/*.c)
ASM_SRCS := Startup/startup_ch32v30x_D8C.S

OBJS := $(addprefix $(BUILD_DIR)/,$(notdir $(C_SRCS:.c=.o))) \
        $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SRCS:.S=.o)))

VPATH := Core:Peripheral/src:Startup:User:Debug

.PHONY: all clean release

all: $(BUILD_DIR)/$(PROJECT).hex $(BUILD_DIR)/$(PROJECT).bin

release: CFLAGS += -DNDEBUG
release: all

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/$(PROJECT).elf: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -o $@
	$(SIZE) $@

$(BUILD_DIR)/$(PROJECT).hex: $(BUILD_DIR)/$(PROJECT).elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD_DIR)/$(PROJECT).bin: $(BUILD_DIR)/$(PROJECT).elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
