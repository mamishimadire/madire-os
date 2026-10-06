/* Built-in user program images (see userimg.h). */
#include "kernel/memory/pte.h"
#include "kernel/memory/vm_layout.h"
#include "kernel/proc/userimg.h"

static bool name_ok(const char *name)
{
    if (!name || !name[0])
        return false;
    unsigned n = 0;
    for (; name[n]; n++) {
        char c = name[n];
        if (n >= USERIMG_NAME_MAX)
            return false;
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
            return false;
    }
    return true;
}

static bool same_name(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

enum userimg_result userimg_check(const struct userimg *img)
{
    if (!name_ok(img->name))
        return USERIMG_ERR_NAME;
    if (!img->text || img->text_size == 0 || img->text_size > USERIMG_TEXT_MAX)
        return USERIMG_ERR_TEXT;
    if (img->data_size > img->data_mem || img->data_mem > USERIMG_DATA_MAX ||
        (img->data_size && !img->data))
        return USERIMG_ERR_DATA;
    if (img->entry < VM_USER_TEXT || img->entry - VM_USER_TEXT >= img->text_size)
        return USERIMG_ERR_ENTRY;
    return USERIMG_OK;
}

enum userimg_result userimg_check_table(const struct userimg *imgs, uint64_t count, uint64_t *bad)
{
    for (uint64_t i = 0; i < count; i++) {
        enum userimg_result r = userimg_check(&imgs[i]);
        if (r == USERIMG_OK)
            for (uint64_t j = 0; j < i; j++)
                if (same_name(imgs[i].name, imgs[j].name))
                    r = USERIMG_ERR_DUPLICATE;
        if (r != USERIMG_OK) {
            if (bad)
                *bad = i;
            return r;
        }
    }
    return USERIMG_OK;
}

const struct userimg *userimg_find(const struct userimg *imgs, uint64_t count, const char *name)
{
    if (!name)
        return 0;
    for (uint64_t i = 0; i < count; i++)
        if (imgs[i].name && same_name(imgs[i].name, name))
            return &imgs[i];
    return 0;
}

uint32_t userimg_pages(const struct userimg *img)
{
    return (uint32_t)(vm_ceil(img->text_size) / VM_PAGE_SIZE + vm_ceil(img->data_mem) / VM_PAGE_SIZE +
                      VM_USER_STACK_SIZE / VM_PAGE_SIZE);
}

const char *userimg_result_str(enum userimg_result result)
{
    switch (result) {
    case USERIMG_OK:            return "ok";
    case USERIMG_ERR_NAME:      return "bad name";
    case USERIMG_ERR_TEXT:      return "bad code size";
    case USERIMG_ERR_DATA:      return "bad data size";
    case USERIMG_ERR_ENTRY:     return "entry point outside the code";
    case USERIMG_ERR_DUPLICATE: return "duplicate name";
    }
    return "unknown";
}
