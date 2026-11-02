#ifndef BUILD_VARIANT_GAME /* GHD3: full UART build only (single ELF: game_noserial.c stubs) */
#include "demo_command_text.h"

const char *demo_skip_spaces(const char *text)
{
    while ((*text == ' ') || (*text == '\t'))
        text++;
    return text;
}

int demo_command_arg(const char *line, const char *command,
                            const char **argument)
{
    const char *p = demo_skip_spaces(line);

    while (*command != '\0') {
        if (*p++ != *command++)
            return 0;
    }
    if ((*p != ' ') && (*p != '\t'))
        return 0;
    *argument = demo_skip_spaces(p);
    return 1;
}

int demo_parse_uint(const char *text, u32 *value)
{
    u32 parsed = 0u;
    u32 digit_count = 0u;

    while ((*text >= '0') && (*text <= '9')) {
        u32 digit = (u32)(*text - '0');
        if (parsed > (0xFFFFFFFFu - digit) / 10u)
            return 0;
        parsed = parsed * 10u + digit;
        digit_count++;
        text++;
    }
    text = demo_skip_spaces(text);
    if ((digit_count == 0u) || (*text != '\0'))
        return 0;
    *value = parsed;
    return 1;
}

int demo_text_equal(const char *text, const char *expected)
{
    while ((*text != '\0') && (*expected != '\0')) {
        if (*text++ != *expected++)
            return 0;
    }
    return (*expected == '\0') && (*demo_skip_spaces(text) == '\0');
}
#endif
