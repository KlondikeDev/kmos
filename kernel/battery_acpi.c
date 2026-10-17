// This file has been generated in whole or in part by a Large Language Model
// Control-method batteries and AC adapters. Only PID 0 calls this adapter.
// EC ports come from ECDT/_CRS; no laptop-specific register offsets are used.
#include <battery.h>
#include <uacpi/uacpi.h>
#include <uacpi/utilities.h>
#include <uacpi/opregion.h>
#include <uacpi/resources.h>
#include <uacpi/tables.h>
#include <uacpi/acpi.h>
#include <io.h>
#include <ktime.h>
#include <klog.h>

typedef struct { u16 data, control; bool global_lock; } ec_t;
static ec_t controllers[4];
static u32 controller_count;
static uacpi_namespace_node* batteries[BATTERY_MAX];
static uacpi_namespace_node* adapters[BATTERY_MAX];
static u32 battery_count, adapter_count;

static bool ec_wait(ec_t* ec, u8 mask, bool set) {
    u64 end = time_counter_ns() + 20000000ULL;
    do {
        u8 status = inb(ec->control);
        if (status == 0xff) return false;
        if (((status & mask) != 0) == set) return true;
        __asm__ volatile("pause");
    } while (time_counter_ns() < end);
    return false;
}

static bool ec_byte(ec_t* ec, u8 address, u8* value, bool write) {
    if (!ec_wait(ec, 2, false)) return false;
    if (inb(ec->control) & 1) inb(ec->data); // discard a late reply from a timed-out request
    outb(ec->control, write ? 0x81 : 0x80);
    if (!ec_wait(ec, 2, false)) return false;
    outb(ec->data, address);
    if (write) {
        if (!ec_wait(ec, 2, false)) return false;
        outb(ec->data, *value);
        return ec_wait(ec, 2, false);
    }
    if (!ec_wait(ec, 1, true)) return false;
    *value = inb(ec->data);
    return true;
}

static uacpi_status ec_region(uacpi_region_op op, uacpi_handle data) {
    if (op == UACPI_REGION_OP_ATTACH) {
        uacpi_region_attach_data* a = data;
        return a->generic_info.base <= 255 && a->generic_info.length <= 256-a->generic_info.base ?
            UACPI_STATUS_OK : UACPI_STATUS_INVALID_ARGUMENT;
    }
    if (op == UACPI_REGION_OP_DETACH) return UACPI_STATUS_OK;
    if (op != UACPI_REGION_OP_READ && op != UACPI_REGION_OP_WRITE) return UACPI_STATUS_UNIMPLEMENTED;
    uacpi_region_rw_data* r = data;
    ec_t* ec = r->handler_context;
    if (!r->byte_width || r->byte_width > 8 || r->offset > 255 || r->byte_width > 256-r->offset)
        return UACPI_STATUS_INVALID_ARGUMENT;
    uacpi_u32 sequence = 0;
    if (ec->global_lock && uacpi_acquire_global_lock(100, &sequence) != UACPI_STATUS_OK)
        return UACPI_STATUS_TIMEOUT;
    u64 result = 0;
    bool ok = true;
    for (u32 i = 0; i < r->byte_width; i++) {
        u8 value = (u8)(r->value >> (i*8));
        if (!ec_byte(ec, (u8)(r->offset+i), &value, op == UACPI_REGION_OP_WRITE)) { ok = false; break; }
        result |= (u64)value << (i*8);
    }
    if (ec->global_lock) uacpi_release_global_lock(sequence);
    if (op == UACPI_REGION_OP_READ) r->value = result;
    return ok ? UACPI_STATUS_OK : UACPI_STATUS_HARDWARE_TIMEOUT;
}

typedef struct { u16 ports[2]; u32 count; } ec_ports_t;
static uacpi_iteration_decision ec_resource(void* user, uacpi_resource* resource) {
    ec_ports_t* p = user;
    u16 port;
    if (resource->type == UACPI_RESOURCE_TYPE_IO && resource->io.length &&
        resource->io.minimum == resource->io.maximum) port = resource->io.minimum;
    else if (resource->type == UACPI_RESOURCE_TYPE_FIXED_IO && resource->fixed_io.length)
        port = resource->fixed_io.address;
    else return UACPI_ITERATION_DECISION_CONTINUE;
    if (p->count < 2) p->ports[p->count++] = port;
    return UACPI_ITERATION_DECISION_CONTINUE;
}

static uacpi_iteration_decision find_ec(void* user, uacpi_namespace_node* node, uacpi_u32 depth) {
    (void)user; (void)depth;
    ec_ports_t ports = {0};
    if (uacpi_for_each_device_resource(node, "_CRS", ec_resource, &ports) != UACPI_STATUS_OK || ports.count != 2)
        return UACPI_ITERATION_DECISION_CONTINUE;
    ec_t* ec = NULL;
    for (u32 i = 0; i < controller_count; i++) {
        if (controllers[i].data == ports.ports[0] && controllers[i].control == ports.ports[1]) ec = &controllers[i];
    }
    if (!ec) {
        if (controller_count == 4) return UACPI_ITERATION_DECISION_BREAK;
        ec = &controllers[controller_count++];
        ec->data = ports.ports[0]; ec->control = ports.ports[1];
    }
    uacpi_u64 lock = 0;
    if (uacpi_eval_simple_integer(node, "_GLK", &lock) == UACPI_STATUS_OK) ec->global_lock = lock != 0;
    uacpi_status status = uacpi_install_address_space_handler(node,
        UACPI_ADDRESS_SPACE_EMBEDDED_CONTROLLER, ec_region, ec);
    if (status != UACPI_STATUS_OK) klog(LOG_WARN, "battery: EC handler unavailable (%u)\n", (u32)status);
    return UACPI_ITERATION_DECISION_CONTINUE;
}

static uacpi_iteration_decision find_battery(void* user, uacpi_namespace_node* node, uacpi_u32 depth) {
    (void)user; (void)depth;
    if (battery_count < BATTERY_MAX) batteries[battery_count++] = node;
    return UACPI_ITERATION_DECISION_CONTINUE;
}
static uacpi_iteration_decision find_adapter(void* user, uacpi_namespace_node* node, uacpi_u32 depth) {
    (void)user; (void)depth;
    if (adapter_count < BATTERY_MAX) adapters[adapter_count++] = node;
    return UACPI_ITERATION_DECISION_CONTINUE;
}

bool battery_acpi_init(void) {
    uacpi_status status = uacpi_initialize(UACPI_FLAG_BAD_CSUM_FATAL);
    if (status != UACPI_STATUS_OK) goto error;
    // ECDT permits EC accesses during module-level AML, before devices can
    // be enumerated. The normal _CRS path below handles machines without it.
    uacpi_table early;
    if (uacpi_table_find_by_signature("ECDT", &early) == UACPI_STATUS_OK) {
        const struct acpi_ecdt* e = early.ptr;
        if (e->hdr.length >= sizeof(*e) && e->ec_control.address_space_id == 1 &&
            e->ec_data.address_space_id == 1 && e->ec_control.address && e->ec_data.address &&
            e->ec_control.address <= 65535 && e->ec_data.address <= 65535 &&
            e->ec_control.register_bit_width == 8 && e->ec_data.register_bit_width == 8 &&
            !e->ec_control.register_bit_offset && !e->ec_data.register_bit_offset) {
            controllers[0] = (ec_t){(u16)e->ec_data.address, (u16)e->ec_control.address, false};
            controller_count = 1;
            uacpi_install_address_space_handler(uacpi_namespace_root(),
                UACPI_ADDRESS_SPACE_EMBEDDED_CONTROLLER, ec_region, &controllers[0]);
        }
        uacpi_table_unref(&early);
    }
    status = uacpi_namespace_load();
    if (status != UACPI_STATUS_OK) goto error;
    uacpi_find_devices("PNP0C09", find_ec, NULL);
    uacpi_set_interrupt_model(UACPI_INTERRUPT_MODEL_PIC);
    status = uacpi_namespace_initialize();
    if (status != UACPI_STATUS_OK) goto error;
    uacpi_find_devices("PNP0C0A", find_battery, NULL);
    uacpi_find_devices("ACPI0003", find_adapter, NULL);
    klog(LOG_OKAY, "battery: ACPI ready, %u battery device(s), %u AC adapter(s), %u EC(s)\n",
         battery_count, adapter_count, controller_count);
    return true;
error:
    klog(LOG_WARN, "battery: firmware initialization failed (%u)\n", (u32)status);
    return false;
}

static bool package_values(uacpi_namespace_node* node, const char* name, u32* out, u32 n) {
    uacpi_object* object = NULL;
    if (uacpi_eval_simple_package(node, name, &object) != UACPI_STATUS_OK) return false;
    uacpi_object_array package;
    bool ok = uacpi_object_get_package(object, &package) == UACPI_STATUS_OK && package.count >= n;
    for (u32 i = 0; ok && i < n; i++) {
        uacpi_u64 value;
        ok = uacpi_object_get_integer(package.objects[i], &value) == UACPI_STATUS_OK && value <= 0xffffffffULL;
        if (ok) out[i] = (u32)value;
    }
    uacpi_object_unref(object);
    return ok;
}

bool battery_acpi_sample(battery_info_t* out, u32* count, i32* ac) {
    *count = battery_count;
    *ac = -1;
    bool ok = true;
    for (u32 i = 0; i < battery_count; i++) {
        uacpi_u32 status = 0;
        if (uacpi_eval_sta(batteries[i], &status) != UACPI_STATUS_OK) {
            battery_normalize(&out[i], true, BATTERY_UNKNOWN, BATTERY_UNKNOWN, NULL);
            ok = false;             // failed presence query is unknown, not absent
            continue;
        }
        bool present = (status & 16) != 0;
        u32 info[4], bst[4];
        u32 unit = BATTERY_UNKNOWN, full = BATTERY_UNKNOWN;
        if (present) {
            if (package_values(batteries[i], "_BIX", info, 4)) { unit=info[1]; full=info[3]; }
            else if (package_values(batteries[i], "_BIF", info, 3)) { unit=info[0]; full=info[2]; }
        }
        bool sampled = present && package_values(batteries[i], "_BST", bst, 4);
        battery_normalize(&out[i], present, unit, full, sampled ? bst : NULL);
        if (present && (!sampled || !out[i].valid)) ok = false;
    }
    for (u32 i = 0; i < adapter_count; i++) {
        uacpi_u64 value;
        if (uacpi_eval_simple_integer(adapters[i], "_PSR", &value) == UACPI_STATUS_OK && value <= 1) {
            if (value || *ac < 0) *ac = (i32)value;
        }
    }
    return ok;
}
