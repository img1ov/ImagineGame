# Traversal 检测与执行

相关主题：[Traversal 调试](traversal-debugging.md) · [普通移动与 CMC](locomotion.md) · [Gameplay 框架](gameplay.md)。

## 职责与装配

- Traversal运行时及编辑器源码的注释统一使用英文，配置合同与示例保留原意；中文工程导航继续写在AGENTS中。
- 源码位于 `Source/ImagineGame/{Public,Private}/Character/Traversal`，属于 ImagineGame。以 UE 合同为准：GAS 管能力和 Montage 生命周期，CMC 管权威移动、预测与纠偏，MotionWarping 修改根运动；GTA 只对照职责与算法意图。
- Traversal 运行时与编辑器源码统一为 `.h` 在 `Public`、`.cpp` 在 `Private`，相对目录配对；执行算法及调试头也遵守此布局，include 从 Public 根起写。目录公开不自动增加跨模块导出，模块约定见 [工程与编辑器](project.md)。

| 入口 | 职责 |
| --- | --- |
| `IMGTraversalDetector` | 无状态同步几何查询，产生候选和建议动作，不选择动画。 |
| `IMGGameplayAbility_Traversal` | 检测、CHT/PoseMatch 选片、校验/Commit、播放和退出。请求共用 `OnTargetDataReady → ResolveTraversalRequest → StartTraversalExecution`。 |
| `IMGTraversalComponent` | 配置、Target、临时碰撞忽略、Flying 接管/释放和复制快照。 |
| `{Public,Private}/Character/Traversal/Execution/IMGTraversalWarpWindows`、`IMGTraversalPlaybackPath` | 显式配置的窗口收集、有限 Section 路径及播放时间映射。 |
| `{Public,Private}/Character/Traversal/Execution/IMGTraversalWarpPrediction` | 根运动/原生 Warp 窗口预测及当次计算缓存，不反查 Component 或 GA。 |
| `{Public,Private}/Character/Traversal/Execution/IMGTraversalPlacement` | 目标准备、支撑落位、可选补偿、显式 Landing 校验与 Mantle 交接边界。 |
| `{Public,Private}/Character/Traversal/Execution/IMGGroundPlacement` | 无行为类型依赖的支撑几何计算及实际胶囊有限恢复/查地。 |
| `{Public,Private}/Character/Traversal/Animation` | 专用手部 AnimNode 与 Traversal 前沿接触算法；编辑器包装在 ImagineEditor 同路径。 |
| Private `IMGTraversalReplay.cpp` | 同一 Component 的逐 Move 上下文记录/恢复，无独立复制服务。 |
| Types / TargetData / Debug | 公共数据、客户端请求、私有观测采集、独立Trace与DrawDebugLibrary表现；编辑器轨道位于ImagineEditor同层级Character/Traversal。 |

- 保持来源组件 → Traversal → CMC 的源码依赖；CMC 只暴露通用委托/闭包接口，不包含 Traversal 类型。热插拔指组件与能力运行时接合/释放，不表示共享运行时可作为独立插件卸载。
- Pawn 装配 Traversal、MotionWarping 并由 ASC 授予 GA。`ActionSettings` 提供动作通用执行规则；`MontageSettings` 按 Action/Montage 整份覆盖，不合并字段。`WarpBindings/RequiredTargets` 管接触目标，Notify 管窗口，`Landing` 管落地合同。选片只返回一个 Montage；服务端本地/AI 选片缺少 PoseHistory 时须有后备，远端请求使用客户端选择并由服务端重检。

### 独立系统与生命周期合同

- 用户明确：同一角色同时只有一个 Traversal，翻越、攀爬、悬挂、梯子、绳索各自拥有完整行为生命周期，行为之间不互串。每次行为以 Traversal GA 生命周期为权威；Component 承接执行资源与快照，AnimNode 管理局部姿势缓存。无需 Traversal 内部并发仲裁、目标混合或行为交接状态机。未来其他行为按自己的运动模型实现，共用结束合同不等于共用 Flying 或有限 Montage。
- 开发阶段不保留旧蓝图/C++兼容入口；现有手部表现仅走专用 AnimNode。`FIMGTraversalHandContact` 位于 Traversal/Animation，其前沿球扫、早段横扫和接触历史属于当前 Traversal 表现，不是通用 Animation 服务。Component 不包含手部算法、骨名/Socket 绑定或逐手历史，动画表现只读执行数据。
- `FIMGTraversalExecutionSettings` 表达 WarpBindings、RequiredTargets、Landing、净空和播放速率等执行合同；`FindExecutionSettings` 按 Action/Montage 整份选择配置。手骨、掌点 Socket、逐手 Alpha 和接触参数归 AnimNode。当前根运动 Montage、Warp 窗口和落位预测影响移动结果，仍属执行层。
- `BeginTraversal` 在取得资源前分配本地非零 ExecutionId，GA 用该标识验证、发布和释放自己的执行；它与复制 Sequence、预测键和 TargetRevision 分责，不新增复制字段。结束使用 `EndTraversal(ExpectedExecutionId)`，旧调用不能释放新动作资源。服务端 GAS 保持权威，自主代理继续预测；模拟代理消费权威快照和 GAS Montage，不要求本机运行对应 GA。
- `IsTraversalAvailable` 表达组件注册/活动及不在释放/失效过程，`IsTraversalReady` 核对 CMC/MotionWarping 执行设施，模拟代理另要求有效 ASC Montage 接合；本地/权威端的 ASC/Avatar 有效性由 GA 当前 ActorInfo 核对，不等待 Component 收到 ASC 初始化广播。MotionWarping 的原生更新依赖注册及 OwnerAdapter，不以组件 IsActive 标记作为准入。GA 从激活开始订阅 `OnTraversalUnavailable`，覆盖等待 TargetData 与已执行阶段；基础接合失效时先关闭入口、报告给 GA，再兜底幂等释放资源。GA 结束精确解除自己的委托。
- ASC 解绑/换绑、停用、注销、EndPlay 及执行接合失效均在通知 GA 前退休本方 Replay，并结束本方执行；回调后的对象/绑定重新核对。组件或模拟代理 ASC 接合不可用时 OnRep 只保留复制值；重新接合后从当前快照补齐。模拟代理 CMC/移动模式、Target 引用和快照可以乱序，保持原有活动快照及 Tick 重试，由 Montage 前置条件等待所需数据，不把暂未就绪误判为执行失效。同 ASC 重复接合不重置活动执行；首次 ASC 接合不走解绑清理，因为 OnSpawn 能力可在 InitAbilityActorInfo 内先于 PawnExt 初始化广播激活。热插拔不依赖 ABP 回调或手点曲线，不表示运行时卸载 C++ 模块。
- 选片入口保持 `PreSelectTraversalAnimation → SelectTraversalAnimation → CHT/PoseMatch`。原生读取选中 Action/Montage 的执行规则：匹配 MontageSettings 则整份覆盖，否则用 ActionSettings；不要求为每个 CHT 结果登记 MontageSettings 或 ReplicatedMontages。ReplicatedMontages 保持原有可选语义：空列表按执行窗口/绑定合同识别模拟代理播放，非空才限定列表及 MontageSettings 的归属；它不替代 CHT 选片。
- 执行算法只依赖传入合同和原生设施：`IMGTraversalWarpWindows::Collect` 显式接收 MotionWarping 的 `bSearchForWindowsInAnimsWithinMontages`；PlaybackPath/Prediction/Placement 不包含 Component/GA。完整窗口/预测上下文只存在于单次准备调用；Component/Replay 另保存不含窗口或 Modifier 指针的数值 Section 路径，用于跨帧判断交接。外部死亡/Ragdoll 通过 GAS/移动接管合同中断，具体行为不互查组件。

## 检测、发起与选片

- 自动分层搜索使用具名标量：`Search → Automatic` 下的 `AutomaticLowerLayerCount=2`、`AutomaticLowerSearchDistance=250 cm`、`AutomaticUpperSearchDistance=150 cm`。实际层数仍由 MaxHeight、胶囊高度、SearchStartHeightOffset、SearchProbeRadius 和 MaxSearchLayers 计算，低层数量不增加探针；0表示全部用高层距离，不小于实际层数则全部用低层距离。每层最终长度为 `Min(SearchDistance, 所属组距离) * SearchScale`；Manual/AI 仅用统一 SearchDistance 与速度缩放。旧逐层数组已删除，无兼容/自动迁移；旧蓝图覆盖需在新字段重设，例如250/250/200/200/200/200对应2、250、200。入口：`IMGTraversalTypes.h`、`IMGTraversalDetector.cpp::Detect`。
- 配置按消费职责排列并使用原生详情分组：Detector 的 Search（含 Speed Scaling/Automatic）、Geometry、Facing、Handhold、Depth、Clearance、Edges、Scoring、Slopes、Stairs、Surface Policy、Collision、Validation、Recommendation；推荐速度及 Hurdle/Vault 高度的显示名明确标注 Recommended，不代表硬准入。Initiation 分 Manual and AI、Automatic、Networking，关闭 bEnabled 后禁用子参数；StandingMaxDistance 另依赖允许站立。Execution 分 Warp Targets、Landing、Ground Handoff、Clearance、Playback；Landing 关闭时禁用附属字段，只有 DistanceCurve 模式可编辑曲线/接触锚点参数。长度、速度、角度和时间标注单位；原有字段名与默认值除替换数组外保持不变。
- ActionSettings、MontageSettings、WarpBindings、RequiredTargets、表面标签及网络列表按动作/资产/目标名表达条目身份，保留可变数组；MontageSettings/AllowedMontages 标题显示 Action + Montage，WarpBindings 显示 TargetName。GA RetryInterval 归 Activation，超时/白名单统一归 Networking；Component 的 ReplicatedMontages 与 GA 的 AllowedMontages 分别控制模拟代理播放归属和远端请求准入，不能因都列出 Montage 就合并。
- 每次激活只尝试一次，外部决定触发时机，`RequestSource` 与 `ActivationPolicy` 独立。`CanActivate` 作本地几何/发起预筛，激活后依次执行 `PreSelectTraversalAnimation`、`SelectTraversalAnimation`。Chooser 消费入场 MovementMode/Gait/Speed，必须早于 `BeginTraversal` 切 Flying。
- `SampleHandhold` 采样中心/左右顶部、宽度和楼梯轮廓，`MakeCandidate → ValidateCandidateDirection` 验证方向。方向回退复用本次公共采样，但从原始候选副本重算深度、后沿、净空和坡向；不跨 Detect 调用、帧或网络端缓存，不省略服务端重检。
- `InitiationSettings.bEnabled=false` 仅跳过发起限制。Manual 默认允许站立、范围150 cm；Automatic 默认允许 Falling，要求有效意图、朝向点积≥0.65、固定范围150 cm，不按速度二次缩距。速度缩放只影响搜索长度；`Candidate.Speed/RunningSpeedThreshold` 仅供建议/选片，不限制原生 Supports。
- 斜向接近的角度门分两处：Detector 的 `AutomaticMinFacingDot=0.65` 检查探测方向对墙面内向法线及目标方向，`CanInitiate` 的 `AutomaticMovementDirectionDot=0.65` 另检查输入意图对沿点方向；两项默认均允许约±49.46°，给45°（点积约0.707）的斜向接近留余量。Manual 对应两项默认0.3（约±72.54°）。玩家 GA 发起探测使用角色 ActorForward，输入意图单独取 PendingInput/Acceleration；输入映射采用 ControlRotation.Yaw。`RequestSource` 原生默认 Manual，持续按键/WhileInputActive 不自动等于 Automatic。`MaxApproachAngle=42.5°` 是接近路径几何相对世界Z的角度，不是这里的水平入墙夹角。蓝图显式覆盖值不随C++默认值强制重置，现场模式与实际参数须按实例确认。
- 意图优先待消费输入，再取 CMC.Acceleration，不以 Velocity 替代。玩家使用 GA.RequestSource；服务端 AI 强制 AI 来源，复用 Manual 规则，无输入/加速度时可取 Moving 的 PathFollowing 当前段目标，无意图时可按手动站立规则向前尝试。
- 发起距离由当前 Actor 与前沿重新计算 XY；`Candidate.PlanarDistanceToLedge` 保留检测时的选片/诊断值。`ServerDistanceTolerance` 只放宽远端发起距离，不扩大服务端几何搜索。发起角度使用本次请求方向和重检前沿，不换成收包时朝向。
- PreSelect、Select、Commit 回调后复核原 ASC/Avatar/Component。Commit 后还须核对入场位姿、胶囊、Mesh/AnimInstance、BaseOffset 和参与表面的变换，防止旧落位结果应用到已变化的对象。候选默认时效 `MaxCandidateAge=0.25 s`。
- Supports 与推荐动作分开：Mantle 是登顶，要求前沿；Hurdle 是有后方支撑的翻越，要求前沿+后沿+`bHasBackFloor`；Vault 是没有确认后方支撑的翻越，要求前沿+后沿且 `!bHasBackFloor`。Mantle 可与其中一种翻越同时可选，由 CHT 决定覆盖与优先级；高度/深度/速度不在 Supports 中硬编码。`RecommendedAction` 仅作建议，Hurdle/Vault 各用自己的推荐高度限制，不因 Hurdle 超出建议高度改推有地面的 Vault。
- `bHasBackFloor=false` 表示探测范围内尚未确认可用后方支撑，不证明世界中不存在地面，也不能当作落点。`BackLedge` 在 `bHasBackLedge=false` 时仍保留最后顶面采样位置，坐标与 ObstacleDepth 都不能代替已确认后沿。空中 Height 随当前脚底升高而减小，需结合 StartTime 和选片核对。
- 后方地面即使命中且 `CMC.IsWalkable=true`，仍须满足 `0 <= DropHeight <= DetectorSettings.MaxDrop`（默认180 cm）、Block FloorTraceChannel 及组件可用，才置 `bHasBackFloor=true`；`DropHeight` 从前沿量到采样地面，`BackLedgeHeight` 从后沿量到该地面。`DepthProbeLength` 默认500 cm，探针可量到超过准入落差的地面；`Sampled floor: drop ... max ... walkable ...` 可区分命中与接纳。`AutomaticMaxGroundDrop` 是另一项相对角色地面/脚底基准的自动发起限制，不代替此处 MaxDrop。入口：`IMGTraversalDetector.cpp::ValidateCandidateDirection`、`IMGTraversalTypes.h`。
- 编辑器配置归角色蓝图的 `IMGTraversalComponent` 默认值：`IMG|Traversal|Detection → Detector Settings → Geometry → Max Drop` 控制后方支撑接纳；`IMG|Traversal|Execution → Action Settings → Action=Hurdle → Settings → Landing` 配置通用落地规则，特定动画用同分类 `Montage Settings → Action + Montage → Settings → Landing` 整份覆盖。三组属性均为 `EditDefaultsOnly`，应编辑蓝图组件默认值而非关卡实例或运行时实例；Warp 窗口时间及 Modifier 参数仍在 Montage 的 MotionWarping Notify 中。入口：`IMGTraversalComponent.h`、`IMGTraversalTypes.h::FIMGTraversalLandingSettings`。

## 手点、坡面与表面规则

- GroundLocation 只取 CMC.CurrentFloor 的有效可走支撑；空中或无有效支撑（含无碰撞支撑的 NavWalking）回退当前缩放胶囊底部，不读取 GetGroundInfo 的长距离下探。`bUseGroundPositionForHeight` 默认 false，开启后仍使用同一回退规则；坡面/脚下楼梯只消费有效支撑。
- 手点取前侧命中点上方的中心/左右实际顶部接触，不固定向内平移或投影回前侧平面。`UseRequestDirectionBelowHeight=80 cm` 以下优先请求方向，设0关闭；左右接触连线求交并插值高度，调整范围50 cm，失败再用原始手点/墙面法线重检。此近似不替代双手 IK。`MinFacingDot` 默认 Manual/AI=0.3、Automatic=0.65；HClear/VClear 是净空，不是动画轨迹。
- `AngularSurfaceTag` 默认 `Traversal.Angular`，读取命中组件或所属 Actor 的 FName 标签。请求与入面方向 dot>0.95 时，高障碍也可保留请求方向，失败回退法线。低障碍、棱面或 Automatic 要求双侧支撑；其余 Manual/AI 可追加外侧探针并重定中心，新中心组件须纳入障碍跟踪。标签不引入车辆滑行动作。
- 楼梯由 `StairSurfaceTag=Traversal.Stairs` 指定：前侧/顶部命中带标签，或站立时脚下带标签，启用轮廓过滤；未标记不保证识别。边缘跨度截止值默认22.5 cm、命中楼梯表面时30 cm，严格小于才拒绝，等于可过；跨度<40 cm且水平漂移>10 cm也拒绝。Span 不等于台阶高度，Height 默认相对脚底；独立顶部组件也须覆盖标签。
- `BlockedSurfaceTags` 拒绝全部来源的攀爬接触；`AutomaticBlockedSurfaceTags` 仅拒绝 Automatic 的攀爬接触，两者不禁止后方落脚。Actor Tags 作用于全部组件，局部限制优先 Component Tags。例如 `Traversal.NoAuto` 必须显式加入自动禁用列表才生效；`Traversal.Stairs` 只是启用过滤，不禁止全部攀爬。自动深度限制、高位开放接近拒绝和楼梯下行方向限制只用于 Automatic，楼梯轮廓也用于 Manual/AI。
- 深度断层可能在曲面上形成后沿。`AutomaticMinDepthSamples` 是结束扫描前的最小扫描次数，不是连续顶面通过次数。`Candidate.GroundNormal` 是深度命中的平均法线，不能替代补偿实际命中的法线或可走判定。
- **平台可走坡度归 CMC。** BackFloor、补偿与落地检查使用 CMC.IsWalkable。通用上限为角色 Character Movement → Walking → Walkable Floor Angle（UE 5.8 原生约44.8°，以组件实际配置为准）；局部平台用碰撞组件 Physics → Walkable Slope Override。`MaxClearanceSlopeAngle=42.5°` 只控制净空探针方向。`MaxMovingSpeed=500 cm/s`、`MaxAngularSpeedDegrees=85°/s` 限制载体运动，检测及执行均复核。
- **攀爬与后方落脚通道独立。** 角色蓝图 Traversal 组件的 `Detector Settings → Collision` 下，`TraceChannel` 默认映射 Traversable，负责搜索/手点/后沿及检测层接近/净空；`FloorTraceChannel` 默认 Visibility，负责后方地面查询与接纳。深度采样同时查询两通道并保留最近阻挡，保证障碍即使 Ignore Visibility 仍保留顶面轮廓，地面即使 Ignore Traversable 仍可量到落差；通道相同则不重复查询。连续顶面和后沿仍须通过攀爬资格，BackFloor 则走 `CanUseFloor`，不受攀爬标签限制，仍核对组件有效性、物理/载体速度限制、CMC 可走性及落差。入口：`IMGTraversalTypes.h`、`IMGTraversalDetector.cpp`。
- `IMGTraversalPlacement.cpp` 的选片后落地线探针及 Hurdle 补偿的支撑平面线探针使用 FloorTraceChannel；Mantle 顶面补偿保留原有攀爬资格。完整胶囊支撑扫掠、净空/可选直线路径及 `ComputeFloorDist` 继续使用角色移动碰撞，Hurdle 支撑接纳额外要求 `CanUseFloor`，所以 Block Visibility 不等于角色实际可站立。`FloorOnlyComponents` 记录准备阶段新增的纯落脚载体（含向上补偿加入的碰撞忽略），与执行快照一同复制/回填；选片复检、Commit 后及定时复检据此使用落脚规则，结束随候选/快照清理。同组件原本参与攀爬几何时仍保留攀爬资格要求。`DetectionBegin` 明文记录两通道，Trace/导出事件结构未改变。

### 手部接触表现合同

- `Traversal Hand IK` 为唯一手部入口，运行时和编辑器包装位于各模块的 `Character/Traversal/Animation`。手骨、掌点 Socket、左右 Alpha 和接触参数归节点，Component 只提供执行数据。输入姿势与装配见 [动画](animation.md#traversal-手部表现参考)。
- `ResolvePalmOffsetLocal` 只读取直接挂到对应手骨的 Socket 局部位置，再用当前输入手骨变换得到掌点；None 使用腕骨原点，无效 Socket 关闭该侧。Socket 旋转不参与贴面，节点没有命中后的掌点高度偏移。
- `Left/RightAlphaInputType` 支持 Float/Curve，逐手 Float 默认0。Float 可接 Pin/绑定，先用 `AlphaScaleBiasClamp` 再用 `AlphaScaleBias`；Curve 从当前输入 `Output.Curve` 读取 `AlphaCurveName`，只使用 `AlphaScaleBiasClamp`，缺失曲线或无效值透传该侧。有效结果夹到0–1；修饰器默认直通、插值默认关闭。重复求值继续读当前曲线，但同一次 Update 的时间步长只消费一次。继承的整体 Alpha 默认1，作为独立总控制。
- 执行顺序：`PreUpdate` 在游戏线程读取活动参照、消费上次 Pre-IK 数值样本，并逐手调用 `FIMGTraversalHandContact::UpdateContact`，复制实际命中表面的变换；`UpdateInternal` 记录动画时间步长；求值时 `EvaluateHandAlpha` 读取当前输入曲线，`EvaluateHandIK` 从同份输入姿势采左右掌点，经 `InterpolatePalmTarget` 平滑位置后完整求解，再按逐手 Alpha 在局部空间混合回输入姿势。每侧权重只用一次，位置平滑不再乘该权重；任一侧失效不影响另一侧。求值不读取玩法 UObject 或发碰撞。
- 参照为补偿后的最终执行 Warp Target：`LocalTransform * ComponentTransform` 包含绑定偏移与成功落位补偿。HandIK 固定调用 `FindWarpTarget(EIMGTraversalAnchor::FrontLedge)` 只读执行数据，要求唯一已解析的前沿绑定；不提供额外名称覆盖配置，缺失或歧义时关闭本次手部修正。动画侧 `ResolveContactReference` 核对本地 Montage，模拟代理另核对 ASC 实例；自主代理先消费预测目标再接受匹配权威快照，模拟代理消费权威快照。手点不增加复制、准入或 Montage 前置条件，不回写 Warp Target、落地或移动状态。
- 历史只存在节点的 `FHandState`：动作/AnimInstance/载体/参照局部变换/允许表面集合变化、骨骼缓存重建、LOD 禁用或样本超时均清理。骨名、Socket/局部偏移变更仅清理该侧；Alpha 输入类型/曲线名变更在求值阶段识别，因为 Pin/绑定晚于 `PreUpdate` 更新，不能拿旧来源的探测结果驱动新来源。无参照/样本、专服及 CMC Replay 不探测。权威回填未改变接触参照时保留历史；接触几何参数变化会作废已接纳点和断开标记。
- 竖扫中心为逐手 Pre-IK 掌点 XY、执行前沿 Z；斜坡高低差来自各手实际命中。位置平滑、探测范围、允许修正距离与接触断开距离分责，参数定义在 `IMGAnimNode_TraversalHandIK.h`，默认值如下，长度单位为 cm，时间单位为 s：

| 参数 | 默认值 | 消费位置与含义 |
| --- | --- | --- |
| `AttachBlendTime` | 0.1 | Contact 分类；首次有效求值从当帧 Pre-IK 掌点起步，经 SmoothStep 在此时间内过渡到跟随目标。0立即附着。 |
| `DetachBlendTime` | 0.1 | Contact 分类；探测/目标距离校验失效后，将最后有效手臂修正按 SmoothStep 衰减到当前输入动画姿势。0立即恢复。曲线Alpha仍在最后应用一次，不延迟曲线松手。 |
| `TargetInterpSpeed` | 20 | Contact 分类；实际接触表面局部空间中的目标指数跟随速率（1/s），系数为 `1-exp(-Speed*DeltaTime)`。越大越快，0直接跟随目标。 |
| `VerticalTraceHalfHeight` | 30 | 竖扫上下各延伸此距离，同时用于命中点高度过滤；不是命中后高度偏移。 |
| `VerticalTraceRadius` | 7.5 | 竖向球扫半径，0可退化为线探测。 |
| `MaxHandCorrectionDistance` | 25 | 掌点到候选的三维距离必须小于此值；接纳新点、短时漏探复用和当帧求解前均检查。 |
| `ContactBreakDistance` | 50 | 已缓存点到掌点超过此值便停止当前接触，直至接触历史重置；建议不小于允许修正距离。 |
| `FrontEdgeTraceDistance` | 50 | 前沿横扫长度，同时限制命中点在前沿后的最大深度。 |
| `FrontEdgeTraceRadius` | 5 | 早段前沿细化的横向球扫半径。 |
| `MaxArmReachRatio` | 0.98 | 肩腕距离相对上下臂总长的最大比例；不拉伸骨骼。 |

- 编辑器按输入类型切换对应 Pin/Details，只解除变更侧不再使用的连接和绑定；曲线名校验考虑 Pin 默认值、连接及属性绑定，不仅检查结构默认值。距离必须有限且为正，半径允许0，伸臂比例在(0,1]；断开距离小于允许修正距离给 Warning，避免接纳后下一次更新立即断开。
- 切换逐手 Float/Curve 时，编辑器先选定唯一活动属性，清理该侧其余输入的连接及绑定，再把活动属性的 `ShowPinForProperties[].bShowPin` 置为 true，统一重建一次；之后仍可从原生菜单切换 Pin/属性/绑定。只改 `PinShownByDefault` 不会覆盖旧节点的可选引脚状态（`BlueprintGraph/Private/K2Node.cpp::FOptionalPinManager::RebuildProperty` 恢复旧 bShowPin）。已有 Curve 模式的隐藏 Pin 可用原生菜单 Expose As Pin，或在更新编辑器模块后切回 Float 再选 Curve。
- 逐手类型已复用引擎 `EAnimAlphaInputType`，修饰器复用 `FInputScaleBias/FInputScaleBiasClamp`；支持范围统一由 `FIMGAnimNode_TraversalHandIK::IsSupportedHandAlphaInput` 提供给运行时和编辑器校验。父类只有一组总 Alpha 状态，在 `AnimNode_SkeletalControlBase.cpp::Update_AnyThread` 中求权重，Curve 路径读 `UAnimInstance::GetCurveValue`，并在求值后统一混合全部输出骨骼。逐手权重须分别保存状态，并在 Evaluate 从当前输入 `Output.Curve` 读值，不能直接拿父类的单份字段/Update 求值代替。
- 节点 Details 固定按 `Left Hand → Right Hand → Contact → Solver → Alpha → Performance → Functions → Tag` 排列，Functions/Tag 默认折叠。逐手骨骼、Socket 和 Alpha 合并到对应手分类，不再使用会显示为独立分类的 `Left/Right Hand|Alpha`；位置平滑及接触探测参数归 Contact，MaxArmReachRatio 归 Solver。左右权重显示为 `Left/Right Hand Alpha`，曲线名和修饰器采用相同前缀；总 Alpha 保留原名。名称只定义在 UPROPERTY 的 DisplayName，`FOptionalPinManager::CreateVisiblePins` 每次生成 Pin 时赋予此基础名称，节点仅追加当前模式使用的修饰器表达式，不另写重复 LOCTEXT。不改上述 Alpha 的反射属性名或引脚标识。
- `Persona/Private/AnimGraphNodeDetails.cpp::FAnimGraphNodeDetails::CustomizeDetails` 在节点 CustomizeDetails 之后生成原生属性行及 Pin/绑定、骨骼/曲线控件，跳过 `IsCustomized()` 属性；因此本节点只调整分类、排序和按输入类型隐藏字段，不提前将这些属性搬入自建 AddGroup/AddPropertyRow，避免丢失原生控件。
- 位置平滑仅属于节点表现，不参与 Traversal 移动、Warp、准入或复制。`AttachBlendTime/DetachBlendTime/TargetInterpSpeed` 由左右手共用配置、各自保存历史；Alpha曲线仍独立控制姿势权重与正常松手。首次起点是当前Pre-IK掌点，首次求值不推进附着时间；后续 Update 推进 SmoothStep 附着计时及目标指数跟随，同次 Update 重复求值只消费一次时间步长。入场时长与目标跟随速率均为0时直接使用有效接触位置，退出另由 DetachBlendTime 控制。目标跟随只过滤相对实际接触表面的变化；游戏线程提供数值变换快照，求值不读取该表面组件，也不使用角色组件空间保存跨帧目标。
- `UpdateContact` 的 `bOutNewContact` 在接纳新表面、丢失后重接或几何参数重置后重新接纳时置true，短时漏探复用返回false。新接触、探测失败、当帧原始目标或插值位置超出修正距离时清该侧位置历史；探测失败保留接触算法的断开状态，0.1秒漏探宽限内的有效复用保留位置历史。原始接触点继续独立接受距离和资格校验，无效目标不再参与IK求解。
- 探测失效的 BlendOut 由 `FHandState` 保存数值姿势修正：有效求解后、逐手Alpha混合前，缓存上臂/前臂/腕骨相对输入动画的局部旋转差量。首次失效冻结该修正，计时从0开始，按 DetachBlendTime 衰减并应用到每次求值的当前动画局部姿势，保留当前局部位移/骨长；不缓存失效世界目标继续追点。重复求值不重复推进退出时间。恢复期间重新取得有效目标时，将退出修正与新附着求解按附着进度衔接；衔接中再次失效，从最近实际生成的未加Alpha修正重新开始退出。逐手Alpha只在最终局部姿势混合时应用一次，归零后立即清理，不另延长作者松手曲线。
- 无参照/样本、动作切换、骨骼重建、LOD跳过、样本超时、无效姿势/配置或Alpha来源变化仍硬清全部历史（包含退出修正）；BlendOut只覆盖可继续求值的有效姿势下接触丢失/距离复核失败等表现退出，不跨动作保存状态。
- 接触算法仍保留前0.5秒同组件横扫细化、0.1秒漏探宽限、同表面接触0.1秒后的60 cm/s Z限速；这些与新增位置平滑分责，不构成整段锁点。固定抓点需求应归未来行为自身的锁点策略。新配置须有限且非负，编辑器编译及运行时均校验。
- `SampleHandhold` 的左右顶面命中只用于几何验证、前沿重定与障碍归属，不发布为左右掌点。`FrontLedgeNormal` 是前侧法线，`GroundNormal` 是深度采样均值，`TopSupportHeightRange` 是检测层位范围，均不能替代逐手当前顶面命中。顶面和前立面、左右手可分属同一已验证障碍的不同有效组件。GTA 的0–10 cm上坡偏移发生在目标提交时，IMG不套用该抬高量；探测范围和修正距离仍须独立调节。

## 播放路径与预测能力

- GA 以 `StartSection=NAME_None`、Selection.StartTime 启播。资产 `GetPlayLength()` 不代表本次末端；`FIMGTraversalPlaybackPath` 从 StartTime 所属 Section 沿资产 NextSectionName 解析有限路径，合并相邻段，将前跳/后跳映射到顺序播放时间轴。根运动、DistanceCurve 和窗口筛选只累计实际路径，显式窗口编号保留资产原始序号。循环、无效链或活动 Warp 跨非连续边界在 Commit 前拒绝。
- Motion Matching/PoseMatch返回的StartTime经Selection → TargetData.NetSerialize → ResolveTraversalRequest → PrepareTargets及PlayMontageAndWait原值传递；不按0起播，也不在服务端另选时间。`FIMGTraversalWarpPrediction::Project` 的 Cursor从PlaybackStart开始，窗内起播只Advance剩余区间；SkewWarp的有/无平移分支判定仍用完整窗口位移，与原生TotalRootMotionWithinWindow一致，不能误改成剩余位移判定。DistanceCurve的接触窗末→落地窗末距离是两锚点间距，不应减去StartTime；它与当前胶囊→落地的剩余位移不是同一量。
- 跳过起播前已结束及预计混出后才开始的窗口，窗内起播只算剩余区间；显式提前混出从末 Section 计算。逐段组合窗间根运动和各窗约束，IgnoreZAxis 沿用入窗高度，未约束旋转沿用动画。窗口可共用 Target，单窗/多窗共用预测与修正后复算；骨点参照和原生尾段按窗口缓存。
- 原生 `bSubtractRemainingRootMotion` 扣除的是 Window.EndTime → **资产全长**，实际后续播放位移才按 Section 路径累计。`ResolveWindowPose.NativeTail` 必须匹配原生扣除，不能统一替换为 SectionEnd。DistanceCurve 按路径分段累计，再分别处理接触/落地窗的原生扣余量参照偏移。
- Bone/Static/None 参照将 Target 换算成胶囊位置。有动画平移时按 BaseTranslationOffset 换算中心，关闭 WarpToFeetLocation 则清除该偏移的 Z；无平移时按脚底换算。Target 与胶囊中心不应直接比较。支撑求值只累计至所选接触窗口末端，不累计其后的走位；播放路径及预计混出时间仍用于筛选可到达窗口，不承担动作接触终点的含义。
- 自动补偿的模型支持完整、顺序、不重叠、未限速的原生 Skew 窗口，要求单位 Actor/Mesh 世界缩放、直立胶囊；旋转为 Default/Slerp，时间倍率在(0,1]。非 Warp Modifier 也保留范围，以空 Modifier 标记降级，防止漏算 Scale/自定义效果。超出模型范围时 Mantle、Landing=None 的 Hurdle 保留原目标执行；显式 Landing 仍要求可靠预测。重叠窗口当前不自动补偿，不能把此限制当作动画整体准入条件。`URootMotionModifier_SkewWarp::MaxSpeedClampRatio` 是 protected 且无公开 getter，Placement 用 `FindFProperty` 反射读取（0 表示不限速）；反射取不到时该窗按不可预测处理，Mantle/Landing=None 降级保留原目标，显式 Landing 直接拒绝，故引擎升级后需复核该字段名与语义。

- 已验证重叠执行语义：UE `MotionWarpingComponent.cpp::ProcessRootMotionPreConvertToWorld` 按 Modifiers 数组顺序处理全部 Active Modifier，并不禁止窗口重叠；`RootMotionModifier_SkewWarp.cpp::ProcessRootMotion` 的平移/旋转分支会重新计算并写入对应分量，不能把重叠理解为两个独立终点修正相加。IMG `PrepareTargets` 的 `bProjectSequence` 当前对整个剩余可达窗口链判定；任一受检窗口重叠即关闭本次预测，即使冲突在所选接触终点之后。首尾严格相接（前窗 End <= 后窗 Start）允许；同 Target 或仅旋转/平移职责分离的重叠也未获特殊放行。
- 重叠诊断已接通：`IMGGameplayAbility_Traversal.cpp::PrepareWarpTargets` 调用 Placement 时，Verbose 日志或 `IMGTraversalDebug::IsObserving` 均开启详细诊断；`PlacementPredictionUnavailable`/`PlacementPredictionFallback` 的原因含 `Overlap`、`OverlapWith`、六位小数时间、Target 和轴设置，进入 Rewind Execution Details 与原始导出。当前只描述与前一受检窗口的冲突，不是全部重叠对清单；无观测且未开 Verbose 时，普通可选补偿不构造完整原因。
- 设计建议（尚未实现）：维持普通补偿保留原目标、显式 Landing 预测失败拒绝的边界，以现有诊断辅助手调窗口。若扩展模型，优先研究按所选求值终点裁剪依赖链，及职责独立重叠的严格可预测条件；不可直接忽略重叠、自动截短 Notify 或跳过显式 Landing 校验。重叠资产的实际表现、Modifier 激活顺序与帧步长影响仍需运行确认。

## 高度补偿与显式落地

- 当前补偿实现只调整执行 Target.Z，不改检测锚点、XY、旋转，也不在播放中程序化移动胶囊。反推关系为 `TargetZ = SafeCenterZ - CenterOffsetZ`，允许上下修正；支撑层位容差与修正幅度分开。Mantle 在最后可到达平移窗末检查登顶；Hurdle 有显式 Landing 时用所选落地窗，否则仅在有 BackFloor 平移窗时用最后一个此类窗。Vault 不做支撑补偿；Hurdle 仅绑定沿点且 Landing=None 时保留动画跨越轨迹。Mantle 的资产窗口合同见下节；仅抓沿即结束、其后仍靠根运动登顶的资产不满足当前窗末支撑补偿模型，不能将其跳过补偿解释为重叠或碰撞算法必然错误。
- 普通 Mantle 的补偿是可选修正。平面支撑、完整胶囊扫掠、CMC可走性、归属、层位、边缘容差、重叠及全窗口链复算通过才写回；失败恢复/保留原目标，不追加名义终点贴地硬门槛，全 IgnoreZ 同样处理。`CompensatedPlacementCenter` 只缓存所选求值终点上校正成功的安全中心。降级不证明最终站稳；几何、表面、绑定、RequiredTargets 和有限路径校验仍生效。
- 补偿排查入口：`PlacementEvaluation` 已出现说明接触终点投影已通过；随后 `PlacementPlane` 的 Hit=1 只证明线探针命中。`IsPlacementSupport` 仍要求载体/障碍归属、顶层高度（Mantle）或后方落差（Hurdle）、有效阻挡、CMC.IsWalkable 和表面规则全部通过，否则以 `No usable plane support on the selected layer` 跳过，尚未进入完整胶囊扫掠与 Target.Z 写回。该日志当前不逐项输出失败布尔值或实际可走阈值，不能只凭 Normal 推定具体坡度配置；检测摘要 GroundSlopeAngle 是采样均值，不代表此命中面坡度。若最后平移窗仍对应抓沿阶段，求值点也在该窗末，不会自动等到后续登顶姿态或累计窗后根运动。
- Mantle 始终限定障碍顶层，`Landing.Mode` 必须为 None，不允许实际消费 BackFloor 目标；Vault 同样禁止显式 Landing 和实际消费 BackFloor。未消费的默认 BackFloor binding 可保留。顶层取 `TopSupportHeightRange ± DepthHeightTolerance`，范围来自中心、两侧、首个断层前连续顶面和后沿细化。仅比较组件不足以区分同一车身上的车顶与低层斜面。
- 支撑参照取求值接触窗的几何绑定，修正 Target 则取该窗及之前最后控制 Z 的窗口，二者可以不同。Mantle 接受该接触载体/障碍集的顶层；Hurdle 仅接受接触 BackFloor 载体、位于后沿朝向的后方半空间且落差在 `0..MaxDrop` 的支撑，不借前沿载体或障碍顶面代替后方地面。Hurdle 探高范围由胶囊、接触锚点距离及 CMC/组件可走坡度推导。安全中心保留 CMC 地面间隙，改后复算截至同一接触终点的窗口链，防止同名 Target 影响前窗。向下修正保留独立支撑碰撞，向上修正通过复核后才可加入临时忽略。
- 显式 Landing 仅适用 Hurdle，需 BackFloor 绑定和预计混出前完成的平移窗；None 可保留动画自带落地，不自动补设窗口。补偿、修正后复算、净空/支撑/直线路径与预览胶囊统一在选中Landing窗口末端求值，即使它是最后平移窗也不累计窗后Montage根运动。只选该落地窗及之前最后控制Z的窗口修正Target；之后的高度窗不能反过来补偿先前落点。NativeTail的bSubtractRemainingRootMotion参照仍按引擎资产全长扣除，不能因截断求值而删掉该扣除。较早Landing不让后窗覆盖共享同名Target。CapsuleClearance 只加校验余量；预测、后沿后方可走落点、落差、完整胶囊、实际根位置的后方支撑及可选直线路径不通过即拒绝。普通补偿的“保留原目标”不豁免此合同。结果按载体局部 Target 复制。

## Mantle 窗口制作与优化边界

- 用户确认采用现有窗末求值合同：让 Root 完成上抬、到达 attach 所代表的平台落位阶段后结束最后一个平移 Warp 窗口；不把抓沿后仍需登顶的中间姿态作为站立检查点。此处不是要求胶囊中心与 attach 世界坐标重合，仍须按 Bone/Static Provider、Mesh BaseOffset 与胶囊尺寸换算；也不要求覆盖后续走路尾段或自动选择 Root 最高点。
- UE `RootMotionModifier.cpp` 的 Bone Provider 在 Window.EndTime 采样 Root 相对骨点的 Transform；改变 EndTime 会改变参照关系，窗口变长不等于逐帧手部 IK。资产作者须保证 attach 在所选窗末仍是有效的平台参照，实际接触与运动表现由资产运行验收。
- 当前选定方案保留原生 MotionWarping 与既有 Target.Z 可选修正，不新增独立补偿 ANS、后段 Modifier、时间字段或复制计划。两者若都按作者指定区间修正同一根运动，仅拆出独立标记不构成算法能力提升。后续优化聚焦原模型的计算、适用范围和诊断；短 Warp 加窗后登顶预测仅在出现明确资产需求后另行设计。
- Target.Z 的非零修正会改变接触 Target，相同名称的相关前窗也受影响；即使支撑/完整复算通过，也不能称为未偏移原几何手点的绝对对齐。若要求原手点严格不变，应独立评估该约束与最终站立是否兼容，不通过强行拉长窗口或新增标记声称两者同时保证。
- 待优化诊断：区分无需修正、窗末尚无可用顶面、修正后校验失败、窗口重叠/模型不支持及实际执行偏差；保留原目标降级与显式 Hurdle Landing 的不同合同。不将补偿量非零当作动作成功的必要条件。

## 执行与三端网络

- LocalPredicted 顺序：校验 → Commit → 获取 Target/碰撞忽略 → Flying → 启动 Montage → 提交 TargetData/发布快照。本地即时播放，不等待权威落点。服务端重检来源、方向/意图、几何、Action、时间/倍率、Warp 合同和可选 AllowedMontages，不接受客户端几何；请求不携带检测位姿/时间戳，不回溯世界。TargetData 的 Source/Action 分别按 2bit 序列化，扩展枚举须同步核对 `IMGTraversalTargetData.cpp::NetSerialize`。两端数据结构及配置须一致。
- 启动前只发一次取消；Commit 后重入或播放失败共用退出链。激活/请求栈退出前禁止复用同一 GA 实例；处理时保留 TargetData 句柄，防止同步回调消费 GAS 缓存后悬空。远端等待 TargetData 默认2秒，开始后复用计时器，以默认0.05秒检查目标。
- `WhileInputActive` 在 Spec 不活动时持续尝试。已激活尝试的 RetryInterval 默认0.25秒，**只从本地 EndAbility 计时**；自主代理另等匹配 Avatar/Spec/预测键的权威终止，晚通知只释放门、不重计间隔。等待门/间隔先于 GAS 常规准入和几何扫描判断。ASC.AbilityNetworkTermination 透传可靠 ClientEnd/Cancel/ActivateFailed，含本地已结束实例；远端权威退出须发送终止。

| 角色 | 执行合同 |
| --- | --- |
| 权威端 | 独立重检并计算 Target，复制执行快照；生命周期沿 GAS，移动沿 CMC。 |
| 自主代理 | 本地先执行；仅活动且 Avatar/Spec/预测键/Action/Montage/StartTime 匹配时回填权威 Target/障碍。引用未齐继续预测并在活动 Tick 重试；保留本机 PreviousWarpTargets，版本变化后 GA 重建表面复核快照。晚到/结束快照不启动或结束本地会话。 |
| 模拟代理 | 不选片、不重检几何；按 Montage/PlayInstanceId 及阶段等待快照引用：Warping 等 Target、碰撞忽略和 Flying，GroundExit 等地面模式与权威尾段相位。结束先到时保留已播放实例至退出（含混出），未播放终态实例丢弃，晚引用不得复活。恢复 Target 使用权威历史，非 Flying 到达后先恢复碰撞再查支撑。 |

- Component 在模拟代理按独立句柄注册 ASC Montage 前置条件。ReplicatedMontages 为空时无需预登记 CHT 结果，按动作配置的 Warp 窗口/绑定识别缺快照的待播 Montage；非空列表保留原有精确归属限制。已匹配执行实例按 Warping/GroundExit 分别核对空中执行或地面收尾前置条件；同实例引用补齐不重复拆建，结束快照保留 Montage/PlayInstanceId。
- `CMC.ClientCorrectionFilter` 仅处理 Packed MoveResponse 中仍匹配 SavedMove 的纠偏。GA 只在本方活动且对应 Warping/Flying 或 GroundExit/地面模式有效时过滤启动前已记录的待确认 Move；保留原生 ACK/缓存释放，不应用该次位置/模式/根运动，超出时间戳重置安全窗口则放行。启动后的纠偏和权威拒绝仍有效，不能据过滤推断落位已被权威消费。
- UE 的 ASC.PrePredictionActivation 和 GameplayAbility.PreActivate 已有激活前 FlushServerMoves 路径；沿用它们，不额外发送或增加确认等待。

- 丢包诊断：Traversal 请求沿 GAS 的可靠激活与 TargetData RPC，底层丢包可使重传晚到。CMC 启动后纠偏和 GAS 权威拒绝均可使本机预测回退。画面回拉本身无法判定是哪条消息晚到，按预测键对照服务端重检、终止和移动纠偏。

## 重放、生命周期与退出

- 原生 SavedMove 保存原始动画根运动，MotionWarpingCharacterAdapter 在 bClientUpdating 时再次处理。Traversal 经 CMC 通用 MovementReplayCapture 保留逐 Move 的 setup/restore 闭包；在原生 OnPreUpdate（发现窗口后、更新 Modifier 前）记录世界 Target、SwapState 和本方碰撞忽略，记录/重放不合并 Move。
- 重放临时安装历史上下文，以原生 Modifier 副本计算并屏蔽重复 Notify gameplay 回调，随后恢复当前会话。副本仅首次重放创建，后续重置状态复用，FreeMove/Clear 释放历史引用；无额外 Tick/保活计时器。历史 Flying/退出边界可重放，不强制接管外部 Custom/物理模式。
- 权威终止/取消/拒绝按 Avatar/Spec/预测键退休对应上下文；组件停用、注销、销毁、ASC解绑或换绑同样使其失效。旧 Move 仍可能进入 PrepMoveFor，失效分支须清除属于该历史 Montage 的 RootMotionParams，不能只提前返回。闭包弱引用组件；不承诺回滚世界碰撞、动态载体或自定义 Modifier 外部副作用。
- 动作正常完成仍以 PlayMontageAndWait.OnCompleted（含 BlendOut）为边界，有限动作要求 AutoBlendOut 和有限播放路径。Mantle 在已准备的最后登顶平移窗末交回地面物理，正常尾段保留 Montage/GA；Hurdle/Vault 暂保留原有 Montage 退出移动合同。交接与最终完成是不同生命周期边界。
- **登顶后坡面交接问题及修复范围。** 用户指出原来前后坡上 Warp 完成后仍处于 Flying 的缺口：即使窗末补偿位置安全，后续 Root Motion 尾段沿坡前进时所需地面高度仍会变化。原实现直到 `ReleaseExecution` 才恢复障碍碰撞；当前 Mantle 已在准备出的登顶边界恢复碰撞并请求地面交接，原生 `PhysFlying` 虽有碰撞扫掠，却不执行 Walking 的持续地面高度约束，不能仅靠准备阶段的 Target.Z 修正覆盖后续地面移动。若胶囊在所选补偿终点当帧已穿入，则仍须另查补偿是否生效及预测/实际执行偏差，不能仅凭 Flying 定因。现场个例尚未运行核对。
- 当前 Mantle 交接合同：最后登顶根运动消费完成后撤销本方碰撞忽略，用实际胶囊有限恢复并查近处可走支撑，成功交 Walking/Base；失败交 Falling 并取消/混出站起 Montage，不能报告登顶完成。Component、GA、快照/前置门槛和逐 Move Replay 已按阶段接合；不能任一 Warp 窗末都切换，也不以准备阶段安全中心作运行时传送点。资产和联机表现仍待运行验收。
- 完成、中断、取消、死亡、失效或 ASC解绑均幂等释放 Target、碰撞忽略、Tick依赖、委托和计时器。恢复碰撞后，仅存活且仍 Flying 时按实际支撑切 Walking/Falling；Ragdoll 等接管的新模式不得覆盖，退出无需等待 Montage 自然结束。
- BeginPlay/Activate/重新注册接合 MotionWarping、PawnExt/ASC 和 CMC。Deactivate/OnUnregister/EndPlay 释放接合，期间 IsTraversalAvailable=false，晚快照不重启。拒绝无根运动、非正/非有限 RateScale 和无效检测通道；PawnExt 解绑顺序见 ASC 生命周期。

### GTA 登顶交接映射与决定（Mantle 已接通，待运行验收）

- **依据与边界。** 用户要求先沿 GTA 完整机制逐项映射，再取舍，不能把前一版独立推导的“两阶段+切模式”当作已充分覆盖的实现。已复核 Vault 的 Pre/PostFSM、MoveSignals、Physics、ClamberStand、Falling、IK、打断、附着异常、CleanUp、ClonedTaskInfo 和 CloneFSM，并追到 MotionPed/Ped 的动画Z与地面物理、Player 的 movement breakout。未读取运行时动画包或本工程 Content，不能确认 GTA 各素材的实际标记先后；UE 网络部分按本工程 GAS/CMC 合同重建，不声称 GTA 有 SavedMove 式预测重放。

| GTA 已核实职责 | ImagineGame 对应及决定 |
| --- | --- |
| Climb/Slide 才调用 ScaleDesiredVelocity；ClamberStand 不再补偿 | 登顶阶段结束后停止 Warp 移动接管，保留动作会话。 |
| EnterEndVault 信号进入 ClamberStand，EndClip 有自己的相位 | 映射成当前 Montage 内的地面收尾阶段；用户已确认最后登顶 Warp 窗末可承担阶段边界，不新增整套 MoVE/FSM 或另播一段动画。 |
| ClamberStand 另按 AdjustAnimVel 相位恢复碰撞，未附着才恢复重力 | 停止补偿与物理交接职责独立；当前用户素材允许二者同点，最后登顶根运动消费完后恢复本方碰撞忽略并交 CMC，不能泛化为所有动作/资产同点。 |
| IsLanding 启用地面约束；SlopeNormal 交给腿IK | CMC Walking 负责查地、AdjustFloorHeight、沿坡移动和 Base；脚/骨盆表现沿既有 FootPlacement，不用 IK 替代胶囊落地。 |
| ClamberStand 继续 AnimatedVelocity，停止消费动画Z后仍可消费XY | 保留 Montage 水平根运动并由 Walking 沿坡执行，不能在交接时把根运动或水平速度全部清零。 |
| 无支撑进入 Falling，创建 CTaskFall 接管空中/落地 | 作为登顶失败/失去支撑出口，交 CMC Falling 及既有空中表现；不得强行站立，也不得无条件继续站起尾段并报告成功。无需复制一个 RAGE Fall 任务体系。 |
| Detach 是独立相位，附着期间不恢复重力；载体异常/净空丢失可脱离或Ragdoll | 当前 IMG 没有这套物理 Attach 合同，不能照搬标志；落地建立 CMC MovementBase，退出空中目标跟随，避免双重平台位移。继续核对已有动态载体准入/失效，不据静态坡面修复宣称完整 GTA 动态攀爬完成。 |
| 手臂、腿、骨盆分别受动画信号控制，腿有落地混合覆盖 | 已有手Alpha、DisableLegIK/contact、FootPlacement 各自映射；地面交接不等于强制全开IK。资产时序列入验收，不增加逐脚接触任务或复制IK。 |
| EnableHeadingUpdate、MovementBreakout、Interruptible、AimInterrupt 各管不同控制窗口 | 物理落地、朝向接管、输入打断与能力互斥不合并。本次保持原GAS控制/互斥规则；提前输入/瞄准打断是明确未移植差异，不能因切Walking就一并开放。 |
| 退出按 Stand/Walk/Run 和实际速度交接；Fall 显式传递步态避免MBR复制晚到 | 保留 CMC 实际速度与既有Locomotion步态/输入合同，验收尾段结束不急停、不错误重启冲刺；不复制第二套步态或写死退出速度。 |
| ClonedTaskVaultInfo 同步 State 与起点、手点、速率、载体；Clone继续执行FSM | 现有 Traversal 快照同步阶段与执行参照，GAS同步Montage，CMC负责权威移动/预测纠偏；UE模拟代理不额外运行本地GA。此项是网络职责映射，不是复制RAGE Clone协议。 |
| Owner任务消失后已开始的Clone可继续到可退出点；Init/后续新动作有例外 | 保留已播模拟代理的可见尾段与终态不复活规则，同时立即按权威地面阶段撤销物理接管，不能为保留动画延迟碰撞恢复。 |
| CleanUp 恢复碰撞/重力、脱离、Heading、IK覆盖、动画网络、武器/瞄准临时状态 | 按 IMG 实际取得的资源对称清理，交接后清理不覆盖新移动模式；GTA专属武器/镜头/SoftAim策略不迁入共享Traversal。 |

- **源码入口。** 上表对应 `TaskVault.cpp:404,581,647,678,766,794,1847,1914,1986,2033,2376,2781,3263,3442,3550,4237,4325,4379`，`TaskVault.h::CClonedTaskVaultInfo::Serialise`，`TaskMotionPed.cpp::CalcDesiredVelocity`，`Ped.cpp::SetUseExtractedZ/ProcessProbes/ProcessPedStanding`，`TaskPlayer.cpp::Jump_OnUpdate`。GTA 的 ClamberStand 使用 EndClip，恢复碰撞的标记相位不是前面 StartClip 补偿窗口的同一个时间值；先前将这些时点压成一个 Released 事件不够完整。
- **取舍权重。** 最高优先级为完整覆盖上述正常/失败/清理出口，以及权威端、自主代理、模拟代理和历史重放一致；不满足即排除。其次是保留当前资产合同、CMC移动职责与现有IK/输入所有权；最后比较新增配置、标记、状态和维护成本。这是本工程取舍顺序，不是 GTA 注释提供的量化评分。
- **决定。** 采用 GTA 的“爬升执行→地面收尾→正常退出；失去支撑→下落；外部接管→取消清理”职责模型，映射到现有 Component/GA/CMC。当前 Mantle 资产把停止补偿与恢复地面物理两个触发点合并到最后登顶 Warp 窗末，但控制恢复、IK恢复、动作完成仍各守自己的合同。排除继续Flying到整个Montage结束、继续加Target.Z修正代替地面移动，以及移植RAGE站立弹簧/附着/Clone整套实现。阶段名称、复制字段、纠偏通知接口是实现细节，不能仅凭 GTA 状态同步就宣称已解决 UE 预测。
- **触发与时序。** Placement/PlaybackPath 解析已选登顶窗口及有限Section路径中的终点，独立于补偿量/预测降级；不跨帧存临时窗口指针。Component 在当前Move根运动和 scoped movement 完成后的 `OnCharacterMovementUpdated` 执行交接，不在 MotionWarping OnPreUpdate 提前切Walking、不用世界定时器。窗内起播、跨Section、混出、变速、大DeltaTime跨界均纳入核对；若一个Move合并尾段造成不可接受的偏差，需拆分该Move，不能声称亚帧精确。
- **正常交接。** 恢复本方碰撞忽略→按实际胶囊检查近处可走支撑/净空并进行通用受限穿透恢复→建立Walking/Base；继续Montage水平根运动和既有姿势控制。GA/会话仍活动，最终Completed/Interrupted/Cancelled负责清理。RefreshWarpTargets与ignore所有权拆开，地面收尾后不能因刷新或晚到Target再次忽略平台。目标失效由接触表现自行降级；实际MovementBase失效则由CMC进入下落出口。
- **失败与中断。** 无有效支撑、支撑丢失、无法安全消解的穿透均不得伪装为登顶成功；恢复碰撞后交Falling/现有空中表现，站起Montage按失败出口取消或混出。死亡、Ragdoll、Custom/None、ASC/Avatar失效继续沿原能力取消与资源释放路径，不用通用落地修正覆盖它们。失败处理不传送到准备时安全中心、不无界搜索别层支撑。
- **网络和重放必须一起完成。** 同执行阶段更新保留已有实例身份，阶段接收与AppliedAuthoritySequence的Target去重解耦；不加第二套位置/速度/MovementMode或交接RPC。自主代理预测、服务端独立判定；CMC已接受响应对应TimeStamp/SavedMove的权威模式/根运动相位是纠偏锚点，OnRep不能把历史阶段直接强加当前帧。SavedMove闭包记录边界、播放路径及碰撞所有权，在临时上下文中重演过渡并重新查实际支撑；锚点已交接时旧Move不能强制Flying，锚点在交接前则允许合法回退重演。重放不发业务通知、RPC或权威快照。CMC只在必要处提供通用已接受纠偏通知，不包含Traversal业务类型。
- **模拟代理和终态。** 按权威阶段分别等待Warping所需Flying/Targets/ignores，或地面收尾所需模式/当前Montage尾段相位；阶段、模式、Montage、引用允许乱序，不要求已交接实例重新进入Flying。Component→CMC Tick依赖保证先恢复碰撞再查地。已播实例可正常混出，未播终态不得启动，快速重复同Montage不得串实例。
- **实现覆盖与验收。** Placement/PlaybackPath输出边界，Component持阶段/资源/快照，GA处理阶段有效性和正常/失败出口，Replay处理历史模拟，CMC执行地面/下落及必要通用接口。必须覆盖静态前后上下坡、当前允许的动态支撑及Base交接、最后一帧/低帧率/高播放速率、浅穿透/无支撑/深穿透、IK曲线与输入窗口、尾段速度连续性、交接前后取消/Ragdoll、专服/Listen Server、纠偏跨界、消息乱序、引用晚到、远端终态与重复播放。资产具体标记和联机表现仍待用户运行验收。

### 实施步骤与后续拆分（当前 Mantle 已落代码）

- **执行状态。** 用户已发出开始信号。当前代码已接入 Placement 基础计算、Mantle 地面交接、三端阶段与纠偏重放；步骤5的资产/运行验收由用户执行，步骤6的梯子等独立行为仍待后续任务。
- **长期边界。** 共用目标是“为行为找到满足胶囊和支撑约束的落位，并把实际运动交回地面”，不是所有行为都改用同一 Warp、同一轨迹或结束时瞬移胶囊。准备阶段 Placement 补偿、行为执行期间的目标位移、运行时实际胶囊地面交接和最终动作清理是四项独立职责。普通楼梯沿 CMC Walking/StepUp/地面跟随；梯子、悬挂退出等特殊行为才需要自己的退出合同。

| 职责 | 当前落点与后续边界 |
| --- | --- |
| 安全落位的几何求解 | 从现有 Placement 识别可共用的支撑/完整胶囊扫掠、净空、可走性和地面间隙计算；显式接收胶囊形状、参考位置/参照体、搜索范围及表面约束，不反查行为类型。 |
| 行为的补偿适配 | IMGTraversalPlacement 保留 Montage/窗口投影、Bone/Static Provider、Target.Z反推、全链复算与降级合同；Mantle顶层、Hurdle后方、未来梯子出口的资格/层位规则分别归来源。不能把整个 PrepareTargets 当作所有行为的通用接口。 |
| 行为执行 | Mantle消费Warp根运动，未来梯子消费梯档/挂载/退出相位，其他行为按自身模型生成移动；行为独立拥有生命周期。 |
| 实际地面交接 | 在行为指定的安全退出阶段恢复本方碰撞约束，按实际胶囊有限消解误差，交 CMC Walking/Base 或失败下落；来源决定何时请求、可接受支撑和失败策略，CMC提供通用移动执行。 |
| 网络与表现 | 位移继续沿CMC预测/纠偏，每个行为保留自己的阶段与历史上下文；共用必要通用入口，不新增全局行为调度器。IK、输入恢复、动画结束各守原合同。 |

1. **固定合同与改动清单。** 以本节前面的GTA映射为基线，明确登顶窗末、正常地面收尾、无支撑/穿透失败、外部接管和最终清理各出口；保留可选Placement补偿降级与显式Landing严格校验差异。交接时间从行为准备结果取得，不绑定补偿量是否非零。
2. **整理Placement计算边界。** 保留现有有效算法，先分清“可复用的胶囊/支撑几何计算”与“Traversal的Warp预测/Target修正”；必要提取只形成无状态计算接口，不先建设总Placement组件。下一消费者出现后，再依据真实共同输入提升共享层级，不引入GTA无对应证据的统一运动模型。新增struct的具体头文件仍按工程约定在实际落地前确定。
3. **接通Mantle完整生命周期。** Placement产出登顶边界，Component在最后登顶根运动消费后交接；同步修改碰撞ignore刷新、GA阶段有效性、地面/下落失败出口、动态Base交接、尾段速度及取消清理。准备补偿负责到位，运行时交接只处理实际状态和有限残差，不能回写第二轮整链补偿或把预估位置当作执行事实。
4. **同期完成联网与重放。** 按现有实例身份扩展阶段快照，分开Target与阶段消费；接通自主预测、服务端独立执行、CMC纠偏锚点、SavedMove跨界重演，以及模拟代理的分阶段起播/尾段/终态。必要通用CMC接口不反向依赖Traversal。诊断随本步骤加入，不等运行失败再补观测。
5. **静态收口与用户运行验收。** 核对接口、访问权限、依赖、重入、资源对称及三端时序；用户验收平地/前后上下坡、当前允许的移动支撑、低帧率/窗内起播/跨Section、补偿成功/零修正/降级、无支撑/浅深穿透、尾段速度与IK/输入窗口、取消/Ragdoll、专服/ListenServer、延迟丢包/纠偏跨界/乱序和重复Montage。按现有约定不编写测试代码、不自行编译或修改Content。
6. **后续接入第二种行为再抽取。** 梯子先独立实现检测、占用、攀爬和上下端退出，只有其落位几何和地面交接与Mantle确实共用的部分向共享执行层提取；梯档对齐、出口选择、动画/控制相位、载体和联网会话继续归梯子。悬挂、绳索等按同一职责边界逐个适配，不能把有限Montage/Flying合同推广到所有持续行为。

### Mantle 地面交接的当前实现

- 模拟代理 GroundExit 起播从项目 ASC 的 `GetReplicatedMontagePosition` 取得匹配 Montage/PlayInstanceId 的权威位置；未取得有效位置则保持等待，不直接访问引擎 protected 复制状态接口。
- `IMGTraversalPlacement::PrepareTargets` 输出独立的 GroundHandoffTime；Mantle 用已选 FinalTranslationWindow 的路径终点，不依赖补偿是否非零或预测模型能否补偿。窗口必须在预计播放结束前可完成。原 Target.Z 反推、全链复算、可选降级与 Hurdle 显式 Landing 合同保留。
- `IMGGroundPlacement` 目前只提取 PlaneAdjustment、IsUsableSupport 等共用计算，未把整个 Montage PrepareTargets 泛化。FindGround 面向当前 CMC/胶囊，沿角色碰撞过滤器查询、调用原生穿透恢复并限制总位移，最终复查完整胶囊和近处可走支撑；失败回滚本次恢复。失败还恢复原 bJustTeleported，支撑距离使用原生 GetDistanceToFloor（含 LineTrace 回退）。默认 MaxGroundAdjustment=10 cm，在 Execution Settings → Ground Handoff 配置，仍按 Action/Montage 整份覆盖；不是额外 Warp 抬高量。
- Component 的 `OnCharacterMovementUpdated` 在当前 Move 的根运动执行后解析数值播放路径，进入 GroundExit 并恢复本方 ignores、Walking/Base；失败进入 Falling，向本 GA 报失败并取消动作。GroundExit 仍在会话中，目标可供手部只读消费，但不会重加 ignores 或继续用障碍准入限制地面尾段；实际失去支撑也走失败下落出口。交接查询期间以原 ExecutionId 临时抑制重新加入 ignores，直到恢复提交前仍保留 Flying 清理所有权，覆盖 overlap 取消/重入。Montage Completed 若先于当次物理结束，只排队完成，待最后根运动和地面检查完成才结束 GA；重放完成交接后在实时移动更新补交完成通知。帧跨界在该 Move 末处理，尚无亚帧拆分；低帧率/高播放速率造成的偏差须运行验收。
- 现有 FIMGTraversalRepState 增加 MovementPhase（Warping/GroundExit/Falling）、GroundHandoffTime、MaxGroundAdjustment；阶段更新不换 Sequence，不增加位置/速度/模式副本或交接 RPC。自主代理接收权威边界但不在 OnRep 直接套历史模式；模拟代理按阶段等待 Flying 或实际地面模式及权威尾段播放位置，晚 Montage 引用重试，不退休有效实例。终态保留交接阶段，允许已播放尾段完成，但碰撞不能随尾段延迟。
- CMC 只增加通用 `ClientCorrectionApplied` 通知：Packed 纠偏实际被接受、对应 SavedMove 且 ACK 推进后才广播，先让原生应用位置/模式/根运动。Traversal 按捕获的 Move→ExecutionId 及 Montage 核对，再用权威模式建立重放交接起点。启动前旧 Move 的现有过滤延续到有效 GroundExit，交接后的正常纠偏不屏蔽。
- Replay 在旧闭包中保留数值路径、边界、恢复上限及本方碰撞所有权，通常使用原生 SavedMove 的重算后 RootMotionTrackPosition 判断跨界；仅在 Montage 已于该 Move 完成、原生不再保留 Montage 引用时使用原 Move 记录的交接标记兜底，这不重建引擎未保存的末帧根运动，仍需最后一帧纠偏验收。已交接锚点不强制 Flying；纠偏回登顶前时可从 GroundHandoffIgnores 取得原属本次动作的忽略集，再重演交接。临时 ReplayPostMovement 不广播玩法失败/发布网络，按实际修正后支撑决定模式，恢复时不撤销物理结果；GA 在实时帧接收失败出口。每次捕获先移除池化 SavedMove 的旧执行身份，正常尾段 Move 即使 Montage 引用已消失仍以捕获身份接收纠偏锚点。新 Begin/解绑清历史索引与弱忽略集，原 Session 退休/失效规则继续有效。
- 新诊断事件：GroundHandoff（路径相位/边界、实际中心、支撑/法线、失败原因）、GroundHandoffFailed、GroundHandoffCorrection（已接受 Move/模式）、ReplayGroundHandoff；快照 State 文本含阶段/边界/恢复上限。事件沿既有 Trace/Rewind/JSONL，不改 Trace 结构版本；Rewind 显式将交接及失败归 Execution。

## 性能与诊断

- 按频率与查询类型计量：检测含分层球扫、顶部/两侧、深度、后沿细化和净空，不是固定几条射线。默认最多6层搜索、5次深度采样、4步细化；一次 Detect 最多尝试12组边缘候选，提前退出/方向回退改变查询数。成功发起通常本地 CanActivate/Activate 各检一次，服务端另检一次，模拟代理不检。
- 按住且无表面时，预筛失败不会激活/EndAbility，故 RetryInterval 不限制这条逐输入帧扫描路径。补偿在已选片的启动准备阶段每次算一次，失败尝试也可能计算；执行中不重算整链，但活动 Tick/OnPreUpdate 刷新目标、定时复核表面、自主代理逐 Move 记录历史，纠偏时才重放。
- 测量须分开探测、选片/骨骼采样、执行、历史记录/重放及 Debug。关闭绘图/日志后量本体；源码计数不等于实测耗时，多客户端同进程 PIE 总成本不等于单角色预算。
- 调试通道、快捷键和录制合同见 [Traversal 调试](traversal-debugging.md)。几何显示开关仅作用于表现；记录与绘图独立，文字仅进Rewind Details。探针球/胶囊显示真实尺寸，点标记大小不再缩放查询体积。记录的 ElapsedMs 包含当次 Debug 开销，只可比较相同采集配置；ExplicitQueries 计项目显式发起的碰撞查询，不包含 CMC.FindFloor 等引擎内部查询。
- 源码诊断用 `Log LogIMGTraversal Verbose`、`Log LogIMGAbilitySystem Verbose`，结束恢复 Log；窗口原生位移用 `a.MotionWarping.Debug 1`。按角色/连接、Spec、激活 PredKey 关联，事件含 Loc/Velocity/MoveMode。LocalRequestPrepared 与 ServerDetectionRejected 对照起点和意图；同键两端各一条不等于重复执行，不同键/键号跳号不能证明输入重复或丢包。

| 事件/标记 | 应如何解释 |
| --- | --- |
| WarpTargetsRejected，Local=1/Auth=0/Started=0/Sent=0 | 本地启动前拒绝；Sent 是 Traversal TargetData，不是 GAS 激活 RPC，不能直接归因延迟/回滚。 |
| PlacementPredictionFallback / PlacementCompensationSkipped | Verbose 正常降级，保留原目标；Plane/CapsuleSupport 提供失败细节，不按 Warning 刷屏。 |
| Overlap / OverlapWith | 同一播放路径内的窗口时间重叠。比较两窗六位小数起止、目标及 Translation/IgnoreZ/Rotation，不代表重复输入。 |
| ServerDetectionRejected / NoSurface | 请求已到服务端，当前几何检测失败；需查两端位置/时序/碰撞。Reliable RPC 也不能证明底层无丢包，须另测重传/延迟。 |
| AuthorityEndedPredictedTraversal | 同键 GAS 权威终止先于本地完成，不是 CMC 重放或预测错误的单独证据。PredictionRejected 才表示激活拒绝。 |
| Safe capsule / PlacementReprojectionRejected | 仅证明模型支撑/复算结果；缺少拒绝日志不证明进入过复算。全链复算只在预测可用且高度修正非零时执行。 |

## GTA 对照与已知边界

- **梯子进一步佐证“独立位移模型，共用地面职责”的边界。** `TaskLadderClimb.cpp::ScaleClimbToMatchRung` 按梯档与动画相位生成DesiredVelocity，`DriveTowardsPerfect` 用动画位移与理想挂载变换求速度，两者不是Vault的DistanceDiff补偿。`StateAtTopDismount_OnEnter` 关闭碰撞，Update在动画IsInterrupt信号恢复碰撞，随后关闭ExtractedZ并设Standing；底部与滑降退出在目标动画状态就绪后恢复碰撞/重力，置IsLanding使胶囊地面弹簧阻尼生效，同时请求腿IK。滑降入口另衰减原有向下速度，注释明确防止猛烈砸地；CleanUp恢复碰撞/重力并释放MP梯子占用及导航阻挡对象。源码入口约为`:388,2399,2429,2498,2898,3532,3575,3628`。这些证明行为各管自身运动和退出时机、地面机制消费交接事实，不能据此宣称GTA所有行为共用同一个Placement类，也不能把挂载对齐函数误当作登顶时统一贴地补偿。
- **GTA 在任务结束前恢复地面约束。** `TaskVault.cpp::StateClamberStand_OnEnter` 取当前 Clip 的 `AdjustAnimVel` 相位作恢复碰撞时机（无标记默认0.5），`StateClamberStand_OnUpdate` 每帧置 `IsLanding`，注释明确为启用强制落地代码及提前控制；超过该相位恢复碰撞，未附着时 `SetUseExtractedZ(false)` 交回重力。`Ped.cpp::SetUseExtractedZ` 实际控制 NO_GRAVITY，MotionPed 同时据此停止消费动画 Z。`Ped.cpp::ProcessPedStanding` 将 IsLanding 视为站立候选，非 ExtractedZ 条件下执行地面弹簧/阻尼，下降时还有防止入地的落地阻尼分支。ClamberStand 同时仍强制 AnimatedVelocity，说明尾段动画水平位移可继续，竖直已交给地面/物理；无支撑则转 Falling，后者入口恢复碰撞和重力。它与“登顶后不等整个动画结束就交回地面移动”职责对应，但不是 UE Flying/Walking 的直接模式映射，也不是恰好补偿窗末自动切换。入口：`TaskVault.cpp:581,1986,2033,2147,1847`、`Ped.cpp:10508,16408,16545,16650`。
- **GTA 补偿停止条件。** `TaskVault.cpp::ProcessPhysics` 仅在 Climb/Slide 调 `ScaleDesiredVelocity`，ClamberStand/ClamberVault 不调用；`ProcessMoveSignals` 通过 `EnterEndVault` / `EnterFall` 动画网络信号转入这两个后续阶段。每轴修正还受调整相位区间约束，未发现以 Root.Z 达到峰值或竖直速度变号来停补偿的条件。`:1553` 的“延长至相位1”只适用于仍处于 Climb 的当前 Clip，不能理解为整个翻墙任务直至落地都持续同一补偿。是否通常在视觉最高点附近切阶段须核对 MoVE/动画资产；当前快照缺少运行时动画包，尚未验证。
- GTA 路径相对 `src/dev_ng/game`：检测/手点 `task/Movement/Climbing/{ClimbDetector,ClimbHandHold}.*`，发起 `task/Default/TaskPlayer.cpp`，Jump/Vault 分流 `task/Movement/Jumping/TaskJump.*`，执行/清理 `task/Movement/Climbing/TaskVault.*`。GTA Clone 同步不等同 UE 的 CMC Replay。
- CTaskVault 按手点、DropPoint、GroundToRootOffset 和动画相位求目标位移，`CalcTotalAnimatedDisplacement`、`SetDesiredVelocityAdjustmentRange` 分别处理动画位移/调整区间。`ClimbDetector::ValidateHandHoldAndDetermineVolume` 调 `CTaskVault::IsHandholdSupported` 校验出口，不能把几何通过等同动画可落位；IMG 分层落实为选片后绑定、可选补偿和显式 Landing 合同。
- 已验证 GTA 位移与接触分别处理：`TaskVault.cpp::CalcTotalAnimatedDisplacement` 从 StartScalePhase 提取至相位1的 mover 位移并按活动双 Clip 权重混合；`ScaleDesiredVelocity` 将 TargetDist 与该总位移之差按各轴调整区间添加到动画速度，`ProcessPhysics` 在 Climb/Slide 消费。`StateClimb_OnUpdate` 在混入或超过初始调整区间时重算剩余位移，并可把后续调整延长到相位1；因此不是只对齐手点后永久冻结后段位移。`ProcessHandIK/UpdateHandIK` 由动画信号启停，向 Arm IK 提交绝对或载体相对目标；`ComputeIsOnGround` 另查实际支撑。这些是本地源码事实，不代表该快照所有资产的配置或发行版实际行为，也不将其整体视为精确接触/必定落位证明。
- **GTA 普通翻墙的移动来源。** `TaskVault.cpp::StateInit_OnEnter` 设置 `SetUseExtractedZ(true)`，`ProcessPreFSM` 强制 `MotionState_AnimatedVelocity` 并请求直接应用速度；`TaskMotionPed.cpp::CalcDesiredVelocity` 的 AnimatedVelocity 分支将提取的动画 XYZ 速度转为世界速度，`Ped.cpp::ApplyMovementRequests` 后的任务 `ProcessPhysics` 再执行 Vault 的速度修正。主路径属于动画 mover 位移驱动加程序目标修正，不是先把胶囊放到平台终点、再靠 Mesh 反向偏移播放 InPlace。RAGE 的 mover 轨道不等同于 UE 某一根骨的具体存储格式；抽取位移后局部姿势看似原地，也不能据此判为 InPlace 驱动。IMG 同样依赖 Root Motion Montage（GA 校验 `HasRootMotion`），Placement 只改 Warp Target.Z，Component 切 Flying 后沿 Montage/CMC 执行；两者职责方向接近，算法与碰撞合同不同。
- **GTA 中看似预移动的例外。** `TaskVault.cpp::StateInit_OnEnter` 的 Clone 分支可纠正远端起始位置/朝向，纠正目标是动作起点，不是预先登顶。`StateClimb_OnEnter` 对动态物体以当前 Ped 位置反变换得到 AttachOffset，虽然带 `ATTACH_FLAG_INITIAL_WARP`，不能仅凭该标志判断先抬胶囊；后续 `ProcessPhysics` 用本帧动画/修正速度积分更新 AttachOffset。`ScaleDesiredVelocity` 的 Slide 特例注释明确要求 XY 由代码插值以避免穿过 slide point，实际在 Climb 且 `m_bSlideVault` 时清零动画局部前向 y；不能据这一特例推断普通翻墙全部 InPlace，或所有 Slide 轴都无动画位移。
- **GTA 方案相关注释（局部实现理由）。** `TaskVault.cpp:524` 为准确到点并避免碰撞而禁止攀爬 AI 分时更新；`:1528` 为抵消混出动画带入的速度而在混入期间重算补偿；`:1553` 初始调整区间结束后允许继续修正至 Clip 相位1；`:1562` 只在完全混入后关闭静态攀爬碰撞，避免额外动画速度导致穿墙，附着分支不这样关闭；`:3176` 是上述 Slide 代码插值理由；`:3185` 说明 Clone 起播相位偏后可能穿过障碍，因此特定混入条件下暂不采用动画前向速度。`TaskVault.h:144` 与 `ClimbDetector.cpp:2656` 明确动画种类有限，检测通过还须执行任务确认资产可支持。上述是源码明文理由，不是“Root Motion 优于 InPlace”的完整选型文档；本次核对的 Vault/ClimbDetector、MotionPed、Ped 及 mover 提取链未发现直接对比这两种总体架构的说明。
- 手部静态对照（`C:/Application/gta5`）：`TaskVault.cpp::UpdateHandIK` 以动画 IK helper 的 XY 发起竖扫，实际目标取命中点；Climb 漏探有保留旧 Z 的分支，本地玩家早段还会尝试横扫以避免误抓后沿，超出漂移/距离阈值会停止提交或拒绝更新。因此“动画 XY、环境 Z”是主要意图，未严格锁定 XY。目标按 helper 到腕骨的差量换算；Vault 未请求目标朝向。`SetHandIKPos` 能保存载体局部点，但 `GetHandIKPosWS` 和提交端在物理引用为空时都直接消费保存值，未见独立的局部点失效门，不能推断载体销毁会自动清掉手部接触。
- `TaskVault.h::tHandIK` 左右独立，`ProcessHandIK` 共用 MoVE Enable/Disable 信号，按左右 `IK_HAND` 分别调用。`GetBonePosition` 读取已有骨架全局矩阵，可能是本帧或更早缓存（`Ped.cpp`）；并非保证当帧最终动画姿势。`CShapeTestCapsuleDesc::SetIsDirected(true)` 的实际类型是 Swept Sphere。竖扫中心 Z 为通常动作的 Handhold Z 或 Slide 的 helper Z，默认上下各 30 cm、半径 7.5 cm；各手有各自的命中 Z，故可体现斜面高低差。坡度加高则是共用的 `GetHandHoldGroundAngle`（`ClimbDetector` 的上坡清障方向角），默认 0–45° 对应 0–10 cm，应用在目标提交空间的 Z；不消费各手法线来旋掌。`HitNormal.Z < 0.1` 只参与已取得目标的 Climb 漏探回退，不能理解为所有首次命中的准入条件。
- GTA Climb 腿部链已静态核对：`TaskVault.cpp::ProcessMoveSignals` 用 MoVE `DisableLegIK/EnableLegIK` 维护 `m_bLegIKDisabled`，Climb 相位>0.9也尝试启用，但同次后续 Disable 信号仍可覆盖；任务逐帧提交 `PEDIK_LEGS_AND_PELVIS_OFF`，由 `CIkManager::CanUseLegIK` 和通用 `CLegIkSolver::BlockLegIK` 消费。没有在该任务中发现手部那样独立生成、提交逐脚接触目标的路径。
- 腿 IK 恢复与骨盆分开：TaskVault 使用 `SetOverrideLegIKBlendTime`，普通默认0.25秒、滑行动作的 ClamberVault 默认0.1秒；通用 `LegSolver.cpp` 用该时间的倒数作为腿/骨盆混合速率，实际仍受脚落地、禁用和即时初始化条件约束。`m_bPelvisFixup=false` 时任务请求 `RequiresLegIk`，`CanUseLegIK` 在 IsVaulting 下允许腿而令 `bCanUsePelvis=false`；Clamber 阶段的 `EnablePelvisFixup` 信号可恢复骨盆参与，退出 `CleanUp` 重置混合时间覆盖。
- ClamberStand 逐帧把 `GroundNormal` 写入 `SlopeNormal` 供普通腿 IK 使用。通用 `CLegIkSolver::GetShapeTestIncludeFlags/CanUseAsyncShapeTests` 取 GroundPhysical，缺失时用 ClimbPhysical，补足车辆/物体等碰撞类别；载体在运动或水中时选同步探测（Partial 模式先走异步分支）。因此这里是通用腿/脚/骨盆求解器配合攀爬的启停、混合、骨盆和载体适配，不是完全未经适配的普通行走 IK，也未证实存在攀墙踩点求解器；动画包未核对，不能推断素材内不存在其他脚部约束。
- 每手首次目标独立记时及锁存载体，后续更新到同一载体空间；普通动作取得目标 100 ms 后每次最多修 1 cm 的 Z，XY 仍可更新，Slide 不进入这条限速分支。Climb 的本地玩家前 0.5 s 可从前向外 50 cm 回扫，半径 5 cm，同实体且在角色前方时插值两次命中并取较高 Z。候选到 helper 必须小于 25 cm，已缓存点相对 helper 漂移大于 50 cm 时该手停止提交。两侧独立淡入/淡出，但没有独立的作者接触曲线或固定掌点保证。数值是快照默认值；按帧限速和缓存空间降级不作为 IMG 的表现合同。
- 锁手复核：在 `CTaskVault` 中，`bDoTest` 默认 true，搜索到的 Vault 源码仅初始化和读取它；`vInitialPosition` 只在初次设点时保存，`GetHandIKInitialPosWS` 无调用，实际提交当前 `vPosition`。不接受新点时可继续提交旧值，超距结束则停止提交；两者均不构成主动的撑手锁定窗口。相邻 `CTaskDropDown::UpdateHandIK` 按动画选择一侧，每次有效扫中仍重写世界目标，漏探可重用旧值。梯子任务源码主要对齐梯档与角色位移，未见 Vault 式 Arm IK 提交；MoVE 资产是否另有接触约束未核对。
- `Ped.cpp` 的 `PreIkUpdate` 和 `StartUpdatePreRender` 进入动画更新，`crmotiontree/dependency_ik.frag` 执行骨骼求解，`PostIkUpdate` 清除该帧 Arm IK 活跃请求。`IkManager.cpp::ProcessSolvers` 的评分和池分配针对 proxy，Arm 分支直接检查自身池并构造求解器。可参考每帧消费当前姿势、目标失效时退出等行为，工程实现遵循上方表现合同。
- 标记职责映射已核对：GTA `StateClimb_OnEnter` 直接读取 Clip 的 `AdjustAnimVel.GetStart/GetEnd`，传给 `SetDesiredVelocityAdjustmentRange`；它是 Climb 的位移调整区间，不特指手点对齐后的第二补偿阶段。UE 的 `UAnimNotifyState_MotionWarping` 本身就是区间标记，`OnBecomeRelevant` 以相同 StartTime/EndTime 创建 Modifier，`MotionWarpingComponent::ProcessRootMotionPreConvertToWorld` 逐帧消费。因此二者在“动画提供允许调整根运动的时间区间、执行端向目标收敛”这一职责上对应，不能据 GTA 使用标记便要求 IMG 再新增独立补偿 ANS。源码只证明标记数据消费，不证明 GTA 编辑器的具体制作操作。
- 数学实现不能视为等同：GTA 以至 Clip 相位1的总动画位移计算 DistanceDiff，将各轴残差按调整区间相位覆盖量加到 DesiredVelocity，并可能动态延长区间；UE SkewWarp 默认从 PreviousPosition 提取到 Window.EndTime 的剩余根运动，缩放/剪切本帧根运动，Bone Provider 另做窗末 Root/骨点目标换算，`bSubtractRemainingRootMotion` 是额外尾段扣除选项。故 GTA 调整标记末端不必等于其目标位移的求值末端，而 IMG Mantle 当前明确以最后平移窗末作支撑求值。保留职责映射，不据此承诺同轨迹、同终点或同碰撞结果；项目准备阶段的 Target.Z 支撑修正也须与原生逐帧 Warp 区分。
- `ms_VaultExitConditions` 的 eVEC_Slide 属于独立滑行动作且禁自动 Vault，IMG 推荐表只保留对应普通 Vault 的六条。27.5 cm深度球、55 cm步距、20 cm断层等保持既有映射，不以调阈值替代执行语义。GTA 多层搜索/坡面后筛、MBR发起范围、JumpVault步态门槛及 AutoVault摇杆/受阻/冷却可作对照；纯自动落地要求与 IMG 默认允许空中属于策略差异。
- 当前预测假设资产默认有限 Section 链正常推进。运行时改链、混合权重、限速/轴耦合和 CMC 碰撞截断会改变实际落位；抓沿下移受阻后，后段会从实际高度继续根运动，预计算不能修复此执行偏差。当前 Mantle 在登顶边界查实际支撑并交 Walking，尾段由 CMC 持续约束地面；未补做运行时轨迹或第二次 Warp。Hurdle/Vault 保持原退出合同，低帧率跨界仍以 Move 为粒度，不应以延长 Warp 或表现调参代替运行验收。
- 局部 Target 跟随不等于完整动态平台支持。BasedMovement/动态 Bone、世界回滚、异步检测、持续悬挂/Shimmy/梯子另行设计；沿用 UE/GAS/CMC，不移植 RAGE 的附着、Capsule、手动位移或网络协议。
- **待运行验收：** 各 Action、窗内起播/跨 Section、上下补偿、失败/中断、专服/Listen Server 的自主与模拟代理、引用晚到及延迟/丢包纠偏。源码合同、静态检查及三客户端 PIE 总帧数均不能替代资产表现与联机验收。

- **核心实现加固与结构收口（已编译 `ImagineGameEditor Win64 Development` 通过，待运行验收）。**
  - GA 与网络：`CanActivateAbility` 与同帧随后的 `ActivateAbility` 经 `FPreflightCache`（核对帧号/Avatar/位姿/方向/意图/来源，退出及换绑清空）复用检测结果；异常中止统一经 `AbortTraversal` 以取消结束；`FIMGTraversalTargetData::NetSerialize` 补齐 2bit 枚举上限 `static_assert` 及 `!Ar.IsError()`，服务端对量化向量 Z 容差采用 `KINDA_SMALL_NUMBER` 并重新 2D 归一化；`TrackedObstacles` 合并原平行数组。
  - Component 与 Replay：`ReconcilePredictedTargets` 同步等待 `FloorOnlyComponents` 引用就绪；`TickComponent` 按角色门控 `ReconcilePredictedTargets` 与 `RetryReplicatedMontage`；`CheckReplicatedMontagePrerequisite` 对无根运动 Montage 直接放行；提供只读 `MatchesActiveReplicatedMontage` 供 AnimNode 消费。`IMGTraversalReplay.cpp` 收口为单次执行一份 `FIMGTraversalReplaySession` + 逐 Move 一份 `FIMGTraversalReplayMove`；本地结束后 `HandleClientCorrectionApplied` 仅更新 `Session->bGroundReleased`。
  - Detector、Execution 与 HandIK：`Detector` 合并边缘聚类为 `FDetectedEdgeCluster`（真实均值质心、缓存 2D 法线、内联缓冲与复用 `TraceTop` 临时数组），修正穿透法线累加、净空坡向 `(SurfaceForward, Up)` 平面投影、最优边失败码保留与策略/运动失败码区分；`IMGTraversalWarpWindows` 将裁剪片段窗口夹到 Montage 时间轴内；`FIMGTraversalPlaybackPath` 补齐跳段边界 `Range.Y` 并抽出 `ForEachInterval`；`FIMGTraversalWarpPrediction` 合并窗口缓存并预缓存 `MeshRelativeInverse`；`IMGTraversalPlacement` 拆分为 7 个阶段函数，改用 `TWeakFieldPtr` 防 Live Coding 悬空，补齐 `bHasBackLedge` 与多窗重叠诊断；`IMGGroundPlacement` 补 `GetWorld()` 判空；`IMGTraversalHandContact` 参数收口为 `FIMGTraversalHandContactSettings` 且运行时确保 `ContactBreakDistance >= MaxHandCorrectionDistance`；`IMGAnimNode_TraversalHandIK` 修复 `PreUpdate` 在 Alpha=0 时反复 `ScaleBiasClamp.Reinitialize()` 导致插值跳变的问题，并拆分 `SolveArmTwoBoneIK` / `ApplyDetachRecovery`。
