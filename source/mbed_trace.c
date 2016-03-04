/*
 * Copyright (c) 2014-2015 ARM Limited. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

#ifndef YOTTA_CFG_MBED_TRACE
#define YOTTA_CFG_MBED_TRACE
#define YOTTA_CFG_MBED_TRACE_FEA_IPV6 1
#endif

#include "mbed-trace/mbed_trace.h"
#if YOTTA_CFG_MTRACE_FEA_IPV6 == 1
#include "mbed-trace/mbed_trace_ip6tos.h"
#endif

#if defined(_WIN32) || defined(__unix__) || defined(__unix) || defined(unix) || defined(YOTTA_CFG)
#ifndef MEM_ALLOC
#define MEM_ALLOC malloc
#endif
#ifndef MEM_FREE
#define MEM_FREE free
#endif
#else // _WIN32|__unix__|__unix|unix
#include "nsdynmemLIB.h"
#ifndef MEM_ALLOC
#define MEM_ALLOC ns_dyn_mem_alloc
#endif
#ifndef MEM_FREE
#define MEM_FREE ns_dyn_mem_free
#endif
#endif

#define VT100_COLOR_ERROR "\x1b[31m"
#define VT100_COLOR_WARN  "\x1b[33m"
#define VT100_COLOR_INFO  "\x1b[39m"
#define VT100_COLOR_DEBUG "\x1b[90m"

/** default max trace line size in bytes */
#ifdef YOTTA_CFG_MBED_TRACE_LINE_LENGTH
#define DEFAULT_TRACE_LINE_LENGTH         YOTTA_CFG_MBED_TRACE_LINE_LENGTH
#else
#define DEFAULT_TRACE_LINE_LENGTH         1024
#endif
/** default max temporary buffer size in bytes, used in
    trace_ipv6, trace_array and trace_strn */
#ifdef YOTTA_CFG_MTRACE_TMP_LINE_LEN
#define DEFAULT_TRACE_TMP_LINE_LEN        YOTTA_CFG_MTRACE_TMP_LINE_LEN
#else
#define DEFAULT_TRACE_TMP_LINE_LEN        128
#endif
/** default max filters (include/exclude) length in bytes */
#define DEFAULT_TRACE_FILTER_LENGTH       24

/** default print function, just redirect str to printf */
static void mbed_trace_realloc( char **buffer, int *length_ptr, int new_length);
static void mbed_trace_default_print(const char *str);
static void mbed_trace_reset_tmp(void);

typedef struct trace_s {
    /** trace configuration bits */
    uint8_t trace_config;
    /** exclude filters list, related group name */
    char *filters_exclude;
    /** include filters list, related group name */
    char *filters_include;
    /** Filters length */
    int filters_length;
    /** trace line */
    char *line;
    /** trace line length */
    int line_length;
    /** temporary data */
    char *tmp_data;
    /** temporary data array length */
    int tmp_data_length;
    /** temporary data pointer */
    char *tmp_data_ptr;

    /** prefix function, which can be used to put time to the trace line */
    char *(*prefix_f)(size_t);
    /** suffix function, which can be used to some string to the end of trace line */
    char *(*suffix_f)(void);
    /** print out function. Can be redirect to flash for example. */
    void (*printf)(const char *);
    /** print out function for TRACE_LEVEL_CMD */
    void (*cmd_printf)(const char *);
} trace_t;

static trace_t m_trace = {
    .filters_exclude = 0,
    .filters_include = 0,
    .line = 0,
    .tmp_data = 0,
    .prefix_f = 0,
    .suffix_f = 0,
    .printf  = 0,
    .cmd_printf = 0
};

int mbed_trace_init(void)
{
    m_trace.trace_config = TRACE_MODE_COLOR | TRACE_ACTIVE_LEVEL_ALL | TRACE_CARRIAGE_RETURN;
    m_trace.line_length = DEFAULT_TRACE_LINE_LENGTH;
    if (m_trace.line == NULL) {
        m_trace.line = MEM_ALLOC(m_trace.line_length);
    }
    m_trace.tmp_data_length = DEFAULT_TRACE_TMP_LINE_LEN;
    if (m_trace.tmp_data == NULL) {
        m_trace.tmp_data = MEM_ALLOC(m_trace.tmp_data_length);
    }
    m_trace.tmp_data_ptr = m_trace.tmp_data;
    m_trace.filters_length = DEFAULT_TRACE_FILTER_LENGTH;
    if (m_trace.filters_exclude == NULL) {
        m_trace.filters_exclude = MEM_ALLOC(m_trace.filters_length);
    }
    if (m_trace.filters_include == NULL) {
        m_trace.filters_include = MEM_ALLOC(m_trace.filters_length);
    }

    if (m_trace.line == NULL ||
            m_trace.tmp_data == NULL ||
            m_trace.filters_exclude == NULL  ||
            m_trace.filters_include == NULL) {
        //memory allocation fail
        mbed_trace_free();
        return -1;
    }
    memset(m_trace.tmp_data, 0, m_trace.tmp_data_length);
    memset(m_trace.filters_exclude, 0, m_trace.filters_length);
    memset(m_trace.filters_include, 0, m_trace.filters_length);
    memset(m_trace.line, 0, m_trace.line_length);

    m_trace.prefix_f = 0;
    m_trace.suffix_f = 0;
    m_trace.printf = mbed_trace_default_print;
    m_trace.cmd_printf = 0;

    return 0;
}
void mbed_trace_free(void)
{
    MEM_FREE(m_trace.line);
    m_trace.line_length = 0;
    m_trace.line = 0;
    MEM_FREE(m_trace.tmp_data);
    m_trace.tmp_data = 0;
    m_trace.tmp_data_ptr = 0;
    MEM_FREE(m_trace.filters_exclude);
    m_trace.filters_exclude = 0;
    MEM_FREE(m_trace.filters_include);
    m_trace.filters_include = 0;
    m_trace.filters_length = 0;
    m_trace.prefix_f = 0;
    m_trace.suffix_f = 0;
    m_trace.printf = mbed_trace_default_print;
    m_trace.cmd_printf = 0;
}
static void mbed_trace_realloc( char **buffer, int *length_ptr, int new_length)
{
    MEM_FREE(*buffer);
    *buffer  = MEM_ALLOC(new_length);
    *length_ptr = new_length;
}
void mbed_trace_buffer_sizes(int lineLength, int tmpLength)
{
    if( lineLength > 0 ) {
        mbed_trace_realloc( &(m_trace.line), &m_trace.line_length, lineLength );
    }
    if( tmpLength > 0 ) {
        mbed_trace_realloc( &(m_trace.tmp_data), &m_trace.tmp_data_length, tmpLength);
        mbed_trace_reset_tmp();
    }
}
void mbed_trace_config_set(uint8_t config)
{
    m_trace.trace_config = config;
}
uint8_t mbed_trace_config_get(void)
{
    return m_trace.trace_config;
}
void mbed_trace_prefix_function_set(char *(*pref_f)(size_t))
{
    m_trace.prefix_f = pref_f;
}
void mbed_trace_suffix_function_set(char *(*suffix_f)(void))
{
    m_trace.suffix_f = suffix_f;
}
void mbed_trace_print_function_set(void (*printf)(const char *))
{
    m_trace.printf = printf;
}
void mbed_trace_cmdprint_function_set(void (*printf)(const char *))
{
    m_trace.cmd_printf = printf;
}
void mbed_trace_exclude_filters_set(char *filters)
{
    if (filters) {
        (void)strncpy(m_trace.filters_exclude, filters, m_trace.filters_length);
    } else {
        m_trace.filters_exclude[0] = 0;
    }
}
const char *mbed_trace_exclude_filters_get(void)
{
    return m_trace.filters_exclude;
}
const char *mbed_trace_include_filters_get(void)
{
    return m_trace.filters_include;
}
void mbed_trace_include_filters_set(char *filters)
{
    if (filters) {
        (void)strncpy(m_trace.filters_include, filters, m_trace.filters_length);
    } else {
        m_trace.filters_include[0] = 0;
    }
}
static int8_t mbed_trace_skip(int8_t dlevel, const char *grp)
{
    if (dlevel >= 0 && grp != 0) {
        // filter debug prints only when dlevel is >0 and grp is given

        /// @TODO this could be much better..
        if (m_trace.filters_exclude[0] != '\0' &&
                strstr(m_trace.filters_exclude, grp) != 0) {
            //grp was in exclude list
            return 1;
        }
        if (m_trace.filters_include[0] != '\0' &&
                strstr(m_trace.filters_include, grp) == 0) {
            //grp was in include list
            return 1;
        }
    }
    return 0;
}
static void mbed_trace_default_print(const char *str)
{
    puts(str);
}
void mbed_tracef(uint8_t dlevel, const char *grp, const char *fmt, ...)
{
    m_trace.line[0] = 0; //by default trace is empty

    if (mbed_trace_skip(dlevel, grp) || fmt == 0 || grp == 0 || !m_trace.printf) {
        //return tmp data pointer back to the beginning
        mbed_trace_reset_tmp();
        return;
    }
    if ((m_trace.trace_config & TRACE_MASK_LEVEL) &  dlevel) {
        bool color = (m_trace.trace_config & TRACE_MODE_COLOR) != 0;
        bool plain = (m_trace.trace_config & TRACE_MODE_PLAIN) != 0;
        bool cr    = (m_trace.trace_config & TRACE_CARRIAGE_RETURN) != 0;

        int retval = 0, bLeft = m_trace.line_length;
        char *ptr = m_trace.line;
        if (plain == true || dlevel == TRACE_LEVEL_CMD) {
            va_list ap;
            va_start(ap, fmt);
            //add trace data
            retval = vsnprintf(ptr, bLeft, fmt, ap);
            va_end(ap);
            if (dlevel == TRACE_LEVEL_CMD && m_trace.cmd_printf) {
                m_trace.cmd_printf(m_trace.line);
                m_trace.cmd_printf("\n");
            } else {
                //print out whole data
                m_trace.printf(m_trace.line);
            }
        } else {
            if (color) {
                if (cr) {
                    retval = snprintf(ptr, bLeft, "\r\x1b[2K");
                    if (retval >= bLeft) {
                        retval = 0;
                    }
                    if (retval > 0) {
                        ptr += retval;
                        bLeft -= retval;
                    }
                }
                if (bLeft > 0) {
                    //include color in ANSI/VT100 escape code
                    switch (dlevel) {
                        case (TRACE_LEVEL_ERROR):
                            retval = snprintf(ptr, bLeft, "%s", VT100_COLOR_ERROR);
                            break;
                        case (TRACE_LEVEL_WARN):
                            retval = snprintf(ptr, bLeft, "%s", VT100_COLOR_WARN);
                            break;
                        case (TRACE_LEVEL_INFO):
                            retval = snprintf(ptr, bLeft, "%s", VT100_COLOR_INFO);
                            break;
                        case (TRACE_LEVEL_DEBUG):
                            retval = snprintf(ptr, bLeft, "%s", VT100_COLOR_DEBUG);
                            break;
                        default:
                            color = 0; //avoid unneeded color-terminate code
                            retval = 0;
                            break;
                    }
                    if (retval >= bLeft) {
                        retval = 0;
                    }
                    if (retval > 0 && color) {
                        ptr += retval;
                        bLeft -= retval;
                    }
                }

            }
            if (bLeft > 0 && m_trace.prefix_f) {
                va_list ap;
                va_start(ap, fmt);
                //find out length of body
                size_t sz = 0;
                sz = vsnprintf(NULL, 0, fmt, ap) + retval + (retval ? 4 : 0);
                //add prefix string
                retval = snprintf(ptr, bLeft, "%s", m_trace.prefix_f(sz));
                if (retval >= bLeft) {
                    retval = 0;
                }
                if (retval > 0) {
                    ptr += retval;
                    bLeft -= retval;
                }
                va_end(ap);
            }
            if (bLeft > 0) {
                //add group tag
                switch (dlevel) {
                    case (TRACE_LEVEL_ERROR):
                        retval = snprintf(ptr, bLeft, "[ERR ][%-4s]: ", grp);
                        break;
                    case (TRACE_LEVEL_WARN):
                        retval = snprintf(ptr, bLeft, "[WARN][%-4s]: ", grp);
                        break;
                    case (TRACE_LEVEL_INFO):
                        retval = snprintf(ptr, bLeft, "[INFO][%-4s]: ", grp);
                        break;
                    case (TRACE_LEVEL_DEBUG):
                        retval = snprintf(ptr, bLeft, "[DBG ][%-4s]: ", grp);
                        break;
                    default:
                        retval = snprintf(ptr, bLeft, "              ");
                        break;
                }
                if (retval >= bLeft) {
                    retval = 0;
                }
                if (retval > 0) {
                    ptr += retval;
                    bLeft -= retval;
                }
            }
            if (retval > 0 && bLeft > 0) {
                va_list ap;
                va_start(ap, fmt);
                //add trace text
                retval = vsnprintf(ptr, bLeft, fmt, ap);
                if (retval >= bLeft) {
                    retval = 0;
                }
                if (retval > 0) {
                    ptr += retval;
                    bLeft -= retval;
                }
                va_end(ap);
            }

            if (retval > 0 && bLeft > 0  && m_trace.suffix_f) {
                //add suffix string
                retval = snprintf(ptr, bLeft, "%s", m_trace.suffix_f());
                if (retval >= bLeft) {
                    retval = 0;
                }
                if (retval > 0) {
                    ptr += retval;
                    bLeft -= retval;
                }
            }

            if (retval > 0 && bLeft > 0  && color) {
                //add zero color VT100 when color mode
                retval = snprintf(ptr, bLeft, "\x1b[0m");
                if (retval >= bLeft) {
                    retval = 0;
                }
                if (retval > 0) {
                    // not used anymore
                    //ptr += retval;
                    //bLeft -= retval;
                }
            }
            //print out whole data
            m_trace.printf(m_trace.line);
        }
        //return tmp data pointer back to the beginning
        mbed_trace_reset_tmp();
    }
}
static void mbed_trace_reset_tmp(void)
{
    m_trace.tmp_data_ptr = m_trace.tmp_data;
}
const char *mbed_trace_last(void)
{
    return m_trace.line;
}
/* Helping functions */
#define tmp_data_left()  m_trace.tmp_data_length-(m_trace.tmp_data_ptr-m_trace.tmp_data)
#if YOTTA_CFG_MTRACE_FEA_IPV6 == 1
char *mbed_trace_ipv6(const void *addr_ptr)
{
    char *str = m_trace.tmp_data_ptr;
    if (str == NULL) {
        return "";
    }
    if (tmp_data_left() < 41) {
        return "";
    }
    if (addr_ptr == NULL) {
        return "<null>";
    }
    str[0] = 0;
    mbed_trace_ip6tos(addr_ptr, str);
    m_trace.tmp_data_ptr += strlen(str) + 1;
    return str;
}
char *mbed_trace_ipv6_prefix(const uint8_t *prefix, uint8_t prefix_len)
{
    char *str = m_trace.tmp_data_ptr;
    int retval, bLeft = tmp_data_left();
    char tmp[40];
    uint8_t addr[16] = {0};

    if (str == NULL) {
        return "";
    }
    if (bLeft < 45) {
        return "";
    }

    if (prefix_len != 0) {
        if (prefix == NULL || prefix_len > 128) {
            return "<err>";
        }
#ifdef COMMON_FUNCTIONS_FN        
        bitcopy(addr, prefix, prefix_len);
#else
        return "";
#endif  //COMMON_FUNCTIONS_FN
    }

    mbed_trace_ip6tos(addr, tmp);
    retval = snprintf(str, bLeft, "%s/%u", tmp, prefix_len);
    if (retval <= 0 || retval > bLeft) {
        return "";
    }

    m_trace.tmp_data_ptr += retval + 1;
    return str;
}
#endif //YOTTA_CFG_MTRACE_FEA_IPV6
char *mbed_trace_array(const uint8_t *buf, uint16_t len)
{
    int i, bLeft = tmp_data_left();
    char *str, *wptr;
    str = m_trace.tmp_data_ptr;
    if (str == NULL || bLeft == 0) {
        return "";
    }
    if (buf == NULL) {
        return "<null>";
    }
    wptr = str;
    wptr[0] = 0;
    const uint8_t *ptr = buf;
    char overflow = 0;
    for (i = 0; i < len; i++) {
        if (bLeft <= 3) {
            overflow = 1;
            break;
        }
        int retval = snprintf(wptr, bLeft, "%02x:", *ptr++);
        if (retval <= 0 || retval > bLeft) {
            break;
        }
        bLeft -= retval;
        wptr += retval;
    }
    if (wptr > str) {
        if( overflow ) {
            // replace last character as 'star',
            // which indicate buffer len is not enough
            *(wptr - 1) = '*';
        } else {
            //null to replace last ':' character
            *(wptr - 1) = 0;
        }
    }
    m_trace.tmp_data_ptr = wptr;
    return str;
}