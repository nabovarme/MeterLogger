ESPTOOL_CHIP ?= esp8266

# Define OTA flag (defaults to 0 / Factory Build)
OTA ?= 0

ifeq ($(OTA), 1)
	BUILD_BASE = build_ota
	FW_BASE = firmware_ota
else
	BUILD_BASE = build
	FW_BASE = firmware
endif

RELEASE_BASE = release
MERGED_BIN = firmware.bin
ESPTOOL = python3 -m esptool
BAUDRATE = 1500000
DEBUG_SPEED = 1200

# name for the target project
TARGET		= app

# rboot Linker scripts (Place rom0.ld and rom1.ld in your project's ld/ folder)
LD_SCRIPT_SLOT0 = ld/rom0.ld
LD_SCRIPT_SLOT1 = ld/rom1.ld

# Output rboot OTA Binaries
USER1_BIN = $(FW_BASE)/user1.bin
USER2_BIN = $(FW_BASE)/user2.bin

# EspFS sector placed between Slot 0 (500KB) and Slot 1 (500KB)
ESPFS	= 0x7E000

FLAVOR ?= release

#GIT_VERSION := $(shell git describe --exact-match 2> /dev/null || echo "`git symbolic-ref HEAD 2> /dev/null | cut -b 12-`-`git log --pretty=format:\"%h\" -1`")
GIT_VERSION ?= $(shell git rev-parse --abbrev-ref HEAD)-$(shell git rev-list HEAD --count)-$(shell git describe --abbrev=4 --dirty --always)
CUSTOM_KEY = $(shell perl -e 'my $$key = qq[$(KEY)]; print(q["{ ] . join(q[, ], (map(qq[0x$$_], $$key =~ /(..)/g))) . q[ }"])')
CUSTOM_AP_PASSWORD = $(shell perl -e 'print substr(qq[$(KEY)], 0, 16)')

#############################################################
# Select compile
#
ifeq ($(OS),Windows_NT)
# WIN32
# We are under windows.
	ifeq ($(XTENSA_CORE),lx106)
		# It is xcc
		AR = xt-ar
		CC = xt-xcc
		LD = xt-xcc
		NM = xt-nm
		CPP = xt-cpp
		OBJCOPY = xt-objcopy
		#MAKE = xt-make
		CCFLAGS += -Os --rename-section .text=.irom0.text --rename-section .literal=.irom0.literal
	else 
		# It is gcc, may be cygwin
		# Can we use -fdata-sections?
		CCFLAGS += -Os -ffunction-sections -fno-jump-tables
		AR = xtensa-lx106-elf-ar
		CC = xtensa-lx106-elf-gcc
		LD = xtensa-lx106-elf-gcc
		NM = xtensa-lx106-elf-nm
		CPP = xtensa-lx106-elf-cpp
		OBJCOPY = xtensa-lx106-elf-objcopy
	endif
	ESPPORT 	?= com1
	SDK_BASE	?= c:/Espressif/ESP8266_SDK
    ifeq ($(PROCESSOR_ARCHITECTURE),AMD64)
# ->AMD64
    endif
    ifeq ($(PROCESSOR_ARCHITECTURE),x86)
# ->IA32
    endif
else
# We are under other system, may be Linux. Assume using gcc.
	# Can we use -fdata-sections?
	ESPPORT ?= /dev/ttyUSB0
	SDK_BASE	?= $(HOME)/esp8266/esp-open-sdk/sdk

	CCFLAGS += -Os -ffunction-sections -fdata-sections -fno-jump-tables
	AR = xtensa-lx106-elf-ar
	AS = xtensa-lx106-elf-as
	CC = xtensa-lx106-elf-gcc
	LD = xtensa-lx106-elf-gcc
	NM = xtensa-lx106-elf-nm
	CPP = xtensa-lx106-elf-cpp
	OBJCOPY = xtensa-lx106-elf-objcopy
	OBJDUMP = xtensa-lx106-elf-objdump
	SIZE = xtensa-lx106-elf-size
    UNAME_S := $(shell uname -s)

    ifeq ($(UNAME_S),Linux)
# LINUX
    endif
    ifeq ($(UNAME_S),Darwin)
# OSX
    endif
    UNAME_P := $(shell uname -p)
    ifeq ($(UNAME_P),x86_64)
# ->AMD64
    endif
    ifneq ($(filter %86,$(UNAME_P)),)
# ->IA32
    endif
    ifneq ($(filter arm%,$(UNAME_P)),)
# ->ARM
    endif
endif
#############################################################

GIT_LWIP_VERSION := $(shell cd $(SDK_BASE)/../esp-open-lwip ; git rev-parse --abbrev-ref HEAD)-$(shell cd $(SDK_BASE)/../esp-open-lwip ; git rev-list HEAD --count)-$(shell cd $(SDK_BASE)/../esp-open-lwip ; git describe --abbrev=4 --dirty --always)

# which modules (subdirectories) of the project to include in compiling
MODULES			= driver mqtt modules user user/crypto rboot
EXTRA_INCDIR	= . \
				include \
				rboot \
				lib/heatshrink \
				user/crypto \
				user/kamstrup \
				user/61107 \
				$(SDK_BASE)/../include \
				$(SDK_BASE)/../lx106-hal/include \
				$(SDK_BASE)/../esp-open-lwip/include

# libraries used in this project, mainly provided by the SDK
LIBS			= main net80211 wpa pp phy hal ssl lwip_open gcc c

# compiler flags using during compilation of source files
CFLAGS			= -Os -Wpointer-arith -Wundef -Wall -Wno-pointer-sign -Wno-comment -Wno-switch -Wno-unknown-pragmas -Wl,-EL -fno-inline-functions -nostdlib -mlongcalls -mtext-section-literals  -D__ets__ -DICACHE_FLASH -DVERSION=\"$(GIT_VERSION)\" -DLWIP_VERSION=\"$(GIT_LWIP_VERSION)\" -DECB=0 -DKEY=$(CUSTOM_KEY) -DAP_PASSWORD=\"$(CUSTOM_AP_PASSWORD)\" -mforce-l32 -DCONFIG_ENABLE_IRAM_MEMORY=1 -DLWIP_OPEN_SRC

# linker flags used to generate the main object file
LDFLAGS = -nostdlib -Wl,--no-check-sections -u call_user_start -Wl,-static -Wl,-Map,app.map -Wl,--cref -Wl,--gc-sections -Lld -L$(SDK_BASE)/ld

ifeq ($(FLAVOR),debug)
    CFLAGS += -g -O2
    LDFLAGS += -g -O2
endif

ifeq ($(FLAVOR),release)
    CFLAGS += -O2
    LDFLAGS += -O2
endif

ifeq ($(DEBUG), 1)
    CFLAGS += -DDEBUG
    CFLAGS += -DDEBUG -DPRINTF_DEBUG
	DEBUG_SPEED = 115200
endif

ifeq ($(OTA), 1)
    CFLAGS += -DOTA_FW
endif

ifdef SERIAL
    CFLAGS += -DDEFAULT_METER_SERIAL=\"$(SERIAL)\"
else
    SERIAL = 9999999
    CFLAGS += -DDEFAULT_METER_SERIAL=\"$(SERIAL)\"
endif

ifeq ($(DEBUG_NO_METER), 1)
    CFLAGS += -DDEBUG_NO_METER
endif

ifeq ($(DEBUG_SHORT_WEB_CONFIG_TIME), 1)
    CFLAGS += -DDEBUG_SHORT_WEB_CONFIG_TIME
endif

ifneq ($(DEBUG_STACK_TRACE), 0)
    CFLAGS += -DDEBUG_STACK_TRACE
endif

ifeq ($(MC_66B), 1)
    EN61107 = 1
	CFLAGS += -DMC_66B -DEN61107
endif

ifeq ($(FLOW_METER), 1)
	CFLAGS += -DFLOW_METER
endif

ifeq ($(EN61107), 1)
    CFLAGS += -DEN61107
    MODULES += user/en61107 user/cron user/ac
    WIFI_SSID = "KAM_$(SERIAL)"
else ifeq ($(IMPULSE), 1)
    CFLAGS += -DIMPULSE
    WIFI_SSID = "EL_$(SERIAL)"
else
    CFLAGS += -DKMP
    MODULES += user/kamstrup user/cron user/ac
    WIFI_SSID = "KAM_$(SERIAL)"
endif

ifeq ($(IMPULSE_DEV_BOARD), 1)
    IMPULSE_DEV_BOARD = 1
	CFLAGS += -DIMPULSE_DEV_BOARD
endif

ifneq ($(AUTO_CLOSE), 0)
    CFLAGS += -DAUTO_CLOSE=1
endif

ifeq ($(NO_CRON), 1)
    CFLAGS += -DNO_CRON=1
endif

ifeq ($(THERMO_NO), 1)
    CFLAGS += -DTHERMO_NO
endif

ifeq ($(THERMO_ON_AC_2), 1)
    CFLAGS += -DTHERMO_ON_AC_2
endif

ifeq ($(LED_ON_AC), 1)
    CFLAGS += -DLED_ON_AC
endif

ifeq ($(AC_TEST), 1)
    CFLAGS += -DLED_ON_AC -DAC_TEST
endif

ifeq ($(EXT_SPI_RAM_IS_NAND), 1)
    CFLAGS += -DEXT_SPI_RAM_IS_NAND
endif

# various paths from the SDK used in this project
SDK_LIBDIR	= lib
SDK_LDDIR	= ld
SDK_INCDIR	= include include/json

####
#### no user configurable options below here
####
FW_TOOL		?= $(ESPTOOL)
SRC_DIR		:= $(MODULES)
BUILD_DIR	:= $(addprefix $(BUILD_BASE)/,$(MODULES))

SDK_LIBDIR	:= $(addprefix $(SDK_BASE)/,$(SDK_LIBDIR))
SDK_INCDIR	:= $(addprefix -I$(SDK_BASE)/,$(SDK_INCDIR))

AS_SRC		:= $(foreach sdir,$(SRC_DIR),$(wildcard $(sdir)/*.S)) 
C_SRC		:= $(foreach sdir,$(SRC_DIR),$(wildcard $(sdir)/*.c)) 
AS_OBJ		:= $(patsubst %.S,%.o,$(AS_SRC))
C_OBJ		:= $(patsubst %.c,%.o,$(C_SRC))
OBJ			:= $(patsubst %.o,$(BUILD_BASE)/%.o,$(AS_OBJ) $(C_OBJ))
LIBS		:= $(addprefix -l,$(LIBS))
APP_AR		:= $(addprefix $(BUILD_BASE)/,$(TARGET)_app.a)

TARGET_OUT_SLOT0 := $(addprefix $(BUILD_BASE)/,$(TARGET)_slot0.out)
TARGET_OUT_SLOT1 := $(addprefix $(BUILD_BASE)/,$(TARGET)_slot1.out)

INCDIR	:= $(addprefix -I,$(SRC_DIR))
EXTRA_INCDIR	:= $(addprefix -I,$(EXTRA_INCDIR))
MODULE_INCDIR	:= $(addsuffix /include,$(INCDIR))

V ?= $(VERBOSE)
ifeq ("$(V)","1")
Q :=
vecho := @true
else
Q := @
vecho := @echo
endif

vpath %.S $(SRC_DIR)
vpath %.c $(SRC_DIR)

define compile-objects
$1/%.o: %.S | checkdirs
	$(vecho) "ASM $$<"
	$(Q) $(CC) $(INCDIR) $(MODULE_INCDIR) $(EXTRA_INCDIR) $(SDK_INCDIR) $(CFLAGS) -D__ASSEMBLER__ -c $$< -o $$@
$1/%.o: %.c | checkdirs
	$(vecho) "CC $$<"
	$(Q) $(CC) $(INCDIR) $(MODULE_INCDIR) $(EXTRA_INCDIR) $(SDK_INCDIR) $(CFLAGS)  -c $$< -o $$@
endef

.PHONY: all checkdirs clean ota_bins merge_bin release flash \
	htmlflash flashall flashblank wifisetup flash107th_bit_0xff \
	size getstacktrace objdump screen minicom test rebuild ota

all: release

ota_bins: $(USER1_BIN) $(USER2_BIN)
	$(vecho) "rboot OTA Slot 0 (user1.bin) and Slot 1 (user2.bin) compiled successfully."

$(TARGET_OUT_SLOT0): $(APP_AR)
	$(vecho) "LD $@ (Slot 0)"
	$(Q) $(LD) -L$(SDK_LIBDIR) -T$(LD_SCRIPT_SLOT0) $(LDFLAGS) -Wl,--start-group $(LIBS) $(APP_AR) -Wl,--end-group -o $@
	$(vecho) "PATCH $@ (cnx_csa_fn(): 12c1f0d911d1f2e1 -> 0df0000000000000)"
	$(Q) xxd -e -p $@ | tr -d '\n' | perl -p -e 's/12c1f0d911d1f2e1/0df0000000000000/' | xxd -r -e -p > $@-patched
	$(Q) mv $@-patched $@
	$(vecho) "PATCH $@ (add + to version)"
	$(Q) xxd -e -p $@ | tr -d '\n' | perl -p -e 's/332e302e362d646576/332e302e362b646576/' | xxd -r -e -p > $@-patched
	$(Q) mv $@-patched $@

$(USER1_BIN): $(TARGET_OUT_SLOT0)
	$(vecho) "FW $@"
	$(Q) $(ESPTOOL) elf2image --version=2 -o $@ $<

$(TARGET_OUT_SLOT1): $(APP_AR)
	$(vecho) "LD $@ (Slot 1)"
	$(Q) $(LD) -L$(SDK_LIBDIR) -T$(LD_SCRIPT_SLOT1) $(LDFLAGS) -Wl,--start-group $(LIBS) $(APP_AR) -Wl,--end-group -o $@
	$(vecho) "PATCH $@ (cnx_csa_fn(): 12c1f0d911d1f2e1 -> 0df0000000000000)"
	$(Q) xxd -e -p $@ | tr -d '\n' | perl -p -e 's/12c1f0d911d1f2e1/0df0000000000000/' | xxd -r -e -p > $@-patched
	$(Q) mv $@-patched $@
	$(vecho) "PATCH $@ (add + to version)"
	$(Q) xxd -e -p $@ | tr -d '\n' | perl -p -e 's/332e302e362d646576/332e302e362b646576/' | xxd -r -e -p > $@-patched
	$(Q) mv $@-patched $@

$(USER2_BIN): $(TARGET_OUT_SLOT1)
	$(vecho) "FW $@"
	$(Q) $(ESPTOOL) elf2image --version=2 -o $@ $<

$(APP_AR): $(OBJ)
	$(vecho) "AR $@"
	$(Q) $(AR) cru $@ $^

checkdirs: $(BUILD_DIR) $(FW_BASE)

$(BUILD_DIR):
	$(Q) mkdir -p $@

$(FW_BASE):
	$(Q) mkdir -p $@

merge_bin: ota_bins webpages.espfs
	$(vecho) "Merging firmware into $(FW_BASE)/$(MERGED_BIN)"
	$(Q) $(ESPTOOL) --chip $(ESPTOOL_CHIP) merge_bin -o $(FW_BASE)/$(MERGED_BIN) \
		0xFE000 $(FW_BASE)/blank.bin \
		0xFC000 firmware/esp_init_data_default_112th_byte_0x03.bin \
		0x00000 rboot/rboot.bin \
		0x02000 $(USER1_BIN) \
		0x7E000 webpages.espfs

# Redefined the release rule to orchestrate both Factory and OTA builds sequentially
# Change this:
release:
	$(vecho) "--- 1/3: Building Factory Firmware (with embedded keys) ---"
	$(Q) $(MAKE) ota_bins webpages.espfs OTA=0
	$(vecho) "--- 2/3: Building OTA Firmware (generic / no keys) ---"
	$(Q) $(MAKE) ota_bins OTA=1
	$(vecho) "--- 3/3: Packaging Release for $(SERIAL) ---"
	$(Q) mkdir -p $(RELEASE_BASE)/$(SERIAL)
	$(Q) cp rboot/rboot.bin $(RELEASE_BASE)/$(SERIAL)/rboot.bin
	$(Q) cp firmware/user1.bin $(RELEASE_BASE)/$(SERIAL)/user1.bin
	$(Q) cp firmware/user2.bin $(RELEASE_BASE)/$(SERIAL)/user2.bin
	$(Q) cp firmware_ota/user1.bin $(RELEASE_BASE)/$(SERIAL)/user1.ota.bin
	$(Q) cp firmware_ota/user2.bin $(RELEASE_BASE)/$(SERIAL)/user2.ota.bin
	$(Q) cp webpages.espfs $(RELEASE_BASE)/$(SERIAL)/webpages.espfs
	$(Q) cp firmware/esp_init_data_default_112th_byte_0x03.bin $(RELEASE_BASE)/$(SERIAL)/esp_init_data_default_112th_byte_0x03.bin
	$(Q) cp firmware/blank.bin $(RELEASE_BASE)/$(SERIAL)/blank.bin
	$(vecho) "Release populated in $(RELEASE_BASE)/$(SERIAL)/ successfully."

# New dedicated OTA target
ota:
	$(vecho) "Building clean OTA firmware without factory keys..."
	$(Q) $(MAKE) all OTA=1

flash: $(USER1_BIN)
	$(ESPTOOL) -p $(ESPPORT) -b $(BAUDRATE) write_flash --flash_size 1MB --flash_mode dout 0x02000 $(USER1_BIN)

webpages.espfs: html/ html/wifi/ mkespfsimage/mkespfsimage
	$(Q) cd html; find | ../mkespfsimage/mkespfsimage > ../webpages.espfs; cd ..

mkespfsimage/mkespfsimage: mkespfsimage/
	$(Q) make -C mkespfsimage

htmlflash: webpages.espfs
	if [ $$(stat -c '%s' webpages.espfs) -gt $$(( 0x2E000 )) ]; then echo "webpages.espfs too big!"; false; fi
	$(ESPTOOL) -p $(ESPPORT) -b $(BAUDRATE) write_flash --flash_size 1MB --flash_mode dout $(ESPFS) webpages.espfs

flashall: ota_bins webpages.espfs
	$(ESPTOOL) -p $(ESPPORT) -b $(BAUDRATE) write_flash --flash_size 1MB --flash_mode dout 0xFE000 $(FW_BASE)/blank.bin 0xFC000 firmware/esp_init_data_default_112th_byte_0x03.bin 0x00000 rboot/rboot.bin 0x02000 $(USER1_BIN) $(ESPFS) webpages.espfs

flashblank:
	$(ESPTOOL) -p $(ESPPORT) -b $(BAUDRATE) write_flash --flash_size 1MB --flash_mode dout 0x0 firmware/blank512k.bin 0x80000 firmware/blank512k.bin

wifisetup:
	until nmcli d wifi connect "$(WIFI_SSID)" password "$(CUSTOM_AP_PASSWORD)"; do echo "retrying to connect to wifi"; done && sleep 2; firefox 'http://192.168.4.1/'

flash107th_bit_0xff:
	$(ESPTOOL) -p $(ESPPORT) -b $(BAUDRATE) write_flash --flash_size 1MB --flash_mode dout 0xFE000 firmware/esp_init_data_default_107th_byte_0xff.bin

size:
	$(Q) $(SIZE) -A -t -d $(APP_AR) | tee $(BUILD_BASE)/../app_app.size
	$(Q) $(SIZE) -B -t -d $(APP_AR) | tee -a $(BUILD_BASE)/../app_app.size

getstacktrace:
	$(ESPTOOL) -p $(ESPPORT) -b $(BAUDRATE) read_flash 0x80000 0x4000 firmware/stack_trace.dump

#stacktracedecode:
#	test -s $(TARGET_OUT) || echo "Need to make all first" && exit
#	test -s firmware/stack_trace.dump || echo "Need to make getstacktrace first" && exit
#	java -jar /meterlogger/EspStackTraceDecoder.jar /meterlogger/esp-open-sdk/xtensa-lx106-elf/bin/xtensa-lx106-elf-addr2line build/app.out firmware/stack_trace.dump

objdump:
	test -s $(TARGET_OUT_SLOT0) || echo "Need to make all first" && exit
	$(OBJDUMP) -f -s -d --source $(TARGET_OUT_SLOT0) > $(TARGET).S

screen:
	screen /dev/ttyUSB0 $(DEBUG_SPEED),cstopb
minicom:
	minicom -D /dev/ttyUSB0 -b 300 -7

test: flash
	screen $(ESPPORT) 115200

rebuild: clean all

clean:
	$(Q) rm -f app_app.size
	$(Q) rm -f $(TARGET).S
	$(Q) rm -rf build build_ota
	$(Q) rm -f firmware/user*.bin firmware_ota/user*.bin
	$(Q) rm -f $(TARGET_OUT_SLOT0)$(TARGET_OUT_SLOT1)
	$(Q) rm -f $(APP_AR)

foo:
#	echo $(CFLAGS)
#	@echo $(CUSTOM_KEY)
#	@echo $(CUSTOM_AP_PASSWORD)
	@echo $(GIT_LWIP_VERSION)

$(foreach bdir,$(BUILD_DIR),$(eval $(call compile-objects,$(bdir))))
