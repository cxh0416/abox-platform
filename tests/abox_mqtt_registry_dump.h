#ifndef ABOX_MQTT_REGISTRY_DUMP_H
#define ABOX_MQTT_REGISTRY_DUMP_H
#include <stdio.h>
#include "abox_mqtt_registry.h"
/* Host-only export of the actual production tables. No fixture-owned copy of
 * the operation policy, matcher, report descriptor or Schema is synthesized. */
static int ABoxMqttRegistry_DumpBytes(void *context, const char *bytes, size_t size)
{ return fwrite(bytes,1,size,(FILE *)context)==size; }
static int ABoxMqttRegistry_Dump(const ABoxMqttRegistryContract *registry)
{
    if (!ABoxMqttRegistry_Describe(registry,ABoxMqttRegistry_DumpBytes,stdout)) return 0;
    return fputc('\n',stdout)!=EOF;
}
#endif
