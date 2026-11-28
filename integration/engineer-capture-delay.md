# 工程师入口外读条（Phobos-Mix）

配置与状态分别位于 Ext/InfantryType 和 Ext/Infantry。Engineer.CaptureDelay 默认0，仅当Engineer=yes、Capture任务、非盟友且目标不是桥梁维修小屋时启用入口等待。它不重写Ares的多工程师公式，不接管驻军或渗透，不改变占领资格。

复用标准Walk::Process，新增75AC85..75AC89入口，原指令是PUSH 1 / LEA ECX,[ESI-4]（6A018D4EFC）。ESI为ILocomotion指针。倒计时期间将EAX恢复为[ESI]中的虚表指针，再跳至75AC99，保留该函数原有PUSH ESI / Is_Moving调用及POP ESI/RET 4，跳过本帧移动控制与wake发射，不跳过工程师的其他AI。正常路径重放原指令后继续75AEC0移动控制。已有wake入口覆盖75AC93..75AC98，包含75AC97的MOV EAX,[ESI]，因此不能直接跳至75AC97；入口和所有直接续执行地址均须核对现有补丁范围。

修订2修复了首次进入读条时的崩溃。修订1跳入wake入口覆盖区，快照中的异常位置为75AC97，实际字节为02 90而非原版8B 06。修订2续执行位置75AC99恰在该覆盖区之后，并显式补回被跳过的虚表寄存器加载。日志中的EngineerCapture revision=2用于区分产物。

入口触发使用ILocomotion::Head_To_Coord，未形成即时坐标时读取原生PathDirections[0]和CellSpread::GetNeighbourOffset，检查下一格属于目标建筑，不能用远程Destination建筑中心作为到达条件。已处于建筑内部的不重新启动计时。计时结束保留同一次任务的完成状态直至原生消耗工程师，避免下一帧再次开始。

TechnoClass_AI既有共享入口更新取消状态，不改写新的玩家命令。目标指针通过现有Detach::Listener<TechnoClass>清理。计时器、目标及双方原归属进入Infantry扩展流序列化，避免存取后即时完成或重新开始。绘制复用6D9076共享的可见对象渲染入口，无需选中。检查房屋关系和迷雾；不按敌方工程师进度泄露情报。

没有每帧全图或全单位扫描。默认0时移动入口直接回退原流程。不新增Phobos的永久不能移动状态，也不改变已有CantMove/接受命令的规则，因此读条中仍能撤销、移动、攻击或换目标。

新DLL应由开发者在游戏中检查90帧入口等待、受伤、改令/目标变化、多人/AI、多工程师扣血、友军修理、进度条可见性和中途存读档。无需独立回归测试。

修订3增加Engineer.CaptureProgress.Offset/SHP/Palette。Offset为屏幕像素，对两种绘制方式统一加在原有中心位置；默认仍绘制32x5矩形条。SHP由原有资源读取接口加载，用所有帧线性表示进度，默认ANIM_PAL，可指定CustomPalette。缺失或无帧资源继续矩形回退；存档复用OptionalSavedShape，缺失可选资源不使存档读取失败。绘制复用引擎SHP缓存及DrawSHP，不逐帧自行读取文件或修改逻辑状态。

原版520F40更新移动序列，即使暂停位移，仍会因Destination存在而选择Walk。新增520FAF..520FB4入口，覆盖MOV ECX,[EBP+5A4h]（8B8DA4050000）。此时SUB ESP,1Ch / PUSH EBP / PUSH ESI均已完成，EBP为步兵。仅在读条未结束时播放原生Idle1（缺失则Idle2、Ready），保留正在播放的Idle1/2或WetIdle1/2，不每帧强制重启；随后跳至52130D的POP ESI / POP EBP / ADD ESP,1Ch / RET（5E5D83C41CC3）。原版ProcessSequence继续推进动画帧并处理序列结束，PlayAnim保留两栖水中序列映射。读条外完全回退原路径。新入口和续执行地址均须核对已登记模块的覆盖区间。
