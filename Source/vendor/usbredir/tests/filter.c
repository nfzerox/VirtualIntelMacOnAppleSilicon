/*
 * Copyright 2021 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
*/

#include <errno.h>
#include <locale.h>
#include <glib.h>
#include <stdlib.h>

#include "usbredirfilter.h"

struct test {
    const char *name;
    const char *filter;
    const char *token_sep;
    const char *rule_sep;

    int want_retval;
    int want_nrules;
    const char *want_serialized;
};

static const struct test test_cases[] = {
    {
        .name = "empty filter",
        .filter = "",
    },
    {
        .name = "separators only",
        .filter = "|||",
        .want_serialized = "",
    },
    {
        .name = "one rule",
        .filter = "0x03,-1,-1,-1,0",
        .want_nrules = 1,
    },
    {
        .name = "two rules",
        .filter = "0x03,-1,-1,-1,0|-1,-1,-1,-1,1",
        .want_nrules = 2,
    },
    {
        .name = "ignore trailing rule_sep",
        .filter = "|0x03,-1,-1,-1,0|-1,-1,-1,-1,1|",
        .want_serialized = "0x03,-1,-1,-1,0|-1,-1,-1,-1,1",
        .want_nrules = 2,
    },
    {
        .name = "ignores empty rules",
        .filter = "0x03,-1,-1,-1,0|||-1,-1,-1,-1,1",
        .want_serialized = "0x03,-1,-1,-1,0|-1,-1,-1,-1,1",
        .want_nrules = 2,
    },
    {
        .name = "several trailing rule_sep and empty rules",
        .filter = "||||0x03,-1,-1,-1,0|||-1,-1,-1,-1,1||||",
        .want_serialized = "0x03,-1,-1,-1,0|-1,-1,-1,-1,1",
        .want_nrules = 2,
    },
    {
        .name = "change rule separator using multiple characters",
        .filter = "0x03,-1,-1,-1,0",
        .want_nrules = 1,
        .token_sep = ",;",
        .rule_sep = " \t\n",
    },
    {
        .name = "mix of different separators",
        .filter = "\t 0x03,-1;-1;-1,0\n\n",
        .want_serialized = "0x03,-1,-1,-1,0",
        .want_nrules = 1,
        .token_sep = ",;",
        .rule_sep = " \t\n",
    },
    {
        .name = "multiple rules, separators not the first character",
        .filter = "\n\t0x03;-1,-1,-1,0\n\n-1,-1,-1;-1;1",
        .want_serialized = "0x03,-1,-1,-1,0 -1,-1,-1,-1,1",
        .want_nrules = 2,
        .token_sep = ",;",
        .rule_sep = " \t\n",
    },
    {
        .name = "upper limit on class",
        .filter = "0x100,-1,-1,-1,0",
        .want_retval = -EINVAL,
    },
    {
        .name = "lower limit on class",
        .filter = "-2,-1,-1,-1,0",
        .want_retval = -EINVAL,
    },
    {
        .name = "upper limit on vendor",
        .filter = "0x03,,0x10000-1,-1,0",
        .want_retval = -EINVAL,
    },
    {
        .name = "lower limit on vendor",
        .filter = "0x03,-2,-1,-1,0",
        .want_retval = -EINVAL,
    },
    {
        .name = "upper limit on product",
        .filter = "0x03,-1,0x10000-1,,0",
        .want_retval = -EINVAL,
    },
    {
        .name = "lower limit on product",
        .filter = "0x03,-1,-2,-1,0",
        .want_retval = -EINVAL,
    },
    {
        .name = "upper limit on bcd",
        .filter = "0x03,-1,-1,0x10000,0",
        .want_retval = -EINVAL,
    },
    {
        .name = "lower limit on bcd",
        .filter = "0x03,-1,-1,-2,0",
        .want_retval = -EINVAL,
    },
    {
        .name = "extra argument",
        .filter = "0x03,-1,-1,-1,0,1",
        .want_retval = -EINVAL,
    },
    {
        .name = "missing argument",
        .filter = "0x03,-1,-1,-1",
        .want_retval = -EINVAL,
    },
    {
        .name = "missing value in argument",
        .filter = "0x03,-1,-1,,-1",
        .want_retval = -EINVAL,
    },
    {
        .name = "letter as value in argument (1)",
        .filter = "0x03,-1,-1,a,-1",
        .want_retval = -EINVAL,
    },
    {
        .name = "number sign as value in argument (2)",
        .filter = "0x03,-1,-1,#,-1",
        .want_retval = -EINVAL,
    },
    {
        .name = "space as value in argument (3)",
        .filter = "0x03,-1,-1, ,-1",
        .want_retval = -EINVAL,
    },
    {
        .name = "invalid token_sep",
        .filter = "0x03;-1;-1;-1;0",
        .want_retval = -EINVAL,
    },
    {
        .name = "invalid rule_sep",
        .filter = "0x03,-1,-1,-1,0;-1,-1,-1,-1,1",
        .want_retval = -EINVAL,
    },
    {
        .name = "bad rule in many",
        .filter = "0x03,-1,-1,-1,0|3|-1,-1,-1,-1,1",
        .want_retval = -EINVAL,
    },
    {
        .name = "empty token separator",
        .filter = "0x03,-1,-1,-1,0",
        .token_sep = "",
        .want_retval = -EINVAL,
    },
    {
        .name = "empty rule separator",
        .filter = "0x03,-1,-1,-1,0",
        .rule_sep = "",
        .want_retval = -EINVAL,
    },
};

static void
test_check(gconstpointer private)
{
    const struct test *const data = private;
    int retval, count = INT_MIN;
    struct usbredirfilter_rule *rules = NULL;
    const char *token_sep = data->token_sep ? data->token_sep : ",";
    const char *rule_sep = data->rule_sep ? data->rule_sep : "|";

    char *const quoted_filter = g_strescape(data->filter, NULL);
    g_test_queue_free(quoted_filter);

    g_test_message("Filter: %s", quoted_filter);

    retval = usbredirfilter_string_to_rules(data->filter, token_sep, rule_sep,
        &rules, &count);
    g_assert_cmpint(retval, ==, data->want_retval);

    if (retval == 0) {
        const char *const serialized =
            data->want_serialized ? data->want_serialized : data->filter;
        char *filter;

        g_assert_cmpint(count, ==, data->want_nrules);

        filter = usbredirfilter_rules_to_string(rules, count, token_sep,
            rule_sep);
        g_assert_nonnull(filter);
        g_assert_cmpstr(serialized, ==, filter);
        usbredirfilter_free(filter);
    }

    usbredirfilter_free(rules);
}

static void
add_tests(const char *prefix, const struct test items[], int count)
{
    int i;
    for (i = 0; i < count; i++) {
        char *name = g_strdup_printf("%s/#%d/%s", prefix, i, items[i].name);
        g_test_add_data_func(name, (gconstpointer)&items[i], &test_check);
        g_free(name);
    }
}

int
main(int argc, char **argv)
{
    setlocale(LC_ALL, "");
    g_test_init(&argc, &argv, NULL);

    add_tests("/filter/rules", test_cases, G_N_ELEMENTS(test_cases));

    return g_test_run();
}
