#include "abox_mqtt_registry.h"
#include <math.h>
#include <string.h>

static int discard(void *context, const char *bytes, size_t length)
{ (void)context; (void)bytes; (void)length; return 1; }
static int text(const char *s)
{ return s && *s && ABoxMqttDigest_WriteString(s,discard,NULL); }
static int field_valid(const char *s)
{
    size_t i;
    if (!s || strlen(s)>128) return 0;
    if (!strcmp(s,"data.command") || !strcmp(s,"data.params")) return 1;
    if (strncmp(s,"data.params.",12)) return 0;
    s+=12;
    while (*s) {
        if (!((*s>='a' && *s<='z') || (*s>='A' && *s<='Z') || *s=='_')) return 0;
        for (i=0;s[i] && s[i]!='.';++i)
            if (i>=64 || !((s[i]>='a' && s[i]<='z') || (s[i]>='A' && s[i]<='Z') ||
                (s[i]>='0' && s[i]<='9') || s[i]=='_')) return 0;
        s+=i;
        if (*s++==0) return 1;
        if (!*s) return 0;
    }
    return 0;
}
static int matcher_valid(const ABoxMqttMatcher *m, unsigned depth, unsigned *nodes)
{
    size_t i;
    if (!m || depth>ABOX_MQTT_MATCHER_MAX_DEPTH || ++*nodes>ABOX_MQTT_MATCHER_MAX_NODES) return 0;
    if (m->kind==ABOX_MATCH_EQUALS) {
        if (!field_valid(m->field) || m->count || m->children) return 0;
        switch (m->equals.type) {
        case ABOX_MATCH_STRING: return m->equals.value.string && ABoxMqttDigest_WriteString(m->equals.value.string,discard,NULL);
        case ABOX_MATCH_INTEGER: return m->equals.value.integer>=-9007199254740991LL && m->equals.value.integer<=9007199254740991LL;
        case ABOX_MATCH_BOOLEAN: return m->equals.value.boolean<=1;
        case ABOX_MATCH_NULL: return 1;
        case ABOX_MATCH_NUMBER: return isfinite(m->equals.value.number) &&
            !(m->equals.value.number==0 && signbit(m->equals.value.number));
        default: return 0;
        }
    }
    if ((m->kind!=ABOX_MATCH_ALL && m->kind!=ABOX_MATCH_ANY) || m->field ||
        !m->children || !m->count || m->count>ABOX_MQTT_MATCHER_MAX_NODES) return 0;
    for (i=0;i<m->count;++i) if (!matcher_valid(&m->children[i],depth+1,nodes)) return 0;
    return 1;
}
int ABoxMqttMatcher_Valid(const ABoxMqttMatcher *m)
{ unsigned nodes=0; return !m || matcher_valid(m,1,&nodes); }
static const cJSON *field(const cJSON *root, const char *path)
{
    char key[65];
    while (*path && root) {
        size_t length=0;
        while (path[length] && path[length]!='.') ++length;
        if (!length || length>=sizeof(key)) return NULL;
        memcpy(key,path,length); key[length]=0;
        root=cJSON_GetObjectItemCaseSensitive(root,key);
        path+=length;
        if (*path) ++path;
    }
    return root;
}
static int matches(const ABoxMqttMatcher *m, const cJSON *root)
{
    size_t i; const cJSON *value;
    if (!m) return 1;
    if (m->kind!=ABOX_MATCH_EQUALS) {
        for (i=0;i<m->count;++i) {
            int found=matches(&m->children[i],root);
            if (m->kind==ABOX_MATCH_ALL && !found) return 0;
            if (m->kind==ABOX_MATCH_ANY && found) return 1;
        }
        return m->kind==ABOX_MATCH_ALL;
    }
    value=field(root,m->field);
    if (!value) return 0; /* Missing is different from explicit null. */
    switch (m->equals.type) {
    case ABOX_MATCH_STRING: return cJSON_IsString(value) && !strcmp(value->valuestring,m->equals.value.string);
    case ABOX_MATCH_INTEGER: return cJSON_IsNumber(value) && value->valuedouble==(double)m->equals.value.integer;
    case ABOX_MATCH_NUMBER: return cJSON_IsNumber(value) && value->valuedouble==m->equals.value.number;
    case ABOX_MATCH_BOOLEAN: return cJSON_IsBool(value) && cJSON_IsTrue(value)==m->equals.value.boolean;
    case ABOX_MATCH_NULL: return cJSON_IsNull(value);
    default: return 0;
    }
}
int ABoxMqttMatcher_Matches(const ABoxMqttMatcher *m, const ABoxMqttRequest *r)
{ return r && r->root && ABoxMqttMatcher_Valid(m) && matches(m,r->root); }
static unsigned major(const char *version)
{
    unsigned value=0;
    while (*version && *version!='.') value=value*10U+(unsigned)(*version++-'0');
    return value;
}
static uint32_t version_key(const char *version)
{
    uint32_t value=(uint32_t)major(version)<<16;
    const char *part=strchr(version,'.')+1; unsigned minor=0;
    while (*part) minor=minor*10U+(unsigned)(*part++-'0');
    return value|minor;
}
static int profile_valid(const ABoxMqttProfileContract *c)
{
    size_t i,j,k; int supported=0;
    const ABoxMqttProfileDescriptor *p;
    const ABoxMqttProfileDescriptor *profiles[1]; ABoxMqttV4Registry legacy={profiles,1};
    if (!c || !(p=c->descriptor) || !text(c->contract) || !c->supported_majors || !c->supported_major_count ||
        (c->operation_count && !c->operations) || (c->report_count && !c->reports)) return 0;
    profiles[0]=p;
    if (!ABoxMqttV4_RegistryValid(&legacy) || !ABoxMqttV4_NameValid(p->name) ||
        !ABoxMqttV4_ProfileVersionValid(p->version) || c->report_count!=p->report_count) return 0;
    for (i=0;i<c->supported_major_count;++i) {
        if (c->supported_majors[i]==major(p->version)) supported=1;
        for (j=0;j<i;++j) if (c->supported_majors[j]==c->supported_majors[i]) return 0;
    }
    if (!supported) return 0;
    for (i=0;i<c->operation_count;++i) {
        const ABoxMqttOperationContract *o=&c->operations[i]; const ABoxMqttOperation *r=&o->runtime;
        if (!r->command || !r->profile || strcmp(r->profile,p->name) || !strcmp(r->command,"sync_state") ||
            !ABoxMqttV4_NameValid(r->command) || !ABoxMqttV4_FindCommand(p,r->command) || r->matches ||
            r->classification<ABOX_MQTT_COMMAND_QUERY || r->classification>ABOX_MQTT_COMMAND_SAFETY_STOP ||
            (unsigned)r->expiry>ABOX_MQTT_EXPIRY_REQUIRED ||
            (r->expiry==ABOX_MQTT_EXPIRY_FORBIDDEN && r->max_validity_ms) ||
            (r->expiry==ABOX_MQTT_EXPIRY_REQUIRED && !r->max_validity_ms) ||
            (r->classification==ABOX_MQTT_COMMAND_DISCRETE_ACTION &&
             (r->expiry!=ABOX_MQTT_EXPIRY_REQUIRED || r->max_validity_ms>30000U)) ||
            !ABoxMqttMatcher_Valid(o->matcher) || !ABoxMqttV4_ProfileVersionValid(o->introduced_version) ||
            version_key(o->introduced_version)>version_key(p->version) ||
            !text(o->semantic_description) || !text(o->success_boundary) || !text(o->reboot_policy) ||
            !o->params_schema || (o->error_count && !o->error_codes)) return 0;
        for (j=0;j<o->error_count;++j) {
            if (!o->error_codes[j] || o->error_codes[j]==200) return 0;
            for (k=0;k<j;++k) if (o->error_codes[k]==o->error_codes[j]) return 0;
        }
        for (j=0;j<i;++j) if (!strcmp(c->operations[j].runtime.command,r->command) &&
            (strcmp(c->operations[j].introduced_version,o->introduced_version) ||
             strcmp(c->operations[j].semantic_description,o->semantic_description))) return 0;
    }
    for (i=0;i<p->command_count;++i) {
        size_t found=0; ABoxMqttCommandClass projected=ABOX_MQTT_COMMAND_CORE;
        for (j=0;j<c->operation_count;++j) if (!strcmp(p->commands[i].name,c->operations[j].runtime.command)) {
            ABoxMqttCommandClass type=c->operations[j].runtime.classification;
            if (!found) projected=type;
            else if (projected!=type) projected=ABOX_MQTT_COMMAND_PARAMETERIZED;
            ++found;
        }
        if (!found || projected!=p->commands[i].classification) return 0;
    }
    for (i=0;i<c->report_count;++i) {
        const ABoxMqttReportContract *r=&c->reports[i]; const ABoxMqttReportDescriptor *d;
        if (!ABoxMqttV4_NameValid(r->name) || !(d=ABoxMqttV4_FindReport(p,r->name)) ||
            (strcmp(d->report_type,"state") && strcmp(d->report_type,"event") && strcmp(d->report_type,"telemetry")) ||
            !ABoxMqttV4_ProfileVersionValid(r->introduced_version) || version_key(r->introduced_version)>version_key(p->version) ||
            !text(r->semantic_description) || !r->payload_schema ||
            r->full_state>1 || r->instance_required>1 || r->strict_order>1 || r->request_correlation>1 ||
            (r->full_state && strcmp(d->report_type,"state"))) return 0;
        for (j=0;j<i;++j) if (!strcmp(c->reports[j].name,r->name)) return 0;
    }
    return 1;
}
int ABoxMqttRegistry_Valid(const ABoxMqttRegistryContract *r)
{
    size_t i,j,k,l;
    if (!r || r->count>16 || (r->count && !r->profiles)) return 0;
    for (i=0;i<r->count;++i) {
        if (!profile_valid(r->profiles[i])) return 0;
        for (j=0;j<i;++j) {
            if (!strcmp(r->profiles[i]->descriptor->name,r->profiles[j]->descriptor->name)) return 0;
            for (k=0;k<r->profiles[i]->report_count;++k) for (l=0;l<r->profiles[j]->report_count;++l)
                if (!strcmp(r->profiles[i]->reports[k].name,r->profiles[j]->reports[l].name)) return 0;
            for (k=0;k<r->profiles[i]->operation_count;++k) for (l=0;l<r->profiles[j]->operation_count;++l) {
                const ABoxMqttOperationContract *a=&r->profiles[i]->operations[k], *b=&r->profiles[j]->operations[l];
                if (!strcmp(a->runtime.command,b->runtime.command) &&
                    (strcmp(a->introduced_version,b->introduced_version) || strcmp(a->semantic_description,b->semantic_description))) return 0;
            }
        }
    }
    return 1;
}
uint16_t ABoxMqttRegistry_Resolve(const ABoxMqttRegistryContract *registry,
    const ABoxMqttRequest *r, const ABoxMqttOperation **operation)
{
    size_t i,j,found=0; int known=0; const ABoxMqttOperation *selected=NULL;
    if (operation) *operation=NULL;
    if (!r || !r->command || !operation || !ABoxMqttRegistry_Valid(registry)) return 4001;
    if (!strcmp(r->command,"sync_state")) return ABoxMqttRequest_Resolve(r,NULL,0,operation);
    for (i=0;i<registry->count;++i) for (j=0;j<registry->profiles[i]->operation_count;++j) {
        const ABoxMqttOperationContract *o=&registry->profiles[i]->operations[j];
        if (strcmp(o->runtime.command,r->command)) continue;
        known=1;
        if (ABoxMqttMatcher_Matches(o->matcher,r)) { selected=&o->runtime; ++found; }
    }
    if (!known) return 4002;
    if (found!=1) return 4001;
    return ABoxMqttRequest_Resolve(r,selected,1,operation);
}

typedef struct { ABoxCanonicalWriter write; void *context; int ok; } Writer;
static void raw(Writer *w, const char *s)
{ if (w->ok && !w->write(w->context,s,strlen(s))) w->ok=0; }
static void string(Writer *w, const char *s)
{ if (w->ok && !ABoxMqttDigest_WriteString(s,w->write,w->context)) w->ok=0; }
static void number(Writer *w, uint32_t n)
{ char s[21]; ABoxMqttV4_FormatInteger(s,sizeof(s),n); raw(w,s); }
static const char *classification(ABoxMqttCommandClass c)
{
    static const char *const names[]={"CORE","QUERY","DISCRETE_ACTION","CONTINUOUS_SESSION","SAFETY_STOP","PARAMETERIZED"};
    return names[c];
}
static void matcher(Writer *w, const ABoxMqttMatcher *m)
{
    size_t i;
    if (!w->ok) return;
    if (!m) { raw(w,"null"); return; }
    if (m->kind!=ABOX_MATCH_EQUALS) {
        raw(w,m->kind==ABOX_MATCH_ALL ? "{\"allOf\":[" : "{\"anyOf\":[");
        for (i=0;i<m->count;++i) { if (i) raw(w,","); matcher(w,&m->children[i]); }
        raw(w,"]}"); return;
    }
    raw(w,"{\"field\":"); string(w,m->field); raw(w,",\"equals\":");
    switch (m->equals.type) {
    case ABOX_MATCH_STRING: string(w,m->equals.value.string); break;
    case ABOX_MATCH_INTEGER:
        if (!ABoxMqttDigest_WriteInteger(m->equals.value.integer,w->write,w->context)) w->ok=0;
        break;
    case ABOX_MATCH_NUMBER:
        if (!ABoxMqttDigest_WriteNumber(m->equals.value.number,w->write,w->context)) w->ok=0;
        break;
    case ABOX_MATCH_BOOLEAN: raw(w,m->equals.value.boolean ? "true" : "false"); break;
    case ABOX_MATCH_NULL: raw(w,"null"); break;
    }
    raw(w,"}");
}
static int schema_number(void *context, const cJSON *root, const cJSON *number_value)
{ (void)context; (void)root; (void)number_value; return 1; }
static void schema(Writer *w, const char *s)
{
    cJSON *tree;
    if (!w->ok) return;
    if (!s) { raw(w,"null"); return; }
    tree=ABoxMqttDigest_ParseProfile(s,strlen(s),schema_number,NULL);
    if ((!cJSON_IsObject(tree) && !cJSON_IsBool(tree)) ||
        !ABoxMqttDigest_Canonical(tree,w->write,w->context)) w->ok=0;
    cJSON_Delete(tree);
}
static void operation(Writer *w, const ABoxMqttOperationContract *o)
{
    static const char *const expiry[]={"forbidden","optional","required"}; size_t i;
    raw(w,"{\"operationMatcher\":"); matcher(w,o->matcher);
    raw(w,",\"classification\":"); string(w,classification(o->runtime.classification));
    raw(w,",\"expiresAt\":"); string(w,expiry[o->runtime.expiry]);
    raw(w,",\"maxValidity\":"); number(w,o->runtime.max_validity_ms);
    raw(w,",\"params\":"); schema(w,o->params_schema);
    raw(w,",\"result\":"); schema(w,o->result_schema);
    raw(w,",\"200Boundary\":"); string(w,o->success_boundary);
    raw(w,",\"rebootPolicy\":"); string(w,o->reboot_policy);
    raw(w,",\"errorCodes\":[");
    for (i=0;i<o->error_count;++i) { if (i) raw(w,","); number(w,o->error_codes[i]); }
    raw(w,"]}");
}
int ABoxMqttRegistry_Describe(const ABoxMqttRegistryContract *registry, ABoxCanonicalWriter write, void *context)
{
    size_t i,j,k; Writer w={write,context,1};
    if (!write || !ABoxMqttRegistry_Valid(registry)) return 0;
    raw(&w,"{\"schemaVersion\":2,\"protocolVersion\":\"4.0\",\"profiles\":[");
    for (i=0;i<registry->count;++i) {
        const ABoxMqttProfileContract *c=registry->profiles[i]; const ABoxMqttProfileDescriptor *p=c->descriptor;
        if (i) raw(&w,",");
        raw(&w,"{\"name\":"); string(&w,p->name); raw(&w,",\"currentVersion\":"); string(&w,p->version);
        raw(&w,",\"contract\":"); string(&w,c->contract); raw(&w,",\"supportedMajors\":[");
        for (j=0;j<c->supported_major_count;++j) { if (j) raw(&w,","); number(&w,c->supported_majors[j]); }
        raw(&w,"],\"commands\":[");
        for (j=0;j<p->command_count;++j) {
            const ABoxMqttOperationContract *first=NULL; size_t emitted=0;
            for (k=0;k<c->operation_count;++k) if (!strcmp(c->operations[k].runtime.command,p->commands[j].name)) { first=&c->operations[k]; break; }
            if (j) raw(&w,",");
            raw(&w,"{\"name\":"); string(&w,p->commands[j].name);
            raw(&w,",\"introducedVersion\":"); string(&w,first->introduced_version);
            raw(&w,",\"semanticDescription\":"); string(&w,first->semantic_description);
            raw(&w,",\"classification\":"); string(&w,classification(p->commands[j].classification));
            raw(&w,",\"applicableProfiles\":[");
            for (k=0;k<registry->count;++k) if (ABoxMqttV4_FindCommand(registry->profiles[k]->descriptor,p->commands[j].name)) {
                if (emitted++) raw(&w,",");
                string(&w,registry->profiles[k]->descriptor->name);
            }
            raw(&w,"],\"operations\":["); emitted=0;
            for (k=0;k<c->operation_count;++k) if (!strcmp(c->operations[k].runtime.command,p->commands[j].name)) {
                if (emitted++) raw(&w,",");
                operation(&w,&c->operations[k]);
            }
            raw(&w,"]}");
        }
        raw(&w,"],\"reports\":[");
        for (j=0;j<c->report_count;++j) {
            const ABoxMqttReportContract *r=&c->reports[j]; const ABoxMqttReportDescriptor *d=ABoxMqttV4_FindReport(p,r->name);
            if (j) raw(&w,",");
            raw(&w,"{\"name\":"); string(&w,r->name);
            raw(&w,",\"ownerProfile\":"); string(&w,d->profile);
            raw(&w,",\"reportType\":"); string(&w,d->report_type);
            raw(&w,",\"introducedVersion\":"); string(&w,r->introduced_version);
            raw(&w,",\"semanticDescription\":"); string(&w,r->semantic_description);
            raw(&w,",\"operationMatcher\":{\"field\":\"data.reportName\",\"equals\":"); string(&w,d->name); raw(&w,"}");
            raw(&w,",\"payload\":"); schema(&w,r->payload_schema);
            raw(&w,",\"fullState\":"); raw(&w,r->full_state ? "true" : "false");
            raw(&w,",\"instanceId\":"); string(&w,r->instance_required ? "required" : "forbidden");
            raw(&w,",\"stream\":"); string(&w,r->strict_order ? "strict" : "none");
            raw(&w,",\"requestCorrelation\":"); raw(&w,r->request_correlation ? "true" : "false"); raw(&w,"}");
        }
        raw(&w,"]}");
    }
    raw(&w,"]}"); return w.ok;
}
