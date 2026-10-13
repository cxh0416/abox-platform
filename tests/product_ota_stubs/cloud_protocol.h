#include <stdint.h>
typedef struct { char ota_url[256], ota_version[32]; uint32_t ota_size, ota_crc32; } CloudProtocolRequest_t;
