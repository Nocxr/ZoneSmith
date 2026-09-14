.PHONY: configure build run clean

BUILD_DIR := build

configure:
	cmake -S . -B $(BUILD_DIR) -G Ninja

build: configure
	cmake --build $(BUILD_DIR) --config Release

run: build
	$(BUILD_DIR)/ZoneSmith.exe

clean:
	cmake -E remove_directory $(BUILD_DIR)
