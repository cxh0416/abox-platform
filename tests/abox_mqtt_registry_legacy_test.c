#undef NDEBUG
#include <assert.h>
#include <string.h>
#include "abox_mqtt_profile_dump.h"
static const ABoxMqttCommandDescriptor commands[]={{"query_state","fixture_query",ABOX_MQTT_COMMAND_QUERY}};
static const ABoxMqttReportDescriptor reports[]={{"query_status","fixture_query","state"}};
static const ABoxMqttProfileDescriptor profile={"fixture_query","1.0",commands,1,reports,1};
int main(int argc,char **argv)
{
    if (argc==2 && !strcmp(argv[1],"--describe")) { ABoxMqttProfile_Dump(&profile);return 0; }
    const ABoxMqttProfileDescriptor *profiles[]={&profile};ABoxMqttV4Registry registry={profiles,1};
    assert(ABoxMqttV4_RegistryValid(&registry));return 0;
}
