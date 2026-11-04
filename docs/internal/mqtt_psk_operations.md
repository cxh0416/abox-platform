# ABox MQTT ECDHE-PSK 运维（内部）

内部资料，不作为业务对接协议对外交付。

普通固件同时保留 CA、PSK 和显式明文配置；生产端口与车辆配置须另行批准。
默认 EC800E PSK 路径使用 TLS 1.2 / `0xCCAC`
(`ECDHE-PSK-CHACHA20-POLY1305`)。启用模组自动识别的产品在检测到
`EC800ECNLCR01A10M04` 时使用 `0x00AE`（PSK-AES128-CBC-SHA256，
不具备前向保密），A18 使用 `0xCCAC`。未知版本暂停 PSK 入网和连接。`seclevel=0` 在此路径
表示不用 X.509，服务端身份由双方持有的逐设备 PSK 证明；不是匿名 TLS。
PSK 模式不依赖 CCLK、OTA CA 或证书时间。业务 UTC、误差边界、有效期与幂等照常运行。
CA 路径仍要求 CA 和时间。现有 RSA CA profile 显式选择证书套件 `0x009C`
(TLS_RSA_WITH_AES_128_GCM_SHA256)，排除复用 context 中残留 PSK 的协商；
模组实测拒绝空 PSK 清除命令，不能把这个命令加入 CA 准备流程。
该 CA 基线要求 RSA 服务端证书，ECDSA CA endpoint 须另配匹配的证书套件；
失败没有明文降级。

## 配置和秘密边界

`ABoxMqttConfig.tls_credentials` 包含 mode、identity、secret、generation；
CA=0 是零初始化默认值，PSK=1。产品配置 V2/V5 原有字段、长度和校验保持不变；
产品在 OTA 配置容器尾部追加 80 字节 `ABoxTlsSecretRecord`，与连接配置一次写入。
全 FF 表示历史配置/CA；损坏扩展阻止 TLS 建链，不能当作空记录绕过。
记录校验只检测损坏，不是加密或防篡改。配置页不会进入 App/Full；Boot 冻结。
普通 OTA 保存保留扩展；历史旧固件保存可能擦掉扩展，因此降级须先做受控恢复。

每设备一个随机 32 字符 ASCII 秘密，identity 最长 31 字符，generation 单调递增。
A18 模组接收原样 ASCII；A10 命令接收 ASCII 字节的十六进制编码。
stunnel 文件使用这些 ASCII 字节的十六进制编码；不能把
32 字符当成 16 字节 Hex。工具生成 URL-safe 随机字符，有 192 位随机熵。
禁止秘密出现在仓库、构建常量、命令行、普通日志和 get_info。AT owner 在路由前
丢弃 PSK/QMTCONN 的秘密回显。TLS runtime/试用必须在 RAM 保留必要秘密；
本方案不防物理 SWD/Flash 读取或已攻陷的 MCU、工位和服务端。
正式工位限制 SWD 和文件 ACL；读保护与可信升级沿用项目现有措施。

## 隔离服务

使用 `tools/mqtt_psk_files.py generate --identity <UID.gN> --generation N --output <私密工位目录>/device.json`。
密钥永不打印，输出不得放进 Git、dist 或普通证据目录。inventory 是设备 JSON 的数组。
`render --inventory <private.json> --output <private-directory> --listen-port <隔离端口>
--backend-port <回环Broker端口>` 输出 stunnel.conf 和 psk.secrets；目录 0700、文件 0600。
Windows 工位另用当前用户专属 ACL，不能认为 chmod 已提供 POSIX 等效保护。
按发行版正常安装 stunnel，独立 systemd 服务使用受限用户，执行
`stunnel <受保护的绝对配置路径>`；只监听批准端口，后端固定 127.0.0.1。
禁止把此模板直接覆盖生产服务配置。先配置独立 Broker 用户和逐 VID 精确 ACL。
PSK identity 不会自动成为 MQTT username；两层各自验证。V4 Topic/Payload/QoS 不变。

模板同时限制 TLS min/max 1.2、唯一套件、禁止会话恢复和 tickets，避免撤销后
旧会话缓存绕过新握手身份检查。正常运营不需要 debug=7 或 TLS keylog。
秘密文件不得进入 journal、crash dump 或常规备份。

## 首次注入和轮换

首次注入通过受控本地工位。可构建 `-DABOX_PSK_STATION=ON` 的维护 App，
使用匹配 ELF 和只绑定 127.0.0.1 的 J-Link GDB server；不改 Boot，不覆盖配置页。
`tools/mqtt_psk_station.py --bundle <private.json> --elf <maintenance.elf>
--gdb <arm-none-eabi-gdb> --port 2331` 仅向本地 RAM 邮箱注入候选；JSON 可包含
host、port、username、password（空字符串保留原字段）。先核对实机 UID/VID。
产品 Poll 把候选复制进已有 trial，工具先验证 RAM 写入读回，返回 accepted 仅表示受理，随后清空邮箱。
GDB server 不启用内存/协议转储日志。工位工具仅在 SWD 暂停时冻结看门狗，运行时照常计时。
巡防和清扫工位构建暂停 HTTP 入网，借用未入网时空闲的响应缓冲；试用开始后关闭该邮箱，
不复用已就绪设备的幂等响应缓存。送餐车使用独立工位缓冲。
工位固件须换回普通构建，并核验启动、实际 Broker 会话、订阅发布、平台授时、
配置读回和重启。普通构建默认 OFF，没有这个入口，不开放新的网络注入端点。

已认证 PSK 会话可通过现有 `/internal/config` V2 `internal_config`，
params.action=`set_mqtt_psk`，传 identity/secret/generation。端点、MQTT 账号、
VID 保持既有配置；202 发布确认后才试连。CA/明文网络入口拒绝此动作。
候选经过 MQTT 就绪、心跳/状态发布证明、ConfigStore 写入读回后提交；失败恢复
旧连接与秘密。不能用 QSSLCFG OK、TLS 成功或 202 判定迁移成功。
首次未配置设备失败返回未配置状态；恢复失败明确报告，需要本地处理。

服务端先同时保留旧/新身份，设备试连成功、提交并普通重启验证后再撤销旧身份。
更新受保护文件并重启独立 stunnel（或定向结束对应会话）；仅删除文件中的旧身份
不能撤销已经建立的 TLS 会话。最小部署可重启接入服务让全部设备重连，短暂影响
可用性须安排维护时间。泄露设备身份不得用新 MQTT 密码代替 PSK 撤销。
配置轮换结果不明先只读核对，不重复写。旧身份保留期间会扩大可用凭据集合，
确认后及时撤销。恢复仍依赖至少一个未泄露有效身份或受控本地工位。

## 验收证据

必须分别记录 TLS（实际 1.2/0xCCAC）、MQTT CONNECT、订阅发布、可信授时与业务结果。
错误 PSK/未知身份应在 TLS 层失败且后端无 MQTT 流量；正确 PSK/错误 MQTT 密码应
TLS 成功但 Broker 拒绝登录。重连、模组供电重启、MCU 重启、保存失败和轮换恢复
沿用现有测试。扫描日志和候选产物是否包含真实 secret 或其 Hex；证据只保留脱敏
结果、身份、generation、配置权限、握手包和服务日志，删除试验秘密/服务/映射。
该模式有前向保密；当前 PSK 泄露允许冒充该设备及其 Broker（给这台设备），
不会直接取得其他设备秘密。服务端全库存泄露影响所有已泄露身份，需要轮换。
