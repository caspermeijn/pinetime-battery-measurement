/*
 * Copyright 2020 Casper Meijn <casper@meijn.net>
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

#include <assert.h>
#include <string.h>

#include "sysinit/sysinit.h"
#include "os/os.h"
#include "os/os_callout.h"
#include "os/os_dev.h"
#include "bsp/bsp.h"
#include "hal/hal_gpio.h"
#include "sgm4056/sgm4056.h"
#include "console/console.h"
#include "console/ticks.h"
#include "battery/battery.h"
#ifdef ARCH_sim
#include "mcu/mcu_sim.h"
#endif

static int32_t battery_voltage_mv = 0;
static charge_control_status_t charger_status = CHARGE_CONTROL_STATUS_OTHER;
static char hw_addr_str[18] = {'\0'};

static int
ble_hw_get_public_addr(uint8_t addr[6])
{
    uint32_t addr_high;
    uint32_t addr_low;

    /* Copy into device address. We can do this because we know platform */
    addr_low = NRF_FICR->DEVICEADDR[0];
    addr_high = NRF_FICR->DEVICEADDR[1];
    memcpy(addr, &addr_low, 4);
    memcpy(&addr[4], &addr_high, 2);

    return 0;
}

static int 
pinetime_battery_prop_changed(struct battery_prop_listener *listener,
        const struct battery_property *prop)
{
    if(prop->bp_type == BATTERY_PROP_VOLTAGE_NOW) {
        battery_voltage_mv = prop->bp_value.bpv_voltage;
    } else {
        assert(false);
    }
    return 0;
}

static struct battery_prop_listener battery_listener = {
    .bpl_prop_read = NULL,
    .bpl_prop_changed = pinetime_battery_prop_changed,
};

static void 
pinetime_battery_init(void)
{
    int rc;
    struct os_dev *battery;

    battery = os_dev_open("battery", OS_TIMEOUT_NEVER, NULL);
    assert(battery);

    struct battery_property * prop_voltage = battery_find_property(
        battery, BATTERY_PROP_VOLTAGE_NOW, BATTERY_PROPERTY_FLAGS_NONE, NULL);
    assert(prop_voltage);

    rc = battery_prop_change_subscribe(&battery_listener, prop_voltage);
    assert(rc == 0);

    rc = battery_set_poll_rate_ms(battery, 1000);
    assert(rc == 0);
}

static int 
charger_data_callback(struct charge_control *chg_ctrl, void * arg,
        void *data, charge_control_type_t type) 
{
    if (type == CHARGE_CONTROL_TYPE_STATUS) {
        charger_status = *(charge_control_status_t*)(data);
    } else {
        assert(false);
    }
    return 0;
}

static struct charge_control_listener charger_listener = {
    .ccl_type = CHARGE_CONTROL_TYPE_STATUS,
    .ccl_func = charger_data_callback,
};

static void 
charger_init(void)
{
    int rc;
    struct charge_control *charger;

    charger = charge_control_mgr_find_next_bytype(CHARGE_CONTROL_TYPE_STATUS, NULL);
    assert(charger);

    rc = charge_control_set_poll_rate_ms("charger", 1000);
    assert(rc == 0);

    rc = charge_control_register_listener(charger, &charger_listener);
    assert(rc == 0);

    rc = charge_control_read(charger, CHARGE_CONTROL_TYPE_STATUS, NULL, NULL, OS_TIMEOUT_NEVER);
}

static void
print_battery_status()
{
    char * charger_state = NULL;
    if (charger_status == CHARGE_CONTROL_STATUS_NO_SOURCE) {
        charger_state = "no_source";
    } else if (charger_status == CHARGE_CONTROL_STATUS_CHARGING) {
        charger_state = "charging";
    } else if (charger_status == CHARGE_CONTROL_STATUS_CHARGE_COMPLETE) {
        charger_state = "completed";
    }

    console_printf("{ ");
    console_printf("\"mac_addr\": \"%s\", ", hw_addr_str);
    console_printf("\"uptime_usec\": %lli, ", os_get_uptime_usec());
    console_printf("\"charger_state\": \"%s\", ", charger_state);
    console_printf("\"battery_voltage_mv\": %li, ", battery_voltage_mv);
    console_printf("\"backlight\": { \"level\": \"%s\", \"percent\": %i } ", "high", 50);
    console_printf(" }\n");
}

static void periodic_callback(struct os_event *ev);
static struct os_callout periodic_callout;

static void periodic_init()
{
    int rc;

    rc = hal_gpio_init_out(LCD_BACKLIGHT_HIGH_PIN, 1);
    assert(rc == 0);

    uint8_t addr[6];
    rc = ble_hw_get_public_addr(addr);
    assert(rc == 0);

    snprintf(hw_addr_str, sizeof(hw_addr_str), "%02x:%02x:%02x:%02x:%02x:%02x", addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

    os_callout_init(&periodic_callout, os_eventq_dflt_get(),
                    periodic_callback, NULL);

    rc = os_callout_reset(&periodic_callout, 100);
    assert(rc == 0);
}

static void periodic_callback(struct os_event *ev)
{
    int rc;

    /* Toggle blinking led */
    hal_gpio_toggle(LED_BLINK_PIN); 

    print_battery_status();

    /* Trigger in another second */
    rc = os_callout_reset(&periodic_callout, OS_TICKS_PER_SEC);
    assert(rc == 0);
}

int
main(int argc, char **argv)
{
    int rc;

#ifdef ARCH_sim
    mcu_sim_parse_args(argc, argv);
#endif

    sysinit();

    periodic_init();
    charger_init();
    pinetime_battery_init();

    while (1) {
       os_eventq_run(os_eventq_dflt_get());
    }
    
    assert(0);

    return rc;
}
