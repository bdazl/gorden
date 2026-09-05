# Roboslop top-level Makefile. All commands route through CMakePresets.
#
# Usage:
#   make bootstrap PRESET=debug         conan install for PRESET
#   make configure                      cmake --preset
#   make build                          cmake --build --preset
#   make test                           ctest --preset
#   make run ARGS="..."                 run the gorden binary
#   make shaders                        compile shaders for PRESET
#   make format / format-check          clang-format
#   make tidy                           clang-tidy via compile_commands.json
#   make compdb                         refresh /compile_commands.json symlink
#   make clean                          rm -rf build/$(PRESET)
#   make distclean                      rm -rf build/
#   make all                            bootstrap configure build

PRESET ?= debug
JOBS   ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

.DEFAULT_GOAL := help

.PHONY: bootstrap configure build test run shaders \
        format format-check tidy compdb \
        clean distclean all help

bootstrap:
	@./scripts/bootstrap.sh $(PRESET)

configure:
	@cmake --preset $(PRESET)
	@./scripts/compdb.sh $(PRESET)

build:
	@cmake --build --preset $(PRESET) -j$(JOBS)

test:
	@ctest --preset $(PRESET)

run:
	@cd build/$(PRESET) && exec apps/gorden/gorden $(ARGS)

shaders:
	@cmake --build --preset $(PRESET) --target shaders -j$(JOBS)

format:
	@./scripts/format.sh

format-check:
	@./scripts/format.sh --check

tidy:
	@./scripts/tidy.sh

compdb:
	@./scripts/compdb.sh $(PRESET)

clean:
	@rm -rf build/$(PRESET)

distclean:
	@rm -rf build/

all: bootstrap configure build

help:
	@printf 'Roboslop Makefile targets (PRESET=%s):\n' '$(PRESET)'
	@printf '  bootstrap       conan install for PRESET (writes toolchain file)\n'
	@printf '  configure       cmake --preset $(PRESET)\n'
	@printf '  build           cmake --build --preset $(PRESET) -j$(JOBS)\n'
	@printf '  test            ctest --preset $(PRESET)\n'
	@printf '  run [ARGS=...]  run build/$(PRESET)/apps/gorden/gorden\n'
	@printf '  shaders         compile shaders for $(PRESET)\n'
	@printf '  format          clang-format -i across the source tree\n'
	@printf '  format-check    clang-format --dry-run (CI)\n'
	@printf '  tidy            clang-tidy via compile_commands.json\n'
	@printf '  compdb          refresh /compile_commands.json symlink\n'
	@printf '  clean           rm -rf build/$(PRESET)\n'
	@printf '  distclean       rm -rf build/\n'
	@printf '  all             bootstrap configure build\n'
