TARGET=esp32s3
FIRMWARE_ELF=build/eos.elf
WIDTH ?= 280
HEIGHT ?= 240
QEMU_DISPLAY_FLAGS = -D EOS_QEMU_DISPLAY_WIDTH=$(WIDTH) -D EOS_QEMU_DISPLAY_HEIGHT=$(HEIGHT)
IDF_TOOLS_PY ?= $(shell if [ -n "$$IDF_PATH" ] && [ -f "$$IDF_PATH/tools/idf_tools.py" ]; then echo "$$IDF_PATH/tools/idf_tools.py"; elif [ -f "/opt/esp-idf/tools/idf_tools.py" ]; then echo "/opt/esp-idf/tools/idf_tools.py"; elif [ -f "$$HOME/esp/esp-idf/tools/idf_tools.py" ]; then echo "$$HOME/esp/esp-idf/tools/idf_tools.py"; else echo "idf_tools.py"; fi)

.PHONY: clean clean-all reconfigure build flash monitor symbols debug killdebug qemu qemu_graphics \
	qemu_graphics_800x480 qemu_graphics_480x320 qemu_graphics_320x240 qemu_graphics_280x240 qemu_graphics_240x240 \
	install-qemu install-qemu-xtensa install-qemu-riscv install-qemu-all refine-docs

help:
	@echo "===================================================="
	@echo "     (^__^)==\\~ EOS building system ~/==(^__^)     "
	@echo "===================================================="
	@echo "==>   Code generation"
	@echo "===================================================="
	@echo "  driver      - Generate EOS driver code"
	@echo "  bin         - Generate EOS bin code"
	@echo "===================================================="
	@echo "==>   Build"
	@echo "===================================================="
	@echo "  clean       - Clean active board build artifacts"
	@echo "  clean-all   - Clean all board caches and active state"
	@echo "  setboard    - Select board"
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
	@echo "  qemu                  - Run firmware in QEMU (qemu-system-xtensa)"
	@echo "  qemu_graphics         - Run in QEMU with graphics window (WIDTH/HEIGHT, default 280x240)"
	@echo "  qemu_graphics_800x480 - Run in QEMU with 800x480 virtual display"
	@echo "  qemu_graphics_480x320 - Run in QEMU with 480x320 virtual display"
	@echo "  qemu_graphics_320x240 - Run in QEMU with 320x240 virtual display"
	@echo "  qemu_graphics_280x240 - Run in QEMU with 280x240 virtual display (ST7789 size)"
	@echo "  qemu_graphics_240x240 - Run in QEMU with 240x240 virtual display (square)"
	@echo "  install-qemu          - Install QEMU for Xtensa (ESP32/ESP32-S3) via ESP-IDF tools"
	@echo "  install-qemu-riscv    - Install QEMU for RISC-V (ESP32-C3) via ESP-IDF tools"
	@echo "  install-qemu-all      - Install all QEMU packages via ESP-IDF tools"
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
	@echo "  refine-docs - Refine documentation using Gemini (loads @DEVELOPMENT.md)"
	@echo "===================================================="

clean:
	@echo "Cleaning active board build artifacts..."
	idf.py fullclean

clean-all:
	@echo "Cleaning active build, all board caches, and active state..."
	idf.py fullclean
	rm -rf .board_cache .active_board

setboard:
	python3 tools/setboard.py $(BOARD)

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

driver:
	python tools/codegen.py edriver -o main/edriver

bin:
	python tools/codegen.py ebin -o main/ebin

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

qemu:
	@if [ "$$(cat .active_board 2>/dev/null)" != "esp32s3_qemu" ]; then \
		echo "Switching board to esp32s3_qemu..."; \
		python3 tools/setboard.py esp32s3_qemu; \
	fi
	@if ! which qemu-system-xtensa >/dev/null 2>&1; then \
		echo "WARNING: qemu-system-xtensa not found in PATH."; \
		echo "Run 'make install-qemu' to install it, or 'source \$${IDF_PATH:-/opt/esp-idf}/export.sh' if already installed."; \
	fi
	@$(MAKE) build
	@echo "Running QEMU..."
	idf.py qemu

qemu_graphics:
	@if [ "$$(cat .active_board 2>/dev/null)" != "esp32s3_qemu" ]; then \
		echo "Switching board to esp32s3_qemu..."; \
		python3 tools/setboard.py esp32s3_qemu; \
	fi
	@if ! which qemu-system-xtensa >/dev/null 2>&1; then \
		echo "WARNING: qemu-system-xtensa not found in PATH."; \
		echo "Run 'make install-qemu' to install it, or 'source \$${IDF_PATH:-/opt/esp-idf}/export.sh' if already installed."; \
	fi
	@echo "Building EOS for QEMU graphics ($(WIDTH)x$(HEIGHT))..."
	idf.py $(QEMU_DISPLAY_FLAGS) build
	@echo "Running QEMU with graphics window ($(WIDTH)x$(HEIGHT))..."
	idf.py qemu --graphics

qemu_graphics_800x480:
	@$(MAKE) qemu_graphics WIDTH=800 HEIGHT=480

qemu_graphics_480x320:
	@$(MAKE) qemu_graphics WIDTH=480 HEIGHT=320

qemu_graphics_320x240:
	@$(MAKE) qemu_graphics WIDTH=320 HEIGHT=240

qemu_graphics_280x240:
	@$(MAKE) qemu_graphics WIDTH=280 HEIGHT=240

qemu_graphics_240x240:
	@$(MAKE) qemu_graphics WIDTH=240 HEIGHT=240

install-qemu:
	@echo "Installing QEMU for Xtensa (ESP32 / ESP32-S3)..."
	python3 $(IDF_TOOLS_PY) install qemu-xtensa
	@echo ""
	@echo "Installation complete. To activate QEMU in your current shell, run:"
	@echo "  source \$${IDF_PATH:-/opt/esp-idf}/export.sh"

install-qemu-xtensa: install-qemu

install-qemu-riscv:
	@echo "Installing QEMU for RISC-V (ESP32-C3)..."
	python3 $(IDF_TOOLS_PY) install qemu-riscv32
	@echo ""
	@echo "Installation complete. To activate QEMU in your current shell, run:"
	@echo "  source \$${IDF_PATH:-/opt/esp-idf}/export.sh"

install-qemu-all:
	@echo "Installing all QEMU packages (Xtensa + RISC-V)..."
	python3 $(IDF_TOOLS_PY) install qemu-xtensa qemu-riscv32
	@echo ""
	@echo "Installation complete. To activate QEMU in your current shell, run:"
	@echo "  source \$${IDF_PATH:-/opt/esp-idf}/export.sh"

refine-docs:
	gemini --model gemini-3.5-flash-lite --approval-mode auto_edit -p "Load @DEVELOPMENT.md . Analyze current repository state. Update documentation located in @docs Be comprehensive"


