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
- C++ 负责引用、生命周期、属性订阅和 CMC 前置 Tick 排序；Locomotion 连接建立后把自身注册为 CMC 的 Tick prerequisite，并在自身 Tick 中调用 `UpdateLocomotionPreCMC`，未连接及退出时关闭 Tick。CMC 不提供 Locomotion 专用委托或额外 Tick 逻辑。组件蓝图负责监听 GA 意图 Tag、读取属性、计算并写入 CMC。不要把 Tag 规则搬进 C++，也不要重复维护意图或速度数据。
- `UIMGLocomotionSet` 是可选的七项可复制移动属性来源。ASC 中存在该 Set 时组件才注册属性委托；没有 Set 时组件仍会 Ready 并执行 PreCMC，`GetLocomotionSet` 返回空。属性初始化和 Buff 由用户通过 GE 配置；组件只读取 `CurrentValue`，不创建 Set、不代管 GE 或回写属性。
- Locomotion 是 PawnExtension InitState 的订阅者，不是独立 InitState feature：BeginPlay 只在取得 PawnExtension 后通过其 `RegisterAndCallForInitStateChange` 订阅 GameplayReady，必须立即回调；CMC Tick prerequisite 与 ASC 生命周期均在 GameplayReady 回调内才注册。EndPlay 与 OnUnregister 都幂等关闭 Tick、移除 prerequisite 并清理全部委托和缓存，已 BeginPlay 的组件重新 OnRegister 时会重新建立订阅。GameplayReady 后绑定 ASC，并在每次 ASC 初始化时检查一次可选 Set；Locomotion Tick 只转发蓝图计算，不轮询生命周期。
- 蓝图入口：`OnLocomotionReady`、`OnLocomotionAttributeChanged`、`OnLocomotionUnavailable`、`UpdateLocomotionPreCMC`；通过 `GetIMGCharacter`、`GetLocomotionSet`、`GetIMGAbilitySystemComponent`、`GetIMGCharacterMovementComponent` 取引用，通过 `IsLocomotionReady` 查询绑定是否有效。项目专用对象 Getter 使用 `GetIMG...` 并返回具体 IMG 类型，避免蓝图自行 Cast 或与引擎通用节点同名。
- Ready 表示 CMC 与当前 Pawn 的 ASC 已绑定，LocomotionSet 可为空；Unavailable 回调时 ASC/Set 引用已清空，CMC 仍可用于清理。UE 5.8 ASC 没有公开 SpawnedAttributes 变更委托，因此组件不自动追踪 ASC 初始化后的 Set 动态增删；初始化时缺少 Set 会输出一次 Warning。最终移动参数在 PreCMC 回调计算、写入；音频 Foley/OnLanded 和 ABP 接口转发不属于此组件职责。

### Traversal：架构与接入
- Traversal 位于 `ImagineGame/Traversal`，当前为 GASP 单蒙太奇实现。角色蓝图按需装配 `UIMGTraversalComponent` 与 `UMotionWarpingComponent`，AbilitySet 授予蓝图 GA；`AIMGCharacter` 不创建这些组件，输入决策也不属于 Traversal，依赖保持 Traversal 单向指向 Character。
- 职责固定为：`IMGTraversalTypes.h` 放请求、候选、选择结果、远端白名单和代理快照；`FIMGTraversalDetector` 是无状态同步几何真值源；`UIMGTraversalComponent` 持配置、动态 Warp Target 和代理快照；`UIMGGameplayAbility_Traversal` 管检测至蒙太奇退出；`FIMGTraversalTargetData` 只传客户端动画选择和检测方向。
- 旧蓝图映射：检测图 → Detector，Traversal Result/Chooser 输入 → Candidate，CHT/PoseMatch → `SelectTraversalAnimation`，Set Warp Target → Component，Ignore/Flying/PlayMontage/恢复 → GA。蓝图只转换 Candidate、补 Gait/PoseHistory 并返回选择结果；专用服务器筛选必须有不依赖 PoseHistory 的后备分支。

### Traversal：检测合同
- 使用 `IMG_TraceChannel_Traversal`（当前配置为 `ECC_GameTraceChannel1/Traversable`）。默认广域搜索和 `MaxAnimationReachDistance` 均为 250 cm；80 cm 以下 StepUp 100 cm、普通攀越 150 cm、低障碍奔跑 180–200 cm 仅形成 `ReferenceReachDistance`，用于筛选和调试，不截断 GASP 的 ReachLedge 前置段。
- 前向搜索最多 6 层，球半径 21.5 cm、层距 43 cm，聚合同墙命中后按距离平方 5:朝向 1 评分。顶部为半径 10 cm 的中/左右三点探测；角色到手点使用半角色半径小球扫，不用完整角色胶囊。
- 深度检测为 5 组半径 27.5 cm、间距 55 cm、向下 500 cm 的球扫，20 cm 高差终止并区分后边缘和升高障碍；水平净空为两层半径 22.5 cm、首层后退 25 cm、前探 300 cm，35 cm 内受阻拒绝；垂直净空使用单组半径 17.5 cm。后边缘还需精细化，落点保留完整角色胶囊复核。
- Candidate 保留 RecommendedAction、Front/Back Ledge、Back Floor、高度、深度、落差、BackLedgeHeight、DistanceToLedge、MovementMode、Speed、净空和参考距离；DistanceToLedge 是 FrontLedge 到 Mesh 世界位置的三维距离。前后边缘承载组件及全部障碍组成组件也必须保留，供动态跟随、碰撞忽略和执行期检查使用。
- RecommendedAction 只给出站立/低翻/默认/浅落/宽翻/滑越的几何默认值，最终 Action 由动画筛选决定。同步实现已覆盖 GTA 人形 Traversal 的主要探针与 Vault 出口条件，但不等于完整 `CClimbDetector` 语义。

### Traversal：动画与执行合同
- 检测成功后 GA 先调用 `PreSelectTraversalAnimation` 准备或转发输入，再由 `SelectTraversalAnimation` 返回 Action、Montage、StartTime、PlayRate。不要把 CHT 距离配置重复硬编码进 C++。
- 动画执行前校验唯一 Warp Window，且 StartTime 必须早于所需窗口末端：所有动作需要 `FrontLedge`；Vault 允许仅有 FrontLedge，但存在可选 `BackLedge` 时仍校验其唯一性和时间；Hurdle 必须有 `BackLedge`、`BackFloor` 和 `Distance_From_Ledge` 曲线。
- FrontLedge Warp 使用 0.5 cm 上移和 `-FrontLedgeNormal` 朝向。Hurdle 用窗口末端曲线差重算落点，并复核承载物、地面、落点胶囊及后边缘到落点的整段胶囊路径。位移只来自蒙太奇 Root Motion、PoseSearch StartTime 和 UE MotionWarping，不移植 GTA Task 的位置/速率补偿。
- GA 在执行期设置 Flying、忽略全部障碍组成组件并播放蒙太奇；完成、取消、中断、死亡或承载物失效时对称恢复。不要在 BlendOut 提前清理；退出时依据脚下可走地面恢复 Walking/Falling。
- 前/后边缘和落地组件分别保存局部 Warp Transform；Component Tick 排在承载物之后、CMC 之前。每 0.05 秒检查组件存活、物理许可及速度，默认上限为 500 cm/s 和 85 deg/s；Candidate 默认时效 0.25 秒。
- 当前仅让 Warp Target 跟随承载组件；Flying 会清除 Movement Base，尚未完整继承平台在非 Warp Window、旋转和动画积分阶段的运动，不能把现状视为动态平台附着完成。

### Traversal：网络合同
- GA 为 `LocalPredicted + Exclusive_Replaceable`。自主代理立即预测检测，仅在本地 Commit 与蒙太奇启动成功后提交紧凑 TargetData；启动前失败通过单次 TargetData Cancel 释放服务端等待实例，服务端只在 Traversal 尚未开始时接受该取消。
- 服务端不信任客户端几何：远端请求按方向重检，再验证 Action、数值范围、动画合同和可选 `AllowedMontages`。白名单为空时依赖原生验证，非空时才要求 Action/Montage 精确匹配。服务端 AI 走同一检测和筛选链。
- 模拟代理不执行 GA、检测或筛选，只消费 `COND_SkipOwner` 的 Traversal 快照、角色移动复制和 GAS 蒙太奇。GAS 使用精确 Position 复制；PlayerState 上的蒙太奇与 Character 上的快照到达顺序不确定，因此含 `FrontLedge` 合同的蒙太奇必须同时匹配快照中的 Montage 与 GAS `PlayInstanceId`，并等待全部实际 Warp Target 建立后再处理。模拟实例不早于快照 StartTime，并在 UE 原生 0.1 秒纠偏阈值内保留最多 0.075 秒表现余量，使资产 BlendIn 自然推进；超出余量、换段和停止仍走 GAS 原生强校正，自主代理不等待快照。

### Traversal：调试
- `IMG.Traversal.DebugDraw`：0 关闭；1 显示成功最终结果和执行中 Warp Target；2 增加自适应搜索层、候选评分和逐候选失败原因；3 增加顶部/宽度/深度/边缘细化/路径/落点/胶囊、Warp 窗口与曲线、动态组件速度和中止指标。
- 过程和执行中 Draw 只保留一帧；最外层分层搜索探针不进快照。成功候选从搜索摘要与评分开始按原 RequiredLevel 记录，在动画合同、Warp、Commit 和蒙太奇全部通过后统一重放；失败候选与失败流程回滚记录。
- `IMG.Traversal.DebugDuration` 控制成功快照时长，默认 2 秒。显示标记和线宽可以小于真实查询形状但不得改变检测；所有调用受 `ENABLE_DRAW_DEBUG` 保护，不进入 Shipping。调试文本使用项目术语，不暴露参考项目名称。

### Traversal：GTA 参考与后续边界
- 参考根目录：`D:/Application/GTA/GTAVSP/GTAV Source/src/dev_ng/game`。几何、异步结果和手点查 `task/Movement/Climbing/ClimbDetector.*`、`ClimbHandHold.*`；发起与缓存消费查 `task/Default/TaskPlayer.cpp::{DoJumpCheck,CanJumpOrVault,GetClimbDetectorParam}`；Jump/Vault 分流查 `task/Movement/Jumping/TaskJump.*::CTaskJumpVault`；执行、动态载体、清理和 clone 状态查 `task/Movement/Climbing/TaskVault.*`，再沿 `task/System/TaskFsmClone.*`、`Peds/QueriableInterface.cpp`、`network/Objects/Entities/NetObjPed.cpp` 追网络链。不要只复制探针常量。
- 映射边界：`CClimbDetector/CClimbHandHoldDetected` → Detector/Candidate；`CTaskJumpVault` 动作分流 → 蓝图选择结果；`CTaskVault` 单次动作与 CleanUp 子集 → GA；Queriable/clone 状态 → Component 代理快照。Component 不是完整 GTA Task，不能吸收输入决策或核心检测。
- V2 性能项优先采用“异步广域搜索、同步精查”：仅本地持续 AutoVault 预取前向候选，GA 消费缓存后仍同步完成手点、顶部、深度、净空和最终动作判定；手动触发、AI 和服务端可信重检保持同步。异步缓存至少携带请求序号、提交帧/时间、角色位姿、MovementMode、方向和候选组件局部 Transform；检测不连续、独立短时效超时、位姿/模式变化、组件失效或移动超限即丢弃，动态命中需同步重提。不要复用网络 Candidate 的 0.25 秒时效。
- AutoVault、失败面抑制、楼梯退出延迟和动态物体稳定等待属于输入/AI 意图层或独立决策协作者，不进入 Detector。Surface Policy 也应可插拔，以 Traversal 通道、组件 Tag/接口或数据资产表达材质、门、小物体、Pawn/布娃娃、坡面、拱顶、Volume 和动态类型许可，禁止硬编码 GTA 实体类型。
- 移动承载物后续优先验证 CMC BasedMovement，再比较 Traversal 相对 carrier；禁止直接 Attach 绕过移动预测。需覆盖 Root Motion、MotionWarping、网络平滑、平台旋转、动态 Bone、carrier 切换，以及取消/死亡/失效/纠正时幂等 detach。载体异常至少映射为 detach + 取消或跌落。
- 单蒙太奇阶段优先补动画资产校验、已有碰撞忽略状态的保存恢复、网络/缓存诊断，以及静态组合网格、斜面/台阶/拱顶、窄边、移动平台、延迟/丢包、Dedicated Server 和 AI 测试。不要预先复制完整 Task FSM；手腿 IK、武器、布娃娃、水体、失败记忆、悬挂、Shimmy、梯子等按明确需求拆到独立协作者或多个 GA。

## 装备、背包与射击
- 装备、背包、快捷栏、远程追踪和武器生成器基类保持原职责；快捷栏与物品实例仅补跨模块导出。装备授予能力后，快捷栏再为装备实例关联物品。
- 通用开火、换弹、自动换弹能力位于 `ImagineGame/Weapons`；具体武器规则、快捷栏输入、丢弃和拾取派生逻辑位于 `ShooterCoreRuntime`。开火复用远程追踪，派生类处理 TargetData、服务端校验与弹药成本。
- `DropWeapon` 当前只移除快捷栏/背包物品，未生成地面拾取物；初始物品发放和死亡清理留给蓝图装配。校验阈值、自动装弹轮询及实机链路尚未按参考资产逐线验证。

## UI
- UI 可订阅快捷栏 SlotsChanged/ActiveIndexChanged 消息和生命/死亡委托；装弹有开始/结束表现事件，自动重生有可配置消息 Tag。
