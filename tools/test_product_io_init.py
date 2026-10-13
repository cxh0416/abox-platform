"""Compile actual product UART and enrollment Init functions with failed ports.

The shared state machines are covered by native component tests. This checks
each product's boundary adapter without linking peripherals or actuators.
"""
import argparse
from pathlib import Path
import re
import shutil
import subprocess


def function(source: str, name: str) -> str:
    match = re.search(r"^(?:static )?(?:int|uint8_t) " + re.escape(name) +
                      r"\([^;]*?\)\s*\{", source, re.MULTILINE)
    if not match:
        raise AssertionError("missing checked adapter: " + name)
    start = source.index("{", match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


parser = argparse.ArgumentParser()
parser.add_argument("--product-root", type=Path, required=True)
product = parser.parse_args().product_root.resolve()
platform = product / "platform"
build = product / "app/build/host-tests/product-io-init"
build.mkdir(parents=True, exist_ok=True)
cloud = product / "app/Core/User/cloud/src/cloud_service.c"
meal = not cloud.exists()
patrol = (product / "app/Core/User/cloud/src/patrol_enrollment.c").exists()
source = (product / "app/Core/Src/main.c" if meal else cloud).read_text(encoding="utf-8")
rx_name = "g_ec800_rx" if meal or not patrol else "g_cloud.ec800_rx"
uart = """
#include "abox_ec800_rx.h"
#include <assert.h>
#include <string.h>
#define HAL_OK 0
#define UART1_RX_BUF_SZ 1088U
#define APP_LOGE(...) ((void)++logs)
typedef int UART_HandleTypeDef;
UART_HandleTypeDef huart1;
ABoxEc800Rx g_ec800_rx;
struct { ABoxEc800Rx ec800_rx; uint8_t uart_dma[1088]; } g_cloud;
uint8_t uart1_rx_buf[1088], g_uart_dma[1088];
static unsigned stops, starts, logs;
static int stop_status, start_status;
static int HAL_UART_DMAStop(UART_HandleTypeDef *u)
{ (void)u; ++stops; return stop_status; }
static int HAL_UART_AbortReceive(UART_HandleTypeDef *u)
{ return HAL_UART_DMAStop(u); }
static int HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *u, uint8_t *b, uint16_t n)
{ (void)u; assert(b && n>=1088); ++starts; return start_status; }
"""
if not meal:
    uart += function(source, "uart_start_rx") + "\n"
uart += function(source, "app_restart_rx") + "\n"
uart += """
int main(void)
{
    uint8_t dma[1088], ring[1152];
    ABoxEc800Rx *rx=&RX_NAME;
    assert(ABoxEc800Rx_Init(rx,dma,sizeof(dma),ring,sizeof(ring)));
    rx->head=44; rx->tail=40; rx->dma_position=32;
    stop_status=1;
    assert(!app_restart_rx(NULL));
    assert(stops==1 && !starts && rx->head==44 && rx->tail==40 && rx->dma_position==32);
    stop_status=0; start_status=1;
    assert(!app_restart_rx(NULL));
    assert(stops==2 && starts==1 && rx->head==0 && rx->tail==0 && !rx->dma_position);
    start_status=0;
    assert(app_restart_rx(NULL));
    assert(stops==3 && starts==2 && logs==2);
    assert(ABoxEc800Rx_OnDmaEvent(rx,3));
    assert(rx->head==3 && rx->dma_position==3);
    return 0;
}
""".replace("RX_NAME", rx_name)

enroll_path = product / "app/Core/User/cloud/src" / (
    "patrol_enrollment.c" if patrol else "abox_enrollment.c")
enroll_name = "MealEnrollment_Init" if meal else "PatrolEnrollment_Init" if patrol else "SweeperEnrollment_Init"
enroll = """
#include "abox_enrollment_service.h"
#include "abox_ec800_at_legacy.h"
#include <assert.h>
#define ENROLL_OWNER 18U
#define ENROLL_ORIGIN "https://enroll"
#define BOARD_CONFIG_MQTT_TLS_PROFILE 1U
#define PATROL_MQTT_TLS_PROFILE_DEFAULT 0U
static ABoxEnrollmentService g_service;
static char g_url[192],g_body[512],g_response[1025],g_request_id[48],g_poll_token[64];
static char *g_migration_request;
static const uint8_t abox_enrollment_ca_pem[]="CA";
static struct { char last_response_payload[192],response_payload[512],tx_payload[769]; } g_cloud;
static const struct { const char *ota_origin; } config={"https://ota"};
#define PatrolConfig_Get() (&config)
static void CloudService_EnrollmentScratch(char **u,char **b,char **r,char **i,char **t)
{ *u=g_url; *b=g_body; *r=g_response; *i=g_request_id; *t=g_poll_token; }
static uint32_t now_ms(void *u) { (void)u; return 0; }
static int network_ready(void *u) { (void)u; return 0; }
static int needs_enrollment(void *u) { (void)u; return 1; }
#define needs_enrollment_port needs_enrollment
static int identity(void *u,ABoxEnrollmentIdentity *i) { (void)u; (void)i; return 0; }
static void mqtt_pause(void *u,uint8_t p) { (void)u; (void)p; }
static int start_trial(void *u,const ABoxEnrollmentCredential *c) { (void)u; (void)c; return 0; }
static int trial_result(void *u) { (void)u; return 0; }
static void cancel_trial(void *u) { (void)u; }
static int init_status,ca_status;
static unsigned claims,ca_calls;
int ABoxEnrollmentService_Init(ABoxEnrollmentService *s,ABoxEc800At *at,ABoxEc800Owner o,
    const char *ca,const char *origin,const ABoxEnrollmentBuffers *b,uint8_t profile,
    const ABoxEnrollmentServicePort *p)
{
    (void)profile; assert(at && o==18 && ca && origin && b->url && b->body && b->response && p->identity);
    ++claims; s->initialized=init_status; return init_status;
}
int ABoxEnrollmentService_RequirePsk(ABoxEnrollmentService *s,const uint8_t *pem,uint32_t n)
{ assert(s->initialized && pem && n==2); ++ca_calls; s->initialized=ca_status; return ca_status; }
"""
enroll += function(enroll_path.read_text(encoding="utf-8"), enroll_name) + "\n"
enroll += """
int main(void)
{
    ABoxEc800At at={0};
    assert(!ENROLL_INIT(&at) && claims==1 && !ca_calls && !g_service.initialized);
    init_status=1;
""".replace("ENROLL_INIT", enroll_name)
if not patrol:
    enroll += "assert(!" + enroll_name + "(&at) && ca_calls==1 && !g_service.initialized);\n"
enroll += "ca_status=1; assert(" + enroll_name + "(&at) && g_service.initialized); return 0; }\n"

for name, contents in (("uart", uart), ("enrollment", enroll)):
    harness = build / (name + ".c")
    harness.write_text(contents, encoding="utf-8", newline="\n")
    exe = build / (name + ".exe")
    includes = [platform / "components" / p / "include" for p in ("core", "ec800", "enrollment", "mqtt")]
    command = [shutil.which("gcc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
               "-Wno-unused-variable", "-Wno-unused-function",
               *["-I" + str(p) for p in includes], str(harness)]
    if name == "uart":
        command += [str(platform / "components/ec800/src/abox_ec800_rx.c")]
    subprocess.run(command + ["-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("Product UART/Enrollment failure injection: PASS (" + product.name + ")")
