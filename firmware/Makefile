# --- CHIP & BOARD SETTINGS---
MCU = atmega328p
F_CPU = 16000000UL
PORT = /dev/ttyUSB0
BAUD = 115200
PROGRAMMER = arduino

# --- DIRECTORIES ---
BUILD_DIR = build

# --- TARGET & SOURCE CODE ---
TARGET = firmware
SRC = main.cpp Hardware.cpp UI.cpp
# Direct the .o file into BUILD_DIR
OBJ = $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(SRC))

# --- COMPILER CONFIGURATION ---
CC = avr-gcc
CXX = avr-g++
OBJCOPY = avr-objcopy
SIZE = avr-size

# Bare-metal optimization flags
CXXFLAGS = -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Os -Wall -std=gnu++11 -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections
LDFLAGS = -mmcu=$(MCU) -Wl,--gc-sections

# --- MAKE RECIPE ---
all: $(BUILD_DIR)/$(TARGET).hex size

# Create a build folder if it does not already exist
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Linking process (.elf) - depends on the build folder
$(BUILD_DIR)/$(TARGET).elf: $(OBJ) | $(BUILD_DIR)
	$(CXX) $(LDFLAGS) -o $@ $^

# Compilation process (.cpp to .o) - depends on the build folder
$(BUILD_DIR)/%.o: %.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Converting .elf to .hex
$(BUILD_DIR)/$(TARGET).hex: $(BUILD_DIR)/$(TARGET).elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

size: $(BUILD_DIR)/$(TARGET).elf
	@echo "------------------------------------"
	@$(SIZE) --format=avr --mcu=$(MCU) $<
	@echo "------------------------------------"

flash: $(BUILD_DIR)/$(TARGET).hex
	avrdude -F -V -c $(PROGRAMMER) -p $(MCU) -P $(PORT) -b $(BAUD) -U flash:w:$<:i

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all flash clean size