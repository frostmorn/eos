TARGET=esp32s3
FIRMWARE_ELF=build/eos.elf

.PHONY: clean reconfigure build flash monitor symbols debug killdebug

help:
	@echo "===================================================="
	@echo "     (^__^)==\\~ EOS building system ~/==(^__^)     "
	@echo "===================================================="
	@echo "  clean       - Cleans object files"
	@echo "  menuconfig  - Launches configuration menu"
	@echo "  reconfigure - Reconfigures building system"
	@echo "  build       - Builds EOS"
	@echo "  flash       - Flashes firmware"
	@echo "  monitor     - Connects via serial interface"
	@echo "  symbols     - Exposes builtin firmware symbols"
	@echo "  debug       - Tries to launch gdb via builtin jtag"
	@echo "  killdebug   - An action to peform after debuging"
	@echo "                Effectively kills OpenOCD"
	@echo "===================================================="

clean:
	@echo "Cleaning build artifacts..."
	idf.py fullclean

menuconfig:
	@echo "Runing build configuration menu"
	idf.py menuconfig

reconfigure:
	@echo "Reconfiguring project..."
	idf.py reconfigure

build:
	@echo "Building project..."
	idf.py build

flash:
	@echo "Flashing device..."
	idf.py flash

monitor:
	@echo "Starting serial monitor..."
	idf.py monitor
symbols:
	readelf ${FIRMWARE_ELF} -s -X|less

debug:
	tools/./debug_${TARGET}.sh ${FIRMWARE_ELF} 
killdebug:
	@echo "Killing openocd server"
	pkill -9 openocd

