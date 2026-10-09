using System;
using System.Collections.Generic;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;
using ByteDance.Live.Foundation.Logging;
using ByteDance.LiveOpenSdk;
using ByteDance.LiveOpenSdk.Push;
using ByteDance.LiveOpenSdk.Report;
using ByteDance.LiveOpenSdk.Round;
using ByteDance.LiveOpenSdk.Room;
using GeometricWarfare.DouyinHost;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
using UnityEngine;

// Editor-only: run the actual host queue/Update/Execute against public SDK test doubles.
// No SDK private reflection and no runtime fixture injection API.
public static class HostSchedulingTests
{
    static FieldInfo Field(string name) => typeof(DouyinSdkHost).GetField(name, BindingFlags.Instance | BindingFlags.NonPublic);
    static object Call(DouyinSdkHost host, string name, params object[] args) => typeof(DouyinSdkHost).GetMethod(name, BindingFlags.Instance | BindingFlags.NonPublic).Invoke(host, args);
    static void Check(bool value, string why) { if (!value) throw new InvalidOperationException("Host scheduling: " + why); }
    static bool Enqueue(DouyinSdkHost host, JObject command, int? size = null) => (bool)Call(host, "TryEnqueueInput", command, size ?? System.Text.Encoding.UTF8.GetByteCount(command.ToString(Formatting.None)));
    public static void Run()
    {
        var gameObject = new GameObject("HostSchedulingFixture"); gameObject.SetActive(false);
        var host = gameObject.AddComponent<DouyinSdkHost>();
        var sdk = new SdkFixture();
        Field("sdk").SetValue(host, sdk); Field("initialized").SetValue(host, true); Field("connected").SetValue(host, true); Field("roomId").SetValue(host, "123");
        try {
            Check(Enqueue(host, JObject.Parse("{\"op\":\"round\",\"id\":\"slow\",\"room_id\":\"123\",\"round_id\":1,\"status\":1}")), "round queued");
            Call(host, "Update"); Check(sdk.Round.Calls == 1 && (bool)Field("busy").GetValue(host), "SDK round genuinely awaiting controlled delay");
            Check(Enqueue(host, JObject.Parse("{\"op\":\"user_group\",\"id\":\"next\",\"room_id\":\"123\",\"round_id\":1,\"open_id\":\"viewer\",\"group_id\":\"red\"}")), "next normal command queued");
            var pending = (Dictionary<string, string>)Field("pendingAck").GetValue(host);
            for (int i = 0; i < 600; ++i) {
                string id = "message-" + i; pending.Add(id, "live_comment");
                Check(Enqueue(host, new JObject { ["op"] = "ack", ["id"] = id, ["room_id"] = "123", ["msg_id"] = id, ["msg_type"] = "live_comment" }), "ACK burst above 256 fits independent lane at index " + i);
            }
            Call(host, "Update"); Check(sdk.Ack.Count == 128, "ACK budget is bounded each frame");
            for (int i = 0; i < 4; ++i) Call(host, "Update");
            Check(sdk.Ack.Count == 600 && pending.Count == 0, "all ACKs fulfill before slow round completes");
            Check((bool)Field("busy").GetValue(host) && sdk.Round.Groups == 0, "ACK does not clear ordinary busy or reorder normal commands");
            sdk.Round.Release.SetResult(new ResultFixture());
            Check(!(bool)Field("busy").GetValue(host), "normal completion clears busy");
            Call(host, "Update"); Check(sdk.Round.Groups == 1, "normal FIFO resumes after completion");
            for (int i = 0; i < 8192; ++i) Check(Enqueue(host, new JObject { ["op"] = "ack" }), "bounded ACK lane admission");
            Check(!Enqueue(host, new JObject { ["op"] = "ack" }), "ACK lane full rejects");
            Check(Enqueue(host, new JObject { ["op"] = "user_group" }), "normal capacity remains independent");
        }
        finally { Field("sdk").SetValue(host, null); UnityEngine.Object.DestroyImmediate(gameObject); }
        CheckNormalAndByteLimits();
        CheckStartupGate();
        CheckStartupEventCleanupAndLimits();
        CheckDiagnosticPrivacy();
        System.IO.File.WriteAllText("host-scheduling-checks.txt", "PASS: real host Update delayed SDK round + 600 ACKs, 128/frame budget, normal FIFO/busy isolation, 8192 ACK and 256 normal limits, shared 16MiB; startup room-before-event order, failed-init/room-change cleanup, 1024 event and 16MiB startup limits\n");
    }
    static void CheckDiagnosticPrivacy()
    {
        string sentinel = "fake-token-and-private-comment";
        var projected = HostDiagnostics.ProjectStatus(sentinel, false, 42, sentinel).ToString();
        Check(!projected.Contains(sentinel) && projected.Contains("unknown"), "unknown labels cannot leak payload or token");
        Check((string)HostDiagnostics.ProjectStatus("subscribe:live_team", true, 0, "all_subscribed")["op"] == "subscribe:live_team", "known startup stage retained");
        string path = System.IO.Path.GetFullPath("diagnostic-privacy-check.jsonl");
        if (System.IO.File.Exists(path)) System.IO.File.Delete(path);
        var log = new HostDiagnostics(path);
        log.Status(sentinel, false, -1, sentinel); log.Exception(new InvalidOperationException(sentinel)); log.Event(sentinel, 1); log.Event("live_comment", 1);
        Check(!System.IO.File.ReadAllText(path).Contains(sentinel), "disk diagnostics omit exception messages and arbitrary labels");
        System.IO.File.WriteAllBytes(path, new byte[HostDiagnostics.MaxBytes - 10]);
        log.Status("host", true, 0, "ready");
        Check(new System.IO.FileInfo(path).Length <= HostDiagnostics.MaxBytes, "diagnostic disk size bounded");
        System.IO.File.Delete(path);
    }
    static void CheckStartupGate()
    {
        var obj = new GameObject("HostStartupFixture"); obj.SetActive(false);
        var host = obj.AddComponent<DouyinSdkHost>();
        var push = new PushFixture(); var rooms = new RoomServiceFixture();
        Field("initialized").SetValue(host, true); Field("connected").SetValue(host, true);
        Field("rooms").SetValue(host, rooms); Field("push").SetValue(host, push);
        var output = (Queue<byte[]>)Field("outputs").GetValue(host);
        int RoomFrames() { int count = 0; foreach (var bytes in output) if (System.Text.Encoding.UTF8.GetString(bytes).Contains("\"kind\":\"room\"")) ++count; return count; }
        int EventFrames() { int count = 0; foreach (var bytes in output) if (System.Text.Encoding.UTF8.GetString(bytes).Contains("\"kind\":\"event\"")) ++count; return count; }
        try {
            Call(host, "OnRoom", rooms.RoomInfo);
            Check(RoomFrames() == 0, "authenticated room must not start game before subscriptions");
            var task = (Task)Call(host, "StartSubscriptions", new[] { "live_comment", "live_team" }, "init");
            Check(push.Calls == 1 && !task.IsCompleted && RoomFrames() == 0, "first subscription awaiting, no game room");
            push.Release[0].SetResult(true);
            Check(push.Calls == 2 && !task.IsCompleted && RoomFrames() == 0, "partial subscription success cannot start round");
            Call(host, "OnMessage", new PrefollowedPresenceFixture());
            Check(EventFrames() == 0, "pre-followed entry during subscription is retained until room authentication frame");
            push.Release[1].SetResult(true);
            Check(task.IsCompleted && RoomFrames() == 1, "all subscriptions confirmed exposes room exactly once");
            Check(EventFrames() == 1, "pre-followed entry flushes after subscriptions complete");
            bool sawRoom = false;
            foreach (var bytes in output) {
                string frame = System.Text.Encoding.UTF8.GetString(bytes);
                if (frame.Contains("\"kind\":\"room\"")) sawRoom = true;
                if (frame.Contains("\"kind\":\"event\"")) Check(sawRoom, "room authentication precedes buffered platform events");
            }
            Call(host, "InvalidateSdkSession");
            Check(!(bool)Field("subscriptionsReady").GetValue(host), "invalid session clears startup gate");
        }
        finally { Field("sdk").SetValue(host, null); UnityEngine.Object.DestroyImmediate(obj); }
        obj = new GameObject("HostSubscriptionFailureFixture"); obj.SetActive(false); host = obj.AddComponent<DouyinSdkHost>(); push = new PushFixture();
        Field("initialized").SetValue(host, true); Field("connected").SetValue(host, true); Field("rooms").SetValue(host, rooms); Field("push").SetValue(host, push);
        output = (Queue<byte[]>)Field("outputs").GetValue(host);
        try {
            Call(host, "OnRoom", rooms.RoomInfo);
            var task = (Task)Call(host, "StartSubscriptions", new[] { "live_team" }, "init");
            Call(host, "OnMessage", new PrefollowedPresenceFixture());
            push.Release[0].SetException(new InvalidOperationException("secret-bearing SDK text must never be logged"));
            Check(task.IsCompleted && RoomFrames() == 0 && !(bool)Field("initialized").GetValue(host), "failed subscription cannot open a game session");
            Check(EventFrames() == 0, "failed initialization does not publish staged qualification");
            Field("initialized").SetValue(host, true); Field("connected").SetValue(host, true); Field("subscriptionsReady").SetValue(host, true);
            Call(host, "OnRoom", rooms.RoomInfo);
            Check(EventFrames() == 0, "new session cannot inherit failed initialization events");
        }
        finally { Field("sdk").SetValue(host, null); UnityEngine.Object.DestroyImmediate(obj); }
    }
    sealed class PushFixture : IMessagePushService
    {
        public int Calls;
        public readonly List<TaskCompletionSource<bool>> Release = new List<TaskCompletionSource<bool>>();
        public ConnectionState ConnectionState => ConnectionState.Connected;
        public event Action<ConnectionState> OnConnectionStateChanged;
        public event OnPushMessageCallback OnMessage;
        public Task StartPushTaskAsync(string type, MultiPushType mode = MultiPushType.SinglePush) { ++Calls; var source = new TaskCompletionSource<bool>(); Release.Add(source); return source.Task; }
        public Task StopPushTaskAsync(string type) => Task.CompletedTask;
    }
    sealed class RoomFixture : IRoomInfo { public string RoomId => "123"; public IUserInfo Anchor => new AnchorFixture(); }
    sealed class AnchorFixture : IUserInfo { public string OpenId => "anchor"; public string Nickname => "anchor"; public string AvatarUrl => ""; }
    sealed class PrefollowedPresenceFixture : IEnterRoomMessage {
        public string MsgId { get; set; } = "prefollowed-startup"; public string MsgType => "live_enter"; public long Timestamp => 100;
        public string SecOpenId => "viewer"; public string AvatarUrl => ""; public string NickName => "viewer"; public long IsOldPlayer => 0;
        public string GradeLevel => ""; public string InviterGatherOpenid => ""; public long EnterRoomType => 1;
        public string InviterGatherNickname => ""; public string InviterGatherAvatarUrl => ""; public int EnterRoomScene => 0; public long FollowStatus => 1;
    }
    static void CheckStartupEventCleanupAndLimits()
    {
        var obj = new GameObject("HostStartupEventCleanupFixture"); obj.SetActive(false);
        var host = obj.AddComponent<DouyinSdkHost>();
        var rooms = new RoomServiceFixture();
        Field("initialized").SetValue(host, true); Field("connected").SetValue(host, true);
        var output = (Queue<byte[]>)Field("outputs").GetValue(host);
        int EventFrames() { int count = 0; foreach (var bytes in output) if (System.Text.Encoding.UTF8.GetString(bytes).Contains("\"kind\":\"event\"")) ++count; return count; }
        try {
            Call(host, "OnRoom", rooms.RoomInfo);
            Call(host, "OnMessage", new PrefollowedPresenceFixture());
            Call(host, "OnRoom", new OtherRoomFixture());
            Field("subscriptionsReady").SetValue(host, true);Call(host, "OnRoom", new OtherRoomFixture());
            Check(EventFrames() == 0, "room change discards earlier room qualification before publication");
            Field("subscriptionsReady").SetValue(host, false);
            for (int i = 0; i < 1024; ++i) Call(host, "OnMessage", new PrefollowedPresenceFixture { MsgId = "startup-" + i });
            Check((bool)Field("initialized").GetValue(host), "startup event queue admits bounded capacity");
            Call(host, "OnMessage", new PrefollowedPresenceFixture { MsgId = "startup-overflow" });
            Check(!(bool)Field("initialized").GetValue(host), "startup event overflow fails session instead of dropping entitlement");
            Check(EventFrames() == 0, "overflow never exposes staged unbound events");
            Field("initialized").SetValue(host, true);Field("connected").SetValue(host, true);
            Call(host, "OnRoom", rooms.RoomInfo);
            var large = new LargeStartupCommentFixture();
            for (int i = 0; i < 7; ++i) {large.MsgId = "bytes-" + i;Call(host, "OnMessage", large);}
            Check((bool)Field("initialized").GetValue(host), "startup queue admits events within byte capacity");
            large.MsgId = "bytes-overflow";Call(host, "OnMessage", large);
            Check(!(bool)Field("initialized").GetValue(host) && EventFrames() == 0, "startup queue fails session at 16MiB byte bound before publication");
        }
        finally { Field("sdk").SetValue(host, null);UnityEngine.Object.DestroyImmediate(obj); }
    }
    sealed class OtherRoomFixture : IRoomInfo {public string RoomId => "456"; public IUserInfo Anchor => new AnchorFixture();}
    sealed class LargeStartupCommentFixture : ICommentMessage {
        public string MsgId {get;set;} public string MsgType => "live_comment"; public long Timestamp => 100;
        public IUserInfo Sender => new AnchorFixture();public string Content {get;} = new string('x',2*1024*1024);
    }
    sealed class RoomServiceFixture : IRoomInfoService
    {
        public IRoomInfo RoomInfo => new RoomFixture();
        public event Action<IRoomInfo> OnRoomInfoChanged;
        public Task<IRoomInfo> WaitForRoomInfoAsync() => Task.FromResult(RoomInfo);
        public Task<IRoomInfo> UpdateRoomInfoAsync() => Task.FromResult(RoomInfo);
    }
    static void CheckNormalAndByteLimits()
    {
        var gameObject = new GameObject("HostLimitsFixture"); gameObject.SetActive(false);
        var host = gameObject.AddComponent<DouyinSdkHost>();
        try {
            for (int i = 0; i < 256; ++i) Check(Enqueue(host, new JObject { ["op"] = "round" }), "normal lane admission");
            Check(!Enqueue(host, new JObject { ["op"] = "round" }), "normal lane full rejects");
            Check(Enqueue(host, new JObject { ["op"] = "ack" }), "ACK fits beside full normal lane");
        }
        finally { UnityEngine.Object.DestroyImmediate(gameObject); }
        gameObject = new GameObject("HostByteLimitsFixture"); gameObject.SetActive(false); host = gameObject.AddComponent<DouyinSdkHost>();
        try {
            for (int i = 0; i < 4; ++i) Check(Enqueue(host, new JObject { ["op"] = i % 2 == 0 ? "ack" : "round" }, 4 * 1024 * 1024), "shared bytes admission");
            Check(!Enqueue(host, new JObject { ["op"] = "ack" }, 1) && !Enqueue(host, new JObject { ["op"] = "round" }, 1), "shared 16MiB limit rejects both lanes");
        }
        finally { UnityEngine.Object.DestroyImmediate(gameObject); }
    }
    sealed class ResultFixture : IRoundDataRes { public long ErrCode => 0; public string ErrMsg => ""; }
    sealed class AckFixture : IMessageAckService
    {
        public int Count;
        public void ReportAck(string msgId, string msgType) { ++Count; }
        public void ReportAck(IPushMessage message) { throw new NotSupportedException(); }
    }
    sealed class RoundFixture : IRoundApi
    {
        public readonly TaskCompletionSource<IRoundDataRes> Release = new TaskCompletionSource<IRoundDataRes>();
        public int Calls, Groups;
        public Task<IRoundDataRes> UpdateRoundStatusInfoAsync(IRoundStatusInfo info) { ++Calls; return Release.Task; }
        public Task<IRoundDataRes> UpdateUserGroupInfoAsync(IRoundUserGroupInfo info) { ++Groups; return Task.FromResult<IRoundDataRes>(new ResultFixture()); }
        public Task<IRoundDataRes> UpdateUserRoundListInfoAsync(IUserRoundData info) => throw new NotSupportedException();
        public Task<IRoundDataRes> UpdateRoundRankListInfoAsync(IRoundRankData info) => throw new NotSupportedException();
        public Task<IRoundDataRes> UpdateRoundResultInfoAsync(IRoundResultInfo info) => throw new NotSupportedException();
        public Task<IRoundDataRes> UpdateRoundUserDataAsync(IRoundUserData info) => throw new NotSupportedException();
    }
    sealed class SdkFixture : ILiveOpenSdk
    {
        public readonly RoundFixture Round = new RoundFixture(); public readonly AckFixture Ack = new AckFixture();
        public string Version => "test"; public LogSource LogSource => null; public LiveOpenSdkEnv Env => null;
        public SynchronizationContext DefaultSynchronizationContext { get; set; }
        public void Initialize() => throw new NotSupportedException(); public void Initialize(string id) => throw new NotSupportedException(); public void Uninitialize() { }
        public T GetService<T>() { if (typeof(T) == typeof(IRoundApi)) return (T)(object)Round; if (typeof(T) == typeof(IMessageAckService)) return (T)(object)Ack; throw new NotSupportedException(); }
    }
}
