PROJ_NAME=apbTest

STANDALONE =..

CFLAGS+=-DSMP
# GHD3S: default = single ELF (asset upload over UART + complete game, no other serial use). BUILD_VARIANT_GAME removes the
# command line / queries / diagnostics; ASSET_UPLOAD_SERIAL keeps the asset protocol (main_single.c). VARIANT=full builds the
# former UART diagnostic firmware for size comparison only (it does not meet the 3072 B reserve, not delivered).
ifneq ($(VARIANT),full)
CFLAGS+=-DBUILD_VARIANT_GAME -DASSET_UPLOAD_SERIAL
endif

SRCS = 	$(wildcard src/*.c) \
		$(wildcard src/*.cpp) \
		$(wildcard src/*.S) \
		${STANDALONE}/common/start.S

SRCS := $(filter-out src/renderer.c src/renderer_fpga.c src/monitor.c src/quad_mode.c src/step1_motion.c,$(SRCS))

# CMD (from FIX): share standard RV32 register-save/restore code only in new/changed controls
# and cold serial/asset command objects. Hot renderer/physics retain old flags.
# GHD3S: per-frame scene objects (enemy, bullet, missile, player, player bullet, effect) keep normal prologues: code space is no longer short.
SAVE_RESTORE_SRCS = src/joystick_ads1115.c src/player_input.c src/enemy_test.c \
 src/game_keys.c src/game_hud.c src/main.c src/game.c src/bg_uart.c \
 src/asset_upload.c src/uart_reply.c src/demo_command_text.c src/decor_scene.c src/gpu.c src/game_lifecycle.c \
 src/bg_ddr_adapter.c src/cpu_writer.c src/renderer_cpu.c

include ${STANDALONE}/common/bsp.mk
include ${STANDALONE}/common/riscv64-unknown-elf.mk
include ${STANDALONE}/common/standalone.mk

LDSCRIPT = ${BSP_PATH}/linker/default.ld
