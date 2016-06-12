# Changelog
# Changed the variables to include the header file directory
# Added global var for the XTENSA tool root
#
# This make file still needs some work.
#
#
# Output directors to store intermediate compiled files
# relative to the project directory
BUILD_BASE	= build
FW_BASE = firmware
ESPTOOL = esptool.py


# name for the target project
TARGET		= app

# linker script used for the above linkier step
LD_SCRIPT	= eagle.app.v6.ld

# we create two different files for uploading into the flash
# these are the names and options to generate them
FW_1	= 0x00000
FW_2	= 0x40000

FLAVOR ?= release

#GIT_VERSION := $(shell git describe --exact-match 2> /dev/null || echo "`git symbolic-ref HEAD 2> /dev/null | cut -b 12-`-`git log --pretty=format:\"%h\" -1`")
GIT_VERSION := $(shell git describe --abbrev=4 --dirty --always --tags)

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

	CCFLAGS += -Os -ffunction-sections -fno-jump-tables
	AR = xtensa-lx106-elf-ar
	CC = xtensa-lx106-elf-gcc
	LD = xtensa-lx106-elf-gcc
	NM = xtensa-lx106-elf-nm
	CPP = xtensa-lx106-elf-cpp
	OBJCOPY = xtensa-lx106-elf-objcopy
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


# which modules (subdirectories) of the project to include in compiling
MODULES		= driver mqtt modules user user/aes
EXTRA_INCDIR    = . include $(SDK_BASE)/../include $(HOME)/esp8266/esp-open-sdk/sdk/include lib/heatshrink user/aes user/kamstrup user/61107

# libraries used in this project, mainly provided by the SDK
LIBS		= c gcc hal phy pp net80211 lwip wpa main ssl c gcc

# compiler flags using during compilation of source files
CFLAGS		= -Os -Wpointer-arith -Wundef -Wall -Wno-pointer-sign -Wno-comment -Wno-switch -Wno-unknown-pragmas -Wl,-EL -fno-inline-functions -nostdlib -mlongcalls -mtext-section-literals  -D__ets__ -DICACHE_FLASH -DVERSION=\"$(GIT_VERSION)\" -DECB=0

# linker flags used to generate the main object file
LDFLAGS		= -nostdlib -Wl,--no-check-sections -u call_user_start -Wl,-static -Wl,-Map,app.map -Wl,--cref

ifeq ($(FLAVOR),debug)
    CFLAGS += -g -O0
    LDFLAGS += -g -O0
endif

ifeq ($(FLAVOR),release)
    CFLAGS += -g -O2
    LDFLAGS += -g -O2
endif

ifeq ($(DEBUG), 1)
    CFLAGS += -DDEBUG
#    CFLAGS += -DDEBUG -DPRINTF_DEBUG
endif

ifeq ($(DEBUG_NO_METER), 1)
    CFLAGS += -DDEBUG_NO_METER
endif

ifeq ($(DEBUG_SHORT_WEB_CONFIG_TIME), 1)
    CFLAGS += -DDEBUG_SHORT_WEB_CONFIG_TIME
endif

ifeq ($(EN61107), 1)
    CFLAGS += -DEN61107
	MODULES += user/en61107 user/cron user/ac
else ifeq ($(IMPULSE), 1)
    CFLAGS += -DIMPULSE
else
    CFLAGS += -DKMP
	MODULES += user/kamstrup user/cron user/ac
endif

ifeq ($(THERMO_NO), 1)
    CFLAGS += -DTHERMO_NO
endif

ifeq ($(LED_ON_AC), 1)
    CFLAGS += -DLED_ON_AC
endif

ifeq ($(EXT_SPI_RAM_IS_NAND), 1)
    CFLAGS += -DEXT_SPI_RAM_IS_NAND
endif

ifeq ($(AES), 1)
    CFLAGS += -DAES
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

SRC		:= $(foreach sdir,$(SRC_DIR),$(wildcard $(sdir)/*.c))
OBJ		:= $(patsubst %.c,$(BUILD_BASE)/%.o,$(SRC))
LIBS		:= $(addprefix -l,$(LIBS))
APP_AR		:= $(addprefix $(BUILD_BASE)/,$(TARGET)_app.a)
TARGET_OUT	:= $(addprefix $(BUILD_BASE)/,$(TARGET).out)

LD_SCRIPT	:= $(addprefix -T$(SDK_BASE)/$(SDK_LDDIR)/,$(LD_SCRIPT))

INCDIR	:= $(addprefix -I,$(SRC_DIR))
EXTRA_INCDIR	:= $(addprefix -I,$(EXTRA_INCDIR))
MODULE_INCDIR	:= $(addsuffix /include,$(INCDIR))

FW_FILE_1	:= $(addprefix $(FW_BASE)/,$(FW_1).bin)
FW_FILE_2	:= $(addprefix $(FW_BASE)/,$(FW_2).bin)

V ?= $(VERBOSE)
ifeq ("$(V)","1")
Q :=
vecho := @true
else
Q := @
vecho := @echo
endif

vpath %.c $(SRC_DIR)

define compile-objects
$1/%.o: %.c
	$(vecho) "CC $$<"
	$(Q) $(CC) $(INCDIR) $(MODULE_INCDIR) $(EXTRA_INCDIR) $(SDK_INCDIR) $(CFLAGS)  -c $$< -o $$@
endef

.PHONY: all checkdirs clean

all: checkdirs $(TARGET_OUT) $(FW_FILE_1) $(FW_FILE_2)

$(FW_FILE_1): $(TARGET_OUT)
	$(vecho) "FW $@"
	$(ESPTOOL) elf2image $< -o $(FW_BASE)/
	
$(FW_FILE_2): $(TARGET_OUT)
	$(vecho) "FW $@"
	$(ESPTOOL) elf2image $< -o $(FW_BASE)/

$(TARGET_OUT): $(APP_AR)
	$(vecho) "LD $@"
	$(Q) $(LD) -L$(SDK_LIBDIR) $(LD_SCRIPT) $(LDFLAGS) -Wl,--start-group $(LIBS) $(APP_AR) -Wl,--end-group -o $@

$(APP_AR): $(OBJ)
	$(vecho) "AR $@"
	$(Q) $(AR) cru $@ $^

checkdirs: $(BUILD_DIR) $(FW_BASE)

$(BUILD_DIR):
	$(Q) mkdir -p $@

firmware:
	$(Q) mkdir -p $@

flash: $(FW_FILE_1)  $(FW_FILE_2)
	$(ESPTOOL) -p $(ESPPORT) write_flash $(FW_1) $(FW_FILE_1) $(FW_2) $(FW_FILE_2)

webpages.espfs: html/ html/wifi/ mkespfsimage/mkespfsimage
	cd html; find | ../mkespfsimage/mkespfsimage > ../webpages.espfs; cd ..

mkespfsimage/mkespfsimage: mkespfsimage/
	make -C mkespfsimage

htmlflash: webpages.espfs
	if [ $$(stat -c '%s' webpages.espfs) -gt $$(( 0x2E000 )) ]; then echo "webpages.espfs too big!"; false; fi
	$(ESPTOOL) -p $(ESPPORT) write_flash 0x12000 webpages.espfs

flashall: $(FW_FILE_1) $(FW_FILE_2) webpages.espfs
	$(ESPTOOL) -p $(ESPPORT) write_flash $(FW_1) $(FW_FILE_1) $(FW_2) $(FW_FILE_2) 0x12000 webpages.espfs

flashblank:
	$(ESPTOOL) -p $(ESPPORT) write_flash 0x0 firmware/blank512k.bin

flash107th_bit_0xff:
	$(ESPTOOL) -p $(ESPPORT) write_flash 0x7c000 firmware/esp_init_data_default_107th_bit_0xff.bin

size:
	$(SIZE) -A -t -d $(APP_AR) | tee app_app.size
	$(SIZE) -B -t -d $(APP_AR) | tee app_app.size

screen:
	screen /dev/ttyUSB0 1200,cstopb
minicom:
	minicom -D /dev/ttyUSB0 -b 300 -7

test: flash
	screen $(ESPPORT) 115200

rebuild: clean all

clean:
	$(Q) rm -f $(APP_AR)
	$(Q) rm -f $(TARGET_OUT)
	$(Q) rm -rf $(BUILD_DIR)
	$(Q) rm -rf $(BUILD_BASE)
	$(Q) rm -f $(FW_FILE_1)
	$(Q) rm -f $(FW_FILE_2)
	$(Q) rm -f app_app.size
#	$(Q) rm -rf $(FW_BASE)

foo:
	echo $(CFLAGS)

$(foreach bdir,$(BUILD_DIR),$(eval $(call compile-objects,$(bdir))))
