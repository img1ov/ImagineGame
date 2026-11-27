# ImagineGame 工程约定与导航

## 阅读方式

- 每次任务先读本页约定；凡涉及 C++ 方案设计、代码编写、重构或审查，必读 [C++ 工程规范与避坑红线](coding-standards.md)。再按下表选择相关业务主题，先扫标题，再读会影响当前调用链的小节；跨模块时补读上下游。文档中的工程路径以 Git 根目录为基准。
- 仓库根目录的 [AGENTS.md](../AGENTS.md) 是 Codex 自动发现的入口。按 [OpenAI Docs](https://learn.chatgpt.com/docs/agent-configuration/agents-md)，Codex 沿 Git 根目录至当前工作目录逐层发现约定文件，合计默认上限为 32 KiB。`.agents/` 是主题文档目录，由入口引导按需读取。

## 工作约定

- **调查收尾必做。** 把源码、配置、资产或工具调查中新确认的长期事实逐项写回 `.agents/` 对应主题。有条目就补充或纠正，结构变化时清理过时内容。区分已验证事实、线索与待验证项，保留源码或资料入口。调查过程、编译/测试结果及逐轮审查记录留在当次回复。
- **先查证再改。** 核对本地源码、官方资料或可复核案例。改代码前找到现有调用链、同类实现及所属模块，在原职责处扩展。新增 struct 默认写入用户指定的 `.h`。未指定时先调查合适位置，再请用户确定头文件。
- **业务判定归来源，通用调度和执行归消费者。** 来源用自身事实决定是否请求及运行时参数，通过统一入口接入基础机制。消费者不反查来源业务类型。优先分离配置与实现，按实际职责与生命周期提取组件；避免总管理器、重复状态及无效抽象。
- **项目当前处于实验性开发阶段，不做旧兼容。** 重构或收敛接口时直接替换旧签名、旧字段和旧路径，不保留废弃重载、过渡转发层或旧蓝图/C++ 兼容壳。
- **结构清晰与防线前置。** 遵循 [coding-standards.md](coding-standards.md) 的函数粒度、无平行数组、参数结构体收口、UHT/Live Coding 反射安全、三端网络与重放边界、热路径零无谓分配及 AnimNode 线程安全约束；方案须写清边界、不改项与不变量，执行须严格守边界。
- **依赖保持单向。** `ImagineGame` 提供共享运行时，`ShooterCoreRuntime` 提供射击玩法，`ImagineEditor` 提供编辑器实现。注释只解释关键约束、边界及不直观的生命周期。回复保留关键结论、证据、变更和必要风险。
- **验证边界。** 未明确要求时不编译、不调查或修改 `Content`。不编写测试代码，运行测试由用户完成。C++ 变更仍须静态核对引擎接口、访问权限、构建依赖、调用链与差异。

## 收尾检查

- 全面检查须沿配置消费、激活、执行和表现到结束清理追踪全部相关上下游；增量复核说明范围。
- 核对失败回滚、取消/中断、重复激活、对象失效、默认值、配置生效及临时状态恢复，确保委托、计时器、Tick 依赖和引用成对释放。在原职责内修复发现的问题。
- 网络变更分别追踪权威端、自主代理和模拟代理，检查预测及服务端重检、复制乱序、引用晚到和开始/结束次序。
- 对照 [coding-standards.md](coding-standards.md) 核对 UHT 签名、反射缓存安全、热路径分配、数值守卫与异常退出语义。
- 结论注明实际检查范围和待验证项。区分差异格式检查、静态审查、编译与运行验证。

## 主题导航

| 任务 | 先读 | 主要源码入口 |
| --- | --- | --- |
| C++ 工程审美、代码规范、UE/网络/动画避坑与方案交接 | [C++ 工程规范与避坑红线](coding-standards.md) | 全工程 C++（`ImagineGame`、`ShooterCoreRuntime`、`ImagineEditor`） |
| 模块、命名、引擎/参考路径、编辑器 | [工程与编辑器](project.md) | `Source/ImagineEditor`、项目 Target / Build |
| Experience、ASC、能力输入、Bot、UI/设置 | [Gameplay 框架](gameplay.md) | `GameModes`、`AbilitySystem`、`Player`、`UI`、`Settings` |
| 角色输入、步态、CMC 预测 | [普通移动](locomotion.md) | `Character/IMGHero*`、`IMGCharacterMovementComponent` |
| Ragdoll 跟随、三端联网/身体同步、恢复选片与 GA 规划、PA/PCA 基线 | [Ragdoll](ragdoll.md) | `Character/Ragdoll`、CMC、Mannequin/Rigs |
| PA/PCA 跨工程迁移、资料准备、骨骼映射、批量脚本、抖动/滑动诊断 | [PA/PCA 迁移流程](physics-asset-migration.md) | `.agents/tools/physics_asset_migration/run_job.py` |
| Traversal 检测、选片、放置、执行与手部表现 | [Traversal](traversal.md) | `Character/Traversal`（内部 `Execution`、`Animation`） |
| Traversal 录制、Trace、Rewind、Details | [Traversal 调试](traversal-debugging.md) | `Source/ImagineEditor/*/Character/Traversal` |
| 第三人称镜头、视点、构图、阻尼 | [镜头](camera.md) | `Camera/IMGCameraMode_ThirdPerson`、`IMGCameraComponent` |
| 防穿墙、求解、近裁面 | [镜头碰撞](camera-collision.md) | `Camera/IMGCameraCollision`、`IMGCameraComponent` |
| Shake、跑动/射击镜头反馈 | [镜头反馈](camera-shake.md) | `Camera/IMGCamera*Shake*`、`IMGHeroLocomotionComponent_Camera` |
| Montage 曲线、FootPlacement、ControlRig | [动画](animation.md) | `Animation`、`Source/ImagineEditor/*/AnimationModifiers` |
| 背包、快捷栏、装备授予、动态装配 | [装备](equipment.md) | `Inventory`、`Equipment`、`Character/Cosmetics` |
| Part Tag、挂枪、Retarget 和双手握持 | [装备表现](equipment-presentation.md) | `Equipment/IMGEquipmentInstance`、`Weapons/IMGWeaponInstance` |
| 开火、换弹、命中、伤害、拾取 | [武器](weapons.md) | `Weapons`、`AbilitySystem/Executions`、`ShooterCoreRuntime` |
| GTA 3C 和玩法系统对照 | [GTA 参考](gta.md) | 本地 GTA 快照及各主题引用 |

文档写回跟随事实归属。跨模块结论在主责主题保留实现和限制，关联主题用相对链接。新增、移动或重命名主题时同步更新此表和相关链接。
