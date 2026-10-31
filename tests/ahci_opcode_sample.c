/*
 * 1.4 stage 4, test only: the negative sample of part static's check
 * "[1.4] AHCI read only (madire.elf)". It is compiled on the test machine
 * with the kernel's code-generation flags into build/ahci-sample.o and is
 * never linked into the kernel. It holds what the check must catch, stored
 * the way kernel/storage/ahci_core.c stores a command's opcode (a byte
 * written into the FIS, volatile): a WRITE DMA EXT (0x35) and a FLUSH
 * CACHE EXT (0xEA), each in a function whose name starts with ahci_, and
 * an opcode table whose second byte is 0x35. A third function stores READ
 * DMA EXT (0x25), which the check must not flag. Madire's own code; no
 * command here is ever sent to a device.
 */
#include <stdint.h>

static const uint8_t ahci_opcodes[2] __attribute__((used)) = { 0xECu, 0x35u };

void ahci_sample_write(volatile uint8_t *fis);
void ahci_sample_flush(volatile uint8_t *fis);
void blk_sample_read(volatile uint8_t *fis);

void ahci_sample_write(volatile uint8_t *fis)
{
    fis[0] = 0x27u;
    fis[1] = 0x80u;
    fis[2] = 0x35u;         /* WRITE DMA EXT */
}

void ahci_sample_flush(volatile uint8_t *fis)
{
    fis[0] = 0x27u;
    fis[1] = 0x80u;
    fis[2] = 0xEAu;         /* FLUSH CACHE EXT */
}

void blk_sample_read(volatile uint8_t *fis)
{
    fis[0] = 0x27u;
    fis[1] = 0x80u;
    fis[2] = 0x25u;         /* READ DMA EXT: not flagged */
}
