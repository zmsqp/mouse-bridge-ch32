TARGET := SBZFQ_APP
BUILD := build-app
PREFIX ?= riscv-none-embed-
CC := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
OBJDUMP := $(PREFIX)objdump
SIZE := $(PREFIX)size

CFLAGS := -march=rv32imacxw -mabi=ilp32 -msmall-data-limit=8 -msave-restore \
          -Os -g -ffunction-sections -fdata-sections -fno-common -fsigned-char \
          -Wunused -Wuninitialized -std=gnu99
INCLUDES := -ICommon -ICore -IDebug -IUser -IUser/USB_Host \
            -IUser/USBLIB/CONFIG -IUser/USBLIB/USB-Driver/inc -IPeripheral/inc
LDFLAGS := -T Ld/Link.ld -nostartfiles -Wl,--gc-sections \
           -Wl,-Map,$(BUILD)/$(TARGET).map --specs=nano.specs --specs=nosys.specs

VPATH := Common Core Debug Peripheral/src Startup User User/USB_Host \
         User/USBLIB/CONFIG User/USBLIB/USB-Driver/src

C_NAMES := iap_crc iap_mailbox core_riscv debug \
           ch32v20x_adc ch32v20x_bkp ch32v20x_can ch32v20x_crc ch32v20x_dbgmcu \
           ch32v20x_dma ch32v20x_exti ch32v20x_flash ch32v20x_gpio ch32v20x_i2c \
           ch32v20x_iwdg ch32v20x_misc ch32v20x_opa ch32v20x_pwr ch32v20x_rcc \
           ch32v20x_rtc ch32v20x_spi ch32v20x_tim ch32v20x_usart ch32v20x_wwdg \
           bridge_debug bridge_flash bridge_time bridge_uart_cmd bridge_usb_cfg \
           bridge_usb_import ch32v20x_it iap_app led_indicator main mouse_bridge \
           system_ch32v20x app_km ch32v20x_usbfs_host usb_host_hid usb_host_hub \
           hw_config usb_desc usb_endp usb_istr usb_prop usb_pwr \
           usb_core usb_init usb_int usb_mem usb_regs usb_sil
OBJS := $(addprefix $(BUILD)/,$(addsuffix .o,$(C_NAMES))) \
        $(BUILD)/startup_ch32v20x_D6.o

.PHONY: all clean check boot factory

all: $(BUILD)/$(TARGET).hex $(BUILD)/$(TARGET).bin $(BUILD)/$(TARGET).lst check

$(BUILD):
	powershell.exe -NoProfile -Command "New-Item -ItemType Directory -Path '$(BUILD)' -Force | Out-Null"

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/startup_ch32v20x_D6.o: startup_ch32v20x_D6.S | $(BUILD)
	$(CC) $(CFLAGS) -IStartup -MMD -MP -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) -lm
	$(SIZE) --format=berkeley $@

$(BUILD)/$(TARGET).hex: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/$(TARGET).lst: $(BUILD)/$(TARGET).elf
	$(OBJDUMP) --all-headers --demangle --disassemble -M xw $< > $@

check: $(BUILD)/$(TARGET).bin
	powershell.exe -NoProfile -Command "if ((Get-Item -LiteralPath '$(BUILD)/$(TARGET).bin').Length -gt 48896) { exit 1 }"

boot:
	$(MAKE) -C Bootloader all

factory: all boot
	python tools/merge_factory_hex.py --boot Bootloader/build/SBZFQ_BOOT.hex --app build-app/SBZFQ_APP.hex --output release/SBZFQ_FACTORY.hex --version 0x00010000

clean:
	powershell.exe -NoProfile -Command "if (Test-Path -LiteralPath '$(BUILD)') { Remove-Item -LiteralPath '$(BUILD)' -Recurse -Force }"

-include $(OBJS:.o=.d)
