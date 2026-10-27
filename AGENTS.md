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

## 生命周期与 AI
- `AIMGGameMode` 选择 Experience，`UIMGExperienceManagerComponent` 加载；`UIMGAbilitySet` 授予能力、Effect 和 AttributeSet。`AIMGPlayerState` 持有 ASC/属性，`UIMGPawnExtensionComponent` 绑定 Pawn Avatar；`UIMGHeroComponent` 在 DataInitialized 阶段初始化 ASC 和本地输入。
- `UIMGBotCreationComponent` 在 Experience 加载后由服务端生成 Bot；重生分派只支持玩家控制器和 `AIMGPlayerBotController` 派生类。当前无 StateTree 运行时代码；依赖待实现时加到所属模块。
- `UIMGHealthComponent` 发送死亡事件，`UIMGGameplayAbility_Death` 控制开始/结束，`AIMGCharacter` 在结束后销毁；必须授予死亡能力。射击玩法的自动重生能力位于 `ShooterCoreRuntime/GameModes`。

### 持久 Bot：PawnData 与初始化
- BotCreation 配置 BotControllerClass，保留 Controller → PlayerState → Pawn 的生成顺序及通用初始化、分队和出生点规则，不新增 PawnData 配置。`AIMGPlayerBotController` 开启 bWantsPlayerState，每个 Bot 有独立 PlayerState/ASC；共享 PawnData 资产不共享能力运行状态或属性值。
- GameMode::GetPawnDataForController 优先返回 PlayerState 已认领的数据；尚未认领且 Experience 就绪时，查询持久 Bot 的 `AIMGPlayerBotController::GetPawnData`。该入口为 BlueprintNativeEvent/BlueprintPure，支持 C++/蓝图覆写；默认返回空，GameMode 输出 LogIMGExperience Warning 并回退 Experience.DefaultPawnData。普通玩家保留原默认来源，未就绪时不调用 Bot 查询。
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

### Traversal：架构与接入
- 源码位于 `Source/ImagineGame/{Public,Private}/Traversal`，仍为单蒙太奇执行。角色蓝图装配 Traversal/MotionWarping 组件，AbilitySet 授予 GA；输入决策独立，蓝图负责 CHT/PoseMatch，专用服务器需要不依赖 PoseHistory 的后备选择。
- `IMGTraversalTypes.h` 统一声明设置、请求、Candidate、动画执行配置和复制快照；Detector 是无状态同步几何源；Component 持有检测、发起与动画配置、动态 Warp Target、临时碰撞忽略和代理快照；GA 负责检测、选择、Commit、播放和退出。

### Traversal：检测与动画配置
- 主要 Candidate 输出和 CHT 接口保留。检测以 GTA 人形几何为基础，补多命中、单侧手点、方向重试、真实首点深度与坡面净空；`Supports` 只表达必要几何，GTA 动画组合表仅作 RecommendedAction 参考，最终动画覆盖交给 CHT。
- `DetectorSettings` 暴露探针、边缘分组、评分、坡面、安全范围和 Surface Policy；原有数值默认保留，搜索/动画覆盖上限仍为 250 cm，高度默认仍以胶囊脚底计算，可切换 GroundPos 基准。检测及 GA 落点复核共享 Traversal 通道和表面许可。
- 前向各层实际搜索长度可按 CMC 水平速度缩放；`SearchDistance` 为统一上限，Automatic 各层上限参考 250/150 cm，速度缩放是项目扩展，不是 GTA 原生探针公式。起点、球半径和层距不随速度改变；调试图及未命中净空使用实际扫掠长度。零速是否调用检测仍由 GA/蓝图控制。
- `InitiationSettings` 独立控制允许发起距离，GA 在选择前和服务端重检后消费；读取 CMC 速度/加速度与移动输入，使用新增 PlanarDistanceToLedge，原 DistanceToLedge 的 Mesh 三维距离含义不变。Manual 默认允许 150 cm 内站立发起；Automatic 需移动意图及朝向障碍的速度，默认距离 75–150 cm，速度映射可配。WhileInputActive 不等于 Automatic，请求来源由 GA 配置；CHT 继续决定动画覆盖。
- Query/GA 区分 Manual、Automatic、AI。表面许可用组件/Actor Tag；Automatic 的开放高位、深度和楼梯策略不应被误当作所有请求的通用动画门槛。AutoVault 输入、历史抑制和延迟不属于 Detector。
- Component 的 `ActionSettings` 为动作默认值，`MontageSettings` 按 Action/Montage 完整覆盖。WarpBindings 配置名字、Front/Back Ledge/BackFloor 几何锚点及偏移，RequiredTargets 配置必要接触；名字默认仍为 FrontLedge/BackLedge/BackFloor，前沿默认上移 0.5 cm。
- 同名 Warp Window 可重复，项目不限制数量；Notify 继续负责窗口时间和 UE Modifier 参数。Landing 配置选择无落点、检测地面或曲线落点，并配置曲线名、锚点/落地窗口序号（-1 为最后一个可用平移窗口）；默认 Hurdle 使用 Distance_From_Ledge，BackLedge 缺失时回退 FrontLedge。过期的显式窗口序号不能自动移到下一个窗口。
- 最终落点仍校验可走地面、表面许可、最大落差和完整胶囊；直线扫掠为可选保守策略，默认关闭，不能把直线弦当作动画弧线。位移由 Root Motion、PoseSearch StartTime 与 UE MotionWarping 负责，不叠加 GTA Task 的手动速度补偿。
- 净空按 `ImpactPoint` 的表面接触点计算，扫掠球心 `Location` 只用于调试体积；未命中返回的净空上限必须被实际扫掠覆盖，水平长度计入后移和坡度。后沿细化围绕最后有效顶面高度；深度扫掠初始穿透仅停止后续深度采样，保留此前已验证的前沿/支撑/后沿，不新增后沿或落差，也不直接拒绝整个候选；接近路径与净空仍须通过。未确认后沿时 ObstacleDepth 只表示已采样顶面范围（可为0），不能当作完整障碍厚度。面方向重试按实际使用的请求方向判断，失败候选不发布。
- CHT 预选接收 Detector 几何；GA 选定动画后可重解 BackFloor/DropHeight/BackLedgeHeight，OnTraversalStarted 接收该执行结果。DistanceToLedge 保留 Mesh 世界位置到前沿的三维距离，PlanarDistanceToLedge 为 Actor 到前沿的 XY 距离。BackLedge 是跌落前支撑，BackFloor 是采样/动画落点，必须按各自存在标志消费。

### Traversal：执行与网络
- 继续按校验→Commit→建立 Target/碰撞忽略→Flying→蒙太奇启动→提交 TargetData/发布快照执行。Component 统一管理临时碰撞忽略；完成、中断、取消、死亡或载体失效时幂等恢复。GA 不在正常 BlendOut 提前结束，退出按脚下地面恢复 Walking/Falling；模拟代理收到权威非 Flying 模式后先恢复碰撞，使 CMC 地面查询看到支撑，Target 仍可保留至本机实例退出。
- 现有载体逻辑保存局部 Target，并建立载体组件 → Traversal → CMC 的 Tick 依赖；默认每 0.05 秒复核存活、物理许可和速度，默认 500 cm/s、85 deg/s，Candidate 时效 0.25 秒。这不保证载体 Actor Tick 或物理驱动的完整更新时序，也不代表动态平台已受支持；不要直接 Attach 绕过 CMC 网络预测。
- 保留 LocalPredicted、启动成功后提交请求、启动前单次取消和服务端按方向重检；请求来源必须与服务端 GA 配置一致。远端仍验证 Action、时间、PlayRate、动画合同和可选 AllowedMontages，不接受客户端几何。
- 代理仅消费 COND_SkipOwner 快照、CMC 与 GAS 蒙太奇；匹配 Montage/PlayInstanceId，并等待 Target、障碍碰撞忽略与 CMC Flying 就绪。OnPreUpdate 在 Warp 采样前重建当前 Target，既有 Target 的恢复历史随权威快照传输，不在代理捕获乱序的原生复制值；结束快照先到时保留当前动画到本机实例退出（含 BlendOut），同实例引用补齐不反复拆建。
- ASC 保留 Position 复制、StartTime 下界、资产 BlendIn 和 GAS 原生纠偏；已移除固定 0.075 秒起播落后量。社区参考：`https://forums.unrealengine.com/t/motionwarping-warp-targets-and-data-caching/2678223`（Target 与蒙太奇的独立复制时序），不是固定落后量的依据。

### Traversal：Debug 对照与排查
- 仅调试当前 Traversal 实际执行的检测/输出/动画消费，不移植 GTA 网络或画未接入的异步批次、IK/平台体积。对照 `ClimbDetector` 的 Search/Intersections/Compute/Validate、`TestCapsule` 和 `ClimbHandHoldDetected::Render/DumpToLog`；所有代码受 ENABLE_DRAW_DEBUG 保护，控制台项为 Cheat，默认关闭。
- `IMG.Traversal.DebugDraw`：0关闭，1几何/执行输出及活动 Target，2增加搜索/评分/失败/重试，3全部探针。`IMG.Traversal.DebugStages` 为组合掩码：搜索1、命中2、手点4、体积8、输出16、执行32，默认63；例24只看体积与输出、48看输出与执行，等级和掩码同时生效。
- `IMG.Traversal.DebugDuration` 默认2秒，仅控制启动成功后的快照；`IMG.Traversal.DebugProcessDuration` 默认0（单帧），设3可保留失败/重试过程，对照 GTA 的3000ms历史。连续自动检测会叠图，宜单次手动触发或暂停查看。活动 Target 始终单帧、不进入过程快照；失败候选不混入成功快照。
- 搜索分层为不同蓝色，显示真实起终点/半径/实际长度/高度/命中数；紫色 edge 连接真实最低/最高命中，显示编号、Score、Span、Drift。命中显示首次/次级、组件、接触点和外法线；青色小球为可进入分组，红色大球与文字说明表面策略、穿透、水平面或同坡面排除。
- 手点为黄色中央/左右/单侧/附加探针，显示法线阈值、宽度支持、高度容差、投影/调整及请求/面方向重试。体积用青色接近路径、绿色深度 support/rise/drop/missing 与最高支撑、橙色后沿细化/后脸、洋红水平双层净空、黄色垂直净空；并画坡面投影、攀爬角与近身/Automatic 拒绝原因。
- `IMG.Traversal.DebugLineScale` 默认1，线宽/箭头头部为此前1/3设置的3倍；`IMG.Traversal.DebugSphereScale` 默认仍为1/3，控制调试球半径/点标记，设为1时球半径等于真实探针半径。只影响绘制，标签R、查询参数、球心/接触点坐标和胶囊边界保持真实值。
- 本机文字通过 UE DebugDrawService 按各视口投影自动避让，移位时画细引线；`IMG.Traversal.DebugTextScale` 默认0.65。汇总合成多行块，原始几何→执行结果更新同一角色的汇总；活动 Target 按角色/名字更新，同帧过程与快照重放的相同标签合并并延长寿命，不重复叠字。文字过长自动折行，空间不足优先保留汇总和低等级信息，底部提示筛选 DebugStages 或查 DebugLog；专服转发仍沿 UE 原生 DrawDebugString。
- 调试文字仅持有 World/Actor 弱引用，过期/失效时清理，关闭 Draw 清队列；惰性注册的绘制委托在 ImagineGame 模块 Shutdown 注销，不新增 Tick、计时器或 Actor。
- 球扫两端显示探针球（默认按上述比例缩小）；红球为停止时的球心 Location，小接触点/法线为 ImpactPoint/ImpactNormal，白线连接两者。红球只表示接触，拒绝看文字。输出用厘米标尺画高度、XY深度、前沿落差、后沿高度和净空；净空未命中表示已扫范围的有限上限，非无限空间。EdgeSpan 不是左右手点的最高/最低Z，两者分别显示。
- Detector 输出标记 pre-selection，启动后显示执行 Action/Montage/Start/Rate；CHT 原始 BackFloor 与动画解析后的结果可能不同。执行图包括曲线窗口、重新查地、Binding/WarpPoint 偏移、完整胶囊、实际脚下支撑、可选直线路径及锚点→Target。活动 Target 标明角色名、网络角色、当前 CMC Mode 和本系统 AddedIgnore 数量；模拟代理通常只消费执行状态，不跑 Detector。
- `IMG.Traversal.DebugLog`：0关闭，1在 LogIMG 的 [Traversal] 记录输出坐标、法线、尺寸/距离/角度及网络角色，2额外记录所选阶段文字和拒绝原因；仍需开启 DebugDraw 与对应阶段。活动 Target 不持续写日志，成功快照重放不重复写日志，动画合同失败沿现有 LogIMGTraversal。
- 完整观察可依次设置 `IMG.Traversal.DebugDraw 3`、`IMG.Traversal.DebugStages 63`、`IMG.Traversal.DebugProcessDuration 3`、`IMG.Traversal.DebugDuration 3`。单进程 PIE 共享 CVar，多进程分别设置；图和日志需按角色/网络角色区分。优先对照平顶遮挡接触点、斜顶后沿、上坡/后移后的净空范围、方向失败重试、后方独立障碍、偏移后胶囊支撑及各代理结束清理；静态审查与 Debug 覆盖不替代资产和联机运行验证。

### Traversal：GTA 参考与映射边界
- 参考根目录：`D:/Application/GTA/GTAVSP/GTAV Source/src/dev_ng/game`。几何/手点：`task/Movement/Climbing/{ClimbDetector,ClimbHandHold}.*`；发起：`task/Default/TaskPlayer.cpp`（CTaskPlayerOnFoot）；Jump/Vault 分流：`task/Movement/Jumping/TaskJump.*`；执行/载体/清理：`task/Movement/Climbing/TaskVault.*`。
- 网络使用本工程现有 GAS LocalPredicted、TargetData、蒙太奇复制、CMC 与 Traversal Target 快照，不移植 GTA 网络协议。GTA 的 `task/System/TaskFsmClone.*`、`Peds/QueriableInterface.cpp`、`network/Objects/Entities/NetObjPed.cpp` 仅作职责边界参考；Detector/Candidate、CHT、GA、Component 分别承担几何、动作选择、执行和代理状态，未复制 GTA Task FSM。
- GTA 出口表服务于其动画库；GroundPositionAtDrop 是跌落前支撑点，非最终落地。项目按自身动画覆盖配置门槛，以 UE Warp/地面复核映射执行；GTA 实体类型、动画组合与手动位移不能机械硬搬。
- GTA Detector 通用搜索回退为 5 m，但 TaskPlayer::GetClimbDetectorParam 的普通玩家覆写为 2.5 m；项目 250 cm 默认有该调用链依据。检测在边缘分组前过滤水平面与同地面坡面，并保留近身上半身遮挡检查；低障碍按请求朝向验证时同步调整手点。Automatic 的面朝向/朝向手点阈值默认 0.8，Manual/AI 默认 0.3；移动意图阈值仍由 InitiationSettings 独立控制。
- 本轮以 Detector/探测逻辑为主，也复核 GA 执行、GAS 取消握手及自主/模拟代理生命周期；不改用 GTA 网络。Automatic 楼梯下行过滤使用楼梯标签与 CMC 地面法线，不能等同 GTA 独立的 slope-normal 数据源。动画落点复核 WarpPoint/Binding 偏移后的胶囊脚下支撑；曲线锚点仅在主目标没有平移窗口时使用备用名字。ASC 解绑清理代理临时状态，重绑重建权威快照。
- 后续边界：输入历史与楼梯退出延迟、异步 broad phase 缓存、完整 BasedMovement/动态 Bone、悬挂/Shimmy/梯子等仍属独立工作，不应宣称本次已完整等价。

### Traversal：待做——动态平台
- 本轮只收尾静态障碍检测与执行，不扩展平台移动支持；已撤回本轮新增的载体 Actor Tick 前置依赖及运动载体落点特判。既有局部 Target 跟随不代表动态平台已受支持。
- 后续需围绕本工程 GAS 预测、服务端重检、CMC BasedMovement/根运动、载体变换复制及更新顺序统一设计，并验证平台平移/旋转、开始/结束乱序、引用晚到和中断清理；不能仅增加 Tick 依赖、Attach 或套用 GTA 执行/同步套路。

## 装备、背包与射击
- 装备、背包、快捷栏、远程追踪和武器生成器基类保持原职责；快捷栏与物品实例仅补跨模块导出。装备授予能力后，快捷栏再为装备实例关联物品。
- 通用开火、换弹、自动换弹能力位于 `ImagineGame/Weapons`；具体武器规则、快捷栏输入、丢弃和拾取派生逻辑位于 `ShooterCoreRuntime`。开火复用远程追踪，派生类处理 TargetData、服务端校验与弹药成本。
- `DropWeapon` 当前只移除快捷栏/背包物品，未生成地面拾取物；初始物品发放和死亡清理留给蓝图装配。校验阈值、自动装弹轮询及实机链路尚未按参考资产逐线验证。

## UI
- UI 可订阅快捷栏 SlotsChanged/ActiveIndexChanged 消息和生命/死亡委托；装弹有开始/结束表现事件，自动重生有可配置消息 Tag。
