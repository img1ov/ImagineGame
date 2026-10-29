# ImagineGame 工程约定与导航

## 工作约定

- 事实优先：先核对本地源码、官方资料或可复核案例，区分已验证事实、资产名线索和待验证项。改代码前找到调用链、同类实现与所属模块，在原职责处扩展，避免重复状态、分支和抽象。
- 新增 struct：用户指定目标 `.h` 时写入该文件；未指定时先调查合适位置，再请用户确定头文件。
- 保持单向有效依赖和模块独立性。仅在关键约束、不直观的生命周期与边界处写短注释；回复只给关键结论、证据、变更和必要风险。
- 未明确要求不编译；C++ 变更仍须核对引擎接口、访问权限、构建依赖、调用链和差异。不调查或修改 Content，除非用户要求。
- 先扫本文标题，只读相关小节。AGENTS 只维护长期有效的工程导航、职责边界、接口合同、已确认限制、待办及必要调试用法；结构变化时清理过时内容。
- 修复过程、编译/测试结果和逐轮审查记录放当次回复，不追加到 AGENTS，也不另建说明 Markdown。只将其中影响后续工作的稳定约束归入对应小节。

### 收尾检查

- “全面检查、完整链路、收尾”须覆盖全部相关修改及受影响的上下游，沿配置消费、激活、执行/表现至结束清理追踪实际调用；增量复核应说明范围。
- 检查失败回滚、取消/中断、重复激活、对象失效与清理。涉及网络时分别追踪权威端、自主代理、模拟代理，核对预测与服务端重检、复制乱序、引用晚到及开始/结束先后关系。
- 核对配置是否实际生效、默认值与接口是否一致，临时状态是否成对恢复，委托、计时器、Tick 依赖和引用是否正确释放；发现问题在原职责内修复。
- 结论须说明实际检查范围和未验证项。差异格式检查、静态审查、编译和运行验证各有边界，不能相互替代；遵守用户对编译/测试的限制。

## 工程方向与参考源码

- UE 5.8；Gameplay 核心用 C++，蓝图用于配置、装配、转发及明确保留的移动计算；暂不接脚本。项目专有类型用 `IMG`，通用类型遵循同目录命名，不沿用参考项目的专有名称或前缀。
- `Source/ImagineGame` 为共享运行时，`Source/ImagineEditor` 为编辑器模块；`Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime` 放射击玩法派生规则并依赖 ImagineGame，共享层不反向依赖玩法。
- 新规则先核对所有者、权限、复制与生命周期；共享机制扩展核心职责，玩法规则放对应 Game Feature。
- Lyra 参考：`D:/Workspace/UnrealEngine/Projects/LyraStarterGame`。核心源码保留本工程移动、Bot、Traversal 扩展；TopDownArena、LyraExtTool、自动化示例与发布脚本未迁入。文件对应不代表行为一致。
- GTA 参考：`D:/Workspace/Learning/Sources/GTAV Source/src/dev_ng/game`，工程导航见其仓库根目录 `AGENTS.md`。仅作为本地源码快照对照，不代表所有发行版本启用的能力；保留 UE/GAS/CMC 网络体系。
- UE 源码：`D:/Workspace/UnrealEngine/UE_5.8/Engine/Source`。当前工程有 Game/Editor Target 和编辑器 Validator/ContentValidationCommandlet；独立 Client/Server、渠道 Target 按打包需求设计。

### GTA 3C 源码导航与映射

- GTA 的 Player/AI 行为、Movement、Motion、局部动画分别有任务职责；入口为 `Peds/Ped.cpp::ProcessControl_Intelligence` → `PedMotionData.ProcessPreTaskTrees` → `PedIntelligence.Process` → `ProcessPostTaskTrees`。`scene/world/GameWorld.cpp::ProcessAfterAllMovement` 处理移动后任务与 PreCamera，PostCamera 另有回调。借鉴其意图、移动、表现和镜头的执行边界；本工程继续由 GAS 管能力、CMC 管移动与预测、Animation 管表现。
- 下表路径相对 GTA 的 `src/dev_ng/game`；ImagineGame 落点表示职责映射，不代表相应功能已经实现。

| 系统 | GTA 源码入口 | 可借鉴合同与 ImagineGame 落点 |
| --- | --- | --- |
| 输入与控制朝向 | `task/Default/TaskPlayer.cpp` 的 `CTaskPlayerOnFoot`、`CTaskMovePlayer`；`camera/CamInterface.cpp` | 玩家行为解释输入，MovePlayer 分派普通/Strafe/水中移动；控制 Heading/Pitch、瞄准 Frame 有独立查询。Hero 保留移动意图入口，镜头扩展须核对 ControlRotation 与最终画面的关系。 |
| 普通移动与步态 | `task/Motion/Locomotion/TaskMotionPed.*`、`TaskHumanLocomotion.*`；`task/Motion/TaskMotionBase.cpp::UpdateGaitReduction` | MotionPed 分派普通/瞄准/Strafe/潜行等；HumanLocomotion 有 IdleTurn、Start、Moving、Turn180、Stop、Reposition；GaitReduction 对照实际/动画速度及阻挡。规则扩展现有 Locomotion，启停/转向表现归 Animation，不复制真实速度/移动阶段。 |
| 情境镜头 | `camera/gameplay/GameplayDirector.*`、`follow/FollowPedCamera.*` | Director 消费角色、武器、掩体、水中等事实；FollowPed 有 Ragdoll/GetUp 与梯子对齐行为。扩展 CameraMode/选择规则，玩法提供镜头需求；镜头不取得移动状态所有权。 |
| 身体接触与 IK | `ik/IkRequest.h`、`IkManager.*`、`solvers/{LegSolver,ArmIkSolver,BodyLookSolver,TorsoReactSolver}.*` | 请求携带目标、混合参数，BodyLook 有优先级；LegSolver 有坡面/楼梯/动态支撑的脚与骨盆处理，TorsoReact 接收身体反应请求。映射 Animation/独立表现适配；目标失效、动作中断与优先级交接须释放本方请求。 |
| 受击与碰撞反应 | `task/Physics/TaskNM.*`、`TaskNMShot.*`；`task/Movement/TaskCollisionResponse.*`；`ik/solvers/TorsoReactSolver.*` | 局部身体反应、撞墙/避让与物理倒地有独立执行入口。伤害/反应规则由玩法或 GA 决策，表现适配执行；Ragdoll 移动层只消费跟随事实。恢复主链见 Ragdoll 小节的 `TaskGetUp` 对照。 |
| 掩体 | `task/Combat/Cover/TaskCover.*`；`camera/gameplay/GameplayDirector.cpp::UpdateFollowPedCoverSettings` | Seek/Enter/InCover/Exit、探头/盲射/转角及镜头协作；存在本地与 Clone 掩体信息消费。几何/接触机制可共享，射击交互规则归 ShooterCore；与武器、动画、镜头分别接合。 |
| 梯子与主动下沿 | `task/Movement/Climbing/{TaskGoToAndClimbLadder,TaskLadderClimb,DropDownDetector,TaskDropDown}.*` | 梯子区分接近、挂载、持续上下、滑降、阻挡和脱离；DropDown 独立检测与执行。持续交互需独立会话/移动合同，现有 Hurdle/Vault/Mantle 的一次 Montage 合同不能直接视为持续攀爬实现。 |
| AI 特殊路径动作 | `task/Movement/TaskNavBase.*`；`task/General/TaskBasic.*` 的 `CTaskComplexControlMovement` | 导航遇梯子时交接执行，成功恢复 FollowingPath，失败结束当前导航任务；LOD 可禁止 climb/drop/ladder。ImagineGame 可用导航链接适配现有 GA，须处理接近、接管、取消、失败重规划和路径恢复。 |
| 战斗翻滚 | `task/Motion/Locomotion/TaskCombatRoll.*` | Initial/RollLaunch/RollLand/Quit、方向与中止清理独立于 Vault。适合独立 GA，沿正常 Root Motion/CMC 执行，射击限制归玩法。 |
| 游泳与潜水 | `task/Motion/Locomotion/TaskInWater.*` | 水面与潜水分开 Motion task，有水位/净空、入水与游动过渡。MOVE_Swimming 只提供引擎移动基础；完整水中规则、表现与镜头需另行接合。 |
| 载体与上下车 | `task/Vehicle/{TaskGoToVehicleDoor,TaskEnterVehicle,TaskInVehicle,TaskExitVehicle}.*`；`task/Physics/TaskNM.cpp::OnMovingGround` | 门/座位与进入退出是交互执行链；移动地面有参照速度处理。Walking 复用 BasedMovement，Traversal/Ragdoll 的动态参照须扩展各自原合同；车辆规则归玩法模块。 |
| 场景交互 | `task/Scenario/Types/TaskUseScenario.*`、`task/Scenario/ScenarioPoint.*` | 接近起点、Enter、持续行为、Exit 与异常退出分段管理。可在现有 Interaction 上接交互位置/占用与执行；SmartObject 提供查询和预留，执行生命周期仍归调用方。 |
| 多角色预算 | `Peds/PedIntelligence/PedAILodManager.*`；`task/Physics/TaskNM.cpp` 的 Ragdoll Pool | AI、Motion、事件扫描、动画分时和布娃娃池分别有预算/优先级。ImagineGame 的 SignificanceManager 可作接点；降级须保留权威移动及关键动作完成/清理，不以统一停 Tick 代替。 |

- GTA 的梯子、DropDown 已有明确入口；持续悬挂/横移是否有可直接参考的完整实现仍须专项追踪。MoVE、NM、ClipSet、Clone 的实现合同依赖 RAGE；本工程按 UE 动画/物理及 GAS/CMC 合同重建对应能力，插件已启用不能作为项目链路完成的证据。

### GTA 玩法系统参考与职责

- 以下为本地 GTA 快照的源码导航与职责映射，不表示 ImagineGame 已具备对应玩法。共享事实/查询/资源合同可归 ImagineGame；具体反应策略、战斗、人口与警戒规则由对应 Game Feature 提供。现有 GAS、GameplayMessage、Team、Interaction、Equipment、Experience/GamePhase 保持原所有权。

| 系统 | GTA 源码入口（相对 `src/dev_ng/game`） | 可借鉴设计与本工程边界 |
| --- | --- | --- |
| 事件响应与打断 | `Peds/PedIntelligence.cpp::Process`；`event/{EventGroup,EventHandler,EventResponseFactory}.*`；`event/decision/EventDecisionMaker.*` | 扫描、决策、最高优先级事件与任务执行分开；HandlerHistory 保留被临时事件替代的非临时事件。GameplayMessage 适合通知，AI 的有效期、优先级、记忆与恢复策略归 AI 适配/行为层，GA 负责执行。 |
| 世界刺激 | `event/{ShockingEvents,EventShocking}.*` | ShockingEventsManager 管事件添加、非脚本事件去重、寿命覆写及按区域/来源清理，供角色观察、逃离和反应。可为枪声、爆炸、尸体、危险区域提供空间事实；消费者自行决定行为，不让消息总线承担持续世界状态。 |
| 感知与阵营关系 | `Peds/PedIntelligence/PedPerception.*`；`Peds/{Relationships,RelationshipEnums}.*` | 感知区分中央/周边视野、距离和姿态等修饰；关系组独立于角色类型，支持 Respect/Like/Ignore/Dislike/Hate 等。`ComputeFOVVisibility` 主要检查视野和距离，不能作为完整 LOS 证明。扩展 Team/AI attitude 的统一查询，AI 敌意与 CanCauseDamage 的玩法政策分别明确。 |
| 群体战斗协调 | `task/Combat/{CombatDirector,CombatSituation,CombatOrders}.*` | Director 管成员、战况及协调开火；Situation 的 Initialize/Uninitialize 对称，Orders 提供 HoldFire、CoverFire、射速和射击间隔修饰。可由 ShooterCore 的服务端协调层向个体 AI 发约束，个体继续通过能力和导航执行。 |
| AI 射击节奏 | `weapons/FiringPattern.{h,cpp,psc}`；`Peds/PedIntelligence.cpp` 的 FiringPattern Pre/PostProcess | 共享 Info 存储开火前延迟、每轮连发数、发间/轮间间隔；每角色 CFiringPattern 保存执行状态。接在 AI 开火请求层，武器自身射速、弹药与成本仍由现有武器能力校验。 |
| 场景交互与多人同步 | `task/Scenario/{ScenarioPoint,Types/TaskUseScenario}.*`；`network/General/NetworkSynchronisedScene.*` | ScenarioPoint 有使用计数、位置和时间条件；UseScenario 分接近、Enter、持续行为、Exit。同步 Scene 明确参与角色/实体/镜头、起点、Phase、Rate 与开始/结束。可在 Interaction/GA 上扩展位置占用及同步执行；沿 UE 权威/复制体系重新定义会话，不沿用 GTA 控制权迁移协议。 |
| 伤害事实与多路反馈 | `weapons/WeaponDamage.*`；`event/EventDamage.*` | WeaponImpact 分派 Ped/Vehicle/Object、伤害事件、AI、VFX、Audio 与 Ragdoll 请求。复用 EffectContext/HitResult、DamageExecution、GameplayCue/ContextEffects，具体受击反应走独立适配；生命变化、物理冲量、AI 感知和表现各有消费者。 |
| 世界人口与资源预留 | `Core/game.cpp` 注册 `CPedPopulation::Process`；`Peds/{pedpopulation,PopCycle,PopZones}.*`；`streaming/populationstreaming.*`；`network/Objects/NetworkObjectPopulationMgr.*` | 区域/时段配置、可用模型、实体增删和网络名额预留分别管理。扩展场景生成/Encounter 时复用 Bot/PawnData 创建链，业务决定数量/时机，预算与可用资产先批准；Significance 负责更新成本，不能代替生成预算。 |
| 犯罪、警戒与调度 | `game/{Crime,Wanted,witness}.*`；`game/Dispatch/{IncidentManager,Incidents,DispatchManager,DispatchServices}.*` | 犯罪事实、目击/报告、Wanted 与事件调度分别有状态；Dispatch 提供服务开关与资源创建限制，并申请网络人口预留。可映射为独立警戒/增援 Game Feature，事件/目击事实先于升级与出兵决策。 |
| 玩法资源所有权 | `script/Handlers/{GameScriptHandler,GameScriptResources,GameScriptEntity}.*` | Handler 跟踪资源/任务实体，具体资源类型处理释放；实体带 Cleanup/NoLongerNeeded 与所有者信息。借鉴获取句柄、明确交接和对称释放，复用 AbilitySet GrantedHandles、GameFeature ContextData 等现有模式；本工程仍以 C++/GA 实现玩法，不引入脚本体系。 |

- 人口源码优先核对 `pedpopulation.*` 的 `CPedPopulation` 与 `Core/game.cpp` 的注册调用，不能仅凭旧 `Population.*` 的 `CPopulation` 判定当前运行链。`PopCycle::Update` 的快照实现按小时/区域切换配置，但 WeekSubdivision 固定为 Weekday，周末切换代码被注释；最终配置值仍须查实际数据。
- 数据与运行状态分开：EventDecisionMaker 的共享规则/可修改组件、FiringPatternInfo/每角色 FiringPattern 可作参考。映射到本工程时复用 DataAsset/PawnData/能力配置，角色会话、记忆、冷却与占用归运行实例；单帧 override 须按原消费时序清理，持久规则使用显式所有权。

## 初始化、能力与 AI

### Experience 与 ASC 生命周期

- `AIMGGameMode` 选择 Experience，`UIMGExperienceManagerComponent` 加载；`UIMGAbilitySet` 授予能力、Effect、AttributeSet。`AIMGPlayerState` 持有 ASC/属性，`UIMGPawnExtensionComponent` 绑定 Pawn Avatar，`UIMGHeroComponent` 在 DataInitialized 初始化 ASC 和本地输入。
- Experience 优先级：URL Options → PIE 开发设置 `UIMGDeveloperSettings.ExperienceOverride` → 命令行 → World Settings → 专服/默认 Experience。
- Bundle：Standalone/Listen Server 加载两端，Dedicated Server 仅 Server，Client 仅 Client，PIE 同时加载两端。同一 Experience/ActionSets 的 GameFeature URL 去重后计数，保留跨 PIE World 请求计数。
- 已知边界：Experience 加载状态机尚未完整处理插件失败、加载中 EndPlay 与异步 Action 退出。
- `AIMGCharacter` 实现 `IGameplayCueInterface`，角色蓝图可处理 Cue；独立 Notify 由 GameplayCueManager 的全局 CueSet 分发。
- 玩家/Bot Controller.OnUnPossess 仅在 ASC Avatar 和 PawnExt 仍匹配旧 Pawn 时调用 PawnExt.UninitializeAbilitySystem；其顺序为取消能力、清输入/Cue → 清 Avatar/ActorInfo → 广播解绑 → 清组件 ASC 引用。取消保留 SurvivesDeath 能力；缺少匹配 PawnExt 时保留原生清 Avatar 路径，旧 Pawn 不得解绑新 Pawn 的 ASC。
- `UIMGHealthComponent` 发死亡事件，已授予的 `UIMGGameplayAbility_Death` 控制开始/结束，Character 在结束后销毁；自动重生能力位于 `ShooterCoreRuntime/GameModes`。

### 复制 Montage 前置条件

- ASC 的 `RegisterReplicatedMontagePrerequisite(Avatar, Name, Delegate)` 返回注册句柄；委托须弱绑定 UObject，只接受当前 Avatar。相同对象/Name 重复注册复用句柄，不同步执行回调；注册后、前置数据变化后调用 `RetryReplicatedMontage`。检查返回 NotApplicable、Waiting、Ready 或 Discard，业务 Session/Sequence 由提供者自行核对，不复制第二套播放阶段。
- 所有适用条件 Ready 才放行；只有所属表现系统提供非负 LogicalStartTime，其他条件返回 -1。多个起点提供者或非法起点保留等待并诊断，不以注册顺序/优先级覆盖。非所属 Montage 必须返回 NotApplicable；沿用原生 GAS 的 Montage/PlayInstanceId、播放/停止与位置同步，不能据此前置门控认定自主代理预测/重放已满足。
- `UnregisterReplicatedMontagePrerequisite` 按句柄精确注销，默认丢弃本对象参与阻塞的待播放实例；只有明确交接前置职责时才传 false。Discard 仅抑制当前 Avatar/Montage/PlayInstanceId 的本机表现，不改变权威能力或移动阶段。新实例不继承旧丢弃；结束先到不创建已停止的新实例，已播放实例保留原生停止路径。
- 门控/Retry 只作用于非权威、非本地控制角色；重试合并至下一 Tick 并防重入，回调后复核注册版本和实例。Avatar 切换、ClearActorInfo、EndPlay 释放注册、起点缓存和计时器；ASC 临时注销暂停处理/释放计时器，保留弱注册并在重新注册后重试。失效提供者不能释放已放弃的实例。等待原因变化输出 Verbose，连续等待5秒输出一次 Warning并复核，不因超时自动放行；`Log LogIMGAbilitySystem Verbose` 查看 Avatar/Montage/Instance/Reason。

### 持久 Bot 与 PawnData

- `UIMGBotCreationComponent` 在 Experience 就绪后由服务端按 Controller → PlayerState → Pawn 生成 Bot，配置 BotControllerClass并保留分队/出生点规则。重生分派支持玩家控制器及 `AIMGPlayerBotController` 派生类；当前无 StateTree 运行时代码。
- `AIMGPlayerBotController` 开启 bWantsPlayerState，每个 Bot 独享 PlayerState/ASC；共享 PawnData 资产不共享运行状态。BotCreation 不另设 PawnData 配置。
- GameMode.GetPawnDataForController 按 PlayerState 已认领 → Experience 就绪时 Bot.GetPawnData → Experience.DefaultPawnData → AssetManager.DefaultPawnData 查询。全局回退沿用同步加载/保活接口，缺失返回空并记录日志。
- Bot.GetPawnData 为 BlueprintNativeEvent/BlueprintPure，默认空并回退默认配置；只同步、无副作用地返回选择，不授予能力或生成 Pawn。首次认领前保持稳定，选择依据须早于 Controller.BeginPlay、PlayerState 成员赋值、分队和 Possess 可用；空值不表示等待异步注入。
- PlayerState 在 Experience 回调中首次认领，只有服务端首次 SetPawnData 授予 AbilitySets；已加载时注册立即回调，普通 SpawnActor 返回后再覆盖已太晚。后续生成、出生点体型检查及重生复用已认领数据，切换不同 PawnData 须另行设计。
- GameMode 按 PawnData.PawnClass 生成，在 FinishSpawning 前写入 PawnExt；Pawn 仍须装配 PawnExt/Hero 等组件。初始化沿 Spawned → DataAvailable → DataInitialized → GameplayReady；Bot 只跳过玩家输入条件与绑定。
- 网络复用 PawnExt.PawnData、Pawn.PlayerState 与 GAS；客户端由 OnRep_PawnData、OnRep_PlayerState 等推进初始化，模拟代理不要求 AIController。晚注册用 RegisterAndCall；IMGAbilitiesReady 仅在服务端 SetPawnData 发送，本机 OnAbilitySystemInitialized 不保证能力/属性已复制齐全。
- 独立敌人 AI 可用 Pawn 自持 ASC 或其他体系。AIMGCharacterWithAbilities 虽自持 ASC，仍继承 PawnExt，自行 InitAbilityActorInfo 不会自动推进至 GameplayReady。

## 角色与普通移动

### Movement Intent 与 CMC

- `UIMGHeroComponent::AddMovementIntent` 是世界空间移动意图统一入口，平滑方向、发急转事件并提交 AddMovementInput；玩家、自动奔跑和外部 AI 驱动复用。持续移动结束/中断须 ResetMovementIntent，输入结束、Controller 切换和 EndPlay 兜底清理。
- Intent 只塑形方向；`UIMGCharacterMovementComponent` 管姿态、速度、制动、网络预测。原生 AI Path Following 未被该入口接管。
- 普通移动朝向复用原生 CMC 规则，不按制动速度额外定向；胶囊保留逻辑/权威朝向，表现平滑留给 ABP。
- UE 网络默认：Exponential；模拟代理位置/旋转平滑 0.10/0.05 秒，Listen Server 远端角色 0.04/0.033 秒，普通/大误差纠偏最小间隔 0.10/0.05 秒。自主代理纠偏后重放 SavedMoves，不由代理插值、NetUpdateFrequency 或 NetworkPrediction 插件缓冲控制。
- GameNetworkManager 的 MAXPOSITIONERRORSQUARED=3.0，约 1.73 cm；Play As Client 即使不开 Network Emulation 仍走网络移动。先用 `p.NetShowCorrections 1` 定位，再查两端参数和预测，不以放宽容差代替修复。
- PreCMC 的本地待消费输入与服务端远端 RPC 加速度不可混用。ReplicateUsingRegisteredSubObjectList 只选择子对象复制路径，不自动同步 MaxWalkSpeed 等 CMC 参数。

### Locomotion 调度与职责

- `UIMGMovementPrerequisiteComponent` 为独立于 PawnExt/GAS 的抽象调度基类，缓存通用 Character/CMC并声明 BlueprintNativeEvent UpdateLocomotionPreCMC；仅支持 Character/CMC。独立派生默认 CanUpdateLocomotion=true，额外条件由原生覆写提供；不查 ASC，不主动注销/销毁，不另建复制、RPC 或预测。
- 默认 TG_PrePhysics，建立 Controller → PreCMC → CMC；弱引用和 ReceiveControllerChangedDelegate 处理 Controller 晚到、切换与解绑。BeginPlay 在蓝图回调前准备引用，回调后复核；重新 OnRegister 恢复调度；EndPlay/OnUnregister 幂等关 Tick、退订、移除依赖、清缓存。运行中更换 CMC 后需重新注册，不用 Tick 发现重绑。
- `UIMGLocomotionComponent` 适配 PawnExt/ASC/可选 LocomotionSet，CanUpdateLocomotion 返回 IsLocomotionReady；仅在 GameplayReady 且当前 Pawn 的 ASC/Avatar 有效时执行 PreCMC。ASC 解绑停止业务，重绑恢复；子类不重复管理基类缓存、Tick 依赖或更新事件。
- 只订阅 PawnExtension InitState，不注册独立 feature。调度就绪后 RegisterAndCallForInitStateChange 订阅 GameplayReady并立即回调，再用 RegisterAndCall 订阅 ASC 生命周期；Tick 不轮询 ASC。
- EndPlay/OnUnregister 先退订并清 PawnExt 订阅状态，再解除属性委托、清 ASC/Set并发 Unavailable，最后由基类释放 CMC/调度；重新注册重建订阅。
- `UIMGLocomotionSet` 提供七项可复制移动属性，可缺省；缺少时仍 Ready，GetLocomotionSet 返回空并 Warning。组件只读 CurrentValue、订阅变化，不创建 Set、授予 GE 或回写属性；仅在 ASC 初始化时检查 Set，UE 5.8 无公开 SpawnedAttributes 变更委托。
- 事件：OnLocomotionReady、OnLocomotionAttributeChanged、OnLocomotionUnavailable；C++ 覆写 UpdateLocomotionPreCMC_Implementation。IsMovementPrerequisiteReady 查询调度，IsLocomotionReady 查询业务，不可混用；Unavailable 时 ASC/Set 已清，CMC 有效时仍可用于清理。
- GetIMGCharacter、GetIMGCharacterMovementComponent、GetIMGAbilitySystemComponent 返回 IMG 类型。蓝图监听 GA 意图 Tag、读属性，在 PreCMC 写最终 CMC 参数；Foley/OnLanded、ABP 转发不属于该组件，不把 Tag 规则或专属委托/Tick 塞进 CMC。

## Ragdoll 移动基底

### 分层与入口

- 源码位于 `Source/ImagineGame/{Public,Private}/Character/Ragdoll`，包含 IMGRagdollComponent、IMGCustomMovementMode_Ragdoll、IMGRagdollTypes 三组文件，属于 ImagineGame。
- 依赖方向：GA → 可选协调组件 → CMC，由 CMC 分派自身 Mode。Component 管请求、只读采样、事件和绑定清理；CMC 管单一移动快照、装配及网络；Mode 管胶囊跟随、地面抬升、限制、碰撞/朝向接管和恢复落位。AIMGCharacter 不默认持有协调组件，CMC/Mode 不查找或引用它。
- 胶囊不模拟物理。协调组件不操作 Mesh 物理、碰撞、附着、快照、AnimInstance 或 Montage；身体物理与动画由独立适配层负责。移动层不依赖 GA/MM/PAC，不为其增加 Mover、PhysicsControl、Chooser/PoseSearch 或 GAS 依赖。
- 权威入口：`StartRagdoll` → `BeginRecovery(Session)` → `CompleteRecovery(Session)`。选片可先 `QueryRecovery(Session, Transform, Issue, BodyLocalOffset)`，再 `BeginRecoveryAt(Session, Transform, BodyLocalOffset)`；调用者显式完成/中断，无自动起身计时器。
- 无协调组件时直接使用 CMC.StartRagdollMovement、RecoverRagdollMovement/RecoverRagdollMovementAt、CompleteRagdollRecovery、StopRagdollMovement。持续 SubmitRagdollFollowTarget 提交世界空间 Transform、正反面、有效 InputSource、原 Session；Submit/Clear 均要求 Session，同一来源未 Clear 前其他来源不能覆盖。
- OnRagdollStarted、OnRecoveryStarted、OnRagdollEnded 携带 Session，CMC 提供 OnRagdollMovementUpdated。Phase/Session/Revision 只承担移动配置、跟随会话与恢复输入锁，不承担动画或 GA 业务状态机。

### 身体参照与跟随

- 默认位置为已模拟 `pelvis` 的骨骼世界原点，按骨骼索引读取；不是 Mesh 原点、胶囊中心或刚体质心。`spine_05` 辅助平面朝向与正反面，FaceUpAxis 为该骨骼胸口向外的局部轴，默认 Y；缺失朝向骨骼不停止 pelvis 跟随，朝向回退角色朝向、正反面默认 false。
- 无有效骨骼变换或模拟 pelvis 时返回当前胶囊目标；此回退不证明身体跟随生效。可覆写 GetRagdollFollowTarget，返回 false 清本组件输入。PelvisBone、OrientationBone、FaceUpAxis 由组件复制，骨骼名与轴须按资产配置。
- 协调器在 TG_PostPhysics、CMC 和缓存 Mesh 后采样；UE EndPhysics 包含骨骼物理混合及其并行完成任务，常规 PrePhysics CMC 下一次移动消费缓存，存在正常采样延迟。默认读最终混合姿势，权重小于1时不等于纯物理体变换；身体适配负责根部混合、附着和世界空间一致性，或覆写采样。
- 击飞链：权威 Start/开始事件 → 身体适配启用模拟并施加初速度/冲量 → 采样 → CMC 输入 → Mode 跟随。AdditionalVelocity 只合入 InitialVelocity，Move 不替身体施加冲量或模拟重力；仅 Start 不会产生击飞。无 Controller 遵循 bRunPhysicsWithNoController。
- 跟随不做完整胶囊扫掠，身体提供者负责接触；空中跟随目标，近地面按当前胶囊尺寸抬升。默认 MaxFollowSpeed=4000 cm/s、MaxTargetDistance=500 cm，bValidateTargetPath 开启旧中心到跟随位置的保守阻挡线检查，使用进入前胶囊响应。
- 目标失效、超距或阻挡清速度并报告原因，保留会话由上层处置；该检查不能证明完整身体碰撞或反作弊。高速击飞须按玩法配置限制。
- GetRagdollBodyMotion 为可覆写只读入口，返回模拟 pelvis 线速度 cm/s、角速度 rad/s；无有效身体返回 false并清零。身体稳定阈值/持续时长归 GA；MaxRecoverySpeed 只限制胶囊速度。

### 恢复几何与安全退出

- QueryRagdollRecovery 只查询，禁止重放和模拟代理执行。RecoverRagdollMovementAt 由权威复核 Session、有效且未过期的缓存目标、最终变换及量化后落位；拒绝前不改变阶段、输入或碰撞，BeginRecovery 复用该链。
- 原位优先，再搜邻域：默认半径100 cm、2圈、每圈8点，上限4圈/16点，半径0仅原位。检查可走支撑、IsWithinEdgeTolerance、当前完整胶囊、范围、平面约束及通路；下扫从低处渐进抬升，FloorClearance 限于2 cm至 CMC.MAX_FLOOR_DIST-0.1 cm。继承 CMC 移动忽略对象，使用进入前胶囊响应；无查询碰撞的胶囊拒绝开始。
- 恢复检查“采样身体点 → 匹配身体点 → 最终胶囊中心”两段 LOS；起点胶囊无重叠时另做完整胶囊通路扫掠，初始重叠时允许沿身体通路作有界局部脱离。
- BodyLocalOffset 为匹配的跟随点在最终单位缩放胶囊空间中的位置，默认零；须有限且不超过 MaxTargetDistance，由查询、批准及量化复核共同消费。它只作几何输入，不引入复制状态/动画依赖，也不证明完整起身动画手脚空间或深穿透脱困。
- 恢复批准后退出 Custom Mode，清 RequestedVelocity，限制普通移动输入、速度、跳跃、主动姿态切换及 Controller 朝向，允许正常 CMC Root Motion；恢复事件等待 Walking/落位就绪。
- 普通 AbortRagdoll(Session)/StopRagdollMovement 找不到安全出口返回 false并保留会话；成功经同一快照传退出落位，空中退出保留速度。缺少有效身体目标时只允许从无重叠胶囊沿完整胶囊通路退出，普通恢复仍拒绝缺失/过期/错会话/超距目标。
- AbortRagdoll(Session, true) 按 Unavailable 强制结束；无安全位置则 MOVE_None/禁碰撞并报告 ExitBlocked。所有者须安排救援、重生或明确恢复模式/碰撞，不自动 Walking；死亡和外部模式接管不被普通退出覆盖。

### 网络与装配生命周期

- 开始、恢复、完成由服务端批准；自主代理批准后预测跟随，服务端复核 Session、有限数值、距离、步长速度及恢复几何。AI/无自主代理需服务端身体或自定义目标；模拟代理只消费权威胶囊位置、速度、朝向和模式，不以本机身体反向驱动。
- 量化目标、朝向、正反面、Ready/Missing/Expired 和 Session 随 SavedMove/Packed MoveData 保存，非零 Session 的采样不合并；重放只消费历史输入，不采样身体或按当前时间判过期。坐标遵循 UE 零原点约定；远端自主代理禁用 Packed Movement RPC 时拒绝开始。
- MaxTargetAge 默认0.25秒；实时 GetRagdollMovementInput 复核采样/收包时效，远端静默在既有 CMC Tick 清速度并刷新复制。时间仅是本地缓存，查询不读取新姿势，不新增权威状态。
- CMC 属性与 MoveResponse 共用一个权威快照，Revision 拒绝旧/重复版本；配置随快照传输，不依赖客户端 CDO 一致。不回滚/复制逐骨骼物理，序列化布局变化要求各端同版。
- 快照与模式可乱序：结束通知等待退出 Ragdoll，恢复/立即完成或中断保留批准落位；自主代理即使只收到带落位的结束快照也补应用一次。同会话事件去重，旧会话回调不能影响新会话。
- ServerMoveHandleClientError 在原生应答生成时缓存时间戳/落位会话，MoveResponse 的 bRagdollPlacementReflected 只表达相关性；客户端仅在同会话 ACK/纠偏被接受、LastAckedMove 推进时标记落位已消费。忽略纠偏、关闭误差检查的 GoodMove 不作位置证明，避免生成前/打包后的错位及旧锚点回拉。
- Character.OnRep_ReplicatedMovement、CMC.SimulatedTick 仅在 Ragdoll 接管胶囊通道，旧 Ragdoll 模式应答也禁用表现 Root Motion 纠偏替代；退出恢复原生路径。Movement.Mode.Custom.Ragdoll 沿角色 Tag 映射。
- 服务端动态创建/注册可复制组件，客户端等待复制；同角色只绑定一个协调器。FindRagdollComponent 返回已注册、活动且已绑定 CMC 的组件，晚添加/重新激活/注册从当前快照补齐。
- 卸载协调器先退订、移除精确缓存的 Tick 依赖，保留目标供自己发起的会话安全退出，再按原 Session 清自己的来源；不终止其他调用者会话。替换/独立重新注册 CMC 后须重新激活或注册协调器，不用 Tick 发现新 CMC。
- CMC.Deactivate/OnUnregister/EndPlay 释放模式资源、输入与应答缓存，清理回调期间禁止 Start；晚快照可缓存，结束生命周期后不重新接管朝向。事件在移动模式和角色更新后发送，回调重入后复核对象/会话/模式；MOVE_None 保留死亡禁碰撞，Traversal 清理不覆盖后来的 Ragdoll/None。

### GTA 映射、调试与后续边界

GTA 主链为 `task/Physics/TaskNM.{h,cpp}`、`TaskRageRagdoll.cpp`、`TaskBlendFromNM.cpp` → `task/Movement/TaskGetUp.cpp`；保留其分层思路，网络合同映射到 UE。

| GTA 参照 | 本工程归属 |
| --- | --- |
| TaskNM.cpp:2808 身体线/角速度、连续稳定时长 | GA 等待稳定；Component 只提供事实 |
| TaskGetUp.cpp:1583 姿态匹配 | MM/Pose Matching 选择适配 |
| TaskGetUp.cpp:3944/4166/4193/4203 落位、邻域、两段 LOS | Mode 恢复几何与权威最终复核 |
| TaskGetUp.cpp:1641/4487/4494/4652 受阻等待、重试、爬行、救援 | 上层业务，不放进移动计时器 |
| TaskGetUp.cpp:3491 Finish_OnUpdate 克隆等待所有者 | Session/Revision、模式就绪与结束通知；保留 CMC 网络协议 |

- 身体参照：`peds/Ped.cpp`、`physics/gtaInst.cpp`；网络参照：`network/Objects/Prediction/NetBlenderPed.cpp`；调试参照：`ai/debug/types/RagdollDebugInfo.cpp`。
- 尚待独立实施：GA 业务、PAC/PhysicsControl 启停、MM/动画适配、进入预测与拒绝回滚、身体整体网络纠偏、速度/关节/姿态异常约束、深穿透脱困、载体相对事实/恢复锚点及跨角色预算/降级。GTA 的 FixStuckInGeometry、UpdateRagdollBlending、OnMovingGround、池化/LOD 按相应职责参考，不能据此断言 Chaos/资产没有同类能力。
- GetRagdollMovementIssue 是本机诊断。`IMG.Ragdoll.DebugDraw`：0关、1跟随/失败目标、2增加候选胶囊/支撑/两段通路；青点为采样点，黄点为匹配点。`IMG.Ragdoll.DebugDuration` 默认2秒，仅影响显式查询，跟随单帧；绘图受 ENABLE_DRAW_DEBUG 保护，CVar 为 Cheat并默认关闭。
- `Log LogIMG Verbose` 查看 [Ragdoll] 状态、原因及 ExitRejected，按角色/Role/Session/Revision 对照，结束用 `Log LogIMG Log` 恢复。

## Ragdoll GA 接入规划（尚未实现）

### 会话、激活与身体事实

- 一个派生 `UIMGGameplayAbility` 的 InstancedPerActor GA 持有“进入 → 等待稳定 → 选起身 → 恢复执行 → 完成/取消”。首版 ServerInitiated，客户端请求复用 GAS 能力/事件/TargetData；未来 LocalPredicted 须成对实现拒绝/回滚。业务步骤只作本地流程，不复制第二套移动 Phase/Session，不以 GAS 预测键替代 Revision。
- GA 调用协调组件/CMC，选择适配调用 MM，身体适配独立消费同 Session 事件控制可选 PAC。普通移动 Tag 留在 GAS/玩法配置；通用合同可放共享层，伤害、车辆/水域/平台、爬行和救援规则归具体玩法。
- 激活校验 Avatar、ASC 绑定、存活/可打断规则与协调器可用性，完成授予/事件/互斥配置后 Commit；Start 前绑定事件，成功后认领实际新 Session并处理同步回调。失败只清本次资源；有成本时明确 Commit 后 Start 失败的补偿策略，不接管他人会话。
- 首版建议 Independent 配合 Tag 阻塞/取消；Exclusive_Blocking 只阻塞其他独占能力，不能替代通用规则。保持可取消，死亡/PawnExt 解绑可结束它，不默认 SurvivesDeath；尸体物理需显式交接。重复受击可更新当前最短等待/提交冲量，恢复再受击先结束旧执行，再开新会话。
- 业务最短倒地时间之后，GetRagdollBodyMotion=true且线/角速度均达标才累计连续稳定时长；超限、采样失败、Avatar/Session 变化清零。用 GA 管理的 AbilityTask/有界周期与最大等待时间，EndAbility 清理；目标过期后的胶囊零速度或失败采样零输出不代表身体稳定。
- 专服无身体模拟时不能证明身体稳定；须选择服务端轻量模拟、自定义只读事实，或明确的客户端建议＋服务端规则/几何重检降级。实施时定义数据来源、时效、误差与信任范围，不用胶囊速度替代，不让 Movement 依赖 PAC。

### 选片、网络与恢复执行

- MM/Pose Matching 输出 Montage、有限正倍率、起始时间、最终胶囊变换及与采样点一致的 BodyLocalOffset。先 QueryRecovery 取几何参考，再 BeginRecoveryAt 提交实际匹配；异步加载/选择后重检会话与身体位置，失配重选或延期。
- 静态落位不证明完整动画空间；持续 Root Motion与手脚空间由动画合同和正常 CMC 扫掠验证。客户端选片限定 Spec/Activation/Avatar/Session，复用 GAS TargetData/取消通道；服务端按允许资产、时长、倍率/起点及受信匹配点配置重建/重检，不直接信任客户端位置/任意偏移。专服无 PoseHistory/资产未就绪时须有后备选择或明确拒绝。
- 同 Session OnRecoveryStarted 且 Walking/落位就绪后，权威和自主代理按同一批准合同各自启动 GAS PlayMontageAndWait并检查成功；绑定完成/中断/取消，复用 GAS Montage复制和 Root Motion。OnBlendOut 不提前完成；仅权威 OnCompleted 调 CompleteRecovery(Session)，中断用 CompleteRecovery(Session, true)，拥有者本地结束仍等权威生命周期。
- UE 5.8 OnRep_ReplicatedAnimMontage 不为本地控制的自主代理播放；批准选片、时间和 Session 须经 GAS/表现合同传给拥有者。模拟代理不选片/判稳定/提交目标，按 Session 与 Montage/PlayInstanceId 关联等待模式/引用，乱序补齐一次，结束先到不得重启旧表现；该关联不成为第二套移动阶段。
- 后续动画适配可独立注册 ASC 复制 Montage 前置条件，按自己的 Session 与 Montage/PlayInstanceId 判断，不查询 Traversal 或覆盖其他提供者。公共合同见“复制 Montage 前置条件”；不向 RagdollComponent/CMC 引入 ASC/Traversal 依赖。

### 失败、结束与验收范围

- 批准前几何拒绝保留 Simulating，按原因有界重试：缺失/过期先恢复数据，受阻等环境，动画/加载失败释放候选。批准后同步回调可能结束会话/改模式，须读最终 Session/Phase，不能凭 BeginRecoveryAt=false 判断仍在 Simulating。
- 超时后的继续倒地、爬行、救援/重生由玩法决定；交接前 GA 仍持有会话，不得丢弃一次 Abort=false 后直接结束。
- EndAbility 遵循 IsEndAbilityValid、ScopeLockCount、bIsAbilityEnding；先屏蔽重入，清本次任务/计时/加载句柄、TargetData/取消委托及组件/ASC 订阅，再按原 Avatar/缓存组件/Session 清理。正常 CompleteRecovery；异常 Recovering 用 CompleteRecovery(..., true)，Simulating 用 AbortRagdoll(Session, true)；组件失效可由原 Avatar 缓存 CMC.StopRagdollMovement(Unavailable, Session) 兜底。
- 只终止自己的 Montage/身体适配实例，旧回调同时匹配 Activation/Task 实例和 Session，不影响新会话/Pawn。强制结束成功只代表会话结束，ExitBlocked/MOVE_None 仍须交玩法救援。
- 待验收：进入/Commit/动画失败、连续受击与恢复再受击、坡面/台阶/墙角/低顶/初始穿透、来源缺失/过期、死亡/取消/ASC解绑/卸载、生命周期乱序/合并、引用晚到、自主纠偏重放、模拟代理；包括应答生成前后恢复/完成、GoodMove ACK、忽略纠偏和晚快照。按单机 → Listen/Dedicated → 延迟/丢包 → 多角色预算推进。
- 车辆/移动平台未来由来源提供相对事实，扩展原输入/SavedMove/快照并重检；Walking 继续复用 BasedMovement，不预埋玩法类型判断或直接 Attach 绕过预测。

## Traversal

### 目录、检测与选择

- 源码位于 `Source/ImagineGame/{Public,Private}/Character/Traversal`，属于 ImagineGame。Detector 无状态同步查几何；Component 管配置、Warp Target、临时碰撞忽略与代理快照；GA 管检测 → 选择 → 校验/Commit → 播放 → 退出；Types/TargetData 定义数据与请求，Debug 在 Private 同目录。
- 每次激活只作一次尝试，外部决定输入/自动触发/重试频率，RequestSource 与 ActivationPolicy 独立。角色装配 Traversal/MotionWarping并授予 GA；CHT/PoseMatch 经 PreSelectTraversalAnimation/SelectTraversalAnimation 选单 Montage，服务端无 PoseHistory 时需后备。
- InitiationSettings 在检测后、选择前及 GA 校验/服务端重检消费；bEnabled=false 只跳过本组发起限制。Manual 默认允许150 cm内站立发起；Automatic 默认允许 Falling，要求朝向前沿的有效意图、方向阈值0.8，固定 AutomaticMaxDistance=150 cm，不以实际速度设门槛或二次缩距。
- 意图优先待消费输入，再 CMC.Acceleration；AI 复用 Manual，在两者为空时可读 Moving 的 PathFollowing 当前段目标，不以实际 Velocity 替代意图。普通玩家按 GA.RequestSource，服务端 AI 强制 AI 来源；AI 检测/发起共用意图，无意图时可向前按手动站立规则尝试。
- ServerDistanceTolerance 仅补偿远端发起距离。速度缩放用于 Detector 搜索长度；Candidate.Speed/RunningSpeedThreshold 仅供选择建议，不限制原生 Supports，外部 CHT/蓝图可另筛。MaxMovingSpeed/MaxAngularSpeedDegrees 用于障碍/载体运动，检测、落点和执行继续复核。
- 几何/坡面/净空/动作支持与最终动画覆盖分开；自动深度安全限制、高位开放接近拒绝及楼梯下行方向限制仅用于 Automatic，楼梯轮廓过滤也用于 Manual/AI。ActionSettings 提供默认执行规则，MontageSettings 按 Action/Montage 整份覆盖；WarpBindings/RequiredTargets 管锚点/接触，Notify 管窗口，Landing 管曲线/落点。最终复核表面、落差、可走支撑和完整胶囊，Root Motion/MotionWarping 执行位移。
- Traversal 的 GroundLocation 只取 CMC.CurrentFloor 的有效可走支撑；空中或缺少有效支撑（含无碰撞支撑的 NavWalking）回退当前缩放胶囊底部，不消费 GetGroundInfo 的长距离下探。坡面/脚下楼梯判定只消费有效支撑；接近角度和自动落差检查共用该参考。bUseGroundPositionForHeight 默认 false；开启后也按同一支撑/脚底回退，不把远处地面当高度基准。
- 低障碍在高度小于 DetectorSettings.UseRequestDirectionBelowHeight（默认80 cm）时优先保留请求方向，允许斜向越过边沿；该方向的候选失败后可重试墙面法线方向。设为0可关闭此分支；MinFacingDot（Manual/AI默认0.3）和 AutomaticMinFacingDot（默认0.8）限制接近角度。调试 Handhold: request heading 表示所用方向，HClear/VClear 为净空测量，不是动画轨迹。
- 楼梯语义由 DetectorSettings.StairSurfaceTag 指定，默认 Traversal.Stairs；加在实际碰撞组件的 Component Tags 或所属 Actor 的 Tags，使用普通 FName 标签。命中前侧/顶部带标签，或角色在地面且脚下带标签时启用楼梯轮廓过滤；未标记不保证从几何识别楼梯。默认边缘采样垂直跨度截止值22.5 cm，命中楼梯表面时30 cm；与 GTA 参考一致，严格小于截止值才拒绝，不额外添加容差，等于截止值仍可通过本项。跨度小于40 cm且水平漂移大于10 cm也拒绝。Span不是台阶高度，Height默认相对胶囊底部；顶部平台为独立组件/Actor时须覆盖实际命中边缘，不能仅依赖下面阶梯的标签。DebugDraw 2 的 Stair profile 显示标签、前侧/顶部/支撑命中状态、精确跨度和截止值；标签不表示禁止全部攀爬。
- 表面策略在 Traversal组件类默认值的 DetectorSettings 中配置：AutomaticBlockedSurfaceTags 仅拒绝 Automatic，BlockedSurfaceTags 拒绝所有来源。例如给局部楼梯/顶部边缘加 Traversal.NoAuto，并将同名标签加入 AutomaticBlockedSurfaceTags，可禁止该表面自动翻越、保留手动检测；此自定义名字须显式加入列表才生效。Actor标签影响其全部碰撞组件，局部限制优先用 Component Tags；Traversal.Stairs 启用过滤，不等于禁止全部攀爬。

### 执行、网络与清理

- GAS LocalPredicted：校验 → Commit → 建立 Target/碰撞忽略 → Flying → Montage启动 → 提交 TargetData/发布快照。启动前单次取消；服务端按方向重查来源、Action、时间/倍率、Warp合同和可选 AllowedMontages，不接受客户端几何。CHT 前拒绝不启动执行，Commit 后重入/播放失败走既有退出。
- 完成/中断/取消/死亡/目标失效/ASC解绑幂等清理 Target、碰撞忽略、委托、计时器；正常 BlendOut 不提前结束。恢复碰撞后按支撑切 Walking/Falling，仅恢复仍 Flying 的执行，不覆盖后来的模式；PawnExt 解绑顺序见 ASC 生命周期。
- 模拟代理消费 COND_SkipOwner 快照、GAS Montage和 CMC，按 Montage/PlayInstanceId 等待引用、Target、碰撞忽略和 Flying。结束快照先到保留本机实例至退出（含 BlendOut），同实例引用补齐不反复拆建；Target 恢复历史来自权威，非 Flying 到达后先恢复碰撞再查支撑。
- Traversal 通过独立句柄注册 ASC Montage 前置条件。ReplicatedMontages 非空时，选片与复制归属仅接受此列表及 MontageSettings 中的资产，仍可复用 ActionSettings，不再按 Target 同名推断；空列表保留现有推断以兼容装配，多 Warp 系统应配置各端一致的明确列表。结束快照保留 Montage/PlayInstanceId，开始被合并或结束先到且尚未播放时丢弃该次表现，不能被晚引用重新启动。
- 来源组件 → Traversal → CMC，组件仅活动时 Tick；MotionWarping.OnPreUpdate 在实际采样前刷新 Target。默认0.05秒复核载体，运动上限500 cm/s、85 deg/s，候选时效0.25秒。局部 Target 跟随不等于完整支持动态平台；BasedMovement/动态 Bone、输入历史/退出延迟、异步检测、悬挂/Shimmy/梯子另行设计。
- BeginPlay/激活/重新注册建立 MotionWarping、PawnExt/ASC及精确 CMC 依赖；停用/注销/EndPlay 释放绑定、Target、碰撞忽略，清理期间 IsTraversalAvailable=false，非活动时晚快照不重启执行。只有本组件仍有活动 Traversal且 CMC Flying 时恢复移动；拒绝缺少 Root Motion、非正/非有限 RateScale，检测通道须有效。

### 调试与 GTA 参考

- 绘图默认关闭，受 ENABLE_DRAW_DEBUG 保护。`IMG.Traversal.DebugDraw`：0关、1输出/活动Target、2搜索失败、3全部探针；DebugStages 掩码：搜索1、命中2、手点4、体积8、输出16、执行32。
- 完整观察：`IMG.Traversal.DebugDraw 3`、`IMG.Traversal.DebugStages 63`、`IMG.Traversal.DebugProcessDuration 3`、`IMG.Traversal.DebugDuration 3`。`IMG.Traversal.DebugLog 2` 记录启用阶段且须开启绘图；按角色/Role 区分，几何标记 pre-selection，执行输出在动画落点解析后更新。
- `Log LogIMGTraversal Verbose`、`Log LogIMGAbilitySystem Verbose` 按 Avatar/Spec/PredKey 对照，结束恢复 Log。PredictionRejected 表示激活拒绝；TargetData 重检失败发生在激活后；AuthorityEndedPredictedTraversal 只说明权威先结束，不能据此认定预测错误。
- GTA 检测/手点：`task/Movement/Climbing/{ClimbDetector,ClimbHandHold}.*`；发起：`task/Default/TaskPlayer.cpp`；Jump/Vault 分流：`task/Movement/Jumping/TaskJump.*`；执行/清理：`task/Movement/Climbing/TaskVault.*`。
- GTA 人形搜索绕 Z、多层球扫水平、坡面后筛；SearchForHandHold 探针来自固定层值/覆写，GetClimbDetectorParam 的 MBR 插值影响发起范围，JumpVault 有步态门槛，AutoVault 结合摇杆、受阻降步态和冷却且纯自动要求落地。本工程速度搜索缩放、固定发起范围及默认允许空中属项目策略；不机械移植动画表、手动位移或网络协议。

## 镜头

### 调用链与职责

- Hero.DetermineCameraMode 选择能力覆盖或 PawnData.DefaultCameraMode，CameraComponent 回退 Default；CameraModeStack 更新/混合，CameraComponent 输出视图。TPS 顺序为姿态/未滞后枢轴 → 位置 Lag＋PelvisOffset → 阻尼朝向的轨道偏移 → Shake → 防穿墙。旋转误差使用本模式 FOV、UE 投影及分屏尺寸，不按栈混合或后续特效重算；无本地目标 Controller/视口时按相机 AspectRatio 回退。
- `Camera/IMGCameraRotationDamping.h/.cpp`、`IMGCameraShake.h/.cpp` 只有调用方持有的数值配置/历史，不查 World、角色、动画、Controller、GAS 或视口，不自行 Tick/复制。TPS 负责采样和表现，步态/瞄准规则归蓝图或具体玩法；不向 ABP、CMC 或 Mesh 写回。
- View.ControlRotation 保留原始期望朝向，阻尼/Shake 只改表现。武器和 AimAssistInputModifier 仍取最终玩家视点，因而受镜头表现影响。

### 配置与蓝图合同

- Rotation Damping、Pelvis Offset、Shake 均默认关闭。旋转按 Yaw/Pitch 两轴先平滑目标角速度，再收拢角度；频率为 Hz，Roll 透传，MaxErrorFraction 限制相对各轴半 FOV 的误差。
- Shake 各轴振幅默认0；位置 X/Y/Z 为未摇晃相机的前/右/上，旋转以局部四元数叠加，FOV 偏移后限于5至170度。GetShakeInput(TargetActor, DeltaTime) 在每个启用模式的视图更新中调用一次，原生输出 Active=false、AmplitudeScale/FrequencyScale=1；蓝图按步态/曲线返回启停及非负倍率，C++ 不固定奔跑或 Tag 判断。Active=false 用最后一次激活倍率混出，关闭 Enable Shake 立即清空；持续驱动使用 HoldTime=-1。
- 通用 Shake 支持 Sine/DampedSine、相位/随机种子/DC Offset，以及预延迟→Attack→Decay→Hold→Release 包络；曲线为 Linear/SmoothStep/Exponential，默认 Release 为 exp(-7t)。Start 幂等，释放期间重启保留相位并从当前包络恢复；有限 Hold 可单次 Start 后自然结束。Phase/RandomSeed 在新实例 Start 时消费；其他 Shake 的触发和实例调度归调用方。
- PelvisOffsetSocket 默认空，PelvisOffsetHeight 须按模型/模式校准，不以启用瞬间姿势标定。目标修正 = Socket 世界高度＋配置偏移－未滞后的完整姿态枢轴高度，经连续死区、权重、上下限及独立弹簧后加在位置 Lag 外。只读 Character.Mesh 最近完成的骨骼/Socket，支持直接 LeaderPose 映射；它跟随最终身体高度，不能分离纯 FootIK，采样时效受动画降频/更新顺序影响。

### 重置、调试与参考

- CameraComponent.ResetInterpolation/NotifyCameraCut 向所有在栈模式转发，下次完整视图更新消费，回调中的新请求留给下一次。组件注销时停用栈、清单帧 FOV 偏移，重注册重新激活；栈停用期间回退原生视图，不输出空模式结果。TPS 激活、目标/控制器变化或可观察到的 CameraCut 重置历史，重置帧强制重查碰撞探针；停用/关闭释放本方状态，没有通知的瞬移须显式重置。Pelvis 的 Mesh/骨骼资产/Socket 改变或采样失效时清空回退原枢轴。
- 零步长不推进弹簧/振荡或求导，但仍应用轴旁路及 FOV/偏移上限；零时长包络阶段可立即完成。超过32/120秒：旋转清历史并直达目标，Pelvis 直达新目标，Shake 清空并输出零；无效输入/配置分别回退原朝向、原枢轴或零 Shake。位置 Lag 最多32个子步，超出预算直达目标。
- `showdebug camera` 查看 Shake 包络/倍率/阶段及 Pelvis 修正/来源。Draw Pelvis Offset 默认关闭：白点为原枢轴，绿点/竖线为补偿结果，红色为回退，文字为实际厘米数；单帧绘图受 ENABLE_DRAW_DEBUG 保护。
- GTA 路径相对 `camera/`：`gameplay/follow/FollowPedCamera.cpp::UpdateRunningShake` 对照运动驱动；`helpers/{FrameShaker,Oscillator,Envelope}` 对照数值层；`base/BaseCamera.cpp` 对照 PostEffectFrame；`gameplay/GameplayDirector.cpp::RegisterExplosion` 复用同类 Shaker。`gameplay/aim/FirstPersonShooterCamera.cpp::UpdateFrameShakers` 的 FPS 头骨跟随尚未迁入。

## 装备、背包与射击

- 装备、背包、快捷栏、远程追踪、武器生成器基类保持原职责；快捷栏/物品实例可跨模块使用。装备授予能力后，由快捷栏为装备实例关联物品。
- 通用开火、换弹、自动换弹位于 `ImagineGame/Weapons`；具体武器、快捷栏输入、丢弃/拾取派生逻辑位于 ShooterCoreRuntime。开火复用远程追踪，派生处理 TargetData、服务端校验与弹药成本。
- DropWeapon 只移除快捷栏/背包物品，未生成地面拾取物；初始发放和死亡清理依赖蓝图装配。校验阈值、自动装弹轮询与资产/实机链路仍需针对性验收。

## UI 与设置

- UI 可订阅快捷栏 SlotsChanged/ActiveIndexChanged、生命/死亡委托；装弹提供开始/结束事件，自动重生提供可配置消息 Tag。
- GameSettings.UGameSettingDetailView 通过 HandleCurrentSettingChanged 订阅当前 Setting 的变化并刷新描述/动态详情；DLC Registry 定时变更沿同一路径，不另轮询。
- 切换/清空 Setting、NativeDestruct 时退订；NativeConstruct 保留当前选中项并重建详情/扩展。切换、清空、关闭、释放 Slate 均取消并清空扩展异步句柄；完成/装配回调复核当前 Setting，避免旧请求或重入回填过期控件。
