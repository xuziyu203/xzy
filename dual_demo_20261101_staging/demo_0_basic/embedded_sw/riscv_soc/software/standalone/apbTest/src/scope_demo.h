/* Included by main.c after its UART helpers. No rendering from UART callbacks. */
#ifndef SCOPE_DEMO_H
#define SCOPE_DEMO_H
#define SCOPE_ABI 0x53430101u
#define SCOPE_REG(o) (IO_APB_SLAVE_0_INPUT + (o))
static u32 scope_requested, scope_armed, scope_busy_mode, scope_window;
static u32 scope_saved_count, scope_saved_source;
static demo_viewport_mode_t scope_saved_mode;
static void scope_disarm(void)
{
    write_u32(0x53430000u, SCOPE_REG(0x204u));
    scope_armed = 0u;
}
static void scope_reconfigure(void)
{
    scope_disarm();
    step1_pending_count = 64u;
    demo_options_pending = 1u; /* forces existing drain/reseed path even in FULL */
    step1_async_freeze_new = 1u;
    demo_request_display(DEMO_VIEWPORT_MODE_FULL, GPU_FULL_SOURCE_BLIT);
}
static void scope_status(void)
{
    static const char *keys[22] = {
        " N=", " EDGES=", " ABORT=", " LAST_CYCLES=", " MIN_CYCLES=", " MAX_CYCLES=",
        " INTERVAL_CYCLES=", " TICKS_LO=", " TICKS_HI=", " SUM_LO=", " SUM_HI=",
        " FLAGS=", " FAULT=", " AR=", " AW=", " RBEATS=", " WBEATS=", " MAXLEN_AW_AR=",
        " TASK_CYCLES=", " HZ=", " PULSE_CYCLES=", " BYTES="
    };
    u32 i, sample[22];
    write_u32(1u, SCOPE_REG(0x208u));
    for (i=0u;i<22u;i++) sample[i]=read_u32(SCOPE_REG(0x20Cu+4u*i));
    uart_writeStr(BSP_UART_TERMINAL, "SCOPE WINDOW="); demo_uart_write_uint(scope_window);
    uart_writeStr(BSP_UART_TERMINAL, " REQUESTED="); demo_uart_write_uint(scope_requested);
    uart_writeStr(BSP_UART_TERMINAL, " ARMED="); demo_uart_write_uint(scope_armed);
    uart_writeStr(BSP_UART_TERMINAL, scope_busy_mode ? " MODE=BUSY" : " MODE=PULSE");
    uart_writeStr(BSP_UART_TERMINAL, " OPTIONS="); demo_uart_write_uint(gpu_get_demo_options() & 3u);
    for (i=0u;i<22u;i++) {
        if (i==4u || i==5u || i==9u || i==10u || i==13u || i==14u) continue; /* reserved */
        uart_writeStr(BSP_UART_TERMINAL, keys[i]); demo_uart_write_uint(sample[i]); }
    uart_writeStr(BSP_UART_TERMINAL, "\r\n");
}
static int scope_uart(const char *line)
{
    const char *arg;
    if (!demo_command_arg(line, "SCOPE", &arg)) return 0;
    if (read_u32(SCOPE_REG(0x200u)) != SCOPE_ABI) {
        uart_writeStr(BSP_UART_TERMINAL, "ERR SCOPE_ABI: load paired scope bit/ELF\r\n"); return 1;
    }
    if (demo_text_equal(arg, "STATUS")) { scope_status(); return 1; }
    if (demo_text_equal(arg, "STOP")) {
        scope_disarm();
        if (scope_requested) {
            scope_requested=0u; step1_pending_count=scope_saved_count;
            demo_options_pending=1u; step1_async_freeze_new=1u;
            demo_request_display(scope_saved_mode,scope_saved_source);
        }
        uart_writeStr(BSP_UART_TERMINAL, "OK SCOPE STOP; restore scene PENDING\r\n"); return 1;
    }
    if (demo_text_equal(arg, "START")) {
        if (!step1_active || bg_uart_preparing() || bg_uart_binary_active() ||
            bg_uart_recovery_active() || bg_uart_replay_active() || demo_bg_saved_valid) {
            uart_writeStr(BSP_UART_TERMINAL, "ERR SCOPE: exit BG first\r\n"); return 1;
        }
        if (!scope_requested) {
            scope_saved_count=step1_pending_count; scope_saved_mode=demo_confirmed_mode;
            scope_saved_source=demo_confirmed_source;
        }
        scope_requested=1u; scope_reconfigure();
        uart_writeStr(BSP_UART_TERMINAL, "OK SCOPE START PENDING; FULL BLIT 64x64x64 RGB565 frozen; wait SCOPE ARMED\r\n"); return 1;
    }
    if (demo_text_equal(arg, "PULSE") || demo_text_equal(arg, "BUSY")) {
        scope_busy_mode=demo_text_equal(arg,"BUSY") ? 1u : 0u;
        if (scope_requested) scope_reconfigure();
        uart_writeStr(BSP_UART_TERMINAL, "OK SCOPE MODE; new window PENDING if running\r\n"); return 1;
    }
    if (demo_text_equal(arg, "CLEAR") && scope_requested) {
        scope_reconfigure(); uart_writeStr(BSP_UART_TERMINAL, "OK SCOPE CLEAR PENDING\r\n"); return 1;
    }
    uart_writeStr(BSP_UART_TERMINAL, "ERR SCOPE: START STOP PULSE BUSY CLEAR STATUS\r\n"); return 1;
}
/* Called only at the main-loop idle boundary, after the unmeasured seed frame. */
static int scope_service(void)
{
    u32 control;
    if (!scope_requested || scope_armed) return 0;
    if (demo_pending_valid || demo_options_pending || demo_switch_state!=DEMO_SWITCH_IDLE ||
        demo_confirmed_mode!=DEMO_VIEWPORT_MODE_FULL || demo_confirmed_source!=GPU_FULL_SOURCE_BLIT ||
        demo_reseed_pending || full_blit_seed_pending) return 1;
    control=read_u32(SCOPE_REG(0x204u));
    if (!(control & 16u)) return 1;
    write_u32(0x53430001u | (scope_busy_mode<<1), SCOPE_REG(0x204u));
    if (!(read_u32(SCOPE_REG(0x204u)) & 1u)) return 1;
    scope_armed=1u; scope_window++;
    uart_writeStr(BSP_UART_TERMINAL, "SCOPE ARMED WINDOW="); demo_uart_write_uint(scope_window);
    uart_writeStr(BSP_UART_TERMINAL, " OPTIONS="); demo_uart_write_uint(gpu_get_demo_options() & 3u);
    uart_writeStr(BSP_UART_TERMINAL, scope_busy_mode ? " MODE=BUSY\r\n" : " MODE=PULSE\r\n");
    return 0;
}
#endif
