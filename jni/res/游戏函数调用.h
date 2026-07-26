#pragma once
#include <string>

// 调用游戏内授权/测试函数。
// 当前用于 CTF/自有游戏测试：触发 GameCoreCenter.instance.SendDevide()，即游戏源码里的“分身/分裂”操作。
// 实现方式：只使用 IL2CPP 官方 C API 的 il2cpp_runtime_invoke，避免直调 methodPointer/错误 hook 导致闪退。
bool GameCall_InvokeDevide(std::string& outMessage);

// 调用游戏内吐球函数（FreeTypeClick）。
// 正常对战模式走 ReqFreeType → UDP FeedType，与训练营的 PlayerSpiting/MsgFeed 路径不同。
// 实现：先 SetFreeTypeFlag(1)（设吐球方向），再 FreeTypeClick(true)（force 跳过 DisableFeedControl 检查）。
bool GameCall_InvokeSpitBall(std::string& outMessage);

// 吐球加速：在一帧内多次调用 FreeTypeClick(true) 实现加速吐球。
// count 为每帧调用次数（1-20）。FreeTypeClick 无客户端时间限制，直接走 UDP FeedType。
// 返回实际成功调用的次数。
int GameCall_InvokeSpitBallFast(int count, std::string& outMessage);

// 直接调用 NetworkUpdater.instance.ReqFreeType(0, 1) N 次。
// 绕过所有检查（HaveSelfBall、DisableFeedControl、BtnIsFeeding、SpitingDeltaTime）。
// type=0 单次吐球，flag=1 吐球方向。每帧调用 N 次实现加速。
int GameCall_InvokeSpitBallDirect(int count, std::string& outMessage);

// 三角合球：在后台线程中执行左分身→右分身→合球方向→循环分身合球。
// 角度转方向向量：x=sin(angle), z=cos(angle)。
// 运行期间 g_triangle_running=true，再次点击则停止。
void GameCall_TriangleMerge(std::string& outMessage);

// 停止三角合球
void GameCall_StopTriangleMerge();

// 修改视野大小：通过 IL2CPP 设置 CameraFollow.selfCamera.orthographicSize
// scale 为倍率（1.0=原始，1.5=放大50%，2.0=放大100%）
// 每帧调用以对抗游戏 Update2D() 的 Lerp 拉回
void GameCall_SetViewScale(float scale, std::string& outMessage);

// 重置视野修改状态（关闭时调用，允许下次重新触发 GameOverPullUp）
void GameCall_ResetViewPullup();

// 修改粘合值：设置 DrawCircle.ATime_SettingOffet 静态字段
// value 为偏移量（0=默认，值越大粘合越快）
void GameCall_SetMerge(float value, std::string& outMessage);
void GameCall_ResetMerge();

// 排名名字修改：独特 rank_id（数据_extract 语义），禁止 TopPlayers 排行榜
// 优先 NetPlayer.LastRank(>0)，否则 PlayerBase.ID；PlayerBase.Rename 刷新显示
void GameCall_RenameToRank(std::string& outMessage);
// 关闭排名改名时：用 PlayerBase.OriName 恢复所有人名字
void GameCall_RestoreAllNames(std::string& outMessage);

// 独立：球上名字大小。Hook TargetNameScale2.Update，在原公式 *1.875 之后再 *mul
void GameCall_TickNameScale(float mul);

// 调用游戏内摇杆：GameCoreCenter.instance.Move(UnityEngine.Vector3 direction)。
// x/y/z 对应 UnityEngine.Vector3 的三个 float；停止时请调用 GameCall_StopMove。
bool GameCall_InvokeMove(float x, float y, float z, std::string& outMessage);
bool GameCall_StopMove(std::string& outMessage);

// 最近一次调用状态。
std::string GameCall_GetLastStatus();

// 运行时调试信息；函数名保留兼容 UI，当前内容是 IL2CPP/runtime_invoke 调试，不再是 Dobby hook。
std::string GameCall_GetHookDebugInfo();
