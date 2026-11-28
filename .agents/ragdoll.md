# Ragdoll 执行、网络与自动恢复

相关主题：[普通移动](locomotion.md) · [Gameplay/ASC](gameplay.md) · [镜头](camera.md) · [PA/PCA 迁移](physics-asset-migration.md)。

## 当前实现与源码导航（2026-11-28）

- 已接合普通刚体/PhysicsControl 身体、CMC 跟随与网络、Ragdoll GA、恢复批准与 GAS Montage/PoseSnapshot 桥接。默认选择是允许列表中首个兼容正反面的动画；MM/PoseMatch 和实际 AnimBP/起身资产适配仍待接入。TPose 也是有效当前 Pose，进入物理不要求先运行 ABP。
- GTA 是主要组织参考，GASP 仅作单机几何/身体/动画参照。UE Authority/Autonomous/Simulated 不等同 GTA peer owner/clone；只同步 pelvis、朝向和速度不保证 Chaos 全身体一致，也不构成全身体历史回滚。
- 保持现有角色上的单个 Ragdoll 执行组件；按职责拆实现文件，不新建独立恢复组件。使用 PhysicsControl 派生类时替换基类实例，不同时装两份协调器。工程默认不编译、不编写测试代码、不调查 Content；实际三端表现由运行验收确认。

| 主责 | 源码入口（相对 Source/ImagineGame） | 合同 |
| --- | --- | --- |
| GA 阶段/稳定/请求 | Public/Character/Ragdoll/IMGGameplayAbility_Ragdoll.h；Private 同目录 .cpp / _Recovery.cpp | 进入、连续稳定、选择/请求、播放、完成/取消/死亡交接 |
| 身体与表现 | IMGRagdollComponent.cpp / _Body.cpp / _Recovery.cpp | 资源/姿势接管、PostPhysics 采样、身体纠偏、恢复 Pose 与 Montage 门控 |
| 物理后端 | IMGRagdollComponent_PhysicsControl.cpp | 控件/BodyModifier 的创建、执行、精确清理；不作 GA 决策 |
| 移动/几何 | IMGCharacterMovementComponent_Ragdoll.cpp；IMGCustomMovementMode_Ragdoll.cpp | 唯一移动生命周期、胶囊跟随、恢复/外部移交、权威几何 |
| 网络/姿态/平滑 | Character/IMGCharacterMovementComponent_{Network,Stance,Smoothing}.cpp；IMGCharacter.cpp | SavedMove/MoveResponse、原生复制接合、附着边界与姿态恢复 |
| 公共数据 | Public/Character/Ragdoll/IMGRagdollTypes.h；GA 私有/TargetData 在 GA 头内 | 移动纯数值 NetSerialize，带资产引用的 RecoveryApproval 保持原生 FRepLayout |

## 网络与数据不变量

- CMC 单独持有 Session/Revision/Inactive-Simulating-Recovering；GA 的 Entering/Settling/SelectingRecovery/Recovering/Ending 是本地业务步骤，不复制第二套移动阶段。角色装配仍负责 Mesh/PA/模型身份。
- 远端玩家 Autonomous 是唯一身体输入来源；Authority 验证并模拟胶囊、批准恢复/结束；Simulated 的本机身体只作表现，不提交目标。AI/本地权威由服务端身体提供输入。专服远端玩家默认不模拟身体，可配置启用消费者；无有效身体不能输出伪稳定零速度。
- FIMGRagdollMovementInput 包含 Session、有效性、物理 pelvis 位置/该点线速度、入场捕获的 CapsuleHeightOffset、FacingYaw/FaceUp；位置走零原点，速度 cm/s。CaptureRagdollInput 在实时 Move 边界按有限采样年龄外推并量化一次；SavedMove 保存并重放结果，不读取实时骨骼/时钟、不重放身体发射。活动 Ragdoll Move 不合并，重要 Move 检查全部相关字段。
- 服务端复核来源/会话、有限数值、目标距离/通路、高度偏移及步长限速。该合同验证客户端建议和胶囊，不独立证明客户端关节动力学或连续身体稳定。
- FIMGRagdollBodyReference 包含 Session/MovementRevision/Sequence、服务端采样时间、Move 时间戳、认可 pelvis/同帧胶囊位置、物理 pelvis 线速度、朝向/正反面/Issue。Velocity 当前来自接受输入的物理速度并限幅，已不再是胶囊跟随速度；GA 仍直接读取身体线/角速度判稳定。
- 参照沿 CMC 属性及 Packed MoveResponse 发布；成功移动使用 ApprovedPelvis = InputPelvis + ActualCapsule - RequestedCapsule，扣回跟随限制未达到的位移。线上 CapsuleLocation + PelvisOffset 防独立量化漂移。Move 结果供响应消费，属性心跳不超过 0.05 秒及 MaxTargetAge/2；实际送达受网络调度影响，不保证固定 20 Hz，不新增逐 Tick Reliable RPC。
- getter 按当前 Session/Phase/Mode、MovementRevision/Sequence、时效和服务端时钟返回本地世界坐标。早到参照缓存但不启动生命周期；坏样本/失效参照不得牵引身体。MaxTargetAge 默认 0.25 秒，断流清速度并发布 Expired。
- Actor/胶囊位置、速度、模式仍走 CMC/Character/FastShared；FSharedRepMovement 不含专门的 Ragdoll 快照/身体参照，且只供 Simulated，不能代替 Autonomous MoveResponse。协议布局更改后服务端与客户端须同版并重启。

## 入场 Pose、速度与身体平滑

- 首入先等待已有并行动画求值完成，把当前显示骨骼姿势交给刚体，再启用模拟；不为 TPose 强行刷新另一套动画姿势。Detach 使用 KeepWorld，物理前缓存附着、规范基础 Mesh 偏移、碰撞、逐 Body 状态及 Tick/更新设置。不能把网络平滑偏移当作永久附着偏移。
- 自由倒地时身体负责接触，胶囊为 NoCollision 的逻辑根。目标为 Pelvis + GravityUp * 入场高度偏移；不逐帧按站立半高/地面探测把胶囊抬起。通路校验用身体锚点路线，恢复时再作完整胶囊查询。自由阶段保留胶囊现有旋转，不把每帧身体 FacingYaw 强设给胶囊；FacingYaw 用于恢复候选。
- 入场将姿态规范到 Stand，保护显示 Mesh 世界变换及平滑偏移；姿态回调不得给已脱离角色根的 Mesh 写相对坐标。模拟端模式接合先保护附着 Mesh，身体接管在模式更新后同步协调，避免先被正常动画/朝向写入一帧。
- InitialVelocity 由角色速度 + 一次移动基底贡献 + AdditionalVelocity 构成；自主端在原基底清除前捕获本地预测发射速度。新鲜入口保留本机显示位置，不直接对齐迟到参照；迟到消费者等待当前有效参照，以参照位置/速度初始化，不重放旧冲量。MaxInitialVelocityAge 默认 0.5 秒。
- 真实已模拟/重启身体保留逐体物理变换、线速度残差及有界角速度；不把运动学身体的缓存角速度当作可信动量。回到运动学时清残留速度。同会话资源重建保留高度偏移；不同会话/资源身份拒绝旧缓存。动画各骨骼的入场线/角速度继承尚未实现。
- 远端自主玩家的服务端入口在 Walking/NavWalking/Falling 时先发布批准，继续处理在途普通 Move，收到首个同会话有效 Ready 输入才切 Custom；EntryInputTimeout 默认 2 秒。不是 LocalPredicted GA 激活，也不承诺零延迟准入。
- 自主端仅过滤当前批准会话中、对应历史 Move 尚未进入 Ragdoll 的旧普通模式纠偏；仍通过原生 GoodMove ACK 清历史。当前会话真实纠偏保留，接受且推进 ACK 的纠偏在重放结束输出一次净位移；历史 SavedMove 不改。
- 拥有者累计 OwnerCorrectionRemaining，小误差按时间/限速整体平移身体，大误差整体重定位；上传物理 pelvis 加尚未消费的剩余纠偏，已施加位移不再叠加第二次。超距/通路拒绝首次约束回最后认可偏移并重置线速度；普通有效 ACK 不反拉身体到旧绝对位置。
- 观察端以 Reference.PelvisLocation + Reference.Velocity * 有界服务端时钟年龄为身体目标，用同一个 pelvis 点的物理线速度作反馈，分水平/竖直限制速度与加速度，并限制反向牵引。超大误差有整体重定位兜底；这不是完整深穿透脱困或接触安全证明。
- Detached Mesh 不消费普通胶囊相对平滑；身体自己收敛旋转与物理，不用包朝向硬改身体。恢复重新附着后，仅清本会话 Mesh 平滑残差，不清 SavedMove/ACK/RootMotion 历史。蓝图可覆写的采样/选片调用之后必须重检组件、Avatar 和会话。

## GA 准入与自动起身

- UIMGGameplayAbility_Ragdoll 为 InstancedPerActor、ServerInitiated、Independent，支持 OnInputTriggered 或精确 GameplayEvent.Ragdoll；AbilitySet 授予仍走既有链。持有 Status.Ragdoll 和 Gameplay.AbilityInputBlocked，阻止死亡/重复激活；权威 Commit 后取消其他非 OnSpawn 能力。分组本身不代替 Tag/玩法互斥。
- 最短允许时间过后，由拥有者或权威 AI 读取 GetRagdollBodyMotion 的有效物理线/角速度。首次确认双阈值稳定才记录 StableStartTime；连续满足达到 RequiredStableTime 后才尝试恢复，不把首次样本前一个检查间隔算入。超速、无有效样本、检查间隔超过 CheckInterval*2、受击版本变化或会话变化均重新起计。

| GA C++ 默认项 | 值 | 语义 |
| --- | --- | --- |
| MinimumRagdollTime | 0.5 s | 初始/受击后最短允许时间 |
| RequiredStableTime | 0.25 s | 最短时间之后的连续稳定时长 |
| MaxStableLinearSpeed / MaxStableAngularSpeedRadians | 40 cm/s / 1 rad/s | 身体速度阈值，不是胶囊零速度 |
| CheckInterval / SelectionRetryInterval | 0.05 s / 0.25 s | 稳定检查 / 选择或落位失败后的重试间隔 |
| MaxRagdollTime / RecoveryStartTimeout | 15 s / 2 s | 异常等待上限 / 恢复启动等待，不是到点强制正常站起 |

- GameplayEvent.Ragdoll.Impact 仅权威接收且未进入恢复时延后最早恢复时间，增加 ImpactRevision、清稳定计时；当前源码只有接收，没有伤害/碰撞来源发送链，蓝图接入未核查。它不自动代表每次接触，更不等于已经实现同会话命中冲量/序号同步。
- 玩家稳定建议经 GAS TargetData，绑定 Spec/ActivationKey、Session、Attempt、ImpactRevision、认可身体参照 Sequence/时间及线/角速度。服务端检查尝试顺序、时效、最短时间、允许资产/倍率/起点及最终几何；无服务端身体时依然是客户端稳定建议加规则复核，不能宣称服务端独立验证连续物理稳定。Simulated 不判稳定、不决定起身。
- 拒绝选片/落位仍保留 Simulating 并按 SelectionRetryInterval 重试。强制异常退出找不到安全落位时可 MOVE_None/禁碰撞并保留身体，救援/重生由上层安排。GA 结束精确清理 TargetData/取消/事件委托、计时器和 Task；死亡先移交身体，Transferred 不覆盖接收者模式。

## 选片、批准与起身表现

- PreSelectRecoveryAnimation / SelectRecoveryAnimation 是 Ragdoll 自己的蓝图适配入口，不启动 Traversal；默认从 Body.RecoveryAnimations 选第一个匹配正反面且兼容骨架的 Montage。每项允许范围约束 StartTime/PlayRate，并持有可信 BodyLocalOffset；客户端不能上传任意落位或匹配偏移。
- 批准前核对 Montage 骨架、Slot、有限且非循环的 Section 路径、正倍率/RateScale、AutoBlendOut；当前拒绝 MotionWarping 窗口，没有恢复专属 Warp 目标/重放合同。MM 输出 Sequence 后须由后续适配明确映射到 Montage 时间，不能直接当作相同秒数。
- QueryRecovery/BeginRecoveryAt 共用 CMC.BuildRagdollPlacement 的原值和量化后核对：完整站立胶囊重叠、可走支撑/边缘、范围/重力/平面约束、身体匹配点两段 LOS 及必要扫掠。FloorClearance 仅用于恢复，FloorTraceChannel/FollowTraceRadius/FollowTraceDistance 已从 Ragdoll Settings 删除；静态入场净空不证明整段动画手脚空间。
- 权威先发布 Recovering 并让身体冻结当前 Pose，再移动/旋转恢复胶囊、恢复碰撞及 Walking。CMC 保持普通输入/跳跃/姿态/Controller 朝向约束，允许正常 RootMotion；胶囊与自由身体不能同时驱动位置。
- FIMGRagdollRecoveryRepState 是表现批准，关联 Session/ActivationKey/ImpactRevision/Attempt、Selection、PlaybackServerTime、MontagePlayInstanceId、批准/跳过/结束；保留原生 FRepLayout 追踪资产引用晚到，不新增逐帧进度 RPC。CMC 仍唯一决定移动阶段。
- PrepareRecoveryPresentation 从冻结身体取 FPoseSnapshot，安装到 AnimInstance 的具名 RagdollRecovery 快照；释放物理保持、恢复规范附着后，对根骨作世界 Pose 反变换补偿。GetRecoveryPoseWeight 用本机实际准备时刻混出（默认 0.2 秒），不因网络迟到跳过全部过渡。下一会话先到时传递原始 Tick 设置、清旧 Pose，避免把恢复期强制 Tick 保存成原值。
- Authority 与 Autonomous 显式执行原生 PlayMontageAndWait；拥有者按服务器开始时间及 RateScale/Section 计算迟到播放位置。模拟端通过 ASC prerequisite 等待批准、同一 Montage/PlayInstanceId、模式/落位和本机姿势就绪；不依赖运行 GA。结束先到不得重启旧 Montage。
- 仅 Authority 的完整 OnCompleted 后下一帧完成恢复，BlendOut 不提前解锁；拥有者本地播放结束仍等权威，失败通过同激活取消通道报告。RootMotion 沿 CMC/GAS；本机 PoseSnapshot/PhysicsControl 句柄/逐骨骼结果不复制。实际 ABP 必须消费恢复快照/权重及 Montage Slot 才能显示完整过渡。
- 用户要求的测试兜底：bAllowRecoveryWithoutMontage 默认 true，选不出 Montage 时仍经权威安全落位，然后跳过播放并完成恢复。此行为优先于旧规划“空 Montage 一律等待”的要求；不是失败落位时强行站起。

## 身体资源与 PhysicsControl

- 一名角色只绑定一个已注册/活动协调器；GetMesh/骨架/PA/pelvis 刚体和配置均须就绪。监听 Parts 变化及 PhysicsCreated，创建失败按 Session/Mesh/SkelMesh/PA 记录并先释放再通知，避免每帧重建；修好配置后可 RetryBodyExecution(Session)。资源等待和真实失败分别报告。
- PhysicsControl 只用现有派生实现，硬引用 ControlAsset；创建成功并验证集合/具名资源后才应用 Profile、模拟 BodyModifier 和混合权重。借用组件须显式或唯一空闲，不清空他人资源；只销毁本方记录的 controls/modifiers，恢复借用的 PCA/Tick，自建空闲 transient 实例在停用/注销/EndPlay 释放。
- Body PrePhysics 在移动/动画后更新控制，关闭后端组件的重复 Tick；PostPhysics 依赖该 PrePhysics 及 Mesh。需要时启用专服物理并重建，释放恢复原状态。恢复冻结先停控件及模拟、保留已显示姿势；组件不自行决定何时起身。
- 组件停用/注销/EndPlay 精确退订 CMC/ASC/Parts/PhysicsCreated、Tick 依赖及前置门控；只结束自己启动的 OwnedSession。外部重新附着是接管，不用旧附着覆盖它；HandOffRagdollBody(Session) 退休本机采样且不恢复身体，旧会话不抢回。MOVE_None 默认保留自由身体。

## 诊断入口

- IMG.Movement.NetDiagnostics 1 对照 RagdollAdmission、RagdollFirstMove 与 Correction 的 Move/Session/ACK；级别 2 增加 RagdollFollow 的物理目标、身体速度与胶囊速度。普通自主端 MeshOffset 的诊断仍见 [普通移动](locomotion.md)。
- Log LogIMG Verbose 查看 Ragdoll Entry 的 Role/Session、PhysicsPoseError、Fresh/Reference、初始平移/速度及胶囊/Mesh/pelvis 朝向，另有 State/ExitRejected；Log LogIMGAbilitySystem Verbose 查看 RagdollGA 与 Montage 前置等待原因。数值异常须结合角色、时间、会话与真实表现核对，日志名称不证明因果。
- IMG.Ragdoll.DebugDraw 为 0 关闭、1 跟随/失败目标、2 恢复候选/支撑/通路；DebugDuration 默认 2 秒仅影响显式查询。编译/静态检查不代替原地/移动入场、旋转、恢复和三端网络运行验收。

## GTA 对照与明确待做

源码相对本机 GTA 的 src/dev_ng/game；路径根见 [工程导航](project.md)。下列为本地快照事实，不代表全部发行版配置或已经达到相同运行表现。

| 入口 | 已核对事实与 IMG 映射 |
| --- | --- |
| Peds/Ped.cpp::SwitchToRagdollInternal（4769）；physics/gtaInst.cpp::ActivatePhysics | 等待动画、交接当前姿势/速度、切换主物理实例。IMG 保留当前世界 Pose；动画逐关节速度继承仍待做 |
| NetBlenderPed.cpp::Update（243）、UpdateRagdollBlending（1291） | 自由倒地禁普通 heading blending，位置误差转为有界速度/加速度、限制倒拉及分离 XY/Z；IMG 身体纠偏按此分责，参数按 UE 单位制定 |
| GetCurrentPredictedPosition（1573）；NetObjPed::GetStandingOnObjectData（7779） | 载体引用、局部位置及特殊支撑分支；非载体一般外推条件排除 Ragdoll，不能把 IMG 的时间外推说成逐行照搬 GTA。持续载体相对同步/稳定判定未实现 |
| TaskNM.cpp::ProcessFinishConditionsBase（2884）、CheckBlendOutThreshold（2808）、GetSettledTime（6879） | 最短时间后检查真实线/角速度、连续稳定起点，超限重置；有玩家/AI/MP 分档和可选固定 seed 映射的随机稳定时长。IMG 现为固定配置，不照搬未核实的最终调参值 |
| TaskNM.cpp::OnMovingGround（2754）、BumpPerformanceTime（5904）；TaskNMShot.cpp（1377/2535） | 载体参照速度和受击延时由各自来源供给；IMG 的 Impact 接收已接合，发送/后续冲量及载体相对事实仍待做 |
| TaskNM.cpp::HandleDriveToGetup（5935）；TaskGetUp.cpp::ChooseGetupSet_OnUpdate（1583） | 起身匹配按专门间隔/探测就绪节流，受阻重试；驱动到起身姿势还受速度、姿态、生命和最短时间约束。IMG 仅完成选择/重试框架，未实现 NM 主动驱动或 MM |
| Peds/Ped.cpp::SwitchToAnimated（5178）；TaskGetUp.cpp::Finish_OnUpdate（3491） | 先停身体再放置根并反补偿冻结 Pose，clone 不能抢先宣告完成；IMG 以 Pose 桥、权威完成和三端门控映射 |

- GTA 网络位置在 NetObjPed.cpp::GetPosition（3080）取 RAGDOLL_BUTTOCKS 世界参照，PedSyncNodes.cpp（466）可传载体 ID/局部偏移；不是只同步站立胶囊。TaskGetUp::CreateQueriableState/ReadQueriableState（572/587）同步阶段/集合，clone 仍可按本机姿势选片；IMG 首版统一批准的 Montage/逻辑起点/倍率，不能宣称各端 MM 或终姿相同。
- 随机 seed 仅约束消费该 seed 的行为，不能保证 Chaos 接触确定；TaskNM.cpp::SwitchClonePedToRagdoll（6043）复制 seed 也不能证明全骨骼回滚。当前不为没有消费者的随机状态新增复制字段。稀疏姿态约束、深穿透脱困、LOD/预算与载体救援仍需独立范围和运行证据。
- **待做：倒地玩家方向输入（用户要求本轮仅调查，不接入）。** TaskNM.cpp::ProcessPhysics（2627）受 GetPlayerDataHasControlOfRagdoll 或默认 false 的开发开关门控；commands_player.cpp::CommandGivePlayerRagdollControl（2585）显式授权，Ped.cpp::SwitchToAnimated 清标记。摇杆按相机 heading 转为水平力，强度乘输入幅度、质量和行为控制系数，作用于身体根部；标记分支还发 NM look-at/body-turn，无输入时有注释标明 Drinking 来源的随机倾斜，不能当成普通倒地默认控制。
- IMG 当前 Input_Move → AddMovementIntent → AddMovementInput 仍是普通意图链，Ragdoll Mode 不用普通 Acceleration 施力，身体上的 AddForce 只用于网络收敛。以后在现有输入入口分流、身体 PrePhysics 消费有界控制请求，明确 Session/角色/时效、三端准入与稳定计时关系；不直接移动胶囊、不建立第二个输入/移动权威，也不机械照搬 GTA 单位或 NM 行为。

## 外部恢复与空中接管

- 用户明确：落地起身只是保底路线，恢复可由外部动作入口触发；不要求先落地、先稳定或先播放站起动画。完整语义是“退出自由 Ragdoll，并交给已批准的目标执行者”。最短倒地/稳定等待属于需要这些条件的业务路线，不是所有外部接管的组件硬门槛。
- GTA 已核对：`event/Events.cpp:3218 CEventInAir::CanInterruptNMForParachute` 检查当前 ragdoll 与 CanPedParachute；`task/Movement/TaskParachute.cpp:1656 CanPedParachuteFromPosition` 对 ragdoll 检查下降速度、NM 运行时长及后续空中净空；该任务进入时若仍为 ragdoll 切 State_BlendFromNM（约2382），`BlendFromNM_OnEnter:2399` 创建 CTaskBlendFromNM，`AddGetUpSets:862` 提供 NMBS_SKYDIVE_GETUPS 和非 clone 的 CTaskMoveInAir。这里的 GetUp/BlendFromNM 具备空中动作过渡含义，不能按名字限定为地面站起；下落/开伞/碰撞中断还有各自状态检查。
- UE 分责：滑翔/飞行/水中等来源 GA 判断自身入口是否成立并准备资源，通过统一过渡合同与当前 Ragdoll 执行交接；Ragdoll GA 组织本次能力结束/转交。组件提供当前姿势、运动事实和本方资源释放；CMC 批准对齐、速度及目标运动模式。组件/CMC 不反查 Glide/Parachute GA，也不把特定滑翔条件写入 PhysicsControl 派生类。
- 通用过渡须关联 Session/本次请求/接收执行者、批准目标模式/CustomMode、必要对齐和身体参照、承接速度及时间/姿势依据。地面起身入口继续执行地面几何与 Walking 合同；空中接管执行对应空中净空/速度/姿态合同，支持 Flying 或明确注册的 CustomMode。目标资源及放置未就绪前保持 Ragdoll，批准并成功接合后精确退休旧输入/牵引/回调，保留连续姿势及速度；拒绝/失败不提前关闭物理，也不自动改 Walking。旧 GA 的结束清理不能覆盖已接管的模式/身体。
- 三端按批准的过渡合同/模式/身体资源就绪接合；模拟代理不靠本机猜测触发空中动作。正常落地可由 GA 保底策略提出地面恢复请求，Component 不新增自动落地起身计时器。
- 统一外部入口现为 Component.QueryTransition → BeginTransitionAt → CMC.QueryRagdollTransition/TransitionRagdollMovementAt。这些入口不自行发送客户端 RPC，真实准入与客户端请求仍由来源 GA 的 GAS 合同负责；应先准备接收动作、请求移交，成功后再结束旧 GA，不能先 Cancel 旧 GA 并关闭身体再尝试目标动作。组件/CMC 不新增滑翔类引用、ASC 或动作选择依赖。
- 地面 BeginRecovery/BeginRecoveryAt 保留原有起身语义；两种路线共用 CMC.BuildRagdollPlacement 的原位置与量化后几何核对。外部入口要求当前同 Session 的有效身体输入，检查目标模式、单位缩放、重力对齐、胶囊净空、通路与局部放置范围；Walking 要求地面，其余路线不施加地面起身速度阈值或强制落地。各动作自己的最短时间、下降速度、所需空中净空/水域等仍由来源校验。
- SupportsRagdollTransitionMode 为 CMC 的 protected 原生扩展点，默认支持 Walking/Falling/Flying/Swimming 且 CustomMode=0。Custom 必须在实现对应物理的 CMC 派生类显式开放；不能把 Ragdoll 自己作为接收模式，拒绝未知/无效编号。开放模式只声明执行支持，不代替 GA 动作准入。
- CMC 提交前验证 CarryVelocity 有限值及 NetQuantize10 可传输性，使用量化承接速度。位置提交采用 FScopedMovementUpdate；仍处于原 Ragdoll 而移动未达到目标/模式被拒时恢复位置与速度；回调中真实的其他模式接管则结束旧会话并尊重新所有者，不回滚覆盖它。延迟 overlap/物理体积回调先解析，再发布成功结果。
- 目标模式进入前先写入批准传入速度并释放旧朝向占用，再 SetMovementMode；接收模式可按自身物理规则处理实际速度/朝向，旧 Ragdoll 不在之后覆盖它。TransitionVelocity 记录批准的传入事实，当前实际速度仍沿原生移动通道更新。权威 Tick 也兜底退休已被其他模式接管却仍残留 Simulating 的会话。
- 成功结果直接将移动 Phase 退休为 Inactive、EndReason=Transferred，保留 bHasRecovery/bExternalTransition、目标 Mode/CustomMode、承接速度、对齐及原 Session/新 Revision。接收动作自己的生命周期由来源 GA 持有；Ragdoll 不再维持第二个飞行/滑翔状态机。普通旧 Abort/CompleteRecovery 对已移交会话不能重设 Falling/Walking，即使拥有者还在等待原生移动结果。停止/注销旧组件同样不再终止接收移动。
- 网络继续扩展既有 FIMGRagdollRepState.NetSerialize，沿原 CMC 复制属性和 FIMGCharacterMoveResponseDataContainer 回传；位置、模式及持续速度继续由原生 CMC/Character/FastShared 承载，没有新增移动 RPC 或身体逐骨骼复制。Transferred 使结束原因从2位扩为3位；客户端与服务端须同版。外部快照只作批准事实，不从 OnRep 再硬设拥有者位置/速度/模式。
- 自主代理须收到原生真正推进 ACK、对应已批准放置且未被过滤/忽略的移动应答后才能就绪；模拟代理等待当前原生模式接合。原生模式/快照/应答可乱序：OnMovementModeChanged 及接受应答会唤醒组件重检，早到事实不构成可执行信号。地面恢复也区分本机已落位标记 RagdollLocalPlacementSession 和原生接受证明 RagdollRecoveryAppliedSession，避免把一次本机应用快照当成权威应答；已结束且已脱离 Ragdoll 的旧地面快照不再把拥有者拉回起点。
- 接收方使用 (Session, TransitionRevision) 关联自己的 Spec/Activation/执行实例；接收者指针和业务类型不塞进 CMC。GetTransitionMovement 可提前读取批准模式/速度准备资源，IsTransitionReady 才是执行门控；不要用迟到的 TransitionVelocity 每帧覆盖当前速度，持续运动由原生移动通道负责。事件只通知事实，不自动激活 GA；晚加入无完整物理姿势时接收动画必须有后备。
- Component.ReconcileExternalTransition 在就绪后保留本机姿势/世界参照并冻结本方身体，先发旧 OnRagdollEnded(Transferred)，再发 OnTransitionReady(Session,Revision)，避免旧结束逻辑清掉新动画。接收动画用 GetTransitionPose 取得快照及捕获世界变换，完成相对当前 Mesh 的姿势接合后 ReleaseTransitionPose；接收者取消则用 bDiscard=true，仅释放该令牌的本机姿势/资源，不改接收移动模式。旧地面释放入口和裸 HandOffRagdollBody 不得释放新移交保持；旧 Session/Revision 一律拒绝。
- 保持期间根位置继续由新移动模式驱动，Mesh 暂持捕获世界姿势。权威端或客户端已接合后的实际模式被其他动作接走，则释放旧保持并报告 TransitionSuperseded；客户端首次就绪前不能凭仍持有身体就断言接管失败，旧原生模式可能晚于快照到达，由接收 GA 管等待/取消。外部重新附着同样退休本方身体写入；Mesh/PA/组件失效先退休资源再通知，包含已结束旧逻辑会话但仍持接收姿势的情况。BodyIssue 去重同时关联 Session，外部回调后重检令牌再继续。GA 仍负责自己等待资源的超时、取消及精确退订，组件不新增业务计时器。
- 当前边界：已提供通用外部移交/姿势执行与网络接合接口；Ragdoll GA 及地面恢复门控已接合；滑翔/飞行 GA、物种主动行为和 MM/动画资产适配仍未实现。外部动作事实与视觉衔接仍由对应接收者落地，不能将接口齐全视为完整滑翔玩法或三端表现验收完成。
- MOVE_None/执行设施失效会退休接收保持并通知不可执行，不保留永久冻结身体；死亡保留物理遵循既有 bKeepBodyWhenMovementDisabled 策略。正常释放须等组件已准备接合，取消可提前丢弃；旧结束回调中取消接收者后，不再补发该令牌的就绪事件。

## 非人形物种边界

- 当前执行器不是任意 Actor/Pawn 通用组件：Initialize 明确要求 ACharacter 与 UIMGCharacterMovementComponent，身体固定取 Character.GetMesh；普通物理、PhysicsControl 创建/清理及整体纠偏算法不检查人类类型。
- PelvisBone、OrientationBone、FaceUpAxis 是可配置骨架绑定，pelvis/spine_05/Y 只是人形默认值。PelvisBone 实际承担有有效模拟刚体的身体位置参照，可绑定四足/鸟的相应骨盆或躯干骨；朝向默认由参照骨到 OrientationBone 的向量投影计算，特殊骨架可覆写采样，须保留拥有者 GetBodyMovementOffset 合同。各物种分别配置 PA/PCA、约束/质量/控制集合、碰撞和跟随/纠偏参数。
- 物理倒地、采样、联网跟随和生命周期可复用，不等于完整恢复流程已适配。当前 Mode 使用直立且沿重力对齐的 Character 胶囊；地面起身查询和 CMC.IsRagdollRecoveryReady 保持 Walking 合同，外部动作改走上述通用移交。四足长躯干、鸟展开翼的净空不能由该胶囊证明，水平碰撞体/多碰撞体不属于当前实现。
- 鸟的物理坠落/落地可按上述合同配置接入；空中退出 Ragdoll 并承接 Flying 已有通用移动/姿势移交接口，仍需物种 GA 准入与动画实现。不能在 PhysicsControl 派生类直接改移动模式，也不能把通用 Flying 等同于已实现鸟类飞行控制。
- 四足翻身/四肢支撑起立、鸟收翼/展翼/重返飞行等动作属于物种 GA、选择/动画及需要时的主动身体行为。bFaceUp 是单一正反面事实，不足以完整分类动物侧卧/背卧/腹卧和支撑姿态；MM 可读取完整本机姿势，新增决定性分类/网络事实时再按实际消费者扩展。未针对动物资产或运行场景验收，不宣称全部物种即插即用。

## GASP 单机参照（2026-11-26 核对）

- 用户明确：权威/联网组织以 GTA 为主参考，GASP 没有联网，本文仅将其作为单机身体、跟随几何和动画实例；读取到 Mover InputCollection 不构成联网验证，不迁入 Mover 网络架构。
- 已从本地 `D:/Application/UnrealProjects/GameAnimationSanple` 使用 UE 5.8 Python commandlet 只读加载并经 ObjectExporterT3D 导出 `/Game/Blueprints/SandboxCharacter_Mover_Ragdoll` 和 `/Game/Blueprints/MovementModes/BP_MovementMode_Ragdoll`，沿节点 LinkedTo 核对，未调用保存资产。临时证据在 `Saved/Codex/RagdollResearch`；长期入口以资产路径和函数名为准，不依赖临时导出继续存在。
- `SandboxCharacter_Mover_Ragdoll.Get_RagdollTransform` 确实读 `pelvis` 的 RTS_World 骨骼位置；另读 `spine_05`，以 spine-pelvis 和胸部 RightVector.Z 的正负整理水平朝向，通过 MakeRotFromZX 保持世界Z朝上。`ProduceInput` 在 Ragdoll 模式分支调用它，把 RagdollTransform 写入 `S_MoverCustomInputs_Ragdoll` 并加入 InputCmd.InputCollection，朝向另写入 CharacterDefaultInputs.OrientationIntent。它把身体采样与移动消费分开，不在 Mode 内直接逐步读取实时身体姿势。
- `BP_MovementMode_Ragdoll.SimulationTick` 从输入读取上述结构，先采用身体变换，再从身体位置向世界Z下方88 cm做 Radius=30 cm 的 SphereTraceSingle。命中时保持 XY，按 `BodyZ + 86 - Abs(ImpactPointZ - TraceStartZ) + 1.9` 修改目标Z；通常地面在身体下方时等价于 `ImpactPointZ+87.9`。未命中保留身体目标。它使用样例硬编码尺寸，并非适用于任意胶囊的参数合同；IMG 应保持当前实际缩放胶囊半高/重力方向等消费规则，不能原样搬常量。IMG 当前自由倒地胶囊不做逐帧地面抬升，仅恢复时查询完整胶囊落位；两者公式及职责不同。
- 随后以 TargetLocation-StartSyncLocation 计算 MoveDelta，调用 `TrySafeMoveAndSlideUpdatedComponentNoMovementRecord`，该节点明确 `bSweep=false`；朝向来自 OrientationIntent。输出位置/朝向从 UpdatedComponent 读取，速度按 MoveDelta/StepSeconds 填入 Mover SyncState。故“身体参照→地面高度修正→胶囊移动”属实，截图只能辅助观察，不能单凭静态图证明调用链或网络能力。
- Epic 5.8 官方 GASP 更新说明 `https://www.unrealengine.com/tech-blog/download-the-latest-game-animation-sample-project-now-updated-for-ue-5-8` 确认新 Pawn 通过 Physics Control 的关节马达做 powered ragdoll，涵盖保护落地、受伤、挥动、滚动及 motion-matched getup。它是主动身体与选片参照，不证明跨端物理一致性。
- 身体实现可拆装：普通 Mesh 刚体模拟可作被动身体提供者，PhysicsControl 可在同一批 Chaos 刚体上增加关节驱动、Profile 和 BodyModifier，并非必然是与 Simulate Physics 完全分离的另一套求解器。引擎 `PhysicsControlComponentImpl.cpp::ApplyBodyModifier`（约794行）按 Static/Kinematic/Simulated 调 `FBodyInstance::SetInstanceSimulatePhysics`，并设置 PhysicsBlendWeight、碰撞及重力。直接 Mesh 模拟开关与持续 BodyModifier 更新会争写同一状态，必须只有一个身体适配所有者负责接管/释放；切换时保留姿势/速度并退休旧控制资源，不叠加两个独立驱动者。
- “最终只影响 pelvis”仅能用于描述移动层的消费接口：身体模拟实际影响全部关节、碰撞、角动量、稳定性、正反面和起身姿势；pelvis 是低维移动锚点，不足以重建完整身体。公共适配需覆盖可用性、开始/退出、施加事件、采样身体参照/运动、消费权威纠偏和恢复交接。当前 GetRagdollFollowTarget/SubmitRagdollFollowTarget 已有目标接点，完整身体执行已接合，但此位置接口仍不能代替全身体状态。
- AddMovementInput 的类比在“来源提交→逐步保存→移动层消费”上成立；实际 AddMovementInput 是运动意图，上传 pelvis 是身体模拟结果作为移动输入。建议沿 GTA 分层适配现有 UE 合同：玩家 Autonomous 主导本机身体轨迹，服务端验证输入后重算胶囊跟随并发布批准身体参照，Simulated 消费胶囊与身体参照并运行本机表现收敛。服务端由同一采样复现的是胶囊运动，不是完整身体物理；无需先选定 PhysicsControl 才能实现通用移动层，也不能宣称单靠 pelvis 已支持全身体网络重模拟。

## PA / PhysicsControl 资产基线

以下为历史迁移与运行时调查记录，参数以当时回读为准，后续资产改动须重新核对。

完整复用流程、用户资料包、不同骨架映射公式、统一脚本入口、工具限制及本轮全部参数/原脚本归档见 [PA/PCA 迁移流程](physics-asset-migration.md)。下次迁移由AGENTS先读该入口，不直接重跑Saved中的历史硬编码脚本。

- 2026-11-25 已从 `D:/Application/UnrealProjects/GameAnimationSanple/Content/Characters/UEFN_Mannequin/Rigs/PA_UEFN_Mannequin.uasset` 移植到 `Content/Characters/Heroes/Mannequin/Rigs/PA_Mannequin_PC.uasset`。目标为 Manny 预览 Mesh；原 `PA_Mannequin` 不作为此次修改对象。目标现为23刚体、19胶囊+4盒、22约束、185对可碰撞组合（68对禁用），已在编辑器回读确认。迁移前目标为22刚体、23约束（3交叉约束）、4对可碰撞组合。
- 源/目标骨架骨名对应，但参考姿势和骨长不同，不能直接复制全部关节坐标。2026-11-25 迁移时保留源碰撞形状、BodyInstance、Constraint Profile/驱动/软限位/投影等参数；保留子体 `Pos1/PriAxis1/SecAxis1`，按 Manny 参考姿势重新对齐父体 `Pos2`，旋转父体 `PriAxis2/SecAxis2`，保持源参考姿势下两约束框架的相对角度。直接套用源坐标在 Manny 上的最大锚点偏差约9.43cm；不得用增加阻尼掩盖该几何错误。骨架参考入口为 `AnimPoseExtensions.get_reference_pose(mesh.skeleton)` 的 LOCAL/WORLD 姿势；后续换 Mesh 仍须重新核对参考姿势及形状适配。
- 2026-11-26 按用户指定，以同目录 `PA_Mannequin` 为 Manny 几何基准，修正 `PA_Mannequin_PC` 的22个同名刚体：复制胶囊/盒的中心、旋转和尺寸，保留目标形状名称及其他属性；额外 `spine_01` 无参考项，维持原设置。仍为23体/22约束，BodyInstance、约束框架/Profile、材质、碰撞禁用表和PCA引用沿用上次结果。保存前已确认完整T3D只有AggGeom变化；独立新进程从磁盘导出的目标T3D与保存前修正版完全一致。用户反馈“正常了”，未做新的游戏/网络测试。备份与此次临时脚本/导出已按用户要求清理；热重载断言边界见[工具细节](references/physics-asset-migration/tools.md)。
- 新物理材质 `Rigs/PM_Mannequin_PC` 复制源 `PM_UEFN_Mannequin`：Friction=1.75、StaticFriction=1、FrictionCombine=Multiply并覆盖、Restitution=0、RestitutionCombine=Multiply并覆盖、SleepLinearVelocityThreshold=1、SleepAngularVelocityThreshold=0.05、SleepCounterThreshold=4。依源资产保留 `neck_02/head` 的无显式材质状态，其余21体引用新材质。源 `neck_02` 的 MassInKgOverride 数字为默认100但 **bOverrideMass=false**，不能将该数字加进覆盖质量总和或解释为实际100kg；此次按源设置保留。
- `PCA_Mannequin` 与源 `PCA_SandboxCharacter` 的 authored setup、集合及 `kinematic/physicalAnimation/ragdoll` Profiles 原本一致；此次将其 PhysicsAsset 从旧 `PA_Mannequin` 改为 `PA_Mannequin_PC`。回读 `IsCompilationNeeded=false`，不以只比较 authored 字段代替编译数据检查。PA 的 SolverSettings 同源为 Position/Velocity/Projection=6/1/1、CullDistance=3、UseLinearJointSolver=true、UseManifolds=false，SolverType=World；引擎 `PhysicsAsset.h` 明确这些 SolverSettings 的数值用于 RBAN，不应据此断言它们控制 World ragdoll 的全部迭代设置。
- `Config/DefaultEngine.ini` 的 `[/Script/Engine.PhysicsSettings]` 同源显式配置：`bTickPhysicsAsync=False`、`bSubstepping=True`、`MaxSubstepDeltaTime=0.016667`、`MaxSubsteps=16`、`bSubsteppingAsync=False`、`AsyncFixedTimeStepSize=0.016667`。迁移前目标 Substepping=false、MaxSubsteps=6、AsyncFixedTimeStepSize约1/30秒；该配置影响全工程物理。两边完整 PhysicsSettings 回读一致，1208个 `p.*` CVar 同名同值；不是只比 DefaultEngine.ini 文本。
- 编辑器鼠标弹簧与插值是 `UPhysicsAssetEditorOptions` 的预览设置，不是 PA 关节驱动或 PCA Control 参数。本次两边本来一致：线阻尼/刚度200/750、角阻尼/刚度500/1500、InterpolationSpeed=50；重力倍率1、无GravityOverride、PhysicsBlend=1、UpdateJointsFromAnimation=false、MaxFPS=-1。源码入口 `Engine/Source/Editor/UnrealEd/Classes/Preferences/PhysicsAssetEditorOptions.h`。比较动态表现仍需一致的预览初姿、帧率、地面碰撞、拖拽/冲量及 PCA Profile。
- 迁移轮已完成参数/依赖/编译状态回读与编辑器结构确认；用户实际模拟反馈“效果很好”。这不等于完整游戏运行链、全部冲量/姿态/帧率或网络场景验收。历史导出、适配说明与回读证据在 `Saved/Codex/PhysicsParity` 及实例文档的case-data；该目录原备份和迁移验证备份已于2026-11-26按用户要求清理。Saved只作当次证据，不成为运行时依赖。临时迁移Content已清理；最终PA仅依赖 Manny预览Mesh与新物理材质，没有 UEFN工程或临时目录依赖。

### 运行时调用时序（PhysicsControl 组件）

- 四条刷屏 warning 同源，全部来自 `UPhysicsControlComponent`（引擎 `Engine/Plugins/Animation/PhysicsControl/Source/PhysicsControl/Private/PhysicsControlComponent.cpp`，5.8 已从 Experimental 移到 Animation）。`No physics asset in skeletal mesh`（无 owner 后缀）只可能出自 `CreateControlsAndBodyModifiersFromLimbBones` 的 line 3191；该函数在 `SkeletalMeshComponent->GetPhysicsAsset()` 为空时直接 return false，`CreateControlsAndBodyModifiersFromPhysicsControlAsset` 见失败即整链 return（line 3232-3246），因此控件与 BodyModifier 数为 0，PCA 的 InitialControlAndModifierUpdates 也不会应用。
- `WorldSpace`/`ParentSpace` 是 `GetControlTypeName()`（line 39-50）自动命名的集合，`All` 由 `FPhysicsControlNameRecords::AddControl` 的默认 `bAddToAll=true` 自动加入（`PhysicsControlNameRecords.cpp` line 18-27）；三者只在控件/修饰器成功创建后才存在。`ExpandName`（`PhysicsControlNameRecords.cpp` line 147）命中集合才展开，否则原样当控件名查 → `ApplyControlAndModifierUpdates`（line 3330-3414）报 `Failed to find control/modifier with name ...`。即"找不到集合"等价于"本次调用时一个控件都没建出来"，不是名字拼错。
- 本机 `Saved/Logs/ImagineGame.log`（2026-11-25）对照：13:35-13:36 批次含 `WorldSpace_Feet` 与 `Feet` 修饰器，对应 PCA 的 `PhysicalAnimation`；13:38 起只剩 `WorldSpace`/`ParentSpace`×2/`All`，对应 `Ragdoll`（与上方 PCA 快照 1/2/1 一致）。每批都是"No physics asset"同帧紧跟 ApplyControlAndModifierUpdates，说明调用方没有用 Create 的返回值兜住就调 InvokeControlProfile。
- 触发时机在 `B_ExperienceDef_ShooterCore` 加载后的 Pawn 生成阶段，紧跟其后同一毫秒报 `Controller Parts ... has no available PawnComponent_CharacterParts ... waiting for CharacterPartsReady`（`IMGControllerComponent_CharacterParts.cpp` line 161）。身体网格要等 `UIMGPawnComponent_CharacterParts::BroadcastChanged` 里的 `SetSkeletalMesh` / `SetPhysicsAsset(ForcedPhysicsAsset)`（line 345-359）才带 PA，此时 `GetPhysicsAsset()` 仍为空，故不是 PCA 或 PA 资产问题，而是运行时顺序：先建控件失败，再套 profile。
- 修法与就绪信号：`NAME_CharacterPartsReady`（`IMGPawnComponent_CharacterParts.cpp` line 254 的 GameFrameworkComponentExtensionEvent）或 `OnCharacterPartsChanged` 之后再 Create，Create 返回 true 才 Invoke；换 body style 或 `ForcedPhysicsAsset` 变更后须先释放旧控件再重建。`bWarnAboutInvalidNames`（`PhysicsControlComponent.h` line 1706-1710）只静音不修复。新增 C++ 派生执行器已核对实际 Mesh/PA/身体就绪并管理创建/释放；此前 warning 的具体 BP/AnimBP 调用点仍未调查，接入原生执行器时不能让旧蓝图再建立第二套控件或 Tick。
- UE 5.8 `UPhysicsControlComponent::GetAllControlNames()` / `GetAllBodyModifierNames()`（`PhysicsControlComponent.cpp:3167/3446`）返回线性 `TArray<FName>` 引用；运行时存在性校验应使用 O(1) 的 `GetControlExists` / `GetBodyModifierExists`（`PhysicsControlComponent.h:1639/1643`），避免热路径内层 `.Contains()` 退化为 $O(N^2)$。

### 编辑器工具边界

- 本轮会话 MCP 端点为 ImagineGame `http://127.0.0.1:8001/mcp`、GameAnimationSanple `http://127.0.0.1:8002/mcp`。端口/编辑器状态会变，操作前先验证工程身份。HTTP需先 initialize并保存 `Mcp-Session-Id`，再通知 initialized；不带session直接调用tools会400。内置客户端配置曾指向8000，不能据配置文件认定实时端口。
- 内置 ObjectTools 不提供 PA 刚体数组和非反射 `CollisionDisableTable` 的完整编辑。`ObjectExporterT3D` 可导出刚体/约束结构，但不能代表完整碰撞禁用表；整资产复制保留该表，再逐项适配。`PhysicsAssetEditorSharedData.cpp::CopySelectedBodiesAndConstraintsToClipboard/PasteBodiesAndConstraintsFromClipboard` 会按骨名输出/恢复 `PhysicsAssetCollisionPair`；粘贴先启用所有粘贴体之间碰撞，再按文本禁用，但不会自动删除目标多余约束，不能只粘贴一次就认为拓扑一致。
- Python读取未生成专用包装的内嵌UObject时须用原始反射名，如 `load_object(None, pa_path+':SkeletalBodySetup_18').get_editor_property('DefaultInstance')`；snake_case 可能找不到字段。PA的 `SkeletalBodySetups/ConstraintSetup/PreviewSkeletalMesh` 是受保护字段，不应假定通用getter可读。此次预览Mesh经原生反射 `get_default_object(SystemLibrary).call_method('SetSoftObjectPropertyByName', args=(pa, Name('PreviewSkeletalMesh'), mesh))` 设置；Value传加载的Mesh对象，不能传SoftObjectPath包装。修改后须用完整导出和最终依赖回读确认。
- 已加载uasset可能仍被UE文件读取器占用；仅关闭资产编辑器不保证释放。此次目标无referencer且无dirty修改，备份后关闭编辑器、释放Python资产引用并 `EditorLoadingAndSavingUtils.unload_packages`，再替换同名包、加载并保存。任何后续迁移须重新确认引用/dirty状态及备份，不能直接覆盖已加载或有未保存修改的文件。会话内的 CodexPhysicsParityTools 是此次任务临时Python Toolset，不是工程插件或通用运行时接口，重启即消失；不依赖开启Python远程执行。

## 运行验收矩阵

- 拓扑：Standalone、Listen 主机玩家、Listen 远端玩家、Dedicated 拥有者与第二观察客户端、服务端 AI/无 Controller（核对 bRunPhysicsWithNoController）；专服必须验证未渲染仍完成所需逻辑/Root Motion。
- 运动：原地、运动中/高速击飞、空中落地、坡面/台阶/墙角/低顶、初始重叠、身体/胶囊偏离、通路受阻、睡眠唤醒、冲量重复/丢失及反复受击；角色尺寸/缩放、不同 PA/骨架坐标按实际支持范围验证。
- 恢复：正反面/侧躺、无 PoseHistory/MM 无结果、Sequence→Montage 时间映射、非零 StartTime/RateScale/有限 Section、资源晚到、起身失败/受阻/中断/再受击、Root Motion 结束/混出、姿势过渡与最终支撑。
- 网络：延迟/抖动/丢包/乱序、旧 SavedMove 与快照、GoodMove ACK/真实纠偏/忽略纠偏、入场/恢复/结束同帧合并、结束先到、引用晚到、重新相关/晚加入、Dormancy 唤醒、拥有者断流/退出；确认没有身体→胶囊→身体反馈、双重初速度/冲量、重复恢复与旧回调清新会话。
- 移交：死亡/Reset/重生、ASC换 Avatar、取消/移除能力、组件停用/重注册、Mesh/PA替换、地图切换及动态载体消失；检查资源成对释放且不覆盖接管者的模式/碰撞。按工程约定，运行验收由用户完成，最新代码仅完成静态核对；不得用历史编译结果代替当前运行验收。
