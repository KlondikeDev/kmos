<!-- This file has been generated in whole or in part by a Large Language Model -->

Upstream: https://github.com/uACPI/uACPI

This copy identifies itself as uACPI 7.0.0 in include/uacpi/uacpi.h.
Source and headers are unmodified upstream code, licensed under the included
MIT LICENSE. Kunix-specific types and host callbacks live in include/uacpi_types.h
and kernel/uacpi_host.c. The battery/EC driver is kernel/battery_acpi.c.

The interpreter is entered only by the kernel task, with a two-second AML loop
timeout. SCI work is polled with its PIC line masked. This is initial polling
support, not an interrupt-driven ACPI event subsystem.
