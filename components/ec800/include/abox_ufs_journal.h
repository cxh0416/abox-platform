#ifndef ABOX_UFS_JOURNAL_H
#define ABOX_UFS_JOURNAL_H
#include <stddef.h>
#include "abox_ec800_ufs.h"
/* Fixed-size A/B files; payload codec and namespace are supplied by the owner.
 * Buffer is borrowed for the entire transaction; its final byte is reserved
 * for EOF/overlong-file detection. No private snapshot copy. */
typedef struct {
    void *context;
    int (*decode)(void *, const uint8_t *, size_t); /* NULL,0 only for proven first use. */
    int (*encode)(void *, uint8_t *, size_t, size_t *);
    int (*initialized)(void *); /* 0: virgin marker; 1: used; -1: damaged/unknown. */
    int (*mark_initialized)(void *); /* Durable marker, verified by the owner. */
    int (*acquire)(void *);
    void (*release)(void *);
} ABoxUfsJournalPort;
typedef struct {
    ABoxEc800Ufs io;
    ABoxUfsJournalPort port;
    const char *paths[2];
    uint8_t *buffer;
    size_t size, payload_capacity;
    uint32_t sequence, crc, expected_crc, started;
    uint8_t state, slot, current, found, absent, leased, copies, submitted;
} ABoxUfsJournal;
int ABoxUfsJournal_Init(ABoxUfsJournal *journal, const ABoxEc800UfsPort *files,
    const ABoxUfsJournalPort *port, const char *path_a, const char *path_b,
    uint8_t *buffer, size_t buffer_size);
/* Load/recover or save. A declined resource acquisition is retryable. */
int ABoxUfsJournal_Load(ABoxUfsJournal *journal, uint32_t now);
int ABoxUfsJournal_Save(ABoxUfsJournal *journal, uint32_t now);
void ABoxUfsJournal_Poll(ABoxUfsJournal *journal, uint32_t now);
int ABoxUfsJournal_Status(const ABoxUfsJournal *journal); /* -1 failed, 0 pending, 1 verified */
/* Only after physical reset/drain; reload both files. FAILED stays latched
 * until an explicit owner-controlled Init, not on every modem recovery. */
void ABoxUfsJournal_OnModemReset(ABoxUfsJournal *journal);
#endif
