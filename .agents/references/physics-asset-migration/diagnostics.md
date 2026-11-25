# 对症诊断与验收

## 先分清三个“弹簧”

| 参数层 | 实际用途 | 常见误判 |
| --- | --- | --- |
| PA关节 | 软限位、Linear/Angular Drive、投影等约束响应 | 看到TwistDrive数值便认为它在当前SLERP模式/启用位下必然工作 |
| PCA Control | WorldSpace/ParentSpace控制、Strength、DampingRatio、ExtraDamping、目标速度前馈、MaxForce/Torque、AccelerationMode | 把DampingRatio当成BodyInstance.AngularDamping，或忽略稀疏更新的bEnable字段 |
| PhysicsAssetEditor MouseSpring | 鼠标抓取的线/角刚度阻尼、目标InterpolationSpeed | 调了拖拽手感却以为自由倒地或游戏内PhysicsControl也跟着改变 |

本轮MouseSpring两边原本一致，因此没有为解决滑动额外修改它。Body damping、物理摩擦和主动控制目标是不同消能/施力机制，应按证据定位。

## 症状 → 应检查的事实

| 症状 | 先核对 | 下一步 |
| --- | --- | --- |
| 一开始就弹开/关节抽搐 | 两端锚点误差、框架方向、初始shape深重叠、错误交叉约束 | 修参考姿势映射和碰撞图，再看时间步/迭代 |
| 在平地持续滑行 | 实际PM来源与Combine覆盖位、接触是否存在、脚/手/躯干几何、主动drive或运动学目标是否继续拉动 | 区分惯性衰减与持续注入能量；检查地面材质，不先全体加大阻尼 |
| 身体慢慢爬行/颤动 | 互相竞争的关节/碰撞、动画更新目标、睡眠阈值、位置纠正/投影、world控制未停 | 用相同输入比较源；确认ragdoll Profile和运行时设置实际生效 |
| 手脚被拉向异常方向 | 左右映射、局部轴/镜像、ParentSpace参照、Generated Control名称、目标空间 | 检查B映射和框架语义，不能靠限制角度更小掩盖 |
| 长链伸长 | 锚点、质量/惯性比、真实求解覆盖、dt、驱动强度、投影 | 几何正确后，按源预算验证时间步与求解能力 |
| 落地抖动/弹跳 | 反弹及Combine、源/目标有效dt、CCD、初始穿透、去穿透速度、接触几何 | 比较有效设置，排除帧率差异再评估求解 |
| PA正常而游戏内异常 | Mesh默认PA/Override、PCA实际引用、Profile启停、动画/后处理、胶囊碰撞、组件移动/Attach、Tick顺序/网络纠偏 | 沿运行链核对来源与消费者，不继续只改PA |
| 拖拽表现不同、自由倒地一致 | MouseSpring刚度/阻尼/InterpolationSpeed、抓取点/方向、帧率 | 对齐预览操作，不修改已对齐的关节数据 |
| 改骨名后按骨查询失败 | PA瞬态BodySetupIndexMap | 保存/重载候选并回读，不重复新增同骨body |

## 有效参数要连同开关比较

- MassInKgOverride、惯性/质心/迭代数字要同时看各自override标志；不用未生效的默认数字计算“实际质量”。
- CCD BodyInstance启用位还受全局DisableCCD及运动类型影响。
- PM的摩擦/反弹Combine设置要与其覆盖位和接触另一侧PM一起判断。
- 软限位Stiffness/Damping要看bSoftConstraint和相应自由度模式；锁定、自由与有限不是相同意义。
- 关节的DriveMode、各驱动启用位、MaxForce/Torque及目标不是独立可互换的数字。零值的语义按具体API/引擎实现核对，不能一律认作“关闭”。
- PCA稀疏更新里的bEnable...决定本次覆盖哪些字段；“值相同但掩码不同”是有效差异。
- 父空间和世界空间控制同时启用时可能竞争；ragdoll并不必然意味着所有ParentSpace控制都关闭。本轮源ragdoll仍保留ParentSpace AngularStrength=10、DampingRatio=3，照源保留。
- PA SolverType=World与RBAN不同；`PhysicsAssetSolverSettings`注释标明RBAN用途，不把其6/1/1直接当作World solver实际全部迭代。

## 比较顺序

1. 先停在参考姿势，检查body/constraint/disabled-pair拓扑和几何。
2. 验证关节框架映射和source/default状态，之后比较所有动力学参数与PM例外。
3. 核对PA级、PCA Setup/Profiles/编译、项目实际PhysicsSettings与相关CVar。
4. 匹配预览初姿、地面、重力、实际帧率、时间长度、外力/拖拽和动画设置。
5. 由用户先自由倒地，再单部位受力、不同落地方向，最后进入真实游戏运行链；记录每轮改变的单一因素。

不是把每个数字越大越稳定。源行为良好而目标有问题时，先找已证实差异，不引入与源不同的新补偿项。确需针对目标体型调参，记录它是有意适配而不是逐值复制，并说明表现代价。

## 验收记录建议

| 范围 | 证据 |
| --- | --- |
| 文件与静态回读 | PA/PCA/PM路径、包hash、拓扑/形状/材质/Profiles/配置差异、编译状态、依赖、dirty状态 |
| 初始几何 | 两端锚点误差、轴向/正交性、碰撞重叠、新旧参考姿势截图 |
| 自由倒地 | 同条件1/5/10秒，停止外力后的漂移、稳定时间、显著拉伸/穿透 |
| 受力 | 同部位/方向/强度，头颈/四肢/躯干，恢复和睡眠是否合理 |
| 预算 | 30/60等实际目标帧率与卡顿场景、子步进上限、World/RBAN消费路径 |
| 游戏运行 | 模拟进入/退出、PCA Profile切换、动画混合/后处理、胶囊跟随、恢复、外部移动/网络影响 |

工程约定将运行测试交由用户，代理提供清晰复现条件和静态结果；不编写测试代码或伪造运行结论。用户一句“效果很好”可以作为这轮实际试验的反馈，不能扩大成全部网络、姿态和低帧率场景验收。

## 需要手动处理时给用户什么

不要只说“MCP不支持”。先给已完成的候选、具体资产/骨/约束名称、当前与目标值、为什么不能自动处理、精准菜单入口及预期统计；然后只请用户完成缺失步骤。

例如：工具不能完整写入碰撞表时，提供已经映射的全体body/constraint/collision-pair剪贴板文本和目标计数。说明目标多余约束必须在备份副本中移除，粘贴不会自动删；接着 `Ctrl+Shift+V` 粘贴，并核对复制/粘贴提示及左上统计。不得让用户在没有备份、参考轴和明确目标值时逐关节猜着调。
