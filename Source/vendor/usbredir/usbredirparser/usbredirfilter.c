/* usbredirfilter.h usb redirection filter header

   Copyright 2012 Red Hat, Inc.

   Red Hat Authors:
   Hans de Goede <hdegoede@redhat.com>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, see <http://www.gnu.org/licenses/>.
*/
#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef WIN32
#include "strtok_r.h"
#define strtok_r  glibc_strtok_r
#endif

#include "usbredirfilter.h"

USBREDIR_VISIBLE
int usbredirfilter_string_to_rules(
    const char *filter_str, const char *token_sep, const char *rule_sep,
    struct usbredirfilter_rule **rules_ret, int *rules_count_ret)
{
    char *rule, *rule_saveptr, *token, *token_saveptr, *ep;
    struct usbredirfilter_rule *rules = NULL;
    int i, rules_count, *values, ret = 0;
    char *buf = NULL;
    const char *r;

    if (strlen(token_sep) == 0 || strlen(rule_sep) == 0) {
        return -EINVAL;
    }

    *rules_ret = NULL;
    *rules_count_ret = 0;

    /* Figure out how much rules there are in the file, so we know how
       much memory we must allocate for the rules array.
       Note this will come up with a slightly too large number if there are
       empty rule strings in the set. */
    r = filter_str;
    rules_count = 0;
    for (;;) {
        r += strspn(r, rule_sep);
        if (!*r) {
            break;
        }
        rules_count++;
        r += strcspn(r, rule_sep);
    }

    rules = calloc(rules_count, sizeof(struct usbredirfilter_rule));
    if (!rules)
        return -ENOMEM;

    /* Make a copy since strtok mangles the string */
    buf = strdup(filter_str);
    if (!buf) {
        ret = -ENOMEM;
        goto leave;
    }

    /* And actually parse the string */
    rules_count = 0;
    rule = strtok_r(buf, rule_sep, &rule_saveptr);
    while (rule) {
        /* We treat the filter rule as an array of ints for easier parsing */
        values = (int *)&rules[rules_count];
        token = strtok_r(rule, token_sep, &token_saveptr);
        for (i = 0; i < 5 && token; i++) {
            values[i] = strtol(token, &ep, 0);
            if (*ep)
                break;
            token = strtok_r(NULL, token_sep, &token_saveptr);
        }
        if (i != 5 || token != NULL ||
                usbredirfilter_verify(&rules[rules_count], 1)) {
            ret = -EINVAL;
            goto leave;
        }
        rules_count++;
        rule = strtok_r(NULL, rule_sep, &rule_saveptr);
    }

    *rules_ret = rules;
    *rules_count_ret = rules_count;

leave:
    if (ret)
        free(rules);
    free(buf);
    return ret;
}

USBREDIR_VISIBLE
char *usbredirfilter_rules_to_string(const struct usbredirfilter_rule *rules,
    int rules_count, const char *token_sep, const char *rule_sep)
{
    int i;
    char *str, *p;

    if (usbredirfilter_verify(rules, rules_count))
        return NULL;

    if (strlen(token_sep) == 0 || strlen(rule_sep) == 0) {
        return NULL;
    }

    /* We need 28 bytes per rule in the worst case */
    str = malloc(28 * rules_count + 1);
    if (!str)
        return NULL;

    p = str;
    for (i = 0; i < rules_count; i++) {
        if (rules[i].device_class != -1)
            p += sprintf(p, "0x%02x%c", rules[i].device_class, *token_sep);
        else
            p += sprintf(p, "-1%c", *token_sep);

        if (rules[i].vendor_id != -1)
            p += sprintf(p, "0x%04x%c", rules[i].vendor_id, *token_sep);
        else
            p += sprintf(p, "-1%c", *token_sep);

        if (rules[i].product_id != -1)
            p += sprintf(p, "0x%04x%c", rules[i].product_id, *token_sep);
        else
            p += sprintf(p, "-1%c", *token_sep);

        if (rules[i].device_version_bcd != -1)
            p += sprintf(p, "0x%04x%c", rules[i].device_version_bcd, *token_sep);
        else
            p += sprintf(p, "-1%c", *token_sep);

        p += sprintf(p, "%d", rules[i].allow ? 1:0);
        if (i < rules_count - 1) {
            p += sprintf(p, "%c", *rule_sep);
        }
    }
    *p = '\0';

    return str;
}

static int usbredirfilter_check1(const struct usbredirfilter_rule *rules,
    int rules_count, uint8_t device_class, uint16_t vendor_id,
    uint16_t product_id, uint16_t device_version_bcd, int default_allow)
{
    int i;

    for (i = 0; i < rules_count; i++) {
        if ((rules[i].device_class == -1 ||
                rules[i].device_class == device_class) &&
            (rules[i].vendor_id == -1 ||
                rules[i].vendor_id == vendor_id) &&
            (rules[i].product_id == -1 ||
                rules[i].product_id == product_id) &&
            (rules[i].device_version_bcd == -1 ||
                rules[i].device_version_bcd == device_version_bcd)) {
            /* Found a match ! */
            return rules[i].allow ? 0 : -EPERM;
        }
    }

    return default_allow ? 0 : -ENOENT;
}

USBREDIR_VISIBLE
int usbredirfilter_check(
    const struct usbredirfilter_rule *rules, int rules_count,
    uint8_t device_class, uint8_t device_subclass, uint8_t device_protocol,
    uint8_t *interface_class, uint8_t *interface_subclass,
    uint8_t *interface_protocol, int interface_count,
    uint16_t vendor_id, uint16_t product_id, uint16_t device_version_bcd,
    int flags)
{
    int i, rc, num_skipped=0;

    if (usbredirfilter_verify(rules, rules_count))
        return -EINVAL;

    /* Check the device_class */
    if (device_class != 0x00 && device_class != 0xef) {
        rc = usbredirfilter_check1(rules, rules_count, device_class,
                                   vendor_id, product_id, device_version_bcd,
                                   flags & usbredirfilter_fl_default_allow);
        if (rc)
            return rc;
    }

    /* Check the interface classes */
    for (i = 0; i < interface_count; i++) {
        if (!(flags & usbredirfilter_fl_dont_skip_non_boot_hid) &&
                interface_count > 1 && interface_class[i] == 0x03 &&
                interface_subclass[i] == 0x00 && interface_protocol[i] == 0x00) {
            num_skipped++;
            continue;
        }
        rc = usbredirfilter_check1(rules, rules_count, interface_class[i],
                                   vendor_id, product_id, device_version_bcd,
                                   flags & usbredirfilter_fl_default_allow);
        if (rc)
            return rc;
    }

    /* If all interfaces were skipped, then force check on that device,
     * by recursively calling this function with a flag that forbids
     * skipping (usbredirfilter_fl_dont_skip_non_boot_hid)
     */
    if (interface_count > 0 && num_skipped == interface_count) {
        rc = usbredirfilter_check(rules, rules_count,
                                  device_class, device_subclass, device_protocol,
                                  interface_class, interface_subclass,
                                  interface_protocol, interface_count,
                                  vendor_id, product_id, device_version_bcd,
                                  flags | usbredirfilter_fl_dont_skip_non_boot_hid);
        return rc;
    }

    return 0;
}

USBREDIR_VISIBLE
int usbredirfilter_verify(
    const struct usbredirfilter_rule *rules, int rules_count)
{
    int i;

    for (i = 0; i < rules_count; i++) {
        if (rules[i].device_class < -1 || rules[i].device_class > 255)
            return -EINVAL;
        if (rules[i].vendor_id < -1 || rules[i].vendor_id > 65535)
            return -EINVAL;
        if (rules[i].product_id < -1 || rules[i].product_id > 65535)
            return -EINVAL;
        if (rules[i].device_version_bcd < -1 ||
                rules[i].device_version_bcd > 65535)
            return -EINVAL;
    }
    return 0;
}

USBREDIR_VISIBLE
void usbredirfilter_print(
    const struct usbredirfilter_rule *rules, int rules_count, FILE *out)
{
    int i;
    char device_class[16], vendor[16], product[16], version[16];

    for (i = 0; i < rules_count; i++) {
        if (rules[i].device_class != -1)
            sprintf(device_class, " %02x", rules[i].device_class);
        else
            strcpy(device_class, "ANY");

        if (rules[i].vendor_id != -1)
            sprintf(vendor, "%04x", rules[i].vendor_id);
        else
            strcpy(vendor, " ANY");

        if (rules[i].product_id != -1)
            sprintf(product, "%04x", rules[i].product_id);
        else
            strcpy(product, " ANY");

        if (rules[i].device_version_bcd != -1)
            sprintf(version, "%2d.%02d",
                    ((rules[i].device_version_bcd & 0xf000) >> 12) * 10 +
                    ((rules[i].device_version_bcd & 0x0f00) >>  8),
                    ((rules[i].device_version_bcd & 0x00f0) >>  4) * 10 +
                    ((rules[i].device_version_bcd & 0x000f)));
        else
            strcpy(version, "  ANY");

        fprintf(out, "Class %s ID %s:%s Version %s %s\n", device_class, vendor,
                product, version, rules[i].allow ? "Allow":"Block");
    }
}

USBREDIR_VISIBLE
void usbredirfilter_free(void *ptr)
{
    /* for compatibility with 0.10.0 and older this MUST call free() */
    free(ptr);
}
