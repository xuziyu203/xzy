PROJ_NAME=apbTest

STANDALONE =..

CFLAGS+=-DSMP

# The active firmware is the moving-block demo and independent BG uploader.
# Keep the source set explicit so archived game files cannot enter this build.
SRCS = src/main.c \
       src/gpu.c \
       src/renderer.c \
       src/renderer_cpu.c \
       src/renderer_fpga.c \
       src/cpu_writer.c \
       src/quad_mode.c \
       src/monitor.c \
       src/step1_motion.c \
       src/demo_command_text.c \
       src/uart_reply.c \
       src/bg_uart.c \
       src/bg_ddr_adapter.c \
       ${STANDALONE}/common/start.S

include ${STANDALONE}/common/bsp.mk
include ${STANDALONE}/common/riscv64-unknown-elf.mk
include ${STANDALONE}/common/standalone.mk

LDSCRIPT = ${BSP_PATH}/linker/default.ld
