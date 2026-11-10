#include "abox_mqtt_trial.h"
#ifdef PRODUCT_SWEEPER
#include "abox_ec800_diagnostics.h"
/* This recovery fixture uses certificate TLS and has no diagnostics instance. */
static inline const ABoxEc800Diagnostics *CloudService_Ec800Diagnostics(void) { return 0; }
#endif
void CloudService_NetworkChanged(void);
void CloudTx_NetworkChanged(void);
ABoxMqttTrialState CloudService_EnrollmentTrialState(void);
uint8_t CloudService_RuntimeRecoveryAllowed(void);
