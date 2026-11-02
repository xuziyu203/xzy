#ifndef DEMO_COMMAND_TEXT_H
#define DEMO_COMMAND_TEXT_H

#include "type.h"

const char *demo_skip_spaces(const char *text);
int demo_command_arg(const char *line, const char *command,
                     const char **argument);
int demo_parse_uint(const char *text, u32 *value);
int demo_text_equal(const char *text, const char *expected);

#endif
