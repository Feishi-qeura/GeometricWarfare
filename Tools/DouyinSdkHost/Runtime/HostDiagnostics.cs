using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

namespace GeometricWarfare.DouyinHost
{
    // Deliberate allowlist, independent of Unity/SDK logging. No payloads,
    // credentials, identities, URLs, exception messages or stack traces.
    public sealed class HostDiagnostics
    {
        public const int MaxBytes = 1024 * 1024;
        static readonly HashSet<string> Types = new HashSet<string> { "live_comment", "live_like", "live_gift", "live_team", "live_enter", "live_follow" };
        static readonly HashSet<string> Ops = new HashSet<string> { "host", "protocol", "init", "connection", "room", "events", "ack", "round", "user_group", "user_results", "room_rank", "complete", "backend_auth", "backend_round", "stop" };
        static readonly HashSet<string> States = new HashSet<string> { "ready", "failed", "stopped", "initializing", "backend_configured", "sdk_available", "token_loaded", "sdk_initialized", "waiting_room", "room_authenticated", "all_subscribed", "connected", "disconnected", "submitted", "stale_room", "backend_unconfigured", "backend_busy", "backend_authenticated", "backend_pending", "backend_accepted", "backend_reauthorization_required" };
        readonly string path; readonly object gate = new object();
        public HostDiagnostics(string path) { this.path = path; }
        public static JObject ProjectStatus(string op, bool success, long code, string state)
        {
            bool subscription = op != null && op.StartsWith("subscribe:", StringComparison.Ordinal) && Types.Contains(op.Substring(10));
            return new JObject { ["kind"] = "status", ["op"] = Ops.Contains(op ?? "") || subscription ? op : "unknown", ["success"] = success, ["err_code"] = code, ["state"] = state == null ? "" : States.Contains(state) ? state : "unknown" };
        }
        public void Status(string op, bool success, long code, string state) => Write(ProjectStatus(op, success, code, state));
        public void Event(string type, long count) { if (Types.Contains(type) && count >= 0) Write(new JObject { ["kind"] = "event_count", ["type"] = type, ["count"] = count }); }
        public void Exception(Exception error)
        {
            string kind = error is OperationCanceledException ? "cancelled" : error is TimeoutException ? "timeout" : error is ArgumentException ? "argument" : error is InvalidOperationException ? "invalid_operation" : error is NotSupportedException ? "unsupported" : error is IOException ? "io" : "sdk_or_transport";
            Write(new JObject { ["kind"] = "exception_category", ["category"] = kind });
        }
        void Write(JObject record)
        {
            try {
                record["utc"] = DateTime.UtcNow.ToString("O");
                var bytes = new UTF8Encoding(false).GetBytes(record.ToString(Formatting.None) + "\n");
                lock (gate) { using var file = new FileStream(path, FileMode.Append, FileAccess.Write, FileShare.Read); if (file.Length + bytes.Length <= MaxBytes) file.Write(bytes, 0, bytes.Length); }
            }
            catch { /* Diagnostics must never interrupt gameplay or authentication. */ }
        }
    }
}
