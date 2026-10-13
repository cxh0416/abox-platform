#ifndef PRODUCT_CONFIG_STUB_H
#define PRODUCT_CONFIG_STUB_H
typedef struct { const char *ota_origin; } ProductConfigStub;
typedef ProductConfigStub VcuRemoteConfig_t;
typedef ProductConfigStub PatrolRemoteConfigV5_t;
const ProductConfigStub *VcuConfig_Get(void);
const ProductConfigStub *PatrolConfig_Get(void);
#endif
