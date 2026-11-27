# Ragdoll 移动与 GA 接入

相关主题：[普通移动](locomotion.md) · [Gameplay 框架](gameplay.md) · [镜头](camera.md)。

## 落地组织与当前边界（2026-11-27）

- 用户已于2026-11-27授权完成第一步并继续落地身体执行 Component/PhysicsControl 派生接入；按 GTA 已验证的实现链逐项映射到 UE，GASP 只作单机几何、身体驱动和动画参照。自主代理表现流畅优先。CMC 参照/纠偏接点及身体执行代码已接合，尚未编译/运行验收；GA/MM 等后续步骤仍按各自范围实施，本文拟议合同不能当作全部实现完成。
- 实施顺序以本节为准，细节清单见后文。每项变更先列 GTA 来源函数与实际消费者、IMG 现有入口/缺口及 UE 合同差异，再修改；新发现沿完整上下游补查，不能只按先前推测清单施工。区分“源码已验证”“UE 映射选择”“待运行验证”，不把 GTA owner/clone 直接改名后当作 UE Authority/Autonomous/Simulated 等价物。

| 依据 | GTA 已核对入口 | IMG/UE 映射与限制 |
| --- | --- | --- |
| 身体作为位置参照 | `NetObjPed.cpp:3080 GetPosition`、`Ped.cpp:3760 UpdateEntityFromPhysics` | pelvis 参照输入→Mode 计算胶囊；保留骨骼/刚体原点差与地面高度修正。GTA 无本工程 CMC，具体胶囊跟随公式属于 UE 适配 |
| owner 行为与 clone 重建 | `TaskNM.cpp:4892/4913`、`TaskNM.h:1599`、`PedSyncNodes.cpp:466` | 唯一会话/阶段、有效输入及必要行为事实；UE 保持服务端准入/验证，不能把客户端结果样本误当完整身体重模拟 |
| 远端身体收敛 | `NetBlenderPed.cpp:1291 UpdateRagdollBlending`、`:1573 GetCurrentPredictedPosition` | CMC 提供权威移动及身体参考，身体适配消费并纠偏；保留状态/接触/速度约束，不把关节施力塞入 CMC，不照搬 GTA 参数 |
| 迁移和迟到状态 | `NetObjPed.cpp:2691 CanAcceptControl`、`TaskNM.cpp:5092/5146` | 对应来源失效/接管、Avatar/Session 退休及乱序接合；UE 不新增 GTA 式 peer migration，死亡与外部模式不能被旧清理覆盖 |
| 稳定与支撑 | `TaskNM.cpp:2754 OnMovingGround`、`:2808 CheckBlendOutThreshold` | 身体提供运动/支撑事实，GA 决定稳定等待；CMC 胶囊零速度不是身体稳定证明 |
| 起身选择和结束 | `TaskGetUp.cpp:572/587/1583/3491/4785` | 本机身体 MM→服务端批准一个恢复合同→GAS/CMC 执行；首版不复制 GTA clone 独立选片，批准结果统一消费 |

| 顺序 | 工作包 | 完成边界 |
| --- | --- | --- |
| 1 | CMC/MovementMode 基底与网络合同 | 先审查并增量完善现有实现：pelvis 输入/采样来源、Session/Revision/Phase、逐 Move 保存/上传/权威验证/纠偏重放、模拟代理消费、身体参考输出接点、恢复/结束与生命周期清理。数据必须有明确生产者/消费者，不先增加无消费者字段；本步不引入 GA/MM/PhysicsControl 依赖 |
| 2 | 可拆装身体适配与三端表现 | 同一合同支持普通 Mesh 模拟和 PhysicsControl；完成进入、一次性速度/冲量、世界姿势/附着、持续采样、拥有者及观察端身体收敛、停止/恢复交接。完成该步才有完整 Ragdoll 身体三端表现，不能用第一步接口存在代替验收 |
| 3 | GA 业务与恢复请求 | 准入/互斥、稳定建议与权威规则、同会话受击、恢复尝试/超时、取消/死亡交接；复用现有移动恢复几何，不创建第二套移动权威 |
| 4 | MM 选片和 GAS 起身 | Ragdoll 自己的选择入口、Montage/逻辑 StartTime/Rate/匹配依据、服务端复核、拥有者显式播放、模拟代理 Montage 前置接合、PoseSnapshot/Root Motion/完成与中断 |
| 5 | 复杂场景与预算扩展 | 动态载体相对事实、复杂主动身体行为、需要时的稀疏姿态约束、LOD/睡眠/重新相关及多角色预算；逐项以实际源码对照与运行证据扩展 |

- 失败、取消、死亡/外部接管、重复进入、旧回调、乱序与资源清理属于每一步的完成边界，不推迟到第五步集中补做。静态审查须标明范围；三端稳定性须按后文矩阵运行验证，不能由 diff 检查或编译成功代替。遵守工程默认不编译、不编写测试代码、Content 仅在用户明确范围内处理。
- 第一阶段开始前重新读当前工作区差异和实际源码，保留用户及其他任务的未提交变更；新增 `FIMGRagdollBodyReference` 已经用户明确同意放入现有 `IMGRagdollTypes.h`。本轮未新增模块依赖或更改资产配置。

### 2026-11-27 第一步增量实现与验证边界

- 原缺口为“已有 pelvis 输入和胶囊模拟，没有持续向身体适配输出批准后的参照”。参照 `NetObjPed::GetPosition`/`NetBlenderPed::UpdateRagdollBlending` 的位置事实与消费者分层，新增 `FIMGRagdollBodyReference` 和 CMC.GetRagdollBodyReference；CMC/Mode 不施加身体力，不识别 PhysicsControl/Simulate 类型。
- 参照包含 Session、MovementRevision、Sequence、服务端采样时间、来源 Move 时间戳、认可 pelvis/同帧胶囊位置、跟随速度、平面朝向、正反面与 Issue。它不是全骨骼姿势、pelvis 完整旋转或物理体稳定性样本；Velocity 明确为胶囊跟随速度，不能交给 GA 当身体稳定证明。Sequence=0 是可序列化的空参照。
- Mode 在移动成功且会话/阶段仍匹配后输出参照：`ApprovedPelvis = InputPelvis + ActualCapsule - RequestedCapsule`，保留身体与胶囊高度关系，并将限速/平面约束造成的未达到位移扣回，不把原始未达到目标直接发给表现。Mode 仍使用原有胶囊尺寸/地面查询和通路合同，不照搬 GTA/样例常量。
- 权威参照走独立复制属性及 Packed MoveResponse，不提升生命周期 Revision、不产生第二个胶囊写入者。活动结果按 Move 更新供应答使用；属性以不超过0.05秒、且不超过 MaxTargetAge/2 的发布间隔维持心跳，状态/移动错误变化立即请求复制。实际送达频率仍受 Actor 网络调度和连接影响，不保证固定20Hz到达；没有逐 Tick Reliable RPC。恢复查询被拒或另一个本机来源提交失败不会使仍有效的移动参照失效，避免周期性起身尝试打断连续身体表现。
- 消费方按 MovementRevision/Sequence 拒绝旧参照，允许早于生命周期到达的参照暂存；getter 复核当前 Session/Phase/Mode、注册/活动状态、服务端时钟和 MaxTargetAge 后返回本机世界坐标。开始、恢复、结束会发布对应版本的失效参照；重放期间 getter 不提供实时身体参照。GameState/时钟未就绪时等待，不把迟到样本当新样本重新授时。
- 自主代理普通更新不硬拉身体。CMC.RagdollCapsuleCorrection 原生订阅接点只有原生接受且推进 ACK 的真实纠偏才记录第一个修正前位置，重放结束后同会话输出一次最终净位移；GoodMove ACK、过滤/忽略/过时纠偏不生成该回调。第二步执行器现已原生订阅并精确退订，身体平滑的运行效果仍待验收。该修正不是原始权威身体锚点，不能在消费后再叠加同一次绝对位置纠偏。
- SavedMove.IsImportantMove 现在考虑 Ragdoll Session/状态/目标/朝向/正反面变化，以接入原生重要 Move 重传；仍不合并活动 Ragdoll Move，不重新采样或重放身体冲量。额外传输布局要求客户端与服务端同版。
- 当前打包入口：RagdollInput 已并入现有 FIMGCharacterNetworkMoveData；RagdollState/BodyReference 已并入 FIMGCharacterMoveResponseDataContainer，构造时沿 CMC 现有 SetNetworkMoveDataContainer/SetMoveResponseDataContainer 注册。另有 CMC.ReplicatedRagdollState/ReplicatedRagdollBodyReference 属性给当前状态/参照复制，不是另建移动 RPC。Character 的 FSharedRepMovement/FastSharedReplication 是观察端胶囊更新入口（IMGCharacter.cpp:27/90/742/768，ReplicationGraph.cpp:449 注册），目前包含根位置/旋转/速度/模式/姿态等，不含上述 Ragdoll 状态或身体参照；接收实现只处理 SimulatedProxy，不能用它代替自主代理 MoveResponse。下一步身体组件消费这些合同，不自建第二套胶囊同步；若统一观察端高频身体打包，须另扩展 SharedRep 的填充/比较/序列化/消费，并保留阶段及迟到重建，不能表述成当前所有 Ragdoll 数据已在同一个包内。
- 默认协调组件没有有效模拟 pelvis 时返回 false并清本来源目标，不再以当前胶囊伪造有效身体。服务端处理远端自主玩家时不接受本机第二个身体来源；同角色仍只允许一个目标提供者。ForcePositionUpdate 等 Tick 间模拟也检查远端输入时效。Controller 更换/失效时权威端结束原会话，避免新移动拥有者继承旧身体样本。
- 静态核对范围：新类型的序列化及空值、UE NetQuantize10/Blueprint 属性用例、GameState 服务端时钟、原生 ClientUpdatePositionAfterServerUpdate 的 ACK/重放边界、属性与应答乱序、阶段切换、断流、停用/注销/EndPlay 与控制者变更。未编译、未编写测试代码、未运行游戏，也未调查/修改 Content；三端流畅度、实际带宽和完整身体/起身表现仍须后续接合与运行验收。

## 第二步：身体执行组件与 PhysicsControl 接入（2026-11-27）

用户明确：完整动作周期、等待稳定、起身和结束/中断由 GA 组织，沿 Traversal 的 GA/执行组件分工。PhysicsControl 派生组件只管物理对接。下述组织合同中的身体执行部分已有 C++ 实现，实际入口和当前边界见“Component 已实现合同”；GA/MM 流程仍属规划。

### GTA 已核对的执行拆分

路径相对 `C:/Application/gta5/src/dev_ng/game`。Task 的职责须拆分映射到 GAS、组件及 CMC，不将整个 GTA Task 树搬进一个组件。

| GTA 入口 | 已确认职责 | IMG 落点 |
| --- | --- | --- |
| `event/Events.cpp:5646 CEventSwitch2NM::CreateResponseTask` | 活体创建 NMControl 包裹身体行为；死亡路径交 DyingDead 再携带 NMControl | 伤害/玩法提出反应，Ragdoll GA 或 Death GA 决定准入与所有权 |
| `task/Physics/TaskNM.cpp:5579/5795/6136` 的 NMControl Start/Controlling/Deciding | 持有当前子任务、替换/中止、消费建议下一任务、转入恢复或完成 | GA 管动作顺序；组件承接本机执行资源及批准事实，不自行决定何时起身 |
| `TaskNM.h:1070` 与 `TaskNM.cpp:2036/2130/2627` 的 NMBehaviour | StartBehaviour/ControlBehaviour/FinishConditions、反馈、物理阶段控制、可用时的主动身体输入 | 共用执行入口与后续可选身体行为实现；稳定/最短倒地等业务阈值和动作切换归 GA |
| `TaskNMRelax.cpp:92/121/140`、`TaskNMFallDown.cpp:125/213/358` | Relax、跌落等各有身体消息/持续控制/完成建议，跌落可建议 HighFall | 行为与物理接入分别扩展；简单反应可配置 Profile，复杂接触反馈另写执行算法，不为每种行为派生一份 PhysicsControl 组件 |
| `TaskNM.cpp:5579`、`TaskRageRagdoll.h:48` | NMControl 检查非 NM 模拟并选 RageRagdoll；RageRagdoll 直接派生 FSMClone，非 NMBehaviour 子类 | 普通模拟与主动控制共享上层执行合同，后端能力不同；不假定同一 Profile 即可复现所有 NM 行为 |
| `Peds/Ped.cpp:4769/5178`、`physics/gtaInst.cpp` | 物理实例切换、激活、附着/碰撞、姿势/速度及动画切换 | 基类共用身体接管与恢复资源管理，物理操作交虚函数；CMC 继续只管理胶囊 |
| `TaskNM.cpp:4892/4913/6637` | 序列化任务类型/标志/seed，clone 按 TaskInfo 重建；缺信息有 Relax 后备 | 批准的持久行为事实供本机执行接合，首版移动 Phase 不扩成身体行为 FSM；需要新增行为同步时另建明确的事实合同 |
| `network/Objects/Prediction/NetBlenderPed.cpp:1291` | 预测目标与本机身体误差分开；限反向牵引、分水平/垂直限速、限加速度，施加到关节链，超差整体重定位；另有支撑物分支 | 共用身体收敛算法消费 CMC 参照；派生类只执行具体力/速度/重定位操作，参数须按 UE 单位和运行证据制定 |
| `TaskBlendFromNM.cpp:249` → `task/Movement/TaskGetUp.cpp` | 独立恢复子任务选择/放置/播放并交还控制 | GA 调用选择适配与 CMC 几何，再组织 GAS Montage；身体组件仅提供姿势事实及冻结/混出/释放接口 |
| `TaskNM.cpp:4675 CleanUp` | 按中断、同类 clone 替换和死亡等情况清理，不能无条件关闭所有身体 | GA 结束本次业务；组件精确释放本次资源或交接给 Death/新执行者，旧回调不覆盖新所有者 |

### 类与依赖

- 建议保留 `UIMGRagdollComponent` 为唯一装在角色上的共用执行入口，默认物理操作支持普通 Mesh 模拟；新增 `UIMGRagdollComponent_PhysicsControl : UIMGRagdollComponent` 替换相关物理操作。使用派生类时只装这一份 Ragdoll 组件，GA 通过基类访问；现有 FindRagdollComponent/单协调器保护仍适用。名称使用引擎实际的 PhysicsControl，而非 PhysicalControl；它与 PhysicalAnimationComponent 是不同接入。
- 基类接 CMC Session/Phase/参照与重放后纠偏、装配就绪、身体接管/快照/释放、PrePhysics 执行及 PostPhysics 采样、一次性输入去重与事实输出。基类不保存最短倒地计时、不自动重试起身、不独立选择下一业务动作，也不播放/挑选起身 Montage。
- 派生类负责专用 UPhysicsControlComponent 的解析或自动创建/注册、PCA 预加载、控件/BodyModifier 创建与精确销毁、Profile 映射、目标/强度/碰撞/混合参数写入及后端反馈。默认使用自己管理的独立控制组件，避免清空其他系统的控件；借用已有组件时必须显式指定并记录本方句柄。蓝图只配置 PCA、语义到 Profile 的映射及参数，无需转发 Start/End/纠偏事件。
- PhysicsControl 仍作用于 UE 身体实例，并非一套代替 Chaos 的独立求解器。`PhysicsControlComponentImpl.cpp:794 ApplyBodyModifier` 每次应用时会按 MovementType 写模拟开关、写 PhysicsBlendWeight、形状碰撞及重力；这些属性由当前物理实现唯一写入。基类默认 Mesh 模拟函数须作为可替换操作，不能与派生 BodyModifier 持续争写。
- 共用操作按职责形成受保护的原生虚函数：准备/进入身体、执行身体命令、应用纠偏、读取身体事实、暂停自由模拟并交接恢复、释放/移交资源。基类负责调用顺序和幂等门控，派生类实现插件调用；原生内部调用不依赖 BlueprintAssignable 委托互相转发。
- 后续 `UIMGGameplayAbility_Ragdoll` 管激活/互斥/最短倒地/连续稳定、受击更新、恢复请求/超时、选择/批准/播放、完成与中断。MM/姿势选择归独立动画适配。Relax/Brace/HighFall 等是行为维度，不与 Simulate/PhysicsControl 的接入维度交叉派生；先实现基础倒地，实际需要时再增行为文件。
- PhysicsControl 类型与调用限制在派生头/实现及其配置；基类、CMC、GA 公共合同不包含插件句柄或后端类型判断。工程已显式启用 PhysicsControl，ImagineGame.Build.cs 增加私有模块依赖；共享运行时模块链接该插件，源码职责仍为派生实现→基类→CMC，不能将单向源码依赖误述成可独立卸载插件二进制。

### 执行链与三端职责

1. GA 校验 Avatar/能力/业务准入及执行设施就绪，权威调用现有 StartRagdoll 入口建立 Session。组件从 CMC 已批准状态接合本机执行，记录 Session 与本机 Mesh/资源代次；身体就绪与移动阶段就绪分别校验。
2. 组件保留身体世界姿势、处理 Mesh/胶囊附着和碰撞，调用当前物理实现进入；初速度/首次冲量仅在实际新执行进入时应用一次。身体资源晚到、重建和重新相关从当前认可参照/速度接合，不重放历史冲量。
3. PrePhysics 消费身体命令、纠偏及控制目标；PhysicsControl 自带 PrePhysics Tick，须保证目标准备在其 UpdateTargetCaches/UpdateControls 的合适边界，骨骼动画目标缓存已更新。PostPhysics 等待 Mesh 物理/骨骼任务完成再采样，保留当前 CMC 消费缓存的步序；不能只用一个 PostPhysics Tick 假定控制也已在当帧模拟前执行。
4. 自主代理运行本机身体并向 CMC 提交新样本；服务器对远端玩家验证 Move，自己的可选身体复现只消费结果，不再次提供胶囊输入；本地权威玩家/服务端 AI 为身体来源；模拟代理只消费批准参照并本机物理收敛。Listen Server 本地角色只进入/施加输入一次。专服需要身体来源或命中身体时，显式核对 bEnablePhysicsOnDedicatedServer、骨骼更新与物理 Tick，不按渲染可见性停掉必要事实。
5. 自主代理按 CMC 接受后的重放净纠偏更新身体和后续采样参照；普通 ACK 不牵引回旧绝对锚点，也不按包到达频率硬拉 Mesh。物理位置、CMC 上传参照与视觉平滑的关系必须明确，防止平滑中的旧身体漂移再次被上传并抵消服务器纠偏；不能只对 Mesh 做插值便宣称该问题完成。模拟代理按统一时间消费身体/胶囊参照，不能把当前胶囊与过期身体样本混用；远端 BodyReference 只有平面朝向/正反面，尚不支持完整 pelvis 旋转或全姿态约束。
6. GA 读取有效身体运动/接触事实，按自己的阈值累计稳定，调用选择适配；得到 Montage/StartTime/Rate/入场匹配依据后，请求服务端批准并由 CMC 校验恢复落位。组件不因看到低速度而自行进入 Recovering。
7. GA 组织批准后的起身；执行组件在本机恢复条件满足时保留姿势、暂停自由物理/牵引、提供动画交接并释放本方物理资源，动画适配处理 PoseSnapshot/混出。观察端由复制状态及 Montage 前置条件执行，无需启动一份业务 GA。完成/取消由 GA 调用精确结束；资源失效时组件先退休本次输入/回放/身体写入并报告，再兜底释放，不能只等待 GA 一定会回调。

### 实施顺序与持续边界

- 先把基类补成可调用的本机身体执行器：内部原生接点、就绪/资源身份、普通模拟、阶段接合与清理；再加 PhysicsControl 派生实现。`CreateControlsAndBodyModifiersFromPhysicsControlAsset` 成功后还须确认必需身体/集合/Profile 可用；该函数仅对基础创建失败整链返回，额外具名控件创建可部分失败，不能只凭 true 证明配置完整。
- 基类保留私有 Initialize/TearDown/ReconcileState 调度，物理操作已建立 protected 原生虚函数；不靠公开广播在蓝图反复转发。Started 现在等待实际身体进入成功（配置跳过远端专服身体者除外）；同会话身体资源重建独立重试，不能把一次通知当作永久物理就绪。
- 复用 CharacterPartsReady/OnCharacterPartsChanged 并核对实际 Mesh、Skeleton、PA 和 pelvis BodyInstance；`Cosmetics/IMGPawnComponent_CharacterParts.cpp:335 BroadcastChanged` 会先 SetSkeletalMesh/SetPhysicsAsset 再广播。Mesh/PA 被替换或组件停用/注销时先退休旧资源，旧句柄不作用于新物理实例；重入和恢复是否继续由当前合同/GA决定。
- PCA 应在就绪阶段预加载；引擎创建函数内部 LoadSynchronous 不应成为自主代理进入 Ragdoll 那帧的读盘路径。配置缺失/创建失败明确报告，不静默执行半套 Profile；是否允许普通模拟降级归上层配置与准入。
- 然后落实自主代理纠偏与观察端整体牵引、断流/超距/阻挡降级，并联查后续样本。最后验证恢复交接接口和死亡/再受击/移除组件的释放，再实施 GA/MM 完整周期。小误差柔性收敛、超大误差安全重定位；优先验收自主代理连续性，同时保留权威位置约束。
- 新增物理接入不要求 CMC 同步实现类名/控件句柄。影响动作一致性的行为键/配置版本、必要决定性反馈与唯一冲量事件须有明确生产者/复制路径/迟到重建，不能仅调用服务端组件函数就认为所有端收到；这些扩展按实际消费者单独设计，当前第一步字段不包含完整行为/冲量/姿态同步。

### Component 已实现合同

- 文件：IMGRagdollComponent.h/.cpp 管入口/观察/会话接合，IMGRagdollComponent_Body.cpp 管共用身体执行与纠偏，IMGRagdollComponent_PhysicsControl.h/.cpp 管插件接入。没有新增业务阶段、GA、MM 或网络 RPC；没有新增 struct，配置直接暴露在组件分类中。
- 装配只选一份基类或 PhysicsControl 派生组件。Body 分类暴露 BodyCollisionProfile、MaxInitialVelocityAge、专服身体开关、专服物理启用及 MOVE_None 时的身体保留；Correction 分类暴露拥有者平滑时间/最大修正速度、整体重定位阈值、远端收敛时间/速度/反向牵引/加速度上限。IsRagdollReady 检查执行设施与有限参数，IsRagdollBodyActive(Session) 表达当前本机自由身体有效；BodyIssue/OnRagdollBodyUnavailable 提供本机失败事实，不代替业务取消。
- 两阶段更新复用引擎 FActorComponentTickFunction：BodyPrePhysicsTick 依赖 CMC 与 Mesh 主 Tick，Primary PostPhysics 采样；PhysicsControl 引擎主 Tick 在本方持有控件时关闭，由基类 PrePhysics 调 UpdateTargetCaches/UpdateControls，避免双更新。开启插件身体后调用 Mesh 的混合权重 API 更新 EndPhysics 注册，不能只写 BodyInstance 而遗漏骨骼物理混合。
- 原生虚函数为 CanExecuteBody/StartBodySimulation/IsBodySimulationValid/UpdateBodySimulation/StopBodySimulation/ReleaseBodyResources/TranslateBody/AccelerateBody。基类默认普通 Mesh 模拟；派生类唯一写当前插件控制/BodyModifier 参数，不与默认模拟开关争写。基类保持骨骼世界参照语义，TranslateBody 整体移动全部刚体，保留关节相对关系。
- 进入记录当前 Mesh/SkelMesh/PA、物理重建通知、附着/相对变换、碰撞、Tick/可见性/URO/暂停与混合状态，以及各身体模拟、混合、运动学更新和形状碰撞值；退出按当前身份恢复。Mesh/PA 替换不把旧身体值写入新物理实例；当前组件仍观察 CharacterParts 的变化，活动期间 Tick 也读取实际组件/资产，覆盖组件晚装配。新会话重入会保留同一 Mesh/PA 的当前刚体世界姿势再进入，初速度每次新 Session 仅初始化一次；同会话资源重建使用当前参照/速度，不重放首次发射。
- 初始 Mesh 可为 NoCollision/无运行时刚体：记录原状态后先应用身体 Profile，再核对或重建 pelvis 刚体，失败走同一释放回滚。CMC 开始时在既有 FIMGRagdollRepState 写 StartServerTime，沿原状态/应答序列化；本机只在首入且服务端时钟确认启动年龄不超过 MaxInitialVelocityAge 时使用 InitialVelocity，迟到/资源重建优先用新鲜认可速度，否则用当前移动速度。没有新网络通道，但此布局扩展要求各端同版。
- PhysicsControl 分类暴露硬引用 ControlAsset、可选 ControlComponent、默认 true 的 bAutoCreateControlComponent、RagdollProfile、SimulatedBodyModifierSet（All）、DisabledControlSet（WorldSpace）、RequiredControlSets/RequiredBodyModifierSets。硬引用随组件类加载，创建路径不以软引用同步读盘取得 PCA。RagdollProfile=None 使用 PCA 初始配置，之后强制所选 modifier 集合模拟并使用完整混合，关闭所选世界控制集合；自由倒地默认避免被世界目标钉住。
- 控制组件显式指定时须同角色、已注册且空闲；未指定时复用唯一空闲组件，多候选或无候选时可创建专用本机 transient 实例。非空组件不清空、不接管。只删除本方记录的 controls/modifiers，按本次接管的 PCA 关联恢复借用者原 PCA/Tick；自建空闲实例在执行器停用/注销/EndPlay 时移除并销毁，普通会话之间保留空闲实例复用。若外部向该实例新增本方以外的资源，清理不销毁其组件，交 Actor/接管者管理。自建者不复制，三端各自解析/创建物理资源。
- 创建后检查所需集合、额外具名控件/修饰器、Profile 及 pelvis 实际模拟。配置失败不静默降为半套普通模拟。普通模拟和 PCA 配置仍依赖有效 Skeleton/PA/骨架轴与可用物理状态，本次未检查 Content 或具体 PCA 内容。
- 正常等待身体资源/权威参照只保留 BodyIssue，不触发失败广播。实际创建失败先记录当前 Session/Mesh/SkelMesh/PA，再回滚并报告，避免未释放时通知业务；相同身份的失败不会每 Tick 重建/刷警告，GA 或调用方修正配置后可显式 RetryBodyExecution(Session)，新会话/实际 Mesh 或资产变化也可再试。本机资源重试不新增业务计时器。
- 自主代理消费 CMC 重放后的净纠偏，累计 OwnerCorrectionRemaining；小误差按时间和速度上限整体平移身体，大误差一次整体重定位。上传参照为物理 pelvis + 剩余纠偏，身体每消费一步便扣回该步，历史 SavedMove 不改；回调立即刷新实时采样以覆盖同 Tick 的下一 Move。GetBodyMovementOffset 提供给未来选片/稳定判定统一移动参照，普通有效 ACK 只更新认可身体偏移，不拉回旧绝对位置。
- 目标超距/通路被拒时，即使两端胶囊都停住而没有位置纠偏，也在该拒绝首次出现时把采样参照约束到当前胶囊与最后认可身体偏移，并用移动速度重置身体线速度，防止身体持续飞离导致永久拒绝。Missing/Expired 参照不会成为新的远端牵引目标。
- 模拟代理与可选服务端复现使用 CurrentCapsule + (ReferencePelvis - ReferenceCapsule)，保持同一胶囊时间基准，再按位置/速度误差、平面/竖直和反向约束向整个身体施加有界加速度；新鲜参照失效即停止牵引。超大误差整体重定位不是完整身体净空或深穿透脱困证明，接触/动态载体/稀疏姿态约束仍按后续范围处理。
- 专服远端玩家默认不创建身体模拟，只验证输入/消费会话；AI/本地权威仍为身体来源。可开启远端专服身体作为消费者。需要专服物理且配置允许时在进入前设置 bEnablePhysicsOnDedicatedServer 并重建物理状态，正常释放恢复原值；本机刚体准备失败不得输出伪稳定事实。
- Recovering 按 CMC 恢复就绪接合：捕获 FPoseSnapshot/当时 Mesh 世界变换，停止本方控制和自由模拟，暂停 Mesh 更新以保留当前显示姿势。动画适配先 GetRecoveryPose 安装姿势，再 ReleaseRecoveryHold(Session) 恢复附着/原状态及动画更新，然后由 GA 播放批准 Montage。迟到观察端可能没有物理姿势快照，动画适配须有后备；组件不会自行选片、播放或宣布恢复完成。
- PostPhysics 显式依赖本组件 PrePhysics Tick；Mesh 初次进入前若允许动画晚结束，首帧执行可随其依赖延后，身体启用后引擎按物理需求把动画结束约束回 PrePhysics。恢复保持期间按捕获的 Mesh 世界变换消化 Walking 原生平滑的相对变换写入；释放时考虑 Character 当前基础 Mesh 平移变化，避免爬行/蹲姿偏移已更新却覆盖回旧值。外部重新附着会退休本方身体写入并阻止旧会话抢回资源；明确移交仍应先调用 HandOffRagdollBody。
- HandOffRagdollBody(Session) 是本机明确移交入口，退休采样且不恢复身体/附着，原会话不会自动重新抢回身体。MOVE_None 时默认保留自由身体，恢复冻结阶段也可交成自由身体；Death/外部接管的业务仍须 GA 各端组织。停用、注销、EndPlay、CMC 失效先退出本次资源和绑定再报告，不要求一个业务回调保证最终释放。

### 外部恢复入口与空中接管合同（通用执行基底已接合）

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
- 当前边界：已提供通用外部移交/姿势执行与网络接合接口；没有实现 Ragdoll GA、滑翔/飞行 GA、物种主动行为或 MM/姿势混合 AnimNode。GAS 动作事实、蒙太奇前置门控及实际视觉衔接仍由后续对应执行者落地，不能将接口齐全视为完整滑翔玩法或三端表现验收完成。
- MOVE_None/执行设施失效会退休接收保持并通知不可执行，不保留永久冻结身体；死亡保留物理遵循既有 bKeepBodyWhenMovementDisabled 策略。正常释放须等组件已准备接合，取消可提前丢弃；旧结束回调中取消接收者后，不再补发该令牌的就绪事件。

### 非人形物种复用边界

- 当前执行器不是任意 Actor/Pawn 通用组件：Initialize 明确要求 ACharacter 与 UIMGCharacterMovementComponent，身体固定取 Character.GetMesh；普通物理、PhysicsControl 创建/清理及整体纠偏算法不检查人类类型。
- PelvisBone、OrientationBone、FaceUpAxis 是可配置骨架绑定，pelvis/spine_05/Y 只是人形默认值。PelvisBone 实际承担有有效模拟刚体的身体位置参照，可绑定四足/鸟的相应骨盆或躯干骨；朝向默认由参照骨到 OrientationBone 的向量投影计算，特殊骨架可覆写采样，须保留拥有者 GetBodyMovementOffset 合同。各物种分别配置 PA/PCA、约束/质量/控制集合、碰撞和跟随/纠偏参数。
- 物理倒地、采样、联网跟随和生命周期可复用，不等于完整恢复流程已适配。当前 Mode 使用直立且沿重力对齐的 Character 胶囊；地面起身查询和 CMC.IsRagdollRecoveryReady 保持 Walking 合同，外部动作改走上述通用移交。四足长躯干、鸟展开翼的净空不能由该胶囊证明，水平碰撞体/多碰撞体不属于当前实现。
- 鸟的物理坠落/落地可按上述合同配置接入；空中退出 Ragdoll 并承接 Flying 已有通用移动/姿势移交接口，仍需物种 GA 准入与动画实现。不能在 PhysicsControl 派生类直接改移动模式，也不能把通用 Flying 等同于已实现鸟类飞行控制。
- 四足翻身/四肢支撑起立、鸟收翼/展翼/重返飞行等动作属于物种 GA、选择/动画及需要时的主动身体行为。bFaceUp 是单一正反面事实，不足以完整分类动物侧卧/背卧/腹卧和支撑姿态；MM 可读取完整本机姿势，新增决定性分类/网络事实时再按实际消费者扩展。未针对动物资产或运行场景验收，不宣称全部物种即插即用。

## Ragdoll 移动基底

### 分层与入口

- 源码位于 `Source/ImagineGame/{Public,Private}/Character/Ragdoll`，包含 IMGRagdollComponent 及 Body 实现、PhysicsControl 派生组件、IMGCustomMovementMode_Ragdoll、IMGRagdollTypes，属于 ImagineGame。
- 依赖方向：GA → 可选身体执行组件 → CMC，由 CMC 分派自身 Mode。Component 管请求、本机身体资源/纠偏、采样、事件和绑定清理；CMC 管单一移动快照、装配及网络；Mode 管胶囊跟随、地面抬升、限制、碰撞/朝向接管和恢复落位。AIMGCharacter 不默认持有组件，CMC/Mode 不查找或引用它。
- 胶囊不模拟物理。身体执行组件操作 Mesh 物理、碰撞、附着及只读姿势快照，不调用 MM/ASC 或播放 Montage；动画适配消费快照，GA 组织完整周期。PhysicsControl 依赖只在派生实现，移动源码不包含插件类型或行为业务判断。
- 权威入口：`StartRagdoll` → `BeginRecovery(Session)` → `CompleteRecovery(Session)`。选片可先 `QueryRecovery(Session, Transform, Issue, BodyLocalOffset)`，再 `BeginRecoveryAt(Session, Transform, BodyLocalOffset)`；调用者显式完成/中断，无自动起身计时器。
- 无协调组件时直接使用 CMC.StartRagdollMovement、RecoverRagdollMovement/RecoverRagdollMovementAt、CompleteRagdollRecovery、StopRagdollMovement。持续 SubmitRagdollFollowTarget 提交世界空间 Transform、正反面、有效 InputSource、原 Session；Submit/Clear 均要求 Session，同一来源未 Clear 前其他来源不能覆盖。
- 模式选择与会话启动不同：`EIMGCustomMovementMode` 定义在 `IMGCharacterMovementComponent.h`，当前 None=0、Ragdoll=1；底层切换为 `SetMovementMode(MOVE_Custom, static_cast<uint8>(EIMGCustomMovementMode::Ragdoll))`，蓝图对应 Set Movement Mode 的 Custom 与 New Custom Mode=1。实际启用 Ragdoll 应由权威端调用上述 Start 入口，先初始化并发布 Session/Phase/Settings，再切模式；只切编号会执行 Enter，但 Simulate 缺少 Simulating 阶段及匹配的有效输入时清速度并报告问题。执行分派见 CMC.PhysCustom，进入/退出见 CMC.OnMovementModeChanged；新增枚举值不会自动注册新的模式实现。
- OnRagdollStarted、OnRecoveryStarted、OnRagdollEnded 携带 Session，CMC 提供 OnRagdollMovementUpdated。Phase/Session/Revision 只承担移动配置、跟随会话与恢复输入锁，不承担动画或 GA 业务状态机。

### 身体参照与跟随

- 默认位置为已模拟 `pelvis` 的骨骼世界原点，按骨骼索引读取；不是 Mesh 原点、胶囊中心或刚体质心。`spine_05` 辅助平面朝向与正反面，FaceUpAxis 为该骨骼胸口向外的局部轴，默认 Y；缺失朝向骨骼不停止 pelvis 跟随，朝向回退角色朝向、正反面默认 false。
- 无有效骨骼变换或模拟 pelvis 时返回 false，不再提交当前胶囊作为身体目标。可覆写 GetRagdollFollowTarget，返回 false 清本组件输入。PelvisBone、OrientationBone、FaceUpAxis 由组件复制，骨骼名与轴须按资产配置。
- 执行器在 PrePhysics 驱动身体、PostPhysics 在缓存 Mesh 后采样；UE EndPhysics 包含骨骼物理混合及其并行完成任务，常规 PrePhysics CMC 下一次移动消费缓存，存在正常采样延迟。默认读最终混合姿势，权重小于1时不等于纯物理体变换；拥有者存在平滑纠偏时输入位置还包含 GetBodyMovementOffset，覆写采样必须遵守该移动参照合同。
- 击飞链：权威 Start → 本机身体就绪/启用模拟并初始化速度一次 → Started 事件 → 采样 → CMC 输入 → Mode 跟随。AdditionalVelocity 合入 InitialVelocity，由本机执行器在新会话初始化时应用；单独 CMC.Start 且没有执行器仍不会替 Mesh 施加速度/模拟重力。后续独立受击冲量的事件去重与复制仍未实现。无 Controller 遵循 bRunPhysicsWithNoController。
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
- 尚待独立实施：GA 业务、MM/动画适配、进入预测与拒绝回滚、后续受击事件同步、速度/关节/姿态异常约束、深穿透脱困、载体相对事实/恢复锚点及跨角色预算/降级。PhysicsControl 启停和基础身体纠偏已有 C++ 接合，运行效果仍待验证。GTA 的 FixStuckInGeometry、UpdateRagdollBlending、OnMovingGround、池化/LOD 按相应职责参考，不能据此断言 Chaos/资产没有同类能力。
- GetRagdollMovementIssue 是本机诊断。`IMG.Ragdoll.DebugDraw`：0关、1跟随/失败目标、2增加候选胶囊/支撑/两段通路；青点为采样点，黄点为匹配点。`IMG.Ragdoll.DebugDuration` 默认2秒，仅影响显式查询，跟随单帧；绘图受 ENABLE_DRAW_DEBUG 保护，CVar 为 Cheat并默认关闭。
- `Log LogIMG Verbose` 查看 [Ragdoll] 状态、原因及 ExitRejected，按角色/Role/Session/Revision 对照，结束用 `Log LogIMG Log` 恢复。

### GTA 联网 Ragdoll 对照（2026-11-26 源码核对）

- **范围与权威。** 以下为本地 GTA 快照的 `TaskNM`、`TaskRageRagdoll`、`TaskGetUp`、Ped/Player SyncNode、`NetObjPed/Player`、`NetBlenderPed` 和 Ragdoll Request 主链静态事实，不代表当前发行版全部配置。这里的 master/owner 是拥有网络对象控制权的 peer；不能把它直接等同 UE dedicated server。`NetObjPlayer.cpp::CanPassControl/ChangeOwner` 明确玩家对象不迁移；普通 Ped 有 `CanAcceptControl` 及任务 local/clone 迁移路径，Ragdoll 与任务状态不一致时拒绝接管（`NetObjPed.cpp:2696` 起；`TaskNM.cpp:5092/5146`）。事件或伤害是否有效还须沿各自裁决链检查，不能从位置所有权推断所有玩法权威。
- **身体位置本身是同步事实。** `CNetObjPed::GetPosition`（`NetObjPed.cpp:3080`）在 Ragdoll 时取 `RAGDOLL_BUTTOCKS` 的世界矩阵位置；`CPed::UpdateEntityFromPhysics`（`Ped.cpp:3760`）从物理更新实体并同步骨骼。`CPedSectorPosMapNode::Serialise`（`PedSyncNodes.cpp:466`）额外序列化 Ragdoll 标志、承载对象 ID 和局部偏移。不能概括为“身体随便模拟，只同步站立胶囊”。
- **同步行为而非逐骨姿势流。** `CClonedNMControlInfo::Serialise`（`TaskNM.h:1599`）发送随机种子、NM 子任务类型、有限控制标志及 HasFallen；`TaskNM.cpp::ReadQueriableState` 在 owner 已失衡而 clone 未失衡时设置 FORCE_FALL_OVER，`CreateNewNMTaskClone` 从任务信息创建本机行为。`CClonedNMShotInfo`（`TaskNMShot.h:36`）另带攻击者、武器、命中部位、局部命中点、世界冲量/法线；`CClonedNMExposionInfo`（`TaskNMExplosion.h:35`）带爆炸位置。已查主链未见每帧全骨骼变换数组；随机种子减少差异但不证明物理确定性。
- **远端本机物理加整体收敛。** `CNetBlenderPed::UpdateRagdollBlending`（`NetBlenderPed.cpp:1291`）比较本机身体位置与网络目标，计算所需速度/加速度，将纠偏加速度限幅后通过 `ApplyGravityToLinks` 作用到关节链。空中分支限制目标速度差和每步速度变化，反向拉回有限速；部分行为/两端 Ragdoll 状态不一致时关闭 Z 纠偏，不直接按普通站立 heading blender 转身体。大偏差进入专门的 warp/模式切换处理，仍有状态和碰撞约束；不是无条件每帧 SetActorLocation。`Update` 在 Ped 已 Dead 或关闭 blending 时提前退出，不能把活体收敛合同直接推广到所有尸体。
- **预测的准确边界。** 这条路径有本机继续物理、网络速度估计、空中约束及载体位置预测，但本轮未发现类似 CMC SavedMove 的全身体历史回滚/输入重模拟链。特别是 `CNetBlenderPed::GetCurrentPredictedPosition`（`:1573`）非载体分支只对 `!GetUsingRagdoll()` 做一般位置速度外推；普通 Ragdoll 以当前位置快照为目标，不能因函数含 Predicted 就说它统一按速度外推身体目标。`TaskNM.cpp::UpdateClonedFSM` 允许短时本地受击任务等待 owner 确认，未获对应状态时有约 1 秒退出约束；`CRagdollRequestEvent::Decide`（`NetworkEventTypes.cpp:9075`）只在目标为本地拥有、非迁移、活体且准入允许时处理请求，不意味着 clone 自动获得权威。
- **起身同步阶段/候选集合，按本机姿势适配。** `TaskGetUp.cpp::CreateQueriableState/ReadQueriableState`（`:572/587`）交换任务状态、目标及 BlendOutSet 列表，并将标准/玩家 MP 集合映射到专用 clone 集合。`UpdateClonedFSM`（`:737`）等待 owner 选定集合后流式加载；`ChooseGetupSet_OnUpdate` 中约 `:1583` 的 PickClipUsingPointCloud 仍匹配本机姿势、复核落位。`Finish_OnUpdate`（`:3491`）禁止 clone 在 owner 的起身任务仍存在时自行完成。故表现细节可不同，但不是完全不管开始、阶段和结束，也不能断言所有角色强制播放同一个 clip/phase。
- **移动载体和异常不属附带小功能。** `NetObjPed.cpp::GetStandingOnObjectData`（`:7779`）补探测 Ragdoll 的承载物并传相对位置；Blender 重建目标、检查支撑，延时后可经 NMControl warp 并建立物理 attachment。`TaskNM.cpp::OnMovingGround`（`:2754`）另维护接触参照速度；`TaskRageRagdoll.cpp:265` 起在 MP 中衰减关节刚度/阻尼并修正不稳定姿势。联网、行为驱动、碰撞、恢复、失效补偿、资源池/LOD 需要合看。
- **IMG 当前差距。** `MoveAutonomous` 消费拥有者上传的 PelvisLocation/朝向/正反面；服务端重算胶囊跟随并检查会话、有限数值、目标距离、通路及速度限制。它是“客户端身体目标作为输入、服务端约束胶囊”的合同，不是服务端独立重模拟并证明身体动力学。SavedMove 重放缓存目标，不回滚 Chaos 身体；模拟代理不从本机身体反推胶囊。2026-11-27 已增加批准身体参照、拥有者身体平滑参照和远端整体牵引的 C++ 执行代码；身体姿态复制、复杂行为及起身 clip/phase 同步仍未实现，未编译/运行、Content/蓝图适配未检查；不可宣称 GTA 式网络 Ragdoll 已完整验收。
- **UE 可复现边界与拟议方向。** UE 5.8 `AActor::GatherCurrentMovement/PostNetReceivePhysicState`（`Engine/Private/ActorReplication.cpp:426/304`）的默认物理移动复制围绕 RootPrimitive 单个刚体状态，并不自动序列化 Character 子 Mesh 的全关节姿势；CMC 也不会替游戏回滚身体。Network Physics 的 Predictive Interpolation/Resimulation 提供另一套物理网络机制（`Engine/Public/Physics/NetworkPhysicsComponent.h`），与现有 CMC 身体目标历史须明确接合，不能靠勾复制开关视为完成。主责身体执行器现已消费权威身体参照并做基础整体纠偏；恢复适配还需同步所需动画表现参数。需要强身体碰撞/命中权威的玩法，应另确定服务端身体或简化碰撞事实，不能将客户端 pelvis 合法范围校验当成完整反作弊。身体运行效果与扩展方向仍待验证。

### GASP 单机参照与身体适配边界（2026-11-26 核对）

- 用户明确：权威/联网组织以 GTA 为主参考，GASP 没有联网，本文仅将其作为单机身体、跟随几何和动画实例；读取到 Mover InputCollection 不构成联网验证，不迁入 Mover 网络架构。
- 已从本地 `D:/Application/UnrealProjects/GameAnimationSanple` 使用 UE 5.8 Python commandlet 只读加载并经 ObjectExporterT3D 导出 `/Game/Blueprints/SandboxCharacter_Mover_Ragdoll` 和 `/Game/Blueprints/MovementModes/BP_MovementMode_Ragdoll`，沿节点 LinkedTo 核对，未调用保存资产。临时证据在 `Saved/Codex/RagdollResearch`；长期入口以资产路径和函数名为准，不依赖临时导出继续存在。
- `SandboxCharacter_Mover_Ragdoll.Get_RagdollTransform` 确实读 `pelvis` 的 RTS_World 骨骼位置；另读 `spine_05`，以 spine-pelvis 和胸部 RightVector.Z 的正负整理水平朝向，通过 MakeRotFromZX 保持世界Z朝上。`ProduceInput` 在 Ragdoll 模式分支调用它，把 RagdollTransform 写入 `S_MoverCustomInputs_Ragdoll` 并加入 InputCmd.InputCollection，朝向另写入 CharacterDefaultInputs.OrientationIntent。它把身体采样与移动消费分开，不在 Mode 内直接逐步读取实时身体姿势。
- `BP_MovementMode_Ragdoll.SimulationTick` 从输入读取上述结构，先采用身体变换，再从身体位置向世界Z下方88 cm做 Radius=30 cm 的 SphereTraceSingle。命中时保持 XY，按 `BodyZ + 86 - Abs(ImpactPointZ - TraceStartZ) + 1.9` 修改目标Z；通常地面在身体下方时等价于 `ImpactPointZ+87.9`。未命中保留身体目标。它使用样例硬编码尺寸，并非适用于任意胶囊的参数合同；IMG 应保持当前实际缩放胶囊半高/重力方向等消费规则，不能原样搬常量。GASP 公式也不与 IMG 的“只作必要抬升”公式完全相同。
- 随后以 TargetLocation-StartSyncLocation 计算 MoveDelta，调用 `TrySafeMoveAndSlideUpdatedComponentNoMovementRecord`，该节点明确 `bSweep=false`；朝向来自 OrientationIntent。输出位置/朝向从 UpdatedComponent 读取，速度按 MoveDelta/StepSeconds 填入 Mover SyncState。故“身体参照→地面高度修正→胶囊移动”属实，截图只能辅助观察，不能单凭静态图证明调用链或网络能力。
- Epic 5.8 官方 GASP 更新说明 `https://www.unrealengine.com/tech-blog/download-the-latest-game-animation-sample-project-now-updated-for-ue-5-8` 确认新 Pawn 通过 Physics Control 的关节马达做 powered ragdoll，涵盖保护落地、受伤、挥动、滚动及 motion-matched getup。它是主动身体与选片参照，不证明跨端物理一致性。
- 身体实现可拆装：普通 Mesh 刚体模拟可作被动身体提供者，PhysicsControl 可在同一批 Chaos 刚体上增加关节驱动、Profile 和 BodyModifier，并非必然是与 Simulate Physics 完全分离的另一套求解器。引擎 `PhysicsControlComponentImpl.cpp::ApplyBodyModifier`（约794行）按 Static/Kinematic/Simulated 调 `FBodyInstance::SetInstanceSimulatePhysics`，并设置 PhysicsBlendWeight、碰撞及重力。直接 Mesh 模拟开关与持续 BodyModifier 更新会争写同一状态，必须只有一个身体适配所有者负责接管/释放；切换时保留姿势/速度并退休旧控制资源，不叠加两个独立驱动者。
- “最终只影响 pelvis”仅能用于描述移动层的消费接口：身体模拟实际影响全部关节、碰撞、角动量、稳定性、正反面和起身姿势；pelvis 是低维移动锚点，不足以重建完整身体。公共适配需覆盖可用性、开始/退出、施加事件、采样身体参照/运动、消费权威纠偏和恢复交接。当前 GetRagdollFollowTarget/SubmitRagdollFollowTarget 已有目标接点，但不代表完整身体适配已经存在。
- AddMovementInput 的类比在“来源提交→逐步保存→移动层消费”上成立；实际 AddMovementInput 是运动意图，上传 pelvis 是身体模拟结果作为移动输入。建议沿 GTA 分层适配现有 UE 合同：玩家 Autonomous 主导本机身体轨迹，服务端验证输入后重算胶囊跟随并发布批准身体参照，Simulated 消费胶囊与身体参照并运行本机表现收敛。服务端由同一采样复现的是胶囊运动，不是完整身体物理；无需先选定 PhysicsControl 才能实现通用移动层，也不能宣称单靠 pelvis 已支持全身体网络重模拟。

### 随机种子、最终姿势与 MotionMatch 一致性（设计边界）

- GTA `TaskNM.cpp:6060` 在 clone 切入 Ragdoll 前设置 owner 的随机种子，但这不证明全身体结果确定。UE 中 seed 只约束实际使用该种子的行为随机选择，不能自动让 Mesh Simulate/PhysicsControl 的全部身体接触一致；没有随机行为时无需为了物理同步虚设 seed。若使用 seed，按会话/事件稳定派生并隔离随机流，避免各端按渲染帧数消耗不同数量的随机数。
- 只同步 pelvis 位置只能约束位置，不能保证仰卧/俯卧、胸腔方向或四肢终姿。近似停姿需要身体旋转/胸腔或有限关键部位参照、正反面、速度和行为状态配合本机有界姿态纠偏；接触环境、时间步和历史不同仍可产生差异。远端纯位置收敛不得宣称已保证相同 MotionMatch 查询。
- UE `PoseSearchLibrary.h` 的 MotionMatch 消费 PoseHistory、搜索集合及 Continuing/Future 等查询条件；各端独立本机姿势/历史不同，即使 seed、pelvis 和数据库相同也不保证选出同一帧。若强求独立搜索一致，必须统一全部实际参与搜索的查询特征、候选/过滤、持续状态和资产/算法版本，而不是只同步一个 seed；这不是当前首版合同。
- 首版把“身体近似一致”与“恢复选择权威一致”分开：玩家拥有者对真实本机停姿做 MM，发送带 Session/恢复请求/采样关联的 Montage、逻辑 StartTime、Rate 和匹配依据；服务端校验并批准一个结果，所有执行端消费该结果。AI 由服务端选择。服务端不因自己的近似身体姿势独立 MM 得到不同结果就自动替换合法客户端候选；若需独立验证匹配质量，应另统一可信查询事实。
- 选择批准前复核姿势/样本是否过期；批准后固定该恢复实例，不让各端持续 MM 各自换片。逻辑 StartTime 与当前网络播放进度分责，实际到达时间可不同；原生 GAS/CMC 及本机接合修正对齐。以后若允许自主代理提前预测，批准结果及实例匹配则沿用，不同或拒绝时显式纠正/重选，不能承诺从未发生预测分歧。
- 各端用自己的 PoseSnapshot 混入同一批准动画；较大正反面/躯干差异不能只靠长混合掩盖，需先做受控身体姿态收敛或明确恢复表现降级，不额外反推胶囊。因而可设计保证“批准后的 Montage/逻辑入点/倍率/阶段一致”，不能保证“所有端自然停姿逐骨一致”。以上尚需三端运行验收。

## PA / PhysicsControl 资产基线

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

### 编辑器工具边界

- 本轮会话 MCP 端点为 ImagineGame `http://127.0.0.1:8001/mcp`、GameAnimationSanple `http://127.0.0.1:8002/mcp`。端口/编辑器状态会变，操作前先验证工程身份。HTTP需先 initialize并保存 `Mcp-Session-Id`，再通知 initialized；不带session直接调用tools会400。内置客户端配置曾指向8000，不能据配置文件认定实时端口。
- 内置 ObjectTools 不提供 PA 刚体数组和非反射 `CollisionDisableTable` 的完整编辑。`ObjectExporterT3D` 可导出刚体/约束结构，但不能代表完整碰撞禁用表；整资产复制保留该表，再逐项适配。`PhysicsAssetEditorSharedData.cpp::CopySelectedBodiesAndConstraintsToClipboard/PasteBodiesAndConstraintsFromClipboard` 会按骨名输出/恢复 `PhysicsAssetCollisionPair`；粘贴先启用所有粘贴体之间碰撞，再按文本禁用，但不会自动删除目标多余约束，不能只粘贴一次就认为拓扑一致。
- Python读取未生成专用包装的内嵌UObject时须用原始反射名，如 `load_object(None, pa_path+':SkeletalBodySetup_18').get_editor_property('DefaultInstance')`；snake_case 可能找不到字段。PA的 `SkeletalBodySetups/ConstraintSetup/PreviewSkeletalMesh` 是受保护字段，不应假定通用getter可读。此次预览Mesh经原生反射 `get_default_object(SystemLibrary).call_method('SetSoftObjectPropertyByName', args=(pa, Name('PreviewSkeletalMesh'), mesh))` 设置；Value传加载的Mesh对象，不能传SoftObjectPath包装。修改后须用完整导出和最终依赖回读确认。
- 已加载uasset可能仍被UE文件读取器占用；仅关闭资产编辑器不保证释放。此次目标无referencer且无dirty修改，备份后关闭编辑器、释放Python资产引用并 `EditorLoadingAndSavingUtils.unload_packages`，再替换同名包、加载并保存。任何后续迁移须重新确认引用/dirty状态及备份，不能直接覆盖已加载或有未保存修改的文件。会话内的 CodexPhysicsParityTools 是此次任务临时Python Toolset，不是工程插件或通用运行时接口，重启即消失；不依赖开启Python远程执行。

## Ragdoll GA 接入规划（尚未实现）

### 会话、激活与身体事实

- 一个派生 `UIMGGameplayAbility` 的 InstancedPerActor GA 持有“进入 → 等待稳定 → 选起身 → 恢复执行 → 完成/取消”。首版 ServerInitiated，客户端请求复用 GAS 能力/事件/TargetData；未来 LocalPredicted 须成对实现拒绝/回滚。业务步骤只作本地流程，不复制第二套移动 Phase/Session，不以 GAS 预测键替代 Revision。
- GA 调用身体执行组件/CMC，选择适配调用 MM，PhysicsControl 派生通过原生虚函数接入同 Session。普通移动 Tag 留在 GAS/玩法配置；通用合同可放共享层，伤害、车辆/水域/平台、爬行和救援规则归具体玩法。
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

## 三端联网、身体表现与恢复扩展清单（2026-11-26；基础实施状态见前文）

本节细化上面的 GA 规划。已核对事实以源码入口为准；下列新字段、流程和验收项是拟议合同，不代表当前代码已有。先完成 Authority/Autonomous/Simulated 三种角色的进入、物理移动、收敛、起身和退出，再扩展更复杂的主动身体行为。UE 基建可承载同类架构，不以静态调查承诺 GTA 全部行为/物理结果完全相同。

### 锚点、职责和默认权威路线

- GTA 是臀部 `RAGDOLL_BUTTOCKS`，不是臂部。UE 使用 pelvis 世界位置作身体参照合理；当前默认采样是模拟 pelvis 的最终骨骼世界原点，不是刚体质心。做网络纠偏时若使用 BodyInstance 刚体姿态，必须显式转换刚体与骨骼参照偏移，不混用两个原点。胶囊保持直立，yaw 与胸口正反面按骨架配置计算，不直接继承 pelvis 的 pitch/roll。
- 近地面胶囊会抬高，因此 pelvis 与胶囊中心不是同一坐标。复制的身体参照必须能与同一时刻的胶囊位置建立对应；不能用站立半高或 Mesh.RelativeLocation 猜 pelvis。胶囊仍沿唯一 CMC 通道复制，不另设 MeshTransform/ActorTransform 网络写入者。
- 默认延续当前合同：远端玩家的 Autonomous 身体模拟产生建议；服务端验证后批准胶囊运动和对外身体参照。AI、无 Autonomous 或本地权威玩家由服务端身体/明确提供者产生参照。同一会话同一时刻只有一个身体运动来源。Listen 本地玩家同时具备 Authority 和 LocalControl，进入、初速度、冲量与结束只执行一次。
- 客户端采样不等于客户端拥有最终玩法权威。服务端决定准入、伤害/冲量事实、模式与阶段、恢复批准、落位、死亡/救援和最终结束；不能把距离/速度/LOS 校验表述为服务端重模拟并证明了客户端身体动力学。需要精确服务端身体碰撞的玩法另由服务端身体或可信简化碰撞表示裁决，不让远端表现碰撞决定伤害或推动服务端对象。
- GA 管激活、互斥、最短倒地/稳定等待、选择/请求、播放完成与取消；身体执行组件管绑定、采样、会话接合、Mesh 物理/碰撞/附着、整体纠偏、只读姿势快照及资源交接，派生组件对接 PhysicsControl；CMC/Mode 管胶囊、输入锁、几何、网络与历史重放；动画适配管 MM/PoseMatch、PoseSnapshot 消费、Montage 过渡和局部 IK。依赖保持 GA/适配→身体执行组件→CMC，不让 CMC 查 GA、Traversal 或 PhysicsControl。
- Ragdoll 作为 `MOVE_Custom/Ragdoll` 合理且已实现；它控制身体驱动胶囊的阶段。准备选片仍处于 Simulating；批准起身后退出 Custom，进入 Recovering 输入锁并使用现有 Walking/CMC Root Motion。Recovering 的实际下落/外部模式变化按当前中断合同处理，不默认保留正在起身的 Walking 状态。与 Traversal 的 Flying 接管保持各自职责。

### 三端各自运行什么

| 工作 | Authority | Autonomous | Simulated |
| --- | --- | --- | --- |
| 倒地/恢复/结束 | 决定并发布 | 执行批准结果；可提交请求 | 从状态重建，不要求运行 GA |
| 玩家身体位移来源 | 验证拥有者数据，不用本机副本反向争夺胶囊 | 运行身体、PostPhysics 采样、保存逐 Move 输入 | 本机身体仅作表现，不上报/反推胶囊 |
| AI/无拥有者身体来源 | 运行身体或明确提供者并采样 | 无对应角色 | 接收并收敛 |
| 胶囊 | 计算批准运动与纠偏 | 预测跟随，接受纠偏并重放目标 | 只消费 CMC 权威运动 |
| 起身选片 | AI 本地选择；玩家请求重检/批准 | 对本机身体做 MM，提交候选 | 首版消费同一批准 Montage/StartTime/Rate |
| 动画/身体细节 | 为逻辑需要更新；专服不渲染 | 本机求值与物理过渡 | 本机求值、物理与局部视觉适配 |
| 稳定事实 | AI 读服务端身体；玩家按明确的建议+规则验证合同 | 提供身体线/角速度和有效性，不自行结束阶段 | 不决定能否恢复 |

专服不是“没有画面所以没有逻辑姿势”。若服务端身体、骨骼查询或 Root Motion 需要姿势，必须保证对应 Mesh/PA、骨骼更新、`bEnablePhysicsOnDedicatedServer`、可见性 Tick/动画预算及资源 Bundle 就绪；无有效模拟时不输出伪稳定的零速度。远端玩家没有服务端身体的路线需明确只接受客户端稳定建议，再检查最短/最大时间、数据时效和恢复几何，不能宣称独立验证了身体稳定。

### 数据逐项归属与传输

| 数据 | 来源/权威 | 传输与必要边界 |
| --- | --- | --- |
| Avatar、Session、Revision、Phase、EndReason、移动 Settings | 服务端 | 继续唯一 CMC 生命周期快照；属性与 MoveResponse 共享版本语义，不再用 RPC 维护另一套 Phase |
| 玩家 pelvis 位置、FacingYaw、FaceUp、目标有效性 | Autonomous 建议，服务端验证 | 已有 SavedMove/Packed Move；重放使用历史样本，保持错会话/过期处理 |
| 身体姿态参照、线/角速度、样本序号/时间、支撑参照 | 同一身体来源，服务端批准 | 待扩展连续输入/快照；线速度 cm/s、角速度 rad/s；上传只保留实际消费者需要的项，服务端按 Move 时间/历史差分验证；不接受客户端时间改变模拟时长 |
| 对外身体锚点及姿态/速度 | 服务端批准后的身体事实 | 待增加有时间标识的低带宽快照，带 Session/采样顺序及胶囊对应关系；必须覆盖拥有者纠偏和模拟代理表现。由接受/约束后的结果生成，不直接广播被拒绝或超出跟随限制的原始 pelvis |
| 初速度与首次冲量 | 服务端决定 | 进入合同+一次性标识；区分速度赋值与增量冲量，继承角色/载体速度一次；AdditionalVelocity 当前只存 InitialVelocity，不自动给 Mesh 施加 |
| 同会话后续命中/冲量 | 服务端决定 | Session+事件序号，命中部位/骨架映射、位置空间、方向/强度、行为参数；有界去重和时效处理。重要事件需可靠传递或确认/可恢复策略，不能仅靠丢包即消失的 Cue |
| 身体行为/Profile、必要随机种子、模拟开关 | 服务端批准，三端本地应用 | 用稳定配置 ID/版本及阶段消费；非默认变化需传状态。配置一致时不逐关节复制驱动数值，种子不保证 Chaos 确定性 |
| 身体稳定建议 | 身体来源采样 | 有效性、样本关联、线/角速度和支撑条件；连续稳定时长由服务端 GA 规则维护/检查，不信任客户端一个 Ready 布尔量，也不用胶囊零速度替代 |
| 起身候选 | 拥有者/服务端 AI 的选择适配 | GAS TargetData 绑定当前 Avatar/Spec/激活/Session/请求序号，带 Montage、StartTime、PlayRate、匹配参照与样本关联；服务端独立几何/资产/时间复核 |
| 批准恢复播放合同 | 服务端 | 关联 Session/恢复尝试、Montage/PlayInstanceId、逻辑 StartTime、Rate、批准的起身入场落位及匹配偏移、时间基准/必要 Warp 目标；必须显式送到拥有者，模拟代理按 Montage 前置条件接合 |
| Montage 播放/停止/当前进度、Root Motion | GAS/CMC | 复用原生通道，不另复制逐帧 MontagePosition 或根运动 Transform；本地控制拥有者不由 ASC.OnRep 自动播放，需本机 GA 执行批准合同 |
| Actor/胶囊位置、速度、模式 | CMC | 保持一个移动通道；身体参照快照用于身体，不与 CMC 抢胶囊写入 |
| 支撑对象/组件、局部锚点、必要参考速度 | 来源事实，服务端验证 | 动态载体场景传引用及相对事实，引用未到等待；静态场景无需复制空壳数据，Walking 继续用 BasedMovement |
| 每根骨骼 Transform、PhysicsControl 句柄、PoseHistory、PoseSnapshot、IK、混合 Alpha、局部弹簧历史、相机反馈 | 各端本地 | 首版不复制；Mesh/PA/模型身份沿既有角色装配，物理姿势不是勾 Component Replicates 后自动复制 |

连续样本采用量化、频率/变化阈值、重要转折优先及最新有效快照；不每 Tick 发送 Reliable RPC。改变阶段/受击要唤醒 Dormancy 并及时发布；睡眠可降低更新，不能让远端永远失去唤醒事实。生命周期和持久配置必须能在丢事件、重新相关/晚加入后由当前快照重建。一次性旧冲量不得在晚加入时补打一遍，应由当前身体参照/速度初始化；必要重发依赖精确事件去重。世界原点、局部/世界空间、量化前后几何、单位和压缩布局须各端一致。

### Mesh 物理进入、跟随和纠偏

- 装配就绪包括活动 Mesh、有效 Skeleton/PA、pelvis 刚体和身体配置，不能仅检查组件存在。结合 `CharacterPartsReady/OnCharacterPartsChanged`，CreateControls 返回成功才应用 Profile；Mesh/PA 更换先释放旧身体实例并退休旧采样，再按当前会话重新接合或明确中止。
- 开始前缓存本次实际接管的 Mesh 附着/相对变换、碰撞 Profile/响应、物理模拟/混合状态、朝向/Tick/预算以及控制器创建资源；按实际所有权恢复，不覆盖其他系统后来接管的状态。停旧 Root Motion/互斥 Montage，建立一致的世界姿势后启用物理，继承速度并施加首次冲量一次。
- 胶囊不模拟物理；倒地身体承担接触，胶囊保留移动层所需查询，避免 Mesh/胶囊重复阻挡及自碰撞。当前 Mode.Enter 仅将胶囊改 QueryOnly 并忽略 Pawn，身体适配仍须完成全部碰撞职责；不把该状态误写为 NoCollision。身体是否推物/被推及服务器命中代理按玩法权威合同明确。
- CMC 移胶囊时不能再把同样位移传给自由模拟身体，否则 pelvis→胶囊→身体形成反馈。核对 Mesh 根部是否真的模拟、Skeleton root 与 pelvis 的关系、附着及 SetWorldTransform 对刚体的影响，显式保持世界身体姿态；不假定所有 SetSimulatePhysics/局部骨骼模拟调用都会以相同方式 detach。
- PostPhysics 在最终需要的身体/骨骼数据完成后采样，CMC 下一移动步消费缓存。整条链带采样有效性/时效；纯刚体运动与物理混合后的骨骼姿势不得混成同一事实。动画工作线程只读准备好的数据，不发网络或调用角色物理切换。
- 模拟代理的胶囊由 CMC 控制；身体适配在同一时间基准下对身体锚点施加有界平移/速度纠偏，可用整体加速度/力作用于关节链，保持关节相对关系。小误差柔性收敛，较大误差加强有界纠偏，严重偏差安全整体重定位或降级；不每帧硬设 pelvis 单骨造成关节拉扯，不逐帧强套统一四元数扭转整个人。
- 旋转、正反面和地面法向提供低频姿态约束/恢复依据；保持本机接触响应，不盲目压平高度。空中和接地分开限制纠偏，支撑/墙体参与安全检查；参数和硬纠偏阈值经三端/延迟实测确定，不照抄 GTA 的米制常量。
- 拥有者胶囊接受权威纠偏后协调身体及后续采样参考，保留现有历史 SavedMove 输入，不在历史重放中重新采样/给身体重复施力。后续新样本不能立刻把被拒绝的旧漂移重新喂回 CMC。普通移动的 MeshOffset 平滑不是自由 Ragdoll 身体纠偏，保持当前互斥。
- 断流/超距/通路受阻必须停止使用坏样本并报告，旧数据只作有界表现预测，不继续授予新位移或起身；超时交给 GA/玩法延迟、重同步、降级或救援。身体缓存不能反向解除服务端冻结或 MOVE_None。

### 状态、门控与结束结果

移动层继续只维护 Inactive/Simulating/Recovering；死亡仍用 Health，移动模式仍用 CMC。GA 的细分步骤、本机资源就绪、请求等待与失败原因各归其所有者，不新增并行复制状态机。

| 步骤/情况 | 移动阶段 | 必要行为 |
| --- | --- | --- |
| 未激活/结束 | Inactive | 普通移动或当前外部模式；旧 Session 只保留必要终态关联 |
| 服务端批准进入、客户端等待资源 | Simulating | 状态先缓存，Mesh/PA/模式未齐不模拟、不伪采样；就绪补执行一次 |
| 空中翻滚、接地滑动、身体睡眠 | Simulating | 本机身体事实/行为子状态；仅会影响玩法/恢复的决定性信息才同步 |
| 最短倒地、等待连续稳定 | Simulating | 新受击/运动超限/失效采样重置稳定累计；支撑相对速度考虑载体 |
| 选片/加载/等待服务端批准 | Simulating | 继续身体运动与新鲜采样；候选过期或身体变化重新选择，失败不提前站起 |
| 已批准落位、等待本机播放条件 | Recovering | 位置/输入锁按批准合同，暂停本机自由身体驱动；未就绪表现保持安全姿势，不重新争夺胶囊 |
| 起身 Montage 与物理混出 | Recovering | 普通 CMC Root Motion；按批准实例播放与结束，不能从 AnimNotify 提前宣布恢复 |
| 起身完成 | Inactive | 权威完整 OnCompleted 后解锁；按实际支撑/外部接管决定移动模式 |
| 起身再受击 | 结束旧恢复→新 Simulating | 捕获当前身体/动画姿势连续转入；新 Session，旧播放/回调不能清新资源 |
| 死亡/销毁/外部接管 | 原会话结束 | 交接身体资源给死亡/接管者；不恢复 Walking，不自动关闭接管者身体 |
| 无安全落位/强制中止 | 依现有 Abort 合同 | 普通拒绝保留会话；强制可 MOVE_None/禁碰撞并报告 ExitBlocked，所有者负责救援 |

同会话重复受击用唯一冲量事件/业务规则延长最短倒地，不靠重复 Start 重放首次速度。普通移动、Traversal、武器/瞄准/换装、跳跃/姿态及控制器转向通过 GAS/已有入口形成准入与互斥；Independent/Exclusive 分组本身不替代明确的 Tag 阻塞。Death GA 当前取消非 SurvivesDeath 能力，`AIMGCharacter::OnDeathStarted`→DisableMovementAndCollision 将胶囊禁碰撞并 DisableMovement（MOVE_None），`OnDeathFinished` 下一 Tick 进入销毁；尸体驻留和控制权丢失需显式玩法策略，不能从结束 Ragdoll 自动获得。

### 起身选择、批准与播放的完整顺序

1. GA 确认当前 Avatar/ASC/Session、存活/可恢复、最短时间和有效身体稳定；先 QueryRecovery 获得几何参考。PoseHistory 必须覆盖当前物理/混合姿势，不能只保留倒地前 locomotion；没有历史时提供明确后备。
2. Ragdoll 自己的 PreSelect/SelectRecoveryAnimation 入口使用 MM/PoseMatch，返回 Montage、StartTime、PlayRate 及匹配参照。该命名为设计建议；现有 Traversal 的 SelectTraversalAnimation 是绑定 FIMGTraversalCandidate 的受保护 Blueprint 事件，没有可直接跨 GA 调用的通用 SelectMontage 服务。复用其套路/实际通用设施，不通过启动 Traversal 获得起身。
3. 输入包含模型/姿态兼容、正反面、身体相对支撑的速度、允许恢复方向、当前身体姿势与几何。若 MM 输出 Sequence，明确将 Sequence 时间映射到 Montage Segment 的时间/倍率和目标 Slot；StartTime 不直接照抄 Sequence 秒数。选择不加载成功/未匹配则保留倒地或使用明确后备，不能返回空 Montage 后自动放行。
4. 除 Montage/起点/倍率，选片适配给出与该入场姿势一致的胶囊对齐变换和 BodyLocalOffset（或可复核的匹配描述）；这两个量不能由任意客户端坐标获得信任。服务端从允许的资产/匹配合同与已接受身体参照重建并校验。此变换是开始起身时的胶囊落位，不是 Root Motion 播放后的最终终点。
5. 请求经 GAS TargetData/取消通道发送；同一次持续 GA 可多次恢复尝试，必须有 Session 内请求序号防重放，不只依赖可能为0/不变化的激活预测键。服务端检查当前 Spec/Avatar/会话/请求、采样关联及有效期、恢复许可、资产/骨架/Slot/Root Motion 路径、有限且合法的 StartTime/Rate、允许集合/成本，再复核支撑/完整胶囊/两段通路/量化落位。
6. 异步选择/资源加载和 Commit 后，重新核对会话、BodySample、Mesh/PA/AnimInstance、当前位姿/胶囊尺寸、BaseOffset、支撑变换与有效性；有变化延期/重选。业务 Commit 先后与失败补偿明确，不能在验证失败后丢掉仍活动的倒地会话。
7. 首版只在服务端批准后起身，不预测进入/恢复；这是表现优先的基线，与当前 Start/Recover Authority 接口一致。选片结果与可用播放资源先准备，批准后执行 RecoverRagdollMovementAt/BeginRecoveryAt。批准合同、模式、落位、Montage 实例可乱序，客户端按各自前置条件接合，不因一个 bool 返回值猜最终 Phase。
8. 保持当前已完成物理姿势，停止当前会话自由模拟/身体牵引，应用批准的起身入场对齐并建立局部 PoseSnapshot，恢复受控的 Mesh 附着/基础偏移，按动画合同从物理姿势混入 Montage。停止关节模拟、切 Profile、混合权重和恢复碰撞的先后必须在三端按相同语义执行，避免一帧 reference pose、身体再掉一次或附着造成二次位移。
9. Authority 和拥有者本机 GA 执行同一批准 Montage；模拟代理用 GAS Montage 复制加本系统 ASC 前置门控，等待正确 Avatar/Session/恢复合同、落位/模式、Mesh/AnimInstance、资源和 PlayInstanceId。首版三端统一 Montage/逻辑起点/倍率，远端用本机 PoseSnapshot/混合消化姿势差；暂不做 GTA clone 独立选起身片，防止不同行为 Root Motion 争夺网络位置。
10. StartTime 是选片在动画里的逻辑入点，服务器开始时间/采样时间是另一概念。网络延迟后的当前播放位置、RateScale/Section 路径按 GAS 与批准时间关系协调；不能把 StartTime 当作网络时间，也不能每端从0重播。晚进入时按权威当前位置/落位接合，不能视觉跳进度后再给胶囊补一遍已由 CMC 同步的 Root Motion。自主代理当前 GAS OnRep 不代播，其本机合同接收/播放/校正须显式完成。
11. 起身根运动由正常 CMC 消费，不让 Mode 同时 pelvis 跟随；静态入场落位检查不证明动画全程净空。若 Montage 需要 MotionWarping，显式声明本系统目标、窗口及所需历史输入，复用已验证的通用算法/CMC Replay 接点；不能直接继承 Traversal 的 Flying/障碍忽略，也不能在重放时读取当前而非历史 Warp 目标。当前 CMC 保留原生 Root Motion 不等于 Ragdoll 已有自定义 Warp 重放支持。
12. 有限 Montage/Section 路径、AutoBlendOut、有效 Slot 和有限正倍率须在批准前确认；OnCompleted（包含必要混出）由服务端 CompleteRecovery，OnBlendOut/AnimNotify/客户端完成不提前解锁。正常起身中断、目标失效、播放失败分别明确结束、重新倒地或救援，不能留下 Recovering 永久锁。拥有者播放失败应报告对应会话/恢复尝试给服务端处理，不能本机 CompleteRecovery 改权威阶段；服务端逻辑 Montage 亦须能在专服推进。完成后按权威支撑和当前模式恢复输入/姿态/朝向，清理本次快照与控制器。

### 乱序、取消、移交与性能边界

- 生命周期、CMC 模式/位置、身体快照、Montage 和对象引用不保证跨通道到达顺序；用 Avatar/Session/Revision、恢复尝试及 Montage/PlayInstanceId 配对，单调退休旧状态。结束先到不启动迟到的物理/动画；收到 Inactive 但原模式未退时按既有清理合同等待，不借机恢复旧表现。
- 活动中进入相关范围/晚注册组件按当前快照创建当前身体/恢复表现，不重播过期受击。活动 Montage 迟到按原生进度接合；已经停止的实例不重启。引用晚到/模型加载中保持有界等待，明确失败后可降级，但不能以等待超时自动批准站起。
- 所有异步加载、选片、计时器、AbilityTask、CMC/ASC/Parts 订阅与 Tick 依赖按激活和会话精确释放。回调可同步结束/重入；先退休本次身份再通知外部，回调后重新验证对象。旧 EndAbility/Montage 回调不能停止新会话的身体或播放。
- 玩家断开、Controller/Avatar 更换、ASC解绑、CMC/协调器停用注销、Mesh/PA替换、EndPlay、关卡切换及 Actor 失效都要有唯一收尾者。身体来源失效时不自动把同一会话切到另一套物理权威，先终止或通过显式新来源/新会话接管。
- 死亡交接在释放身体前确认接收者，避免 Death 取消 GA 导致尸体瞬间站立或关闭物理。仅恢复仍归本会话拥有的碰撞/附着/朝向/输入；MOVE_None、死亡和后来的 Traversal/其他模式优先。
- 身体睡眠/LOD/动画预算可降低表现成本，但不能停止服务端必要的 CMC 验证、超时、事件及恢复。剔除后重新可见按当前状态重建，不补跑所有历史物理；不承诺所有远端肢体完全一致。Camera 的 actor/capsule 位置和 Mesh 的视觉滞后须单独核对，镜头不取得身体权威。

### 实施顺序与三端验收矩阵

- 顺序以开头“落地组织与当前边界”为准：先 CMC/MovementMode 基底与网络合同→身体适配及三端表现→GA 业务→MM/GAS 起身→复杂场景与预算；取消、死亡和乱序清理逐步完成，不延后补做。首次不混入全身体 Chaos 回滚；胶囊目标重放与物理历史重模拟分开说明。
- 拓扑：Standalone、Listen 主机玩家、Listen 远端玩家、Dedicated 拥有者与第二观察客户端、服务端 AI/无 Controller（核对 bRunPhysicsWithNoController）；专服必须验证未渲染仍完成所需逻辑/Root Motion。
- 运动：原地、运动中/高速击飞、空中落地、坡面/台阶/墙角/低顶、初始重叠、身体/胶囊偏离、通路受阻、睡眠唤醒、冲量重复/丢失及反复受击；角色尺寸/缩放、不同 PA/骨架坐标按实际支持范围验证。
- 恢复：正反面/侧躺、无 PoseHistory/MM 无结果、Sequence→Montage 时间映射、非零 StartTime/RateScale/有限 Section、资源晚到、起身失败/受阻/中断/再受击、Root Motion 结束/混出、姿势过渡与最终支撑。
- 网络：延迟/抖动/丢包/乱序、旧 SavedMove 与快照、GoodMove ACK/真实纠偏/忽略纠偏、入场/恢复/结束同帧合并、结束先到、引用晚到、重新相关/晚加入、Dormancy 唤醒、拥有者断流/退出；确认没有身体→胶囊→身体反馈、双重初速度/冲量、重复恢复与旧回调清新会话。
- 移交：死亡/Reset/重生、ASC换 Avatar、取消/移除能力、组件停用/重注册、Mesh/PA替换、地图切换及动态载体消失；检查资源成对释放且不覆盖接管者的模式/碰撞。按工程约定，运行验收由用户完成，本轮只做静态调查及设计清单。
