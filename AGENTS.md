# ImagineGame 工程速查

## 工作约定
- 先核对本地源码、官方资料或案例；区分事实、资产名线索与待验证项。改代码前找到调用链、同类实现和所属模块，在原职责处扩展，避免重复状态与抽象。
- 新增 struct：指定目标 `.h` 就写入该文件；未指定时先调查位置，再请用户确定头文件。
- 仅在关键约束和生命周期边界写短注释；回复只给结论、证据、变更与必要风险。
- 未明确要求不编译；C++ 变更核对源码、引擎接口和构建依赖，完成后做静态链路与差异检查。不调查或修改 Content，除非用户要求。
- 先扫本文标题，只读相关小节；仅补长期有用的已核实事实，结构变化时清理过时内容。
- 审查结论与调试用法只维护 AGENTS.md，不另建说明 Markdown。

### 收尾检查与结论
- 用户要求“全面检查、完整链路、收尾”时，审查本次全部修改及受影响的上下游；从配置消费、激活条件、执行与表现到结束清理追踪实际调用，不能只抽查入口或重复前次结论。增量复核应明确范围，不称为完整审查。
- 除正常路径外，核对失败回滚、取消/中断、重复激活、对象失效与生命周期清理；涉及网络时分别追踪权威端、自主代理和模拟代理，检查预测与服务端重检、复制乱序、引用晚到及开始/结束先后关系。
- 检查配置是否实际生效、默认值与接口是否一致、临时状态是否成对恢复，以及委托、计时器、Tick 依赖和对象引用是否正确清理。发现问题在原职责处修复，避免借审查扩大无关范围。
- 结论必须与实际证据一致，简述检查范围、发现和未验证项。`git diff --check` 仅证明差异格式；静态审查不能保证运行或网络表现。遵守不编译、不测试的要求，同时完成应有的源码审查，不以此代替或省略检查。

## 方向与模块
- UE 5.8；Gameplay 核心机制用 C++，蓝图用于配置、装配、转发及明确保留的移动计算；暂不接脚本。项目专有类型用 `IMG`，通用类型遵循同目录命名；新增名称不得沿用参考项目名称或前缀。
- `Source/ImagineGame` 是共享运行时，`Source/ImagineEditor` 是编辑器模块；`Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime` 放射击玩法派生规则，依赖 ImagineGame。
- 新规则先核对所有者、权限、复制与生命周期；共享机制扩展现有核心职责，玩法规则放对应 Game Feature。

### Lyra 核心源码移植状态
- 参考工程：`D:/Application/UnrealProjects/LyraStarterGame`。2026-10-28 对照 Source、项目插件 Source 与构建源码，补齐已确认的核心功能差异；保留本工程移动、Bot、Traversal 等扩展。文件对应不代表行为完全一致；本次未编译、未运行测试、未调查 Content。
- GameSettings 的 `UGameSettingDetailView` 已补 `HandleCurrentSettingChanged`，消费当前 Setting 的变更事件以刷新描述及动态详情；切换/清空 Setting、NativeDestruct 时退订，NativeConstruct 保留当前选中项并重建详情/扩展。切换或清空、关闭、释放 Slate 均取消并清空扩展异步加载句柄；完成回调与扩展装配复核当前 Setting，避免旧请求或蓝图回调重入回填过期控件。DLC Registry 的定时 OnSettingChangedEvent 可沿此路径更新详情，不另建轮询。
- `UIMGDeveloperSettings.ExperienceOverride` 已接回 GameMode 的 WITH_EDITOR/PIE 分支，优先级为 URL Options → PIE 开发设置 → 命令行 → World Settings → 专服/默认 Experience。PawnData 在 Experience 就绪后按 PlayerState 已认领 → Bot 同步选择 → Experience.DefaultPawnData → AssetManager.DefaultPawnData 查询；全局回退仍走既有同步加载/保活接口，缺失时返回空并记录日志，不新增授予或复制入口。
- `AIMGCharacter` 已恢复 `IGameplayCueInterface`，支持角色蓝图直接处理 Cue 事件；独立 Notify 继续由 UE 5.8 GameplayCueManager 的全局 CueSet 分发。玩家/Bot Controller.OnUnPossess 在 ASC 仍以旧 Pawn 为 Avatar 且与 PawnExt 绑定一致时，先由 PawnExt::UninitializeAbilitySystem 取消能力、清输入/Cue 并通知生命周期监听者，再清 Avatar；缺少匹配 PawnExt 时保留原生清 Avatar 路径。旧 Pawn 不得解绑已切到新 Pawn 的 ASC。不新增 RPC、复制状态或 Tick；具体 Cue 资产与联机表现需运行确认。
- ExperienceManager 的资源包筛选已纠正为非客户端加载 Server Bundle，PIE 同时加载两端；Standalone/Listen Server 两端、Dedicated Server 仅服务端、Client 仅客户端。同一 Experience/ActionSets 中的 GameFeature URL 去重，再按唯一 URL 计数激活/退出，保留跨 PIE World 的既有请求计数。
- ImagineGame、ImagineEditor、ShooterCoreRuntime 的 Build.cs 对照后未发现缺失的参考模块依赖；本次所用 GameplayAbilities、DeveloperSettings、Engine 与 GameSettings 的 UMG/CommonUI 已有覆盖，无需新增依赖。
- 本次范围为核心功能代码；TopDownArena、LyraExtTool、自动化示例与发布脚本未迁入。当前仅有 Game/Editor Target，独立 Client/Server、渠道 Target 及共享构建策略留给对应打包需求；已有编辑器 Validator/ContentValidationCommandlet。
- 收尾已静态复核本轮全部代码修改及其配置、初始化/复制、重生/ASC 解绑、UI 事件与异步请求上下游，补修资源包筛选、插件去重、Controller 解绑顺序和 UI 过期请求；Build.cs 无新增依赖。既有 Experience 加载状态机仍未完整处理插件失败、加载中 EndPlay 与异步 Action 退出；本轮未重构该状态机，不将正常加载路径的复核等同于这些失败边界已受支持。未编译、未运行测试、未核实 Content/Cook 或实机网络表现。

## 生命周期与 AI
- `AIMGGameMode` 选择 Experience，`UIMGExperienceManagerComponent` 加载；`UIMGAbilitySet` 授予能力、Effect 和 AttributeSet。`AIMGPlayerState` 持有 ASC/属性，`UIMGPawnExtensionComponent` 绑定 Pawn Avatar；`UIMGHeroComponent` 在 DataInitialized 阶段初始化 ASC 和本地输入。
- `UIMGBotCreationComponent` 在 Experience 加载后由服务端生成 Bot；重生分派只支持玩家控制器和 `AIMGPlayerBotController` 派生类。当前无 StateTree 运行时代码；依赖待实现时加到所属模块。
- `UIMGHealthComponent` 发送死亡事件，`UIMGGameplayAbility_Death` 控制开始/结束，`AIMGCharacter` 在结束后销毁；必须授予死亡能力。射击玩法的自动重生能力位于 `ShooterCoreRuntime/GameModes`。

### 持久 Bot：PawnData 与初始化
- BotCreation 配置 BotControllerClass，保留 Controller → PlayerState → Pawn 的生成顺序及通用初始化、分队和出生点规则，不新增 PawnData 配置。`AIMGPlayerBotController` 开启 bWantsPlayerState，每个 Bot 有独立 PlayerState/ASC；共享 PawnData 资产不共享能力运行状态或属性值。
- GameMode::GetPawnDataForController 优先返回 PlayerState 已认领的数据；尚未认领且 Experience 就绪时，查询持久 Bot 的 `AIMGPlayerBotController::GetPawnData`。该入口为 BlueprintNativeEvent/BlueprintPure，支持 C++/蓝图覆写；默认返回空，GameMode 输出 LogIMGExperience Warning 并依次回退 Experience.DefaultPawnData、AssetManager.DefaultPawnData。普通玩家使用相同默认来源，未就绪时不调用 Bot 查询或加载全局默认。
- GetPawnData 只同步返回配置，不负责授予能力或生成 Pawn；查询应无副作用，首次认领前保持选择稳定。首次查询可能早于 Controller.BeginPlay、PlayerState 成员赋值、分队和 Possess，选择依据必须提前可用。空值表示回退默认配置，不表示等待异步注入。BotController 已导出，可供其他运行时模块派生。
- PlayerState 在 Experience 回调中认领数据，仅服务端首次 SetPawnData 授予 AbilitySets。已加载时注册会立即回调，普通 SpawnActor 返回后再覆盖已太晚；同一 PlayerState 的后续生成、出生点体型检查及重生复用已认领配置与能力。切换不同 PawnData 不属于此查询入口，不能直接覆盖只写一次的数据。
- GameMode 按 PawnData.PawnClass 生成 Pawn，在 FinishSpawning 前将同一数据写入 PawnExt；所选 Pawn 仍须装配 PawnExt/Hero 及所需组件。依赖满足后沿 Spawned → DataAvailable → DataInitialized → GameplayReady 绑定 PlayerState ASC，Bot 仅跳过玩家输入要求及绑定，不跳过状态条件。
- 网络继续使用现有 PawnExt.PawnData、Pawn.PlayerState 与 GAS 复制，不新增 RPC、Tick 或重复状态。客户端经 PawnExt.OnRep_PawnData、Character.OnRep_PlayerState 等推进本机初始化，模拟代理不要求 AIController；晚注册监听者使用 RegisterAndCall。IMGAbilitiesReady 仅在服务端 SetPawnData 发送，OnAbilitySystemInitialized 仅表示本机 ASC/Avatar 已绑定，不保证能力、属性全部复制到齐。
- 独立敌人 AI 可采用 Pawn 自持 ASC 或其他技能初始化体系，不强制接入持久 Bot 的 PawnData 选择。AIMGCharacterWithAbilities 自持 ASC，但仍继承 PawnExt；自身 InitAbilityActorInfo 不会自动推进 PawnExt 至 GameplayReady。
- 本次完成同步查询与回退入口及相关生命周期、复制和重生源码审查；未编译、未检查 Content 或验证实机网络表现，客户端资产可用及具体蓝图装配仍需运行确认。

## 角色与移动
- `UIMGHeroComponent::AddMovementIntent` 是世界空间移动意图的统一入口，负责方向平滑、急转事件和提交 `AddMovementInput`；玩家输入、自动奔跑及外部 AI 驱动均复用它。调用方结束或中断持续移动时调用 `ResetMovementIntent`；组件在输入结束、Controller 切换和 EndPlay 时兜底清理。
- Movement Intent 只塑形方向，不另建速度、加速度或制动状态；`UIMGCharacterMovementComponent` 继续负责姿态、速度限制、制动和网络预测。当前未接管原生 AI Path Following，外部 AI 自行调用上述入口。
- CMC 的制动朝向扩展默认关闭；启用后仅在地面无输入制动且速度高于阈值时以实际速度定向，不覆盖寻路请求、Root Motion 或空中旋转。胶囊保持逻辑/权威朝向，表现层平滑留在 ABP。

### CMC 网络默认值与边界
- IMGCMC 沿用 UE 5.8 网络默认值：Exponential；模拟代理位置/旋转平滑 0.10/0.05 秒，Listen Server 远端角色 0.04/0.033 秒，普通/大误差纠偏最小间隔 0.10/0.05 秒。自主代理位置纠偏应用服务端状态并重放 SavedMoves，不由上述代理插值时间、角色 NetUpdateFrequency 或 NetworkPrediction 插件缓冲参数控制。
- GameNetworkManager 位置误差容差沿用引擎 MAXPOSITIONERRORSQUARED=3.0（约 1.73 cm）。Play As Client 未开启 Network Emulation 仍走网络移动；先用 p.NetShowCorrections 1 确认纠偏，再核对两端移动参数和预测，不以放宽容差替代修复。PreCMC 按输入选择制动参数时，本地待消费输入与服务端远端玩家的 RPC 加速度不能混用；ReplicateUsingRegisteredSubObjectList 只选择子对象复制路径，不会自动同步 MaxWalkSpeed 等 CMC 参数。

### Locomotion 的职责与接口
- UIMGMovementPrerequisiteComponent 是独立于 PawnExt/GAS 的抽象调度基类，缓存通用 Character/CMC，统一声明 BlueprintNativeEvent UpdateLocomotionPreCMC。只支持 Character/CMC；独立派生默认 CanUpdateLocomotion=true，调度就绪即可执行，额外数据条件由原生覆写提供。不主动注销或销毁组件，不查询 ASC，不另建复制、RPC 或移动预测。
- 默认 TG_PrePhysics，显式建立 Controller → PreCMC → CMC 依赖；Controller 弱引用及 ReceiveControllerChangedDelegate 处理引用晚到、切换与解绑。BeginPlay 在蓝图回调前准备引用，回调后复核；重新 OnRegister 恢复调度，EndPlay/OnUnregister 幂等关闭 Tick、退订 Controller、移除依赖并清缓存。运行中替换 CMC 会停止业务更新，需重新注册恢复，不做 Tick 发现或重绑。
- UIMGLocomotionComponent 是上述基类的 IMG 适配层，连接 PawnExt/ASC/可选 LocomotionSet，CanUpdateLocomotion 返回 IsLocomotionReady。保留 GameplayReady 且当前 Pawn 的 ASC/Avatar 绑定有效后才执行 PreCMC 的门槛；ASC 解绑停止业务，重绑恢复。基类调度可先建立，子类不重复缓存 CMC、管理 Tick 依赖或声明更新事件。
- Locomotion 仅订阅 PawnExtension InitState，不注册独立 feature。调度就绪后 RegisterAndCallForInitStateChange 订阅 GameplayReady，必须立即回调；到达该状态才订阅 ASC 生命周期，初始化监听使用 RegisterAndCall。EndPlay/OnUnregister 先退订并清空 PawnExt 订阅状态，再解除属性委托、清空 ASC/Set 并触发 Unavailable，最后由基类释放 CMC 与调度，避免回调重入旧订阅。重新注册重建订阅；Tick 不轮询 ASC。
- UIMGLocomotionSet 提供七项可复制移动属性，可缺省；缺少时仍 Ready，GetLocomotionSet 返回空并输出 Warning。组件只读 CurrentValue、订阅属性变化，不创建 Set、授予 GE 或回写属性。Set 仅在 ASC 初始化时检查；UE 5.8 无公开 SpawnedAttributes 变更委托，不自动追踪初始化后动态增删。
- 业务事件为 OnLocomotionReady、OnLocomotionAttributeChanged、OnLocomotionUnavailable；统一更新事件继承自基类，C++ 覆写 UpdateLocomotionPreCMC_Implementation。GetIMGCharacter、GetIMGCharacterMovementComponent、GetIMGAbilitySystemComponent 返回具体 IMG 类型；IsMovementPrerequisiteReady 查询调度，IsLocomotionReady 查询 IMG 业务门槛，不能混用。Unavailable 时 ASC/Set 已清空，CMC 在对象有效时仍可用于清理。
- 蓝图保留监听 GA 意图 Tag、读取属性及在 PreCMC 计算并写入最终 CMC 参数的职责；不把 Tag 规则搬入 C++，不重复维护意图或速度。Foley/OnLanded 与 ABP 转发不属于该组件，CMC 不增加 Locomotion 专用委托或 Tick 逻辑。

### Traversal：目录与职责

- 源码：`Source/ImagineGame/{Public,Private}/Character/Traversal`，仍属 ImagineGame 模块。Detector 无状态同步检测几何；Component 管配置、Warp Target、临时碰撞忽略与代理快照；GA 管检测→选择→校验/Commit→播放→退出；Types/TargetData 分别定义数据与客户端请求，Debug 留在 Private 同目录。
- 每次 GA 激活只作一次尝试，失败即结束；外部逻辑决定输入、自动触发与重试频率，`RequestSource` 与 ActivationPolicy 独立。角色装配 Traversal/MotionWarping 并授予 GA；CHT/PoseMatch 经 PreSelectTraversalAnimation/SelectTraversalAnimation 选择单个蒙太奇，无 PoseHistory 的服务端需后备选择。
- `InitiationSettings` 在检测后、动画选择前及 GA 校验/服务端重检时消费；关闭 bEnabled 仅跳过本组发起限制，不跳过几何、动画合同或改变触发频率。Manual 默认允许150 cm内站立发起；Automatic 默认允许 Falling，但要求朝向前沿的有效输入意图，方向阈值0.8，固定使用 AutomaticMaxDistance（默认150 cm），不以角色实际速度设门槛或二次缩短距离。
- 意图优先待消费输入，再回退 CMC Acceleration；AI 复用 Manual 发起规则，在两者为空时可读有效且 Moving 的 PathFollowing 当前段目标，不用实际 Velocity 充当意图。普通玩家按 GA.RequestSource，服务端控制 AI 强制使用 AI 来源；AI 检测方向与发起判断共用该意图，无意图时仍可按手动站立规则向角色前方尝试。
- 已删除 AutomaticMinSpeed、ServerSpeedTolerance、AutomaticMinDistance、SpeedForMaxAutomaticDistance，不保留无消费者的兼容字段；蓝图若显式引用旧字段需刷新节点。ServerDistanceTolerance 只补偿远端发起距离误差。角色速度缩放保留于 Detector 搜索长度；Candidate.Speed/RunningSpeedThreshold 仅提供动作选择信息与建议，不限制原生 Supports。MaxMovingSpeed/MaxAngularSpeedDegrees 检查障碍/载体运动，检测、落点与执行复核继续消费。
- Detector 的搜索/几何/坡面/净空及必要动作支持与最终动画覆盖分开；Automatic 的深度、高位开放接近及楼梯过滤仅用于 Automatic，不是所有动画的通用门槛。ActionSettings 提供动作默认执行规则，MontageSettings 按 Action/Montage 整份覆盖；WarpBindings/RequiredTargets 管锚点和必要接触，Notify 管窗口，Landing 管曲线/检测落点。最终复核表面、落差、可走支撑与完整胶囊，由 Root Motion/MotionWarping 执行位移。
- GTA 人形搜索仅绕Z定向，多层球扫保持水平，坡面为后置筛选；SearchForHandHold 的探针长度来自固定层值/覆写值，GetClimbDetectorParam 的 MBR 插值影响发起范围，JumpVault 另有步态门槛，AutoVault 结合摇杆、受阻降步态与冷却且纯自动路径要求落地。本工程的速度搜索缩放、固定发起范围及默认允许空中均是项目策略，不声称与 GTA 相同。

### Traversal：执行约束与边界

- 保留 GAS LocalPredicted；校验→Commit→建立Target/碰撞忽略→Flying→蒙太奇启动→提交TargetData/发布快照。启动前单次取消，服务端按方向重新检测并校验来源、Action、时间/倍率、Warp合同及可选AllowedMontages，不接受客户端几何。CHT前拒绝不启动执行，Commit后的回调重入与动画启动失败仍走既有退出路径。
- 正常完成、中断、取消、死亡、目标失效与ASC解绑沿GA/Component既有出口幂等清理Target、碰撞忽略、委托及计时器；正常BlendOut不提前结束，退出在恢复碰撞后按脚下支撑切Walking/Falling，不覆盖后续非Flying移动模式。PawnExt解绑先取消能力、清输入/Cue，再清Avatar并广播生命周期。
- 模拟代理消费COND_SkipOwner快照、GAS蒙太奇与CMC，按Montage/PlayInstanceId等待引用、Target、碰撞忽略和Flying；结束快照先到时保留本机实例至退出（含BlendOut），同实例引用补齐不反复拆建。既有Target恢复历史来自权威快照；权威非Flying到达后须先恢复碰撞，让CMC查到支撑。
- 保持来源组件→Traversal→CMC顺序，组件仅活动时Tick，MotionWarping.OnPreUpdate在实际Warp采样前刷新Target；默认0.05秒复核载体、500 cm/s和85 deg/s运动上限、候选时效0.25秒。局部Target跟随不等于完整支持动态平台；BasedMovement/动态Bone、输入历史/退出延迟、异步检测、悬挂/Shimmy/梯子另行设计，不直接Attach绕过CMC预测。
- 本次速度策略收尾覆盖配置消费、Manual/Automatic/AI、本地预测、服务端重检、选择/Commit前后复核及正常/失败/取消/死亡/解绑清理；行为修改位于InitiationSettings/CanInitiate，核心搜索、分组、手点、深度/净空及落点算法未改，不新增状态、RPC、Tick或构建依赖。未编译、未测试、未调查Content；外部CHT/蓝图可自行按Speed筛选，字段删除的资产兼容及实机网络表现仍需运行确认。

### Traversal：调试与参考定位

- 绘图默认关闭，受ENABLE_DRAW_DEBUG保护。DebugDraw为0关/1输出与活动Target/2搜索失败/3全部探针；DebugStages掩码为搜索1/命中2/手点4/体积8/输出16/执行32。完整观察依次用`IMG.Traversal.DebugDraw 3`、`IMG.Traversal.DebugStages 63`、`IMG.Traversal.DebugProcessDuration 3`、`IMG.Traversal.DebugDuration 3`；`IMG.Traversal.DebugLog 2`记录启用阶段，需同时开启绘图。按角色/网络角色区分，几何标记pre-selection，执行输出在动画落点解析后更新。
- 预测/执行日志独立于绘图：`Log LogIMGTraversal Verbose`、`Log LogIMGAbilitySystem Verbose`，两端按Avatar/Spec/PredKey对照，查完恢复Log。PredictionRejected是激活拒绝；TargetData重检失败发生在激活后；AuthorityEndedPredictedTraversal仅说明权威先结束，不能单凭该日志认定预测错误。静态审查及调试图不替代资产与联机验证。
- GTA参考根目录：`D:/Application/GTA/GTAVSP/GTAV Source/src/dev_ng/game`。检测/手点在`task/Movement/Climbing/{ClimbDetector,ClimbHandHold}.*`，发起在`task/Default/TaskPlayer.cpp`，Jump/Vault分流在`task/Movement/Jumping/TaskJump.*`，执行/清理在`task/Movement/Climbing/TaskVault.*`。保留本工程GAS/CMC网络体系，不机械移植其动画表、手动位移或网络协议。

## 装备、背包与射击
- 装备、背包、快捷栏、远程追踪和武器生成器基类保持原职责；快捷栏与物品实例仅补跨模块导出。装备授予能力后，快捷栏再为装备实例关联物品。
- 通用开火、换弹、自动换弹能力位于 `ImagineGame/Weapons`；具体武器规则、快捷栏输入、丢弃和拾取派生逻辑位于 `ShooterCoreRuntime`。开火复用远程追踪，派生类处理 TargetData、服务端校验与弹药成本。
- `DropWeapon` 当前只移除快捷栏/背包物品，未生成地面拾取物；初始物品发放和死亡清理留给蓝图装配。校验阈值、自动装弹轮询及实机链路尚未按参考资产逐线验证。

## UI
- UI 可订阅快捷栏 SlotsChanged/ActiveIndexChanged 消息和生命/死亡委托；装弹有开始/结束表现事件，自动重生有可配置消息 Tag。
