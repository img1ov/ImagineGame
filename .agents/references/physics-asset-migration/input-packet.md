# 用户提前准备的资料

这是AGENTS的资料收集规范，不是要求用户配置工具。优先利用工程和已给附件；用户不需要编写映射表、JSON或脚本。

## 最少资料包

资料齐全后，代理先批量对照，不要求用户逐个打开几十个关节。能由已连接MCP读取的内容不再要求用户重复填写；剪贴板和截图用于补全工具盲区与表现证据。

| 资料 | 源、目标分别准备什么 | 作用 |
| --- | --- | --- |
| 身份 | 工程绝对路径、引擎版本、MCP端口；PA/PCA/Mesh资产完整路径 | 防止操作错编辑器、错网格、错同名资产 |
| PA剪贴板 | 全部刚体、全部约束、全部禁用碰撞对的文本，标明源/目标与时间 | 一次比较拓扑、形状、BodyInstance、ConstraintInstance和碰撞关系 |
| PA截图 | 非模拟参考姿势全身、左上统计；Solver Settings完整内容 | 看形状适配、体数、约束/交叉约束、碰撞组合与PA级设置 |
| 表现 | 相同操作下的源/目标截图或短视频，注明经过时间、帧率、拖拽/冲量 | 静态截图无法证明持续漂移、频率或偶发抽搐 |
| PCA | 资产路径、当前Profile、复现是在PA编辑器、PCA编辑器还是游戏内 | 避免将PA自由倒地和主动控制结果混为一谈 |
| 修改边界 | 可改哪些PA/PCA/材质/Config；是否要保留已有特殊约束与玩法配置 | 明确哪些差异应迁移，哪些是目标项目的业务设计 |

有未保存修改时先说明。代理应保留实时版本，不用磁盘文件覆盖编辑器中的新修改。

## 如何一次复制 PA

UE 5.8 本轮实测菜单为 **Copy Selected Bodies/Constraints To Clipboard**，快捷键 `Ctrl+Shift+C`。不是 `Copy Properties`，也不是 `Copy Selected Shapes To Clipboard`。

1. 停止模拟，打开正确PA。清除Skeleton Tree/Graph过滤，显示全部物理刚体和约束。
2. 在Skeleton Tree或Graph中全选需要迁移的**刚体与约束**。当前UI模式未显示约束时，单独切到约束选择并补充复制；也可用Graph混合全选。`Ctrl+A`是否覆盖两类选择以右下角复制提示为准，不凭高亮骨骼数量推测。
3. 执行上述复制命令。记下提示的 `N bodies, M constraints and K disabled collision pairs`，检查N/M与左上统计一致。
4. 粘贴为完整文本附件，命名如 `source_PA_UEFN_all_20261125.txt`、`target_PA_PC_all_20261125.txt`。长文本不要只截开头，不要手工删除看起来重复的默认值或 `End Object`。
5. 无法混选时分别交 `source_bodies.txt`、`source_constraints.txt`，目标同样处理。全体刚体那份必须包含禁用碰撞对；约束份若显示0 bodies，不能据其中0 collision pairs认定全PA没有禁用对。

源码已核对：复制函数单独遍历 `UniqueSelectionReferencingBodies()` 和 `SelectedConstraints()`；禁用碰撞对只枚举**此次复制的全部刚体集合内部**。只选一个身体部位无法取得它与其他未选身体的完整碰撞关系。

完整文本通常包含：

```text
Begin Object Class=/Script/Engine.SkeletalBodySetup ...
   AggGeom=...
   DefaultInstance=...
   BoneName="..."
End Object
Begin Object Class=/Script/Engine.PhysicsConstraintTemplate ...
   DefaultInstance=(JointName=...,ConstraintBone1=...,ConstraintBone2=...,...)
End Object
Begin Object Class=/Script/PhysicsAssetEditor.PhysicsAssetCollisionPair ...
   BoneNameA="..."
   BoneNameB="..."
End Object
```

此处是数据格式示意，不是要求用户手填字段。旧UE版本若无CollisionPair导出，提供完整PA包及碰撞统计，由代理核对版本实现。

## 剪贴板没有完整覆盖什么

| 缺口 | 最省事的补充 |
| --- | --- |
| PA级SolverType、SolverSettings、Profiles名称列表、预览Mesh | Solver Settings截图和PA/Mesh路径，或MCP完整资产导出 |
| PhysMaterial引用指向的材质实际参数 | PM资产路径；可由MCP读取，无需逐体截图 |
| PCA的Setup、Sets、Control Updates、参数启用位、编译数据 | PCA资产路径及正在使用的Profile名称 |
| 骨骼父子结构、参考姿势、局部轴、单位/缩放 | 明确源/目标Mesh；不同命名骨架可补骨名/角色对应表 |
| 项目有效PhysicsSettings、CVar、编辑器鼠标弹簧、重力和帧率 | 两边Config目录/工程路径；离线时导出相关INI片段及Tools设置截图 |
| 动态表现和运行时覆盖 | 同操作视频、触发方法、对应角色/蓝图/组件、具体异常时刻 |
| 未保存的实时改动 | 告知dirty状态，实时导出文本；磁盘uasset备份不等于实时备份 |

不要把整份Config、所有插件、整个Content压缩包作为默认要求。已连接且可读时代理自行采样；只有缺失的特定信息才补交。

## 对照截图 / 视频怎么拍

- 第一张：停止模拟后的参考姿势，看到PA名字、全身碰撞形状、左上统计、SolverType。
- 第二组：相同初姿、相同Profile、相同重力和地面、相同帧率目标，点击Simulate后约1秒、5秒、10秒。自由落体时先不拖拽，另拍相同部位/方向的拖拽或冲量。
- 看滑动时保持摄像机和地面参照不动；给出“停止外力后还滑多久/大约多远”。只拍落地瞬间不足以区分惯性与持续驱动。
- 游戏内异常额外注明：何时启用物理/PhysicsControl、正在播放的动画、Mesh缩放、胶囊是否还碰撞、有无Attach/位置跟随、是否联网。
- 不同骨架无法姿势逐像素一致；关注落地稳定时间、持续漂移、关节拉伸/穿透、方向、恢复和受力响应。

## 可直接填写的请求模板

```text
目标：让目标角色的PA/PCA行为对齐源角色；当前问题：____。
源工程：____；UE版本：____；MCP端口：____。
源PA：____；源PCA：____；源Mesh：____。
目标工程：____；UE版本：____；MCP端口：____。
目标PA：____；目标PCA：____；目标Mesh：____。
复现场所：PA编辑器 / PCA编辑器 / 游戏内；使用Profile：____。
复现操作与时刻：____；帧率/重力/地面/外力：____。
允许修改：目标PA / 目标PCA / 新物理材质 / 项目PhysicsSettings / ____。
需要保留的特殊设计：____；未保存修改：有/无，说明____。
附件：源全体PA文本、目标全体PA文本（各注明复制提示N/M/K）。
附件：两边统计/Solver Settings截图；相同操作表现对照。
如骨名不同：已知对应关系____；不确定部位____。
请先完整对照和备份，按实际骨架适配，不用增加阻尼掩盖坐标/碰撞错误。
```

用户没有预先给映射表时，代理应先从骨架结构与网格提出具体映射，只把无法确定的部位集中问清。不能把准备映射本身全部推回用户。
