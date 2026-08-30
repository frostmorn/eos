TARGET=esp32s3
FIRMWARE_ELF=build/eos.elf

.PHONY: clean reconfigure build flash monitor symbols debug killdebug

help:
	@echo "===================================================="
	@echo "     (^__^)==\\~ EOS building system ~/==(^__^)     "
	@echo "===================================================="
	@echo "==>   Build"
	@echo "===================================================="
	@echo "  clean       - Clean object files"
	@echo "  lsboard     - List supported boards"
	@echo "  menuconfig  - Launch configuration menu"
	@echo "  reconfigure - Reconfigure building system"
	@echo "  build       - Build EOS"
	@echo "===================================================="
	@echo "==>   Flash & Test"
	@echo "===================================================="
	@echo "  flash       - Flash firmware"
	@echo "  monitor     - Serial monitor"
	@echo "  debug       - Launch gdb via builtin jtag"
	@echo "  killdebug   - Kill OpenOCD Server"
	@echo "===================================================="
	@echo "==>   Tools"
	@echo "===================================================="
	@echo "  symbols     - List builtin firmware symbols"
	@echo "  disasm      - Disassemble generated elf"
	@echo "  cppcheck    - Static analyzis of a C code"
	@echo "===================================================="
	@echo "==>   Misc"
	@echo "===================================================="
	@echo "  todo        - Make TODO list for EOS"
	@echo "  research    - Make RESEARCH list for EOS"
	@echo "===================================================="

clean:
	@echo "Cleaning build artifacts..."
	idf.py fullclean

lsboard:
	@echo "===================================================="
	@echo "==>   EOS Boards"
	@echo "===================================================="
	@find main/eboard/* -type d | awk -F '/' '{ i++; print i,".", $$3 }';
	@echo "===================================================="

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

todo:
	@echo "===================================================="
	@echo "  (^_^)==\~  TODO:"
	@echo "===================================================="
	@grep -R "TODO" main
	@echo "===================================================="

research:
	@echo "===================================================="
	@echo "  (^_^)==\~  RESEARCH:"
	@echo "===================================================="
	@grep -R "RESEARCH" main
	@echo "===================================================="


monitor:
	@echo "Starting serial monitor..."
	idf.py monitor

symbols:
	readelf ${FIRMWARE_ELF} -s -X|less -R

disasm:
	xtensa-esp32-elf-objdump -D build/eos.elf|less -R

cppcheck:
	cppcheck $$(find main -name *.[ch])  |less -R

debug:
	tools/./debug_${TARGET}.sh ${FIRMWARE_ELF} 
killdebug:
	@echo "Killing openocd server"
	pkill -9 openocd

