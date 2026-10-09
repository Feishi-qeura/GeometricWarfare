using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using ByteDance.LiveOpenSdk;
using ByteDance.LiveOpenSdk.Push;
using ByteDance.LiveOpenSdk.Room;
using ByteDance.LiveOpenSdk.Round;
using ByteDance.LiveOpenSdk.Runtime;
using ByteDance.LiveOpenSdk.Utilities;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
using UnityEngine;

namespace GeometricWarfare.DouyinHost
{
    // Public SDK entry only. No reflection, friend assembly impersonation or private API.
    public sealed class DouyinSdkHost : MonoBehaviour
    {
        const string Prefix = "GWSDK/1 ";
        const int MaxLine = 4 * 1024 * 1024;
        const int MaxQueuedBytes = 16 * 1024 * 1024;
        static readonly HashSet<string> Allowed = new HashSet<string> {
            "live_comment", "live_like", "live_gift", "live_team", "live_enter", "live_follow" };
        sealed class InputFrame { public JObject Command; public int Bytes; }
        readonly ConcurrentQueue<InputFrame> commands = new ConcurrentQueue<InputFrame>();
        readonly ConcurrentQueue<InputFrame> acknowledgments = new ConcurrentQueue<InputFrame>();
        readonly object inputLock = new object();
        readonly Dictionary<string, string> pendingAck = new Dictionary<string, string>();
        readonly Dictionary<string, string> submittedAck = new Dictionary<string, string>();
        readonly Queue<string> submittedOrder = new Queue<string>();
        readonly Queue<JObject> startupEvents = new Queue<JObject>();
        long startupEventBytes;
        readonly List<string> started = new List<string>();
        readonly object outputLock = new object();
        readonly Queue<byte[]> outputs = new Queue<byte[]>();
        readonly AutoResetEvent outputReady = new AutoResetEvent(false);
        int inputCount, ackInputCount;
        long inputBytes, outputBytes;
        volatile bool outputStopping;
        IntPtr stdout;
        [DllImport("kernel32.dll", SetLastError = true)] static extern IntPtr GetStdHandle(int handle);
        [DllImport("kernel32.dll", SetLastError = true)] static extern bool WriteFile(IntPtr handle, byte[] bytes, uint count, out uint written, IntPtr overlapped);
        ILiveOpenSdk sdk;
        IMessagePushService push;
        IRoomInfoService rooms;
        string appId = "", roomId = "", anchorId = "";
        LiveBackendClient backend;
        readonly CancellationTokenSource backendStop = new CancellationTokenSource();
        bool backendBusy;
        bool busy, initialized, connected, subscriptionsReady;
        HostDiagnostics diagnostics;
        readonly Dictionary<string, long> eventCounts = new Dictionary<string, long>();
        volatile bool stopping;
        volatile bool inputClosed;
        Task active;
        Task outputTask;

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.BeforeSplashScreen)]
        static void DisableUnityLogBodies() { Debug.unityLogger.logEnabled = false; }

        void Awake()
        {
            diagnostics = new HostDiagnostics(Path.Combine(Environment.CurrentDirectory, "host-diagnostics.jsonl"));
            DontDestroyOnLoad(gameObject);
            Application.runInBackground = true;
            Application.targetFrameRate = 30;
            // Never route SDK log bodies into stdout or a credential-bearing Unity log.
            Debug.unityLogger.logEnabled = false;
            // Unity's Mono redirects Console.Out into its log; write the inherited pipe directly.
            stdout = GetStdHandle(-11);
            outputTask = Task.Run(WriteOutput);
            Observe(outputTask);
            new Thread(ReadInput) { IsBackground = true, Name = "GWSDK input" }.Start();
            Status("", "host", true, 0, "ready");
        }

        void ReadInput()
        {
            try
            {
                using (var reader = new BufferedStream(Console.OpenStandardInput(), 16384))
                {
                    var utf8 = new UTF8Encoding(false, true);
                    using var line = new MemoryStream();
                    while (true)
                    {
                        int value = reader.ReadByte();
                        if (value < 0) break;
                        if (value == '\n')
                        {
                            if (line.Length == 0) continue;
                            try
                            {
                                int length = (int)line.Length;
                                var command = JObject.Parse(utf8.GetString(line.GetBuffer(), 0, length));
                                if (!TryEnqueueInput(command, length))
                                { Status("", "protocol", false, -1002, "failed"); break; }
                            }
                            catch { Status("", "protocol", false, -1001); }
                            line.SetLength(0);
                        }
                        else if (value != '\r')
                        {
                            if (line.Length >= MaxLine) { Status("", "protocol", false, -1002, "failed"); break; }
                            line.WriteByte((byte)value);
                        }
                    }
                }
            }
            catch { if (!stopping) Status("", "protocol", false, -1003); }
            finally { inputClosed = true; }
        }

        bool TryEnqueueInput(JObject command, int bytes)
        {
            bool ack = S(command, "op") == "ack";
            lock (inputLock) {
                if (bytes <= 0 || bytes > MaxLine || inputBytes + bytes > MaxQueuedBytes ||
                    (ack ? ackInputCount >= 8192 : inputCount >= 256)) return false;
                inputBytes += bytes;
                if (ack) { ++ackInputCount; acknowledgments.Enqueue(new InputFrame { Command = command, Bytes = bytes }); }
                else { ++inputCount; commands.Enqueue(new InputFrame { Command = command, Bytes = bytes }); }
                return true;
            }
        }
        bool TryDequeueInput(bool ack, out InputFrame frame)
        {
            lock (inputLock) {
                if (!(ack ? acknowledgments : commands).TryDequeue(out frame)) return false;
                inputBytes -= frame.Bytes;
                if (ack) --ackInputCount; else --inputCount;
                return true;
            }
        }

        void Update()
        {
            if (inputClosed && !stopping) { active = RunSafely(() => Stop("", "stop")); Observe(active); return; }
            // ACK is synchronous and independent of an awaiting SDK command.
            // It must never release the ordinary command's busy flag.
            for (int budget = 0; budget < 128 && !stopping && TryDequeueInput(true, out var ack); ++budget)
                ExecuteAck(ack.Command);
            for (int budget = 0; budget < 128 && !busy && !stopping && TryDequeueInput(false, out var frame); ++budget)
            {
                busy = true;
                active = RunSafely(() => Execute(frame.Command));
                Observe(active);
            }
        }

        async Task RunSafely(Func<Task> operation)
        {
            try { await operation(); }
            catch (Exception error) { diagnostics?.Exception(error); InvalidateSdkSession(); Status("", "host", false, -1006, "failed"); }
            finally { busy = false; }
        }
        static void Observe(Task task)
        {
            _ = task.ContinueWith(failed => { _ = failed.Exception; }, TaskContinuationOptions.OnlyOnFaulted);
        }

        void ExecuteAck(JObject command)
        {
            string id = "";
            try {
                id = S(command, "id");
                if (!initialized || string.IsNullOrEmpty(roomId)) { Status(id, "ack", false, -1005); return; }
                if (S(command, "room_id") != roomId) { Status(id, "ack", false, -1011, "stale_room"); return; }
                string msgId = S(command, "msg_id"), msgType = S(command, "msg_type");
                if (submittedAck.TryGetValue(msgId, out var submitted) && submitted == msgType)
                { Status(id, "ack", true, 0, "submitted"); return; }
                if (!pendingAck.TryGetValue(msgId, out var expected) || expected != msgType) throw new ArgumentException();
                sdk.GetMessageAckService().ReportAck(msgId, msgType);
                pendingAck.Remove(msgId);
                if (submittedOrder.Count >= 8192) submittedAck.Remove(submittedOrder.Dequeue());
                submittedAck[msgId] = msgType;
                submittedOrder.Enqueue(msgId);
                // SDK submission is not remote acceptance.
                Status(id, "ack", true, 0, "submitted");
            }
            catch (Exception error) when (error is ArgumentException || error is FormatException || error is JsonException || error is OverflowException)
            { Status(id, "ack", false, -1001); }
            catch { Status(id, "ack", false, -1006); }
        }

        async Task Execute(JObject command)
        {
            string id = S(command, "id"), op = S(command, "op");
            try
            {
                if (op == "stop") { await Stop(id, op); return; }
                if (op == "init") { await Initialize(command, id); return; }
                if (!initialized || string.IsNullOrEmpty(roomId)) { Status(id, op, false, -1005); return; }
                if (S(command, "room_id") != roomId) { Status(id, op, false, -1011, "stale_room"); return; }
                if (op == "backend_round")
                {
                    if (backend == null) { Status(id, op, false, -1012, "backend_unconfigured"); return; }
                    if (!connected) { Status(id, op, false, -1005); return; }
                    if (backendBusy) { Status(id, op, false, -1016, "backend_busy"); return; }
                    backendBusy = true;
                    // A slow HTTPS receipt must not block ACK delivery or the Unity main thread.
                    Observe(ExecuteBackendRound((JObject)command.DeepClone(), id, appId, roomId, anchorId));
                    return;
                }
                var api = sdk.GetRoundApi();
                long round = N(command, "round_id");
                if (round <= 0) throw new ArgumentException();
                IRoundDataRes result;
                switch (op)
                {
                    case "round":
                        int state = (int)N(command, "status");
                        var groups = (command["group_results"] as JArray ?? new JArray()).Select(x =>
                            (IGroupResultInfo)new GroupResult { GroupId = Required(x, "group_id"), Result = (int)N(x, "result") }).ToList();
                        if ((state != 1 && state != 2) || groups.Any(g => g.Result < 1 || g.Result > 3) || (state == 2 && groups.Count == 0)) throw new ArgumentException();
                        result = await api.UpdateRoundStatusInfoAsync(new RoundStatus { RoundId = round, StartTime = N(command, "start_time"), EndTime = N(command, "end_time"), Status = state, GroupResultList = groups });
                        break;
                    case "user_group":
                        result = await api.UpdateUserGroupInfoAsync(new UserGroup { RoundId = round, OpenId = Required(command, "open_id"), GroupId = Required(command, "group_id") });
                        break;
                    case "user_results":
                        result = await api.UpdateUserRoundListInfoAsync(new UserData { RoundId = round, RoundInfos = Users(command, 50) });
                        break;
                    case "room_rank":
                        var ranking = Users(command, 150);
                        if (!ranking.SequenceEqual(ranking.OrderByDescending(u => u.Score))) throw new ArgumentException();
                        result = await api.UpdateRoundRankListInfoAsync(new RankData { RoundId = round, RankInfos = ranking });
                        break;
                    case "complete":
                        result = await api.UpdateRoundResultInfoAsync(new CompleteData { RoundId = round, CompleteTime = N(command, "complete_time") });
                        break;
                    default: throw new ArgumentException();
                }
                if (result?.ErrCode == 40004) InvalidateSdkSession();
                Status(id, op, result != null && result.ErrCode == 0, result?.ErrCode ?? -1006,
                    result?.ErrCode == 40004 ? "failed" : null);
            }
            catch (Exception error) when (error is ArgumentException || error is FormatException || error is JsonException || error is OverflowException)
            { diagnostics?.Exception(error); if (op == "init") InvalidateSdkSession(); Status(id, op, false, -1001, op == "init" ? "failed" : null); }
            catch (Exception error) { diagnostics?.Exception(error); if (op == "init") InvalidateSdkSession(); Status(id, op, false, -1006, op == "init" ? "failed" : null); }
            finally { busy = false; }
        }

        async Task Initialize(JObject command, string id)
        {
            if (sdk != null) { InvalidateSdkSession(); Status(id, "init", false, -1007, "failed"); return; }
            string token = S(command, "token");
            appId = S(command, "app_id");
            if (string.IsNullOrWhiteSpace(token) || !appId.StartsWith("tt", StringComparison.Ordinal))
            { Status(id, "init", false, -1004, "failed"); return; }
            var types = (command["msg_types"] as JArray ?? new JArray(Allowed.OrderBy(x => x))).Select(x => (string)x).Distinct().ToArray();
            if (types.Any(t => !Allowed.Contains(t))) throw new ArgumentException();
            if (types.Length == 0) throw new ArgumentException();
            Status(id, "init", true, 0, "initializing");
            string backendUrl = S(command, "backend_url");
            if (!string.IsNullOrWhiteSpace(backendUrl)) backend = new LiveBackendClient(backendUrl, (bool?)command["allow_insecure_backend"] == true);
            Status(id, "init", true, 0, "backend_configured");
            sdk = LiveOpenSdk.Instance;
            Status(id, "init", true, 0, "sdk_available");
            sdk.DefaultSynchronizationContext = SynchronizationContext.Current;
            sdk.Env.Token = token;
            Status(id, "init", true, 0, "token_loaded");
            token = null;
            command.Remove("token");
            sdk.Initialize(appId);
            Status(id, "init", true, 0, "sdk_initialized");
            rooms = sdk.GetRoomInfoService();
            rooms.OnRoomInfoChanged += OnRoom;
            Status(id, "init", true, 0, "waiting_room");
            var wait = rooms.WaitForRoomInfoAsync();
            Observe(wait);
            if (await Task.WhenAny(wait, Task.Delay(30000)) != wait)
            { InvalidateSdkSession(); Status(id, "init", false, -1008, "failed"); return; }
            var room = await wait;
            if (stopping) return;
            if (room == null || string.IsNullOrWhiteSpace(room.RoomId) || room.Anchor == null || string.IsNullOrWhiteSpace(room.Anchor.OpenId))
            { InvalidateSdkSession(); Status(id, "init", false, -1009, "failed"); return; }
            initialized = true;
            OnRoom(room);
            Status(id, "init", true, 0, "room_authenticated");
            push = sdk.GetMessagePushService();
            push.OnMessage += OnMessage;
            push.OnConnectionStateChanged += OnConnection;
            OnConnection(push.ConnectionState);
            if (backend != null) Observe(AuthenticateBackend(appId, roomId, anchorId));
            await StartSubscriptions(types, id);
        }

        async Task StartSubscriptions(string[] types, string id)
        {
            subscriptionsReady = false;
            foreach (string type in types)
            {
                if (stopping) break;
                try
                {
                    await push.StartPushTaskAsync(type, MultiPushType.SinglePush);
                    started.Add(type);
                    Status(id, "subscribe:" + type, true, 0);
                }
                catch (Exception error) { diagnostics?.Exception(error); InvalidateSdkSession(); Status(id, "subscribe:" + type, false, -1006, "failed"); return; }
            }
            if (stopping || !initialized) return;
            subscriptionsReady = true;
            Status(id, "init", true, 0, "all_subscribed");
            // The game submits round-start as soon as it sees a room. Public SDK
            // requires every desired push task to succeed before that happens.
            if (connected) OnRoom(rooms.RoomInfo);
        }

        void OnRoom(IRoomInfo room)
        {
            if (stopping || room == null || string.IsNullOrWhiteSpace(room.RoomId) || room.Anchor == null) return;
            if (roomId != room.RoomId || anchorId != room.Anchor.OpenId) { pendingAck.Clear(); submittedAck.Clear(); submittedOrder.Clear(); ClearStartupEvents(); backend?.ResetSession(); }
            roomId = room.RoomId;
            anchorId = room.Anchor.OpenId;
            if (initialized && connected && subscriptionsReady) {
                diagnostics?.Status("room", true, 0, "ready");
                Emit(new { kind = "room", app_id = appId, room_id = roomId, anchor = User(room.Anchor) });
                // Subscription callbacks may arrive before all push tasks finish.
                // Publish the authenticated room first so UE can bind those events.
                while (startupEvents.Count > 0) Emit(startupEvents.Dequeue());
                startupEventBytes = 0;
            }
        }
        void OnConnection(ConnectionState state)
        {
            connected = state == ConnectionState.Connected;
            if (!connected) ClearStartupEvents();
            Status("", "connection", connected, 0, connected ? "connected" : "disconnected");
            if (connected) OnRoom(rooms.RoomInfo);
        }
        void InvalidateSdkSession()
        {
            initialized = connected = subscriptionsReady = false;
            roomId = "";
            anchorId = "";
            backend?.ResetSession();
            pendingAck.Clear();
            ClearStartupEvents();
            submittedAck.Clear(); submittedOrder.Clear();
            if (push != null) { push.OnMessage -= OnMessage; push.OnConnectionStateChanged -= OnConnection; }
            if (rooms != null) rooms.OnRoomInfoChanged -= OnRoom;
        }

        void OnMessage(IPushMessage message)
        {
            if (!initialized || !connected || stopping || string.IsNullOrEmpty(roomId) || message == null || !Allowed.Contains(message.MsgType)) return;
            if (message is ITeamMessage team && (team.AppId != appId || team.RoomId.ToString() != roomId)) return;
            var data = EncodeMessage(message, roomId);
            if (data == null || string.IsNullOrWhiteSpace(message.MsgId)) return;
            if (submittedAck.TryGetValue(message.MsgId, out var submitted) && submitted == message.MsgType) return;
            if (pendingAck.Count >= 8192) { InvalidateSdkSession(); Status("", "events", false, -1010, "failed"); return; }
            pendingAck[message.MsgId] = message.MsgType;
            eventCounts.TryGetValue(message.MsgType, out long count);
            eventCounts[message.MsgType] = ++count;
            // Aggregate type/count only: never persist viewer IDs or comment text.
            if (count == 1 || count % 100 == 0) diagnostics?.Event(message.MsgType, count);
            if (!subscriptionsReady) {
                int bytes = Encoding.UTF8.GetByteCount(data.ToString(Formatting.None));
                if (bytes > MaxLine || startupEvents.Count >= 1024 || startupEventBytes + bytes > MaxQueuedBytes) {
                    InvalidateSdkSession();Status("", "events", false, -1010, "failed");return;
                }
                startupEvents.Enqueue(data);startupEventBytes += bytes;
            } else Emit(data);
        }

        void ClearStartupEvents() {startupEvents.Clear();startupEventBytes = 0;}

        // Pure projection of public SDK interfaces. Editor contract checks use
        // fixtures here; the player has no command for injecting fixture events.
        public static JObject EncodeMessage(IPushMessage message, string roomId)
        {
            var data = new JObject { ["kind"] = "event", ["room_id"] = roomId, ["msg_id"] = message.MsgId, ["msg_type"] = message.MsgType, ["timestamp"] = message.Timestamp };
            switch (message)
            {
                case ICommentMessage comment: data["user"] = JObject.FromObject(User(comment.Sender)); data["content"] = comment.Content; break;
                case ILikeMessage like: data["user"] = JObject.FromObject(User(like.Sender)); data["count"] = like.LikeCount.ToString(CultureInfo.InvariantCulture); break;
                case IGiftMessage gift:
                    data["user"] = JObject.FromObject(User(gift.Sender)); data["gift_id"] = gift.SecGiftId; data["magic_gift_id"] = gift.SecMagicGiftId;
                    data["count"] = gift.GiftCount.ToString(CultureInfo.InvariantCulture); data["gift_value"] = gift.GiftValue; data["is_test"] = gift.IsTestData; data["audience_open_id"] = gift.AudienceSecOpenId; break;
                case ITeamMessage selected: data["user"] = JObject.FromObject(User(selected.Sender)); data["group_id"] = selected.GroupId; break;
                case IFollowMessage follow:
                    data["user"] = JObject.FromObject(User(follow.Sender)); data["follow_action"] = (int)follow.UserFollowAction; data["follow_target"] = follow.FollowUser?.OpenId ?? ""; break;
                case IEnterRoomMessage presence:
                    data["user"] = JObject.FromObject(new { open_id = presence.SecOpenId, nickname = presence.NickName, avatar_url = presence.AvatarUrl });
                    data["enter_type"] = presence.EnterRoomType; data["enter_scene"] = presence.EnterRoomScene; data["follow_status"] = presence.FollowStatus;
                    data["is_old_player"] = presence.IsOldPlayer; data["grade_level"] = presence.GradeLevel;
                    data["inviter_id"] = presence.InviterGatherOpenid; break;
                default: return null;
            }
            return data;
        }

        async Task Stop(string id, string op)
        {
            if (stopping) return;
            stopping = true;
            foreach (var pair in eventCounts) diagnostics?.Event(pair.Key, pair.Value);
            initialized = false;
            ClearStartupEvents();
            backendStop.Cancel();
            backend?.Dispose();
            if (push != null)
            {
                push.OnMessage -= OnMessage;
                push.OnConnectionStateChanged -= OnConnection;
                var shutdownTasks = started.Select(async type => { try { await push.StopPushTaskAsync(type); } catch { } }).ToArray();
                var shutdown = Task.WhenAll(shutdownTasks);
                Observe(shutdown);
                await Task.WhenAny(shutdown, Task.Delay(750));
            }
            if (rooms != null) rooms.OnRoomInfoChanged -= OnRoom;
            try { sdk?.Uninitialize(); if (sdk != null) sdk.Env.Token = null; } catch { }
            Status(id, op, true, 0, "stopped");
            outputStopping = true;
            outputReady.Set();
            await Task.WhenAny(outputTask, Task.Delay(750));
            Application.Quit();
        }
        void OnApplicationQuit() { backendStop.Cancel(); backend?.Dispose(); try { sdk?.Uninitialize(); if (sdk != null) sdk.Env.Token = null; } catch { } }
        void Emit(object frame)
        {
            try
            {
                byte[] bytes = Encoding.UTF8.GetBytes(Prefix + JsonConvert.SerializeObject(frame, Formatting.None) + "\n");
                if (bytes.Length > MaxLine) { Status("", "protocol", false, -1002, "failed"); inputClosed = true; return; }
                lock (outputLock)
                {
                    if (outputStopping) return;
                    if (outputs.Count >= 1024 || outputBytes + bytes.Length > MaxQueuedBytes) { inputClosed = true; return; }
                    outputs.Enqueue(bytes);
                    outputBytes += bytes.Length;
                }
                outputReady.Set();
            }
            catch { inputClosed = true; }
        }
        void WriteOutput()
        {
            try
            {
                while (true)
                {
                    byte[] bytes = null;
                    lock (outputLock)
                    {
                        if (outputs.Count > 0) { bytes = outputs.Dequeue(); outputBytes -= bytes.Length; }
                        else if (outputStopping) return;
                    }
                    if (bytes == null) { outputReady.WaitOne(100); continue; }
                    if (!WriteFile(stdout, bytes, (uint)bytes.Length, out uint written, IntPtr.Zero) || written != bytes.Length)
                    { inputClosed = true; return; }
                }
            }
            catch { inputClosed = true; }
        }
        async Task AuthenticateBackend(string app, string room, string anchor)
        {
            try {
                var data = await backend.AuthenticateAsync(app, room, anchor, sdk.Env.Token, backendStop.Token);
                if (!stopping && initialized && appId == app && roomId == room && anchorId == anchor)
                    Status("", "backend_auth", true, 0, "backend_authenticated", data);
            }
            catch (BackendFailure error) { if (!stopping) Status("", "backend_auth", false, error.Code, "backend_pending"); }
            catch { if (!stopping) Status("", "backend_auth", false, -1013, "backend_pending"); }
        }
        async Task ExecuteBackendRound(JObject command, string id, string app, string room, string anchor)
        {
            try {
                var data = await backend.SubmitRoundAsync(app, room, anchor, sdk.Env.Token, command, backendStop.Token);
                if (stopping) return;
                if (!initialized || !connected || appId != app || roomId != room || anchorId != anchor) { Status(id, "backend_round", false, -1011, "stale_room"); return; }
                Status(id, "backend_round", true, 0, "backend_accepted", data);
            }
            catch (BackendFailure error) { if (!stopping) Status(id, "backend_round", false, error.Code, error.Code == -1015 ? "backend_reauthorization_required" : "backend_pending"); }
            catch { if (!stopping) Status(id, "backend_round", false, -1013, "backend_pending"); }
            finally { backendBusy = false; }
        }
        void Status(string id, string op, bool success, long code, string state = null, JObject data = null)
        {
            if (op != "ack" || !success) diagnostics?.Status(op, success, code, state);
            Emit(new { kind = "status", request_id = id, op, success, err_code = code, state, data });
        }
        static object User(IUserInfo user) => new { open_id = user?.OpenId ?? "", nickname = user?.Nickname ?? "", avatar_url = user?.AvatarUrl ?? "" };
        static string S(JToken token, string name) => (string)token[name] ?? "";
        static string Required(JToken token, string name) { string value = S(token, name); if (string.IsNullOrWhiteSpace(value)) throw new ArgumentException(); return value; }
        static long N(JToken token, string name) => (long?)token[name] ?? 0;
        static List<IUserRoundInfo> Users(JObject command, int max)
        {
            var array = command["users"] as JArray;
            if (array == null || array.Count > max) throw new ArgumentException();
            return array.Select(x => (IUserRoundInfo)new UserRound {
                OpenId = Required(x, "open_id"), Rank = N(x, "rank"), RoundResult = (int)N(x, "result"), Score = N(x, "score"),
                WinPoints = N(x, "win_points"), WinStreakCount = (int)N(x, "win_streak"), GroupId = Required(x, "group_id") }).ToList();
        }
        sealed class GroupResult : IGroupResultInfo { public string GroupId { get; set; } public int Result { get; set; } }
        sealed class RoundStatus : IRoundStatusInfo { public long RoundId { get; set; } public long StartTime { get; set; } public long EndTime { get; set; } public int Status { get; set; } public List<IGroupResultInfo> GroupResultList { get; set; } }
        sealed class UserGroup : IRoundUserGroupInfo { public string GroupId { get; set; } public string OpenId { get; set; } public long RoundId { get; set; } }
        sealed class UserRound : IUserRoundInfo { public string OpenId { get; set; } public long Rank { get; set; } public int RoundResult { get; set; } public long Score { get; set; } public long WinPoints { get; set; } public int WinStreakCount { get; set; } public string GroupId { get; set; } }
        sealed class UserData : IUserRoundData { public long RoundId { get; set; } public List<IUserRoundInfo> RoundInfos { get; set; } }
        sealed class RankData : IRoundRankData { public long RoundId { get; set; } public List<IUserRoundInfo> RankInfos { get; set; } }
        sealed class CompleteData : IRoundResultInfo { public long RoundId { get; set; } public long CompleteTime { get; set; } }
    }
}
