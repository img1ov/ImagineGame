# Traversal 调试与 Rewind

运行时合同见 [Traversal 检测与执行](traversal.md)，编辑器目录见 [工程方向与编辑器](project.md)。

## 调试工具与记录合同

### 日常入口与数据边界

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
- Warp排查先区分目标接触点、接触窗末胶囊、完整播放路径终点和实际结束胶囊。Mantle 的 TopContactWindow、Hurdle 的 LandingWindow/BackFloorWindow 都以所选窗末为口径，不套用 PlaybackPath.PlacementTime/动画全长。完整路径终点含窗后根运动，不能冒充落地点；提前中断也不能反证或证明完整路径XY正确。Landing clearance胶囊另含校验余量。PlacementEvaluation/LandingEvaluation记录StartTime、求值终点、参考/修正Target、窗口时间、IncludeTail和中心；PlacementWindow记录首次投影的实际入窗时间、剩余平移、原生尾段扣除、WarpPoint/RootOffset与轴选项。旧记录可能仍用 PlaybackEnd/IncludeTail=1，须按记录版本和事件内容解释，不能套用新算法。
- MontageStopped后的Montage_GetPosition可能为0，不能据此认为动画回到了0。应取最后活动帧与事件时序，实际播放时长须减去 Selection.StartTime。新事件 `MontageInterrupted` 对应任务 OnInterrupted（Montage 中断或 GA 取消转发），`MontageTaskCancelled` 对应 OnCancelled（启动失败或 ExternalCancel），记录可观察实例位置/ID、当前活动 Montage、AnimInstance 与结束标志；实例已销毁时位置为 -1。Rewind 继续识别旧合并事件 MontageInterruptedOrCancelled。原生 CancelAbility 先广播取消再 EndAbility，本项目回调可能先 K2_EndAbility，因此 End 日志 Cancelled=0 不能单独排除 GAS 取消或判定中断来源。只有本端 JSONL 时不以晚到权威结束快照倒推最初停止原因，也不以复制 Montage 重试排除自主代理的 GAS 取消。

### 模块、采集与版本

- 运行时根目录为 `ImagineGame/{Public,Private}/Character/Traversal`，编辑器按同层级放在 `ImagineEditor/{Public,Private}/Character/Traversal`，头文件/实现按Public/Private分离，Game不依赖ImagineEditor。

| 源码入口 | 所有权/合同 |
| --- | --- |
| Public IMGTraversalDebugTypes | Frame/Event/Shape/Candidate只读、可反射/序列化，无UObject引用。 |
| Public `.h` / Private `.cpp`：IMGTraversalDebug | 同步弱引用Scope、Actor/Attempt/帧采集；不取得玩法状态所有权。 |
| Public `.h` / Private `.cpp`：IMGTraversalDebugTrace | 版本化Trace、FObjectTrace身份与录制时间换算、Runtime Extension。 |
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

### 原生Details与复制、资产操作

- 使用PropertyEditor的分类、搜索、列宽和记录展开；保留按轨道分工。Detection看最近检测/逐Attempt候选与决策，Execution看Montage/移动/执行候选/补偿，Network / Replay看身份与网络事件。Details不展开Geometry图元列表，不显示普通Search/Intersections/Output阶段的通用Detail绘图文字及Event.Stage实现分类；Warning、决策、失败和具名补偿诊断保留，底层图元/原始事件仍供视口回放及完整导出。向量、Transform、数组逐层展开；浮点最多显示3位小数，Role/NetMode显示名称，Trace默认折叠。事件/候选每页32条记录，Previous/Next访问全部；页码保留并按记录数钳制，搜索只作用于当前页，导出不分页。
- 不整块禁用Details、不改运行时UPROPERTY权限。默认属性隐藏，由标准DetailWidgetRow与只读文本/超链接展示；FStructOnScope只供布局类型，值来自不可变帧。旧固定高度列表/自制双列已移除。字段值可选中Ctrl+C；右键Copy full recorded value不受显示舍入影响，字段名右键还可复制内部名、名称与值、显示文本。记录头Copy为该事件/候选完整JSON，图元通过完整帧导出访问。父轨道Request与Detection共用的Attempt/Source/Direction只在Request显示一次，Detection子轨道仍自带这些上下文；事件与候选自身的同名字段属于独立时间/Attempt，不能按显示去重删除。
- 标量float/double完整值用9/17位有效数字，结构/数组递归保留精度；字符串保留原名/路径。输入控件IsReadOnly，Paste明确禁用。`FDetailWidgetRow::IsCopyPasteBound`要求Copy/Paste均绑定；值控件通过ContextMenuExtender提供完整值复制，避免自身文本菜单遮蔽。过滤后为空的Events/Candidates不创建分类，避免空记录占位。
- `FDetailGroup::HeaderRow()`每次调用都会新建并替换头部，不能当getter反复调用。每组只取一次引用，再设置标题、摘要、复制和Tooltip；事件名放折叠标题，时间/Attempt/Phase放摘要，展开不重复Name字段。
- SHyperlink的下划线覆盖控件实际分配宽度；资产链接置于AutoWidth子槽，避免Details值列的最小/填充宽度将短名称下划线撑长，默认列宽和其它值控件不受影响。
- Montage按完整对象路径打开；GA类路径取ActiveState.AbilitySpec对应Spec.Ability.GetClass，蓝图GA打开ClassGeneratedBy，原生类走FSourceCodeNavigation。资产只在顶部Traversal分类显示一次；同一行提供名称/完整路径复制、名称点击打开、Tooltip完整路径与Content Browser定位，搜索包含名称及路径。Trace不再重复资产路径/打开按钮，只保留采样元数据；原始Frame/JSON仍保留全部路径。/Script类不提供伪资产定位。运行时Actor/Component路径仅供复制。仅点击时TryLoad，刷新不加载资产；缺失/失败提示，不保活录制World对象。
- Height Comparison仅在父轨道/Execution展示本帧的参考世界Z、MontageRootHeight与WarpedCapsuleHeight，可复制完整精度；无有效采样显示状态。Trace的HeightCapture标识独立执行，不用Attempt/Sequence临时变化连接不同起点。JSON的height_basis说明单位、起点及Warp/CMC含义。
- UE 5.8 `String/FormatStringSan.h::TCheckedFormatStringPrivate`以consteval接字面量数组，Printf首参不能三目选择不同长度格式串；精度选择放在调用分支，每个Printf直接传TEXT字面量。

### 文本导出与精度

- 输出 `Saved/Debug/Traversal/Traversal_<ActorId>_<UTC>_<Guid>.jsonl`：UTF-8，首行版本2元数据（角色/世界、范围、帧数、枚举含义、时间基准），后续每行完整Frame。单帧复制为格式化JSON。只导出已分析快照，不生成未录制内容，也不替代utrace。
- 64位整数用十进制字符串，避免Cycle/ActorId/Frame/Attempt经过double丢精度。结构体强制递归为JSON对象，避免原生math ExportTextItem先转成精度受限字符串；旧版本1文本仍可读。转换与行级复制共用RecordToJson/RecordObject，不改变原生Trace格式。
- Session锁内仅复制不可变帧引用，JSON转换、进度/取消和磁盘I/O均在解锁后。无Session/无数据/写入失败显示原因；唯一文件名且不覆盖，取消/失败清理部分文件，清理失败给出路径。

### 图元、Graph与排查边界

- 线用像素细线，点标记最多3cm，探针球/胶囊显示真实尺寸。World后端写普通World/Foreground LineBatcher，暂停回放不能依赖游戏Tick回收；本方图元带独立BatchID，每次现场Tick/回看Update先ClearBatch后画当前帧。隐藏、无帧、切世界、清录制、模块退出也清本方批次，不Flush其他系统；现场暂停不清回看批次。新增会产生Mesh的图元须扩展批次归属。
- 准备胶囊：橙色为补偿前；绿色仅在支撑/重叠/全链复算通过后；已计算但被拒绝的修正位置为红色；无有效预测不伪造胶囊。Purpose区分补偿前后和显式Landing净空，同Attempt活动帧持续带入，结束/新准备/解绑后停止沿用。位置对应动作所选接触窗口末端，Landing=None 也不累计窗后走位；橙色不是 Warp Target 或强制结束位置。位置是准备时世界坐标，不代表动态载体后续预测；模拟代理无本机准备胶囊。补偿为0时前后重合。
- Graph最多240个不同帧的样本，现场与回看均将同一Frame的多次提交合并为最后一个Graph样本，原Trace全部提交不变。回看仅在Graph打开且停止模拟/录制后构建历史；按游标最近5秒采样、剔除窗口外边界帧，不读取游标之后数据，并独立缓存GraphRevision/GraphTime，关闭时释放历史。检测耗时含采集开销，Queries只计项目显式查询，不含CMC.FindFloor等内部查询；后续帧保持最近检测值，不代表每帧重检。Montage进度为资产位置/资产长度，不是Section路径进度。
- 高度框比较同一起点的世界Z增量（cm），橙线MontageRootHeight从本次Selection.StartTime沿PrepareTargets已验证的有限Section路径提取未Warp根运动，以入场Actor/Mesh参照组合；绿线WarpedCapsuleHeight读取同一时刻实际胶囊/Actor高度，包含MotionWarping、碰撞、移动载体及CMC纠偏，不等于单独Target.Z补偿量。原补偿算法只预测所选终点，不据此插值伪造整条“纯高度补偿前后”轨迹。
- 起点/路径只在准备成功且已观测时缓存，实际ExecutionStarted才启用；缓存使用弱Montage引用，随Actor观测释放。录制中途接入且缺起点、模拟代理无本机准备、无效/未起播/已结束或位置不在已验证路径时标不可用。逐帧根运动提取只在录制或Graph开启时进行；仅开几何Draw不提取高度曲线。开启Graph即可在本地采集，无需开始录制；录制开启时即使Graph关闭仍采集。该提取属于Debug开销，不计为纯Traversal检测成本。
- 高度采样必须有实际Montage实例；活动实例已从ActiveMontagesMap移除但Traversal尚在混出时，可使用同资产的原生RootMotionMontageInstance位置，避免Montage_GetPosition的停止后0值伪造回零尖峰。准备成功会退休旧高度起点；仅同帧Prepare→ExecutionStarted接合新起点，未观测的新动作不能继承上一次同名Montage的参考位置。
- 高度框只画最近一次有效HeightCapture，跨执行、缺失样本和采样空档断开；原始与执行曲线共用纵轴，可显示负值，单点画点，无数据只显示状态。不从旧录制或资产名反推缺失样本；History与回看GraphFrames同时保留高度/身份字段。
- 全部Graph曲线共用PlotCurve的采样空档/单点处理，横轴固定为游标前[-5,0]秒；Rewind显式传ScrubTime，空档保留历史并标注History only，当前指标显示n/a，历史高度值标(last)，不把旧值延伸为当前帧。Graph角色优先级为选中的父/子轨道→当前根对象→其它可见调试对象，较低优先级不能覆盖已选角色。无当前Frame不重画旧几何；换会话、隐藏及退出仍清理本方批次。
- Graph使用DrawDebugLibrary的空间World Drawer：运行时在OnWorldPostActorTick采样后直接绘制，Rewind在Extension.Update选定显示轨道后绘制；不再注册Canvas/DebugDrawService回调，也不使用CanvasString。运行时取本地PlayerCameraManager视点，回看优先取同World的当前LevelEditor视口，回退同World本地玩家镜头。图框位于视点前方，按FOV/宽高比布置，旋转后的Y/Z分别对应横纵轴，bDepthTest=false；轴、曲线、图例和字形线全部经WithOwnedBatch归入同一Traversal批次，隐藏/更新/清会话沿既有Clear回收，不另建文字残留或录制通道。一个样本即可显示轴与当前值，无样本显示等待提示；DebugDraw=0仍可独立显示Graph。
- 用户提供的DebugDraw_MultiLineGraph蓝图导出使用MakeVisualLoggerDebugDrawer、旋转后Offset、DrawDebugGraphAxes/Line/Legend，属于空间图形；本工程采用其位置/旋转、轴/曲线/图例和关闭深度测试的用法，但用World Drawer直接绘制，不恢复旧VisualLogger文字通道。原Canvas后端也是库支持的接口；本轮已改用空间路径，旧后端未实际显示的具体运行时断点不能据源码差异断言。
- Traversal业务CPP只通过IMGTraversalDebug提交观测，空间绘制集中于DebugPresentation；不保留直接DrawDebugHelpers、UE_VLOG、屏幕文字、GameplayDebugger入口或等级判断。执行Target不再逐帧生成重复文字事件，目标位置/朝向留在图元中；AddedIgnores数量仅在ExecutionAcquired/Releasing记录。Live图元去重与准备胶囊缓存有各自用途，保留当前帧去重和生命周期清理。旧Frame序列化字段即使当前表现不消费也不能随清理删除，须遵守版本合同。
- 专用 `Traversal Hand IK` 调用 `IMGTraversalDebug::HandContact`，有效掌点以球和修正线提交到 Execution 的 Live 图元，左绿右紫。连线从上次完成的 Pre-IK 掌点样本指向本次游戏线程探测目标，不代表求解后的误差。沿用 `IMG.Traversal.DebugDraw 1` 和现有 Rewind 录制通道，无额外调试探针或表现用腕点兼容参数。
- RewindDebuggerTrack.Update只更新当前节点，父轨道必须显式Child.Update，参照GameplayInsights/Private/MontagesTrack.cpp。子轨道Name稳定且独立；时间轴与Details共用事件分类，时间轴省略Detail/ExecutionState/ReplayCaptured/Restored/PlaybackSection/Path及PlacementWindow/PlacementEvaluation/LandingEvaluation等明细标记，子轨道Details仍保留。Session/Provider缺失时递归清时间轴/选帧/Details。
- 参考入口：PoseSearch的RecordingStarted/Stopped、Trace Module/TrackCreator/Details；GameplayInsights/Private/SMontageView.cpp的资产链接。Visual Logging文本会被引擎画回视口，不再作本系统主通道。GTA的ClimbDebug、PedDebugVisualiser、DebugDrawStore负责空间绘制，debug/Debug.cpp::RenderProfiler独立调用GetRageProfileRenderer().Draw绘制EKG；仅借鉴职责，不依赖RAGE。
- 验证边界：已核对源码接口和单向依赖；三端行为、远程会话、录制开销与全部UI交互仍须运行验收。具体录制只证明相应角色/时间段，编译或静态检查不替代表现验证。不主动调查或修改Content。
