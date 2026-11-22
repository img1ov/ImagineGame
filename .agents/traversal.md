# Traversal 检测与执行

相关主题：[Traversal 调试](traversal-debugging.md) · [普通移动与 CMC](locomotion.md) · [Gameplay 框架](gameplay.md)。

## 职责与装配

- Traversal运行时及编辑器源码的注释统一使用英文，配置合同与示例保留原意；中文工程导航继续写在AGENTS中。
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

## 检测、发起与选片

- 每次激活只尝试一次，外部决定触发时机，`RequestSource` 与 `ActivationPolicy` 独立。`CanActivate` 作本地几何/发起预筛，激活后依次执行 `PreSelectTraversalAnimation`、`SelectTraversalAnimation`。Chooser 消费入场 MovementMode/Gait/Speed，必须早于 `BeginTraversal` 切 Flying。
- `SampleHandhold` 采样中心/左右顶部、宽度和楼梯轮廓，`MakeCandidate → ValidateCandidateDirection` 验证方向。方向回退复用本次公共采样，但从原始候选副本重算深度、后沿、净空和坡向；不跨 Detect 调用、帧或网络端缓存，不省略服务端重检。
- `InitiationSettings.bEnabled=false` 仅跳过发起限制。Manual 默认允许站立、范围150 cm；Automatic 默认允许 Falling，要求有效意图、朝向点积≥0.8、固定范围150 cm，不按速度二次缩距。速度缩放只影响搜索长度；`Candidate.Speed/RunningSpeedThreshold` 仅供建议/选片，不限制原生 Supports。
- 意图优先待消费输入，再取 CMC.Acceleration，不以 Velocity 替代。玩家使用 GA.RequestSource；服务端 AI 强制 AI 来源，复用 Manual 规则，无输入/加速度时可取 Moving 的 PathFollowing 当前段目标，无意图时可按手动站立规则向前尝试。
- 发起距离由当前 Actor 与前沿重新计算 XY；`Candidate.PlanarDistanceToLedge` 保留检测时的选片/诊断值。`ServerDistanceTolerance` 只放宽远端发起距离，不扩大服务端几何搜索。发起角度使用本次请求方向和重检前沿，不换成收包时朝向。
- PreSelect、Select、Commit 回调后复核原 ASC/Avatar/Component。Commit 后还须核对入场位姿、胶囊、Mesh/AnimInstance、BaseOffset 和参与表面的变换，防止旧落位结果应用到已变化的对象。候选默认时效 `MaxCandidateAge=0.25 s`。
- Supports 与推荐动作分开：Mantle 是登顶，要求前沿；Hurdle 是有后方支撑的翻越，要求前沿+后沿+`bHasBackFloor`；Vault 是没有确认后方支撑的翻越，要求前沿+后沿且 `!bHasBackFloor`。Mantle 可与其中一种翻越同时可选，由 CHT 决定覆盖与优先级；高度/深度/速度不在 Supports 中硬编码。`RecommendedAction` 仅作建议，Hurdle/Vault 各用自己的推荐高度限制，不因 Hurdle 超出建议高度改推有地面的 Vault。
- `bHasBackFloor=false` 表示探测范围内尚未确认可用后方支撑，不证明世界中不存在地面，也不能当作落点。`BackLedge` 在 `bHasBackLedge=false` 时仍保留最后顶面采样位置，坐标与 ObstacleDepth 都不能代替已确认后沿。空中 Height 随当前脚底升高而减小，需结合 StartTime 和选片核对。

## 手点、坡面与表面规则

- GroundLocation 只取 CMC.CurrentFloor 的有效可走支撑；空中或无有效支撑（含无碰撞支撑的 NavWalking）回退当前缩放胶囊底部，不读取 GetGroundInfo 的长距离下探。`bUseGroundPositionForHeight` 默认 false，开启后仍使用同一回退规则；坡面/脚下楼梯只消费有效支撑。
- 手点取前侧命中点上方的中心/左右实际顶部接触，不固定向内平移或投影回前侧平面。`UseRequestDirectionBelowHeight=80 cm` 以下优先请求方向，设0关闭；左右接触连线求交并插值高度，调整范围50 cm，失败再用原始手点/墙面法线重检。此近似不替代双手 IK。`MinFacingDot` 默认 Manual/AI=0.3、Automatic=0.8；HClear/VClear 是净空，不是动画轨迹。
- `AngularSurfaceTag` 默认 `Traversal.Angular`，读取命中组件或所属 Actor 的 FName 标签。请求与入面方向 dot>0.95 时，高障碍也可保留请求方向，失败回退法线。低障碍、棱面或 Automatic 要求双侧支撑；其余 Manual/AI 可追加外侧探针并重定中心，新中心组件须纳入障碍跟踪。标签不引入车辆滑行动作。
- 楼梯由 `StairSurfaceTag=Traversal.Stairs` 指定：前侧/顶部命中带标签，或站立时脚下带标签，启用轮廓过滤；未标记不保证识别。边缘跨度截止值默认22.5 cm、命中楼梯表面时30 cm，严格小于才拒绝，等于可过；跨度<40 cm且水平漂移>10 cm也拒绝。Span 不等于台阶高度，Height 默认相对脚底；独立顶部组件也须覆盖标签。
- `BlockedSurfaceTags` 拒绝全部来源；`AutomaticBlockedSurfaceTags` 仅拒绝 Automatic。Actor Tags 作用于全部组件，局部限制优先 Component Tags。例如 `Traversal.NoAuto` 必须显式加入自动禁用列表才生效；`Traversal.Stairs` 只是启用过滤，不禁止全部攀爬。自动深度限制、高位开放接近拒绝和楼梯下行方向限制只用于 Automatic，楼梯轮廓也用于 Manual/AI。
- 深度断层可能在曲面上形成后沿。`AutomaticMinDepthSamples` 是结束扫描前的最小扫描次数，不是连续顶面通过次数。`Candidate.GroundNormal` 是深度命中的平均法线，不能替代补偿实际命中的法线或可走判定。
- **平台可走坡度归 CMC。** BackFloor、补偿与落地检查使用 CMC.IsWalkable。通用上限为角色 Character Movement → Walking → Walkable Floor Angle（UE 5.8 原生约44.8°，以组件实际配置为准）；局部平台用碰撞组件 Physics → Walkable Slope Override。`MaxClearanceSlopeAngle=42.5°` 只控制净空探针方向。`MaxMovingSpeed=500 cm/s`、`MaxAngularSpeedDegrees=85°/s` 限制载体运动，检测及执行均复核。

## 播放路径与预测能力

- GA 以 `StartSection=NAME_None`、Selection.StartTime 启播。资产 `GetPlayLength()` 不代表本次末端；Placement 从 StartTime 所属 Section 沿资产 NextSectionName 解析有限路径，合并相邻段，将前跳/后跳映射到顺序播放时间轴。根运动、DistanceCurve 和窗口筛选只累计实际路径，显式窗口编号保留资产原始序号。循环、无效链或活动 Warp 跨非连续边界在 Commit 前拒绝。
- Motion Matching/PoseMatch返回的StartTime经Selection → TargetData.NetSerialize → ResolveTraversalRequest → PrepareTargets及PlayMontageAndWait原值传递；不按0起播，也不在服务端另选时间。ProjectPlacement的Cursor从PlaybackStart开始，窗内起播只Advance剩余区间；SkewWarp的有/无平移分支判定仍用完整窗口位移，与原生TotalRootMotionWithinWindow一致，不能误改成剩余位移判定。DistanceCurve的接触窗末→落地窗末距离是两锚点间距，不应减去StartTime；它与当前胶囊→落地的剩余位移不是同一量。
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
| 模拟代理 | 不选片、不重检几何；按 Montage/PlayInstanceId 等待快照引用、Target、碰撞忽略和 Flying。结束先到时保留已播放实例至退出（含混出），未播放终态实例丢弃，晚引用不得复活。恢复 Target 使用权威历史，非 Flying 到达后先恢复碰撞再查支撑。 |

- Component 按独立句柄注册 ASC Montage 前置条件。ReplicatedMontages 非空时，选片/复制归属仅接受其列表及 MontageSettings，仍可复用 ActionSettings；空列表保留目标名推断以兼容装配，多 Warp 系统应配置明确列表。同实例引用补齐不重复拆建，结束快照保留 Montage/PlayInstanceId。
- `CMC.ClientCorrectionFilter` 仅处理 Packed MoveResponse 中仍匹配 SavedMove 的纠偏。GA 只在本方活动且 Flying 时过滤启动前已记录的待确认 Move；保留原生 ACK/缓存释放，不应用该次位置/模式/根运动，超出时间戳重置安全窗口则放行。启动后的纠偏和权威拒绝仍有效，不能据过滤推断落位已被权威消费。
- UE 的 ASC.PrePredictionActivation 和 GameplayAbility.PreActivate 已有激活前 FlushServerMoves 路径；沿用它们，不额外发送或增加确认等待。

- 丢包诊断：Traversal 请求沿 GAS 的可靠激活与 TargetData RPC，底层丢包可使重传晚到。CMC 启动后纠偏和 GAS 权威拒绝均可使本机预测回退。画面回拉本身无法判定是哪条消息晚到，按预测键对照服务端重检、终止和移动纠偏。

## 重放、生命周期与退出

- 原生 SavedMove 保存原始动画根运动，MotionWarpingCharacterAdapter 在 bClientUpdating 时再次处理。Traversal 经 CMC 通用 MovementReplayCapture 保留逐 Move 的 setup/restore 闭包；在原生 OnPreUpdate（发现窗口后、更新 Modifier 前）记录世界 Target、SwapState 和本方碰撞忽略，记录/重放不合并 Move。
- 重放临时安装历史上下文，以原生 Modifier 副本计算并屏蔽重复 Notify gameplay 回调，随后恢复当前会话。副本仅首次重放创建，后续重置状态复用，FreeMove/Clear 释放历史引用；无额外 Tick/保活计时器。历史 Flying/退出边界可重放，不强制接管外部 Custom/物理模式。
- 权威终止/取消/拒绝按 Avatar/Spec/预测键退休对应上下文；组件停用、注销、销毁、ASC解绑或换绑同样使其失效。旧 Move 仍可能进入 PrepMoveFor，失效分支须清除属于该历史 Montage 的 RootMotionParams，不能只提前返回。闭包弱引用组件；不承诺回滚世界碰撞、动态载体或自定义 Modifier 外部副作用。
- 正常完成以 PlayMontageAndWait.OnCompleted（含 BlendOut）为边界。有限动作要求开启 AutoBlendOut 且时间有限，否则 Commit 前拒绝。站稳动作应在站稳后混出；预测落点、Warp 窗口结束或途中接触不提前切 Walking，也不增加独立落地阶段复制。
- 完成、中断、取消、死亡、失效或 ASC解绑均幂等释放 Target、碰撞忽略、Tick依赖、委托和计时器。恢复碰撞后，仅存活且仍 Flying 时按实际支撑切 Walking/Falling；Ragdoll 等接管的新模式不得覆盖，退出无需等待 Montage 自然结束。
- BeginPlay/Activate/重新注册接合 MotionWarping、PawnExt/ASC 和 CMC。Deactivate/OnUnregister/EndPlay 释放接合，期间 IsTraversalAvailable=false，晚快照不重启。拒绝无根运动、非正/非有限 RateScale 和无效检测通道；PawnExt 解绑顺序见 ASC 生命周期。

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

- GTA 路径相对 `src/dev_ng/game`：检测/手点 `task/Movement/Climbing/{ClimbDetector,ClimbHandHold}.*`，发起 `task/Default/TaskPlayer.cpp`，Jump/Vault 分流 `task/Movement/Jumping/TaskJump.*`，执行/清理 `task/Movement/Climbing/TaskVault.*`。GTA Clone 同步不等同 UE 的 CMC Replay。
- CTaskVault 按手点、DropPoint、GroundToRootOffset 和动画相位求目标位移，`CalcTotalAnimatedDisplacement`、`SetDesiredVelocityAdjustmentRange` 分别处理动画位移/调整区间。`ClimbDetector::ValidateHandHoldAndDetermineVolume` 调 `CTaskVault::IsHandholdSupported` 校验出口，不能把几何通过等同动画可落位；IMG 分层落实为选片后绑定、可选补偿和显式 Landing 合同。
- 已验证 GTA 位移与接触分别处理：`TaskVault.cpp::CalcTotalAnimatedDisplacement` 从 StartScalePhase 提取至相位1的 mover 位移并按活动双 Clip 权重混合；`ScaleDesiredVelocity` 将 TargetDist 与该总位移之差按各轴调整区间添加到动画速度，`ProcessPhysics` 在 Climb/Slide 消费。`StateClimb_OnUpdate` 在混入或超过初始调整区间时重算剩余位移，并可把后续调整延长到相位1；因此不是只对齐手点后永久冻结后段位移。`ProcessHandIK/UpdateHandIK` 由动画信号启停，向 Arm IK 提交绝对或载体相对目标；`ComputeIsOnGround` 另查实际支撑。这些是本地源码事实，不代表该快照所有资产的配置或发行版实际行为，也不将其整体视为精确接触/必定落位证明。
- 标记职责映射已核对：GTA `StateClimb_OnEnter` 直接读取 Clip 的 `AdjustAnimVel.GetStart/GetEnd`，传给 `SetDesiredVelocityAdjustmentRange`；它是 Climb 的位移调整区间，不特指手点对齐后的第二补偿阶段。UE 的 `UAnimNotifyState_MotionWarping` 本身就是区间标记，`OnBecomeRelevant` 以相同 StartTime/EndTime 创建 Modifier，`MotionWarpingComponent::ProcessRootMotionPreConvertToWorld` 逐帧消费。因此二者在“动画提供允许调整根运动的时间区间、执行端向目标收敛”这一职责上对应，不能据 GTA 使用标记便要求 IMG 再新增独立补偿 ANS。源码只证明标记数据消费，不证明 GTA 编辑器的具体制作操作。
- 数学实现不能视为等同：GTA 以至 Clip 相位1的总动画位移计算 DistanceDiff，将各轴残差按调整区间相位覆盖量加到 DesiredVelocity，并可能动态延长区间；UE SkewWarp 默认从 PreviousPosition 提取到 Window.EndTime 的剩余根运动，缩放/剪切本帧根运动，Bone Provider 另做窗末 Root/骨点目标换算，`bSubtractRemainingRootMotion` 是额外尾段扣除选项。故 GTA 调整标记末端不必等于其目标位移的求值末端，而 IMG Mantle 当前明确以最后平移窗末作支撑求值。保留职责映射，不据此承诺同轨迹、同终点或同碰撞结果；项目准备阶段的 Target.Z 支撑修正也须与原生逐帧 Warp 区分。
- `ms_VaultExitConditions` 的 eVEC_Slide 属于独立滑行动作且禁自动 Vault，IMG 推荐表只保留对应普通 Vault 的六条。27.5 cm深度球、55 cm步距、20 cm断层等保持既有映射，不以调阈值替代执行语义。GTA 多层搜索/坡面后筛、MBR发起范围、JumpVault步态门槛及 AutoVault摇杆/受阻/冷却可作对照；纯自动落地要求与 IMG 默认允许空中属于策略差异。
- 当前预测假设资产默认有限 Section 链正常推进。运行时改链、混合权重、限速/轴耦合和 CMC 碰撞截断会改变实际落位；抓沿下移受阻后，后段会从实际高度继续根运动，预计算不能修复此执行偏差。结束时查实际支撑，不补做运行时轨迹或第二次 Warp；当前用户选择按 Mantle 窗口制作合同保留原生执行，不引入独立后段修正。
- 局部 Target 跟随不等于完整动态平台支持。BasedMovement/动态 Bone、世界回滚、异步检测、持续悬挂/Shimmy/梯子另行设计；沿用 UE/GAS/CMC，不移植 RAGE 的附着、Capsule、手动位移或网络协议。
- **待运行验收：** 各 Action、窗内起播/跨 Section、上下补偿、失败/中断、专服/Listen Server 的自主与模拟代理、引用晚到及延迟/丢包纠偏。源码合同、静态检查及三客户端 PIE 总帧数均不能替代资产表现与联机验收。
