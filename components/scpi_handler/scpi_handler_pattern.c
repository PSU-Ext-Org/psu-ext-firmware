/*
 * Copyright 2026 PSU-EXT Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "scpi_handler_pattern.h"

#include <stddef.h>
#include <string.h>

#include "scpi_handler_protocol.h"

enum { MAX_DEPTH = 4, MAX_EXPANSIONS = 256 };

/* All epsilon edges point forward, allowing closure in a single pass. */
static size_t group_end(const char *pattern, size_t start)
{
    unsigned depth = 1;
    for (size_t i = start + 1; ; ++i) {
        if (pattern[i] == '(' || pattern[i] == '[') {
            ++depth;
        } else if (pattern[i] == ')' || pattern[i] == ']') {
            if (--depth == 0) {
                return i;
            }
        }
    }
}

static size_t alternative_end(const char *pattern, size_t start)
{
    unsigned depth = 0;
    for (size_t i = start + 1; ; ++i) {
        if (pattern[i] == '(' || pattern[i] == '[') {
            ++depth;
        } else if (pattern[i] == ')' || pattern[i] == ']') {
            if (depth == 0) {
                return i;
            }
            --depth;
        }
    }
}

static bool literal(char ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
        ch == ':' || ch == '*' || ch == '?';
}

/* Count the finite language before enumeration, rejecting empty branches and
 * malformed delimiters. Reject counts above the limit before multiplication. */
static unsigned parse_sequence(const char **cursor, unsigned depth)
{
    unsigned count = 1;
    bool nonempty = false;
    while (**cursor != '\0' && **cursor != '|' && **cursor != ')' && **cursor != ']') {
        unsigned choices = 1;
        char ch = *(*cursor)++;
        if (ch == '(' || ch == '[') {
            if (depth == MAX_DEPTH) {
                return 0;
            }
            choices = parse_sequence(cursor, depth + 1);
            if (choices == 0) {
                return 0;
            }
            if (ch == '(') {
                bool alternative = false;
                while (**cursor == '|') {
                    alternative = true;
                    ++*cursor;
                    unsigned next = parse_sequence(cursor, depth + 1);
                    if (next == 0 || choices + next > MAX_EXPANSIONS) {
                        return 0;
                    }
                    choices += next;
                }
                if (!alternative) {
                    return 0;
                }
            } else {
                ++choices;
            }
            if (**cursor != (ch == '(' ? ')' : ']')) {
                return 0;
            }
            ++*cursor;
        } else if (!literal(ch)) {
            return 0;
        }
        if (choices > MAX_EXPANSIONS || count > MAX_EXPANSIONS / choices) {
            return 0;
        }
        count *= choices;
        nonempty = true;
    }
    return nonempty ? count : 0;
}

static bool visit_sequence(const char *pattern, size_t pos, char *keyword,
    size_t length, scpi_handler_pattern_visitor_t visitor, void *context)
{
    for (;;) {
        char ch = pattern[pos];
        if (ch == '\0') {
            keyword[length] = '\0';
            return visitor(keyword, context);
        }
        if (ch == '[') {
            if (!visit_sequence(pattern, group_end(pattern, pos) + 1,
                    keyword, length, visitor, context)) {
                return false;
            }
            ++pos;
        } else if (ch == '(') {
            size_t end = group_end(pattern, pos);
            size_t branch = pos + 1;
            for (size_t i = branch; i < end; ++i) {
                if (pattern[i] == '(' || pattern[i] == '[') {
                    i = group_end(pattern, i);
                } else if (pattern[i] == '|') {
                    if (!visit_sequence(pattern, branch, keyword, length, visitor, context)) {
                        return false;
                    }
                    branch = i + 1;
                }
            }
            pos = branch;
        } else if (ch == '|') {
            pos = alternative_end(pattern, pos) + 1;
        } else if (ch == ')' || ch == ']') {
            ++pos;
        } else {
            keyword[length++] = ch;
            ++pos;
        }
    }
}

bool scpi_handler_pattern_visit(const char *pattern,
    scpi_handler_pattern_visitor_t visitor, void *context)
{
    char keyword[SCPI_HANDLER_COMMAND_BUFFER_SIZE];
    return visit_sequence(pattern, 0, keyword, 0, visitor, context);
}

static bool valid_expansion(const char *keyword, void *context)
{
    (void)context;
    if (keyword[0] == '\0') {
        return false;
    }
    const char *query = strchr(keyword, '?');
    return query == NULL || query[1] == '\0';
}

bool scpi_handler_pattern_valid(const char *pattern)
{
    if (pattern == NULL || strnlen(pattern, SCPI_HANDLER_COMMAND_BUFFER_SIZE) >=
            SCPI_HANDLER_COMMAND_BUFFER_SIZE) {
        return false;
    }
    const char *cursor = pattern;
    return parse_sequence(&cursor, 0) != 0 && *cursor == '\0' &&
        scpi_handler_pattern_visit(pattern, valid_expansion, NULL);
}

bool scpi_handler_pattern_match(const char *pattern, const char *keyword)
{
    if (strpbrk(pattern, "[(") == NULL) {
        return strcmp(pattern, keyword) == 0;
    }
    bool current[SCPI_HANDLER_COMMAND_BUFFER_SIZE] = {true};
    bool next[SCPI_HANDLER_COMMAND_BUFFER_SIZE];
    size_t length = strlen(pattern);
    for (;;) {
        memset(next, 0, sizeof(next));
        for (size_t pos = 0; pos < length; ++pos) {
            if (!current[pos]) {
                continue;
            }
            char ch = pattern[pos];
            if (ch == '[') {
                current[group_end(pattern, pos) + 1] = true;
                current[pos + 1] = true;
            } else if (ch == '(') {
                current[pos + 1] = true;
                size_t end = group_end(pattern, pos);
                for (size_t i = pos + 1; i < end; ++i) {
                    if (pattern[i] == '(' || pattern[i] == '[') {
                        i = group_end(pattern, i);
                    } else if (pattern[i] == '|') {
                        current[i + 1] = true;
                    }
                }
            } else if (ch == '|') {
                current[alternative_end(pattern, pos) + 1] = true;
            } else if (ch == ')' || ch == ']') {
                current[pos + 1] = true;
            } else if (*keyword != '\0' && ch == *keyword) {
                next[pos + 1] = true;
            }
        }
        if (*keyword == '\0') {
            return current[length];
        }
        memcpy(current, next, sizeof(current));
        ++keyword;
    }
}
