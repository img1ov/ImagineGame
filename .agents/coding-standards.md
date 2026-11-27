# ImagineGame C++ 工程审美、代码规范与 UE 避坑红线

本文档是所有 C++ 方案设计与代码落地的通用质量基线。涉及代码编写、重构或审查时，先读本页再改源码；与具体系统相关的合同以 `.agents/` 对应主题文档为准。

---

## 一、代码审美与结构组织

- **入口函数只表达主干，复杂流程按阶段拆解。** 核心入口函数应让人一眼看清阶段顺序（建议 ≤ 80 行）。超过 ~150 行的多阶段流程必须按职责拆为文件内匿名 `namespace` 阶段函数，并用轻量 `F*Context` 结构体在阶段间传递状态，避免单个几百行巨型函数里塞满多层嵌套 Lambda 或 20+ 变量捕获的闭包。
- **消灭平行数组（Parallel Arrays）。** 凡是靠同一索引同步维护的 2 个及以上 `TArray`（例如组件数组与上帧变换数组、聚类命中/中点/法线/得分数组），一律合并为单个 `struct` 数组，杜绝长度失步、重复判空和越界隐患。
- **收口超长参数列表。** 参数超过 6~7 个、尤其包含连续多个同类型 `float`/`bool` 阈值时，将同生命周期的配置或输入打包为具名结构体（附 `IsValid()` 与 `operator==`），避免调用端传参错位以及实现端手写逐字段 diff 与拷贝。
- **私有状态分组与对称重置。** 复杂组件、GA、AnimNode 的私有成员必须按职责分块（如基础设施绑定、运行期快照、碰撞/Tick 依赖、阶段状态、预测/重放、重入守卫）。有独立生命周期的状态优先收口为带 `Reset()` 的私有子结构或会话对象，确保 `Begin` / `End` / `ReleaseBindings` / `OnPawnAvatarSet` 成对释放、不漏字段。
- **单向依赖与最小暴露。** 跨子系统只读查询应在主责组件暴露窄接口（如 `MatchesActiveReplicatedMontage()`），禁止表现层（AnimNode、UI）反向依赖底层装配组件（如 `PawnExtension`、`ASC`）或每帧重复 `FindComponentByClass`。非公开实现类型不暴露到 `Public` 头文件（可用前向声明 + `TSharedPtr` + `.cpp` 默认析构）。
- **控制流、常量与注释风格统一。**
  - 用早返回（Early Return）压平嵌套；同文件保持一致的多行大括号排版，不在同一文件里混用大量单行压缩 `if (...) { a(); return; }`。
  - 函数体内重复出现或有几何/策略含义的魔法数字统一提取到文件顶部匿名 `namespace` 具名 `constexpr`。
  - 注释只解释**为什么**（不变量、边界条件、引擎底层合同、不直观的时序与生命周期），不复述代码字面逻辑；Traversal 运行时与编辑器源码注释统一用英文，工程文档用中文。

---

## 二、UE 5.8 / UHT / Live Coding 反射红线

- **UHT 旧式枚举引用参数。** `UFUNCTION` 暴露引擎旧式 `enum`（如 `EMovementMode`）的输出引用参数时，必须声明为 `TEnumAsByte<EMovementMode>&`，不能直接写 `EMovementMode&`（UHT 生成 thunk 会直接触发 C2664）。
- **反射 `FProperty*` 缓存防 Live Coding 悬空。** 通过 `FindFProperty` 读取引擎 protected 字段并做静态缓存时，必须用 `TWeakFieldPtr<const F*Property>`，禁止用裸 `static const F*Property*`，防止编辑器 Live Coding / Hot Reload 类重实例化后悬空。
- **当前实验性开发阶段不保留旧兼容壳。** 重构或收敛接口时，直接替换旧签名、旧字段和旧路径，不保留废弃重载、过渡转发函数、兼容适配层或冗余别名。
- **`USTRUCT` 头文件边界。** 同模块内拆分 `.h` 不改变 `/Script/<Module>.*` 反射身份；仅跨模块迁移或改名 `USTRUCT`/`UENUM` 时需同步清理对应资产引用（按当前阶段直接改彻底，不堆砌长期 Redirects）。
- **非 `UPROPERTY` 缓存严禁跨帧持有 `UObject`。** 类似 `CanActivateAbility` → `ActivateAbility` 的同帧预筛缓存，必须同时校验 `GFrameCounter`、`TWeakObjectPtr<const AActor>` 及完整输入键（位置、旋转、方向、意图、来源），并在 `EndAbility` / `OnPawnAvatarSet` / `OnRemoveAbility` 立即清空，禁止跨帧复用或遗留裸对象引用。

---

## 三、网络复制、GAS 与 CMC 预测重放防线

- **慎重对待含 `UObject` 引用的自定义 `NetSerialize`。** 含有 `TObjectPtr<UObject>` / `UPrimitiveComponent` 的复制结构体（尤其是关卡/动态组件引用），优先保留原生 `FRepLayout`，以获得引擎自动的 `FNetGUID` 晚到追踪与回填；不要为了压缩几个字节强行加 `WithNetSerializer` 破坏晚到引用解析。
- **纯数值 RPC / `TargetData` 的 `NetSerialize` 纪律。**
  - 凡用 `SerializeBits` 打包枚举，必须在紧邻位置加 `static_assert` 锁定枚举上限，防止未来新增枚举项静默截断。
  - `NetSerialize` 末尾必须执行 `bOutSuccess &= !Ar.IsError()`。
  - 服务端校验网络量化向量（如 `FVector_NetQuantizeNormal`）时，零值/平面分量判断必须给显式容差（如 `KINDA_SMALL_NUMBER`）而非 `UE_SMALL_NUMBER`，校验通过后重新做 2D 归一化再进入几何查询。
- **三端引用晚到必须全集等待。** 自主代理权威回填、模拟代理前置门槛必须检查**全部**参与策略判定的复制对象数组（包括附属分类数组如 `FloorOnlyComponents`），不能只等主数组就绪而放行未解析的附属引用，导致策略回退误判。
- **异常中止与正常完成语义分离。** 死亡、载体失效/超速、失去移动模式或基础接合等异常退出，统一通过 `EndAbility(..., true, /*bWasCancelled=*/true)` 结束，不混用代表正常结束的 `K2_EndAbility()`。
- **已结束动作的晚到纠偏边界。** 本地动作已经结束但仍有未 ACK `SavedMove` 时，CMC 纠偏回调只允许更新重放会话所需的阶段边界，不得回头改写已终结的实时 `ActiveState` 或重新操作已释放的碰撞忽略。
- **`SavedMove` 重放捕获的分配纪律。** `CaptureMovementReplay` 是自主代理 60~120Hz 热路径，按“**单次执行 1 份共享 `Session` + 单 Move 1 份 `MoveRecord`**”设计，禁止每帧零散多次 `MakeShared` 或重复拷贝会话级只读数据；池化 `SavedMove` 复用时必须先清除旧执行身份。
- **按网络角色门控 Tick 与前置检查。** `TickComponent` 内按 `ROLE_AutonomousProxy` / `ROLE_SimulatedProxy` 精准门控各自逻辑，避免权威端或自主端空跑模拟代理重试。模拟代理 Montage 前置检查先用 O(1) 条件快速短路（如 `!Montage->HasRootMotion()`），不让普通动作触发全量 Notify 扫描。

---

## 四、热路径性能、几何数学与 AnimNode 防线

- **热路径控制堆分配与重复计算。**
  - 循环内反复使用的 `TArray`（物理扫掠 `Hit` 列表等）提到循环外 `Reset()` 复用；有界小集合（如搜索层、边缘聚类、少量窗口）优先用 `TInlineAllocator<N>`。
  - `Num() <= 1` 时跳过 `Sort`/`StableSort`。
  - 同一预测或聚类周期内重复使用的昂贵值（如 `ExtractRootMotionFromAnimation`、固定逆变换 `MeshRelative.Inverse()`、聚类归一化法线）合并进单份缓存结构，不在内层循环重复计算。
- **日志与调试延迟格式化。** 在检查 `UE_LOG_ACTIVE` 或调试观测开关之前，禁止提前构造 `FString::Printf(...)` 传入辅助函数；关闭日志与调试时热路径应做到零临时字符串分配。
- **几何与数值安全。**
  - 所有除法、归一化、反三角、外部世界指针（`GetWorld()`）及配置阈值（有限性、正负、范围）均须显式守卫；浮点近零判断使用明确容差（如 `UE_DOUBLE_SMALL_NUMBER`），不用 `!= 0.0` 触发昂贵重算。
  - 多点聚类中心使用真实均值（`Sum / Count` 或递推均值），禁止使用依赖采样顺序的 `(Old + New) * 0.5`。
  - 坡向/净空方向向量须投影到目标纵平面（如 `(Forward, Up)`），防止球扫侧向偏移引入偏航。
  - 表面法线累加排除 `bStartPenetrating` 退穿法线；多候选循环保留最优候选的失败原因，不被低分候选覆盖；策略拒绝与运动限制拒绝返回各自准确的失败码。
  - 裁剪动画片段（`FAnimSegment`）上的 Notify 时间须夹到 Montage `[0, PlayLength]`；分段路径映射须覆盖非末段跳段边界 `Range.Y`。
- **AnimNode 线程与状态守卫。**
  - `PreUpdate`（游戏线程）入口判空 `InAnimInstance`，只读玩法状态并拷贝纯数值变换快照；`Evaluate*_AnyThread` 严禁读取玩法 `UObject` 或发起碰撞查询。
  - Alpha 归零或暂缺样本时只重置接触状态，不要每帧调用 `ScaleBiasClamp.Reinitialize()`，避免破坏 `bInterpResult` 导致下次淡入跳变。
  - 对存在前置约束的配置对（如 `ContactBreakDistance >= MaxHandCorrectionDistance`），运行时做显式夹持兜底，不只依赖注释约束配置者。

---

## 五、方案设计与执行交接约定

- **出方案阶段（Architect）：** 方案须明确写出：目标与边界、刻意不改动的项（含理由，如 `FRepLayout` 晚到引用保护、蓝图结构体稳定性）、改动文件与具体函数/数据结构设计、三端与生命周期不变量，以及区分静态/编译/运行的验收清单。
- **落地阶段（Implementer）：**
  - 严格按方案边界实施，不顺手扩大改动面，也不把未确认的破坏性重构夹带进小修复。
  - 改动完成后按本文档第一至第四节逐项自检：UHT 签名、GC/弱引用、三端乱序与引用晚到、取消/失败回滚、热路径分配与数值边界。
  - 确认的新长期事实按归属写回 `.agents/` 对应主题文档；不把过程记录或临时猜测写入长期文档。
