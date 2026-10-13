# 初始化失败路径修复：第三阶段验证记录

日期：2026-10-13。范围：关键注册返回值、UART 失败处理和连接重试。
以第二阶段未提交工作区为基线，保留第一、第二阶段修改。未提交、未执行真实
OTA、运动控制或生产部署；这里只记录源码、Host 和候选 Release 构建证据。

## 公共平台修改

| 修复点 | 源码及调用链 | 失败后的行为 |
| --- | --- | --- |
| OTA 注册结果 | `boot_v2/abox_boot_v2_app.c:ABoxBootV2App_Init` → `abox_boot_v2_ec800_port.c:register_events` → `ABoxEc800At_Register` | 事件槽不足返回失败；本次启动成功标志保持零，Task、Start、查询和迟到回调保持不可用。 |
| Bridge 部分绑定 | `mqtt/abox_mqtt_ec800_bridge.c:ABoxMqttEc800Bridge_Init` → MQTT 初始化 → ICCID 初始化 | ICCID 注册失败时撤销本次 MQTT 事件绑定和 RX demux，关闭 transport 初始化标志；不得留下部分可用 Bridge。 |
| ICCID、诊断、Enrollment | 对应组件 Init → AT 注册；`enrollment/abox_enrollment_service.c` | 注册失败清空 AT 指针；Enrollment 本地参数先校验，再注册 HTTP。PSK/CA 配置失败统一释放注册槽并关闭服务。 |
| UART RX | `cloud/abox_app_service.c:ABoxAppService_Poll` → 产品 `app_restart_rx` | 必须先成功停止接收，才清 RX 游标并重新启动；失败按 128 ms 重试，暂停通信 Task/OTA/Enrollment/cloud，终止已接受 AT 操作。HAL 成功期间出现的错误中断请求也被保留。 |
| UART TX | `ec800/abox_ec800_at.c:start_next` / `ABoxEc800At_SendPayload` → `terminate_all` | 命令或载荷写失败可能已发出前缀，隔离 AT 会话；活动及排队命令各收到一次 SEND_FAIL。迟到 OK 不得完成新命令。 |
| MQTT 重试 | `mqtt/abox_mqtt_ec800.c:configure/opening/connecting/subscribing/closing` | 拒绝入队保持原阶段，在阶段期限内重试；期限已到不再发送新请求。已接受的命令按 AT 实际发出时间计时，排队由 AT 完成契约收敛；订阅 ID 仅在成功入队时提交。 |

AT 显式 Cancel 的原有静默取消语义保持不变。UART 恢复成功只代表 MCU 接收
可用，不代表模组已经复位；仍需 RDY 或已完成的电源周期作为物理复位证据。
Runtime 的 RESTORE_FAILED、配置回读失败、凭据不一致等锁定继续由第二阶段
恢复策略保护。失败报告不能解除安全锁定。

## 产品适配及兼容性

- 三产品 UART1 首次启动和错误重启统一交给 AppService；产品回调只返回
  HAL 停止/启动结果及本地日志，不各自实现模组恢复状态机。
- 清扫车检查 Enrollment 和诊断注册；送餐车检查 Enrollment、OpenIdem UFS；
  巡防检查 Enrollment。失败服务局部不可用，基础通信失败仍保持门控。
- 送餐车 `serial_bus.c` 增加 UART5 接收失败后的本地任务重试，检查配置过程
  中的 Abort/DeInit/Init。接收不可用时拒绝新锁帧；UART 发送失败不重发已结束
  的锁帧，不产生错误成功。错误 IRQ 只请求 RX 恢复，重试在总线任务执行。
- 三个实际平台工作区均为 `17d2389f8cfb5d81c1af9cd21ec750c08b279b5a` 加同一
  未提交公共补丁。清扫车和送餐车父仓库的待提交 gitlink 更新继续保留，巡防
  原 gitlink 已指向此提交。57 个公共变更文件逐字节一致，CMake 保留各仓库
  原有测试入口及公共主仓库的用户 JLink 修改。
- `ABoxBootV2AppPort.register_events` 和 `ABoxAppServicePort.restart_rx` 改为
  返回 int；旧 EC800 注册包装返回 uint8_t。所有三产品实际调用方已适配并
  构建验证，后续其他消费者须同步返回结果，不能继续使用 void 回调。

## 验证结果

| 仓库 / 候选版本 | 完整 Host | 最后针对性回归 | Release App Flash | 链接 RAM | BSS 到固定邮箱余量 |
| --- | --- | --- | ---: | ---: | ---: |
| ABox Platform | 58/58 | 12/12 | 不适用 | 不适用 | 不适用 |
| 清扫车 `sweeper-vcu-1.3.28` | 56/56 | 12/12 | 137140 B | 61432 B | 0 B |
| 送餐车 `meal-delivery-vehicle-1.1.55` | 57/57 | 12/12 | 138500 B | 56464 B | 8432 B |
| 巡防 `patrol-chassis-0.2.60` | 56/56 | 12/12 | 148936 B | 61432 B | 16 B |

相对第二阶段，App Flash 分别增加 672 / 992 / 560 B。清扫车、巡防 BSS 未
增长；公共 AppService 在 ARM 上仍为 16 B，新增标志与短重试计时使用原有
填充，并以编译期断言约束。送餐车 UART5 增加 8 B BSS。

固定邮箱 `0x2000BC40`、Boot/App Flash 边界、BootState/配置页保持原值。
三产品冻结 Boot SHA256 均保持
`981409ef107d5f5c56a1a80fce107c0292e2c1165668c0279ba6158f24a61c63`。
应用二进制已核对包含上述实际版本字符串。清扫车构建版本参数使用引号传递，
避免 PowerShell 截断点分版本。

静态栈预算校验全部通过。包含 1024 B 预留后，最大已声明调用链分别为
3420/4096、4144/6144、3824/4096 B。新增 RX 重试、错误日志、命令/载荷
终止调用链也纳入预算。邮箱未移动，MCU 安全任务及其栈未压缩。

其他回归已通过：清扫车 44 个 Python 用例及 CAN/AT/MQTT native 用例；
巡防产品套件、驱动/配置/bench native、启动中断交接及 OTA 初始化注入；
送餐车 MQTT adapter/双锁状态、配置提交回读、profile、Boot V2、UART DMA、
OTA 初始化，以及六组业务 native 用例。这里的 bench 是 Host 模拟，未连接
设备。协议字段、幂等持久记录布局及运动安全门控均未改动。

## 失败注入与长期入口

新增或扩展 CTest 覆盖：事件表满、Bridge 部分绑定、PSK/CA 配置失败、污染
`.ota_work` 后注册失败、UART stop/start 失败和重复重试、启动期间错误 IRQ、
16 位短计时/32 位 HAL tick 回绕、命令/载荷 TX 失败、重复终止、迟到响应、
MQTT 队列满与入队期限、订阅 ID、OTA/Enrollment/UFS 失败结束及资源归还。
第二阶段模组复位与 Runtime 恢复失败测试继续执行。

产品工作区的固定入口：

```powershell
./platform/tools/test_failure_recovery.ps1 -ProductRoot .
python platform/tools/test_product_ota_init.py --product-root .
python platform/tools/verify_service_stack.py --build app/build/Release --budget tools/service_stack_budget.json
```

UART/Enrollment 适配注入脚本自动编译各产品实际函数，已纳入 CTest。
送餐车 UART5 注入纳入 `tools/test_meal_capabilities.ps1`。建议公共平台升级、
接入新组件及候选发布验证时执行这些已有入口；无须新增初始化框架。

## 限制与剩余风险

- Host 注入和静态预算不等同于硬件 UART/DMA 时序、模组电源恢复或栈水位
  验证；后续可在明确授权的台架测试中验证，无需本阶段真实 OTA/运动。
- 清扫车 BSS 没有额外余量，巡防仅余 16 B，后续新增状态仍须先核算。
- UART 永久故障时持续局部重试并保持通信不可用；异步操作已终止。注册
  槽不足或 PSK/CA 配置错误会明确报告并关闭对应服务，不伪造初始化成功。
- 候选输出及逐产品 `stage3-validation.json` 保存在 `app/build/`；本阶段未
  刷新正式 `dist/`。所有源代码保持未提交，第一、第二阶段和用户既有修改
  继续保留。
