#include "abox_clock.h"
#include "abox_mqtt_v4.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    ABoxClock c;
    ABoxClockSnapshot s;
    uint64_t utc, local;
    char integer[21];
    const uint64_t t = 1800000000000ULL;
    assert(ABoxMqttV4_FormatInteger(integer,sizeof(integer),UINT64_MAX));
    assert(!strcmp(integer,"18446744073709551615"));
    assert(!ABoxMqttV4_FormatInteger(integer,20,UINT64_MAX) && !integer[0]);
    assert(ABoxMqttV4_FormatInteger(integer,sizeof(integer),0) && !strcmp(integer,"0"));
    ABoxClock_Init(&c);
    assert(ABoxClock_Snapshot(&c,0).quality == ABOX_CLOCK_INVALID);
    s = ABoxClock_Snapshot(&c,0); assert(ABoxClock_ReportedUtcMs(&s) == 0);
    assert(ABoxClock_ReportedUtcMs(NULL) == 0);
    assert(ABoxClock_ParseCCLK("+CCLK: \"26/09/30,08:00:00+32\"",1,&local));
    assert(ABoxClock_ParseCCLK("+CCLK: \"26/09/30,00:00:00+00\"",0,&utc));
    assert(local == utc);
    assert(ABoxClock_ParseCCLK("+CCLK: \"26/09/30,08:00:00+32\"",0,&local));
    assert(local == utc + 28800000ULL); /* UTC calendar mode never subtracts TZ. */
    assert(ABoxClock_ParseCCLK("+CCLK: \"26/09/29,20:00:00-16\"",1,&local));
    assert(local == utc);
    assert(!ABoxClock_ParseCCLK("+CCLK: \"26/02/29,00:00:00+00\"",0,&utc));
    assert(!ABoxClock_ParseCCLK("+CCLK: \"26/09/30,00:00:00+57\"",0,&utc));
    assert(!ABoxClock_ParseCCLK("+CCLK: \"26/09/30,00:00:00\"",0,&utc));
    assert(ABoxClock_Bootstrap(&c,t,100));
    assert(ABoxClock_Snapshot(&c,200).quality == ABOX_CLOCK_BOOTSTRAP);
    s = ABoxClock_Snapshot(&c,200); assert(ABoxClock_ReportedUtcMs(&s) == 0);
    assert(!ABoxClock_Synchronize(&c,t,50,0,801));
    assert(ABoxClock_Synchronize(&c,t,50,100,300));
    s = ABoxClock_Snapshot(&c,300);
    assert(s.utc_ms == t+100 && s.error_ms == 151 && s.quality == ABOX_CLOCK_SYNCHRONIZED);
    assert(ABoxClock_ReportedUtcMs(&s) == t+100);
    assert(!ABoxClock_Bootstrap(&c,t+99999,400));
    assert(ABoxClock_Snapshot(&c,4000000).quality == ABOX_CLOCK_UNTRUSTED);
    s = ABoxClock_Snapshot(&c,4000000); assert(ABoxClock_ReportedUtcMs(&s) == 0);
    assert(ABoxClock_Snapshot(&c,5000000000ULL).utc_ms > t+4000000000ULL);
    assert(ABoxClock_Synchronize(&c,t+150,10,300,500));
    s = ABoxClock_Snapshot(&c,500);
    assert(s.utc_ms == t+300); /* -50 ms slews, no backwards step. */
    assert(ABoxClock_Snapshot(&c,50500).utc_ms == t+50250);
    assert(ABoxMqttV4_RequestLifetimeValid(t,t+10000,10000));
    assert(!ABoxMqttV4_RequestLifetimeValid(t,t+10001,10000));
    assert(!ABoxMqttV4_RequestLifetimeValid(t,t,10000));
    assert(!ABoxMqttV4_RequestLifetimeValid(t,t-1,10000));
    assert(!ABoxMqttV4_RequestLifetimeValid(0,UINT64_MAX,0));
    s.utc_ms = t-800; s.error_ms = 50; s.quality = ABOX_CLOCK_SYNCHRONIZED;
    assert(ABoxMqttV4_CheckExpiry(&s,t+10000) == 200);
    s.utc_ms = t+10000; assert(ABoxMqttV4_CheckExpiry(&s,t+10000) == 4005);
    s.utc_ms -= 30; assert(ABoxMqttV4_CheckExpiry(&s,t+10000) == 4003);
    s.quality = ABOX_CLOCK_UNTRUSTED;
    assert(ABoxMqttV4_CheckExpiry(&s,t+10000) == 4003);
    ABoxClock budget;
    ABoxClock_Init(&budget);
    assert(ABoxClock_ReferenceErrorMs(30,1000,1478)==270);
    assert(ABoxClock_Synchronize(&budget,t,30,1000,1478));
    assert(ABoxClock_Snapshot(&budget,1478).error_ms==270);
    assert(ABoxClock_ReferenceErrorMs(50,0,800)==451);
    assert(ABoxClock_Synchronize(&budget,t,50,0,800));
    assert(!ABoxClock_ReferenceErrorMs(51,0,800));
    assert(!ABoxClock_Synchronize(&budget,t,250,0,800));
    assert(!ABoxClock_ReferenceErrorMs(0,0,801));
    assert(!ABoxClock_ReferenceErrorMs(0,1000,999));
    puts("clock/lifetime tests passed");
    return 0;
}
