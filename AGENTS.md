# ImagineGame 工程速查

## 工作约定
- 先核对本地源码、官方资料或案例；区分事实、资产名线索与待验证项。改代码前找到调用链、同类实现和所属模块，在原职责处扩展，避免重复状态与抽象。
- 新增 struct：指定目标 `.h` 就写入该文件；未指定时先调查位置，再请用户确定头文件。
- 仅在关键约束和生命周期边界写短注释；回复只给结论、证据、变更与必要风险。
- 未明确要求不编译；C++ 变更核对源码、引擎接口和构建依赖，完成后做静态链路与差异检查。不调查或修改 Content，除非用户要求。
- 先扫本文标题，只读相关小节；仅补长期有用的已核实事实，结构变化时清理过时内容。

## 方向与模块
- UE 5.8；Gameplay 核心机制用 C++，蓝图用于配置、装配、转发及明确保留的移动计算；暂不接脚本。项目专有类型用 `IMG`，通用类型遵循同目录命名；新增名称不得沿用参考项目名称或前缀。
- `Source/ImagineGame` 是共享运行时，`Source/ImagineEditor` 是编辑器模块；`Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime` 放射击玩法派生规则，依赖 ImagineGame。
- 新规则先核对所有者、权限、复制与生命周期；共享机制扩展现有核心职责，玩法规则放对应 Game Feature。

## 生命周期与 AI
- `AIMGGameMode` 选择 Experience，`UIMGExperienceManagerComponent` 加载；`UIMGAbilitySet` 授予能力、Effect 和 AttributeSet。`AIMGPlayerState` 持有 ASC/属性，`UIMGPawnExtensionComponent` 绑定 Pawn Avatar；`UIMGHeroComponent` 在 DataInitialized 阶段初始化 ASC 和本地输入。
- `UIMGBotCreationComponent` 在 Experience 加载后由服务端生成 Bot；重生分派只支持玩家控制器和 `AIMGPlayerBotController` 派生类。当前无 StateTree 运行时代码；依赖待实现时加到所属模块。
- `UIMGHealthComponent` 发送死亡事件，`UIMGGameplayAbility_Death` 控制开始/结束，`AIMGCharacter` 在结束后销毁；必须授予死亡能力。射击玩法的自动重生能力位于 `ShooterCoreRuntime/GameModes`。

## 角色与移动
- `UIMGHeroComponent::AddMovementIntent` 是世界空间移动意图的统一入口，负责方向平滑、急转事件和提交 `AddMovementInput`；玩家输入、自动奔跑及外部 AI 驱动均复用它。调用方结束或中断持续移动时调用 `ResetMovementIntent`；组件在输入结束、Controller 切换和 EndPlay 时兜底清理。
- Movement Intent 只塑形方向，不另建速度、加速度或制动状态；`UIMGCharacterMovementComponent` 继续负责姿态、速度限制、制动和网络预测。当前未接管原生 AI Path Following，外部 AI 自行调用上述入口。
- CMC 的制动朝向扩展默认关闭；启用后仅在地面无输入制动且速度高于阈值时以实际速度定向，不覆盖寻路请求、Root Motion 或空中旋转。胶囊保持逻辑/权威朝向，表现层平滑留在 ABP。

### Locomotion 的职责与接口
- `UIMGLocomotionComponent` 是本机 ASC/AttributeSet 与 CMC 之间的中间件，不承担复制、RPC 或移动预测；移动同步仍由角色/CMC 链路负责。
- C++ 负责引用、生命周期、属性订阅和 CMC 前置回调；`UIMGCharacterMovementComponent` 在自身 Tick 入口广播 PreMovementUpdate，`UIMGLocomotionComponent` 注册该委托且自身不可 Tick。组件蓝图负责监听 GA 意图 Tag、读取属性、计算并写入 CMC。不要把 Tag 规则搬进 C++，也不要重复维护意图或速度数据。
- `UIMGLocomotionSet` 是可选的七项可复制移动属性来源。ASC 中存在该 Set 时组件才注册属性委托；没有 Set 时组件仍会 Ready 并执行 PreCMC，`GetLocomotionSet` 返回空。属性初始化和 Buff 由用户通过 GE 配置；组件只读取 `CurrentValue`，不创建 Set、不代管 GE 或回写属性。
- Locomotion 是 PawnExtension InitState 的订阅者，不是独立 InitState feature：BeginPlay 只在取得 PawnExtension 后通过其 `RegisterAndCallForInitStateChange` 订阅 GameplayReady，必须立即回调；CMC PreMovement 与 ASC 生命周期均在 GameplayReady 回调内才注册。EndPlay 与 OnUnregister 都幂等清理全部委托和缓存，已 BeginPlay 的组件重新 OnRegister 时会重新建立订阅。GameplayReady 后绑定 ASC，并在每次 ASC 初始化时检查一次可选 Set；PreMovement 只转发蓝图计算，不轮询生命周期。
- 蓝图入口：`OnLocomotionReady`、`OnLocomotionAttributeChanged`、`OnLocomotionUnavailable`、`UpdateLocomotionPreCMC`；通过 `GetIMGCharacter`、`GetLocomotionSet`、`GetIMGAbilitySystemComponent`、`GetIMGCharacterMovementComponent` 取引用，通过 `IsLocomotionReady` 查询绑定是否有效。项目专用对象 Getter 使用 `GetIMG...` 并返回具体 IMG 类型，避免蓝图自行 Cast 或与引擎通用节点同名。
- Ready 表示 CMC 与当前 Pawn 的 ASC 已绑定，LocomotionSet 可为空；Unavailable 回调时 ASC/Set 引用已清空，CMC 仍可用于清理。UE 5.8 ASC 没有公开 SpawnedAttributes 变更委托，因此组件不自动追踪 ASC 初始化后的 Set 动态增删；初始化时缺少 Set 会输出一次 Warning。最终移动参数在 PreCMC 回调计算、写入；音频 Foley/OnLanded 和 ABP 接口转发不属于此组件职责。

### Traversal 的职责与接口
- Traversal 位于 `ImagineGame/Traversal`，当前是 GASP 单蒙太奇实现。角色蓝图按需装配 `UIMGTraversalComponent` 与 `UMotionWarpingComponent`；`AIMGCharacter` 不持有或创建它们，输入也不属于 Traversal，依赖保持 Traversal 单向指向 Character。
- `IMGTraversalTypes.h` 保存检测请求、候选、蓝图选择结果、远端白名单和代理快照；`FIMGTraversalDetector` 是无状态同步几何核心；`UIMGTraversalComponent` 只持配置、动态 Warp 目标和复制快照；`UIMGGameplayAbility_Traversal` 拥有检测到蒙太奇退出的生命周期；`FIMGTraversalTargetData` 只传客户端动画选择和检测方向。
- 检测使用 `IMG_TraceChannel_Traversal`（当前与配置中的 `ECC_GameTraceChannel1/Traversable` 对应），地面角色按前向速度把探测距离从 75 cm 插值到 350 cm，空中保持 75 cm。核心依次处理三高度候选、墙面朝向、左右宽度、顶部可站立、最多 12 次深度采样与边缘细化、顶部胶囊/路径、后边缘、落地点和落点胶囊空间；候选数和采样数均有上限。
- `FIMGTraversalCandidate` 的 RecommendedAction、Front/Back Ledge、Back Floor、高度、深度、落差、BackLedgeHeight、DistanceToLedge、MovementMode 和 Speed 与旧 CHT 语义对齐；RecommendedAction 只是几何默认值，最终 Action、Montage、StartTime、PlayRate 由蓝图 `SelectTraversalAnimation` 决定，Gait 与 PoseHistory 继续由动画蓝图补入。
- GA 为 `LocalPredicted + Exclusive_Replaceable`。本地玩家预测检测并提交紧凑 TargetData；远端服务器不接受客户端几何，按请求方向重检，并校验 Action 可行性、数值范围及 `AllowedMontages` 中的 Action/Montage 对；服务端 AI直接走同一检测和蓝图筛选。模拟代理只消费服务端组件快照与 GAS 蒙太奇复制，不独立重检。
- 执行前必须存在与动作对应的唯一 Warp Window：所有动作需要 `FrontLedge`，Vault/Hurdle 还需要 `BackLedge`，Hurdle 还需要 `BackFloor` 与 `Distance_From_Ledge` 曲线。Hurdle 用窗口末端曲线差重算落点，并重新验证地面和胶囊空间；缺少这些动画合同就拒绝执行。
- 执行阶段由 GA 设置 Flying、忽略命中障碍、启动蒙太奇并在完成、取消、中断、死亡或承载物失效时对称恢复。非 Vault 只有脚下仍有可走地面才恢复 Walking，否则进入 Falling；不在 BlendOut 提前清理。
- 动态障碍和落地承载物分别保存组件局部 Warp Transform；组件 Tick 排在承载物之后、CMC 之前。执行期每 0.05 秒检查组件存活、物理许可、线速度与角速度，超限就中止；当前阈值默认 500 cm/s、85 deg/s。
- Traversal Debug Draw 由 `IMG.Traversal.DebugDraw` 控制：0 关闭，1 显示最终候选、失败和执行中的 Warp Target，2 追加三层搜索、候选命中与逐候选失败原因，3 追加顶部/宽度/深度/路径/落点和胶囊验证；`IMG.Traversal.DebugDuration` 控制检测图形保留秒数。所有调用受 `ENABLE_DRAW_DEBUG` 保护，不进入 Shipping。
- 旧蓝图职责映射为：Traversal 检测图 → `FIMGTraversalDetector`，Traversal Result/Chooser 输入 → `FIMGTraversalCandidate`，CHT/PoseMatch → `SelectTraversalAnimation`，三个 Set Warp Target → `UIMGTraversalComponent`，Ignore/Flying/PlayMontage/恢复 → GA。蓝图只需转换 Candidate、补 Gait/PoseHistory 并返回选择结果。
- GTA 职责映射为：`CClimbDetector/CClimbHandHoldDetected` 的同步几何合同 → Detector/Candidate；`CTaskJumpVault` 的一次动作分流 → 蓝图选择结果；`CTaskVault` 的当前单动作执行、动态 physical 跟随和 CleanUp 子集 → GA；owner 的 Queriable/clone 状态 → Component 代理快照。Component 不等价于 GTA 的完整 Task，也不应吸收输入决策或核心检测。
- 当前未移植 GTA 的异步探测/跨帧缓存、完整多阶段 Task FSM、跨多个碰撞组件拼接同一障碍、IK/武器/布娃娃/水体/失败记忆，以及攀爬悬挂、Shimmy、梯子等持续行为。前三项应按实际性能和复杂障碍需求扩展 Detector/GA；表现与异常系统通过独立协作者或生命周期 Hook 接入；持续行为拆成多个 GA，由上层选择/组合能力协调。
- 接入要求：角色蓝图装配 Traversal 与 MotionWarping 组件、AbilitySet 授予蓝图 GA、蓝图实现 `SelectTraversalAnimation`，并为网络玩家配置全部合法 Action/Montage 对。专用服务器 AI若没有可靠 PoseHistory，筛选图必须提供不依赖 PoseMatch 的后备分支。当前已完成源码/API/差异静态检查，未替代资产装配和实机网络验证。

## 装备、背包与射击
- 装备、背包、快捷栏、远程追踪和武器生成器基类保持原职责；快捷栏与物品实例仅补跨模块导出。装备授予能力后，快捷栏再为装备实例关联物品。
- 通用开火、换弹、自动换弹能力位于 `ImagineGame/Weapons`；具体武器规则、快捷栏输入、丢弃和拾取派生逻辑位于 `ShooterCoreRuntime`。开火复用远程追踪，派生类处理 TargetData、服务端校验与弹药成本。
- `DropWeapon` 当前只移除快捷栏/背包物品，未生成地面拾取物；初始物品发放和死亡清理留给蓝图装配。校验阈值、自动装弹轮询及实机链路尚未按参考资产逐线验证。

## UI
- UI 可订阅快捷栏 SlotsChanged/ActiveIndexChanged 消息和生命/死亡委托；装弹有开始/结束表现事件，自动重生有可配置消息 Tag。
