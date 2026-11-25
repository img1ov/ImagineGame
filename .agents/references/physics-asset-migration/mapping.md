# 骨骼、关节与形状映射

## 先确认映射对象

动画Skeleton可以有数百骨骼，PA只覆盖其中一部分。以 `SkeletalBodySetup.BoneName` 建物理体集合，以 `ConstraintBone1/2` 建物理图；不要把twist、IK、socket骨数量当作刚体数量。稳定标识使用骨名和约束连接两端，`SkeletalBodySetup_18` 这类内嵌对象名只用于定位序列化对象，数组索引只用于保持完整克隆后的碰撞表。

映射方向统一为 **源骨名 → 目标骨名**。关节必须明确child/parent；不要凭ConstraintTemplate对象编号或JointName猜方向。UE `ConstraintBone1` 是child，`ConstraintBone2` 是parent。

| 骨架差异 | 处理 |
| --- | --- |
| 同名、同参考姿势、同轴向和比例 | 完整复制后回读；仍确认碰撞图和实际Mesh |
| 同名、不同参考姿势/骨长 | 保留动力学设置，重新计算关节父框架；形状单独评估 |
| 名称不同、语义一一对应 | 显式bone_map；重写Body BoneName和两约束端点；保持刚体数组顺序可保留按索引存储的碰撞表 |
| 局部轴旋转、左右轴符号不同 | 给出child_basis；同时转换子框架和形状，不能只改Pos2 |
| 多出非物理twist/辅助骨 | 保留目标动画骨架；物理图仍连接映射后的物理体，检查实际跨越的变换链 |
| 目标缺少某个源物理骨 | 不映射到root或最近同名骨凑数；选择新目标物理骨、重建图或明确合并设计 |
| 多源体合并到一目标体 | 通用脚本拒绝。需重建质量/质心/惯性、几何、约束、碰撞规则和PCA集合 |
| 一个源体拆成多目标体 | 新增体和约束，重新分配质量、惯性、力矩/驱动与碰撞图，不能复制一体质量到每个新体 |
| 非均匀/负缩放、镜像或不同物种 | 不是单位四元数变换；需专门的仿射/拓扑适配与形状重建 |

代理应先从骨架结构、位置、左右命名、关节活动语义和网格拟合提出对应表。只把无法确定的部位集中问用户；不按名字相似度自动批准。

## 参考姿势来源和坐标空间

本轮已验证入口：

```python
mesh = unreal.load_asset(mesh_path)
pose = unreal.AnimPoseExtensions.get_reference_pose(mesh.get_editor_property('skeleton'))
names = unreal.AnimPoseExtensions.get_bone_names(pose)
local = unreal.AnimPoseExtensions.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.LOCAL)
component = unreal.AnimPoseExtensions.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.WORLD)
```

这里的WORLD是AnimPose骨架参考姿势空间，采样时没有场景Actor变换；不能称作某个运行角色的实时世界姿势。该API读取的是 **Skeleton存储的参考姿势**。不同Mesh共享Skeleton且导入绑定姿势不同，必须另核对Mesh自己的ReferenceSkeleton或实例参考姿势；不能直接把这个API结果当作任意Mesh的真实bind pose。没有可靠的Mesh参考变换时，计划可以生成，但 `review.reference_pose` 不应通过。

记录单位cm、手性、各骨局部与参考姿势全局变换、scale、源/目标Mesh与Skeleton身份。动画retarget pose、动画首帧、PhysicsAsset当前模拟姿势都不能静默替代bind/reference pose。

通用脚本只接受单位参考scale。普通Actor的非1缩放也要在表现验证中单独记录；它不会因为骨架参考scale全1而自动消失。

## 本轮关节框架公式

下面采用列向量约定，四元数乘积 `A * B` 表示先B后A，四元数数组顺序为 `[x,y,z,w]`。不要直接套用UE `FTransform`/其他库可能相反的乘法顺序。

- `S_c,S_p`：源child/parent参考姿势骨变换；旋转记 `R_Sc,R_Sp`。
- `T_c,T_p`：目标映射child/parent参考姿势骨变换。
- `p1,p2`：源关节在child/parent骨局部空间中的锚点。
- `X1,Y1,X2,Y2`：`PriAxis1/SecAxis1/PriAxis2/SecAxis2`。
- `B_c`：源child骨局部坐标到目标child骨局部坐标的**主动旋转映射**。同轴则单位四元数。
- `s_c`：明确选择的child几何/偏移统一缩放，默认1。

位置适配：

```text
p1_target = s_c * (B_c * p1)
anchor_target = T_c.TransformPosition(p1_target)
p2_target = T_p.InverseTransformPosition(anchor_target)
```

保持源参考姿势下两关节框架的相对角度：

```text
X1_target = B_c * X1_source
Y1_target = B_c * Y1_source
D = inverse(R_Tp) * R_Tc * B_c * inverse(R_Sc) * R_Sp
X2_target = D * X2_source
Y2_target = D * Y2_source
```

因此 `inverse(R_Tc * F1_target) * (R_Tp * F2_target)` 等于源的 `inverse(R_Sc * F1_source) * (R_Sp * F2_source)`。这是保留源限制中心/姿势偏置的重要条件；不是把两端轴都重设为世界XYZ，也不是把参考姿势下的关节角强行清零。

本轮 `B_c=Identity,s_c=1`：保留Pos1和子轴，仅改Pos2与父轴。`AngularRotationOffset`、ProfileInstance中的限位/驱动目标等不因此重置；它们已经表达在关节框架中。若主动改变框架语义、镜像某一自由度或重新定义drive target，则要另行转换目标和限制，这不属于上述保持语义的适配。

## 如何确定 child_basis

child_basis不是两骨“当前世界旋转之差”的自动同义词。先选解剖局部坐标：沿肢段的轴、关节弯曲方向、第三轴由叉积得到；将源的同语义右手坐标组映射到目标的同语义右手坐标组。参考姿势的举臂角度应由T/S变换处理，不应又被B重复补偿。

可以用骨到下一物理关节的方向定纵轴，用膝/肘自然弯曲平面或网格方向定横轴。单根直线无法唯一决定扭转方向；多解处须从骨轴、网格或用户定义确定。左右两侧分别检查，不把左侧B直接复制到右侧。

记录每个非单位B的依据和验证截图。若所得3×3变换行列式为-1，则是反射，不能塞进单位四元数；先做镜像语义与几何重建设计。

## 形状适配与质量

本轮保留了源全部形状尺寸，用户实测效果良好。这仅证明UEFN→Manny本例可行，不能推广为任意骨架都不必拟合。

- 局部旋转映射B：中心 `c'=s Bc`，盒/胶囊朝向 `Q'=B Q`。普通球只变中心与半径。
- 统一缩放s：球半径、胶囊Radius/Length、盒XYZ均乘s。UE胶囊Length字段与总外形长度的关系按本地 `FKSphylElem` 实现核对，不能把半长与全长混用。
- 非均匀拉长肢段时，球/胶囊无法精确表示一般仿射变换后的形状，不能只缩放Center却保留错误端点。选择重新拟合、改变胶囊长度并单独决定半径，或重建复杂几何。
- 脊柱/骨盆胶囊常横向放置，不一定沿骨X。不能把“下一骨距离倍率”同时套到每个形状的全部尺寸。
- 形状拟合关注关节周围覆盖、相邻体重叠、自碰撞可启用对、脚掌接触平面、初始脚底高度、网格明显穿出和质心。
- BodyInstance质量只有 `bOverrideMass=true` 时对应覆盖值。false时实际质量还受几何、密度、MassScale、RaiseMassToPower等影响。尺寸改变后惯性、重心和相对关节力臂会变，即使数字质量相同也不等于动态相同。

脚本自动转换只支持球、盒、普通胶囊及正统一缩放。凸包、tapered capsule、level set等保留源几何时可随完整克隆保留；一旦需要旋转/缩放会拒绝自动适配，避免漏改顶点或烘焙数据。

## 拓扑、碰撞与PCA跟随映射

完整克隆保持body数组顺序，逐体只改骨名时，源碰撞禁用表的索引对仍表达对应的两物理体。删除、排序、合并body后该推理不再成立，必须用**骨名/语义对**重建并回读碰撞规则。每个禁用对要求两端存在且不同；映射后的重复对与自配对都应报告。

PCA至少检查：LimbSetupData.StartBone、额外Control的父子骨、BodyModifier.BoneName、Control/Modifier Sets成员、Profiles更新项所引用的控制/集合名称、父PCA和AdditionalProfileAssets、运行时调用的Profile名。

脚本按已知骨字段和精确 `name/names` 值做映射，`reference_name_map` 用于生成控制名等附加名称；**不做子串替换，不自动改任意字典键**。额外Control/Modifier字典键、自定义命名生成规则、继承PCA或目标特有控制集合，须审阅并扩展映射，不能仅Compile无错误就认为所有更新项都找到了运行时对象。

## 接受适配计划的证据

- 全部物理体和约束端点被唯一映射，目标骨确实存在，左右与父子语义正确。
- 源初始锚点误差单独报告；有明显误差时先解释来源，不静默抹掉源的设计。
- 目标两端锚点世界位置重合，轴单位且正交、方向符合目标关节；再保存并回读实际值。
- Profile、BodyInstance默认/覆盖位、材质空引用及显式材质均符合决定。
- 改骨名后保存并卸载/重载候选，重建 `UPhysicsAsset::PostLoad/UpdateBodySetupIndexMap` 的查找缓存。
- 参考姿势下几何关系合理，不因启用源自碰撞图引入新的深重叠。
- 最终用户模拟验收与静态几何验证分开记录。动画、PCA主动驱动、场景接触和网络仍有独立边界。
