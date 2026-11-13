#ifndef ABOX_TARGET_IDENTITY_H
#define ABOX_TARGET_IDENTITY_H
#include "abox_enrollment_identity.h"
int ABoxTargetIdentity_Build(ABoxEnrollmentIdentity *identity,
    ABoxEnrollmentIdentityStorage *storage, const char *iccid,
    const char *configured_vid, uint8_t vid_assigned, const char *fallback_prefix,
    const char *hardware_contract, const char *app_version);
#endif
