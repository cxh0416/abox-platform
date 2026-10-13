#ifndef ABOX_MQTT_RUNTIME_H
#define ABOX_MQTT_RUNTIME_H
#include "abox_ec800_at.h"
#include "abox_ec800_command_port.h"
#include "abox_ec800_tls.h"
#include "abox_mqtt_tls.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define ABOX_MQTT_RUNTIME_HOST_SIZE 64U
#define ABOX_MQTT_RUNTIME_USERNAME_SIZE 32U
#define ABOX_MQTT_RUNTIME_PASSWORD_SIZE 64U
typedef struct {
  const char *host;
  uint16_t port;
  const char *username;
  const char *password;
  uint8_t tls_enabled;
  uint8_t tls_profile_id;
  ABoxTlsCredentials tls_credentials;
} ABoxMqttConfig;
typedef enum {
  ABOX_MQTT_RUNTIME_IDLE = 0,
  ABOX_MQTT_RUNTIME_DISCONNECT,
  ABOX_MQTT_RUNTIME_UNBIND,
  ABOX_MQTT_RUNTIME_WAIT_CA,
  ABOX_MQTT_RUNTIME_CLOCK,
  ABOX_MQTT_RUNTIME_PREPARE,
  ABOX_MQTT_RUNTIME_BIND,
  ABOX_MQTT_RUNTIME_PREPARED,
  ABOX_MQTT_RUNTIME_CONNECT,
  ABOX_MQTT_RUNTIME_CONNECTED,
  ABOX_MQTT_RUNTIME_READY,
  ABOX_MQTT_RUNTIME_FAILED,
  ABOX_MQTT_RUNTIME_STOP_WAIT_DRAIN,
  ABOX_MQTT_RUNTIME_STOP_DISCONNECT,
  ABOX_MQTT_RUNTIME_STOP_CLOSE,
  ABOX_MQTT_RUNTIME_STOP_UNBIND,
  ABOX_MQTT_RUNTIME_UNENROLLED,
  ABOX_MQTT_RUNTIME_BLOCKED,
  ABOX_MQTT_RUNTIME_PROBE_TLS,
  ABOX_MQTT_RUNTIME_CLOCK_MODE
} ABoxMqttRuntimeState;
typedef struct {
  void *user;
  void (*set_paused)(void *, uint8_t);
  void (*set_security_ready)(void *, uint8_t);
  int (*apply_config)(void *, const ABoxMqttConfig *);
  void (*mqtt_task)(void *);
  /* Quiesce and reset the transport after a modem RDY. The runtime gates it
   * off first, then reapplies the active configuration. */
  void (*mqtt_modem_reset)(void *);
  /* When supplied, transport owns QMTDISC/QMTCLOSE and drain. Return 1 only
   * after confirmed close, 0 while stopping, -1 when reuse is unsafe. */
  int (*mqtt_stop)(void *, uint32_t now_ms);
  uint8_t (*mqtt_ready)(void *);
  uint8_t (*mqtt_connected)(void *);
  uint8_t (*ca_ready)(void *);
  uint8_t (*time_valid)(void *);
  void (*observe_modem_line)(void *, const char *);
  void (*before_network_change)(void *);
  void (*state_changed)(void *, ABoxMqttRuntimeState, const char *);
  /* Optional product safety policy (restore/readback/credential latch).
   * False latches Block; a later true value alone does not unlock it. */
  uint8_t (*recovery_allowed)(void *);
} ABoxMqttRuntimePort;
/* Adapter for the common product MQTT_Core API. The table must outlive the
 * runtime; lifecycle callbacks may use port.user to recover product context. */
typedef struct {
  void (*set_paused)(uint8_t);
  void (*set_security_ready)(uint8_t);
  uint8_t (*apply_config)(const ABoxMqttConfig *);
  void (*task)(void);
  void (*modem_reset)(void);
  int (*stop)(uint32_t);
  uint8_t (*ready)(void);
  uint8_t (*connected)(void);
} ABoxMqttRuntimeCoreApi;
/* Fill only transport slots; the caller supplies CA/time/network policy. */
int ABoxMqttRuntimePort_BindCore(ABoxMqttRuntimePort *port,
                                  const ABoxMqttRuntimeCoreApi *api);
typedef struct {
  uint32_t owner, command_timeout_ms, tls_timeout_ms, connect_timeout_ms,
      subscribe_timeout_ms;
  const char *ca_file;
  uint32_t ca_revision;
  uint16_t ca_ciphersuite;
  uint8_t plain_client, tls_client, tls_context, tls_profile_id,
      supported_tls_context_mask;
  /* Opt in when modem TLS context capability must be observed at runtime.
   * The configured mask then limits acceptable observed contexts. */
  uint8_t probe_tls_capability;
  uint8_t query_clock_mode;
} ABoxMqttRuntimeOptions;
/* Standard STM32F105/EC800 wiring; callers still supply product lifecycle
 * callbacks and may override timeouts for a bench fault-injection build. */
void ABoxMqttRuntimeOptions_StandardEc800(ABoxMqttRuntimeOptions *,
                                          uint32_t owner,
                                          uint8_t tls_profile_id);
typedef struct {
  ABoxMqttRuntimePort callbacks;
  ABoxMqttRuntimeOptions options;
  ABoxEc800CommandAdapter adapter;
  ABoxEc800CommandPort command;
  ABoxEc800Tls tls;
  ABoxMqttTls mqtt_tls;
  ABoxTlsLease lease;
  uint64_t operation;
  ABoxMqttRuntimeState state;
  uint32_t started;
  uint8_t waiting, disconnect_step, active_tls, initialized, candidate_staged, recovering;
  uint8_t blocked, tls_probe_supported, tls_probe_complete, safety_locked;
  ABoxMqttConfig requested, active;
  char host[64], username[32], password[64];
  char active_host[64], active_username[32], active_password[64];
} ABoxMqttRuntime;
int ABoxMqttRuntime_Init(ABoxMqttRuntime *, ABoxEc800At *,
                         const ABoxMqttRuntimePort *,
                         const ABoxMqttRuntimeOptions *, const ABoxMqttConfig *,
                         uint32_t);
void ABoxMqttRuntime_Poll(ABoxMqttRuntime *, uint32_t);
int ABoxMqttRuntime_Stage(ABoxMqttRuntime *, const ABoxMqttConfig *);
int ABoxMqttRuntime_Activate(ABoxMqttRuntime *, uint32_t);
int ABoxMqttRuntime_Apply(ABoxMqttRuntime *, const ABoxMqttConfig *, uint32_t);
int ABoxMqttRuntime_Restore(ABoxMqttRuntime *, const ABoxMqttConfig *, uint32_t);
/* Returns 1 when fully stopped, 0 while stopping or waiting for AT to drain,
 * and -1 when cleanup is uncertain. Poll must continue after a 0 result. */
int ABoxMqttRuntime_StopFirst(ABoxMqttRuntime *, uint32_t);
void ABoxMqttRuntime_Block(ABoxMqttRuntime *);
/* Explicit Block is a safety latch. RecoveryComplete requires an explicit
 * verified external repair; RDY never clears it. Internal communication
 * blocks may recover after physical modem reset. */
int ABoxMqttRuntime_RecoveryComplete(ABoxMqttRuntime *);
uint8_t ABoxMqttRuntime_NeedsModemRecovery(const ABoxMqttRuntime *);
ABoxMqttRuntimeState ABoxMqttRuntime_GetState(const ABoxMqttRuntime *);
uint8_t ABoxMqttRuntime_IsTlsActive(const ABoxMqttRuntime *);
void ABoxMqttRuntime_OnModemReset(ABoxMqttRuntime *, uint32_t);
uint8_t ABoxMqttRuntime_IsReady(const ABoxMqttRuntime *);
uint8_t ABoxMqttRuntime_ActiveClient(const ABoxMqttRuntime *);
uint8_t ABoxMqttRuntime_ActiveContext(const ABoxMqttRuntime *);
const ABoxMqttConfig *ABoxMqttRuntime_ActiveConfig(const ABoxMqttRuntime *);
const ABoxMqttConfig *ABoxMqttRuntime_StagedConfig(const ABoxMqttRuntime *);
const ABoxMqttConfig *ABoxMqttRuntime_RequestedConfig(const ABoxMqttRuntime *);
#ifdef __cplusplus
}
#endif
#endif
