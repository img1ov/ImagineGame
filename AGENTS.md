# ImagineGame 工程速查

## 工作约定
- 先核对本地源码、官方资料或案例；区分事实、资产名线索与待验证项。改代码前找到调用链、同类实现和所属模块，在原职责处扩展，避免重复状态与抽象。
- 新增 struct：指定目标 `.h` 就写入该文件；未指定时先调查位置，再请用户确定头文件。
- 仅在关键约束和生命周期边界写短注释；回复只给结论、证据、变更与必要风险。
- 未明确要求不编译；C++ 变更核对源码、引擎接口和构建依赖，完成后做静态链路与差异检查。不调查或修改 Content，除非用户要求。
- 先扫本文标题，只读相关小节；仅补长期有用的已核实事实，结构变化时清理过时内容。

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

### AI PawnData：已核实链路与设计结论
- 当前 BotCreation 只配置一种 BotControllerClass，按 Controller → PlayerState → Pawn 的顺序创建。`AIMGPlayerBotController` 开启 bWantsPlayerState，每个 Bot 自动拥有独立 PlayerState/ASC；PawnData 是可共用的配置资产引用，不共享能力运行状态或属性值。
- 当前 PlayerState 在 Experience 回调中通过 GameMode::GetPawnDataForController 取数据：先取该 Controller 的 PlayerState.PawnData，再取 Experience.DefaultPawnData。回调注册时若 Experience 已加载会立即执行；SetPawnData 仅允许服务端首次设置，并立即授予 AbilitySets。普通 SpawnActor 返回后再替换数据已经太晚。
- GameMode 根据 PawnData.PawnClass 生成 Pawn，在 FinishSpawning 前给 PawnExt 写入同一 PawnData。装有 Hero 且依赖满足的 Bot 仍走 PawnExt/Hero 的 Spawned → DataAvailable → DataInitialized → GameplayReady，并广播本机 OnAbilitySystemInitialized；仅跳过玩家输入要求及绑定。
- 已确定的扩展方向（尚未实现）：AI 的 PawnData 配置入口随 Pawn，优先复用其现有 PawnExt 字段，不在 Pawn、Controller、Spawner 重复维护配置。支持地图放置及不同入口动态生成；保留 PlayerState ASC、现有 PawnExt/Hero 状态链和复制机制。玩家继续使用原有 Experience 配置流程。
- 现有 PawnExt.PawnData 为 EditInstanceOnly 且可复制，尚不支持 Pawn 蓝图类默认配置；仅给 PawnExt 填数据不会授予 PlayerState 能力。Pawn 自带配置方案需要扩展 AI PlayerState 的数据就绪协调，不能让其先认领 Experience 默认数据后再覆盖。
- 晚生成本身不会漏掉 Experience 初始化；客户端通过 PawnExt.OnRep_PawnData、Character.OnRep_PlayerState 等重新推进本机状态，模拟代理不要求 AIController。晚注册监听者使用 RegisterAndCall；IMGAbilitiesReady 当前仅在服务端 SetPawnData 发送，非网络广播；OnAbilitySystemInitialized 只表示本机 ASC/Avatar 已绑定，不保证能力和属性已全部复制到齐。

### AI PawnData：待做
- 扩展现有 PawnExt 配置，使 AI Pawn 蓝图类默认值及关卡实例可指定 PawnData；明确 PawnData.PawnClass 与实际 Pawn 类型的合同，避免重复配置不一致。
- 在原有 PlayerState/PawnExt/Hero 职责内协调 AI 初始化：等待 PawnData、Controller/PlayerState 与 Experience 就绪后，服务端首次设置 PlayerState 数据并授予能力，再沿现有状态链绑定 ASC。覆盖 Experience 先到/后到、Possess 先于/晚于 BeginPlay；避免错误默认授予、重复授予及重复广播，不新增 Tick 轮询或补发初始化 RPC。
- 调整 BotCreation 与生成/重生入口，使 Pawn 类型可先被选定，读取 Pawn 自带配置并保留必要的通用初始化、分队和出生点处理；支持共用 Controller 类，不要求每种 PawnData 建一个 Controller 蓝图。
- 明确能力生命周期：同一 PlayerState 重生同类 Pawn 复用配置与能力；切换不同 PawnData 时设计旧能力/Effect/AttributeSet 回收或 PlayerState 重建，不能直接覆盖当前只写一次的数据。
- 完成后审查服务端、模拟代理、Listen/Dedicated Server、晚加入、引用乱序、死亡重生、重复初始化和销毁清理；核对客户端资产可用及既有 RegisterAndCall 回调。当前仅完成源码调查和设计记录，未实现、未编译或验证网络表现。

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

### Traversal：执行与网络
- 继续按校验→Commit→建立 Target/碰撞忽略→Flying→蒙太奇启动→提交 TargetData/发布快照执行。Component 统一管理所有角色的临时碰撞忽略；完成、中断、取消、死亡或载体失效时幂等恢复，BlendOut 不提前清理。GA 退出按脚下地面恢复 Walking/Falling。
- 载体保存局部 Target，Tick 排在载体之后、CMC 之前；默认每 0.05 秒复核存活、物理许可和速度，默认 500 cm/s、85 deg/s，Candidate 时效 0.25 秒。当前是 Target 跟随，尚非完整动态平台附着；不要直接 Attach 绕过 CMC 网络预测。
- 保留 LocalPredicted、启动成功后提交请求、启动前单次取消和服务端按方向重检；请求来源必须与服务端 GA 配置一致。远端仍验证 Action、时间、PlayRate、动画合同和可选 AllowedMontages，不接受客户端几何。
- 代理仅消费 COND_SkipOwner 快照、CMC 与 GAS 蒙太奇；匹配 Montage/PlayInstanceId，并等待 Target、障碍碰撞忽略与 CMC Flying 就绪。OnPreUpdate 在 Warp 采样前重建当前 Target，既有 Target 的恢复历史随权威快照传输，不在代理捕获乱序的原生复制值；结束快照先到时保留当前动画到本机实例退出（含 BlendOut），同实例引用补齐不反复拆建。
- ASC 保留 Position 复制、StartTime 下界、资产 BlendIn 和 GAS 原生纠偏；已移除固定 0.075 秒起播落后量。社区参考：`https://forums.unrealengine.com/t/motionwarping-warp-targets-and-data-caching/2678223`（Target 与蒙太奇的独立复制时序），不是固定落后量的依据。
- `IMG.Traversal.DebugDraw` 为 0–3 级；`IMG.Traversal.DebugDuration` 默认 2 秒。执行 Draw 为单帧，成功快照待全部启动成功才重放；失败回滚，所有 Draw 受 ENABLE_DRAW_DEBUG 保护。

### Traversal：GTA 参考与映射边界
- 参考根目录：`D:/Workspace/Learning/Sources/GTAV Source/src/dev_ng/game`。几何/手点：`task/Movement/Climbing/{ClimbDetector,ClimbHandHold}.*`；发起：`task/Default/TaskPlayer.cpp`（CTaskPlayerOnFoot）；Jump/Vault 分流：`task/Movement/Jumping/TaskJump.*`；执行/载体/清理：`task/Movement/Climbing/TaskVault.*`。
- 网络参考继续沿 `task/System/TaskFsmClone.*`、`Peds/QueriableInterface.cpp`、`network/Objects/Entities/NetObjPed.cpp` 追踪。Detector/Candidate、CHT、GA、Component 快照分别映射几何、动作选择、执行和代理状态；未复制完整 GTA Task FSM 或网络协议。
- GTA 出口表服务于其动画库；GroundPositionAtDrop 是跌落前支撑点，非最终落地。项目按自身动画覆盖配置门槛，以 UE Warp/地面复核映射执行；GTA 实体类型、动画组合与手动位移不能机械硬搬。
- 后续边界：输入历史与楼梯退出延迟、异步 broad phase 缓存、完整 BasedMovement/动态 Bone、悬挂/Shimmy/梯子等仍属独立工作，不应宣称本次已完整等价。

## 装备、背包与射击
- 装备、背包、快捷栏、远程追踪和武器生成器基类保持原职责；快捷栏与物品实例仅补跨模块导出。装备授予能力后，快捷栏再为装备实例关联物品。
- 通用开火、换弹、自动换弹能力位于 `ImagineGame/Weapons`；具体武器规则、快捷栏输入、丢弃和拾取派生逻辑位于 `ShooterCoreRuntime`。开火复用远程追踪，派生类处理 TargetData、服务端校验与弹药成本。
- `DropWeapon` 当前只移除快捷栏/背包物品，未生成地面拾取物；初始物品发放和死亡清理留给蓝图装配。校验阈值、自动装弹轮询及实机链路尚未按参考资产逐线验证。

## UI
- UI 可订阅快捷栏 SlotsChanged/ActiveIndexChanged 消息和生命/死亡委托；装弹有开始/结束表现事件，自动重生有可配置消息 Tag。
