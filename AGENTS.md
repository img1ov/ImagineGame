# ImagineGame 工程约定与速查

## 通用工作约定
- 结论和代码以本地源码、官方资料或可复核案例为依据；明确区分事实、资产名线索和待验证事项。
- 修改前核对现有调用链、同类实现和所属模块，在原职责处扩展，避免重复状态与冗余逻辑。
- 新增 struct 时，用户指定目标 `.h` 就写在该文件；未指定先调查位置，再请用户确定头文件。
- 只在关键约束和生命周期边界写最短注释；回复只给结论、证据、变更和必要风险。

## 方向
- UE 5.8 C++ 项目。Gameplay 规则由 C++ 实现；蓝图主要负责配置、资产装配和事件转发。当前不引入脚本语言。
- 工程内新增名称、注释、文档不得沿用参考项目名称或前缀。项目专有类型沿用 `IMG`；引擎扩展和通用类型先检查同目录现有命名，不机械加前缀。
- 未经用户明确要求，不运行编译；实现后做静态调用链与差异检查。

## 已核对的结构
- `Source/ImagineGame`：运行时共享框架；`Source/ImagineEditor`：编辑器模块。
- `Plugins/GameFeatures/ShooterCore/Source/ShooterCoreRuntime`：射击玩法专属 C++；该插件依赖 `ImagineGame`。其 `Content` 用于玩法装配与配置。
- `Source/ImagineGame/Public` 与 `Private` 依职责对应：`AbilitySystem`（能力、属性、阶段）、`GameModes`（Experience 与模式入口）、`Character`/`Player`（Pawn 初始化与玩家状态）、`Equipment`/`Inventory`/`Weapons`（装备、物品与武器）、`Interaction`、`Teams`、`Input`、`Messages`、`GameFeatures`、`UI`。
- 已有 `UIMGExperienceDefinition`、`UIMGExperienceManagerComponent`、`UIMGAbilitySet`、`UIMGGameplayAbility`、`UIMGGameplayAbility_RangedWeapon`、`UIMGEquipmentManagerComponent`、`UIMGInventoryManagerComponent` 等基类；开发具体玩法前先检查这些类及现有调用链。
- Experience 由 `AIMGGameMode` 选择、`UIMGExperienceManagerComponent` 加载；能力集可由装备和 Game Feature Action 授予。武器目标采集与 TargetData 回调已有 C++ 实现。`Content/Characters/Heroes/Abilities` 已有移动能力资产，移植前先核对现状。
- 背包增删、快捷栏切换、装备授予/卸除、武器生成器冷却、伤害计算与生命/死亡状态已有 C++。拾取发放与重复武器补弹在 `Weapons/IMGWeaponSpawner::GiveWeapon`；装备先关联物品，再授予能力。远程武器 TargetData 的原生扩展点在 `Weapons/IMGGameplayAbility_RangedWeapon`。不保留旧 Gameplay 蓝图事件兼容入口。
- 通用重生能力在 `GameModes/IMGGameplayAbility_AutoRespawn`；射击、装弹、自动装弹在 `ShooterCoreRuntime/Weapons`，快捷栏输入与丢弃在 `ShooterCoreRuntime/Equipment`。装备能力通过装备实例获取背包物品，弹药使用物品的 StatTagStack。射击伤害在服务端重查目标并以服务端命中结果计算。
- UI 可订阅 `IMG.QuickBar.Message.SlotsChanged`、`IMG.QuickBar.Message.ActiveIndexChanged`、`IMG.Inventory.Message.ItemStatsChanged`；武器状态组件有本地 `OnShotConfirmed` 委托；生命组件有血量/死亡委托。装备物品引用可在 `OnInstigatorChanged` 后读取，装弹有开始/结束表现事件；重生能力另有可配置的开始/完成消息 Tag。

## 落点
- 多玩法共用机制放 `Source/ImagineGame` 的现有职责目录；仅射击玩法的规则放 `ShooterCoreRuntime`。配置留在对应 Content。
- 新规则先核对现有所有者、生命周期、服务端权限与复制路径；扩展现有类，避免另建平行系统。
- 本文件只保留已核实、长期有用的事实；目录和调用链改变时同步更新。


