# 公共 MQTT V4 业务 Number 实现（内部）

> 内部资料，不作为业务对接协议对外交付。

2026-10-15，依据公共 MQTT V4 第 3.2 节和附录 A 接入业务 Number。
对外合同仍以[公共协议](../protocols/A-BOX设备公共MQTT协议-V4.0.md)为准。

## 接口与边界

- `ABoxMqttDigest_WriteNumber()` 输出 IEEE 754 binary64 的 JCS 最短表示；
  `ABoxMqttDigest_Canonical()` 和现有 SHA-256 请求/值摘要复用该编码。
  `1.5`、`1.500`、`15e-1` 产生相同规范化字节；不同 binary64 值不按显示精度合并。
- `ABoxMqttDigest_ParseProfile()` 按回调识别具体 Number 节点；未明确选择的数值
  保持安全 Integer 范围和十进制整数词法。回调接收整棵树和当前节点，可区分嵌套字段、
  数组元素与同名字段。公共 `timestamp`、`expiresAt`、`code`、序号及计数不得选择为 Number。
- Profile 负责协商版本，并在解析后、执行或摘要前验证其 Number 字段范围、分辨率、
  单位和无效值语义；本组件不猜测业务范围，也不静默取整或量化。
- 既有 `ABoxMqttDigest_Parse()` 仍为严格 Integer 入口，等价于不提供 Number 回调。
  `WriteInteger()`、`WriteMilli()`、请求信封及 SHA-256 算法保持原样。
- 解析阶段校验原始数字 Token，拒绝负零（含指数形式）、非有限值、溢出、非零下溢为零、
  非 JSON 数字及非法 Integer 词法。继续拒绝重复解码 Key、非法 UTF-8/代理项、BOM、
  非法空白、尾随数据和超过 16 层的嵌套。cJSON 无法无损表示的嵌入 NUL 继续拒绝。

## 编码依赖与资源

最短有效数字来自 [Ryu](https://github.com/ulfjack/ryu)，固定上游提交
`4c0618b0e44f7ef027ebae05d2cc7812048f7c8f`，源码和双许可证保存在
`components/mqtt/third_party/ryu/`。本地仅调整引用头文件的相对路径。
采用小只读查找表和可移植 64 位整数运算；随后按
[RFC 8785](https://www.rfc-editor.org/rfc/rfc8785.html)及公共协议展开的规则应用
普通十进制阈值 `1e-6 <= abs(value) < 1e21`、小写 `e`、正指数 `+` 与无多余零的格式。

Number 写入不使用浮点 printf、堆分配或可写全局状态；Profile 解析沿用 cJSON 的树分配
和 C 运行库 `strtod`。树本身不保留原始词法，直接传树进行规范化之前必须完成字段校验。
当前清扫车 Profile 1.1 没有 Number 字段，不因此改变已有字段类型或扩大能力声明；
其最终 ELF 未链接 Number/通用树解析路径，后续产品启用时须按实际调用链重新核对资源。

## 验证

- 清扫车完整入口：46 项 Python、产品 C 夹具及 57 项公共 CTest 全部通过。
- MinGW 和主仓库 MSVC Debug 的摘要单测、历史向量及 Number 测试各 3 项通过。
- 10017 个确定性 binary64 输入与 Node ECMAScript 输出逐字节相同，覆盖随机位模式、
  最小次正规数、最大有限值、阈值相邻值及舍入边界。
- 7 项 Number 固定合法向量、6 项非法向量、Integer 词法策略、重复 Key、请求摘要等价与
  冲突测试通过；历史 `valid` / `invalidJson` 各 5 项保持原定义和原固定结果。
- 清扫车 ARM Release 编译和原有全部声明栈、RAM 预算通过；具体制品与现场边界见产品
  内部记录。本次没有部署或修改 Broker/平台服务。
