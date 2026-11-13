# ImagineGame 工程约定与导航

## 工作约定

- **最高优先级｜沉淀每次探索的新发现。** 每次源码、配置、资产或工具调查收尾前，将新发现逐项归入本文件对应小节。已有条目直接补充或纠正，结构变化时清理过时内容。已验证事实、线索和待验证项分开写，保留可复核入口。调查过程与逐轮结果留在当次回复。
- 事实优先：先核对本地源码、官方资料或可复核案例，区分已验证事实、资产名线索和待验证项。改代码前找到调用链、同类实现与所属模块，在原职责处扩展，避免重复状态、分支和抽象。
- 新增 struct：用户指定目标 `.h` 时写入该文件；未指定时先调查合适位置，再请用户确定头文件。
- 保持单向有效依赖和模块独立性。仅在关键约束、不直观的生命周期与边界处写短注释；回复只给关键结论、证据、变更和必要风险。
- 未明确要求不编译；C++ 变更仍须核对引擎接口、访问权限、构建依赖、调用链和差异。不调查或修改 Content，除非用户要求。
- 不编写测试代码；运行测试由用户自行完成，发现问题后按反馈修复。交付前仍须完成必要的静态核对，不以此替代运行验证。
- 先扫本文标题，只读相关小节。工程导航、职责边界、接口合同、已确认限制、待办及必要调试用法统一归入 AGENTS。修复过程、编译/测试结果和逐轮审查记录留在当次回复。

### 收尾检查

- “全面检查、完整链路、收尾”须覆盖全部相关修改及受影响的上下游，沿配置消费、激活、执行/表现至结束清理追踪实际调用；增量复核应说明范围。
- 检查失败回滚、取消/中断、重复激活、对象失效与清理。涉及网络时分别追踪权威端、自主代理、模拟代理，核对预测与服务端重检、复制乱序、引用晚到及开始/结束先后关系。
- 核对配置是否实际生效、默认值与接口是否一致，临时状态是否成对恢复，委托、计时器、Tick 依赖和引用是否正确释放；发现问题在原职责内修复。
- 结论须说明实际检查范围和未验证项。差异格式检查、静态审查、编译和运行验证各有边界，不能相互替代；遵守用户对编译/测试的限制。

## 装备、背包与射击

### 组件、资产与授予

- 背包、装备、快捷栏、通用开火/换弹/自动换弹和远程追踪归 ImagineGame。具体武器资产、快捷栏输入、丢弃和原生拾取派生入口归 ShooterCore。PlayerController 装配 `UIMGInventoryManagerComponent`、`UIMGQuickBarComponent` 和 `UIMGWeaponStateComponent`，Pawn 装配 `UIMGEquipmentManagerComponent`。可在射击 Experience 或其 ActionSet 中用 Add Components 为对应类配置服务端与客户端组件。WeaponState 驱动当前远程武器的散布 Tick，并确认命中标记。
- `UIMGInventoryItemDefinition` 蓝图的 `EquippableItem.EquipmentDefinition` 指向 `UIMGEquipmentDefinition` 蓝图，`SetStats.InitialItemStats` 初始化每个物品实例的弹匣余量、备弹余量和弹匣容量。三个 Tag 由资产指定，各消费者使用同一组 Tag。准星和快捷栏图标分别配置 `ReticleConfig` 和 `QuickBarIcon` 片段。
- `EquipmentDefinition.InstanceType` 指向 `UIMGRangedWeaponInstance` 或其蓝图子类，`AbilitySetsToGrant` 引用武器 `UIMGAbilitySet`。`ActorsToSpawn` 配置枪体 Actor、`AttachSocket` 和 `AttachTransform`，角色装备挂到角色 Mesh。需要远端显示时由枪体 Actor 配置复制。
- 服务端发放沿 `AddItemDefinition` → `AddItemToSlot` → `SetActiveSlotIndex`。EquipmentManager 在当前 Pawn 的 ASC 上授予能力，Spec 的 SourceObject 为装备实例，QuickBar 随后将物品实例设为装备的 Instigator。开火/换弹用这两个引用读取武器和弹药，故由装备 AbilitySet 授予并保留 `InstancedPerActor`。切槽卸下旧装备并清除其能力、效果和枪体，弹药随仍在背包里的物品实例保留。初始发放和死亡清理依赖蓝图装配。

### 输入、开火与换弹

- `UIMGAbilitySet.GrantedGameplayAbilities[].InputTag` 与 `UIMGInputConfig.AbilityInputActions` 精确匹配，IMC 将按键映射到对应 Input Action。InputConfig 可由 PawnData 提供，或由射击 Game Feature 的 `Add Input Binds` 提供，IMC 由 Hero 的默认映射或 `Add Input Mapping` 装载。
- 开火 GA 继承 `UIMGGameplayAbility_WeaponFire`，必填 `MagazineAmmoTag` 和 `DamageEffect`。`OnInputTriggered` 每次按下尝试一发，`WhileInputActive` 按住时在上一发 GA 结束后重新尝试。`FireIntervalSeconds` 默认 0.1 秒。开火 Montage 和 Fire/Impact Cue 可选。C++ 已在提交射击时由服务端扣一发弹匣余量，额外 InventoryItem Cost 会再次扣同一弹药。
- 换弹 GA 继承 `UIMGGameplayAbility_WeaponReload`，设置 `MagazineAmmoTag`、`SpareAmmoTag`、`MagazineSizeTag`。无 `ReloadMontage` 时以 `ReloadDurationSeconds` 完成，默认 1 秒。有 Montage 时由完成回调或指定的 `ReloadCompleteEventTag` 完成，先到者生效。提交后服务端按弹匣缺口与备弹取较小值转移弹药。需用 Notify 提前完成时，确保服务端也收到该事件。换弹持有 `Ability.Weapon.NoFiring`，开火 GA 的 `ActivationBlockedTags` 配置同一 Tag 才能覆盖普通按键激活，基类的 `SourceBlockedTags` 只检查传入的来源标签。
- 自动换弹 GA 继承 `UIMGGameplayAbility_AutoReload`，保留 `OnSpawn` / `LocalOnly`，与开火、换弹一起由装备授予，输入 Tag 留空。配置弹匣、备弹 Tag 和 `ReloadEventTag`，在换弹 GA 的 `Ability Triggers` 添加同一 Tag，触发源选 Gameplay Event。默认每 0.25 秒检查，距装备或上次开火至少 0.5 秒、空仓且有备弹时发送换弹请求。请求 Tag 与 Montage 完成 Tag 分开。
- 弹匣/备弹/弹匣容量、`InputTag.Weapon.*` 与 `IMG.Weapon.SteadyAimingCamera` 都不在 `Config/DefaultGameplayTags.ini`：该文件只登记 `InputTag.Aim/Jump/Sprint/Strafe/Walk` 等，ADS 那个 Tag 只有 `IMGRangedWeaponInstance.cpp` 的 `UE_DEFINE_GAMEPLAY_TAG_STATIC`。新增武器须先把这组 Tag 登记到 ini，才能让物品 Stats、三个 GA 和镜头模式按同一字符串匹配。
- `UIMGGameplayAbility_WeaponFire::ActivateAbility` 显式调用 `UIMGGameplayAbility_FromEquipment::ActivateAbility`（实际落到 `UIMGGameplayAbility::ActivateAbility`），所以 `UIMGGameplayAbility_RangedWeapon::ActivateAbility` 的 TargetData 委托绑定和 `OnRangedWeaponTargetDataReady`（Lyra 蓝图出道口）不在开火 GA 链上；只有直接派生 `RangedWeapon` 并自行调用 `StartRangedWeaponTargeting` 的蓝图才走那条路。

### 命中、伤害与拾取

- 开火走 `CameraTowardsFocus` 的即时追踪。玩家从最终视点计算方向和起点，服务端校验 TargetData 的起点距离、射程、条数及遮挡。`BulletsPerCartridge` 是每次消耗一发弹药产生的射线数，`MaxDamageRange` 单位 cm，`BulletTraceSweepRadius` 默认为 0。散布用热量三条曲线和姿态倍率，ADS 倍率读取镜头模式 Tag `IMG.Weapon.SteadyAimingCamera` 的混合权重。`SpreadRecoveryCooldownDelay` 当前读取的 `LastFireTime` 未随开火更新，开火后延迟恢复仍需修复。
- 追踪与重检都使用 `IMG_TraceChannel_Weapon`（`ECC_GameTraceChannel2`）。项目 `Config/DefaultEngine.ini` 当前只登记了 `Traversable`（Channel1），缺少武器 Channel2 及 `AIMGCharacter` 请求的 `IMGPawnCapsule` / `IMGPawnMesh` Profile。若按 Physics Asset 命中装配，固定使用 Channel2，胶囊设 Ignore，有查询碰撞的角色 Mesh/Physics Asset 与应挡弹的场景设 Block。用 `IMG.Weapon.DrawBulletTraceDuration 2` 对照射线与碰撞响应。
- 常规 `DamageEffect` 用 Instant GE 的 `UIMGDamageExecution`。执行读取射手 `UIMGCombatSet.BaseDamage`（C++ 默认 0），再乘距离曲线、物理材质 Tag 倍率和 TeamSubsystem 裁决。基础伤害由射手 ASC 的初始化 GE 提供，按武器变化可由装备 AbilitySet 授予随卸下移除的持续 GE。目标需 ASC 和 HealthSet。首轮验收用异队目标，观察服务端生命与物品弹药。
- 场景拾取用 `AIMGShooterWeaponSpawner`，实例 `WeaponDefinition` 指向 `UIMGWeaponPickupDefinition`，填物品定义、展示 Mesh、正数冷却和 Pawn Overlap。基类 `AIMGWeaponSpawner` 的发放走蓝图 `GiveWeapon`。Shooter 派生类服务端发物品并放入空槽，默认选中该槽，`BlockAutoEquipTag` 可阻止切换。启用 `bRefillDuplicateWeaponAmmo` 时，按同一 `SpareAmmoTag` 将已有武器的备弹补至物品默认值。`UIMGGameplayAbility_QuickBarSlots` 监听选槽和前后切槽事件，选槽索引用 `EventMagnitude`，从 0 开始。`DropWeapon` 当前移除快捷栏及背包物品，地面掉落实体待接入。
- 以上装配合同只由源码与项目配置确定；不要依据现有 Content 里的武器相关资产推断配置，其中有无效资源，需要时按源码重建。

## 工程方向与参考源码

- UE 5.8；Gameplay 核心用 C++，蓝图用于配置、装配、转发及明确保留的移动计算；暂不接脚本。
- 命名按职责区分：项目专有类型用 `IMG`；通用概念遵循 UE 类型前缀及同目录风格，不因定义在本工程就机械添加 `IMG`，例如步态枚举用 `EGait`，不用 `EIMGGait`。不沿用参考项目的专有名称或前缀，不为统一前缀批量重命名既有类型。
- `Source/ImagineGame` 为共享运行时，`Source/ImagineEditor` 为编辑器模块；`Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime` 放射击玩法派生规则并依赖 ImagineGame，共享层不反向依赖玩法。
- 新规则先核对所有者、权限、复制与生命周期；共享机制扩展核心职责，玩法规则放对应 Game Feature。
- Lyra 参考：`D:/Workspace/UnrealEngine/Projects/LyraStarterGame`。核心源码保留本工程移动、Bot、Traversal 扩展；TopDownArena、LyraExtTool、自动化示例与发布脚本未迁入。文件对应不代表行为一致。
- GTA 参考：`C:/Application/gta5/src/dev_ng/game`，工程导航见 `C:/Application/gta5/AGENTS.md`。仅作为本地源码快照对照，不代表所有发行版本启用的能力；保留 UE/GAS/CMC 网络体系。
- UE 源码位于 `D:/Application/UnrealEngine/UE_5.8/Engine/Source`，`Engine/Build/Build.version` 为 5.8.4；旧 `D:/Workspace/UnrealEngine/UE_5.8` 路径当前不可用。当前工程有 Game/Editor Target 和编辑器 Validator/ContentValidationCommandlet；独立 Client/Server、渠道 Target 按打包需求设计。

### ImagineEditor 目录与接口

- 用户明确要求ImagineEditor按文件类型分离：所有 `.h` 放 `Public`，所有 `.cpp` 放 `Private`，配对文件使用两侧相同的相对目录。不因内部辅助用途把头文件留在Private，也不为这次分离额外拆出System/Style/Feedback/Rewind/Export子目录；已有动画、验证和Traversal分类保留。文件搬到Public不代表自动增加跨模块DLL导出API。

| 目录（相对Source/ImagineEditor） | 职责 |
| --- | --- |
| `Public/*.h`、`Private/*.cpp` | 模块入口、ImagineEditorEngine、ImagineEditorStyle、ContextEffects Factory与AssetTypeActions。 |
| `Public/AnimationModifiers`、`Private/AnimationModifiers` | 动画修改器及FootstepUtils。 |
| `Public/Validation`、`Private/Validation` | 资产验证器。 |
| `Public/Commandlets`、`Private/Commandlets` | ContentValidationCommandlet。 |
| `Private/Utilities` | 独立编辑器工具与控制台操作。 |
| `{Public,Private}/Character/Traversal` | Rewind轨道/回看接合及事件分类。 |
| `{Public,Private}/Character/Traversal/Trace` | Trace分析存储及文本导出。 |
| `{Public,Private}/Character/Traversal/Widgets` | Details与Slate控件。 |

- 模块内头文件include从Public根写相对路径，不使用 `../` 穿越或依赖Private同目录头搜索。公开头直接包含的AssetTools/Slate/SlateCore/RewindDebuggerInterface/TraceServices列为Public依赖，TraceAnalysis及PropertyEditor实现依赖仍留Private。UBT自动发现子目录CPP，无需为搬迁增加IncludePaths。只改路径不改反射类型、模块名或 `/Script/ImagineEditor.*` 配置身份；生成头及UE_INLINE_GENERATED_CPP_BY_NAME继续使用原类型文件名。

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
- Experience 失败为本机终态：定义/Bundle/插件失败不广播 Loaded；HasExperienceLoadFailed/GetExperienceLoadError 提供诊断，加载屏保留失败原因，不自动重试、切图或新增蓝图失败引脚。各端仍独立加载，不复制失败裁决；同一 Manager 不支持运行中切换 Experience。
- 失败/EndPlay 共用退出链：先阻止后续激活并取消本机加载句柄/延迟计时器，再按实际进入的 Action 阶段逆序停用，等待原生 Deactivation Pauser 后 Unloading/Unregistering，最后释放已完成的插件请求。未完成插件由自己的晚回调释放；收尾不强持有 Manager/旧 World。Pauser 调用方必须在游戏线程执行完成委托，不能遗失。
- 跨 PIE 的未释放插件计数不在新一轮 BeginPIE 清空；共享输入映射 Action 的全局注册按使用者计数，最后一个使用者退出才注销。Bundle 仍由全局 AssetManager 持有，不按单个 World 强制卸载共享资产。
- 既有 WaitForExperienceReady 节点保留 OnReady 接口；加载失败、Manager EndPlay 或 World 退出时停止等待并解除委托/计时器及 GameInstance 保活，晚回调不再广播就绪。成功回调保持高/普通/低优先级，各回调之间复核退出状态。
- `AIMGCharacter` 实现 `IGameplayCueInterface`，角色蓝图可处理 Cue；独立 Notify 由 GameplayCueManager 的全局 CueSet 分发。
- 玩家/Bot Controller.OnUnPossess 仅在 ASC Avatar 和 PawnExt 仍匹配旧 Pawn 时调用 PawnExt.UninitializeAbilitySystem；其顺序为取消能力、清输入/Cue → 清 Avatar/ActorInfo → 广播解绑 → 清组件 ASC 引用。取消保留 SurvivesDeath 能力；缺少匹配 PawnExt 时保留原生清 Avatar 路径，旧 Pawn 不得解绑新 Pawn 的 ASC。
- `UIMGHealthComponent` 发死亡事件，已授予的 `UIMGGameplayAbility_Death` 控制开始/结束，Character 在结束后销毁；自动重生能力位于 `ShooterCoreRuntime/GameModes`。

### 能力输入绑定

- `FIMGInputAction` 只保存 InputTag/InputAction；`UIMGInputComponent::BindAbilityActions` 对有效 Action/Tag 按 IFE 的 LyraInputComponent 固定绑定 Started→Pressed、Completed→Released，不配置每项 PressedTriggerEvent 或 BindReleaseEvent。NativeAction 仍由调用方显式选择 TriggerEvent。
- 默认 PawnData 输入和附加 InputConfig 均经 Hero 的 Pressed/Released 转发当前 Avatar 匹配的 ASC，再由 PlayerController.PostProcessInput 调用 ASC.ProcessAbilityInput。GA.ActivationPolicy 决定 OnInputTriggered/WhileInputActive/OnSpawn；Started 在输入开始时转发一次 Pressed，Held 状态保持至 Released，避免持续 Triggered 被 WaitInputPress 当作下一次点按。
- Completed→ASC.AbilityInputTagReleased 移除 Held 并登记 Released；ProcessAbilityInput 更新 Spec.InputPressed，AbilitySpecInputReleased 发 InputReleased 通用事件，GA.WaitInputRelease 依赖该链及自身预测键向服务端传递释放。移除配置开关不能删掉释放转发。ProcessAbilityInput 固定本轮 Pressed/Released/Held 列表，能力回调中新排入的输入留给下一轮；ClearAbilityInput 或 Avatar 切换使旧轮次失效，禁止跨 ASC 共享临时激活列表或重入消费。ASC Avatar 切换、ClearActorInfo、EndPlay 清理输入。输入解绑的绑定句柄与 ASC.ClearAbilityInput 保持原职责；额外输入配置的移除仍是现有未实现边界，不因固定绑定而视为已解决。

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

### 输入、GAS 与 Locomotion 职责

- `HeroComponent.AddMovementIntent` 统一处理玩家、自动奔跑和外部 AI 的世界空间方向，再提交 AddMovementInput。输入结束、取消自动奔跑、换 Controller 或 EndPlay 清理意图。原生 AI Path Following 独立；急转回调可能同步取消或替换请求，回调后须复核再提交输入。
- **步态选目标，GE 改目标值。** GA 管准入、互斥和意图释放，HeroLocomotion 用 Sprint/Walk、Aim/Strafe 四个 Tag 形成请求，再按输入方向和姿态选择实际 Gait。只有 Walk/Run/Sprint/Crouch 四项速度属于 `LocomotionSet`，便于各类 GE 共用速度入口，避免 GA 分散写 CMC。Set 构造默认 165/380/585/170，差异由 AbilitySet 初始化 GE 设置基础值。PlayerState 持有的 Set 随 ASC 保留，换 Pawn 读取已有 CurrentValue。
- Locomotion 汇聚属性与步态，统一计算并写入 CMC。基础加速度、地面摩擦、行走制动为组件配置，默认 800/2/800；Hero 按步态、速度、前侧后倍率和 0.3 秒落地窗口派生。Aim/Strafe 冲刺还须满足 50° 方向条件。Crouch 用独立速度属性，Crawl 沿 CMC 限速。组件配置和曲线在预测历史存在期间保持稳定。
- HeroLocomotion 的 `StrafeSpeedMapCurve` 在组件类默认值中配置为可选软引用。BeginPlay 在接合 CMC 前解析一次，未配置或加载失败时记 Warning 并使用前向倍率。权威端与自主代理使用一致的配置，EndPlay 解除移动绑定后释放曲线缓存。
- **单向接合。** Locomotion 通过通用弱委托注册一个 CMC 提供者，CMC 只理解请求字节及 `IMGMovementSimulationContext.h` 中的通用上下文和参数，字段含义由提供者解释。Controller → PreCMC → CMC 的 Tick 依赖供本地输入与表现更新；蓝图覆写 PreCMC 须调用父实现。远端 RPC 按 Move 自带输入模拟，逐 Move 计算和重放不派发 PreCMC 蓝图事件。
- **动态 Set。** GameplayReady、当前 ASC/Avatar、真实 Set 与提供者同时有效才 Ready。等待 Set 时关闭 Tick，CMC 按原生移动。AbilitySet/Game Feature 走 ASC 的 `AddRuntimeAttributeSet` / `RemoveRuntimeAttributeSet`，客户端在 `PostRepNotifies` 后重检。下一次 PreCMC 接合并读取已施加初始化 GE 的值。移除或换绑释放旧代次、恢复接管前参数；物理子步中的 Set 变化等当前 Move 结束再释放。授予方先结束依赖 Set 的 GE。停用、注销、EndPlay 精确退订，旧提供者不能覆盖新提供者。换 CMC 须重新注册。

### 逐 Move 预测与权威纠偏

- **把模拟输入钉在 Move 上。** 原生 CMC 已记录原生移动状态和输入；外部写 MaxWalkSpeed/MaxAcceleration 或启用子对象复制不会把值纳入历史。AttributeChanged 只更新下一次捕获的输入。ControlledCharacterMove 在缩放前捕获，SavedMove 保存请求、上下文、提供者代次和参数快照。Hero 上下文是四项速度与落地标志。参数或请求变化阻止跨状态合并，重放按历史输入及纠偏后的起点重算，保留非本组件所有的制动摩擦快照，不回写 GAS 或覆盖实时步态。
- **区分力度与加速度。** Packed Move 传 0–255 的输入力度和原生舍入方向，服务端及重放按当次加速度上限重建物理加速度，避免碰撞降速或合并回退后以旧上限解释力度。力度和请求、上下文变化参与 ImportantMove。权威端按自身 Set 重算，外推保留已接收的输入并采样当前属性。本地权威/AI 不生成客户端 SavedMove，模拟代理沿原生同步、只做本机表现。
- **指纹发现分歧，响应接合权威事实。** Packed Move 带本机 Revision/代次及输入、实际参数两份 32 位指纹，按字宽混合并借用 FNV 常量。两份都不同时可提前要求位置/速度纠偏，即使历史位置差很小；仅无关步态属性不同可随 ACK 更新上下文。指纹不授予客户端限速，也不代替原生位置验证。Packed Move/Response 两端须同版。
- 权威上下文在处理 Move 时留存，响应按 PendingAdjustment 时间戳回传。只在原生推进 ACK、对应 SavedMove 与代次/Revision 匹配、纠偏未被业务过滤时接收。逐属性本机变更标识允许新的合法 GAS 预测覆盖旧权威值；普通 OnRep 保留标识，避免复制乱序让晚到旧值撤销更新。停用/注销/换绑作废旧历史；Ragdoll 和 Traversal 维持各自接管及纠偏规则。
- **场景 Buff 的时序。** GE 的 Duration 不决定执行端。场景触发按施加目标的 Authority 裁定，避免把非复制触发器 Self 的本机 Role 当成服务端判断。客户端可在合法 GAS 预测窗口预测已知操作；服务端独知的生效/到期仍有信息延迟，可能产生首次纠偏。现有速度上限按 Move 切换；如需平滑模拟目标，两端须按 Move 时间推进并保存过渡状态。

### 自主代理表现与排查

- 拥有者纠偏时胶囊立即采用权威位置和速度，再重放 SavedMove。模拟代理插值参数不控制这条链。CMC 仅在本地自主代理的普通地面/下落移动中，以首个已接受纠偏前到重放后的**净位移**补偿网格。40 cm 内反向补偿重力平面分量并限制累计量，按 0.05 秒时间常数衰减。移动 Tick 前撤销、后应用；相机跟胶囊。瞬移、Root Motion、Ragdoll、网格更换和停用清理。
- Play As Client 即使未开 Network Emulation 也会纠偏。先用 `p.NetShowCorrections 1` 与 `IMG.Movement.NetDiagnostics 1` 对照历史误差、权威响应和 `[AutonomousMeshCorrection]` 的 PostReplayDelta/MeshOffset；历史位置差为零仍可能修正速度。级别 2 对照 Predict/Authority/Replay 的限速、力度、摩擦和制动。用 `IMG.Movement.AutonomousMeshSmoothing 0` 做表现对照。单机也急刹时先查超速制动，两端分歧时查 Move 输入与时序，不靠放宽误差掩盖。
- `LogIMGLocomotion` 默认输出 ASC/Set 等待、就绪和四项速度。权威端在 ASC 配对后缺 Set 或 Set 变更后失去 Set、Hero 在 BeginPlay 缺方向曲线、移动上下文或参数无效时直接记 Warning。客户端等待 Set 复制记 Log。用 `Log LogIMGLocomotion Verbose` 查看 GameplayReady、Set 变更排队和解绑。提供者冲突或 Packed RPC 未启用见 `LogIMGMovementNet`。

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

### 职责与装配

- 源码位于 `Source/ImagineGame/{Public,Private}/Character/Traversal`，属于 ImagineGame。以 UE 合同为准：GAS 管能力和 Montage 生命周期，CMC 管权威移动、预测与纠偏，MotionWarping 修改根运动；GTA 只对照职责与算法意图。

| 入口 | 职责 |
| --- | --- |
| `IMGTraversalDetector` | 无状态同步几何查询，产生候选和建议动作，不选择动画。 |
| `IMGGameplayAbility_Traversal` | 检测、CHT/PoseMatch 选片、校验/Commit、播放和退出。请求共用 `OnTargetDataReady → ResolveTraversalRequest → StartTraversalExecution`。 |
| `IMGTraversalComponent` | 配置、Target、临时碰撞忽略、Flying 接管/释放和复制快照。 |
| Private `IMGTraversalPlacement` | 实际播放路径、窗口链、落点、可选补偿和显式 Landing 校验。 |
| Private `IMGTraversalReplay.cpp` | 同一 Component 的逐 Move 上下文记录/恢复，无独立复制服务。 |
| Types / TargetData / Debug | 公共数据、客户端请求、私有观测采集、独立Trace与DrawDebugLibrary表现；编辑器轨道位于ImagineEditor同层级Character/Traversal。 |

- 保持来源组件 → Traversal → CMC 的源码依赖；CMC 只暴露通用委托/闭包接口，不包含 Traversal 类型。热插拔指组件与能力运行时接合/释放，不表示共享运行时可作为独立插件卸载。
- Pawn 装配 Traversal、MotionWarping 并由 ASC 授予 GA。`ActionSettings` 提供动作通用执行规则；`MontageSettings` 按 Action/Montage 整份覆盖，不合并字段。`WarpBindings/RequiredTargets` 管接触目标，Notify 管窗口，`Landing` 管落地合同。选片只返回一个 Montage；服务端本地/AI 选片缺少 PoseHistory 时须有后备，远端请求使用客户端选择并由服务端重检。

### 检测、发起与选片

- 每次激活只尝试一次，外部决定触发时机，`RequestSource` 与 `ActivationPolicy` 独立。`CanActivate` 作本地几何/发起预筛，激活后依次执行 `PreSelectTraversalAnimation`、`SelectTraversalAnimation`。Chooser 消费入场 MovementMode/Gait/Speed，必须早于 `BeginTraversal` 切 Flying。
- `SampleHandhold` 采样中心/左右顶部、宽度和楼梯轮廓，`MakeCandidate → ValidateCandidateDirection` 验证方向。方向回退复用本次公共采样，但从原始候选副本重算深度、后沿、净空和坡向；不跨 Detect 调用、帧或网络端缓存，不省略服务端重检。
- `InitiationSettings.bEnabled=false` 仅跳过发起限制。Manual 默认允许站立、范围150 cm；Automatic 默认允许 Falling，要求有效意图、朝向点积≥0.8、固定范围150 cm，不按速度二次缩距。速度缩放只影响搜索长度；`Candidate.Speed/RunningSpeedThreshold` 仅供建议/选片，不限制原生 Supports。
- 意图优先待消费输入，再取 CMC.Acceleration，不以 Velocity 替代。玩家使用 GA.RequestSource；服务端 AI 强制 AI 来源，复用 Manual 规则，无输入/加速度时可取 Moving 的 PathFollowing 当前段目标，无意图时可按手动站立规则向前尝试。
- 发起距离由当前 Actor 与前沿重新计算 XY；`Candidate.PlanarDistanceToLedge` 保留检测时的选片/诊断值。`ServerDistanceTolerance` 只放宽远端发起距离，不扩大服务端几何搜索。发起角度使用本次请求方向和重检前沿，不换成收包时朝向。
- PreSelect、Select、Commit 回调后复核原 ASC/Avatar/Component。Commit 后还须核对入场位姿、胶囊、Mesh/AnimInstance、BaseOffset 和参与表面的变换，防止旧落位结果应用到已变化的对象。候选默认时效 `MaxCandidateAge=0.25 s`。
- Supports 与推荐动作分开：Mantle 要求前沿，Vault/Hurdle 要求前沿+后沿；`RecommendedAction` 是独立选片建议。`bHasBackFloor=false` 表示尚未确认后方支撑，不能当作落点。空中 Height 随当前脚底升高而减小，需结合 StartTime 和选片核对。

### 手点、坡面与表面规则

- GroundLocation 只取 CMC.CurrentFloor 的有效可走支撑；空中或无有效支撑（含无碰撞支撑的 NavWalking）回退当前缩放胶囊底部，不读取 GetGroundInfo 的长距离下探。`bUseGroundPositionForHeight` 默认 false，开启后仍使用同一回退规则；坡面/脚下楼梯只消费有效支撑。
- 手点取前侧命中点上方的中心/左右实际顶部接触，不固定向内平移或投影回前侧平面。`UseRequestDirectionBelowHeight=80 cm` 以下优先请求方向，设0关闭；左右接触连线求交并插值高度，调整范围50 cm，失败再用原始手点/墙面法线重检。此近似不替代双手 IK。`MinFacingDot` 默认 Manual/AI=0.3、Automatic=0.8；HClear/VClear 是净空，不是动画轨迹。
- `AngularSurfaceTag` 默认 `Traversal.Angular`，读取命中组件或所属 Actor 的 FName 标签。请求与入面方向 dot>0.95 时，高障碍也可保留请求方向，失败回退法线。低障碍、棱面或 Automatic 要求双侧支撑；其余 Manual/AI 可追加外侧探针并重定中心，新中心组件须纳入障碍跟踪。标签不引入车辆滑行动作。
- 楼梯由 `StairSurfaceTag=Traversal.Stairs` 指定：前侧/顶部命中带标签，或站立时脚下带标签，启用轮廓过滤；未标记不保证识别。边缘跨度截止值默认22.5 cm、命中楼梯表面时30 cm，严格小于才拒绝，等于可过；跨度<40 cm且水平漂移>10 cm也拒绝。Span 不等于台阶高度，Height 默认相对脚底；独立顶部组件也须覆盖标签。
- `BlockedSurfaceTags` 拒绝全部来源；`AutomaticBlockedSurfaceTags` 仅拒绝 Automatic。Actor Tags 作用于全部组件，局部限制优先 Component Tags。例如 `Traversal.NoAuto` 必须显式加入自动禁用列表才生效；`Traversal.Stairs` 只是启用过滤，不禁止全部攀爬。自动深度限制、高位开放接近拒绝和楼梯下行方向限制只用于 Automatic，楼梯轮廓也用于 Manual/AI。
- 深度断层可能在曲面上形成后沿。`AutomaticMinDepthSamples` 是结束扫描前的最小扫描次数，不是连续顶面通过次数。`Candidate.GroundNormal` 是深度命中的平均法线，不能替代补偿实际命中的法线或可走判定。
- **平台可走坡度归 CMC。** BackFloor、补偿与落地检查使用 CMC.IsWalkable。通用上限为角色 Character Movement → Walking → Walkable Floor Angle（UE 5.8 原生约44.8°，以组件实际配置为准）；局部平台用碰撞组件 Physics → Walkable Slope Override。`MaxClearanceSlopeAngle=42.5°` 只控制净空探针方向。`MaxMovingSpeed=500 cm/s`、`MaxAngularSpeedDegrees=85°/s` 限制载体运动，检测及执行均复核。

### 播放路径与预测能力

- GA 以 `StartSection=NAME_None`、Selection.StartTime 启播。资产 `GetPlayLength()` 不代表本次末端；Placement 从 StartTime 所属 Section 沿资产 NextSectionName 解析有限路径，合并相邻段，将前跳/后跳映射到顺序播放时间轴。根运动、DistanceCurve 和窗口筛选只累计实际路径，显式窗口编号保留资产原始序号。循环、无效链或活动 Warp 跨非连续边界在 Commit 前拒绝。
- Motion Matching/PoseMatch返回的StartTime经Selection → TargetData.NetSerialize → ResolveTraversalRequest → PrepareTargets及PlayMontageAndWait原值传递；不按0起播，也不在服务端另选时间。ProjectPlacement的Cursor从PlaybackStart开始，窗内起播只Advance剩余区间；SkewWarp的有/无平移分支判定仍用完整窗口位移，与原生TotalRootMotionWithinWindow一致，不能误改成剩余位移判定。DistanceCurve的接触窗末→落地窗末距离是两锚点间距，不应减去StartTime；它与当前胶囊→落地的剩余位移不是同一量。
- 跳过起播前已结束及预计混出后才开始的窗口，窗内起播只算剩余区间；显式提前混出从末 Section 计算。逐段组合窗间根运动和各窗约束，IgnoreZAxis 沿用入窗高度，未约束旋转沿用动画。窗口可共用 Target，单窗/多窗共用预测与修正后复算；骨点参照和原生尾段按窗口缓存。
- 原生 `bSubtractRemainingRootMotion` 扣除的是 Window.EndTime → **资产全长**，实际后续播放位移才按 Section 路径累计。`ResolveWindowPose.NativeTail` 必须匹配原生扣除，不能统一替换为 SectionEnd。DistanceCurve 按路径分段累计，再分别处理接触/落地窗的原生扣余量参照偏移。
- Bone/Static/None 参照将 Target 换算成胶囊位置。有动画平移时按 BaseTranslationOffset 换算中心，关闭 WarpToFeetLocation 则清除该偏移的 Z；无平移时按脚底换算。Target 与胶囊中心不应直接比较。窗口后根运动累计到预计混出末端。
- 自动补偿的模型支持完整、顺序、不重叠、未限速的原生 Skew 窗口，要求单位 Actor/Mesh 世界缩放、直立胶囊；旋转为 Default/Slerp，时间倍率在(0,1]。非 Warp Modifier 也保留范围，以空 Modifier 标记降级，防止漏算 Scale/自定义效果。超出模型范围时普通 Mantle 保留原目标执行；显式 Landing 仍要求可靠预测。重叠窗口当前不自动补偿，不能把此限制当作动画整体准入条件。

### 高度补偿与显式落地

- 补偿只调整执行 Target.Z，不改检测锚点、XY、旋转，也不在播放中程序化移动胶囊。反推关系为 `TargetZ = SafeCenterZ - CenterOffsetZ`，允许上下修正；支撑层位容差与修正幅度分开。适用 Mantle、显式 Landing，或末平移/末高度窗口绑定 BackFloor 的动作；普通 Vault/Hurdle 仅绑定沿点且 Landing=None 时保留动画跨越轨迹。
- 普通 Mantle 的补偿是可选修正。平面支撑、完整胶囊扫掠、CMC可走性、归属、层位、边缘容差、重叠及全窗口链复算通过才写回；失败恢复/保留原目标，不追加名义终点贴地硬门槛，全 IgnoreZ 同样处理。`CompensatedPlacementCenter` 只缓存所选求值终点上校正成功的安全中心。降级不证明最终站稳；几何、表面、绑定、RequiredTargets 和有限路径校验仍生效。
- 默认 Mantle 限定障碍顶层；仅显式 Landing 或控制 Z/末平移窗绑定 BackFloor 时可用后方支撑。顶层取 `TopSupportHeightRange ± DepthHeightTolerance`，范围来自中心、两侧、首个断层前连续顶面和后沿细化。仅比较组件不足以区分同一车身上的车顶与低层斜面。
- 非顶层支撑范围由胶囊、锚点距离及 CMC/组件可走坡度推导；支撑限锚点载体、障碍集和适用的 BackFloor，安全中心保留 CMC 地面间隙。求值终点及之前最后控制 Z 的窗口提供修正 Target，改后复算截至同一终点的窗口链，防止同名 Target 影响前窗。向下修正保留独立支撑碰撞，向上修正通过复核后才可加入临时忽略。
- 显式 Landing 需 BackFloor 绑定和预计混出前完成的平移窗。补偿、修正后复算、净空/支撑/直线路径与预览胶囊统一在选中Landing窗口末端求值，即使它是最后平移窗也不累计窗后Montage根运动。只选该落地窗及之前最后控制Z的窗口修正Target；之后的高度窗不能反过来补偿先前落点。NativeTail的bSubtractRemainingRootMotion参照仍按引擎资产全长扣除，不能因截断求值而删掉该扣除。较早Landing不让后窗覆盖共享同名Target。CapsuleClearance 只加校验余量；预测、可走落点、落差、完整胶囊、实际根位置支撑及可选直线路径不通过即拒绝。普通补偿的“保留原目标”不豁免此合同。结果按载体局部 Target 复制。

### 执行与三端网络

- LocalPredicted 顺序：校验 → Commit → 获取 Target/碰撞忽略 → Flying → 启动 Montage → 提交 TargetData/发布快照。本地即时播放，不等待权威落点。服务端重检来源、方向/意图、几何、Action、时间/倍率、Warp 合同和可选 AllowedMontages，不接受客户端几何；请求不携带检测位姿/时间戳，不回溯世界。两端数据结构及配置须一致。
- 启动前只发一次取消；Commit 后重入或播放失败共用退出链。激活/请求栈退出前禁止复用同一 GA 实例；处理时保留 TargetData 句柄，防止同步回调消费 GAS 缓存后悬空。远端等待 TargetData 默认2秒，开始后复用计时器，以默认0.05秒检查目标。
- `WhileInputActive` 在 Spec 不活动时持续尝试。已激活尝试的 RetryInterval 默认0.25秒，**只从本地 EndAbility 计时**；自主代理另等匹配 Avatar/Spec/预测键的权威终止，晚通知只释放门、不重计间隔。等待门/间隔先于 GAS 常规准入和几何扫描判断。ASC.AbilityNetworkTermination 透传可靠 ClientEnd/Cancel/ActivateFailed，含本地已结束实例；远端权威退出须发送终止。

| 角色 | 执行合同 |
| --- | --- |
| 权威端 | 独立重检并计算 Target，复制执行快照；生命周期沿 GAS，移动沿 CMC。 |
| 自主代理 | 本地先执行；仅活动且 Avatar/Spec/预测键/Action/Montage/StartTime 匹配时回填权威 Target/障碍。引用未齐继续预测并在活动 Tick 重试；保留本机 PreviousWarpTargets，版本变化后 GA 重建表面复核快照。晚到/结束快照不启动或结束本地会话。 |
| 模拟代理 | 不选片、不重检几何；按 Montage/PlayInstanceId 等待快照引用、Target、碰撞忽略和 Flying。结束先到时保留已播放实例至退出（含混出），未播放终态实例丢弃，晚引用不得复活。恢复 Target 使用权威历史，非 Flying 到达后先恢复碰撞再查支撑。 |

- Component 按独立句柄注册 ASC Montage 前置条件。ReplicatedMontages 非空时，选片/复制归属仅接受其列表及 MontageSettings，仍可复用 ActionSettings；空列表保留目标名推断以兼容装配，多 Warp 系统应配置明确列表。同实例引用补齐不重复拆建，结束快照保留 Montage/PlayInstanceId。
- `CMC.ClientCorrectionFilter` 仅处理 Packed MoveResponse 中仍匹配 SavedMove 的纠偏。GA 只在本方活动且 Flying 时过滤启动前已记录的待确认 Move；保留原生 ACK/缓存释放，不应用该次位置/模式/根运动，超出时间戳重置安全窗口则放行。启动后的纠偏和权威拒绝仍有效，不能据过滤推断落位已被权威消费。
- UE 的 ASC.PrePredictionActivation 和 GameplayAbility.PreActivate 已有激活前 FlushServerMoves 路径；沿用它们，不额外发送或增加确认等待。

### 重放、生命周期与退出

- 原生 SavedMove 保存原始动画根运动，MotionWarpingCharacterAdapter 在 bClientUpdating 时再次处理。Traversal 经 CMC 通用 MovementReplayCapture 保留逐 Move 的 setup/restore 闭包；在原生 OnPreUpdate（发现窗口后、更新 Modifier 前）记录世界 Target、SwapState 和本方碰撞忽略，记录/重放不合并 Move。
- 重放临时安装历史上下文，以原生 Modifier 副本计算并屏蔽重复 Notify gameplay 回调，随后恢复当前会话。副本仅首次重放创建，后续重置状态复用，FreeMove/Clear 释放历史引用；无额外 Tick/保活计时器。历史 Flying/退出边界可重放，不强制接管外部 Custom/物理模式。
- 权威终止/取消/拒绝按 Avatar/Spec/预测键退休对应上下文；组件停用、注销、销毁、ASC解绑或换绑同样使其失效。旧 Move 仍可能进入 PrepMoveFor，失效分支须清除属于该历史 Montage 的 RootMotionParams，不能只提前返回。闭包弱引用组件；不承诺回滚世界碰撞、动态载体或自定义 Modifier 外部副作用。
- 正常完成以 PlayMontageAndWait.OnCompleted（含 BlendOut）为边界。有限动作要求开启 AutoBlendOut 且时间有限，否则 Commit 前拒绝。站稳动作应在站稳后混出；预测落点、Warp 窗口结束或途中接触不提前切 Walking，也不增加独立落地阶段复制。
- 完成、中断、取消、死亡、失效或 ASC解绑均幂等释放 Target、碰撞忽略、Tick依赖、委托和计时器。恢复碰撞后，仅存活且仍 Flying 时按实际支撑切 Walking/Falling；Ragdoll 等接管的新模式不得覆盖，退出无需等待 Montage 自然结束。
- BeginPlay/Activate/重新注册接合 MotionWarping、PawnExt/ASC 和 CMC。Deactivate/OnUnregister/EndPlay 释放接合，期间 IsTraversalAvailable=false，晚快照不重启。拒绝无根运动、非正/非有限 RateScale 和无效检测通道；PawnExt 解绑顺序见 ASC 生命周期。

### 性能与诊断

- 按频率与查询类型计量：检测含分层球扫、顶部/两侧、深度、后沿细化和净空，不是固定几条射线。默认最多6层搜索、5次深度采样、4步细化；一次 Detect 最多尝试12组边缘候选，提前退出/方向回退改变查询数。成功发起通常本地 CanActivate/Activate 各检一次，服务端另检一次，模拟代理不检。
- 按住且无表面时，预筛失败不会激活/EndAbility，故 RetryInterval 不限制这条逐输入帧扫描路径。补偿在已选片的启动准备阶段每次算一次，失败尝试也可能计算；执行中不重算整链，但活动 Tick/OnPreUpdate 刷新目标、定时复核表面、自主代理逐 Move 记录历史，纠偏时才重放。
- 测量须分开探测、选片/骨骼采样、执行、历史记录/重放及 Debug。关闭绘图/日志后量本体；源码计数不等于实测耗时，多客户端同进程 PIE 总成本不等于单角色预算。
- 调试通道、快捷键和录制合同见下节。几何显示开关仅作用于表现；记录与绘图独立，文字仅进Rewind Details。探针球/胶囊显示真实尺寸，点标记大小不再缩放查询体积。记录的 ElapsedMs 包含当次 Debug 开销，只可比较相同采集配置；ExplicitQueries 计项目显式发起的碰撞查询，不包含 CMC.FindFloor 等引擎内部查询。
- 源码诊断用 `Log LogIMGTraversal Verbose`、`Log LogIMGAbilitySystem Verbose`，结束恢复 Log；窗口原生位移用 `a.MotionWarping.Debug 1`。按角色/连接、Spec、激活 PredKey 关联，事件含 Loc/Velocity/MoveMode。LocalRequestPrepared 与 ServerDetectionRejected 对照起点和意图；同键两端各一条不等于重复执行，不同键/键号跳号不能证明输入重复或丢包。

| 事件/标记 | 应如何解释 |
| --- | --- |
| WarpTargetsRejected，Local=1/Auth=0/Started=0/Sent=0 | 本地启动前拒绝；Sent 是 Traversal TargetData，不是 GAS 激活 RPC，不能直接归因延迟/回滚。 |
| PlacementPredictionFallback / PlacementCompensationSkipped | Verbose 正常降级，保留原目标；Plane/CapsuleSupport 提供失败细节，不按 Warning 刷屏。 |
| Overlap / OverlapWith | 同一播放路径内的窗口时间重叠。比较两窗六位小数起止、目标及 Translation/IgnoreZ/Rotation，不代表重复输入。 |
| ServerDetectionRejected / NoSurface | 请求已到服务端，当前几何检测失败；需查两端位置/时序/碰撞。Reliable RPC 也不能证明底层无丢包，须另测重传/延迟。 |
| AuthorityEndedPredictedTraversal | 同键 GAS 权威终止先于本地完成，不是 CMC 重放或预测错误的单独证据。PredictionRejected 才表示激活拒绝。 |
| Safe capsule / PlacementReprojectionRejected | 仅证明模型支撑/复算结果；缺少拒绝日志不证明进入过复算。全链复算只在预测可用且高度修正非零时执行。 |

### 调试工具与记录合同

#### 日常入口与数据边界

- 非Shipping/Test：PIE后直接在Rewind Debugger开始录制，复现后暂停/停止，选角色的Traversal轨道回看。运行时注册Rewind Runtime Extension，开始/停止录制自动切换IMGTraversal Channel；无采集开关，无需GameplayDebugger。显示设置不影响正在进行的录制，回看不重新检测世界或修改游戏对象。

| 入口 | 默认值与行为 |
| --- | --- |
| Traversal轨道 | 父轨道看概览/关键事件；Requests / GA、Detection、Execution、Network / Replay各看本领域Details。隐藏轨道停止回看绘制，不停止采集。 |
| `IMG.Traversal.DebugDraw 0/1` | 默认0，全局几何show/hide，现场与Rewind共用。开关不写入配置，每次进程启动使用默认值；不影响Rewind采集。无等级、阶段掩码和世界长文字。 |
| `IMG.Traversal.DebugGraph 0/1` | 默认0，四框显示实际平面速度、Montage资产归一化位置、最近一次检测耗时/查询次数；第五框横跨两列，对比原始Montage根运动高度与执行胶囊高度。现场取本地Pawn，回看取当前显示轨道；不造Contact/EnableWarping曲线。 |
| Copy frame JSON | 复制当前完整帧，含所有轨道、候选、事件、图元；不受搜索或分页影响。 |
| Export visible range | 停止录制后，将该角色与时间轴可见范围重叠的全部已分析帧导出为JSONL；边界保留完整帧和同时间戳的多次提交。客户端/权威端分别导出。 |
| Last export | 完整路径可选中复制；剪贴板图标复制路径，文件夹图标定位文件。失败保留上次成功路径；文件已移走则提示重新导出。 |

- 调试文字只进Details；常规Warning/Verbose仍用 `LogIMGTraversal` 写Output Log。旧Visual Logger桥接、Traversal GameplayDebugger Category、F6..F10、采集/等级/阶段/角色过滤、残留时长及两个DDCvar.Traversal配置均已移除，无兼容别名。旧Visual Logging录制需重新录制，不能追溯删除或转换。
- 本地分析优先读取 `Saved/Debug/Traversal` 最新JSONL的metadata与全部帧，再按ActorId/Role/NetMode、Spec/PredKey、Attempt、Sequence和Montage实例关联。先核对Dropped、范围是否包含开始和结束、是否只有客户端记录；有最后检测摘要不等于该帧重新检测。单端记录不能替代服务端重检，跨进程录制时间也不能直接当作同一时钟。
- Warp排查先区分目标接触点、Landing窗末胶囊、完整播放路径终点和实际结束胶囊。显式Landing以选中窗口末端为口径，不能因它是最后平移窗而套用PlaybackPath.PlacementTime/动画全长；Landing.Mode=None的普通补偿仍保留名义播放末端模型。完整路径终点含窗后根运动，不能冒充落地点；提前中断也不能反证或证明完整路径XY正确。Landing clearance胶囊另含校验余量。PlacementEvaluation/LandingEvaluation记录StartTime、求值终点、参考/修正Target、窗口时间、IncludeTail和中心；PlacementWindow记录首次投影的实际入窗时间、剩余平移、原生尾段扣除、WarpPoint/RootOffset与轴选项。需新录制，旧记录没有这些参数时不声称已逐项数值复算。
- MontageStopped后的Montage_GetPosition可能为0，不能据此认为动画回到了0。应取最后活动帧与事件时序。`PlayMontageAndWait::OnInterrupted`既可来自Montage混出中断，也可由OnGameplayAbilityCancelled转发；原生CancelAbility先广播取消再EndAbility，本项目回调可能先K2_EndAbility，因此End日志Cancelled=0不能单独排除GAS取消或判定中断来源。只有本端JSONL时不以晚到权威结束快照倒推最初停止原因。

#### 模块、采集与版本

- 运行时根目录为 `ImagineGame/{Public,Private}/Character/Traversal`，编辑器按同层级放在 `ImagineEditor/{Public,Private}/Character/Traversal`，头文件/实现按Public/Private分离，Game不依赖ImagineEditor。

| 源码入口 | 所有权/合同 |
| --- | --- |
| Public IMGTraversalDebugTypes | Frame/Event/Shape/Candidate只读、可反射/序列化，无UObject引用。 |
| Private IMGTraversalDebug | 同步弱引用Scope、Actor/Attempt/帧采集；不取得玩法状态所有权。 |
| Private IMGTraversalDebugTrace | 版本化Trace、FObjectTrace身份与录制时间换算、Runtime Extension。 |
| Private IMGTraversalDebugPresentation | DrawDebugLibrary几何/Graph，拥有本方LineBatch批次。 |
| Editor Trace/IMGTraversalTraceProvider | Analyzer、AnalysisSession内不可变帧与查询。 |
| Editor IMGTraversalRewindDebugger / RewindData | 轨道、时间轴、回看与统一事件分类。 |
| Editor Widgets/SIMGTraversalDetails / IMGTraversalDetailsCustomization / IMGTraversalDetailWidgets | 导出工具栏、原生Details分类、记录展开、只读复制和资产操作。 |
| Editor Trace/IMGTraversalTraceExport | 文本转换、范围快照和文件写入。 |

- Game仅接RewindDebuggerRuntimeInterface、TraceLog及DrawDebugLibrary；Editor接RewindDebuggerInterface、TraceServices/TraceAnalysis、PropertyEditor和Json/JsonUtilities/ApplicationCore。Shipping/Test不采集绘制，仅保留数据类型/无操作接口。
- 当前原生事件 `IMGTraversal/FrameV4`；V3在V2后追加MontageAssetPath/AbilityClassPath，V4再追加bHasHeightSample、HeightCapture、HeightReferenceZ、MontageRootHeight、WarpedCapsuleHeight。Analyzer分别读取V2/V3/V4，旧版本显式置高度不可用，不伪造零曲线。旧录制缺路径仅显示名称，不扫描Content或按同名猜资产；新增路径/高度需新录制。后续序列化变更须升级事件版本。
- Frame/Event时间统一换算为对象World的录制ElapsedTime，执行区间用实际DeltaSeconds；Provider在Session读锁下访问，时间查询用 `Algo/BinarySearch.h::UpperBoundBy`，不将旧帧延续到无样本空档。
- Events包括预筛、实际激活、检测/选片/落点、降级/拒绝、开始/完成/中断、复制等待/回填与Replay，含Phase/Stage、Attempt及已知身份。预筛不复用旧激活预测键，GA在ActiveState建立前显式提供Spec/Pred。Replay保留历史Spec/Pred/Sequence/Montage/Move时间戳；网络等待按原因/实例去重，新录制重置去重。
- 同帧多次Detect各保留Attempt、候选和几何，不让最后一次覆盖预筛；阶段只作分类，详细数据默认采集。当前帧预算：512事件、4096新图元、32候选，另带入最多8个当前执行的准备胶囊；超出累加Dropped。运行时缓存最多64个Actor，淘汰前提交待写帧。
- RecordingStarted可能来自任意线程，只更新Channel与原子代次；Actor采集在游戏线程。停止录制尝试提交末帧，最终边界仍取决于原生Trace连接/帧时序。录制代次清缓存/去重；Actor/Scope弱引用，组件/ASC解绑、World清理、模块退出释放缓存与委托；Provider随Session释放。

#### 原生Details与复制、资产操作

- 使用PropertyEditor的分类、搜索、列宽和记录展开；保留按轨道分工。Detection看最近检测/逐Attempt候选与决策，Execution看Montage/移动/执行候选/补偿，Network / Replay看身份与网络事件。Details不展开Geometry图元列表，不显示普通Search/Intersections/Output阶段的通用Detail绘图文字及Event.Stage实现分类；Warning、决策、失败和具名补偿诊断保留，底层图元/原始事件仍供视口回放及完整导出。向量、Transform、数组逐层展开；浮点最多显示3位小数，Role/NetMode显示名称，Trace默认折叠。事件/候选每页32条记录，Previous/Next访问全部；页码保留并按记录数钳制，搜索只作用于当前页，导出不分页。
- 不整块禁用Details、不改运行时UPROPERTY权限。默认属性隐藏，由标准DetailWidgetRow与只读文本/超链接展示；FStructOnScope只供布局类型，值来自不可变帧。旧固定高度列表/自制双列已移除。字段值可选中Ctrl+C；右键Copy full recorded value不受显示舍入影响，字段名右键还可复制内部名、名称与值、显示文本。记录头Copy为该事件/候选完整JSON，图元通过完整帧导出访问。父轨道Request与Detection共用的Attempt/Source/Direction只在Request显示一次，Detection子轨道仍自带这些上下文；事件与候选自身的同名字段属于独立时间/Attempt，不能按显示去重删除。
- 标量float/double完整值用9/17位有效数字，结构/数组递归保留精度；字符串保留原名/路径。输入控件IsReadOnly，Paste明确禁用。`FDetailWidgetRow::IsCopyPasteBound`要求Copy/Paste均绑定；值控件通过ContextMenuExtender提供完整值复制，避免自身文本菜单遮蔽。过滤后为空的Events/Candidates不创建分类，避免空记录占位。
- `FDetailGroup::HeaderRow()`每次调用都会新建并替换头部，不能当getter反复调用。每组只取一次引用，再设置标题、摘要、复制和Tooltip；事件名放折叠标题，时间/Attempt/Phase放摘要，展开不重复Name字段。
- SHyperlink的下划线覆盖控件实际分配宽度；资产链接置于AutoWidth子槽，避免Details值列的最小/填充宽度将短名称下划线撑长，默认列宽和其它值控件不受影响。
- Montage按完整对象路径打开；GA类路径取ActiveState.AbilitySpec对应Spec.Ability.GetClass，蓝图GA打开ClassGeneratedBy，原生类走FSourceCodeNavigation。资产只在顶部Traversal分类显示一次；同一行提供名称/完整路径复制、名称点击打开、Tooltip完整路径与Content Browser定位，搜索包含名称及路径。Trace不再重复资产路径/打开按钮，只保留采样元数据；原始Frame/JSON仍保留全部路径。/Script类不提供伪资产定位。运行时Actor/Component路径仅供复制。仅点击时TryLoad，刷新不加载资产；缺失/失败提示，不保活录制World对象。
- Height Comparison仅在父轨道/Execution展示本帧的参考世界Z、MontageRootHeight与WarpedCapsuleHeight，可复制完整精度；无有效采样显示状态。Trace的HeightCapture标识独立执行，不用Attempt/Sequence临时变化连接不同起点。JSON的height_basis说明单位、起点及Warp/CMC含义。
- UE 5.8 `String/FormatStringSan.h::TCheckedFormatStringPrivate`以consteval接字面量数组，Printf首参不能三目选择不同长度格式串；精度选择放在调用分支，每个Printf直接传TEXT字面量。

#### 文本导出与精度

- 输出 `Saved/Debug/Traversal/Traversal_<ActorId>_<UTC>_<Guid>.jsonl`：UTF-8，首行版本2元数据（角色/世界、范围、帧数、枚举含义、时间基准），后续每行完整Frame。单帧复制为格式化JSON。只导出已分析快照，不生成未录制内容，也不替代utrace。
- 64位整数用十进制字符串，避免Cycle/ActorId/Frame/Attempt经过double丢精度。结构体强制递归为JSON对象，避免原生math ExportTextItem先转成精度受限字符串；旧版本1文本仍可读。转换与行级复制共用RecordToJson/RecordObject，不改变原生Trace格式。
- Session锁内仅复制不可变帧引用，JSON转换、进度/取消和磁盘I/O均在解锁后。无Session/无数据/写入失败显示原因；唯一文件名且不覆盖，取消/失败清理部分文件，清理失败给出路径。

#### 图元、Graph与排查边界

- 线用像素细线，点标记最多3cm，探针球/胶囊显示真实尺寸。World后端写普通World/Foreground LineBatcher，暂停回放不能依赖游戏Tick回收；本方图元带独立BatchID，每次现场Tick/回看Update先ClearBatch后画当前帧。隐藏、无帧、切世界、清录制、模块退出也清本方批次，不Flush其他系统；现场暂停不清回看批次。新增会产生Mesh的图元须扩展批次归属。
- 准备胶囊：橙色为补偿前；绿色仅在支撑/重叠/全链复算通过后；已计算但被拒绝的修正位置为红色；无有效预测不伪造胶囊。Purpose区分补偿前后和显式Landing净空，同Attempt活动帧持续带入，结束/新准备/解绑后停止沿用。显式Landing位置对应所选窗口末端，Landing=None仍为名义播放末端；位置是准备时世界坐标，不代表动态载体后续预测；模拟代理无本机准备胶囊。补偿为0时前后重合。
- Graph最多240个不同帧的样本，现场与回看均将同一Frame的多次提交合并为最后一个Graph样本，原Trace全部提交不变。回看仅在Graph打开且停止模拟/录制后构建历史；按游标最近5秒采样、剔除窗口外边界帧，不读取游标之后数据，并独立缓存GraphRevision/GraphTime，关闭时释放历史。检测耗时含采集开销，Queries只计项目显式查询，不含CMC.FindFloor等内部查询；后续帧保持最近检测值，不代表每帧重检。Montage进度为资产位置/资产长度，不是Section路径进度。
- 高度框比较同一起点的世界Z增量（cm），橙线MontageRootHeight从本次Selection.StartTime沿PrepareTargets已验证的有限Section路径提取未Warp根运动，以入场Actor/Mesh参照组合；绿线WarpedCapsuleHeight读取同一时刻实际胶囊/Actor高度，包含MotionWarping、碰撞、移动载体及CMC纠偏，不等于单独Target.Z补偿量。原补偿算法只预测所选终点，不据此插值伪造整条“纯高度补偿前后”轨迹。
- 起点/路径只在准备成功且已观测时缓存，实际ExecutionStarted才启用；缓存使用弱Montage引用，随Actor观测释放。录制中途接入且缺起点、模拟代理无本机准备、无效/未起播/已结束或位置不在已验证路径时标不可用。逐帧根运动提取只在录制或Graph开启时进行；仅开几何Draw不提取高度曲线。开启Graph即可在本地采集，无需开始录制；录制开启时即使Graph关闭仍采集。该提取属于Debug开销，不计为纯Traversal检测成本。
- 高度采样必须有实际Montage实例；活动实例已从ActiveMontagesMap移除但Traversal尚在混出时，可使用同资产的原生RootMotionMontageInstance位置，避免Montage_GetPosition的停止后0值伪造回零尖峰。准备成功会退休旧高度起点；仅同帧Prepare→ExecutionStarted接合新起点，未观测的新动作不能继承上一次同名Montage的参考位置。
- 高度框只画最近一次有效HeightCapture，跨执行、缺失样本和采样空档断开；原始与执行曲线共用纵轴，可显示负值，单点画点，无数据只显示状态。不从旧录制或资产名反推缺失样本；History与回看GraphFrames同时保留高度/身份字段。
- 全部Graph曲线共用PlotCurve的采样空档/单点处理，横轴固定为游标前[-5,0]秒；Rewind显式传ScrubTime，空档保留历史并标注History only，当前指标显示n/a，历史高度值标(last)，不把旧值延伸为当前帧。Graph角色优先级为选中的父/子轨道→当前根对象→其它可见调试对象，较低优先级不能覆盖已选角色。无当前Frame不重画旧几何；换会话、隐藏及退出仍清理本方批次。
- Graph使用DrawDebugLibrary的空间World Drawer：运行时在OnWorldPostActorTick采样后直接绘制，Rewind在Extension.Update选定显示轨道后绘制；不再注册Canvas/DebugDrawService回调，也不使用CanvasString。运行时取本地PlayerCameraManager视点，回看优先取同World的当前LevelEditor视口，回退同World本地玩家镜头。图框位于视点前方，按FOV/宽高比布置，旋转后的Y/Z分别对应横纵轴，bDepthTest=false；轴、曲线、图例和字形线全部经WithOwnedBatch归入同一Traversal批次，隐藏/更新/清会话沿既有Clear回收，不另建文字残留或录制通道。一个样本即可显示轴与当前值，无样本显示等待提示；DebugDraw=0仍可独立显示Graph。
- 用户提供的DebugDraw_MultiLineGraph蓝图导出使用MakeVisualLoggerDebugDrawer、旋转后Offset、DrawDebugGraphAxes/Line/Legend，属于空间图形；本工程采用其位置/旋转、轴/曲线/图例和关闭深度测试的用法，但用World Drawer直接绘制，不恢复旧VisualLogger文字通道。原Canvas后端也是库支持的接口；本轮已改用空间路径，旧后端未实际显示的具体运行时断点不能据源码差异断言。
- Traversal业务CPP只通过IMGTraversalDebug提交观测，空间绘制集中于DebugPresentation；不保留直接DrawDebugHelpers、UE_VLOG、屏幕文字、GameplayDebugger入口或等级判断。执行Target不再逐帧生成重复文字事件，目标位置/朝向留在图元中；AddedIgnores数量仅在ExecutionAcquired/Releasing记录。Live图元去重与准备胶囊缓存有各自用途，保留当前帧去重和生命周期清理。旧Frame序列化字段即使当前表现不消费也不能随清理删除，须遵守版本合同。
- RewindDebuggerTrack.Update只更新当前节点，父轨道必须显式Child.Update，参照GameplayInsights/Private/MontagesTrack.cpp。子轨道Name稳定且独立；时间轴与Details共用事件分类，时间轴省略Detail/ExecutionState/ReplayCaptured/Restored/PlaybackSection/Path及PlacementWindow/PlacementEvaluation/LandingEvaluation等明细标记，子轨道Details仍保留。Session/Provider缺失时递归清时间轴/选帧/Details。
- 参考入口：PoseSearch的RecordingStarted/Stopped、Trace Module/TrackCreator/Details；GameplayInsights/Private/SMontageView.cpp的资产链接。Visual Logging文本会被引擎画回视口，不再作本系统主通道。GTA的ClimbDebug、PedDebugVisualiser、DebugDrawStore负责空间绘制，debug/Debug.cpp::RenderProfiler独立调用GetRageProfileRenderer().Draw绘制EKG；仅借鉴职责，不依赖RAGE。
- 验证边界：已核对源码接口和单向依赖；三端行为、远程会话、录制开销与全部UI交互仍须运行验收。具体录制只证明相应角色/时间段，编译或静态检查不替代表现验证。不主动调查或修改Content。

### GTA 对照与已知边界

- GTA 路径相对 `src/dev_ng/game`：检测/手点 `task/Movement/Climbing/{ClimbDetector,ClimbHandHold}.*`，发起 `task/Default/TaskPlayer.cpp`，Jump/Vault 分流 `task/Movement/Jumping/TaskJump.*`，执行/清理 `task/Movement/Climbing/TaskVault.*`。GTA Clone 同步不等同 UE 的 CMC Replay。
- CTaskVault 按手点、DropPoint、GroundToRootOffset 和动画相位求目标位移，`CalcTotalAnimatedDisplacement`、`SetDesiredVelocityAdjustmentRange` 分别处理动画位移/调整区间。`ClimbDetector::ValidateHandHoldAndDetermineVolume` 调 `CTaskVault::IsHandholdSupported` 校验出口，不能把几何通过等同动画可落位；IMG 分层落实为选片后绑定、可选补偿和显式 Landing 合同。
- `ms_VaultExitConditions` 的 eVEC_Slide 属于独立滑行动作且禁自动 Vault，IMG 推荐表只保留对应普通 Vault 的六条。27.5 cm深度球、55 cm步距、20 cm断层等保持既有映射，不以调阈值替代执行语义。GTA 多层搜索/坡面后筛、MBR发起范围、JumpVault步态门槛及 AutoVault摇杆/受阻/冷却可作对照；纯自动落地要求与 IMG 默认允许空中属于策略差异。
- 当前预测假设资产默认有限 Section 链正常推进。运行时改链、混合权重、限速/轴耦合和 CMC 碰撞截断会改变实际落位；抓沿下移受阻后，后段会从实际高度继续根运动，预计算不能修复此执行偏差。结束时必须查实际支撑，不补做运行时轨迹或第二次 Warp。
- 局部 Target 跟随不等于完整动态平台支持。BasedMovement/动态 Bone、世界回滚、异步检测、持续悬挂/Shimmy/梯子另行设计；沿用 UE/GAS/CMC，不移植 RAGE 的附着、Capsule、手动位移或网络协议。
- **待运行验收：** 各 Action、窗内起播/跨 Section、上下补偿、失败/中断、专服/Listen Server 的自主与模拟代理、引用晚到及延迟/丢包纠偏。源码合同、静态检查及三客户端 PIE 总帧数均不能替代资产表现与联机验收。

## 镜头

### 调用链与职责

- Hero.DetermineCameraMode 选择能力覆盖或 PawnData.DefaultCameraMode，CameraComponent 回退 Default；CameraModeStack 更新/混合，CameraComponent 输出视图。TPS 顺序为未滞后枢轴 → 位置 Lag → 姿态与骨盆高度补偿 → 阻尼朝向的轨道偏移 → Shake → 防穿墙。旋转误差使用本模式 FOV、UE 投影及分屏尺寸，不按栈混合或后续特效重算；无本地目标 Controller/视口时按相机 AspectRatio 回退。
- `Camera/IMGCameraDamping.h/.cpp` 为项目相机阻尼库，合并标量/FVector 弹簧核、`FIMGCameraLocationDampingState` 的位置 Lag 与既有 `FIMGCameraRotationDamping*` 类型；保留反射类型名及 CameraMode 属性名/默认值。`IMGCameraShake.h/.cpp` 独立提供振荡/包络。各 CameraMode 按需持有本方数值状态；位置、旋转、骨盆及不同 Mode 不共享历史。依赖只从 Mode 指向算法库，数值层不查 World、角色、动画、Controller、GAS 或视口，不自行 Tick/复制。新增效果的可复用运算/数值状态扩展相机算法层，采样、玩法选择、重置时机与表现编排归 Mode；不向 ABP、CMC 或 Mesh 写回。
- View.ControlRotation 保留原始期望朝向，阻尼/Shake 只改表现。武器和 AimAssistInputModifier 仍取最终玩家视点，因而受镜头表现影响。

### 配置与蓝图合同

- 位置 Lag 默认启用，使用 GTA 风格弹簧，XYZ 共用强度/阻尼。保留蓝图属性 `CameraLocationLagSpeed`（默认10），作为 sqrt(k) 的响应强度，实际 k=Speed²；不是 Hz，也不保证与旧 VInterpTo 相同的跟随曲线。新增 `CameraLagDampingRatio` 默认1、范围0至10，欠阻尼允许超调；Speed=0直达目标。`CameraLagMaxDistance` 仍以世界 cm 限制枢轴误差，0不限制，触及硬限距时清速度。
- Lag 数值状态/子步/限距封装在 `FIMGCameraLocationDampingState`，TPS 仅传入采样、现有配置与本帧直达请求。子步保留 `bUseCameraLagSubstepping` / `CameraLagMaxTimeStep`（默认开启、1/60秒、运行时最小1/200秒），从上一帧真实期望枢轴插值到本帧期望枢轴，均分时间推进弹簧，不从滞后输出构造目标轨迹。停用、重激活、CameraCut、目标/控制器变化清理位置/速度及目标采样历史；关闭、非法配置/时间或超出子步预算直达有效目标并清速度，无效目标清历史并返回上次有效滞后位置（未初始化时为零）。
- Rotation Damping、Shake 均默认关闭。旋转按 Yaw/Pitch 两轴执行目标角速度平滑 → 积分推进朝向 → 限制角度误差 → 弹簧收拢，继续使用 UE `FMath::SpringDamper` 两级公式；频率为 Hz，Roll 透传，MaxErrorFraction 限制相对各轴半 FOV 的误差，并在角度弹簧后再次限幅。保留最短角度路径、轴旁路和既有重置合同。
- 旋转优先沿现有结构调参，不因位置 Lag 的表现改善或同属算法库而统一替换弹簧核。UE 的临界/过阻尼分支保留完整速度状态，GTA 快照保留慢指数模式，两者启停/拖尾响应不同；k=(2πf)² 只对应单位质量下的频率/系数关系，不能保证参数或手感兼容。替换数值核须有明确响应需求，不能据结构相似认定表现一致。
- TPS 的 `Third Person|Pelvis Offset` 默认关闭，原生曲线名默认 `LegIK_PelvisDeltaZ`、骨骼 `pelvis`、SpringConstant=10、DampingRatio=1；资产输出及派生相机配置须同名，修改原生默认值不会重命名资产曲线。`UpdatePelvisOffset` 从目标 ACharacter 主 Mesh 的 `GetCurveValue(Name,0,Value)` 读取已求值的组件曲线（含 PostProcess/URO 混合），只消费世界 cm 的已加权高度差，不读取骨骼绝对高度、不再乘 IK 权重；骨骼名用于校验对应骨架。曲线缺失以0为目标渐退；关闭、无效来源/配置/数值立即清理，切换目标、Mesh/骨架、主 AnimInstance、曲线/骨骼名及 CameraCut 后首样本直达目标。相机只读已有姿势，不强制动画求值，不新增 Tick/复制或移动依赖。
- Lag/骨盆平滑共用 `IMGCameraDamping::AdvanceSpring`，按 GTA `camera/helpers/DampedSpring.cpp` 的单位质量 k/阻尼比公式：欠阻尼携带速度；临界/过阻尼沿用该快照只保留慢指数模式的近似，阻尼比1时响应约为 exp(-sqrt(k)·dt)，不是 UE 的 Hz 参数或完整临界阻尼解。k=0直达目标；已初始化时零步长不推进。骨盆来源校验/曲线采样与缓存留在 TPS，补偿同号加在位置 Lag 之后，并进入现有防穿墙；`showdebug camera` 显示 Lag 误差/速度/强度/阻尼及骨盆曲线/骨骼名与当前 cm 偏移。
- Shake 各轴振幅默认0；位置 X/Y/Z 为未摇晃相机的前/右/上，旋转以局部四元数叠加，FOV 偏移后限于5至170度。GetShakeInput(TargetActor, DeltaTime) 在每个启用模式的视图更新中调用一次，原生输出 Active=false、AmplitudeScale/FrequencyScale=1；蓝图按步态/曲线返回启停及非负倍率，C++ 不固定奔跑或 Tag 判断。Active=false 用最后一次激活倍率混出，关闭 Enable Shake 立即清空；持续驱动使用 HoldTime=-1。
- 通用 Shake 支持 Sine/DampedSine、相位/随机种子/DC Offset，以及预延迟→Attack→Decay→Hold→Release 包络；曲线为 Linear/SmoothStep/Exponential，默认 Release 为 exp(-7t)。Start 幂等，释放期间重启保留相位并从当前包络恢复；有限 Hold 可单次 Start 后自然结束。Phase/RandomSeed 在新实例 Start 时消费；其他 Shake 的触发和实例调度归调用方。

### 重置、调试与参考

- CameraComponent.ResetInterpolation/NotifyCameraCut 向所有在栈模式转发，下次完整视图更新消费，回调中的新请求留给下一次。组件注销时停用栈、清单帧 FOV 偏移，重注册重新激活；栈停用期间回退原生视图，不输出空模式结果。TPS 激活、目标/控制器变化或可观察到的 CameraCut 重置历史，重置帧强制重查碰撞探针；停用/关闭释放本方状态，没有通知的瞬移须显式重置。
- 零步长不推进弹簧/振荡或求导，但仍应用轴旁路及 FOV/偏移上限；零时长包络阶段可立即完成。超过32/120秒：旋转清历史并直达目标，Shake 清空并输出零；无效输入/配置分别回退原朝向或零 Shake。位置 Lag 开启子步时最多32步，超出预算直达目标并清速度；零步长保留弹簧状态但刷新目标采样并应用限距。
- `showdebug camera` 查看 Shake 包络/倍率/阶段。
- GTA 路径相对 `camera/`：`gameplay/follow/FollowPedCamera.cpp::UpdateRunningShake` 对照运动驱动；`helpers/{FrameShaker,Oscillator,Envelope}` 对照数值层；`base/BaseCamera.cpp` 对照 PostEffectFrame；`gameplay/GameplayDirector.cpp::RegisterExplosion` 复用同类 Shaker。`gameplay/aim/FirstPersonShooterCamera.cpp::UpdateFrameShakers` 的 FPS 头骨跟随尚未迁入。
- 旋转参考 `helpers/LookAtDampingHelper.cpp::Update`：角速度/角度两级弹簧与当前结构相近，已确认有 Cinematic Mounted/Tracking 调用，不能视为普通第三人称镜头统一采用的算法。`gameplay/follow/FollowCamera.cpp::ComputeOrbitOrientation` 的跟随及 PullAround 另有速度、输入与配置分支；GTA 镜头表现须沿实际消费者追踪，不能仅凭 `DampedSpring::UpdateAngular` 判定整条旋转链。

### FootPlacement ControlRig 与镜头曲线

- 资产：`Content/Characters/Heroes/Mannequin/Rigs/CR_Biped_FootPlacement.uasset`，对象路径 `/Game/Characters/Heroes/Mannequin/Rigs/CR_Biped_FootPlacement.CR_Biped_FootPlacement`。用户维护既有 Graph 对齐和注释框；编辑前读取实时图与坐标，不用旧 T3D 覆盖当前资产。`D:/CR_Biped_FootPlacement.T3D` 仅为调查时的导出快照。
- 主图为 EditorAsset 的 `RigVMModel`（编辑器显示 Rig Graph），同时包含 Construction 与 Forwards Solve。Forwards Solve 原入口为 `RigUnit_BeginExecution` → EarlyOut Branch；主 Sequence 按顺序执行 Reset/动画目标缓存、`UpdateRootDamper`、地面查询/平滑、植脚、腿/骨盆控制、腿 IK/Post Solve，再缓存世界变换和调试绘制。
- 骨盆主链：`Update Leg Controls` 左/右 → `Update Pelvis Control` → `RigUnit_SetRelativeTransformForItem_1`（Child=pelvis，Parent=pelvis_ctrl）→ `Solve Leg IK` 左/右 → `Update Foot Post Solve` 左/右。`Update Pelvis Control` 的 `Pelvis Offset` 已经腿长约束和平滑，按世界方向保存再逆旋转用于落位；它不等于包含 RootDamper 的完整骨盆高度差。
- 镜头输出接在主图，不修改上述函数内部。`CameraPelvisCaptureComment` 框在 Forwards Solve/EarlyOut 前，以 Current pelvis Global Transform → Translation → To World(Location) 保存 `CameraPelvisBeforeWS`，再清零曲线。`CameraPelvisOutputComment` 框接在右脚 `Update Foot Post Solve_1` 后，读取同次求解后的 pelvis 世界位置，以 After−Before 的 Z 写 `FootIK_PelvisDeltaZ`；包含 RootDamper 和骨盆修正，单位 UE cm、上正下负。Rig Global 不是 World；To World 的位置版本包含组件变换与缩放。
- 曲线通过 Rig Hierarchy 中的 Curve 和 `Set Curve Value` 输出，不从 Mesh Socket 读取上一完成姿势来充当求解前采样。Rig 内不额外乘 ABP 的 ControlRig Alpha；外部 Pose/曲线混合仍须核对。ABP 同名曲线应以上游 `Modify Curve`（Blend、Alpha=1、Value=0）提供零基线，避免旁路继承旧值；TPS 已提供曲线消费者，但 Rig 输出本身不证明 ABP 基线、动画 Tick/曲线过滤和当前派生相机配置均已接通。
- `FootIK_PelvisDeltaZ` 仅供相机消费，不驱动腿部 IK、骨盆或 ControlRig 权重；曲线值不能单独证明最终姿势采用了完整 Rig 输出或可见脚底已贴地。

### FootPlacement 地面查询、Contact 与 Montage 合同

- `ABP_Mannequin_Base` 主姿势顺序为 `DefaultSlot` → `Inertialization` → `OffsetRootBone` → `Procedural_Feet` → `Procedural_Hand` → `PoseHistory` → 输出。`Procedural_Feet` 的 ControlRig 节点引用本资产并传入姿势/曲线，输出再经 `BlendListByEnum(E_MovementModeForAnim)`；其输入包含 `ControlRigIK`、`BasicIK` 和 `PreProceduralPose`。Rig 执行与最终输出权重分开，不从枚举名推断具体旁路规则。
- `Procedural_Feet` 将 `AllowFootPinning` 返回值接到 `DoRaycast`，并非 `Enable Foot Pinning`；`AllowSlopeWarping` 同时驱动 `Enable Slope Warping`、`WorldZDamperEnabled`，两个查询均按 MovementMode 选择。Rig 持续执行不等于每帧执行查询：主图先查 `DoRaycast`，再查 `Enable Slope Warping`；两者为真才依次执行左右 `Do Scene Query`。DoRaycast 为假不更新原始 floor；前者为真、后者为假则将左右 collision 恢复 Initial Transform。之后独立执行左右 `Update Floor Control`。
- 动画目标链：主 Sequence.C 在 RootDamper 前用 `CalculateTargetAnimatedToeTransformGlobal` 缓存左右 `animated_toe_target_*_null` 和 `animated_pelvis_null`，D 更新 RootDamper，E 查询地面，F 平滑 floor。`UseIKBoneTargets` 默认 true：Toe 相对 Foot × 外部 IK Foot 相对 Foot × Foot Global，等效将 Toe 的局部偏移应用到 `ik_foot_l/r`；false 时直接取当前 `ball_l/r` Global。须区分动画 IK 骨骼与变形脚骨，不能只凭其中一套骨骼推断另一套的目标。
- `Do Scene Query` 先缓存 Animated Foot Target Current Global。第一段球扫从 Rig 空间 `(0,0,HeightOffset)` 横向扫至 `(Target.X,Target.Y,HeightOffset)`，检测脚前墙面；HeightOffset 当前局部默认50 cm。命中且 `Adjust Feet Targets For Collision`（默认 true）时只修正动画目标 XY，保留动画 Z。第二段从第一段命中回退位置或横扫终点向下扫 `2 × HeightOffset`，默认约 `Z=+50 → -50`，半径5 cm；检测高度未使用动画脚目标 Z，不能视为覆盖完整攀爬/落差范围。坐标为 Rig Global，经引擎查询节点转换到 World，不能把这些值当世界绝对高度。
- 第二段只在 bHit=true 时写 `floor_l_collision` / `floor_r_collision` 的位置与旋转；未命中分支没有清零、失效标记或重建 floor，保留上次值。高度悬空须区分未命中留下旧值、平滑和实际姿势权重，不能用 Contact 曲线解释所有位置问题。当前节点为 `Sphere Trace By Trace Channel`，TraceTypeQuery1；对应引擎 `Units/Collision/RigUnit_WorldCollision.cpp` 按执行次数和输入缓存，同次 Solve 可复用，跨 Solve 重新检查；不能仅因 Start/End 不变就认定射线跨帧不刷新。节点忽略 OwningActor（或缺少 Actor 时忽略 OwningComponent）。
- **Contact 控制地面法线混合，不控制地面命中位置或查询启停**：主图 `GetCurveValue_2/3` 读取 `contact_l/r` 传入各自 `Do Scene Query.Contact`；函数将传入的世界 `Ground Normal` 转到 Rig 空间，再 `Interpolate(A=GroundNormal,B=HitNormal,T=Contact)`，由 Z 轴到该结果的旋转驱动 collision。Contact=0 使用传入法线，=1 使用该脚命中法线；即使射线已命中斜坡，Contact=0 仍可能保持与坡面不同的朝向。最终还受 SlopeWarpingAlpha、坡度限制和平滑影响。
- `contact_l/r` 按左右脚实际接触窗口配置：离地趋0，接触时平滑趋1；最终效果以 Montage 混合后的求值曲线为准，不把单个源动画或旧快照中的关键帧状态当作当前运行值。
- `DisableLegIK` 与 Contact 分责：ABP ControlRig Alpha 选 Curve，暴露引脚名为 `DisableLegIK`，`AlphaScaleBiasClamp` 的 Scale=-1、Bias=1，禁用曲线为1会抑制整体 Rig 修正；Contact 仅控制对应机制，不能代替整体权重。
- `Update Floor Control` 消费原始 collision：先按 `GroundMovementWorldDelta` 更新世界历史，平滑 `SlopeWarpingAlpha` 向启用状态的0/1，再在世界空间平滑位置/旋转、转回 Rig 空间并混合到零平移/单位旋转，写 `smoothed_floor_*_ctrl` 并保存左右世界历史。`Floor Smoothing Time` 当前默认0.05秒；不是 Montage 结束事件触发更新。`DebugDrawGroundCollision` 两个深绿色长方框画的是这些 smoothed 控制器，不能作为原始射线是否命中、是否更新的直接证据。
- Hierarchy：`floor_*_collision` 为无父级 Control；`animated_toe_target_*_null` 为无父级 Null，其 ctrl 是子级；`smoothed_floor_*_null` 挂在 root 下，smoothed ctrl 挂在对应 null 下；`ik_foot_l/r` 挂在 `ik_foot_root` 下，后者挂在 root 下。RootDamper 修改 root，动画目标缓存与 floor/腿部求解需按各自时序和空间区分；静态图和资产默认值不替代运行时 bHit、Contact、GroundNormal 或最终 Alpha。

### 动画曲线 Modifier 写入边界

- 工程自定义的五个 Animation Modifier（`AddAnimationCurves`、`RemoveAnimationCurves`、`RemoveAnimationNotifiers`、`FootContactCurves`、`Footstep`）统一为一次性，均继承 `UIMGOneShotAnimationModifier`。正常 Apply 后移除源实例及应用记录，保留生成/删除结果；重新生成须重新添加。FootContact/Footstep 的参数或写入失败会提前返回，不安排自动移除。基类 Revert 不修改动画数据，不能用它恢复一次性写入前的内容。
- 已核对源码：`ImagineEditor/AnimationModifiers/IMGAnimationModifier_FootContactCurves` 的 Apply 仅经 `Footstep::ReplaceFloatCurve` 重建配置的左右 Float Curve，默认 `contact_l/r`。该类不直接写 `DisableLegIK`，但名称可编辑，实际写入边界以实例配置为准。`IMGAnimationModifier_FootstepUtils.cpp` 的删除最终按名称/类型调用 Controller，不清空其他曲线。
- UE 5.8 的批量 `Add Animation Modifier(s)` 窗口并非仅执行新添加项：`Editor/AnimationModifiers/Private/SAnimationModifierContentBrowserWindow.cpp::OnApply` 将新实例加入后，遍历动画已有的全部 Modifier 并 Apply。单个 Modifier 重复 Apply 还会先执行其上次应用实例的 Revert，见 `AnimationModifier.cpp::ApplyToAnimationSequence`。只重算 Contact 时，已存在实例应从动画的 Animation Modifiers 页选中该项单独 Apply，避免批量添加窗口或 Apply All 重跑其他项。
- `UIMGAnimationModifier_AddAnimationCurves` 的 `bOverrideExistingCurves` 默认 true，匹配名称会删除整条旧 Float Curve，再写时间0的默认值；false 时保留已有同名曲线。旧 `IMGAnimationModifier_ApplyAnimationCurves` 由 `Config/DefaultEngine.ini` 的 ClassRedirect 映射到该类；遗留实例是否仍挂在资产上须实际回读，不能只凭资产字符串判定。
- 一次性清理在 Core Ticker 延后执行，等待 `FApplyModifiersScope` 与编辑器当前事务结束，避免修改引擎正在遍历的 Modifier 数组或在模态 Apply 对话框中提前清理。每个回调保存实际源实例的强引用、动画与本次应用副本的弱引用，不用模板复制时可能重复的 GUID 合并跨资产清理；回滚销毁/后续 Apply 替换应用副本或动画失效时跳过。清理范围内建立 `FApplyModifiersScope`，避免清应用记录时 PostEditChange 再触发其他 Modifier；回调完成后释放引用。`InstanceId` 只在目标动画/骨架的实例列表中匹配引擎应用副本与源实例。
- 排查线索与待验证项：若批量添加 Contact 后其他曲线被重置，先核对动画已有 Modifier、Add 曲线项的 Override/DefaultValue 及 Contact 的输出名称。资产二进制出现旧 Add/Apply 类名只证明存在序列化名称线索，不证明当前活动实例或曲线原值；具体受影响资产须回读实际 Modifier 配置和原始关键帧，不能据此自动恢复为猜测值。

### FootPlacement 骨盆目标、约束与重置

- `Update Pelvis Control` 本身不查询 Montage、Contact、移动模式、SlopeWarpingAlpha 或独立 Pelvis Enable/Alpha；它在左右 `Update Leg Controls` 后直接执行。整体 IK 有效仍不证明上游 `DoRaycast` / `Enable Slope Warping` 有效；Contact 经地面法线和可选植脚间接影响腿/骨盆，不是骨盆专用启用曲线，缺少 Contact 不能单独解释全部骨盆高度问题。
- 腿目标链：`Update Leg Controls` 从 `smoothed_floor_*_ctrl` 取平面点与 Z 轴法线，先混合动画 Toe/Pin 目标，再经 `AdjustPositionToPlane` 写 `toe_*_ctrl`，随后由 Toe 到 Foot 的动画关系、AllowHeelLift 和坡度混合求 `foot_*_ctrl`。`AdjustPositionToPlane` 沿 `(0,0,-1)` 与调整后的地面平面求交，再加回 `Animated Height Offset` 的 Z；它消费 `animated_slope_angle_ctrl`（来源为 ik_foot_root 旋转）扣除动画已表达的坡度。骨盆消费这些修正后的 Toe/Foot 控制器，不直接读取原始 bHit。
- 左右骨盆候选分别为 `AnimatedPelvisPosition - AnimatedToeTargetPosition + ToeControlPosition`；XY 取两候选平均，**Z 取两候选 Maximum**，不是平均/Minimum，也不按 Contact 筛选支撑脚。左右一侧要求下降、另一侧保持原高时，初始 Z 保留较高一侧；是否再下降由后续腿长约束决定，不能据此直接判定骨盆函数没执行，也不能未核对支撑策略就将 Maximum 改 Minimum。
- `Clamp Pelvis Offset` 先处理左腿，再处理右腿，保持当前 Pelvis 到 UpperLeg 的相对偏移，把候选 UpperLeg 到修正后 Foot Control 的向量限制在最大长度内。最大长度取 `Max(预计算 LegLength × Hyper Extension Limit Factor, 当前动画 Foot 相对 UpperLeg 的距离)`；因当前动画已伸直而保留较大长度属现有保护策略。Hyper Extension Limit Factor 默认0.98，不能认定始终以0.98倍腿长强制放松。动画腿部弯曲/伸展变化会改变约束结果，不要求 Montage 结束事件。
- 两腿约束后减去当前 pelvis Global Position，按组件世界旋转转换成世界方向偏移，`Damp(Vector)` 使用 `Pelvis Offset` 历史和 `Pelvis Smoothing Time`（默认0.3秒）平滑；再逆旋转回 Rig 空间，加到当前 pelvis 位置写 `pelvis_null`，旋转沿用当前 pelvis。主图将 pelvis 以单位相对变换贴到 `pelvis_ctrl`（Weight=1、传播子级），随后解腿 IK。该平滑每次有效求解执行，不是等满0.3秒或等 Montage 结束才开始。
- `Has Teleported || PendingReset || ForceReset` 在当次目标缓存之前执行 `Reset`，清 `Pelvis Offset`、植脚状态/权重、RootDamper 速度，重建 floor/世界历史并清 PendingReset/ForceReset；该帧后续仍走正常求解。反复外部置位可能反复清掉平滑历史，须与纯粹目标不变区分。EarlyOut 要求 FootPinning、SlopeWarping、WorldZDamper 都关闭且对应 Alpha/PinWeight 接近0，并且不用外部 IK 目标或两脚 IK/动画位置已一致；EarlyOut 设置 PendingReset，恢复求解时重置，不是 Montage 状态门控。
- 后续 `Solve Leg IK` 求解 thigh/calf/foot，`Update Foot Post Solve` 修正脚部，不再写 pelvis；Post Solve 的最小 Toe 高度来自动画相关高度，不是无条件把可见鞋底夹到地面。
- RootDamper 独立于 `Pelvis Offset`：它在骨盆目标缓存后修改 root，消费 WorldZDamperEnabled、世界位移、GroundNormal 与自身历史；RootDamperSmoothingTime 默认0.15秒。动画输入姿势、原始 collision、smoothed floor、腿部目标、骨盆约束和平滑、最终输出混合分别核对，不能将其中一项代替完整姿势结果。

### UE 5.8 ControlRig 编辑工具与接口限制

- 本地 MCP 服务：`ModelContextProtocol.StartServer 8000`，端点 `http://127.0.0.1:8000/mcp`；`ModelContextProtocol.RefreshTools` 刷新目录。`EditorToolset` 提供资产/属性工具，`AnimationAssistantToolset` 提供 CR 工具。先 `list_toolsets`/`describe_toolset` 核对实际加载；`call_tool` 的 tool_name 用短名，toolset_name 单独传。
- 当前资产是 `ControlRigRuntimeAsset`；内置 `animation_toolset.toolsets.controlrig.ControlRigTools` 的参数仍要求旧 `ControlRigBlueprint`，会拒绝该资产。不要强转或改资产类型。UE Python 正确入口：`rig=unreal.load_asset(path)` → `ed=rig.get_editor_asset()`（ControlRigEditorAsset）→ `g=ed.get_default_model()` → `c=ed.get_controller(g)`；HierarchyController 用 `ed.get_hierarchy_controller()`。
- **结构类型变量的 AddMemberVariable 必须传完整类型路径**：`ed.add_member_variable(name, '/Script/CoreUObject.Vector', False, False, '(X=0,Y=0,Z=0)')`。此接口将字符串按 CPPTypePath 解析，裸 `FVector` 会使 CPPTypeObject 为空并触发 RigVMExternalVariable 断言，不能靠 Python try/except 恢复。反之，`c.add_variable_node` 分别接收 `FVector` 和 `unreal.load_object(None, '/Script/CoreUObject.Vector')`；两种签名不能混用。修改前核对本地源码、方法 docstring 和 `ed.generate_python_commands()`。
- 实际引脚名以 `node.get_pins()` 为准：当前 `RigUnit_BeginExecution`、`RigUnit_SetCurveValue` 等 RigUnit 使用 `ExecutePin`；Variable Setter 和函数引用使用 `ExecuteContext`。连线用 `c.add_link(source_path,target_path)`，对返回值做检查；浮点类型、Getter/Setter 和子引脚也先回读，不能从节点标题推测接口。
- 编辑前检查 dirty 状态，并备份磁盘资产、实时 `c.export_nodes_to_text(...)`、节点坐标/连线和 `ed.generate_python_commands()`。分步创建变量/Hierarchy Curve、创建节点、设置默认值、接线、`ed.recompile_vm()`；使用事务/UndoBracket，批量操作期间暂停自动 VM 重编译并在 finally 恢复。回读检查只移除预期旧连线、保留既有节点/用户修改；仅保存目标资产。
- 需要通用 Python API 而 MCP 工具签名不兼容时，可使用引擎自带 `PythonScriptPlugin/Content/Python/remote_execution.py` 的本机入口；先核对绑定为 `127.0.0.1`、匹配 `project_root/project_name`，记录并恢复 Remote Execution 原开关。该入口用于编辑资产，不修改 C++ 或工具插件源码。独立验证可用 `ed.create_rig_vm_host()`，执行 Construction/Forwards Solve，避免直接改运行角色或资产默认配置。
- 引擎目录以「工程方向与参考源码」为准。引擎 Python 为 `Engine/Binaries/ThirdParty/Python3/Win64/python.exe`，系统 `python.exe` 可能只是不可用的 WindowsApps 别名。本机接口发现不到节点时区分沙箱网络可见性与编辑器设置，不能据此反复改配置；Remote Execution 属于安全相关设置，调查资产本身不自动授权开启，须遵守审批并在授权使用后恢复原值。
- 只读 Rig 图可用 `ed.get_all_models()` 覆盖主图、函数库及聚合子图；节点取 `get_all_pins_recursively()`，引脚取 `get_pin_path/get_cpp_type/get_default_value/get_linked_source_pins/get_linked_target_pins`，坐标用 `node.get_position()`，默认配置用 `ed.get_asset_variables()`。调查快照位于 `Saved/Codex/FootPlacement/live_rig.json`，仅作导航/证据；修改前仍按现有要求回读实时目标图，不用快照覆盖资产。
- 已有角色 Rig 的只读查询：`unreal.ControlRig.find_control_rigs(actor, unreal.ControlRig)`，再用 `rig.get_hierarchy()`、`get_variable_as_string(name)`；Outer=None 会返回空，不表示没有实例。`RigElementKey` 构造使用 `type=unreal.RigElementType.CONTROL, name='...'` 等关键字，不能按 name/type 的位置顺序传参。动画只读接口为 `unreal.AnimationLibrary.get_float_keys(seq,name)` 和 `get_bone_pose_for_time(seq,bone,time,extract_root_motion)`，当前不是 `AnimationBlueprintLibrary`；前者返回两组 UE Array，须转 list 才能看到值。没有运行实例时不将资产默认值当作运行时数据，不为调查自行启动 PIE 或构造验证运行。
- 需要明确骨骼采样空间时，可用 `unreal.AnimPoseExtensions.get_anim_pose_at_time(seq,time,unreal.AnimPoseEvaluationOptions(...))` 取得 AnimPose，再 `get_bone_pose(pose,bone,space=...)`；后者默认 `AnimPoseSpaces.LOCAL`，须区分局部、组件与去 Root Motion 后的高度，不能直接把局部 Z 当脚底离地高度。`Saved/Codex/FootPlacement/live_pelvis.json` 与 live_rig.json 同样只作导航，不覆盖实时资产。

## UI 与设置

- UI 可订阅快捷栏 SlotsChanged/ActiveIndexChanged、生命/死亡委托；装弹提供开始/结束事件，自动重生提供可配置消息 Tag。
- GameSettings.UGameSettingDetailView 通过 HandleCurrentSettingChanged 订阅当前 Setting 的变化并刷新描述/动态详情；DLC Registry 定时变更沿同一路径，不另轮询。
- 切换/清空 Setting、NativeDestruct 时退订；NativeConstruct 保留当前选中项并重建详情/扩展。切换、清空、关闭、释放 Slate 均取消并清空扩展异步句柄；完成/装配回调复核当前 Setting，避免旧请求或重入回填过期控件。
