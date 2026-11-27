# GTA 3C 与玩法参考

本地参考路径见 [工程方向与编辑器](project.md)。各领域的执行细节见 [装备表现](equipment-presentation.md)、[武器](weapons.md)、[Traversal](traversal.md)、[Ragdoll](ragdoll.md) 和 [镜头碰撞](camera-collision.md)。

## GTA 3C 源码导航与映射

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

## GTA 3C 过渡与交接候选

- **梯子检测与翻墙检测独立。** `TaskPlayer.cpp::CanJumpOrVault`（约8001/8177）消费 `CClimbDetector` 手点/障碍结果，`DoJumpCheck`（7686）及 `CTaskJumpVault::WillVault` 分流 Jump/Vault。梯子由 `CheckShouldClimbLadder`（4451/4509）调用 `CTaskGoToAndClimbLadder::ScanForLadderToClimb`（832），扫描 NearbyObjects 中 GetIsLadder 的模型以及静态建筑注册的 ms_Ladders，沿 ET_LADDER/CalcLadderInfo 取得顶、底、法线及出口信息；不是用通用墙沿探测识别任意梯子网格。方向另在 `IsPedOrientatedCorrectlyToLadderToGetOn`（1132）校验输入期望朝向/速度、梯面交点及入梯侧，底部背面进入还受距离与 m_CanMountBehind 元数据限制；梯子不等于无方向的附近触发器。
- **入口与执行边界。** `TaskPlayer.cpp` 的普通状态选择约6641处理梯子、6678处理 DoJumpCheck，3555创建独立的 CTaskGoToAndClimbLadder；梯子可由移动意图触发，不证明 GTA 将梯子、Jump、Vault 全绑在同一跳跃按键。可映射共享玩家意图入口、独立候选检测与独立执行生命周期；本工程是否用单个分流 GA 属于 UE 侧设计，不能宣称 GTA 本身使用此结构。

- 资产目录线索（只枚举目录）：`Content/Characters/Heroes/Mannequin/Animations` 已按 Actions、Interactions、OverlaySystem、Poses、Unarmed 组织。Unarmed 下有 Avoidance、LookAtPOI、Ragdoll、MotionMatchingData（Databases/Schemas/Traversal）、StateMachineData、Traversal（Catch/Climb/Hurdle/Mantle/Vault）及 Bench/Shoves/Tackle/Takedowns 交互；Rifle Overlay 有 AimOffsets/Locomotion，CasualGirl 有独立 Animations/Rigs。覆盖面支持优先复用现有分层，具体选片、运行接线和内容质量待验证。
- **Character｜注意力与局部反应。** GTA `TaskAmbient.cpp` 提交 BodyLook 请求，`ik/IkManager.cpp::Request` 的主姿态分支可拒绝低于当前优先级的请求，`BodyLookSolver` 分部位加权并处理混出；`weapons/WeaponDamage.cpp` 另提交命中位置、方向和部位给 TorsoReact。IMG 可从现有 LookAtPOI/Rifle Overlay 验证瞄准与 POI 的抢占/释放，从已确认的伤害事实验证局部受击与全身硬直/Ragdoll 的交接。表现由 AnimGraph 消费，来源负责是否发起；伤害、移动及身体约束沿现有权威链。
- **Character｜有交接条件的起停与世界交互。** GTA `TaskHumanLocomotion.cpp::StateMoving_OnUpdate` 同时检查转向幅度及近期方向一致性，再进入 Turn180；Turn180 读取动画允许提前退出/混出的信号，Stop 使用步态、脚相位及中断信息。IMG 的 Hero 已有 Pivot 绕过及 GAS 事件入口，可用现有 Motion Matching/状态机验证急转连续性和落脚匹配。GTA `TaskUseScenario.cpp` 分接近、Enter、持续与 Exit，并在异常清理路径处理道具；IMG 可从现有 Interaction 的 GA 触发链和 Bench 目录验证单个交互点的占用、对齐、取消和释放。
- **Camera｜构图切换保持同一关注点。** GTA `ThirdPersonPedAimCamera.cpp::ReorientCameraToPreviousAimDirection` 在满足首帧、非锁定等条件时沿旧瞄准方向取世界点，从新枢轴重新定向，另有炮塔分支。IMG 的 `IMGCameraMode_ThirdPerson` 已有显式焦点，`IMGCameraComponent` 混合后统一碰撞与构图。建议从普通视角进入 ADS 的短交接验证，保留玩家视点与准星一致；当前远程能力会读取玩家视点，须验证画面变化对瞄准的实际影响。
- **Camera｜手动输入与自动跟随交接。** GTA `ControlHelper.cpp::UpdateLookAroundInputEnvelope` 提供玩家看向输入包络，`FollowCamera.cpp::ApplyPullAroundOrientationOffsets` 用它压低自动回正，并按宿主速度、空中策略与设备配置加权。IMG 默认第三人称已有旋转阻尼，自动策略可先限定为手柄普通跟随。Hero 提供本机手动输入事实，CameraMode 消费表现权重。现有 `GetCameraView` 会将 Mode 的 ControlRotation 写回 Controller，移动方向也依赖该值；引入回正时须明确表现旋转与控制朝向的归属，验证手动接管、停步、空中、瞄准及设备切换。
- **Control｜手柄响应与短时意图。** GTA `ControlHelper.cpp` 对摇杆幅值加曲线、角速度加减速，`ThirdPersonAimCamera.cpp::ComputeLookAroundOrientationOffsets` 对自由瞄准按当前 FOV 缩放看向。IMG 已有可配死区、普通/瞄准灵敏度，原生 `HeroComponent::Input_LookStick` 以输入、角速度常量与帧时间求增量。候选是在 Enhanced Input/Hero 调整摇杆响应，按需接入瞄准倍率。GTA `TaskPlayer.cpp::Jump_OnUpdate/CanJumpOrVault` 保留短时按下时间，等待当前动作允许交接，并区分跳跃退出窗口与手动翻越检测窗口。IMG 的 `ProcessAbilityInput` 消费当轮 Pressed、另按 WhileInputActive 重试；若需缓存跳跃/翻越，仅为该业务保存一次有期限的意图，重新验证候选并按正常 GAS/CMC 路径发起，成功、超时、取消及换 Pawn 时清理。具体输入映射、资产行为及联机效果待验证。
- 取舍：先用现有资源验证手柄普通视角至 ADS 的响应与画面焦点，以及一个 POI 优先级或 Bench 交互的完整退出。跨体型/FOV 自动取景已有 GTA FootRoom 参考，等出现具体构图需求再与手调模式比较。RAGE 的 MoVE、IK 调度器、任务树与 GameplayDirector 整套迁移不在当前候选内。

## GTA 玩法系统参考与职责

- 以下为本地 GTA 快照的源码导航与职责映射，不表示 ImagineGame 已具备对应玩法。共享事实/查询/资源合同可归 ImagineGame；具体反应策略、战斗、人口与警戒规则由对应 Game Feature 提供。现有 GAS、GameplayMessage、Team、Interaction、Equipment、Experience/GamePhase 保持原所有权。

| 系统 | GTA 源码入口（相对 `src/dev_ng/game`） | 可借鉴设计与本工程边界 |
| --- | --- | --- |
| 武器定义与近战/枪械配置 | `weapons/info/{WeaponInfo.h,WeaponAnimationsManager.psc}`；`weapons/{WeaponFactory.cpp,Weapon.cpp,WeaponEnums.h}`；`task/Weapons/Gun/TaskGun.*`、`task/Combat/TaskCombatMelee.*` | 快速静态核对：基础定义共用 CWeaponInfo，FireType 区分 Melee/InstantHit/DelayedHit/Projectile 等，GetIsMelee 依据 DamageType，GetIsGun 依据 Flag；工厂常规武器创建同一 CWeapon。CWeaponAnimations 同时含射击、近战 ClipSet 与 WeaponSwapData/WeaponSwapClipSetHash；执行由 Gun 与 Melee 任务分开，CWeapon::Fire 对 Melee 返回 false，不能将配置共用等同于攻击链共用。可借鉴共用装备/动画选择机制、分离攻击执行；不据此给本工程增设新的武器继承中间层。 |
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
- GTA 装备与骨架主链已核对：`weapons/inventory/PedEquippedWeapon.cpp::AttachObject` 从所属 `m_pPed` 解析 `BONETAG_L_PH_HAND/R_PH_HAND`，从武器模型解析 `WEAPON_GRIP_R`，再 `AttachObjects(m_pPed, ...)`；`Peds/Ped.cpp::SetModelId` 创建 Ped 自身 Drawable/Skeleton/AnimDirector。当前链不以“隐藏驱动实体→独立 Retarget 外观实体→外观实体挂枪”为合同，因此不直接作为本工程多 Mesh 装配参考；本次没有证明 RAGE 全部系统不存在重定向。
- GTA 动画并非运行时只能使用一套固定集：`modelinfo/PedModelInfo.h` 配 Movement/Strafe 等 ClipSet，`weapons/inventory/WeaponItem.cpp::RequestAnims` 按 Ped/武器/第一或第三人称等请求多种 ClipSet，`task/Motion/TaskMotionBase.*` 与 `Peds/Ped.cpp::GetMotionTaskFromType` 支持覆盖及交接。可确认预制骨架/资产合同与本工程工作流不同，不能把它表述成完全没有动画选择、覆盖或适配机制。
