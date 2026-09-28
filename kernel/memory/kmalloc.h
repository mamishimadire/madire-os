#ifndef MADIRE_MEMORY_KMALLOC_H
#define MADIRE_MEMORY_KMALLOC_H

/*
 * Kernel heap: one heap at VM_HEAP_BASE (vm_layout.h), 64 KiB at start,
 * growing on demand (whole pages, at least 16 KiB per step) up to a 16 MiB
 * cap; it never shrinks. New heap pages are physical frames from the page
 * allocator, mapped through paging.c. Allocator: kernel/memory/heap.c.
 *
 *   kmalloc(0) returns NULL; running out (cap or physical memory) returns
 *   NULL, never panics. Payloads are 16-byte aligned.
 *   kcalloc(n, size) returns zeroed memory; NULL if n * size overflows.
 *   kfree(NULL) does nothing. Freeing a pointer that is not from the heap,
 *   is misaligned, was already freed, or finds the heap corrupted is a
 *   kernel bug: the crash screen shows the pointer and the reason.
 *
 * CONTEXT RULE: kmalloc, kcalloc and kfree may be called only from normal
 * kernel context with interrupts enabled - never from an IRQ handler or an
 * exception handler. The heap is not interrupt-safe (no locking).
 * Enforced: each call first checks EFLAGS.IF (irq_enabled()) and panics if
 * interrupts are disabled. Every IRQ and exception enters through an
 * interrupt gate, which clears IF, so any call from a handler is caught.
 * Limitations of this check:
 *   - it also refuses calls from normal code that has disabled interrupts
 *     (e.g. inside irq_save()/irq_restore()); that is intended, since the
 *     heap must not be used in such sections either;
 *   - it cannot catch a handler that re-enabled interrupts itself (no
 *     handler does in 0.6).
 */

#include <stdbool.h>
#include <stdint.h>
#include "kernel/memory/heap.h"

/* Maps the first 64 KiB and sets up the heap. Needs paging on.
 * false: heap disabled, kmalloc/kcalloc return NULL. */
bool kheap_init(void);

/* Boot self-test (about 200 mixed allocations, growth, no page leak).
 * false: the heap is disabled from then on. */
bool kheap_selftest(void);

void *kmalloc(uint32_t size);
void *kcalloc(uint32_t n, uint32_t size);
void kfree(void *p);

/* Current figures; arena_bytes == 0 means the heap is disabled. Runs a
 * full heap_check first: a corrupted heap ends on the crash screen. */
const struct heap_stats *kheap_stats(void);

#endif
